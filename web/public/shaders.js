/*
    The outrun stroke pipeline, ported to WebGL2 (GLSL ES 3.00).

    This is a PORT, not a mirror the tests rely on: the plugin's GLSL 4.10 in
    source/Shaders.cpp stays the single home of the algorithm, and this file
    follows it. The differences are mechanical -- the ES version/precision
    header, and the palette table arriving as an 8-bit PNG (the same bake the
    plugin uploads, written by `outruntest --palettes-image`) instead of a
    float texture.

    Keep the maths in step with Shaders.cpp when it changes.
*/

export const VERTEX = `#version 300 es
layout( location = 0 ) in vec2 vPosition;
out vec2 uv;
void main()
{
	gl_Position = vec4( vPosition, 0.0, 1.0 );
	uv = vPosition * 0.5 + 0.5;
}
`;

const HEADER = `#version 300 es
precision highp float;
precision highp int;
`;

// --------------------------------------------------------------------------
// Pass 1: copy the source (demo clip or webcam) into our own texture.
// --------------------------------------------------------------------------
export const COPY = HEADER + `
uniform sampler2D InputTexture;
in vec2 uv;
out vec4 fragColor;
void main()
{
	fragColor = texture( InputTexture, uv );
}
`;

// --------------------------------------------------------------------------
// Pass 2: edge. Sobel at a mip scale, as in the plugin.
// --------------------------------------------------------------------------
export const EDGE = HEADER + `
uniform sampler2D CopyTexture;
uniform vec2 TexelSize;
uniform float Detail;
uniform float SourceMode;

in vec2 uv;
out vec4 fragColor;

float channel( vec2 at )
{
	vec4 c = textureLod( CopyTexture, at, Detail );
	int mode = int( SourceMode + 0.5 );
	if( mode == 1 )
		return c.a;
	if( mode == 2 )
	{
		float y = dot( c.rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
		return length( c.rgb - vec3( y ) ) + y * 0.25;
	}
	vec3 straight = c.a > 0.0031 ? c.rgb / c.a : c.rgb;
	float luma = dot( straight, vec3( 0.2126, 0.7152, 0.0722 ) );
	if( mode == 3 )
		return c.a * ( 0.35 + 0.65 * luma );
	return luma;
}

void main()
{
	vec2 tap = TexelSize * exp2( Detail );
	float tl = channel( uv + vec2( -tap.x,  tap.y ) );
	float tc = channel( uv + vec2(    0.0,  tap.y ) );
	float tr = channel( uv + vec2(  tap.x,  tap.y ) );
	float ml = channel( uv + vec2( -tap.x,    0.0 ) );
	float mr = channel( uv + vec2(  tap.x,    0.0 ) );
	float bl = channel( uv + vec2( -tap.x, -tap.y ) );
	float bc = channel( uv + vec2(    0.0, -tap.y ) );
	float br = channel( uv + vec2(  tap.x, -tap.y ) );
	float gx = ( tr + 2.0 * mr + br ) - ( tl + 2.0 * ml + bl );
	float gy = ( tl + 2.0 * tc + tr ) - ( bl + 2.0 * bc + br );
	fragColor = vec4( length( vec2( gx, gy ) ) * 0.25, 0.0, 0.0, 1.0 );
}
`;

// --------------------------------------------------------------------------
// Pass 3: stabilise. Asymmetric IIR + threshold + centroid moments.
// --------------------------------------------------------------------------
export const STABILISE = HEADER + `
uniform sampler2D EdgeTexture;
uniform sampler2D HistoryTexture;
uniform float Attack;
uniform float Release;
uniform float Sensitivity;
uniform float Softness;
uniform float Reset;

in vec2 uv;
out vec4 fragColor;

void main()
{
	float current = texture( EdgeTexture, uv ).r;
	float history = texture( HistoryTexture, uv ).r;
	float blend = current > history ? Attack : Release;
	float stable = mix( history, current, blend ) * ( 1.0 - Reset ) + current * Reset;
	float lower = Sensitivity * ( 1.0 - Softness );
	float upper = Sensitivity * ( 1.0 + Softness );
	float mask = smoothstep( lower, max( upper, lower + 1e-5 ), stable );
	fragColor = vec4( stable, uv.x * mask, uv.y * mask, mask );
}
`;

