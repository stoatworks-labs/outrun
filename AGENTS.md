# outrun — orientation for another LLM (or a newcomer)

**What it is:** neon synthwave strokes as **two** FFGL 2.1 plugins for
Resolume Arena/Avenue. `Outrun` is a source that generates neon paths — the
perspective grid with the striped sun is the flagship — and `Outrun Trace` is
an effect that traces the clip's own outlines as neon tubes and lets them
break away from the real geometry. C++17 + GLSL 4.10, CMake, universal macOS
`.bundle`s and Windows `.dll`s. Public, MIT,
`github.com/stoatworks-labs/outrun`.

`CLAUDE.md` is the command reference — build, install, verify. This file is
the *why*: read it before touching the stroke field, the breakaway warps, or
the path library.

Family notes: the edge pipeline (copy/edge/stabilise), the glow, the palette
scheme and the harness shape are lifted from **tinsel**; the twin-plugin
structure, the Sync recovery and the audio smoothing from **orrery** and
**downpour**. Where those repos document a trap, it applies here too.

---

## The one idea

**Every stroke producer answers the same two questions about one pixel: how
much tube is here, and where along the stroke is it.**

That pair — `strokeField()` returning (mass, along-coordinate) — is the whole
interface between "what the strokes trace" and "what a neon tube looks like".
The effect answers it from the stabilised edge mask; the source answers it
from a distance function; and everything downstream — breakaway, palettes,
clip colour, the white-hot core, the glow — is one shared piece of shader
text that cannot diverge between the two plugins, because there is only one
of it.

Three design rules fall out, and each is load-bearing:

- **A tube is measured in pixels, via the field's own screen-space
  gradient.** `|d| / |∇d|` is the first-order distance in pixels whatever
  units `d` arrives in — so a path function only needs a continuous field
  whose zero set is the stroke, mixed units across a `min()` are fine, and a
  perspective grid line thins toward the horizon without any per-path effort.
  This is tinsel's lamps-per-pixel division, applied to a distance.
- **Breakaway warps where the field is *sampled*, never what it is.** Echo
  re-samples shrunk toward the centroid; Angular bends the sampling angle;
  Scan/Flow displace it; Rays march it. No feedback, no contour tracing, no
  state — the same frame at the same phase is the same picture, which is what
  makes any frame renderable on its own and the whole thing testable.
