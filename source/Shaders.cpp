#include "Shaders.h"

namespace outrun
{

const char* const kVertexShader = R"(#version 410 core

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;

	//Straight through, in 0..1 picture space. The usual FFGL vertex shader
	//folds MaxUV in here; that happens once in the copy pass instead, and
	//every pass after it works on a texture we allocated, where the picture
	//really does fill the texture.
	uv = vUV;
}
)";

//---------------------------------------------------------------------------
// Pass 1: copy. Effect only. Lifted from tinsel.
//---------------------------------------------------------------------------
const char* const kCopyShader = R"(#version 410 core

uniform sampler2D InputTexture;
uniform vec2 MaxUV;      //the part of the input texture that is really picture
uniform vec2 HalfTexel;  //half an input texel, in picture space

in vec2 uv;
out vec4 fragColor;

void main()
{
	//Half a texel in from the edge. GL_LINEAR at the picture boundary takes
	//half its weight from the texture's undrawn padding, and on a logo that
	//shows up as a false edge running down the side of the frame -- which this
	//plugin would then dutifully trace in neon.
	vec2 picture = clamp( uv, HalfTexel, vec2( 1.0 ) - HalfTexel );

	//Premultiplied in, premultiplied out. The mip chain built on this texture
	//is a box filter, and averaging premultiplied samples is the correct
	//filter; averaging straight colour smears the colour of transparent pixels
	//into the picture.
	fragColor = texture( InputTexture, picture * MaxUV );
}
)";

//---------------------------------------------------------------------------
// Pass 2: edge. Effect only. Lifted from tinsel.
//---------------------------------------------------------------------------
const char* const kEdgeShader = R"(#version 410 core

uniform sampler2D CopyTexture;
uniform vec2 TexelSize;   //one texel of the copy buffer, in picture space
uniform float Detail;     //mip level to detect at, 0 = per pixel
uniform float SourceMode; //0 luma, 1 alpha, 2 chroma, 3 luma or alpha

in vec2 uv;
out vec4 fragColor;

//What "different" means between two pixels. The choice matters more than the
//operator does: a logo delivered with alpha has a perfect edge already in the
//alpha channel and running a luma Sobel over it instead is throwing away the
//only clean signal in the frame.
float channel( vec2 at )
{
	vec4 c = textureLod( CopyTexture, at, Detail );
	int mode = int( SourceMode + 0.5 );

	if( mode == 1 )
		return c.a;

	if( mode == 2 )
	{
		//Chroma distance from the pixel's own grey. Finds the boundary between
		//two colours of equal brightness, which is exactly the case a luma
		//Sobel is blind to and which brand artwork is full of.
		float y = dot( c.rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
		return length( c.rgb - vec3( y ) ) + y * 0.25;
	}

	//Un-premultiply before taking luma, or a soft edge in the alpha channel
	//reads as a brightness ramp and the Sobel finds a wide smear where there
	//is a hard boundary.
	vec3 straight = c.a > 0.0031 ? c.rgb / c.a : c.rgb;
	float luma = dot( straight, vec3( 0.2126, 0.7152, 0.0722 ) );

	if( mode == 3 )
	{
		//Both channels at once, for artwork that could be delivered either way.
		//
		//The alpha sets a floor so that a *dark* logo on transparency still has
		//a boundary -- weight it purely by luma and a black mark on nothing is
		//zero on both sides of its own edge and vanishes. The luma term then
		//adds the detail inside the shape on top of that floor.
		//
		//Written as `max( luma * c.a, c.a )` this is identically 1.0 for every
		//opaque pixel, and the mode -- the default -- found no edges at all on
		//clips without alpha. That bug cost tinsel 27 of 31 controls reading as
		//dead in its sweep; the fixed form is inherited here.
		return c.a * ( 0.35 + 0.65 * luma );
	}

	return luma;
}

void main()
{
	//Tap spacing follows the detail level. Detecting at mip 2 with taps one
	//full-resolution texel apart samples the same texel three times and
	//reports no edge anywhere -- the scale of the blur and the scale of the
	//operator have to move together.
	vec2 tap = TexelSize * exp2( Detail );

	//Sobel. Two 3x3 convolutions; the magnitude of the pair is the gradient.
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

	//Divide by four, which is the sum of one side of the kernel, so that a
	//clean black-to-white step gives exactly 1.0 and the Sensitivity control
	//has the same meaning whatever the footage.
	fragColor = vec4( length( vec2( gx, gy ) ) * 0.25, 0.0, 0.0, 1.0 );
}
)";

