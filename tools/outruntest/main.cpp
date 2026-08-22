/**
    outruntest -- render Outrun offline, and check what its strokes are doing.

    It drives the REAL plugin class over a synthetic test card. Engine A
    (Trace) is the default; `--set "Engine=1"` switches to Engine B (Paths).
    A test that exercises a reimplementation tests the reimplementation.

        outruntest --out /tmp/frame.png       Engine A, tracing the test card
        outruntest --set "Engine=1" --out ..  Engine B, the perspective grid
        outruntest --list                     every parameter and its default
        outruntest --palettes                 GLSL palette lookup vs the C++ bake
        outruntest --palettes-image /tmp/p.png  the palette table, as a picture
        outruntest --paths /tmp/paths.png     every path, 8-up, checked distinct
        outruntest --breaks /tmp/b.png        every break mode, 6-up, checked live
        outruntest --presets /tmp/pre.png     every factory preset, checked distinct
        outruntest --card /tmp/card.png       the test card on its own
        outruntest --pipe                     raw frames in, raw frames out

    The clock is synthetic and has to be: left to the wall clock the harness
    renders a hundred frames in milliseconds, no time passes, and Speed
    measurably does nothing. `driveClock` advances SetTime and SetBeatInfo
    together (120 BPM, 4/4, from zero -- bar N starts at exactly 2N seconds) so
    the Beat and Bar sync modes are as reproducible offline as Free, and it
    writes a fixed synthetic spectrum into the Audio buffer parameter so the
    audio controls provably do something.

    `--script` is a plain text file of `frame  Parameter Name  value` lines,
    the same format as the rest of the fleet's harnesses, so one build.py can
    film any of them. `--pipe` takes the fleet's raw RGBA frame protocol:

        ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - \
          | outruntest --pipe --width 1920 --height 1080 [--script cues.txt] \
          | ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -i - out.mov

    What is deliberately NOT here: a CPU mirror of the path distance functions
    or the breakaway warps. The palette lookup is the one mirrored piece of
    GLSL (`--palettes` proves it against the C++ bake); the geometry is proven
    alive and distinct by the contact sheets and by tools/sweep.py.
*/

#include "Controls.h"
#include "Hash.h"
#include "Outrun.h"
#include "Palette.h"
#include "Paths.h"
#include "Presets.h"
#include "Shaders.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <thread>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace outrun;

namespace
{
//---------------------------------------------------------------------------
/// How far the GLSL palette lookup and the C++ bake may disagree. The shader
/// interpolates between two exact fetches of the same table the CPU baked, so
/// the only honest differences are float rounding in the mix -- what this
/// catches is a drifted index or a swapped row, which misses by whole colours.
constexpr float kPaletteTolerance = 2e-3f;

//---------------------------------------------------------------------------
// A PNG writer. zlib ships with the OS, so this is a few chunk headers and a
// CRC rather than a dependency.
//---------------------------------------------------------------------------
void putU32( std::vector< unsigned char >& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

bool writePng( const std::string& path, int width, int height, const std::vector< unsigned char >& rgba )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = 0; y < height; ++y )
	{
		raw.push_back( 0 );//filter: none
		const unsigned char* row = rgba.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}

	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

	std::vector< unsigned char > ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.push_back( 8 );//bit depth
	ihdr.push_back( 6 );//truecolour with alpha
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	FILE* file = fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = fwrite( png.data(), 1, png.size(), file );
	fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// The test card the effect variant is exercised on. Same construction as
// tinsel's: each shape tests one thing the edge detector claims to do.
//---------------------------------------------------------------------------
std::vector< unsigned char > buildCard( int width, int height )
{
	std::vector< unsigned char > image( static_cast< size_t >( width ) * height * 4, 0 );

	const float w = static_cast< float >( width );
	const float h = static_cast< float >( height );

	for( int y = 0; y < height; ++y )
	{
		for( int x = 0; x < width; ++x )
		{
			const float u = ( static_cast< float >( x ) + 0.5f ) / w;
			const float v = ( static_cast< float >( y ) + 0.5f ) / h;

			float r = 0.0f, g = 0.0f, b = 0.0f;

			//A soft horizontal gradient everywhere, as the noise floor.
			r = g = b = 0.06f + 0.10f * u;

			//Disc, left third: a curved boundary at every angle.
			const float dx1 = ( u - 0.20f ) * ( w / h );
			const float dy1 = v - 0.5f;
			if( std::sqrt( dx1 * dx1 + dy1 * dy1 ) < 0.16f )
				r = g = b = 0.95f;

			//Ring, middle: two concentric boundaries close together.
			const float dx2 = ( u - 0.50f ) * ( w / h );
			const float dy2 = v - 0.5f;
			const float d2  = std::sqrt( dx2 * dx2 + dy2 * dy2 );
			if( d2 < 0.18f && d2 > 0.12f )
				r = g = b = 0.90f;

			//Bar, right third: straight boundaries on both axes.
			if( u > 0.72f && u < 0.88f && v > 0.20f && v < 0.80f )
				r = g = b = 1.0f;

			//Two fields of equal luminance, bottom left: invisible to a luma
			//Sobel, obvious to a chroma one.
			if( v < 0.16f && u < 0.40f )
			{
				const bool right = u > 0.20f;
				r                = right ? 0.10f : 0.9333f;
				g                = right ? 0.2775f : 0.0f;
				b                = 0.0f;
			}

			image[ ( static_cast< size_t >( y ) * width + x ) * 4 + 0 ] =
				static_cast< unsigned char >( std::min( 255.0f, r * 255.0f ) );
			image[ ( static_cast< size_t >( y ) * width + x ) * 4 + 1 ] =
				static_cast< unsigned char >( std::min( 255.0f, g * 255.0f ) );
			image[ ( static_cast< size_t >( y ) * width + x ) * 4 + 2 ] =
				static_cast< unsigned char >( std::min( 255.0f, b * 255.0f ) );
			image[ ( static_cast< size_t >( y ) * width + x ) * 4 + 3 ] = 255;
		}
	}

	return image;
}

/// The card with deterministic per-frame noise, which is the only way to test
/// Stability: on a still picture a temporal filter provably does nothing.
std::vector< unsigned char > addNoise( const std::vector< unsigned char >& card, int frame, float amount )
{
	std::vector< unsigned char > noisy = card;
	if( amount <= 0.0f )
		return noisy;

	const float scale = amount * 255.0f;
	for( size_t i = 0; i < noisy.size(); i += 4 )
	{
		const uint32_t seed = Hash2( static_cast< uint32_t >( i / 4 ), static_cast< uint32_t >( frame ) );
		const float jitter  = ( Unit( seed ) - 0.5f ) * scale;

		for( int c = 0; c < 3; ++c )
		{
			const float value = static_cast< float >( noisy[ i + c ] ) + jitter;
			noisy[ i + c ]    = static_cast< unsigned char >( std::min( 255.0f, std::max( 0.0f, value ) ) );
		}
	}
	return noisy;
}

//---------------------------------------------------------------------------
// GL plumbing.
//---------------------------------------------------------------------------
CGLContextObj createContext()
{
	//Accelerated first; fall back so the harness still runs somewhere without
	//a GPU, where it will at least prove the shaders compile.
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};

	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( software, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}

	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;

