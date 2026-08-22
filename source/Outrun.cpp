#include "Outrun.h"

#include "Diag.h"
#include "Palette.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name. The symptom without it is an
//unknown-type error on ScopedFBOBinding and nothing else.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

using namespace ffglex;

namespace outrun
{
namespace
{
/// glGetString returns nullptr when there is no current context, and feeding
/// that to std::string is undefined behaviour. A logging call must never be
/// the thing that brings the host down.
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

const char* const kEngineNames[]     = { "A: Trace", "B: Paths" };
const char* const kSourceNames[]     = { "Luma", "Alpha", "Chroma", "Luma or Alpha" };
const char* const kTraceNames[]      = { "Spiral", "Angle", "Linear", "Radial" };
const char* const kBreakNames[]      = { "None", "Echo", "Angular", "Scan", "Flow", "Rays" };
const char* const kColourModeNames[] = { "Palette", "Clip", "Clip x Palette" };
const char* const kSyncNames[]       = { "Free", "Beat", "Bar", "Manual" };
const char* const kBackgroundNames[] = { "Black", "Source", "Dimmed Source", "Transparent", "Edges" };

constexpr int kEngineCount     = 2;
constexpr int kSourceCount     = 4;
constexpr int kTraceCount      = 4;
constexpr int kBreakCount      = 6;
constexpr int kColourModeCount = 3;
constexpr int kSyncCount       = 4;
constexpr int kBackgroundCount = 5;

constexpr float kTau = 6.2831853071795864f;

/// Seconds of host time a single frame is allowed to advance the animation
/// by. The host's clock is not ours: it jumps when the composition is
/// scrubbed, when a clip is retriggered, and by however long the machine was
/// asleep. An unclamped delta turns any of those into the grid lurching
/// forward by minutes.
constexpr double kMaxFrameDelta = 0.25;

/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;

/// Wall clock, for hosts that never call SetTime. Steady rather than system,
/// so nothing here moves when the machine's clock is corrected.
double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}
} // namespace

// The buttons are declared one per link, so the run in the enum and the run the
// block actually has must agree. They diverge the day somebody writes a user
// guide, and this is what says so.
static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount,
               "the About run no longer matches StoatworksAbout.h -- "
               "add or remove a PT_ABOUT_BUTTON_n to match" );