//---------------------------------------------------------------------------
// Pass 3: stabilise. Effect only. Lifted from tinsel.
//---------------------------------------------------------------------------
const char* const kStabiliseShader = R"(#version 410 core

uniform sampler2D EdgeTexture;
uniform sampler2D HistoryTexture; //the previous frame's output of this pass
uniform float Attack;             //0..1 blend towards a *stronger* edge
uniform float Release;            //0..1 blend towards a *weaker* edge
uniform float Sensitivity;        //gradient magnitude at which a stroke is fully lit
uniform float Softness;           //width of the threshold, as a fraction of it
uniform float Reset;              //1 to ignore history entirely

in vec2 uv;
out vec4 fragColor;

void main()
{
	float current = texture( EdgeTexture, uv ).r;
	float history = texture( HistoryTexture, uv ).r;

	//Asymmetric on purpose, and this is the whole of "survives video".
	//
	//A symmetric IIR is a low-pass, and a low-pass on an edge signal trades
	//flicker for lag: the outline of anything moving arrives late and smeared
	//behind it. What actually goes wrong on footage is not that edges move,
	//it is that they *drop out* -- a boundary that grades through the
	//threshold for one frame, or sensor noise on a nearly-flat gradient, and
	//the stroke blinks. So rise fast enough to be immediate and fall slowly
	//enough to bridge the gap. An edge that appears is believed at once; an
	//edge that vanishes is given a few frames to come back.
	float blend = current > history ? Attack : Release;
	float stable = mix( history, current, blend ) * ( 1.0 - Reset ) + current * Reset;

	//Threshold with a soft shoulder rather than a step. A hard threshold makes
	//Sensitivity a control that does nothing at all and then everything at
	//once, and puts a stack of aliasing on every diagonal.
	float lower = Sensitivity * ( 1.0 - Softness );
	float upper = Sensitivity * ( 1.0 + Softness );
	float mask = smoothstep( lower, max( upper, lower + 1e-5 ), stable );

	//r: what feeds back, before the threshold, so that Sensitivity can be
	//   moved without the history having to re-converge.
	//gb: the first moments of the mask, for the centroid. Reduced to a single
	//   texel by the mip chain, so the trace coordinate can rotate about the
	//   artwork instead of about the middle of the frame.
	//a: the mask itself, which is also the denominator of that average.
	fragColor = vec4( stable, uv.x * mask, uv.y * mask, mask );
}
)";

//---------------------------------------------------------------------------
// Pass 4: stroke. The plugin. Assembled by StrokeShaderSource() so both
// variants compile from the one source string.
//---------------------------------------------------------------------------
static const char* const kStrokePreamble = R"(
uniform sampler2D PaletteTexture; //kPaletteSize x Palette::Count, texelFetch only

#ifdef OUTRUN_EFFECT
uniform sampler2D StableTexture;  //r = raw stable edge, a = mask, gb = moments
uniform sampler2D CopyTexture;    //the picture, for the Clip colour modes
uniform float CentroidLod;        //top of the stable buffer's mip chain
uniform float Trace;              //0 spiral, 1 angle, 2 linear, 3 radial
#endif

uniform float Aspect;             //width / height, so a circle is a circle
uniform vec2 PictureSize;         //in pixels; the paths' anti-alias needs it
uniform float WidthPx;            //tube radius, in pixels
uniform float Core;               //how much of the tube saturates to white
uniform float TraceAngle;         //turns; the Linear trace and Rays' heading

uniform float PathIndex;
uniform float PathScale;
uniform float PathDetail;
uniform float Horizon;