	CGLSetCurrentContext( context );
	return context;
}

GLuint makeTexture( int width, int height, const unsigned char* pixels )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

GLuint makeFramebuffer( GLuint texture )
{
	GLuint fbo = 0;
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
	return fbo;
}

std::vector< unsigned char > flipRows( const std::vector< unsigned char >& image, int width, int height )
{
	std::vector< unsigned char > flipped( image.size() );
	const size_t stride = static_cast< size_t >( width ) * 4;
	for( int y = 0; y < height; ++y )
		std::memcpy( flipped.data() + static_cast< size_t >( y ) * stride,
		             image.data() + static_cast< size_t >( height - 1 - y ) * stride, stride );
	return flipped;
}

std::vector< unsigned char > readBackRaw( GLuint fbo, int width, int height )
{
	std::vector< unsigned char > pixels( static_cast< size_t >( width ) * height * 4 );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
	return pixels;
}

//---------------------------------------------------------------------------
// Shader compilation, for the palette probe. The plugin uses ffglex for this;
// the probe cannot, because ffglex::FFGLShader insists on a vertex shader with
// the SDK's attribute layout, and the probe needs none of it.
//---------------------------------------------------------------------------
GLuint compileStage( GLenum type, const std::string& source, std::string& error )
{
	const GLuint shader   = glCreateShader( type );
	const char* const ptr = source.c_str();
	glShaderSource( shader, 1, &ptr, nullptr );
	glCompileShader( shader );

	GLint compiled = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if( compiled == GL_TRUE )
		return shader;

	GLint length = 0;
	glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &length );
	std::string log( static_cast< size_t >( std::max( length, 1 ) ), '\0' );
	glGetShaderInfoLog( shader, length, nullptr, log.data() );
	error = log;
	glDeleteShader( shader );
	return 0;
}

GLuint buildProbeProgram( std::string& error )
{
	static const char* const vertexSource = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
void main() { gl_Position = vPosition; }
)";

	const GLuint vertex = compileStage( GL_VERTEX_SHADER, vertexSource, error );
	if( vertex == 0 )
		return 0;

	const GLuint fragment = compileStage( GL_FRAGMENT_SHADER, PaletteProbeShaderSource(), error );
	if( fragment == 0 )
	{
		glDeleteShader( vertex );
		return 0;
	}

	const GLuint program = glCreateProgram();
	glAttachShader( program, vertex );
	glAttachShader( program, fragment );
	glLinkProgram( program );
	glDeleteShader( vertex );
	glDeleteShader( fragment );

	GLint linked = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );
	if( linked == GL_TRUE )
		return program;

	GLint length = 0;
	glGetProgramiv( program, GL_INFO_LOG_LENGTH, &length );
	std::string log( static_cast< size_t >( std::max( length, 1 ) ), '\0' );
	glGetProgramInfoLog( program, length, nullptr, log.data() );
	error = log;
	glDeleteProgram( program );
	return 0;
}

//---------------------------------------------------------------------------
// Parameters by display name, so the automation reads as English.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	std::string kind;
};

const char* kindName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_RED: return "red";
	case FF_TYPE_GREEN: return "green";
	case FF_TYPE_BLUE: return "blue";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_BUFFER: return "buffer";
	case FF_TYPE_STANDARD: return "standard";
	default: return "other";
	}
}

std::vector< NamedParameter > listParameters( OutrunPlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < PT_COUNT; ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ),
		                                 kindName( plugin.GetParamType( i ) ) } );
	}
	return list;
}

bool applySetting( OutrunPlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}

	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );

	for( const NamedParameter& parameter : listParameters( plugin ) )
	{
		if( parameter.name != name )
			continue;
		plugin.SetFloatParameter( parameter.index, std::strtof( value.c_str(), nullptr ) );
		return true;
	}

	error = "no parameter called '" + name + "'";
	return false;
}

