#pragma once

#include "Controls.h"
#include "PassBuffer.h"
#include "Paths.h"
#include "Presets.h"
#include "Shaders.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <array>

/**
    Outrun -- neon strokes for Resolume: two engines, one plugin.

    **Engine A (Trace)** finds the outlines in the clip -- tinsel's edge
    pipeline, lifted whole -- and draws them as continuous neon tubes that can
    *break away* from the real geometry: echoes, angular snapping, scanline
    glitch, flow, rays. **Engine B (Paths)** draws the same tubes along
    generated paths instead: the perspective grid with the striped sun,
    tunnels, circuits, skylines, and the routed audio as an oscilloscope.

    One FF_EFFECT plugin, one dropdown. The engines share every stroke-facing
    control, the shader is one compiled program with a uniform switch, and the
    clip is always present -- Engine B uses it as background and colour
    source rather than tracing it.

    **The stroke field idea.** Both variants reduce to the same contract: a
    per-pixel tube mass and a coordinate along the stroke. The effect's mass
    is the stabilised edge mask read at a width-dependent mip level; the
    source's is a path distance divided by its own screen-space gradient, so a
    receding grid line thins on screen without any per-path effort. Everything
    after that -- breakaway, colour, the white-hot core, the glow -- is shared
    text.

    See AGENTS.md for the traps.
*/
namespace outrun
{
class OutrunPlugin : public CFFGLPlugin
{
public:
	OutrunPlugin();

	//CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;

	char* GetTextParameter( unsigned int index ) override;

	/// Declared only so the About line can accept its own default.
	/// instantiateGL pushes every declared default back through the setters on
	/// a fresh instance and deletes the instance if one fails, and
	/// CFFGLPlugin's SetTextParameter is a stub that returns exactly that
	/// failure -- so without this override no real host can load the plugin,
	/// while every offline harness here carries on passing.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	float GetFloatParameter( unsigned int index ) override;

	FFResult SetTime( double time ) override;

	//---------------------------------------------------------------------
	// Clock test hooks.
	//
	// The offline harness DECLARES its unit rather than leaving UpdateClock to
	// infer one. A single absolute time handed over in one frame is genuinely
	// ambiguous -- 2.0 is two seconds or two milliseconds and nothing in it
	// says which -- so inference is only possible against a live host's frame
	// deltas. An implicit unit is exactly what caused the bug these exist to
	// keep fixed.
	//---------------------------------------------------------------------
	void SetClockScaleForTest( double scale );
	void TickClockForTest();
	double ClockScaleForTest() const;
	double HostSecondsForTest() const;

	/// Render one frame into whatever is currently bound, at `width` x
	/// `height`. Exposed for the offline harness, which drives this class
	/// rather than a copy of it -- a test that exercises a reimplementation
	/// tests the reimplementation.
	void Render( int width, int height, GLuint inputTexture, float maxU, float maxV );

	/// Pin the driven phase, ignoring the host clock and the beat. The Phase
	/// parameter is still added on top, so pinning does not make that slider
	/// look dead to a sweep.
	void SetPhaseOverride( float phase );

private:
	/// The ParamId each presets::Param drives, in presets::Param order. The
	/// preset table stays host-agnostic; this is the FFGL binding of it.
	static constexpr unsigned int kPresetParamIDs[ presets::kParamCount ] = {
		PT_ENGINE,
		PT_PATH, PT_PATH_SCALE, PT_PATH_DETAIL, PT_HORIZON,
		PT_WIDTH, PT_CORE, PT_TRACE, PT_TRACE_ANGLE,
		PT_BREAK_MODE, PT_BREAK_AMOUNT, PT_BREAK_SPREAD, PT_BREAK_HUE,
		PT_COLOUR_MODE, PT_PALETTE, PT_SPREAD,
		PT_C1_R, PT_C1_G, PT_C1_B, PT_C2_R, PT_C2_G, PT_C2_B,
		PT_SATURATION, PT_BRIGHTNESS,
		PT_SYNC, PT_SPEED, PT_AUDIO_LEVEL, PT_AUDIO_BREAK,
		PT_GLOW, PT_GLOW_SIZE, PT_BACKGROUND, PT_DIM
	};