uniform float BreakMode;          //0 none, 1 echo, 2 angular, 3 scan, 4 flow, 5 rays
uniform float BreakAmount;        //already audio-boosted, CPU-side
uniform float BreakSpread;
uniform float BreakHue;

uniform float ColourMode;         //0 palette, 1 clip, 2 clip x palette
uniform float PaletteIndex;
uniform vec3 Colour1;
uniform vec3 Colour2;
uniform float Spread;
uniform float Saturation;
uniform float Brightness;

uniform float Phase;              //cycles; the only clock this shader sees

uniform float Audio[ 64 ];        //smoothed spectrum, low frequencies first
uniform float AudioLevel;         //0 ignores the spectrum entirely

#ifndef OUTRUN_EFFECT
uniform vec2 Curve[ 49 ];         //CPU-solved samples of the marched Lissajous
#endif

in vec2 uv;
out vec4 fragColor;

const int kPaletteSize = 256;
const float kTau = 6.283185307179586;
const uint kOdd = 2654435761u;
)";

//---------------------------------------------------------------------------
// The palette lookup, as its own fragment: it is shared verbatim with the
// harness's probe, so `outruntest --palettes` checks the text the plugin
// actually runs -- the one piece of this shader that has a CPU mirror.
//---------------------------------------------------------------------------
static const char* const kPaletteGLSL = R"(
vec3 paletteColour( float position )
{
	position = fract( position );

	int index = int( PaletteIndex + 0.5 );
	if( index == 0 )
		return Colour1;
	if( index == 1 )
		return mix( Colour1, Colour2, position );

	//texelFetch rather than texture(). One texture has one filter for both
	//axes, and bilinear on this one would blend a palette into the palette
	//below it along the way -- so the interpolation along the gradient is done
	//here, from two exact fetches, and the sampler never filters anything.
	float p = position * float( kPaletteSize - 1 );
	float i0 = floor( p );
	int a = int( i0 );
	int b = min( a + 1, kPaletteSize - 1 );

	vec3 ca = texelFetch( PaletteTexture, ivec2( a, index ), 0 ).rgb;
	vec3 cb = texelFetch( PaletteTexture, ivec2( b, index ), 0 ).rgb;
	return mix( ca, cb, p - i0 );
}
)";

static const char* const kStrokeLibrary = R"(
//---------------------------------------------------------------------------
// Randomness. Integer throughout: anything built on sin() gives the driver's
// answer, and two GPUs then disagree about which building is tall or which
// scanline jumps.
//---------------------------------------------------------------------------
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

//Distance to the nearest integer: a triangle wave, continuous everywhere,
//which is what makes it safe under the derivative the stroke pass takes.
//`abs( fract( x ) - 0.5 )` puts its zeros at half-integers instead; the lines
//want to sit on the integers.
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

#ifdef OUTRUN_EFFECT
//---------------------------------------------------------------------------
// The effect's field: the stabilised mask, and a coordinate along it.
//---------------------------------------------------------------------------
vec2 strokeCentre()
{
	//The middle of the artwork, not the middle of the frame. Reduced out of
	//the stable buffer's mip chain in one fetch: the top level is the average
	//of (x*mask, y*mask, mask) over the picture, and the first two divided by
	//the third are the centroid. A frame with no edges divides by zero, so it
	//falls back to the centre.
	vec4 moments = textureLod( StableTexture, vec2( 0.5 ), CentroidLod );
	return moments.a > 1e-4 ? moments.gb / moments.a : vec2( 0.5 );
}

float traceCoordinate( vec2 at, vec2 centre )
{
	//`wiring`, not `layout`: `layout` is a GLSL keyword, and using it fails
	//with nothing but "syntax error" and a line number in a file that does
	//not exist, because this shader is assembled from strings.
	int wiring = int( Trace + 0.5 );

	vec2 p = ( at - centre ) * vec2( Aspect, 1.0 );
	float ang = atan( p.y, p.x ) / kTau + 0.5;
	float radius = length( p );

	if( wiring == 0 )   //Spiral -- round the artwork, climbing
		return ang + radius * 1.5;

	if( wiring == 2 )   //Linear -- a plain projection
	{
		float a = TraceAngle * kTau;
		return dot( p, vec2( cos( a ), sin( a ) ) ) * 0.5 + 0.5;
	}

	if( wiring == 3 )   //Radial -- rings out from the middle
		return radius;

	//Angle (1), the default: once round the artwork is one palette run, which
	//is what a bent neon tube does.
	return ang;
}