//---------------------------------------------------------------------------
/// Drive the plugin's clock, its beat clock, and its spectrum together.
///
/// The synthetic transport is 120 BPM in 4/4 from time zero -- bar N starts
/// at exactly 2N seconds -- so the Beat and Bar sync modes are as
/// reproducible offline as Free is. Every path that advances time goes
/// through here; a path that called SetTime alone would leave barPhase frozen
/// and the synced modes stepping once a bar instead of animating.
///
/// The spectrum is a fixed shape rather than anything time-driven, so renders
/// stay reproducible: bass-heavy like programme material, with a ripple so
/// neighbouring bands differ. Without it Audio Level and Audio Break
/// measurably do nothing offline and the sweep would report them dead.
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
/// Prove the host clock lands in seconds whatever unit the host speaks.
///
/// This is the gap that let a thousand-times-fast bug ship: every harness in
/// the fleet drove SetTime in SECONDS, Resolume drives it in MILLISECONDS, so
/// the path the users actually run was the one path nothing exercised. The
/// deltas below are fed in real time -- the calibration measures host time
/// against a steady_clock, so a test that raced through them would measure
/// nothing.
//---------------------------------------------------------------------------
int runClockTest()
{
	struct Case
	{
		const char* name;
		double perFrame;///< what the host adds per frame
		double expected;///< the scale it should settle on
	};
	const Case cases[] = {
		{ "milliseconds (Resolume)", 20.0, 0.001 },
		{ "seconds (harness)", 0.02, 1.0 },
	};

	int failures = 0;

	for( const Case& c : cases )
	{
		OutrunPlugin plugin;
		double host = 0.0;

		// Twelve frames at a real ~20 ms apart: comfortably more than the
		// four agreeing frames the calibration asks for, and slow enough
		// that the wall clock has something to measure.
		for( int frame = 0; frame < 12; ++frame )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
			host += c.perFrame;
			plugin.SetTime( host );
			plugin.TickClockForTest();
		}

		const double scale = plugin.ClockScaleForTest();
		const double secs  = plugin.HostSecondsForTest();

		// Twelve frames of 20 ms is about 0.24 s of programme time whichever
		// unit the host counts in. Loose bounds: the point is that it is not
		// out by a factor of a thousand.
		const bool scaleOk = std::abs( scale - c.expected ) < 1e-9;
		const bool timeOk  = secs > 0.05 && secs < 1.0;

		std::printf( "clock %-26s scale=%-6g seconds=%-8.4f %s\n",
		             c.name, scale, secs,
		             ( scaleOk && timeOk ) ? "ok" : "FAILED" );

		if( !scaleOk )
		{
			std::fprintf( stderr, "  expected scale %g, got %g\n", c.expected, scale );
			++failures;
		}
		if( !timeOk )
		{
			std::fprintf( stderr, "  %.4f seconds is not a plausible 0.24s of clock\n", secs );
			++failures;
		}
	}

	// And the arithmetic itself: a declared millisecond host and a declared
	// seconds host must put the clock in the same place for the same instant.
	{
		OutrunPlugin ms;
		ms.SetClockScaleForTest( 0.001 );
		ms.SetTime( 2500.0 );
		ms.TickClockForTest();

		OutrunPlugin sec;
		sec.SetClockScaleForTest( 1.0 );
		sec.SetTime( 2.5 );
		sec.TickClockForTest();

		const double a  = ms.HostSecondsForTest();
		const double b  = sec.HostSecondsForTest();
		const bool same = std::abs( a - b ) < 1e-9 && std::abs( a - 2.5 ) < 1e-9;
		std::printf( "clock %-26s ms=%.4f seconds=%.4f %s\n",
		             "2500ms == 2.5s", a, b, same ? "ok" : "FAILED" );
		if( !same )
			++failures;
	}

	std::printf( "%s\n", failures == 0 ? "clock: all ok" : "clock: FAILURES" );
	return failures == 0 ? 0 : 1;
}

void driveClock( OutrunPlugin& plugin, double seconds )
{
	constexpr double kBpm       = 120.0;
	constexpr double barSeconds = 240.0 / kBpm;

	// Say what unit we are speaking. The harness renders frames as fast as the
	// GPU allows, so the plugin's own calibration -- which measures host time
	// against real elapsed time -- has nothing to measure here. Declaring it
	// is not a workaround for the test's benefit: an absolute time in a single
	// frame really is ambiguous, and leaving the unit implicit is what let the
	// millisecond bug through in the first place.
	plugin.SetClockScaleForTest( 1.0 );
	plugin.SetTime( seconds );
	plugin.SetBeatInfo( static_cast< float >( kBpm ),
	                    static_cast< float >( std::fmod( seconds, barSeconds ) / barSeconds ) );

	for( int bin = 0; bin < kAudioBins; ++bin )
	{
		const float across = static_cast< float >( bin ) / static_cast< float >( kAudioBins - 1 );
		const float level  = 0.7f * ( 1.0f - across ) * ( 1.0f - across )
		                   + 0.2f * ( 0.5f + 0.5f * std::sin( 25.0f * across ) );
		plugin.SetParamElementValue( PT_AUDIO, static_cast< unsigned int >( bin ), level );
	}
}