- **The phase is the only clock the shader sees.** Free integrates it (Speed
  changes what happens next, never rescales history); Beat and Bar recover an
  absolute bar number from the host transport (`within + round(estimate −
  within)` — orrery's recovery); Manual hands the Phase slider the wheel.

### What is deliberately NOT mirrored

The path distance functions and the breakaway warps exist **only in GLSL**.
Orrery's precedent: a CPU mirror of per-pixel maths is a second
implementation bought to restate the shader, and it would be tested against
itself. What the harness proves instead is that every path and every break
mode is *alive and distinct* (`--paths`/`--breaks` contact sheets with
assertions) and that no control is dead (`tools/sweep.py`). The one mirrored
piece is the palette lookup — `--palettes` compares the exact GLSL text the
stroke pass runs against the C++ bake, via a probe assembled around the same
string.

---

## The traps

Ordered by how much time they will cost you.

**The finite-difference gradient lies past Nyquist.** `dFdx` of a triangle
wave sampled at more than ~half a cell per pixel is noise, and `|d| / noise`
renders as a band of speckle exactly where the eye expects the horizon bloom.
The fix is not anti-aliasing: infinitely many lines per pixel IS solid, so
the grid and the tunnel **drive their distance to zero** as cells go
subpixel (`cellsPerPixel` in `pathGrid`/`pathRings`). Any new path with a
repeating lattice needs the same merge, and the analytic cells-per-pixel to
drive it — which is why `PictureSize` is a uniform.

**A continuous tube exposes blur ghosting that dots hide.** The glow is
separable Gaussians run in widening pairs, and a pair only reads as smooth
falloff while the accumulated blur underneath covers the gaps between its
five taps. Tinsel ships two pairs at 1.8×; on continuous strokes that ratio
puts a coherent ghost line parallel to every tube. Outrun runs three pairs at
1.55× for exactly this reason — do not "optimise" it back down without
looking at a long straight stroke at high Glow Size.

**Height discontinuities are the building sides.** The skyline's roofline
jumps at every building edge; the derivative spike there makes `dPx ≈ 0` and
draws a vertical stroke the full height of the jump. That is not an artefact
— it is the only reason the buildings have sides — and the same mechanism
puts a bright seam anywhere else a path function is discontinuous. Make a
path discontinuous on purpose or not at all.

**`ScopedFBOBinding` does not restore the viewport** (SDK b1afaf9). The host
viewport is captured at the top of `Render()` and restored before the
composite; without it the composite inherits the quarter-size glow viewport
and the plugin renders into a corner — which in most viewers reads as "blown
out to white with a small picture bottom-left", not as a viewport bug.

**Every `ffglex::Scoped*` binding clears to 0 on scope exit — it does not
restore.** So every `PassBuffer::Ensure()` happens before anything binds a
texture: `FFGLFBO::Initialise` sizes its colour texture under a scoped
binding, and allocating mid-chain silently unbinds the input texture. The
symptom is the dangerous part: correct on every frame except the one that
allocates. The same reasoning is why `setStrokeUniformsAndDraw()` is called
*inside* the effect's texture-binding scope.

**`layout` is a GLSL keyword.** So are `flat`, `active`, `filter`, `input`,
`output`, `sample`, `common`. The stroke shader is assembled from strings, so
the "syntax error, line N" a reserved word produces points into a file that
does not exist. The trace uniform is `Trace` and its local is `wiring`.

**A ranged parameter cannot have a ranged default.** `SetParamInfo` clamps an
`FF_TYPE_STANDARD` default into 0..1 *before* `SetParamRange` could widen it,
so every host parameter is 0..1 and every conversion lives in `Controls.cpp`.
Option parameters are the exception: they hold the element value.

**`SetParamGroup` collapses runs of consecutive same-group ids.** The id
order in `Controls.h` is therefore load-bearing: reorder it, or insert a
parameter mid-enum, and a group silently splits in two (and every saved
composition renumbers). Append only.

**The plugin registers itself from a file-scope constructor.** `outrun_core`
is an OBJECT library and each `CFFGLPluginInfo` lives in its own entry file,
listed only in its own MODULE target. A STATIC core drops the registration
TU and ships a bundle that loads, exports `plugMain`, and contains no
plugins; an entry file in the shared core registers both plugins into both
bundles. Verify with `nm -gU … | grep _plugMain` *and* a host load.

**Randomness must be integer.** `fract(sin(x)·43758…)` is the driver's
answer, so two GPUs disagree about which building is tall and which scanline
jumps. The shader carries a PCG mix (`HashInt`); the C++ side uses
`Hash.h`'s `lowbias32`. Neither may be replaced by anything transcendental.

**The harness needs the synthetic transport, all three parts.** `driveClock`
advances `SetTime`, `SetBeatInfo` (120 BPM 4/4 from zero) and a fixed
synthetic spectrum together. Drop any one and something measurably does
nothing offline: no time → Speed dead; no beat → Beat/Bar step once a bar;
no spectrum → both Audio knobs dead — and `sweep.py` will duly report it.

**The sweep's awkward values are load-bearing.** 0, 0.5, 1 lands periodic
patterns on pixel-identical frames and reports a working slider dead; 0.137
and 0.611 are not rational multiples of anything swept. The CONTEXT table is
the other half: most parameters are *supposed* to do nothing by default
(Break Amount in mode None, swatches under a baked palette, Stability on a
still frame), and each entry states what has to be true before the control
can act. Add a parameter, add its context, or the sweep fails honestly.

---

## Shape of the code

    source/Outrun.{h,cpp}    the one plugin class: parameters, buffers, passes.
    source/Shaders.{h,cpp}   all GLSL. Stroke + composite assembled per
                             variant around #define OUTRUN_EFFECT.
    source/Controls.{h,cpp}  0..1 host parameters to physical units; the enums.
    source/Palette.{h,cpp}   16 palettes as gradient stops; bakes the table.
    source/Paths.{h,cpp}     the Path enum and names. The maths is in GLSL.
    source/Presets.h         8 factory presets, plain data, host-agnostic.
    source/PassBuffer.*      FFGLFBO with tinsel's leak fix, three samplings.
    source/{Source,Effect}Plugin.cpp   the two registrations. See traps.
    source/Diag.*            a log file, for the shader that will not compile.
    tools/outruntest/        the offline harness (drives the real classes).
    tools/sweep.py           no control is silently dead, both variants.
    tools/verify.sh          all of it.

Pass chains: the effect runs copy → edge → stabilise → **stroke** → glow×6 →
composite (the first three and the glow/composite are tinsel's, lifted); the
source runs stroke → glow×6 → composite, and never allocates the edge
buffers. The stabilise buffer's `.gba` carries (x·mask, y·mask, mask), so the
artwork centroid is one `textureLod` off the top of its mip chain.