vec2 strokeField( vec2 at, vec2 centre )
{
	//Width is a dilation done with the mip chain that is already there.
	//Blurring the thin Sobel ridge lowers its peak in proportion to how much
	//it spread, so reading it blurred and restoring the peak widens the tube
	//without a second buffer. The fractional level makes the control
	//continuous; the native ridge is about 3 px, which is the divisor.
	float lod = clamp( log2( max( WidthPx / 3.0, 1.0 ) ), 0.0, 3.5 );
	float m = textureLod( StableTexture, at, lod ).a;
	m = clamp( m * ( 1.0 + lod * 2.0 ), 0.0, 1.0 );

	return vec2( m, traceCoordinate( at, centre ) );
}

#else
//---------------------------------------------------------------------------
// The source's field: a distance to one of the paths, and a coordinate along
// it. Everything is a pure function of (uv, Phase) -- no state anywhere, so
// nothing drifts with the frame rate and any frame renders on its own.
//
// The distances are NOT normalised here. The stroke pass divides by the
// screen-space gradient, so each function only needs a continuous field whose
// zero set is the stroke -- mixed units between branches of a min() are fine,
// and the perspective grid thins toward the horizon without any per-path
// effort.
//---------------------------------------------------------------------------
vec2 strokeCentre() { return vec2( 0.5 ); }

vec2 pathGrid( vec2 at )
{
	float d = 1e6;
	float t = 0.0;

	//The ground plane -- the bottom of the frame in GL's y-up picture space.
	//World depth is 1/(Horizon - uv.y), so lines at integer world coordinates
	//converge on the horizon by construction.
	float below = Horizon - at.y;
	if( below > 1e-3 )
	{
		vec2 g = vec2( ( at.x - 0.5 ) * Aspect / below, 1.0 / below ) * ( PathDetail * 0.35 );
		g.y += Phase;   //toward the viewer
		d = min( tri( g.x ), tri( g.y ) );

		//Merge the lines into a solid band as the cells go subpixel. Past
		//about half a cell per pixel the screen-space derivative the stroke
		//pass takes is sampling a triangle wave beyond Nyquist and comes back
		//as noise -- which renders as a band of speckle where the eye expects
		//the horizon bloom. Driving the distance to zero there is not a
		//workaround, it is the correct limit: infinitely many lines per pixel
		//IS solid.
		float cellsPerPixel = ( PathDetail * 0.35 ) / max( below * below, 1e-6 ) / PictureSize.y;
		d = mix( d, 0.0, clamp( ( cellsPerPixel - 0.3 ) / 0.3, 0.0, 1.0 ) );

		t = clamp( below * 2.0, 0.0, 1.0 );
	}

	//The horizon itself, as a tube.
	float dh = abs( at.y - Horizon ) * 3.0;
	if( dh < d ) { d = dh; t = 0.0; }

	//The sun, sitting on the horizon.
	float sunR = 0.04 + 0.16 * PathScale;
	vec2 ps = ( at - vec2( 0.5, Horizon + sunR * 1.05 ) ) * vec2( Aspect, 1.0 );
	float sunIn = length( ps ) - sunR;
	float dSun = abs( sunIn ) * 3.0;
	if( dSun < d ) { d = dSun; t = 0.85; }

	//The blinds across its lower half, sliding down forever.
	float stripes = tri( ps.y * ( 9.0 / max( sunR, 1e-3 ) ) - Phase * 2.0 );
	float blind = max( stripes, max( sunIn * 20.0, ps.y * 20.0 ) );
	if( blind < d ) { d = blind; t = 0.85; }

	return vec2( d, t );
}

