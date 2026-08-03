#pragma once

/**
    The passes, as GLSL source.

    The effect variant runs six, the source variant three:

    1. **copy**      picture size. Resolves MaxUV once so no later pass has to
                     think about it, and carries a mip chain so the edge pass
                     can detect at a scale rather than at a pixel. Effect only.
    2. **edge**      picture size. Sobel at a selectable scale on a selectable
                     channel. Raw gradient magnitude and nothing else. Effect only.
    3. **stabilise** picture size, ping-ponged against itself. Asymmetric IIR
                     over time, then the threshold. Also writes the moments the
                     centroid is reduced from. Effect only.
    4. **stroke**    picture size. The plugin: the stroke field (the stabilised
                     mask in the effect, a path distance function in the
                     source), the breakaway warp, the neon shading, the colour.
    5. **blur**      quarter size, run twice per axis. The glow.
    6. **composite** output size. Background mode, glow, mix.

    The stroke pass is one fragment shader for both variants, assembled by
    `StrokeShaderSource( bool effect )`: the effect build gets
    `#define OUTRUN_EFFECT 1` injected after the version line, which swaps the
    field producer (mask vs path SDFs) and gates the clip-sampling colour
    modes. One source file is what stops the two variants drifting apart.

    **The stroke maths is GPU-only.** There is no C++ mirror of the distance
    functions or the breakaway warps -- the harness proves the paths are alive
    and distinct by contact sheet and sweep, not per-pixel (orrery's precedent;
    a mirrored SDF is a second implementation bought to restate the shader).
    The one mirrored thing is the palette bake, checked by
    `outruntest --palettes`.
*/

#include <string>

namespace outrun
{

extern const char* const kVertexShader;
extern const char* const kCopyShader;
extern const char* const kEdgeShader;
extern const char* const kStabiliseShader;
extern const char* const kBlurShader;

/// The stroke pass for one variant. `effect` injects OUTRUN_EFFECT.
std::string StrokeShaderSource( bool effect );

/// The composite pass for one variant. The source build compiles the clip- and
/// mask-dependent background modes out entirely rather than sampling textures
/// that were never allocated.
std::string CompositeShaderSource( bool effect );

/// One pixel per palette entry, writing `paletteColour()`'s answer straight
/// out so it can be read back and compared against the CPU bake. Built from
/// the same palette fragment the stroke pass uses -- the one mirrored piece
/// of GLSL in the plugin. Exists only for `outruntest --palettes`.
std::string PaletteProbeShaderSource();

/// Spectrum bins in the Audio buffer parameter and in the stroke shader's
/// `Audio[]` uniform. The two must agree, and the shader's is a literal.
constexpr int kAudioBins = 64;

/// Sample points uploaded for the marched Lissajous curve: 48 segments.
constexpr int kCurvePoints = 49;

} // namespace outrun