OutrunPlugin::OutrunPlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//The host drives the animation where it can, so that rendering the same
	//frame twice gives the same picture twice and an export matches the
	//preview.
	SetTimeSupported( true );

	//---------------------------------------------------------------------
	// Defaults. SetParamInfof reads each one back out of GetFloatParameter,
	// so these assignments are what the host is told the defaults are.
	//
	// They add up to: Engine A tracing the clip's outline as a pink neon
	// tube -- recognisable before a single slider moves -- with Engine B one
	// dropdown away, opening on the Miami perspective grid.
	//---------------------------------------------------------------------
	params[ PT_ENGINE ]      = 0.0f;//Engine A: trace whatever is on the layer
	params[ PT_SOURCE ]      = 3.0f;//Luma or Alpha -- right for artwork either way
	params[ PT_SENSITIVITY ] = 0.60f;
	params[ PT_SOFTNESS ]    = 0.35f;
	params[ PT_DETAIL ]      = 0.15f;
	params[ PT_STABILITY ]   = 0.35f;

	params[ PT_PATH ]        = 0.0f;//Grid
	params[ PT_PATH_SCALE ]  = 0.5f;
	params[ PT_PATH_DETAIL ] = 0.4f;
	params[ PT_HORIZON ]     = 0.5f;

	params[ PT_WIDTH ]       = 0.35f;
	params[ PT_CORE ]        = 0.5f;
	params[ PT_TRACE ]       = 1.0f;//Angle: once round the artwork is one palette run
	params[ PT_TRACE_ANGLE ] = 0.0f;

	params[ PT_BREAK_MODE ]   = 0.0f;//None: the faithful outline until asked otherwise
	params[ PT_BREAK_AMOUNT ] = 0.0f;
	params[ PT_BREAK_SPREAD ] = 0.4f;
	params[ PT_BREAK_HUE ]    = 0.15f;

	params[ PT_COLOUR_MODE ] = 0.0f;//Palette
	params[ PT_PALETTE ]     = static_cast< float >( Palette::Miami );
	params[ PT_SPREAD ]      = 0.40f;
	params[ PT_C1_R ]        = 1.00f;//hot pink
	params[ PT_C1_G ]        = 0.20f;
	params[ PT_C1_B ]        = 0.80f;
	params[ PT_C2_R ]        = 0.10f;//electric cyan
	params[ PT_C2_G ]        = 0.90f;
	params[ PT_C2_B ]        = 1.00f;
	params[ PT_SATURATION ]  = 0.667f;//1.0 after mapping
	params[ PT_BRIGHTNESS ]  = 0.50f; //1.0 after mapping

	params[ PT_SYNC ]  = 0.0f;//Free
	params[ PT_SPEED ] = 0.25f;
	params[ PT_PHASE ] = 0.0f;

	params[ PT_AUDIO_LEVEL ] = 0.0f;
	params[ PT_AUDIO_BREAK ] = 0.0f;

	params[ PT_GLOW ]       = 0.55f;
	params[ PT_GLOW_SIZE ]  = 0.40f;
	params[ PT_BACKGROUND ] = 0.0f;//Black
	params[ PT_DIM ]        = 0.25f;
	params[ PT_MIX ]        = 1.0f;

	params[ PT_PRESET ] = 0.0f;//Custom: the sliders are the truth

	//---------------------------------------------------------------------
	// Declaration. Every numeric parameter is a plain 0..1 float even where
	// it stands for a width in pixels or a mip level; SetParamInfo clamps an
	// FF_TYPE_STANDARD default into 0..1 *before* a range can be attached
	// (SDK b1afaf9). The conversions live in Controls.cpp.
	//
	// Both engines declare everything; the inactive engine's group is
	// simply ignored, so switching engines never shifts the parameter list.
	//---------------------------------------------------------------------
	SetOptionParamInfo( PT_ENGINE, "Engine", kEngineCount, params[ PT_ENGINE ] );
	for( int i = 0; i < kEngineCount; ++i )
		SetParamElementInfo( PT_ENGINE, i, kEngineNames[ i ], static_cast< float >( i ) );

	SetOptionParamInfo( PT_SOURCE, "Detect On", kSourceCount, params[ PT_SOURCE ] );
	for( int i = 0; i < kSourceCount; ++i )
		SetParamElementInfo( PT_SOURCE, i, kSourceNames[ i ], static_cast< float >( i ) );

	SetParamInfof( PT_SENSITIVITY, "Sensitivity", FF_TYPE_STANDARD );
	SetParamInfof( PT_SOFTNESS, "Softness", FF_TYPE_STANDARD );
	SetParamInfof( PT_DETAIL, "Detail", FF_TYPE_STANDARD );
	SetParamInfof( PT_STABILITY, "Stability", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_TRACE, "Trace", kTraceCount, params[ PT_TRACE ] );
	for( int i = 0; i < kTraceCount; ++i )
		SetParamElementInfo( PT_TRACE, i, kTraceNames[ i ], static_cast< float >( i ) );

	SetParamInfof( PT_TRACE_ANGLE, "Direction", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_PATH, "Path", static_cast< int >( Path::Count ), params[ PT_PATH ] );
	for( int i = 0; i < static_cast< int >( Path::Count ); ++i )
		SetParamElementInfo( PT_PATH, i, PathName( static_cast< Path >( i ) ), static_cast< float >( i ) );

	SetParamInfof( PT_PATH_SCALE, "Path Size", FF_TYPE_STANDARD );
	SetParamInfof( PT_PATH_DETAIL, "Path Detail", FF_TYPE_STANDARD );
	SetParamInfof( PT_HORIZON, "Horizon", FF_TYPE_STANDARD );

	SetParamInfof( PT_WIDTH, "Width", FF_TYPE_STANDARD );
	SetParamInfof( PT_CORE, "Core", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_BREAK_MODE, "Break Mode", kBreakCount, params[ PT_BREAK_MODE ] );
	for( int i = 0; i < kBreakCount; ++i )
		SetParamElementInfo( PT_BREAK_MODE, i, kBreakNames[ i ], static_cast< float >( i ) );

	SetParamInfof( PT_BREAK_AMOUNT, "Break Amount", FF_TYPE_STANDARD );
	SetParamInfof( PT_BREAK_SPREAD, "Break Spread", FF_TYPE_STANDARD );
	SetParamInfof( PT_BREAK_HUE, "Break Hue", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_COLOUR_MODE, "Colour Mode", kColourModeCount, params[ PT_COLOUR_MODE ] );
	for( int i = 0; i < kColourModeCount; ++i )
		SetParamElementInfo( PT_COLOUR_MODE, i, kColourModeNames[ i ], static_cast< float >( i ) );

	SetOptionParamInfo( PT_PALETTE, "Palette", static_cast< int >( Palette::Count ), params[ PT_PALETTE ] );
	for( int i = 0; i < static_cast< int >( Palette::Count ); ++i )
		SetParamElementInfo( PT_PALETTE, i, PaletteName( static_cast< Palette >( i ) ), static_cast< float >( i ) );

	SetParamInfof( PT_SPREAD, "Spread", FF_TYPE_STANDARD );

	//Consecutive red/green/blue parameters are what a host needs to show a
	//colour swatch instead of three sliders, so the naming follows the SDK's
	//own convention rather than being tidied up.
	SetParamInfof( PT_C1_R, "Colour 1", FF_TYPE_RED );
	SetParamInfof( PT_C1_G, "Colour1_Green", FF_TYPE_GREEN );
	SetParamInfof( PT_C1_B, "Colour1_Blue", FF_TYPE_BLUE );
	SetParamInfof( PT_C2_R, "Colour 2", FF_TYPE_RED );
	SetParamInfof( PT_C2_G, "Colour2_Green", FF_TYPE_GREEN );
	SetParamInfof( PT_C2_B, "Colour2_Blue", FF_TYPE_BLUE );

	SetParamInfof( PT_SATURATION, "Saturation", FF_TYPE_STANDARD );
	SetParamInfof( PT_BRIGHTNESS, "Brightness", FF_TYPE_STANDARD );

	// What Speed means: cycles per second (Free), cycles per beat or bar
	// phase-locked to the host's BPM clock, or nothing at all (Manual, where
	// the Phase slider is the only driver).
	SetOptionParamInfo( PT_SYNC, "Sync", kSyncCount, params[ PT_SYNC ] );
	for( int i = 0; i < kSyncCount; ++i )
		SetParamElementInfo( PT_SYNC, i, kSyncNames[ i ], static_cast< float >( i ) );

	SetParamInfof( PT_SPEED, "Speed", FF_TYPE_STANDARD );
	SetParamInfof( PT_PHASE, "Phase", FF_TYPE_STANDARD );

	// Audio. An FFT buffer: Resolume shows it as an audio-source picker and
	// writes one spectrum bin per element. Element defaults are zero on
	// purpose -- with no audio routed, Audio Level does nothing rather than
	// the strokes twitching to a phantom signal.
	SetBufferParamInfo( PT_AUDIO, "Audio", kAudioBins, FF_USAGE_FFT );
	for( int i = 0; i < kAudioBins; ++i )
		SetParamElementInfo( PT_AUDIO, i, "", 0.0f );

	SetParamInfof( PT_AUDIO_LEVEL, "Audio Level", FF_TYPE_STANDARD );
	SetParamInfof( PT_AUDIO_BREAK, "Audio Break", FF_TYPE_STANDARD );

	SetParamInfof( PT_GLOW, "Glow", FF_TYPE_STANDARD );
	SetParamInfof( PT_GLOW_SIZE, "Glow Size", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_BACKGROUND, "Background", kBackgroundCount, params[ PT_BACKGROUND ] );
	for( int i = 0; i < kBackgroundCount; ++i )
		SetParamElementInfo( PT_BACKGROUND, i, kBackgroundNames[ i ], static_cast< float >( i ) );

	SetParamInfof( PT_DIM, "Dim", FF_TYPE_STANDARD );
	SetParamInfof( PT_MIX, "Mix", FF_TYPE_STANDARD );

	// Factory presets. Element 0 is Custom; picking anything else copies that
	// preset's values into the covered parameters and raises value events so
	// the host re-reads the sliders. Editing a covered slider flips back to
	// Custom.
	SetOptionParamInfo( PT_PRESET, "Preset", 1 + presets::kCount, params[ PT_PRESET ] );
	SetParamElementInfo( PT_PRESET, 0, "Custom", 0.0f );
	for( int i = 0; i < presets::kCount; ++i )
		SetParamElementInfo( PT_PRESET, 1 + i, presets::kPresets[ i ].name, static_cast< float >( 1 + i ) );

	//---------------------------------------------------------------------
	// Groups. SetParamGroup collapses *runs* of consecutive same-group ids,
	// which is why the ids in Controls.h have to stay in this order.
	//---------------------------------------------------------------------
	SetParamGroup( PT_ENGINE, "Engine" );
	for( unsigned int id = PT_SOURCE; id <= PT_TRACE_ANGLE; ++id )
		SetParamGroup( id, "Engine A - Trace" );
	for( unsigned int id = PT_PATH; id <= PT_HORIZON; ++id )
		SetParamGroup( id, "Engine B - Paths" );
	for( unsigned int id = PT_WIDTH; id <= PT_CORE; ++id )
		SetParamGroup( id, "Stroke" );
	for( unsigned int id = PT_BREAK_MODE; id <= PT_BREAK_HUE; ++id )
		SetParamGroup( id, "Breakaway" );
	for( unsigned int id = PT_COLOUR_MODE; id <= PT_BRIGHTNESS; ++id )
		SetParamGroup( id, "Colour" );
	for( unsigned int id = PT_SYNC; id <= PT_PHASE; ++id )
		SetParamGroup( id, "Tempo" );
	for( unsigned int id = PT_AUDIO; id <= PT_AUDIO_BREAK; ++id )
		SetParamGroup( id, "Audio" );
	for( unsigned int id = PT_GLOW; id <= PT_MIX; ++id )
		SetParamGroup( id, "Output" );
	SetParamGroup( PT_PRESET, "Preset" );
	// The About block. Declared inline rather than through a helper, because
	// SetParamInfo is protected on CFFGLPlugin and nothing outside the class
	// can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
		SetParamGroup( id, "About" );


	FFGLLog::LogToHost( "Created Outrun effect" );

	diag::init();
}