//---------------------------------------------------------------------------
// --palettes: the GLSL lookup against the CPU bake, entry by entry.
//---------------------------------------------------------------------------
int runPaletteCheck()
{
	std::string error;
	const GLuint program = buildProbeProgram( error );
	if( program == 0 )
	{
		std::fprintf( stderr, "the palette probe would not build:\n%s\n", error.c_str() );
		return 1;
	}

	//The plugin's own upload path, replicated: the same bake, the same
	//GL_NEAREST float texture geometry.
	const std::vector< float > table = BakePaletteTable();
	GLuint paletteTexture            = 0;
	glGenTextures( 1, &paletteTexture );
	glBindTexture( GL_TEXTURE_2D, paletteTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, kPaletteSize, static_cast< GLsizei >( Palette::Count ), 0,
	              GL_RGBA, GL_FLOAT, table.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glBindTexture( GL_TEXTURE_2D, 0 );

	GLuint target = 0;
	glGenTextures( 1, &target );
	glBindTexture( GL_TEXTURE_2D, target );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, kPaletteSize, 1, 0, GL_RGBA, GL_FLOAT, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glBindTexture( GL_TEXTURE_2D, 0 );

	const GLuint fbo = makeFramebuffer( target );
	if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
	{
		std::fprintf( stderr, "could not make a float target for the probe\n" );
		return 1;
	}

	//A full-screen triangle, which needs no index buffer and no UVs.
	GLuint vao = 0, vbo = 0;
	glGenVertexArrays( 1, &vao );
	glBindVertexArray( vao );
	const float triangle[] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
	glGenBuffers( 1, &vbo );
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	glBufferData( GL_ARRAY_BUFFER, sizeof( triangle ), triangle, GL_STATIC_DRAW );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 0, nullptr );

	//The swatch palettes need swatches; awkward values on purpose, so a
	//swapped channel cannot cancel out.
	const Rgb colour1 = { 0.83f, 0.21f, 0.55f };
	const Rgb colour2 = { 0.07f, 0.64f, 0.91f };

	glUseProgram( program );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glViewport( 0, 0, kPaletteSize, 1 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, paletteTexture );
	glUniform1i( glGetUniformLocation( program, "PaletteTexture" ), 0 );
	glUniform3f( glGetUniformLocation( program, "Colour1" ), colour1.r, colour1.g, colour1.b );
	glUniform3f( glGetUniformLocation( program, "Colour2" ), colour2.r, colour2.g, colour2.b );

	std::vector< float > readback( static_cast< size_t >( kPaletteSize ) * 4 );

	int checks        = 0;
	int disagreements = 0;
	float worst       = 0.0f;
	std::string worstWhere;

	for( int palette = 0; palette < static_cast< int >( Palette::Count ); ++palette )
	{
		glUniform1f( glGetUniformLocation( program, "PaletteIndex" ), static_cast< float >( palette ) );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		glReadPixels( 0, 0, kPaletteSize, 1, GL_RGBA, GL_FLOAT, readback.data() );

		for( int x = 0; x < kPaletteSize; ++x )
		{
			const float position = static_cast< float >( x ) / 256.0f;

			//The CPU side of the mirror: the two special palettes from the
			//swatches, the rest from the same baked table via the same
			//two-entry interpolation the shader does.
			Rgb expected;
			if( palette == 0 )
				expected = colour1;
			else if( palette == 1 )
			{
				expected.r = colour1.r + ( colour2.r - colour1.r ) * position;
				expected.g = colour1.g + ( colour2.g - colour1.g ) * position;
				expected.b = colour1.b + ( colour2.b - colour1.b ) * position;
			}
			else
			{
				const float p = position * static_cast< float >( kPaletteSize - 1 );
				const int a   = static_cast< int >( std::floor( p ) );
				const int b   = std::min( a + 1, kPaletteSize - 1 );
				const float t = p - std::floor( p );
				const size_t offsetA = ( static_cast< size_t >( palette ) * kPaletteSize + a ) * 4;
				const size_t offsetB = ( static_cast< size_t >( palette ) * kPaletteSize + b ) * 4;
				expected.r = table[ offsetA + 0 ] + ( table[ offsetB + 0 ] - table[ offsetA + 0 ] ) * t;
				expected.g = table[ offsetA + 1 ] + ( table[ offsetB + 1 ] - table[ offsetA + 1 ] ) * t;
				expected.b = table[ offsetA + 2 ] + ( table[ offsetB + 2 ] - table[ offsetA + 2 ] ) * t;
			}

			const float dr = std::fabs( readback[ static_cast< size_t >( x ) * 4 + 0 ] - expected.r );
			const float dg = std::fabs( readback[ static_cast< size_t >( x ) * 4 + 1 ] - expected.g );
			const float db = std::fabs( readback[ static_cast< size_t >( x ) * 4 + 2 ] - expected.b );
			const float d  = std::max( dr, std::max( dg, db ) );

			++checks;
			if( d > worst )
			{
				worst = d;
				char where[ 128 ];
				std::snprintf( where, sizeof( where ), "%s entry %d",
				               PaletteName( static_cast< Palette >( palette ) ), x );
				worstWhere = where;
			}

			if( d <= kPaletteTolerance )
				continue;

			++disagreements;
			if( disagreements <= 12 )
				std::printf( "  %-14s entry %3d  got %.4f %.4f %.4f  expected %.4f %.4f %.4f\n",
				             PaletteName( static_cast< Palette >( palette ) ), x,
				             readback[ static_cast< size_t >( x ) * 4 + 0 ],
				             readback[ static_cast< size_t >( x ) * 4 + 1 ],
				             readback[ static_cast< size_t >( x ) * 4 + 2 ],
				             expected.r, expected.g, expected.b );
		}
	}

	glDeleteBuffers( 1, &vbo );
	glDeleteVertexArrays( 1, &vao );
	glDeleteFramebuffers( 1, &fbo );
	glDeleteTextures( 1, &target );
	glDeleteTextures( 1, &paletteTexture );
	glDeleteProgram( program );

	std::printf( "%d palettes, %d comparisons, %d disagreements past %g (worst %.3g at %s)\n",
	             static_cast< int >( Palette::Count ), checks, disagreements, kPaletteTolerance,
	             worst, worstWhere.c_str() );

	return disagreements == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --palettes-image
//---------------------------------------------------------------------------
int writePalettes( const std::string& path )
{
	const std::vector< float > table = BakePaletteTable();

	//Twelve rows per palette so the strip is thick enough to judge by eye, and
	//the two colour-driven palettes are left black on purpose: they are not
	//in the table, and a picture that invented something for them would be
	//claiming the table holds more than it does.
	constexpr int kRowHeight = 12;
	const int rows           = static_cast< int >( Palette::Count );
	const int height         = rows * kRowHeight;

	std::vector< unsigned char > image( static_cast< size_t >( kPaletteSize ) * height * 4, 0 );
	for( int row = 0; row < rows; ++row )
	{
		for( int band = 0; band < kRowHeight; ++band )
		{
			const int y = row * kRowHeight + band;
			for( int x = 0; x < kPaletteSize; ++x )
			{
				const size_t from = ( static_cast< size_t >( row ) * kPaletteSize + x ) * 4;
				const size_t to   = ( static_cast< size_t >( y ) * kPaletteSize + x ) * 4;
				for( int c = 0; c < 3; ++c )
					image[ to + c ] = static_cast< unsigned char >(
						std::min( 255.0f, std::max( 0.0f, table[ from + c ] ) * 255.0f ) );
				image[ to + 3 ] = 255;
			}
		}
	}

	if( !writePng( path, kPaletteSize, height, image ) )
	{
		std::fprintf( stderr, "could not write %s\n", path.c_str() );
		return 1;
	}

	std::printf( "wrote %s -- %d palettes, top to bottom:\n", path.c_str(), rows );
	for( int row = 0; row < rows; ++row )
		std::printf( "  %2d  %s\n", row, PaletteName( static_cast< Palette >( row ) ) );

	return 0;
}

//---------------------------------------------------------------------------
// Contact sheets. Both run the real plugin at a small size once per entry,
// tile the frames, and assert two things a human would otherwise have to
// keep noticing: every tile has strokes in it, and no two tiles are the same
// picture. "The same" is a mean absolute difference below a floor, not a
// hash: two paths could legitimately share a few pixels.
//---------------------------------------------------------------------------
struct SheetResult
{
	std::vector< std::vector< unsigned char > > tiles;
	std::vector< std::string > names;
};

double tileEnergy( const std::vector< unsigned char >& tile )
{
	double sum = 0.0;
	for( size_t i = 0; i < tile.size(); i += 4 )
		sum += tile[ i ] + tile[ i + 1 ] + tile[ i + 2 ];
	return sum / ( static_cast< double >( tile.size() / 4 ) * 3.0 * 255.0 );
}

double tileDifference( const std::vector< unsigned char >& a, const std::vector< unsigned char >& b )
{
	double sum = 0.0;
	for( size_t i = 0; i < a.size(); i += 4 )
		sum += std::abs( int( a[ i ] ) - int( b[ i ] ) )
		     + std::abs( int( a[ i + 1 ] ) - int( b[ i + 1 ] ) )
		     + std::abs( int( a[ i + 2 ] ) - int( b[ i + 2 ] ) );
	return sum / ( static_cast< double >( a.size() / 4 ) * 3.0 * 255.0 );
}

int writeSheet( const std::string& path, const SheetResult& sheet, int tileW, int tileH, int columns,
                const char* what )
{
	const int count = static_cast< int >( sheet.tiles.size() );
	const int rows  = ( count + columns - 1 ) / columns;
	std::vector< unsigned char > image(
		static_cast< size_t >( tileW * columns ) * static_cast< size_t >( tileH * rows ) * 4, 0 );

	for( int i = 0; i < count; ++i )
	{
		const int cx = ( i % columns ) * tileW;
		const int cy = ( i / columns ) * tileH;
		for( int y = 0; y < tileH; ++y )
		{
			const size_t to   = ( static_cast< size_t >( cy + y ) * ( tileW * columns ) + cx ) * 4;
			const size_t from = static_cast< size_t >( y ) * tileW * 4;
			std::memcpy( image.data() + to, sheet.tiles[ i ].data() + from, static_cast< size_t >( tileW ) * 4 );
		}
	}

	if( !writePng( path, tileW * columns, tileH * rows, image ) )
	{
		std::fprintf( stderr, "could not write %s\n", path.c_str() );
		return 1;
	}

	//The assertions. Empty means the entry is dead; identical means two
	//entries are wired to the same maths.
	int failures = 0;
	for( int i = 0; i < count; ++i )
	{
		const double energy = tileEnergy( sheet.tiles[ i ] );
		if( energy < 0.002 )
		{
			std::printf( "  FAIL  %s '%s' rendered black (energy %.5f)\n", what, sheet.names[ i ].c_str(), energy );
			++failures;
		}
	}
	for( int i = 0; i < count; ++i )
	{
		for( int j = i + 1; j < count; ++j )
		{
			const double difference = tileDifference( sheet.tiles[ i ], sheet.tiles[ j ] );
			if( difference < 0.001 )
			{
				std::printf( "  FAIL  %s '%s' and '%s' rendered the same picture (mad %.6f)\n",
				             what, sheet.names[ i ].c_str(), sheet.names[ j ].c_str(), difference );
				++failures;
			}
		}
	}

	std::printf( "wrote %s -- %d %ss%s\n", path.c_str(), count, what,
	             failures == 0 ? ", all live and distinct" : "" );
	return failures == 0 ? 0 : 1;
}
} // namespace

