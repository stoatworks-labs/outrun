# outrun

Neon synthwave strokes — as **two** FFGL plugins for Resolume Arena/Avenue: a
source (`Outrun`, generated paths + audio waveform) and an effect
(`Outrun Trace`, the clip's outlines as breakaway neon tubes). C++/GLSL, CMake
MODULE → universal `.bundle` (macOS) + Windows `.dll`. Public MIT repo.

Read `AGENTS.md` before changing the stroke field, the breakaway warps or the
path distance functions.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install both bundles to Resolume: `cmake --install build`
- Render the source offline: `./build/outruntest --out /tmp/frame.png --phase 0.6`
- The effect over a test card: `./build/outruntest --effect --out /tmp/frame.png`
- Drive the real clock instead of pinning: `--time 2.0`
- List parameters: `./build/outruntest --list`
- Contact sheets: `./build/outruntest --paths /tmp/p.png --breaks /tmp/b.png`
  (each also asserts every entry is live and distinct)
- Set anything by name: `--set "Path=5" --set "Break Amount=0.7"`
- Put real footage through the real shaders (for the project video):
  `ffmpeg … -f rawvideo -pix_fmt rgba - | ./build/outruntest --effect --pipe --width W --height H [--script cues.txt] | ffmpeg …`

## Verify
- Everything: `tools/verify.sh`
- GLSL palette lookup vs the C++ bake: `./build/outruntest --palettes`
- No dead controls: `python3 tools/sweep.py`
- Render cost: `./build/outruntest --bench` (and `--effect --bench`) —
  source 0.54 ms/frame at 1080p, 1.05 at 4K; effect 0.67 and 2.57 (M-series)
- Universal + exports: `lipo -archs` and `nm -gU … | grep _plugMain` on BOTH
  bundles — never trust the build log for either.

## Notes
- **Both plugins are one class** (`OutrunPlugin(bool overInput)`), one shader
  source specialised by an injected `#define OUTRUN_EFFECT`. The entry files
  `SourcePlugin.cpp`/`EffectPlugin.cpp` are listed in their own MODULE targets,
  never in `outrun_core` — and the core is an **OBJECT** library, or the
  linker drops the self-registering `CFFGLPluginInfo`. IDs `OU01`/`OU02`.
- **The stroke field contract**: every producer returns (tube mass, coordinate
  along the stroke). The effect's mass is the stabilised edge mask at a
  width-dependent mip level; the source's is a path distance divided by its
  own screen-space gradient — which is what keeps a receding grid line the
  same on-screen width as a nearby one.
- **Path maths is GPU-only.** No C++ mirror; the harness proves paths by
  contact sheet + sweep. The palette lookup is the one mirrored piece
  (`--palettes`). The Lissajous curve is solved on the CPU and uploaded as 49
  points.
- **Breakaway warps where the field is *sampled*, never what it is.** All
  modes are stateless; the base tap always survives so Break Amount is a true
  morph.
- All host parameters are 0..1 and mapped in `Controls.cpp`. `SetParamInfo`
  clamps a standard default into 0..1 before `SetParamRange` can widen it.
  Option parameters hold the element value.
- `flat`, `active`, `filter`, `input`, `output`, `sample`, `common`, `layout`
  are GLSL reserved words. Shader errors surface only at runtime, in the
  diagnostics log.
- Randomness is an integer PCG hash, never `fract(sin(x)*…)`.
- macOS build must be universal (arm64 + x86_64). Verify with `lipo`, never
  the build log.
- The harness drives `SetTime` **and** `SetBeatInfo` on a synthetic clock
  (120 BPM 4/4 from zero), and writes a fixed synthetic spectrum every render.
  Without any of the three, some control measurably does nothing offline.
- Public repo. "Commit" = commit **and** push.

## Diagnostics

`source/Diag.{h,cpp}` — log file only, no crash handler (this runs inside
Resolume). It exists for the failure that actually happens: a shader that will
not compile, which otherwise looks like "the plugin does nothing" with no
message anywhere. It records which pass, and the GL vendor/renderer next to it.

    ~/Library/Logs/outrun/outrun.YYYY-MM-DD.log