//---------------------------------------------------------------------------
bool OutrunPlugin::UploadPalettes()
{
	const std::vector< float > table = BakePaletteTable();

	glGenTextures( 1, &paletteTexture );
	if( paletteTexture == 0 )
		return false;

	Scoped2DTextureBinding binding( paletteTexture );

	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F,
	              kPaletteSize, static_cast< GLsizei >( Palette::Count ), 0,
	              GL_RGBA, GL_FLOAT, table.data() );

	//Addressed by texelFetch only. Nothing may be interpolated -- one texture
	//has one filter for both axes, and bilinear here would blend each palette
	//into the one below it.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	return true;
}

//---------------------------------------------------------------------------
FFResult OutrunPlugin::InitGL( const FFGLViewportStruct* vp )
{
	//The GL strings first, and unconditionally: when a shader will not
	//compile it is almost always the driver or the GL version, and knowing
	//which machine reported what is most of the diagnosis.
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR )
	            + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	//The stroke pass is assembled rather than written out -- both engines in
	//one program. Held in a local so the pointer handed to Compile outlives
	//the call.
	const std::string strokeSource = StrokeShaderSource();

	struct Stage
	{
		FFGLShader* shader;
		const char* fragment;
		const char* name;
	};
	const Stage stages[] = {
		{ &copyShader, kCopyShader, "copy" },
		{ &edgeShader, kEdgeShader, "edge" },
		{ &stabiliseShader, kStabiliseShader, "stabilise" },
		{ &strokeShader, strokeSource.c_str(), "stroke" },
		{ &blurShader, kBlurShader, "blur" },
		{ &compositeShader, kCompositeShader, "composite" },
	};

	for( const Stage& stage : stages )
	{
		if( stage.shader->Compile( kVertexShader, stage.fragment ) )
			continue;

		//Returning FF_FAIL here is invisible to the operator: the plugin
		//simply does nothing in Resolume, with no message anywhere. These two
		//lines are the only record of which pass it was.
		diag::error( std::string( "the " ) + stage.name
		             + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Outrun: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !UploadPalettes() )
	{
		diag::error( "could not upload the palette table" );
		DeInitGL();
		return FF_FAIL;
	}

	historyValid = false;

	diag::info( "initialised, " + std::to_string( static_cast< int >( Path::Count ) ) + " paths and "
	            + std::to_string( static_cast< int >( Palette::Count ) ) + " palettes" );

	//Use base-class init as the success result so it retains the viewport --
	//the source variant sizes itself from it.
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
void OutrunPlugin::UpdateClock()
{
	// FFGL never says what unit SetTime arrives in, and hosts disagree:
	// Resolume sends MILLISECONDS (measured live at 20.0 per frame at its
	// 50 fps, and the SDK's own Particles sample divides by 1000), while the
	// offline harness sends seconds. Reading it raw is a thousand times fast
	// on the one host that matters and exactly right on the one that gets
	// tested, which is how it stays hidden.
	//
	// The fleet used to guess the unit from the magnitude of a single frame
	// delta. That had three holes: a delta between 0.5 and 2.0 decided
	// nothing, a burst of sub-0.5 ms frames at load -- a thumbnail render on
	// a quick GPU -- locked it to "seconds" for the rest of the session, and
	// while undecided it assumed seconds, which is precisely the millisecond
	// host's wrong answer.
	//
	// So measure instead of guessing. steady_clock says how much real time
	// passed, the host says how much host time passed, and the ratio names
	// the unit outright. Nothing plausible sits between 1 and 1000, so both
	// acceptance bands are wide and a frame fitting neither simply does not
	// vote.
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;

	const double raw = hostTime;

	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;

		// A paused host, a looping clip or a stalled frame tells us nothing.
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;

			// Several frames rather than one, so a single odd frame -- the
			// first after a seek, say -- cannot decide it on its own.
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale = millisVotes > secondsVotes ? 0.001 : 1.0;
				diag::info( std::string( "host clock is " )
				            + ( clockScale == 0.001 ? "milliseconds" : "seconds" )
				            + ", scale=" + std::to_string( clockScale ) );
			}
		}
	}

	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	// Until the unit is settled -- and for a host that never calls SetTime at
	// all -- run on the real clock. Wrong in origin but right in rate, where
	// assuming seconds would be a thousand times fast on Resolume.
	hostSeconds = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale
	                                                  : wallNow - wallStart;
}