//---------------------------------------------------------------------------
int main( int argc, char** argv )
{
	std::string outPath = "/tmp/outrun.png";
	std::string cardPath;
	std::string paletteImagePath;
	std::string pathsSheetPath;
	std::string breaksSheetPath;
	std::string presetsSheetPath;
	std::string scriptPath;
	int width        = 1280;
	int height       = 720;
	int frames       = 30;
	double fps       = 60.0;
	double timeSeconds = -1.0;
	float noise      = 0.0f;
	bool wantList    = false;
	bool wantClock   = false;
	bool wantPalettes = false;
	bool wantPipe    = false;
	bool havePhase   = false;
	float phasePin   = 0.0f;
	std::vector< std::string > settings;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;

		if( argument == "--help" )
		{
			std::printf(
				"outruntest -- render and check the Outrun neon plugins\n"
				"\n"
				"  --out PATH            render a frame over the test card (default /tmp/outrun.png)\n"
				"  --card PATH           write the test card alone, undecorated\n"
				"  --width N             width (default 1280)\n"
				"  --height N            height (default 720)\n"
				"  --size WxH            both at once\n"
				"  --frames N            frames to render before reading back (default 30)\n"
				"  --time T              spread the frames over T seconds of clock instead\n"
				"  --clock               self-test the host clock unit calibration\n"
				"  --fps N               synthetic frame rate driving the animation (default 60)\n"
				"  --phase F             pin the driven phase (the Phase slider stays live)\n"
				"  --noise F             per-frame noise on the card, 0..1. What Stability is for.\n"
				"  --set \"Name=V\"        set a parameter by its display name. Repeatable.\n"
				"  --list                print every parameter and its default, then exit\n"
				"  --palettes            run the GLSL palette lookup against the C++ bake\n"
				"  --palettes-image PATH write the palette table as a picture\n"
				"  --paths PATH          contact sheet of every path, checked live and distinct\n"
				"  --breaks PATH         contact sheet of every break mode, likewise\n"
				"  --presets PATH        contact sheet of every factory preset, likewise\n"
				"  --bench               time a frame at 720p through 4K\n"
				"  --pipe                raw RGBA frames on stdin, raw RGBA frames on stdout\n"
				"  --script PATH         parameter cues for --pipe: 'frame Name Value'\n"
				"  --help\n" );
			return 0;
		}
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--card" && hasNext )
			cardPath = argv[ ++i ];
		else if( argument == "--palettes-image" && hasNext )
			paletteImagePath = argv[ ++i ];
		else if( argument == "--paths" && hasNext )
			pathsSheetPath = argv[ ++i ];
		else if( argument == "--breaks" && hasNext )
			breaksSheetPath = argv[ ++i ];
		else if( argument == "--presets" && hasNext )
			presetsSheetPath = argv[ ++i ];
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--width" && hasNext )
			width = std::atoi( argv[ ++i ] );
		else if( argument == "--height" && hasNext )
			height = std::atoi( argv[ ++i ] );
		else if( argument == "--size" && hasNext )
		{
			const std::string dims = argv[ ++i ];
			const size_t x         = dims.find( 'x' );
			if( x == std::string::npos )
			{
				std::fprintf( stderr, "--size expects WxH\n" );
				return 2;
			}
			width  = std::atoi( dims.substr( 0, x ).c_str() );
			height = std::atoi( dims.substr( x + 1 ).c_str() );
		}
		else if( argument == "--time" && hasNext )
			timeSeconds = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--frames" && hasNext )
			frames = std::atoi( argv[ ++i ] );
		else if( argument == "--fps" && hasNext )
			fps = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--phase" && hasNext )
		{
			havePhase = true;
			phasePin  = std::strtof( argv[ ++i ], nullptr );
		}
		else if( argument == "--noise" && hasNext )
			noise = std::strtof( argv[ ++i ], nullptr );
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--list" )
			wantList = true;
		else if( argument == "--clock" )
			wantClock = true;
		else if( argument == "--palettes" )
			wantPalettes = true;
		else if( argument == "--pipe" )
			wantPipe = true;
		else if( argument == "--bench" )
			settings.push_back( "__bench__" );//handled below, keeps the flag loop flat
		else
		{
			std::fprintf( stderr, "unknown argument: %s (try --help)\n", argument.c_str() );
			return 2;
		}
	}

	// Before any GL: the clock has nothing to do with the GPU, and a self-test
	// that needed a context would not run in CI.
	if( wantClock )
		return runClockTest();

	bool wantBench = false;
	settings.erase( std::remove_if( settings.begin(), settings.end(),
	                                [ & ]( const std::string& s ) {
		                                if( s == "__bench__" )
		                                {
			                                wantBench = true;
			                                return true;
		                                }
		                                return false;
	                                } ),
	                settings.end() );

	if( width <= 0 || height <= 0 || frames <= 0 || fps <= 0.0 )
	{
		std::fprintf( stderr, "width, height, frames and fps must all be positive\n" );
		return 2;
	}

	if( !cardPath.empty() )
	{
		const std::vector< unsigned char > card = buildCard( width, height );
		if( !writePng( cardPath, width, height, card ) )
		{
			std::fprintf( stderr, "could not write %s\n", cardPath.c_str() );
			return 1;
		}
		std::printf( "wrote %s\n", cardPath.c_str() );
		return 0;
	}

	//The palette image is pure C++ and needs no context, so it is answered
	//before one is made -- which also means it still works on a machine where
	//creating a GL context fails.
	if( !paletteImagePath.empty() )
		return writePalettes( paletteImagePath );

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL context\n" );
		return 1;
	}

	if( wantPalettes )
	{
		const int result = runPaletteCheck();
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return result;
	}

	//The contact sheets pick their own engine: paths on Engine B, breakaway
	//on Engine A over the card. Presets pick neither -- each one carries its
	//own engine, which is most of what a preset is for.
	if( !pathsSheetPath.empty() || !breaksSheetPath.empty() || !presetsSheetPath.empty() )
	{
		enum class Sheet
		{
			Paths,
			Breaks,
			Presets
		};
		const Sheet sheetKind = !pathsSheetPath.empty()  ? Sheet::Paths
		                      : !breaksSheetPath.empty() ? Sheet::Breaks
		                                                 : Sheet::Presets;
		const bool doPaths = sheetKind == Sheet::Paths;
		const int tileW = 480, tileH = 270;

		OutrunPlugin plugin;
		if( sheetKind != Sheet::Presets )
			plugin.SetFloatParameter( PT_ENGINE, doPaths ? 1.0f : 0.0f );
		plugin.SetPhaseOverride( 0.6f );

		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( tileW );
		viewport.height             = static_cast< FFUInt32 >( tileH );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see the diagnostics log for which shader\n" );
			return 1;
		}

		const std::vector< unsigned char > card = buildCard( tileW, tileH );
		const GLuint sourceTexture              = makeTexture( tileW, tileH, card.data() );
		const GLuint outputTexture              = makeTexture( tileW, tileH, nullptr );
		const GLuint outputFBO                  = makeFramebuffer( outputTexture );

		FFGLTextureStruct inputStruct = {};
		inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( tileW );
		inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( tileH );
		inputStruct.Handle                              = sourceTexture;
		FFGLTextureStruct* inputs[ 1 ]                  = { &inputStruct };

		ProcessOpenGLStruct process = {};
		process.numInputTextures    = 1;
		process.inputTextures       = inputs;
		process.HostFBO             = outputFBO;

		SheetResult sheet;
		const int count = sheetKind == Sheet::Paths     ? static_cast< int >( Path::Count )
		                : sheetKind == Sheet::Breaks    ? 6
		                                                 : presets::kCount;

		for( int entry = 0; entry < count; ++entry )
		{
			if( sheetKind == Sheet::Paths )
			{
				plugin.SetFloatParameter( PT_PATH, static_cast< float >( entry ) );
				sheet.names.push_back( PathName( static_cast< Path >( entry ) ) );
			}
			else if( sheetKind == Sheet::Breaks )
			{
				plugin.SetFloatParameter( PT_BREAK_MODE, static_cast< float >( entry ) );
				plugin.SetFloatParameter( PT_BREAK_AMOUNT, entry == 0 ? 0.0f : 0.7f );
				static const char* const kBreakNames[] = { "None", "Echo", "Angular", "Scan", "Flow", "Rays" };
				sheet.names.push_back( kBreakNames[ entry ] );
			}
			else
			{
				//Element 0 of the host dropdown is Custom, so preset i is
				//element i + 1. Going through SetFloatParameter rather than
				//poking params is the point: it is the path the host takes,
				//so the sheet also proves applyPreset's copy and its history
				//invalidation, not just the numbers in the table.
				plugin.SetFloatParameter( PT_PRESET, static_cast< float >( entry + 1 ) );
				sheet.names.push_back( presets::kPresets[ entry ].name );
			}

			//A few frames, not one: the effect's temporal filter needs to
			//settle, and both variants deserve a converged picture.
			for( int frame = 0; frame < 12; ++frame )
			{
				driveClock( plugin, static_cast< double >( frame ) / fps );
				glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
				glViewport( 0, 0, tileW, tileH );
				glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
				glClear( GL_COLOR_BUFFER_BIT );
				if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
				{
					std::fprintf( stderr, "ProcessOpenGL failed on '%s'\n", sheet.names.back().c_str() );
					return 1;
				}
			}

			sheet.tiles.push_back( flipRows( readBackRaw( outputFBO, tileW, tileH ), tileW, tileH ) );
		}

		plugin.DeInitGL();
		const std::string sheetPath = sheetKind == Sheet::Paths  ? pathsSheetPath
		                            : sheetKind == Sheet::Breaks ? breaksSheetPath
		                                                          : presetsSheetPath;
		const int columns = sheetKind == Sheet::Breaks ? 3 : 4;
		const char* const what = sheetKind == Sheet::Paths  ? "path"
		                       : sheetKind == Sheet::Breaks ? "break mode"
		                                                     : "preset";
		const int result = writeSheet( sheetPath, sheet, tileW, tileH, columns, what );
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return result;
	}

	OutrunPlugin plugin;

	if( havePhase )
		plugin.SetPhaseOverride( phasePin );

	for( const std::string& setting : settings )
	{
		std::string error;
		if( applySetting( plugin, setting, error ) )
			continue;
		std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
		return 2;
	}

	if( wantList )
	{
		std::printf( "%-3s %-16s %-9s %s\n", "id", "name", "kind", "default" );
		for( const NamedParameter& parameter : listParameters( plugin ) )
			std::printf( "%-3u %-16s %-9s %.4f\n", parameter.index, parameter.name.c_str(),
			             parameter.kind.c_str(), parameter.value );
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return 0;
	}

	FFGLViewportStruct viewport = {};
	viewport.width              = static_cast< FFUInt32 >( width );
	viewport.height             = static_cast< FFUInt32 >( height );

	if( plugin.InitGL( &viewport ) != FF_SUCCESS )
	{
		std::fprintf( stderr, "InitGL failed -- see the diagnostics log for which shader\n" );
		return 1;
	}

	const std::vector< unsigned char > card = buildCard( width, height );
	GLuint sourceTexture                    = makeTexture( width, height, card.data() );
	GLuint outputTexture                    = makeTexture( width, height, nullptr );
	const GLuint outputFBO                  = makeFramebuffer( outputTexture );

	FFGLTextureStruct inputStruct = {};
	inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
	inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
	inputStruct.Handle                              = sourceTexture;
	FFGLTextureStruct* inputs[ 1 ]                  = { &inputStruct };

	ProcessOpenGLStruct process = {};
	process.numInputTextures    = 1;
	process.inputTextures       = inputs;
	process.HostFBO             = outputFBO;

	if( wantBench )
	{
		//Time one frame at each broadcast size. glFinish on both sides --
		//without it this times how fast the driver accepts commands -- and a
		//warm-up thrown away, because the first frames pay for allocation and
		//the temporal filter's convergence.
		struct Size
		{
			const char* name;
			int w, h;
		};
		const Size sizes[] = {
			{ "1280x720  ", 1280, 720 },
			{ "1920x1080 ", 1920, 1080 },
			{ "2560x1440 ", 2560, 1440 },
			{ "3840x2160 ", 3840, 2160 },
		};

		std::printf( "%d frames each, after a 20-frame warm-up, glFinish both sides.\n\n", frames );
		std::printf( "resolution     ms/frame   equivalent fps   %% of a 60fps frame\n" );

		for( const Size& size : sizes )
		{
			const std::vector< unsigned char > benchCard = buildCard( size.w, size.h );
			const GLuint benchSource = makeTexture( size.w, size.h, benchCard.data() );
			const GLuint benchTarget = makeTexture( size.w, size.h, nullptr );
			const GLuint benchFBO    = makeFramebuffer( benchTarget );

			FFGLTextureStruct benchInput = {};
			benchInput.Width = benchInput.HardwareWidth = static_cast< FFUInt32 >( size.w );
			benchInput.Height = benchInput.HardwareHeight = static_cast< FFUInt32 >( size.h );
			benchInput.Handle                             = benchSource;
			FFGLTextureStruct* benchInputs[ 1 ]           = { &benchInput };

			ProcessOpenGLStruct benchProcess = {};
			benchProcess.numInputTextures    = 1;
			benchProcess.inputTextures       = benchInputs;
			benchProcess.HostFBO             = benchFBO;

			//Re-initialised per size so every resolution starts cold.
			plugin.DeInitGL();
			FFGLViewportStruct benchViewport = {};
			benchViewport.width              = static_cast< FFUInt32 >( size.w );
			benchViewport.height             = static_cast< FFUInt32 >( size.h );
			if( plugin.InitGL( &benchViewport ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "InitGL failed at %s\n", size.name );
				return 1;
			}

			auto renderOne = [ & ]( int frame ) {
				driveClock( plugin, static_cast< double >( frame ) / fps );
				glBindFramebuffer( GL_FRAMEBUFFER, benchFBO );
				glViewport( 0, 0, size.w, size.h );
				plugin.ProcessOpenGL( &benchProcess );
			};

			for( int frame = 0; frame < 20; ++frame )
				renderOne( frame );
			glFinish();

			const auto start = std::chrono::steady_clock::now();
			for( int frame = 0; frame < frames; ++frame )
				renderOne( 20 + frame );
			glFinish();
			const auto end = std::chrono::steady_clock::now();

			const double ms = std::chrono::duration< double >( end - start ).count() * 1000.0
			                / static_cast< double >( frames );
			std::printf( "%s    %7.3f       %8.0f            %5.1f%%\n",
			             size.name, ms, ms > 0.0 ? 1000.0 / ms : 0.0, ms / 16.667 * 100.0 );

			glDeleteFramebuffers( 1, &benchFBO );
			glDeleteTextures( 1, &benchTarget );
			glDeleteTextures( 1, &benchSource );
		}

		plugin.DeInitGL();
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return 0;
	}

	if( wantPipe )
	{
		//Raw RGBA in, raw RGBA out, one frame at a time. The script's names
		//are resolved to indices up front, and an unknown name refuses to
		//run: a misspelled cue that silently did nothing would produce a take
		//that looks deliberate and is wrong.
		using Track = std::vector< std::pair< int, float > >;

		std::map< unsigned int, Track > automation;
		if( !scriptPath.empty() )
		{
			std::map< std::string, Track > tracks;
			std::ifstream file( scriptPath );
			if( !file )
			{
				std::fprintf( stderr, "cannot open %s\n", scriptPath.c_str() );
				return 2;
			}

			std::string line;
			while( std::getline( file, line ) )
			{
				const size_t hash = line.find( '#' );
				if( hash != std::string::npos )
					line.erase( hash );
				std::istringstream in( line );

				int frame = 0;
				if( !( in >> frame ) )
					continue;

				//The name is everything up to the last token: parameters have
				//spaces in them ("Break Amount") and the value never does.
				std::vector< std::string > words;
				std::string word;
				while( in >> word )
					words.push_back( word );
				if( words.size() < 2 )
				{
					std::fprintf( stderr, "%s: expected `frame Parameter Name value`\n", scriptPath.c_str() );
					return 2;
				}

				const float value = std::strtof( words.back().c_str(), nullptr );
				words.pop_back();
				std::string name = words.front();
				for( size_t w = 1; w < words.size(); ++w )
					name += " " + words[ w ];

				tracks[ name ].emplace_back( frame, value );
			}

			const std::vector< NamedParameter > known = listParameters( plugin );
			for( auto& entry : tracks )
			{
				std::sort( entry.second.begin(), entry.second.end() );
				bool found = false;
				for( const NamedParameter& parameter : known )
				{
					if( parameter.name != entry.first )
						continue;
					automation[ parameter.index ] = entry.second;
					found                         = true;
					break;
				}
				if( !found )
				{
					std::fprintf( stderr, "script names '%s', which is not a parameter (try --list)\n",
					              entry.first.c_str() );
					return 2;
				}
			}
		}

		auto valueAt = []( const Track& track, int frame ) {
			if( track.empty() )
				return 0.0f;
			if( frame <= track.front().first )
				return track.front().second;
			if( frame >= track.back().first )
				return track.back().second;
			for( size_t i = 1; i < track.size(); ++i )
			{
				if( frame <= track[ i ].first )
				{
					const auto& a    = track[ i - 1 ];
					const auto& b    = track[ i ];
					const float span = static_cast< float >( b.first - a.first );
					const float t    = span > 0.0f ? ( static_cast< float >( frame - a.first ) / span ) : 1.0f;
					return a.second + ( b.second - a.second ) * t;
				}
			}
			return track.back().second;
		};

		std::vector< unsigned char > frame( static_cast< size_t >( width ) * height * 4 );

		for( int index = 0;; ++index )
		{
			size_t filled = 0;
			while( filled < frame.size() )
			{
				const ssize_t got = read( STDIN_FILENO, frame.data() + filled, frame.size() - filled );
				if( got <= 0 )
					break;
				filled += static_cast< size_t >( got );
			}
			if( filled < frame.size() )
				break;

			for( const auto& track : automation )
				plugin.SetFloatParameter( track.first, valueAt( track.second, index ) );

			//The same synthetic clock as the still path, so a filmed sequence
			//advances at the rate it will be played back at rather than at
			//whatever rate the pipe happens to deliver.
			driveClock( plugin, static_cast< double >( index ) / fps );

			//Flipped on the way in: a raw frame arrives top row first and GL
			//wants bottom row first.
			const std::vector< unsigned char > flipped = flipRows( frame, width, height );
			glBindTexture( GL_TEXTURE_2D, sourceTexture );
			glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data() );
			glBindTexture( GL_TEXTURE_2D, 0 );

			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
				break;

			const std::vector< unsigned char > out = flipRows( readBackRaw( outputFBO, width, height ), width, height );
			size_t written                         = 0;
			while( written < out.size() )
			{
				const ssize_t put = write( STDOUT_FILENO, out.data() + written, out.size() - written );
				if( put <= 0 )
					break;
				written += static_cast< size_t >( put );
			}
		}

		plugin.DeInitGL();
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return 0;
	}

	//A still. Several frames, not one: the effect's temporal filter needs a
	//few to settle, and a picture taken on frame zero is a picture of the
	//history buffer being empty rather than of the plugin. `--time` spreads
	//the frames over a chosen duration instead of the synthetic frame rate,
	//which is how the sweep proves Speed and Sync while everything else runs
	//pinned.
	for( int frame = 0; frame < frames; ++frame )
	{
		const double seconds = timeSeconds > 0.0
		                       ? timeSeconds * static_cast< double >( frame ) / std::max( 1, frames - 1 )
		                       : static_cast< double >( frame ) / fps;
		driveClock( plugin, seconds );

		if( noise > 0.0f )
		{
			const std::vector< unsigned char > noisy = addNoise( card, frame, noise );
			glBindTexture( GL_TEXTURE_2D, sourceTexture );
			glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, noisy.data() );
			glBindTexture( GL_TEXTURE_2D, 0 );
		}

		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glViewport( 0, 0, width, height );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "ProcessOpenGL failed on frame %d\n", frame );
			return 1;
		}
	}

	const std::vector< unsigned char > image = flipRows( readBackRaw( outputFBO, width, height ), width, height );
	if( !writePng( outPath, width, height, image ) )
	{
		std::fprintf( stderr, "could not write %s\n", outPath.c_str() );
		return 1;
	}

	std::printf( "wrote %s (%dx%d, %d frames)\n", outPath.c_str(), width, height, frames );

	plugin.DeInitGL();
	glDeleteFramebuffers( 1, &outputFBO );
	glDeleteTextures( 1, &outputTexture );
	glDeleteTextures( 1, &sourceTexture );
	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return 0;
}