vec2 pathLissajous( vec2 at )
{
	//The one path with no closed form: distance to 48 CPU-solved segments.
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

	//Two interleaved rectangular lattices are a hex lattice; the nearer centre
	//wins, and the Voronoi cell of that lattice is the hexagon.
	const vec2 s = vec2( 1.0, 1.7320508 );
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

	//Every motif passes through edge midpoints, which is what keeps a trace
	//continuous into the next cell whatever that cell rolled.
	vec2 o = f * vec2( ( id & 1u ) == 0u ? 1.0 : -1.0, ( id & 2u ) == 0u ? 1.0 : -1.0 );

	uint m = ( id >> 2u ) % 5u;
	float d;
	if( m == 0u )
		d = abs( f.y );                                        //straight across
	else if( m == 1u )
		d = abs( f.x );                                        //straight down
	else if( m == 2u )
		d = min( sdSeg( o, vec2( -0.5, 0.0 ), vec2( 0.0, 0.0 ) ),
		         sdSeg( o, vec2( 0.0, 0.0 ), vec2( 0.0, 0.5 ) ) ); //elbow
	else if( m == 3u )
		d = min( abs( f.y ),
		         sdSeg( o, vec2( 0.0, 0.0 ), vec2( 0.0, 0.5 ) ) ); //T junction
	else
		d = min( abs( f.y ), abs( f.x ) );                     //cross

	//Pads on a fraction of the cells, as rings round the junction.
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

	//The back layer: ridged mountains on slow parallax. Folding the noise --
	//abs of a signed value -- is what puts peaks on it.
	float xm = at.x * Aspect * n * 0.35 + Phase * 0.15;
	float ridge = abs( valueNoise( xm ) * 2.0 - 1.0 ) * 0.5
	            + abs( valueNoise( xm * 2.3 + 37.0 ) * 2.0 - 1.0 ) * 0.15;
	float mtnY = Horizon + ridge * 0.45 * PathScale;
	float d = abs( at.y - mtnY );
	float t = 0.75;

	//The front layer: a hashed roofline on faster parallax. The height jumps
	//at each building edge, and the derivative spike the stroke pass sees
	//there is what draws the building's *side* -- the vertical glow at every
	//discontinuity is the feature, not an artefact.
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

	//Log spacing: every ring is the same ratio further out, which is what a
	//tunnel receding at constant speed looks like. Phase pulls the rings
	//outward.
	float w = log( max( r, 1e-3 ) ) * ( PathDetail * 0.5 + 1.0 ) - Phase;
	float d = tri( w );

	//The same subpixel merge as the grid's horizon: ring spacing shrinks
	//toward the centre, and past Nyquist the derivative-based width reads
	//speckle where the eye expects the bright mouth of the tunnel.
	float ringsPerPixel = ( PathDetail * 0.5 + 1.0 ) / max( r, 1e-3 ) / ( PictureSize.y * max( PathScale, 0.05 ) );
	d = mix( d, 0.0, clamp( ( ringsPerPixel - 0.3 ) / 0.3, 0.0, 1.0 ) );

	float ang = atan( p.y, p.x ) / kTau + 0.5;

	//Spokes, kept away from the middle where every spoke converges on one
	//pixel and the tube maths would read solid.
	float spokes = max( tri( ang * ( floor( PathDetail * 0.5 ) + 5.0 ) ), ( 0.12 - r ) * 8.0 );
	if( spokes < d )
		d = spokes;

	return vec2( d, ang );
}

float sdStar5( vec2 p, float r, float rf )
{
	const vec2 k1 = vec2( 0.809016994375, -0.587785252292 );
	const vec2 k2 = vec2( -k1.x, k1.y );
	p.x = abs( p.x );
	p -= 2.0 * max( dot( k1, p ), 0.0 ) * k1;
	p -= 2.0 * max( dot( k2, p ), 0.0 ) * k2;
	p.x = abs( p.x );
	p.y -= r;
	vec2 ba = rf * vec2( -k1.y, k1.x ) - vec2( 0.0, 1.0 );
	float h = clamp( dot( p, ba ) / dot( ba, ba ), 0.0, r );
	//The sign convention does not matter here: the stroke takes abs() of it.
	return length( p - ba * h ) * sign( p.y * ba.x - p.x * ba.y );
}