//---------------------------------------------------------------------------
void OutrunPlugin::SetClockScaleForTest( double scale )
{
	clockScale = scale;
}

void OutrunPlugin::TickClockForTest()
{
	UpdateClock();
}

double OutrunPlugin::ClockScaleForTest() const
{
	return clockScale;
}

double OutrunPlugin::HostSecondsForTest() const
{
	return hostSeconds;
}

//---------------------------------------------------------------------------
float OutrunPlugin::AdvancePhase()
{
	const double now  = hostSeconds;
	const int    sync = static_cast< int >( std::lround( params[ PT_SYNC ] ) );
	const double speed = static_cast< double >( SpeedFromParam( params[ PT_SPEED ] ) );
	const float  manual = params[ PT_PHASE ];

	// Pinning replaces the CLOCK, not the whole phase. The Phase slider stays
	// live underneath it, which is what lets tools/sweep.py prove that slider
	// is connected -- a pin that swallowed it too would make it look dead.
	if( phasePinned )
		return pinnedPhase + manual;

	if( sync == static_cast< int >( Sync::Beat ) || sync == static_cast< int >( Sync::Bar ) )
	{
		// The host hands us a tempo and a position *within* the current bar,
		// never which bar it is. Recover a continuous bar number without
		// keeping state: the clock estimates how many bars have passed,
		// `barPhase` gives the exact position inside this one, and the whole
		// number reconciling them is round( estimate - barPhase ). Continuous
		// across the bar line -- as barPhase wraps 1 to 0 the rounded integer
		// steps up at the same moment -- and exact while the clock estimate
		// is within half a bar. (Same recovery as orrery's and tinsel's.)
		const double tempo      = bpm > 1.0f ? static_cast< double >( bpm ) : 120.0;
		const double barSeconds = 240.0 / tempo;//four beats to the bar
		const double estimate   = now / barSeconds;
		const double within     = std::clamp( static_cast< double >( barPhase ), 0.0, 1.0 );

		const double bars = within + std::round( estimate - within );

		phase = ( sync == static_cast< int >( Sync::Beat ) ? bars * 4.0 : bars ) * speed;
	}
	else if( sync == static_cast< int >( Sync::Manual ) )
	{
		// Speed is deliberately ignored: this is the mode for driving Phase
		// from Resolume's own BPM-synced animation, or from a keyframe, and a
		// second clock underneath it would fight whatever is doing the
		// driving.
		phase = 0.0;
	}
	else if( lastHostTime >= 0.0 )
	{
		const double delta = std::clamp( now - lastHostTime, 0.0, kMaxFrameDelta );
		phase += delta * speed;
	}
	lastHostTime = now;

	return static_cast< float >( phase ) + manual;
}