// --------------------------------------------------------------------------
// Pass 4: stroke. Both engines, one program -- ported from Shaders.cpp.
// --------------------------------------------------------------------------
export const STROKE = HEADER + `
uniform sampler2D PaletteTexture; //the plugin's own bake, 256 x (16*12) PNG
uniform sampler2D StableTexture;
uniform sampler2D CopyTexture;

uniform float Engine;
uniform float CentroidLod;
uniform float Trace;

uniform float Aspect;
uniform vec2 PictureSize;
uniform float WidthPx;
uniform float Core;
uniform float TraceAngle;

uniform float PathIndex;
uniform float PathScale;
uniform float PathDetail;
uniform float Horizon;

uniform float BreakMode;
uniform float BreakAmount;
uniform float BreakSpread;
uniform float BreakHue;

uniform float ColourMode;
uniform float PaletteIndex;
uniform vec3 Colour1;
uniform vec3 Colour2;
uniform float Spread;
uniform float Saturation;
uniform float Brightness;

uniform float Phase;

uniform float Audio[ 64 ];
uniform float AudioLevel;

uniform vec2 Curve[ 49 ];

in vec2 uv;
out vec4 fragColor;

const int kPaletteSize = 256;
const float kTau = 6.283185307179586;
const uint kOdd = 2654435761u;

uint HashInt( uint x )
{
	x = x * 747796405u + 2891336453u;
	uint w = ( ( x >> ( ( x >> 28u ) + 4u ) ) ^ x ) * 277803737u;
	return ( w >> 22u ) ^ w;
}

float Hash01( uint x )
{
	return float( HashInt( x ) ) * ( 1.0 / 4294967296.0 );
}

float tri( float x )
{
	return 0.5 - abs( fract( x ) - 0.5 );
}

float sdSeg( vec2 p, vec2 a, vec2 b )
{
	vec2 pa = p - a;
	vec2 ba = b - a;
	float h = clamp( dot( pa, ba ) / max( dot( ba, ba ), 1e-9 ), 0.0, 1.0 );
	return length( pa - ba * h );
}

vec3 paletteColour( float position )
{
	position = fract( position );
	int index = int( PaletteIndex + 0.5 );
	if( index == 0 )
		return Colour1;
	if( index == 1 )
		return mix( Colour1, Colour2, position );

	//The table PNG holds 12 rows per palette; sample a row centre. The two
	//fetches + mix follow the plugin's paletteColour.
	float p = position * float( kPaletteSize - 1 );
	float i0 = floor( p );
	int a = int( i0 );
	int b = min( a + 1, kPaletteSize - 1 );
	int row = index * 12 + 6;
	vec3 ca = texelFetch( PaletteTexture, ivec2( a, row ), 0 ).rgb;
	vec3 cb = texelFetch( PaletteTexture, ivec2( b, row ), 0 ).rgb;
	return mix( ca, cb, p - i0 );
}

vec2 traceCentroid()
{
	vec4 moments = textureLod( StableTexture, vec2( 0.5 ), CentroidLod );
	return moments.a > 1e-4 ? moments.gb / moments.a : vec2( 0.5 );
}

float traceCoordinate( vec2 at, vec2 centre )
{
	int wiring = int( Trace + 0.5 );
	vec2 p = ( at - centre ) * vec2( Aspect, 1.0 );
	float ang = atan( p.y, p.x ) / kTau + 0.5;
	float radius = length( p );
	if( wiring == 0 )
		return ang + radius * 1.5;
	if( wiring == 2 )
	{
		float a = TraceAngle * kTau;
		return dot( p, vec2( cos( a ), sin( a ) ) ) * 0.5 + 0.5;
	}
	if( wiring == 3 )
		return radius;
	return ang;
}

vec2 maskField( vec2 at, vec2 centre )
{
	float lod = clamp( log2( max( WidthPx / 3.0, 1.0 ) ), 0.0, 3.5 );
	float m = textureLod( StableTexture, at, lod ).a;
	m = clamp( m * ( 1.0 + lod * 2.0 ), 0.0, 1.0 );
	return vec2( m, traceCoordinate( at, centre ) );
}

vec2 pathGrid( vec2 at )
{
	float d = 1e6;
	float t = 0.0;
	float below = Horizon - at.y;
	if( below > 1e-3 )
	{
		vec2 g = vec2( ( at.x - 0.5 ) * Aspect / below, 1.0 / below ) * ( PathDetail * 0.35 );
		g.y += Phase;
		d = min( tri( g.x ), tri( g.y ) );
		float cellsPerPixel = ( PathDetail * 0.35 ) / max( below * below, 1e-6 ) / PictureSize.y;
		d = mix( d, 0.0, clamp( ( cellsPerPixel - 0.3 ) / 0.3, 0.0, 1.0 ) );
		t = clamp( below * 2.0, 0.0, 1.0 );
	}
	float dh = abs( at.y - Horizon ) * 3.0;
	if( dh < d ) { d = dh; t = 0.0; }
	float sunR = 0.04 + 0.16 * PathScale;
	vec2 ps = ( at - vec2( 0.5, Horizon + sunR * 1.05 ) ) * vec2( Aspect, 1.0 );
	float sunIn = length( ps ) - sunR;
	float dSun = abs( sunIn ) * 3.0;
	if( dSun < d ) { d = dSun; t = 0.85; }
	float stripes = tri( ps.y * ( 9.0 / max( sunR, 1e-3 ) ) - Phase * 2.0 );
	float blind = max( stripes, max( sunIn * 20.0, ps.y * 20.0 ) );
	if( blind < d ) { d = blind; t = 0.85; }
	return vec2( d, t );
}

vec2 pathLissajous( vec2 at )
{
	float d = 1e6;
	float t = 0.0;
	for( int k = 0; k < 48; ++k )
	{
		vec2 pa = at - Curve[ k ];
		vec2 ba = Curve[ k + 1 ] - Curve[ k ];
		float h = clamp( dot( pa, ba ) / max( dot( ba, ba ), 1e-9 ), 0.0, 1.0 );
		float dk = length( pa - ba * h );
		if( dk < d )
		{
			d = dk;
			t = ( float( k ) + h ) / 48.0;
		}
	}
	return vec2( d, t );
}

vec2 pathHex( vec2 at )
{
	vec2 p = ( at - 0.5 ) * vec2( Aspect, 1.0 ) * ( PathDetail * 1.2 + 1.0 );
	p += vec2( Phase * 0.5, Phase * 0.29 );
	vec2 s = vec2( 1.0, 1.7320508 );
	vec2 p1 = mod( p, s ) - s * 0.5;
	vec2 p2 = mod( p + s * 0.5, s ) - s * 0.5;
	vec2 h = dot( p1, p1 ) < dot( p2, p2 ) ? p1 : p2;
	vec2 q = abs( h );
	float dc = max( q.x, dot( q, vec2( 0.5, 0.8660254 ) ) );
	float d = abs( dc - 0.5 );
	vec2 cellCentre = p - h;
	float t = fract( cellCentre.x * 0.27 + cellCentre.y * 0.17 );
	return vec2( d, t );
}

vec2 pathCircuit( vec2 at )
{
	vec2 p = ( at - 0.5 ) * vec2( Aspect, 1.0 ) * ( PathDetail * 1.5 + 2.0 ) + vec2( Phase * 0.4, 0.0 );
	vec2 cell = floor( p );
	vec2 f = p - cell - 0.5;
	uint id = HashInt( uint( int( cell.x ) + 4096 ) * kOdd ^ uint( int( cell.y ) + 4096 ) * 0x9E3779B9u );
	vec2 o = f * vec2( ( id & 1u ) == 0u ? 1.0 : -1.0, ( id & 2u ) == 0u ? 1.0 : -1.0 );
	uint m = ( id >> 2u ) % 5u;
	float d;
	if( m == 0u )
		d = abs( f.y );
	else if( m == 1u )
		d = abs( f.x );
	else if( m == 2u )
		d = min( sdSeg( o, vec2( -0.5, 0.0 ), vec2( 0.0, 0.0 ) ),
		         sdSeg( o, vec2( 0.0, 0.0 ), vec2( 0.0, 0.5 ) ) );
	else if( m == 3u )
		d = min( abs( f.y ), sdSeg( o, vec2( 0.0, 0.0 ), vec2( 0.0, 0.5 ) ) );
	else
		d = min( abs( f.y ), abs( f.x ) );
	if( ( id & 96u ) == 0u )
		d = min( d, abs( length( f ) - 0.14 ) );
	float t = Hash01( id ) * 0.6 + ( f.x + f.y ) * 0.2;
	return vec2( d, t );
}

float valueNoise( float x )
{
	float i = floor( x );
	float f = x - i;
	float a = Hash01( uint( int( i ) + 8192 ) );
	float b = Hash01( uint( int( i ) + 8193 ) );
	return mix( a, b, f * f * ( 3.0 - 2.0 * f ) );
}

vec2 pathSkyline( vec2 at )
{
	float n = max( PathDetail, 2.0 );
	float xm = at.x * Aspect * n * 0.35 + Phase * 0.15;
	float ridge = abs( valueNoise( xm ) * 2.0 - 1.0 ) * 0.5
	            + abs( valueNoise( xm * 2.3 + 37.0 ) * 2.0 - 1.0 ) * 0.15;
	float mtnY = Horizon + ridge * 0.45 * PathScale;
	float d = abs( at.y - mtnY );
	float t = 0.75;
	float xr = at.x * Aspect * n + Phase * 0.5;
	float c = floor( xr );
	float hgt = Hash01( uint( int( c ) + 4096 ) );
	float roofY = Horizon + ( 0.05 + hgt * 0.35 ) * PathScale;
	float dR = abs( at.y - roofY );
	if( dR < d )
	{
		d = dR;
		t = 0.25 + hgt * 0.3;
	}
	return vec2( d, t );
}

vec2 pathRings( vec2 at )
{
	vec2 p = ( at - 0.5 ) * vec2( Aspect, 1.0 ) / max( PathScale, 0.05 );
	float r = length( p );
	float w = log( max( r, 1e-3 ) ) * ( PathDetail * 0.5 + 1.0 ) - Phase;
	float d = tri( w );
	float ringsPerPixel = ( PathDetail * 0.5 + 1.0 ) / max( r, 1e-3 ) / ( PictureSize.y * max( PathScale, 0.05 ) );
	d = mix( d, 0.0, clamp( ( ringsPerPixel - 0.3 ) / 0.3, 0.0, 1.0 ) );
	float ang = atan( p.y, p.x ) / kTau + 0.5;
	float spokes = max( tri( ang * ( floor( PathDetail * 0.5 ) + 5.0 ) ), ( 0.12 - r ) * 8.0 );
	if( spokes < d )
		d = spokes;
	return vec2( d, ang );
}

float sdStar5( vec2 p, float r, float rf )
{
	vec2 k1 = vec2( 0.809016994375, -0.587785252292 );
	vec2 k2 = vec2( -k1.x, k1.y );
	p.x = abs( p.x );
	p -= 2.0 * max( dot( k1, p ), 0.0 ) * k1;
	p -= 2.0 * max( dot( k2, p ), 0.0 ) * k2;
	p.x = abs( p.x );
	p.y -= r;
	vec2 ba = rf * vec2( -k1.y, k1.x ) - vec2( 0.0, 1.0 );
	float h = clamp( dot( p, ba ) / dot( ba, ba ), 0.0, r );
	return length( p - ba * h ) * sign( p.y * ba.x - p.x * ba.y );
}

vec2 pathStar( vec2 at )
{
	vec2 p = ( at - 0.5 ) * vec2( Aspect, 1.0 ) / max( PathScale * 0.6, 0.05 );
	float a = Phase * kTau * 0.25;
	p = mat2( cos( a ), -sin( a ), sin( a ), cos( a ) ) * p;
	float d = abs( sdStar5( p, 0.7, 0.45 ) );
	float t = atan( p.y, p.x ) / kTau + 0.5;
	return vec2( d, t );
}

vec2 pathWaveform( vec2 at )
{
	float fi = clamp( at.x, 0.0, 1.0 ) * 63.0;
	int i = int( fi );
	float h = mix( Audio[ i ], Audio[ min( i + 1, 63 ) ], fi - float( i ) );
	float d = abs( abs( at.y - 0.5 ) - h * PathScale * 0.35 );
	return vec2( d, at.x );
}

vec2 pathDistance( vec2 at )
{
	int path = int( PathIndex + 0.5 );
	if( path == 1 ) return pathLissajous( at );
	if( path == 2 ) return pathHex( at );
	if( path == 3 ) return pathCircuit( at );
	if( path == 4 ) return pathSkyline( at );
	if( path == 5 ) return pathRings( at );
	if( path == 6 ) return pathStar( at );
	if( path == 7 ) return pathWaveform( at );
	return pathGrid( at );
}

vec2 pathField( vec2 at )
{
	vec2 dt = pathDistance( at );
	float g = max( length( vec2( dFdx( dt.x ), dFdy( dt.x ) ) ), 1e-6 );
	float dPx = abs( dt.x ) / g;
	float m = 1.0 - smoothstep( WidthPx * 0.35, max( WidthPx, 1.5 ), dPx );
	return vec2( m, dt.y );
}

vec2 strokeCentre()
{
	return int( Engine + 0.5 ) == 0 ? traceCentroid() : vec2( 0.5 );
}

vec2 strokeField( vec2 at, vec2 centre )
{
	if( int( Engine + 0.5 ) == 0 )
		return maskField( at, centre );
	return pathField( at );
}

vec3 strokeColour( float s )
{
	float pos = s * Spread - Phase * 0.25;
	vec3 pal = paletteColour( pos );
	int cm = int( ColourMode + 0.5 );
	if( cm >= 1 )
	{
		vec4 clip = texture( CopyTexture, uv );
		vec3 straight = clip.a > 0.0031 ? clip.rgb / clip.a : clip.rgb;
		return cm == 1 ? straight : straight * pal;
	}
	return pal;
}

void main()
{
	vec2 centre = strokeCentre();
	int mode = int( BreakMode + 0.5 );
	float amount = BreakAmount;

	vec2 field;

	if( mode == 1 && amount > 0.001 )
	{
		field = strokeField( uv, centre );
		for( int k = 1; k <= 4; ++k )
		{
			float breathe = 0.7 + 0.3 * sin( kTau * ( Phase * 0.25 + float( k ) * 0.13 ) );
			float shrink = 1.0 + float( k ) * amount * ( 0.4 + 1.2 * BreakSpread ) * breathe;
			vec2 fk = strokeField( centre + ( uv - centre ) / shrink, centre );
			fk.x *= pow( 0.65, float( k ) );
			fk.y += float( k ) * BreakHue;
			if( fk.x > field.x )
				field = fk;
		}
	}
	else if( mode == 2 && amount > 0.001 )
	{
		vec2 p = ( uv - centre ) * vec2( Aspect, 1.0 );
		float lattice = kTau / 8.0;
		float base = BreakSpread * lattice;
		float ang = atan( p.y, p.x );
		float snapped = floor( ( ang - base ) / lattice + 0.5 ) * lattice + base;
		float bent = mix( ang, snapped, amount );
		vec2 p2 = length( p ) * vec2( cos( bent ), sin( bent ) );
		field = strokeField( centre + p2 / vec2( Aspect, 1.0 ), centre );
	}
	else if( mode == 3 && amount > 0.001 )
	{
		float rowH = mix( 0.006, 0.06, BreakSpread );
		float row = floor( uv.y / rowH );
		uint slot = uint( int( floor( Phase * 8.0 ) ) + 65536 );
		float r = Hash01( uint( int( row ) + 4096 ) * kOdd ^ slot * 0x9E3779B9u ) * 2.0 - 1.0;
		field = strokeField( uv + vec2( r * r * r * amount * 0.3, 0.0 ), centre );
		field.y += r * BreakHue;
	}
	else if( mode == 4 && amount > 0.001 )
	{
		float f = 1.0 / ( BreakSpread * 0.4 + 0.05 );
		float ph = Phase * kTau * 0.25;
		float w = amount * 0.08;
		vec2 bent = uv + w * vec2(
			sin( f * uv.y * 1.1 + ph * 2.0 ) + 0.5 * sin( f * uv.x * 2.3 - ph * 3.1 ),
			cos( f * uv.x * 1.3 + ph * 1.7 ) + 0.5 * cos( f * uv.y * 1.9 + ph * 2.3 ) );
		field = strokeField( bent, centre );
	}
	else if( mode == 5 && amount > 0.001 )
	{
		float a = TraceAngle * kTau;
		vec2 v = vec2( cos( a ), sin( a ) ) / vec2( Aspect, 1.0 );
		field = strokeField( uv, centre );
		for( int k = 1; k <= 8; ++k )
		{
			vec2 fk = strokeField( uv - v * float( k ) * amount * 0.02, centre );
			fk.x *= pow( 0.8, float( k ) );
			fk.y += float( k ) * BreakHue * 0.25;
			if( fk.x > field.x )
				field = fk;
		}
	}
	else
	{
		field = strokeField( uv, centre );
	}

	float m = field.x;
	float body = smoothstep( 0.10, 0.85, m );
	float hot = smoothstep( mix( 0.995, 0.55, Core ), 1.0, m );

	vec3 tube = strokeColour( field.y );
	float y = dot( tube, vec3( 0.2126, 0.7152, 0.0722 ) );
	tube = mix( vec3( y ), tube, Saturation );

	float band = Audio[ clamp( int( fract( field.y ) * 64.0 ), 0, 63 ) ];
	float gate = mix( 1.0, band, AudioLevel );

	float gain = gate * Brightness;
	vec3 rgb = ( tube * body + vec3( 1.0 ) * hot * 0.8 ) * gain;
	float alpha = clamp( max( body, hot ) * gain, 0.0, 1.0 );

	fragColor = vec4( rgb, alpha );
}
`;