vec2 pathStar( vec2 at )
{
	vec2 p = ( at - 0.5 ) * vec2( Aspect, 1.0 ) / max( PathScale * 0.6, 0.05 );
	float a = Phase * kTau * 0.25;   //one revolution per four cycles
	p = mat2( cos( a ), -sin( a ), sin( a ), cos( a ) ) * p;
	float d = abs( sdStar5( p, 0.7, 0.45 ) );
	float t = atan( p.y, p.x ) / kTau + 0.5;
	return vec2( d, t );
}

vec2 pathWaveform( vec2 at )
{
	//The smoothed spectrum as a mirrored oscilloscope trace: low frequencies
	//at the left, the palette running along the frequency axis. With nothing
	//routed the bins are zero and this collapses to a single centre line,
	//which is what an oscilloscope with no signal shows.
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

vec2 strokeField( vec2 at, vec2 centre )
{
	vec2 dt = pathDistance( at );

	//The screen-space gradient converts the field's own units to pixels, so a
	//tube is WidthPx wide *on screen* everywhere -- including a perspective
	//grid line halfway to the horizon. This is the same idea as tinsel's
	//lamps-per-pixel division, applied to a distance instead of a coordinate.
	float g = max( length( vec2( dFdx( dt.x ), dFdy( dt.x ) ) ), 1e-6 );
	float dPx = abs( dt.x ) / g;

	float m = 1.0 - smoothstep( WidthPx * 0.35, max( WidthPx, 1.5 ), dPx );
	return vec2( m, dt.y );
}
#endif
)";

static const char* const kStrokeMain = R"(
vec3 strokeColour( float s )
{
	float pos = s * Spread - Phase * 0.25;
	vec3 pal = paletteColour( pos );

#ifdef OUTRUN_EFFECT
	int cm = int( ColourMode + 0.5 );
	if( cm >= 1 )
	{
		//The artwork's own colour along its own outline. Un-premultiplied
		//first: the copy is premultiplied, and multiplying by it directly
		//would darken the stroke wherever the artwork happens to be soft.
		//Sampled at the pixel's own position, not the warped tap's, so an
		//echo drifting off the artwork keeps the colour it left behind.
		vec4 clip = texture( CopyTexture, uv );
		vec3 straight = clip.a > 0.0031 ? clip.rgb / clip.a : clip.rgb;
		return cm == 1 ? straight : straight * pal;
	}
#endif

	return pal;
}