//---------------------------------------------------------------------------
void OutrunPlugin::UpdateAudio()
{
	const ParamInfo* info = FindParamInfo( PT_AUDIO );
	if( info == nullptr )
		return;

	// Frame delta for the release filter, off the same clock everything else
	// runs on. First frame -- or a clock that has not moved -- snaps instead.
	const double now = hostSeconds;
	const double dt  = ( audioClock >= 0.0 && now > audioClock ) ? now - audioClock : 0.0;
	audioClock       = now;

	// Fast up, slow down -- the same asymmetry as the stabilise pass and for
	// the same reason: a flash that arrives a frame late reads as broken,
	// while one that takes ~150 ms to die away reads as intended.
	const float release = dt > 0.0 ? 1.0f - std::exp( static_cast< float >( -dt / 0.15 ) ) : 1.0f;

	const size_t bins = std::min( info->elements.size(), audioLevel.size() );
	for( size_t i = 0; i < bins; ++i )
	{
		// sqrt because bin magnitudes bunch near zero: a spectrum used raw
		// lights nothing but the kick drum's stretch of tube.
		const float raw = std::sqrt( std::max( 0.0f, info->elements[ i ].value ) );

		if( raw >= audioLevel[ i ] )
			audioLevel[ i ] = raw;
		else
			audioLevel[ i ] += ( raw - audioLevel[ i ] ) * release;
	}
}