// --------------------------------------------------------------------------
// Pass 5: blur -- the same five-fetch Gaussian.
// --------------------------------------------------------------------------
export const BLUR = HEADER + `
uniform sampler2D SourceTexture;
uniform vec2 Direction;
uniform float SourceLod;

in vec2 uv;
out vec4 fragColor;

void main()
{
	float offsets[ 3 ];
	offsets[ 0 ] = 0.0; offsets[ 1 ] = 1.3846153846; offsets[ 2 ] = 3.2307692308;
	float weights[ 3 ];
	weights[ 0 ] = 0.2270270270; weights[ 1 ] = 0.3162162162; weights[ 2 ] = 0.0702702703;

	vec4 sum = textureLod( SourceTexture, uv, SourceLod ) * weights[ 0 ];
	for( int i = 1; i < 3; ++i )
	{
		sum += textureLod( SourceTexture, uv + Direction * offsets[ i ], SourceLod ) * weights[ i ];
		sum += textureLod( SourceTexture, uv - Direction * offsets[ i ], SourceLod ) * weights[ i ];
	}
	fragColor = sum;
}
`;

// --------------------------------------------------------------------------
// Pass 6: composite.
// --------------------------------------------------------------------------
export const COMPOSITE = HEADER + `
uniform sampler2D LightTexture;
uniform sampler2D GlowTexture;
uniform sampler2D CopyTexture;
uniform sampler2D StableTexture;

uniform float Background; //0 black, 1 source, 2 dimmed, 4 edges
uniform float Dim;
uniform float Glow;

in vec2 uv;
out vec4 fragColor;

void main()
{
	vec4 light = texture( LightTexture, uv );
	vec4 glow = texture( GlowTexture, uv );
	vec4 lit = light + glow * Glow;

	int mode = int( Background + 0.5 );
	vec4 source = texture( CopyTexture, uv );

	vec4 result;
	if( mode == 4 )
	{
		float mask = texture( StableTexture, uv ).a;
		result = vec4( vec3( mask ), 1.0 );
	}
	else
	{
		vec4 back = source;
		if( mode == 0 )
			back = vec4( 0.0, 0.0, 0.0, 1.0 );
		else if( mode == 2 )
			back = vec4( source.rgb * Dim, source.a );
		result = vec4( back.rgb + lit.rgb, 1.0 );
	}

	fragColor = result;
}
`;
