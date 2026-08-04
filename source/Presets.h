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

// Option values are element indices:
//   Engine       0 A (Trace) / 1 B (Paths)
//   Path         the Path enum (0 Grid, 3 Circuit, 5 Rings, 7 Waveform)
//   Trace        0 Spiral / 1 Angle / 2 Linear / 3 Radial
//   Break Mode   the BreakMode enum (1 Echo, 2 Angular, 3 Scan, 4 Flow,
//                5 Rays)
//   Colour Mode  0 Palette / 1 Clip / 2 Clip x Palette
//   Palette      the Palette enum (2 Miami, 4 Sunset Drive, 5 Laser,
//                6 Vice, 7 Chrome, 8 Ultraviolet, 9 Neon Noir, 10 Acid,
//                11 Sodium, 12 Cyberdeck, 13 Hologram, 14 Blood Neon,
//                15 Mono)
//   Sync         0 Free / 1 Beat / 2 Bar
//   Background   0 Black / 1 Source / 2 Dimmed Source / 3 Transparent /
//                4 Edges
// Saturation 0.667 and Brightness 0.5 are unity after mapping.
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

	//-----------------------------------------------------------------------
	// The Engine A bank.
	//
	// The eight above were curated around Engine B and the neon sign, and
	// between them they reach one Trace coordinate (Angle), three of six
	// break modes, one of three colour modes and one of five backgrounds.
	// Everything the trace engine can do to an outline that is not "make it
	// a pink tube on black" was therefore unreachable in one gesture.
	//
	// These eight are chosen to cover that ground rather than to be eight
	// more neon looks: between them they use every remaining Trace, both
	// remaining break modes, both clip-driven colour modes, and three of
	// the four remaining backgrounds -- plain Source is skipped on purpose,
	// for the reason given under Tinted Print. The edge detector is still
	// left alone -- see the file comment; these change what is *done* with
	// an outline, never what counts as one.
	//-----------------------------------------------------------------------

	// Radial trace: the palette runs with distance from the centre, so
	// Cyberdeck's hard thirds land as banded rings on the artwork's own
	// shape -- a topographic reading of the outline rather than a lit one.
	// Core is low here and in every other palette-reading preset below for
	// one reason: the white-hot centre is drawn *over* the palette, so a
	// tube thin enough to be a contour line is nearly all core, and the
	// banding this preset exists for bleaches to white at the values the
	// neon presets use.
	{ "Contour Map",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.22f, /*Core*/ 0.25f, /*Trace*/ 3, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.4f, /*BHue*/ 0.15f,
	    /*CMode*/ 0, /*Palette*/ 12, /*Spread*/ 0.85f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.1f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.3f, /*GlowSz*/ 0.35f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// Clip colour mode: every outline is lit in the colour of whatever it
	// is an outline *of*, over its own footage held back. The one preset
	// that stays legible on material with its own palette.
	{ "Self Lit",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.22f, /*Core*/ 0.55f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.4f, /*BHue*/ 0.15f,
	    /*CMode*/ 1, /*Palette*/ 15, /*Spread*/ 0.4f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.15f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.28f, /*GlowSz*/ 0.4f, /*Back*/ 2, /*Dim*/ 0.45f } },

	// The Linear trace, which nothing else here uses: colour runs down one
	// fixed direction instead of round the shape, so an outline reads as
	// drawn against a straightedge rather than as lit.
	//
	// Deliberately no Angular, despite the name wanting it. Angular bends
	// the angle the field is *sampled* at, and on a curve that pulls the
	// samples off the contour -- at any amount strong enough to see, a
	// circle comes apart into disconnected arcs. It squares a straight edge
	// and shatters a curved one, which is why Shattered owns it and this
	// preset does not: continuous thin line is the entire point here.
	{ "Blueprint",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.14f, /*Core*/ 0.3f, /*Trace*/ 2, /*TAngle*/ 0.25f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.3f, /*BHue*/ 0.05f,
	    /*CMode*/ 0, /*Palette*/ 7, /*Spread*/ 0.75f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.08f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.2f, /*GlowSz*/ 0.3f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// Transparent background: flat white line on nothing at all, so the
	// outline leaves the plugin as an alpha layer for the composition to
	// place rather than as a finished picture.
	{ "Alpha Line",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.15f, /*Core*/ 0.85f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.4f, /*BHue*/ 0.15f,
	    /*CMode*/ 0, /*Palette*/ 15, /*Spread*/ 0.4f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.1f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.1f, /*GlowSz*/ 0.3f, /*Back*/ 3, /*Dim*/ 0.25f } },

	// Flow, which nothing above reaches: the outline bent through a
	// stateless field until it stops being the artwork's edge and starts
	// being weather coming off it. Wide and soft, because a thin stroke
	// under a flow warp reads as an error rather than as drift.
	{ "Smoke Drift",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.28f, /*Core*/ 0.35f, /*Trace*/ 0, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 4, /*Amount*/ 0.65f, /*BSpread*/ 0.45f, /*BHue*/ 0.25f,
	    /*CMode*/ 0, /*Palette*/ 8, /*Spread*/ 0.45f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.3f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.6f, /*GlowSz*/ 0.6f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// Rays, the other mode nothing above reaches: every edge extended into
	// a streak along one heading, in sodium, so the artwork throws light
	// the way a streetlamp does rather than glowing like a sign.
	{ "Sun Rays",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.24f, /*Core*/ 0.5f, /*Trace*/ 1, /*TAngle*/ 0.62f,
	    /*BreakMode*/ 5, /*Amount*/ 0.6f, /*BSpread*/ 0.4f, /*BHue*/ 0.1f,
	    /*CMode*/ 0, /*Palette*/ 11, /*Spread*/ 0.4f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.18f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.65f, /*GlowSz*/ 0.55f, /*Back*/ 0, /*Dim*/ 0.25f } },

	// The Edges background, which no preset above selects. It does not put
	// the mask *behind* the stroke -- it replaces the picture with it, which
	// is the whole point: judging a threshold through a layer of neon and
	// glow is guesswork. So this is the tuning view, the one to leave up
	// while setting Sensitivity, Softness, Detail and Stability, and the
	// only preset here whose stroke and colour values are inert by design.
	// They are set to something sane anyway, because leaving this preset
	// flips the dropdown to Custom and hands those values straight back to
	// the operator.
	{ "Edge Matte",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.18f, /*Core*/ 0.8f, /*Trace*/ 1, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.4f, /*BHue*/ 0.15f,
	    /*CMode*/ 0, /*Palette*/ 15, /*Spread*/ 0.4f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.1f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.15f, /*GlowSz*/ 0.3f, /*Back*/ 4, /*Dim*/ 0.25f } },

	// Clip x Palette: the palette tints what the clip already is instead of
	// replacing it, so the outline reads as a print pulled off the footage
	// rather than as something drawn on top.
	//
	// The only preset with Core at zero, and it has to be: the core is white
	// laid over the palette, so any of it at all turns "tinted" into "lit",
	// and over the clip's own bright areas -- which is exactly where a
	// clip-multiplied stroke is strongest -- white on white is nothing at
	// all. Killing the core is what leaves the palette as the only thing
	// the stroke contributes.
	//
	// Over *dimmed* source rather than plain source for the same reason.
	// Plain source is the one background no preset here selects, and that
	// is deliberate: it changes the picture least by design, so a preset
	// built on it is mostly the clip with a stroke somewhere in it -- worth
	// having as a mode, worth nothing as a one-gesture look.
	{ "Tinted Print",
	  { /*Engine*/ 0, /*Path*/ 0, /*Scale*/ 0.5f, /*Detail*/ 0.4f, /*Horizon*/ 0.5f,
	    /*Width*/ 0.22f, /*Core*/ 0.0f, /*Trace*/ 0, /*TAngle*/ 0.0f,
	    /*BreakMode*/ 0, /*Amount*/ 0.0f, /*BSpread*/ 0.4f, /*BHue*/ 0.15f,
	    /*CMode*/ 2, /*Palette*/ 4, /*Spread*/ 0.5f,
	    /*C1*/ 1.0f, 0.20f, 0.80f, /*C2*/ 0.10f, 0.90f, 1.00f,
	    /*Sat*/ 0.667f, /*Bright*/ 0.5f, /*Sync*/ 0, /*Speed*/ 0.12f,
	    /*AudioLvl*/ 0.0f, /*AudioBrk*/ 0.0f,
	    /*Glow*/ 0.45f, /*GlowSz*/ 0.45f, /*Back*/ 2, /*Dim*/ 0.55f } },
};

inline constexpr int kCount = int( sizeof( kPresets ) / sizeof( kPresets[ 0 ] ) );

} // namespace presets
} // namespace outrun