//---------------------------------------------------------------------------
void OutrunPlugin::Render( int width, int height, GLuint inputTexture, float maxU, float maxV )
{
	if( width <= 0 || height <= 0 )
		return;

	//The host's viewport, read before anything of ours changes it.
	//`ScopedFBOBinding` restores the framebuffer binding and *only* the
	//framebuffer binding (SDK b1afaf9) -- every pass's ResizeViewPort() leaks
	//into the pass after it, and the composite, which draws to the host's own
	//framebuffer, has no buffer of its own to size itself from. Without this
	//it inherits the quarter-size glow viewport and renders into a corner.
	GLint hostViewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, hostViewport );

	UpdateClock();
	const float phaseNow = AdvancePhase();
	UpdateAudio();

	//The bass mean feeds Break Amount, so a kick can shove the strokes off
	//the geometry. Folded in here rather than in the shader so the shader has
	//one Break Amount and the sweep sees Audio Break move the picture.
	float bass = 0.0f;
	for( int i = 0; i < 8; ++i )
		bass += audioLevel[ static_cast< size_t >( i ) ];
	bass *= 1.0f / 8.0f;

	const float breakEffective = std::clamp(
		params[ PT_BREAK_AMOUNT ] + params[ PT_AUDIO_BREAK ] * bass, 0.0f, 1.0f );

	//---------------------------------------------------------------------
	// Buffers. Every Ensure() happens here, before anything binds a texture:
	// ffglex::FFGLFBO::Initialise sizes its new colour texture under a
	// ScopedTextureBinding, and every ffglex Scoped* binding *clears* to 0 on
	// scope exit rather than restoring what was there. Allocating a buffer
	// mid-chain therefore unbinds the input texture from the active unit --
	// correct on every frame except the one that allocates.
	//---------------------------------------------------------------------
	const float glowSize = GlowSizeFromParam( params[ PT_GLOW_SIZE ] );
	const int glowWidth  = std::max( 16, width / 4 );
	const int glowHeight = std::max( 16, height / 4 );

	//All of them, whichever engine is active: switching engines mid-show must
	//not allocate mid-chain (the Scoped* clear-to-0 trap above), and the
	//stroke pass binds the stable buffer either way.
	const bool allocated =
		strokeBuffer.Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Mipmapped )
		&& glowBuffer[ 0 ].Ensure( glowWidth, glowHeight, GL_RGBA16F, PassBuffer::Sampling::Linear )
		&& glowBuffer[ 1 ].Ensure( glowWidth, glowHeight, GL_RGBA16F, PassBuffer::Sampling::Linear )
		&& copyBuffer.Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Mipmapped )
		&& edgeBuffer.Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Linear )
		&& stableBuffer[ 0 ].Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Mipmapped )
		&& stableBuffer[ 1 ].Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Mipmapped );

	if( !allocated )
	{
		diag::error( "could not allocate the pass buffers" );
		return;
	}

	const bool engineA = static_cast< int >( std::lround( params[ PT_ENGINE ] ) )
	                     == static_cast< int >( Engine::Trace );

	int stableTarget = stableCurrent;

	//---------------------------------------------------------------------
	// 1. The picture, into a texture of ours, with a mip chain on it. Both
	//    engines need it: A traces it, B backgrounds and colours from it.
	//---------------------------------------------------------------------
	{
		{
			ScopedFBOBinding fbo( copyBuffer.GetGLID(), ScopedFBOBinding::RB_REVERT );
			copyBuffer.ResizeViewPort();
			ScopedShaderBinding shader( copyShader.GetGLID() );
			ScopedSamplerActivation sampler( 0 );
			Scoped2DTextureBinding texture( inputTexture );

			copyShader.Set( "InputTexture", 0 );
			copyShader.Set( "MaxUV", maxU, maxV );
			copyShader.Set( "HalfTexel",
			                0.5f / static_cast< float >( width ),
			                0.5f / static_cast< float >( height ) );
			quad.Draw();
		}
		copyBuffer.GenerateMipmaps();
	}

	//---------------------------------------------------------------------
	// 2 + 3. Edge and stabilise: Engine A only. Engine B never reads the
	//    stable buffer, and its history going stale while B runs is handled
	//    by the historyValid reset when the engine changes.
	//---------------------------------------------------------------------
	if( engineA )
	{
		{
			ScopedFBOBinding fbo( edgeBuffer.GetGLID(), ScopedFBOBinding::RB_REVERT );
			edgeBuffer.ResizeViewPort();
			ScopedShaderBinding shader( edgeShader.GetGLID() );
			ScopedSamplerActivation sampler( 0 );
			Scoped2DTextureBinding texture( copyBuffer.TextureID() );

			edgeShader.Set( "CopyTexture", 0 );
			edgeShader.Set( "TexelSize",
			                1.0f / static_cast< float >( width ),
			                1.0f / static_cast< float >( height ) );
			edgeShader.Set( "Detail", DetailFromParam( params[ PT_DETAIL ] ) );
			edgeShader.Set( "SourceMode", params[ PT_SOURCE ] );
			quad.Draw();
		}

		//-----------------------------------------------------------------
		// 3. Stabilise, ping-ponged against the previous frame's result.
		//-----------------------------------------------------------------
		const int history = stableCurrent;
		stableTarget      = 1 - stableCurrent;
		{
			ScopedFBOBinding fbo( stableBuffer[ stableTarget ].GetGLID(), ScopedFBOBinding::RB_REVERT );
			stableBuffer[ stableTarget ].ResizeViewPort();
			ScopedShaderBinding shader( stabiliseShader.GetGLID() );

			ScopedSamplerActivation sampler0( 0 );
			Scoped2DTextureBinding edgeTexture( edgeBuffer.TextureID() );
			ScopedSamplerActivation sampler1( 1 );
			Scoped2DTextureBinding historyTexture( stableBuffer[ history ].TextureID() );

			stabiliseShader.Set( "EdgeTexture", 0 );
			stabiliseShader.Set( "HistoryTexture", 1 );
			stabiliseShader.Set( "Attack", AttackFromParam( params[ PT_STABILITY ] ) );
			stabiliseShader.Set( "Release", ReleaseFromParam( params[ PT_STABILITY ] ) );
			stabiliseShader.Set( "Sensitivity", SensitivityFromParam( params[ PT_SENSITIVITY ] ) );
			stabiliseShader.Set( "Softness", SoftnessFromParam( params[ PT_SOFTNESS ] ) );
			stabiliseShader.Set( "Reset", historyValid ? 0.0f : 1.0f );
			quad.Draw();
		}
		stableCurrent = stableTarget;
		historyValid  = true;
		stableBuffer[ stableTarget ].GenerateMipmaps();
	}

	//---------------------------------------------------------------------
	// 4. Stroke. The plugin.
	//---------------------------------------------------------------------
	{
		ScopedFBOBinding fbo( strokeBuffer.GetGLID(), ScopedFBOBinding::RB_REVERT );
		strokeBuffer.ResizeViewPort();
		ScopedShaderBinding shader( strokeShader.GetGLID() );

		ScopedSamplerActivation sampler0( 0 );
		Scoped2DTextureBinding paletteBinding( paletteTexture );
		ScopedSamplerActivation sampler1( 1 );
		Scoped2DTextureBinding stableTexture( stableBuffer[ stableTarget ].TextureID() );
		ScopedSamplerActivation sampler2( 2 );
		Scoped2DTextureBinding copyTexture( copyBuffer.TextureID() );

		strokeShader.Set( "PaletteTexture", 0 );
		strokeShader.Set( "StableTexture", 1 );
		strokeShader.Set( "CopyTexture", 2 );
		strokeShader.Set( "CentroidLod", stableBuffer[ stableTarget ].MaxMipLevel() );
		strokeShader.Set( "Trace", params[ PT_TRACE ] );

		//The scoped bindings clear their units on scope exit; the draw has to
		//happen while they are alive.
		setStrokeUniformsAndDraw( width, height, phaseNow, breakEffective );
	}

	//The glow's first pass reads this while drawing into a buffer a quarter
	//the size, so it needs a pre-filtered level to read rather than five
	//point samples of a picture four times finer than its target.
	strokeBuffer.GenerateMipmaps();

	//---------------------------------------------------------------------
	// 5. Glow. Two separable passes, run twice: the second pair is wider than
	//    the first, and summing two Gaussians of different widths is what
	//    gives a tube a tight halo and a wide falloff instead of one blob.
	//---------------------------------------------------------------------
	{
		const float downsampleLod =
			std::log2( std::max( 1.0f, static_cast< float >( width ) / static_cast< float >( glowWidth ) ) );

		const float stepX = glowSize * 0.001f * static_cast< float >( width ) / static_cast< float >( glowWidth );
		const float stepY = glowSize * 0.001f * static_cast< float >( height ) / static_cast< float >( glowHeight );

		//Three pairs, widening by 1.55x each time, not tinsel's two at 1.8x.
		//A wider pass only reads as a smooth falloff while the accumulated
		//blur underneath it covers the gaps between its five taps; on
		//tinsel's dot-like lamps a marginal ratio hides, but a continuous
		//tube turns every uncovered tap into a coherent ghost line running
		//parallel to itself, which the eye finds immediately.
		struct BlurStage
		{
			int from;    ///< -1 for the stroke buffer
			int to;
			float x, y;
			float lod;   ///< only the first pass reads anything but level 0
		};
		const BlurStage stages[] = {
			{ -1, 0, stepX, 0.0f, downsampleLod },
			{ 0, 1, 0.0f, stepY, 0.0f },
			{ 1, 0, stepX * 1.55f, 0.0f, 0.0f },
			{ 0, 1, 0.0f, stepY * 1.55f, 0.0f },
			{ 1, 0, stepX * 2.4f, 0.0f, 0.0f },
			{ 0, 1, 0.0f, stepY * 2.4f, 0.0f },
		};

		for( const BlurStage& stage : stages )
		{
			ScopedFBOBinding fbo( glowBuffer[ stage.to ].GetGLID(), ScopedFBOBinding::RB_REVERT );
			glowBuffer[ stage.to ].ResizeViewPort();
			ScopedShaderBinding shader( blurShader.GetGLID() );
			ScopedSamplerActivation sampler( 0 );
			Scoped2DTextureBinding texture( stage.from < 0 ? strokeBuffer.TextureID()
			                                               : glowBuffer[ stage.from ].TextureID() );

			blurShader.Set( "SourceTexture", 0 );
			blurShader.Set( "Direction", stage.x, stage.y );
			blurShader.Set( "SourceLod", stage.lod );
			quad.Draw();
		}
	}

	//---------------------------------------------------------------------
	// 6. Composite, straight to the host's framebuffer.
	//---------------------------------------------------------------------
	{
		//Back to the host's viewport. See the note where it was captured.
		glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );

		ScopedShaderBinding shader( compositeShader.GetGLID() );

		ScopedSamplerActivation sampler0( 0 );
		Scoped2DTextureBinding lightTexture( strokeBuffer.TextureID() );
		ScopedSamplerActivation sampler1( 1 );
		Scoped2DTextureBinding glowTexture( glowBuffer[ 1 ].TextureID() );

		ScopedSamplerActivation sampler2( 2 );
		Scoped2DTextureBinding copyTexture( copyBuffer.TextureID() );
		ScopedSamplerActivation sampler3( 3 );
		Scoped2DTextureBinding stableTexture( stableBuffer[ stableTarget ].TextureID() );

		compositeShader.Set( "LightTexture", 0 );
		compositeShader.Set( "GlowTexture", 1 );
		compositeShader.Set( "Background", params[ PT_BACKGROUND ] );
		compositeShader.Set( "Glow", GlowFromParam( params[ PT_GLOW ] ) );
		compositeShader.Set( "CopyTexture", 2 );
		compositeShader.Set( "StableTexture", 3 );
		compositeShader.Set( "Dim", params[ PT_DIM ] );
		compositeShader.Set( "MixAmount", params[ PT_MIX ] );
		quad.Draw();
	}
}

