#pragma once

/**
    The source variant's generator paths.

    C++ carries only the enum and the names; the distance functions live in the
    stroke shader (Shaders.cpp) and nowhere else. That follows the fleet's
    split (orrery's Shapes.h): the harness proves each path is *alive and
    distinct* -- by contact sheet and by sweep -- rather than mirroring per-pixel
    distance maths a second time to restate the shader.

    Every path is a pure function of (uv, phase, scale, detail, horizon, and
    for Waveform the smoothed spectrum). No state, so nothing drifts with the
    frame rate, beat sync is free, and any frame renders on its own.
*/
namespace outrun
{

enum class Path
{
	Grid = 0,   ///< The perspective ground grid with the striped sun. The flagship.
	Lissajous,  ///< A marched harmonic curve; non-integer ratios precess.
	Hex,        ///< A scrolling hexagonal lattice.
	Circuit,    ///< PCB traces: per-cell hashed motifs that stay connected.
	Skyline,    ///< A hashed roofline over folded-noise mountains, on parallax.
	Rings,      ///< Log-spaced tunnel rings with spokes.
	Star,       ///< A five-pointed star outline, spinning.
	Waveform,   ///< The smoothed spectrum as a mirrored oscilloscope trace.

	Count
};

const char* PathName( Path path );

} // namespace outrun
