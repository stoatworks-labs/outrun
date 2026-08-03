#pragma once

/**
    Factory presets: named neon looks an operator can reach in one gesture.
    Each entry is a curated path/breakaway/palette pairing, not a random
    collection of slider positions.

    The values live in the 0..1 parameter space the host sees, so one table
    drives every binding of it. Plain data only; the application machinery
    lives with the host glue in Outrun.cpp.

    Element 0 of the host-facing dropdown is "Custom" and is not in this
    table: it means "the sliders are the truth".

    A preset covers the path, stroke, breakaway, colour, tempo and look
    parameters, and which engine they belong to. The edge-detector settings
    are deliberately left alone -- sensitivity and detail are tuned to the
    operator's artwork, and a preset that undid that tuning every time it was
    picked would cost more than it gave. Mix and Phase stay the operator's
    too.
*/

namespace outrun
{
namespace presets
{
/// The parameters a preset sets, in one fixed order. Outrun.cpp binds this
/// order to its ParamIds and static_asserts against kParamCount so the two
/// lists cannot drift apart silently.
enum Param
{
	kEngine,
	kPath,
	kPathScale,
	kPathDetail,
	kHorizon,
	kWidth,
	kCore,
	kTrace,
	kTraceAngle,
	kBreakMode,
	kBreakAmount,
	kBreakSpread,
	kBreakHue,
	kColourMode,
	kPalette,
	kSpread,
	kC1R,
	kC1G,
	kC1B,
	kC2R,
	kC2G,
	kC2B,
	kSaturation,
	kBrightness,
	kSync,
	kSpeed,
	kAudioLevel,
	kAudioBreak,
	kGlow,
	kGlowSize,
	kBackground,
	kDim,
	kParamCount
};

struct Preset
{
	const char* name;
	float v[ kParamCount ];
};

// Option values are element indices: Engine is 0 A (Trace) / 1 B (Paths);
// Path is the Path enum (0 Grid,
// 3 Circuit, 5 Rings, 7 Waveform); Break Mode is the BreakMode enum (1 Echo,
// 2 Angular, 3 Scan); Palette is the Palette enum (2 Miami, 5 Laser, 6 Vice,
// 7 Chrome, 9 Neon Noir, 10 Acid, 13 Hologram, 14 Blood Neon); Sync 0 Free /
// 1 Beat / 2 Bar; Background 0 Black. Saturation 0.667 and Brightness 0.5
// are unity after mapping.
inline constexpr Preset kPresets[] = {
	// The flagship: the perspective grid rolling to the beat in Miami pink.
	{ "Outrun Grid",
	  { /*Engine*/ 1, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.35f, /*Core*/ 0.5f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.4f, /*BHue*/ 0.15f,
	    /*CMode*/ 0, /*Palette*/ 2, /*Spread*/ 0.4f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 1, /*Speed*/ 0.25f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.6f, /*GlowSz*/ 0.45f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// The faithful outline, hot-cored, in bar-sign red: what the effect does
	// to a logo with everything else turned off.
	{ "Neon Sign",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.45f, /*Core*/ 0.7f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.4f, /*BHue*/ 0.15f,
	    /*CMode*/ 0, /*Palette*/ 14, /*Spread*/ 0.3f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.15f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.65f, /*GlowSz*/ 0.5f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// Echoes of the outline drifting outward through teal and pink.
	{ "Ghost Echo",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.35f, /*Core*/ 0.5f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 1, /*Amount*/ 0.5f, /*BSpread*/ 0.5f, /*BHue*/ 0.4f,
	    /*CMode*/ 0, /*Palette*/ 6, /*Spread*/ 0.4f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.2f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.55f, /*GlowSz*/ 0.4f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// The outline snapped to chrome technical linework.
	{ "Shattered",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.3f, /*Core*/ 0.6f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 2, /*Amount*/ 0.8f, /*BSpread*/ 0.3f, /*BHue*/ 0.1f,
	    /*CMode*/ 0, /*Palette*/ 7, /*Spread*/ 0.4f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.2f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.55f, /*GlowSz*/ 0.4f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// Scanline glitches that kick harder when the bass does.
	{ "Scan Damage",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.35f, /*Core*/ 0.5f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 3, /*Amount*/ 0.6f, /*BSpread*/ 0.35f, /*BHue*/ 0.3f,
	    /*CMode*/ 0, /*Palette*/ 9, /*Spread*/ 0.4f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.35f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.6f,
	    /*Glow*/ 0.5f, /*GlowSz*/ 0.4f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// Down the tunnel, one ring per bar.
	{ "Laser Tunnel",
	  { /*Engine*/ 1, /*Path*/ 5, /*Scale*/ 0.6f, /*Detail*/ 0.5f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.35f, /*Core*/ 0.55f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.4f, /*BHue*/ 0.15f,
	    /*CMode*/ 0, /*Palette*/ 5, /*Spread*/ 0.5f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 2, /*Speed*/ 0.25f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.7f, /*GlowSz*/ 0.5f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// The routed audio as a holographic oscilloscope trace.
	{ "Oscilloscope",
	  { /*Engine*/ 1, /*Path*/ 7, /*Scale*/ 0.7f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.3f, /*Core*/ 0.6f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.4f, /*BHue*/ 0.15f,
	    /*CMode*/ 0, /*Palette*/ 13, /*Spread*/ 0.4f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.25f,
	    /*AudioLvl*/ 0.8f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.6f, /*GlowSz*/ 0.45f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// Acid-green traces pulsing round a slow board.
	{ "Circuit Pulse",
	  { /*Engine*/ 1, /*Path*/ 3, /*Scale*/ 0.5f, /*Detail*/ 0.55f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.32f, /*Core*/ 0.5f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.4f, /*BHue*/ 0.15f,
	    /*CMode*/ 0, /*Palette*/ 10, /*Spread*/ 0.6f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.15f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.55f, /*GlowSz*/ 0.4f, /*Back*/ 0, /*Dim*/ 0.25f } },
};

inline constexpr int kCount = int( sizeof( kPresets ) / sizeof( kPresets[ 0 ] ) );

} // namespace presets
} // namespace outrun