//---------------------------------------------------------------------------
void OutrunPlugin::setStrokeUniformsAndDraw( int width, int height, float phaseNow, float breakEffective )
{
	strokeShader.Set( "Engine", params[ PT_ENGINE ] );
	strokeShader.Set( "Aspect", static_cast< float >( width ) / static_cast< float >( height ) );
	strokeShader.Set( "PictureSize", static_cast< float >( width ), static_cast< float >( height ) );
	strokeShader.Set( "WidthPx", WidthFromParam( params[ PT_WIDTH ] ) );
	strokeShader.Set( "Core", params[ PT_CORE ] );
	strokeShader.Set( "TraceAngle", TraceAngleFromParam( params[ PT_TRACE_ANGLE ] ) );

	strokeShader.Set( "PathIndex", params[ PT_PATH ] );
	strokeShader.Set( "PathScale", PathScaleFromParam( params[ PT_PATH_SCALE ] ) );
	strokeShader.Set( "PathDetail", PathDetailFromParam( params[ PT_PATH_DETAIL ] ) );
	strokeShader.Set( "Horizon", HorizonFromParam( params[ PT_HORIZON ] ) );

	strokeShader.Set( "BreakMode", params[ PT_BREAK_MODE ] );
	strokeShader.Set( "BreakAmount", breakEffective );
	strokeShader.Set( "BreakSpread", params[ PT_BREAK_SPREAD ] );
	strokeShader.Set( "BreakHue", BreakHueFromParam( params[ PT_BREAK_HUE ] ) );

	strokeShader.Set( "ColourMode", params[ PT_COLOUR_MODE ] );
	strokeShader.Set( "PaletteIndex", params[ PT_PALETTE ] );
	strokeShader.Set( "Colour1", params[ PT_C1_R ], params[ PT_C1_G ], params[ PT_C1_B ] );
	strokeShader.Set( "Colour2", params[ PT_C2_R ], params[ PT_C2_G ], params[ PT_C2_B ] );
	strokeShader.Set( "Spread", SpreadFromParam( params[ PT_SPREAD ] ) );
	strokeShader.Set( "Saturation", SaturationFromParam( params[ PT_SATURATION ] ) );
	strokeShader.Set( "Brightness", BrightnessFromParam( params[ PT_BRIGHTNESS ] ) );

	strokeShader.Set( "Phase", phaseNow );

	//FFGLShader::Set has no array overloads, so these go up raw.
	strokeShader.Set( "AudioLevel", params[ PT_AUDIO_LEVEL ] );
	glUniform1fv( strokeShader.FindUniform( "Audio" ), kAudioBins, audioLevel.data() );

	//The Lissajous curve is solved here, once per frame, and marched in the
	//shader: 48 segment distances against uniform points beats 96 sines per
	//pixel, and keeps the curve maths in one language.
	const bool engineB = static_cast< int >( std::lround( params[ PT_ENGINE ] ) )
	                     == static_cast< int >( Engine::Paths );
	if( engineB && static_cast< int >( std::lround( params[ PT_PATH ] ) ) == static_cast< int >( Path::Lissajous ) )
	{
		const float aspect = static_cast< float >( width ) / static_cast< float >( height );
		const float scale  = PathScaleFromParam( params[ PT_PATH_SCALE ] );
		const float detail = PathDetailFromParam( params[ PT_PATH_DETAIL ] );

		//Continuous ratios on purpose: an integer ratio closes the figure and
		//repeats exactly, and a ratio slightly off an integer precesses,
		//which is the difference between a logo and a pattern that stays
		//interesting for an hour.
		const float a = detail * 0.5f + 1.0f;
		const float b = detail * 0.35f + 2.0f;

		for( int i = 0; i < kCurvePoints; ++i )
		{
			const float tau = static_cast< float >( i ) / static_cast< float >( kCurvePoints - 1 ) * kTau;
			curveScratch[ static_cast< size_t >( i ) * 2 + 0 ] =
				0.5f + 0.5f * scale * std::sin( a * tau + phaseNow * kTau * 0.25f + kTau * 0.25f ) / aspect;
			curveScratch[ static_cast< size_t >( i ) * 2 + 1 ] =
				0.5f + 0.5f * scale * std::sin( b * tau );
		}
		glUniform2fv( strokeShader.FindUniform( "Curve" ), kCurvePoints, curveScratch.data() );
	}

	quad.Draw();
}