	/// Copy a factory preset's values into params[] and raise value events so
	/// the host re-reads the sliders. `presetIndex` is 1-based; 0 is Custom.
	void applyPreset( int presetIndex );

	/// Bake the palettes and upload them. Once, at InitGL: the table does not
	/// depend on any parameter, which is the point of keeping the two
	/// colour-driven palettes out of it.
	bool UploadPalettes();

	/// The tail of the stroke pass: every variant-independent uniform, the
	/// Lissajous curve upload, and the draw itself. Split out so the effect
	/// build can call it while its extra texture bindings are still in scope.
	void setStrokeUniformsAndDraw( int width, int height, float phaseNow, float breakEffective );

	/// Advance and return the phase, in cycles, for this frame. Free mode
	/// integrates the rate (moving Speed must not rescale the history); Beat
	/// and Bar recover an absolute phase from the host's transport so a cycle
	/// boundary lands on the grid; Manual leaves the Phase slider in charge.
	/// Decide what unit the host's clock is in, and advance `hostSeconds`.
	void UpdateClock();

	float AdvancePhase();

	void UpdateAudio();

	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader edgeShader;
	ffglex::FFGLShader stabiliseShader;
	ffglex::FFGLShader strokeShader;
	ffglex::FFGLShader blurShader;
	ffglex::FFGLShader compositeShader;
	ffglex::FFGLScreenQuad quad;

	PassBuffer copyBuffer;        ///< the picture, ours, mipmapped
	PassBuffer edgeBuffer;        ///< raw gradient magnitude. Engine A only.
	PassBuffer stableBuffer[ 2 ]; ///< ping-pong: stabilised edge + moments. Engine A only.
	PassBuffer strokeBuffer;      ///< the tubes, premultiplied, mipmapped
	PassBuffer glowBuffer[ 2 ];   ///< quarter size, ping-ponged by the blur

	/// Which of stableBuffer[] holds the frame just rendered. The other one is
	/// the history the next frame reads.
	int stableCurrent = 0;

	GLuint paletteTexture = 0;

	//---------------------------------------------------------------------
	// Time. The phase is accumulated from the host's clock in Free mode
	// rather than computed from it: `time * speed` rescales the whole history
	// the instant Speed is touched, which is exactly when an operator is
	// watching. Beat and Bar are absolute instead -- the point of those modes
	// is that a cycle boundary lands on the host's grid.
	//---------------------------------------------------------------------
	double hostTime     = -1.0;
	double lastHostTime = -1.0;
	double phase        = 0.0;

	//---------------------------------------------------------------------
	// Host clock units.
	//
	// FFGL never says what unit SetTime is in and hosts disagree: Resolume
	// sends MILLISECONDS, the offline harness sends seconds. UpdateClock
	// calibrates against a real clock and publishes `hostSeconds`, which is
	// the only time anything else here should read.
	//---------------------------------------------------------------------
	double hostSeconds  = 0.0;
	double clockScale   = 0.0;///< 0 until decided; then 1.0 or 0.001
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;

	bool phasePinned  = false;
	float pinnedPhase = 0.0f;

	//---------------------------------------------------------------------
	// Audio. The host writes one spectrum bin per element of PT_AUDIO;
	// UpdateAudio runs them through an attack/release filter into
	// `audioLevel`, and the stroke pass reads that as a uniform array.
	//---------------------------------------------------------------------
	std::array< float, kAudioBins > audioLevel = {};
	double audioClock = -1.0;

	/// Scratch for the Lissajous curve upload, kept as a member so a frame
	/// does not allocate. x,y interleaved, kCurvePoints of each.
	std::array< float, kCurvePoints * 2 > curveScratch = {};

	/// Set when the history buffers hold nothing worth blending against --
	/// the first frame, and any frame after a resize. Without it the first
	/// stabilised frame blends the new picture against the last clip's
	/// outlines. Engine A only.
	bool historyValid = false;

	float params[ PT_COUNT ] = {};
};

} // namespace outrun
