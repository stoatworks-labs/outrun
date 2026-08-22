#pragma once

/**
    Host parameters are 0..1; these are what they mean.

    Every numeric parameter this plugin declares is a plain FF_TYPE_STANDARD
    float in 0..1, including the ones that stand for a width in pixels or a mip
    level. That is not a style preference. `CFFGLPluginManager::SetParamInfo`
    clamps a standard default into 0..1 *before* returning, and `SetParamRange`
    can only be called afterwards because it finds the parameter by ID -- so a
    parameter declared in pixels cannot declare a default in pixels, and 12
    would silently become 1. The conversions live here instead, in one file
    that both the plugin and the test harness use, so there is only ever one
    answer to what a slider position means.

    Where a mapping is geometric rather than linear it is because the
    interesting range is at one end: the difference between a 2 px and a 4 px
    tube is a different look, the difference between 28 px and 30 px is not.
*/
namespace outrun
{

/**
    Parameter ids.

    The declaration order in Outrun.cpp is the order they appear in the host,
    and the groups depend on consecutive ids staying consecutive --
    `SetParamGroup` collapses *runs* of same-group parameters, so reordering
    these silently splits a group in two. Saved compositions refer to these
    ids, so once released they may only ever be appended to. Both engines
    declare everything; the inactive engine's group is simply ignored.
*/
enum ParamId : unsigned int
{
	// Which algorithm draws the strokes. Engine A traces the clip's outlines;
	// Engine B generates paths. One plugin, one dropdown.
	PT_ENGINE = 0,

	// Engine A -- the trace: the edge detector and the coordinate that runs
	// colour along the outline.
	PT_SOURCE,
	PT_SENSITIVITY,
	PT_SOFTNESS,
	PT_DETAIL,
	PT_STABILITY,
	PT_TRACE,
	PT_TRACE_ANGLE,

	// Engine B -- the generator.
	PT_PATH,
	PT_PATH_SCALE,
	PT_PATH_DETAIL,
	PT_HORIZON,

	// Stroke
	PT_WIDTH,
	PT_CORE,

	// Breakaway
	PT_BREAK_MODE,
	PT_BREAK_AMOUNT,
	PT_BREAK_SPREAD,
	PT_BREAK_HUE,

	// Colour
	PT_COLOUR_MODE,
	PT_PALETTE,
	PT_SPREAD,
	PT_C1_R,
	PT_C1_G,
	PT_C1_B,
	PT_C2_R,
	PT_C2_G,
	PT_C2_B,
	PT_SATURATION,
	PT_BRIGHTNESS,

	// Tempo
	PT_SYNC,
	PT_SPEED,
	PT_PHASE,

	// Audio. PT_AUDIO is an FFT buffer (FF_TYPE_BUFFER, FF_USAGE_FFT):
	// Resolume shows it as an audio-source picker and writes one spectrum bin
	// per element, low frequencies first.
	PT_AUDIO,
	PT_AUDIO_LEVEL,
	PT_AUDIO_BREAK,

	// Output
	PT_GLOW,
	PT_GLOW_SIZE,
	PT_BACKGROUND,
	PT_DIM,
	PT_MIX,

	// Preset
	PT_PRESET,

	// -- The Stoatworks About block ------------------------------------------
	//
	// One display-only text line, then one button per link the block carries:
	// the guide, the project page, the source, the funding page. A button opens
	// a browser and stores nothing.
	//
	// How many buttons there are is decided by which URLs StoatworksAbout.h
	// actually holds, so Outrun.cpp static_asserts this run against
	// `about::kParamCount` -- writing a user guide later adds one, and without
	// the assert that would silently shift PT_COUNT and leave the last button
	// undeclared.
	//
	// Last in the enum so no saved composition's parameter ids shift.
	PT_ABOUT_TEXT,
	PT_ABOUT_BUTTON_1,
	PT_ABOUT_BUTTON_2,
	PT_ABOUT_BUTTON_3,
	PT_COUNT
};

/// The two algorithms.
enum class Engine
{
	Trace = 0,  ///< Engine A: the clip's outlines, via the edge pipeline.
	Paths,      ///< Engine B: generated paths; the clip is background and colour.

	Count
};

/// Where phase comes from.
enum class Sync
{
	Free = 0,  ///< The host clock. Speed is cycles per second.
	Beat,      ///< The host's beat. Speed is cycles per beat.
	Bar,       ///< The host's bar. Speed is cycles per bar.
	Manual,    ///< Speed is ignored; the Phase slider is the only driver, so the
	           ///< operator can key it, or let Resolume's own BPM-synced
	           ///< animation drive it.

	Count
};

/// How the strokes leave the real geometry behind.
enum class BreakMode
{
	None = 0,  ///< The faithful outline.
	Echo,      ///< Displaced copies drifting outward, each dimmer and hue-shifted.
	Angular,   ///< Outlines snapped toward 45-degree technical linework.
	Scan,      ///< Per-scanline glitch displacement.
	Flow,      ///< A stateless flow field bends the strokes.
	Rays,      ///< Every edge extended into a directional streak.

	Count
};

/// 0.01 to 1.0, geometrically. The gradient magnitude at which an edge is
/// fully lit; a clean black-to-white step measures 1.0, so the whole useful
/// range for photographic footage sits below 0.15.
float SensitivityFromParam( float value );

/// 0.05 to 1.0, linear, as a fraction of the sensitivity.
float SoftnessFromParam( float value );

/// 0 to 4, linear. The mip level the Sobel runs at.
float DetailFromParam( float value );

/// The two halves of the temporal filter, from one Stability control.
/// Asymmetric on purpose: rise nearly instantly, fall slowly. See the comment
/// in the stabilise shader.
float AttackFromParam( float value );
float ReleaseFromParam( float value );

/// Tube radius **in pixels**, 1 to 32, geometrically. In pixels and not in
/// field units: the stroke pass divides by the field's screen-space gradient,
/// which is what keeps a receding grid line and a nearby one the same
/// thickness on screen.
float WidthFromParam( float value );

/// Path extent as a fraction of the frame, 0.2 to 1.5.
float PathScaleFromParam( float value );

/// Path density -- grid cells across the frame, rings per octave, buildings
/// per width. 1 to 16, geometrically.
float PathDetailFromParam( float value );

/// The horizon's height in frame space, 0.2 to 0.8. Grid and Skyline only.
float HorizonFromParam( float value );

/// 0 to 1 turn, for the Linear trace's direction and Rays' heading.
float TraceAngleFromParam( float value );

/// Per-echo palette shift, 0 to 0.5 of a palette cycle.
float BreakHueFromParam( float value );

/// 0 to 2 cycles per unit of the sync source, geometrically, with a dead zone
/// at the bottom so "stopped" is reachable by dragging to zero rather than by
/// luck.
float SpeedFromParam( float value );

/// 0.25 to 8 palette cycles along the stroke, geometrically.
float SpreadFromParam( float value );

/// 0 to 1.5. Over 1 pushes past the palette's own saturation.
float SaturationFromParam( float value );

/// 0 to 2. Over 1 on purpose: a neon tube that is not clipping does not read
/// as a neon tube.
float BrightnessFromParam( float value );

/// 0 to 3.
float GlowFromParam( float value );

/// 0.5 to 8, geometrically: the glow's radius as a fraction of the picture
/// width, in per-mille.
float GlowSizeFromParam( float value );

} // namespace outrun