void main()
{
	vec2 centre = strokeCentre();
	int mode = int( BreakMode + 0.5 );
	float amount = BreakAmount;

	//-----------------------------------------------------------------------
	// Breakaway: warps of where the field is *sampled*, never of what it is.
	// The base tap survives in every mode, which is what makes Break Amount a
	// true morph from the faithful geometry. All state-free: the same frame
	// at the same phase is the same picture.
	//-----------------------------------------------------------------------
	vec2 field;

	if( mode == 1 && amount > 0.001 )
	{
		//Echo: re-sample the field shrunk toward the centre, so the copies
		//appear to drift outward -- each dimmer and shifted along the palette.
		//Combined with max, not sum: overlapping echoes must not blow out.
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
		//Angular: snap the sampling direction about the centre toward a 45
		//degree lattice. Smooth outlines collapse into technical linework;
		//the pinch where several directions fold onto one snap line is the
		//look, not a defect. Spread rotates the lattice.
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
		//Scan: per-scanline displacement, cubed so most rows barely move and
		//a few jump -- glitch, not noise. Time-quantised so a jump holds for
		//an eighth of a cycle rather than boiling.
		float rowH = mix( 0.006, 0.06, BreakSpread );
		float row = floor( uv.y / rowH );
		uint slot = uint( int( floor( Phase * 8.0 ) ) + 65536 );
		float r = Hash01( uint( int( row ) + 4096 ) * kOdd ^ slot * 0x9E3779B9u ) * 2.0 - 1.0;
		field = strokeField( uv + vec2( r * r * r * amount * 0.3, 0.0 ), centre );
		field.y += r * BreakHue;
	}
	else if( mode == 4 && amount > 0.001 )
	{
		//Flow: a stateless perturbation, each component driven mostly by the
		//other axis so the field is divergence-poor -- strokes wander without
		//bunching up.
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
		//Rays: march the field along one direction so every stroke trails a
		//comet streak. The heading is Direction's; Spread is unused here so
		//the two ray controls do not fight.
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

	//-----------------------------------------------------------------------
	// The neon look: a coloured tube skirt with a white-hot centre. Core sets
	// how much of the tube saturates; the glow pass supplies the halo.
	//-----------------------------------------------------------------------
	float m = field.x;
	float body = smoothstep( 0.10, 0.85, m );
	float hot = smoothstep( mix( 0.995, 0.55, Core ), 1.0, m );

	vec3 tube = strokeColour( field.y );

	//Desaturate towards the tube's own luminance, so Saturation at zero gives
	//white tubes rather than grey ones.
	float y = dot( tube, vec3( 0.2126, 0.7152, 0.0722 ) );
	tube = mix( vec3( y ), tube, Saturation );

	//Audio: a brightness gate from the host's FFT, laid along the stroke with
	//the low frequencies at the start of the run. Orthogonal to everything
	//else: any path or trace becomes a spectrum display of itself.
	float band = Audio[ clamp( int( fract( field.y ) * 64.0 ), 0, 63 ) ];
	float gate = mix( 1.0, band, AudioLevel );

	float gain = gate * Brightness;
	vec3 rgb = ( tube * body + vec3( 1.0 ) * hot * 0.8 ) * gain;
	float alpha = clamp( max( body, hot ) * gain, 0.0, 1.0 );

	//Premultiplied out, matching everything else in the chain.
	fragColor = vec4( rgb, alpha );
}
)";

//---------------------------------------------------------------------------
// Pass 5: blur. Run twice, once per axis, twice over. Lifted from tinsel.
//---------------------------------------------------------------------------
const char* const kBlurShader = R"(#version 410 core

uniform sampler2D SourceTexture;
uniform vec2 Direction;  //one tap step, in picture space. Zero on the other axis.
uniform float SourceLod; //mip level to read at. Non-zero only on the first pass.

in vec2 uv;
out vec4 fragColor;

void main()
{
	//A nine-tap Gaussian folded into five fetches. The offsets are not texel
	//centres: each fetch sits between two texels so that GL_LINEAR returns
	//their weighted average, which is why this needs Sampling::Linear and
	//would silently become a five-tap box on a Nearest buffer.
	const float offsets[ 3 ] = float[]( 0.0, 1.3846153846, 3.2307692308 );
	const float weights[ 3 ] = float[]( 0.2270270270, 0.3162162162, 0.0702702703 );

	//The level matters on the first pass and only there: it reads the
	//full-resolution stroke buffer while drawing into a quarter-size glow
	//buffer, and five point samples of a picture four times finer than the
	//target is not a blur, it is an aliased downsample -- thin tubes are
	//caught or missed by where they happen to sit, and the miss pattern reads
	//as streaks. The pre-filtered level costs nothing and removes it.
	vec4 sum = textureLod( SourceTexture, uv, SourceLod ) * weights[ 0 ];
	for( int i = 1; i < 3; ++i )
	{
		sum += textureLod( SourceTexture, uv + Direction * offsets[ i ], SourceLod ) * weights[ i ];
		sum += textureLod( SourceTexture, uv - Direction * offsets[ i ], SourceLod ) * weights[ i ];
	}

	fragColor = sum;
}
)";

//---------------------------------------------------------------------------
// Pass 6: composite. One source string, gated like the stroke pass: the
// source build has no clip and no mask, and compiles those modes out rather
// than sampling textures that were never allocated.
//---------------------------------------------------------------------------
static const char* const kCompositeShader = R"(#version 410 core

uniform sampler2D LightTexture;
uniform sampler2D GlowTexture;