//---------------------------------------------------------------------------
FFResult OutrunPlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL == nullptr || pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	const FFGLTextureStruct& texture = *pGL->inputTextures[ 0 ];
	const int width                  = static_cast< int >( texture.Width );
	const int height                 = static_cast< int >( texture.Height );

	//The input texture can be bigger than the picture; MaxUV is the fraction
	//that was really drawn. Resolved once, in the copy pass.
	const FFGLTexCoords coords = GetMaxGLTexCoords( *pGL->inputTextures[ 0 ] );

	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	Render( width, height, texture.Handle, coords.s, coords.t );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult OutrunPlugin::DeInitGL()
{
	copyShader.FreeGLResources();
	edgeShader.FreeGLResources();
	stabiliseShader.FreeGLResources();
	strokeShader.FreeGLResources();
	blurShader.FreeGLResources();
	compositeShader.FreeGLResources();
	quad.Release();

	copyBuffer.Destroy();
	edgeBuffer.Destroy();
	stableBuffer[ 0 ].Destroy();
	stableBuffer[ 1 ].Destroy();
	strokeBuffer.Destroy();
	glowBuffer[ 0 ].Destroy();
	glowBuffer[ 1 ].Destroy();

	if( paletteTexture != 0 )
	{
		glDeleteTextures( 1, &paletteTexture );
		paletteTexture = 0;
	}

	historyValid = false;

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
char* OutrunPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		// Function-local rather than a member: the line is built from
		// compile-time facts, so it is the same for every instance, and the
		// host only needs the pointer to outlive the call.
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}

	return CFFGLPlugin::GetTextParameter( index );
}

//---------------------------------------------------------------------------
FFResult OutrunPlugin::SetTextParameter( unsigned int index, const char* value )
{
	// See the declaration: the base class fails, and a failed default deletes
	// the instance. The About line is display-only, so there is genuinely
	// nothing to store -- but it has to say so successfully.
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;

	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult OutrunPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing, so they are handled
	// before any of the bookkeeping below: pressing one is not the operator
	// editing a control.
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	if( index == PT_PRESET )
	{
		const int chosen = static_cast< int >( std::lround( value ) );
		if( chosen != static_cast< int >( std::lround( params[ PT_PRESET ] ) ) )
			applyPreset( chosen );
		return FF_SUCCESS;
	}

	const float previous = params[ index ];
	params[ index ]      = value;

	//Changing what an edge *is* invalidates the history, because the numbers
	//being blended are no longer measuring the same thing. Without this,
	//turning Detail up with Stability high leaves the old scale's outlines
	//decaying underneath the new ones, which looks like the control having
	//two effects.
	if( index == PT_DETAIL || index == PT_SOURCE || index == PT_ENGINE )
		historyValid = false;

	// A slider moved while a preset is active means the operator has taken
	// over: the dropdown falls back to Custom. The equality guard matters --
	// hosts that honour the value events echo the preset's own values
	// straight back through here, and that echo must not un-set the preset.
	const int active = static_cast< int >( std::lround( params[ PT_PRESET ] ) );
	if( active > 0 && std::fabs( value - previous ) > 1e-4f )
	{
		for( unsigned int id : kPresetParamIDs )
		{
			if( id == index )
			{
				params[ PT_PRESET ] = 0.0f;
				RaiseParamEvent( PT_PRESET, FF_EVENT_FLAG_VALUE );
				break;
			}
		}
	}

	return FF_SUCCESS;
}

void OutrunPlugin::applyPreset( int presetIndex )
{
	params[ PT_PRESET ] = static_cast< float >( presetIndex );

	if( presetIndex <= 0 || presetIndex > presets::kCount )
		return;//Custom: the sliders keep whatever they said

	const presets::Preset& preset = presets::kPresets[ presetIndex - 1 ];
	for( int j = 0; j < presets::kParamCount; ++j )
	{
		const unsigned int id = kPresetParamIDs[ j ];
		if( std::fabs( params[ id ] - preset.v[ j ] ) <= 1e-6f )
			continue;

		// The copy is what changes the picture; the event only tells the host
		// to re-read the slider. A host that ignores it renders the preset
		// correctly and merely shows stale knobs.
		params[ id ] = preset.v[ j ];
		RaiseParamEvent( id, FF_EVENT_FLAG_VALUE );

		// The same invalidation SetFloatParameter does, for the same reason:
		// this writes params[] directly, so without it a preset is the one
		// way to change the engine *without* dropping the edge history. The
		// symptom is specific and would be blamed on the preset -- pick an
		// Engine B preset, leave it up while its stale mask rots, then pick
		// an Engine A one, and the first frames blend the new outlines
		// against outlines the clip no longer has. Only Engine is reachable
		// here (the detector parameters are deliberately not in the table),
		// but the condition is written against all three so that adding one
		// to the preset table cannot quietly reintroduce this.
		if( id == PT_ENGINE || id == PT_DETAIL || id == PT_SOURCE )
			historyValid = false;
	}
}

float OutrunPlugin::GetFloatParameter( unsigned int index )
{
	if( index >= PT_COUNT )
		return 0.0f;

	return params[ index ];
}

FFResult OutrunPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void OutrunPlugin::SetPhaseOverride( float pin )
{
	phasePinned = true;
	pinnedPhase = pin;
}

} // namespace outrun