#ifdef OUTRUN_EFFECT
uniform sampler2D CopyTexture;
uniform sampler2D StableTexture;
uniform float MixAmount;
uniform float Dim;
#endif

uniform float Background;  //0 black, 1 source, 2 dimmed source, 3 transparent, 4 edges
uniform float Glow;

in vec2 uv;
out vec4 fragColor;

void main()
{
	vec4 light = texture( LightTexture, uv );
	vec4 glow = texture( GlowTexture, uv );

	//The glow is added, not blended. A tube's halo is light arriving on top
	//of whatever is already there, and alpha-blending it would have the halo
	//*hide* the picture it is supposed to be sitting on.
	vec4 lit = light + glow * Glow;

	int mode = int( Background + 0.5 );

#ifdef OUTRUN_EFFECT
	vec4 source = texture( CopyTexture, uv );

	vec4 result;
	if( mode == 4 )
	{
		//The edge mask on its own, in white. Not a look -- it is how
		//Sensitivity, Detail and Width are actually set, because judging a
		//threshold through a layer of neon and glow is guesswork.
		float mask = texture( StableTexture, uv ).a;
		result = vec4( vec3( mask ), 1.0 );
	}
	else if( mode == 3 )
	{
		//Premultiplied already, so this is the plugin's own output over
		//nothing, ready for the layer below to show through.
		result = lit;
	}
	else
	{
		vec4 back = source;
		if( mode == 0 )
			back = vec4( 0.0, 0.0, 0.0, 1.0 );
		else if( mode == 2 )
			back = vec4( source.rgb * Dim, source.a );

		result = vec4( back.rgb + lit.rgb, clamp( back.a + lit.a, 0.0, 1.0 ) );
	}

	fragColor = mix( source, result, MixAmount );
#else
	//The source has no clip: Source, Dimmed Source and Edges all degrade to
	//Black, and Mix is ignored -- the parameters exist so a composition can
	//move between the two plugins, not because they mean anything here.
	if( mode == 3 )
		fragColor = lit;
	else
		fragColor = vec4( lit.rgb, 1.0 );
#endif
}
)";

//---------------------------------------------------------------------------
// Assembly.
//---------------------------------------------------------------------------
namespace
{
std::string assemble( const char* body, bool effect )
{
	std::string s( "#version 410 core\n" );
	if( effect )
		s += "#define OUTRUN_EFFECT 1\n";
	s += body;
	return s;
}

/// For sources that carry their own #version line: inject the define on the
/// line after it, where the compiler will still accept a preprocessor token.
std::string withDefine( const char* source, bool effect )
{
	std::string s( source );
	if( effect )
	{
		const size_t nl = s.find( '\n' );
		if( nl != std::string::npos )
			s.insert( nl + 1, "#define OUTRUN_EFFECT 1\n" );
	}
	return s;
}
} // namespace

std::string StrokeShaderSource( bool effect )
{
	return assemble(
		( std::string( kStrokePreamble ) + kStrokeLibrary + kPaletteGLSL + kStrokeMain ).c_str(), effect );
}

std::string PaletteProbeShaderSource()
{
	//One palette entry per pixel across a 256-wide, one-pixel-tall RGBA32F
	//target, with paletteColour's answer written straight out. No stroke, no
	//breakaway: those are the stroke pass's business, and folding any of them
	//in would mean a disagreement could not be attributed. The float target
	//matters -- an 8-bit readback would agree to within 1/255 with almost any
	//drift and call it a pass.
	static const char* const probe = R"(
uniform sampler2D PaletteTexture;
uniform float PaletteIndex;
uniform vec3 Colour1;
uniform vec3 Colour2;

out vec4 fragColor;

const int kPaletteSize = 256;
)";

	static const char* const probeMain = R"(
void main()
{
	float position = ( gl_FragCoord.x - 0.5 ) / 256.0;
	fragColor = vec4( paletteColour( position ), 1.0 );
}
)";

	return std::string( "#version 410 core\n" ) + probe + kPaletteGLSL + probeMain;
}

std::string CompositeShaderSource( bool effect )
{
	return withDefine( kCompositeShader, effect );
}

} // namespace outrun
