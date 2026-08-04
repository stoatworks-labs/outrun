# outrun

Neon synthwave strokes for Resolume Arena/Avenue, as a pair of FFGL plugins:

- **Outrun** (source) generates neon paths from nothing: the perspective grid
  with the striped sun, tunnel rings, hex lattices, circuit traces, a city
  skyline over folded mountains, a spinning star, a Lissajous figure -- or the
  routed audio as a mirrored oscilloscope trace.
- **Outrun Trace** (effect) finds the outlines in whatever is on the layer and
  draws them as continuous glowing neon tubes -- and then lets them **break
  away** from the real geometry: echoes drifting outward, outlines snapped to
  45° technical linework, scanline glitches, a flow field, comet rays. Break
  Amount morphs from the faithful outline to the synthetic pattern.

Strokes are coloured from sixteen authored synthwave palettes, from two
swatches, or from the clip's own colours along its own outline. Everything can
run free, locked to Resolume's beat or bar, or driven manually, and the
spectrum of any routed audio can gate the strokes, drive the breakaway, or be
the picture itself.

**Video:** [What it does, in 49 seconds](https://www.youtube.com/watch?v=x1Qe6OMdTlQ)
**Try it live:** [outrun.stoatworks-labs.com](https://outrun.stoatworks-labs.com) — the same GLSL in your browser

![The perspective grid](docs/grid.png)

## Install

Drop the bundle (macOS) or the DLL (Windows) into Resolume's plugin folder,
then restart Resolume:

    ~/Documents/Resolume Arena/Extra Effects      (or "Resolume Avenue")

On macOS the bundles are unsigned; see `docs/UNSIGNED.md` if Gatekeeper
objects, or build from source:

    git clone --recursive https://github.com/stoatworks-labs/outrun
    cd outrun
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    cmake --install build

"Outrun" appears under Effects. Engine B needs any clip under it -- a solid
black clip works, and the Background modes decide whether the clip shows.

## The controls, briefly

- **Engine**: A traces the clip, B generates paths.
- **Engine A — Trace**: what counts as an outline. `Detect On` picks the
  channel (Luma or Alpha is right for artwork either way), `Sensitivity` and
  `Softness` set the threshold, `Detail` the scale, `Stability` how long an
  edge survives a flickery frame. Set them against the `Edges` background,
  which shows the raw mask. `Trace` decides how colour runs along the
  outline.
- **Engine B — Paths**: which generator, its size, its density, and where
  the horizon sits.
- **Stroke**: tube `Width` in pixels, and `Core` -- how much of the tube
  saturates to white.
- **Breakaway**: the mode, the amount (0 is the faithful geometry), a
  per-mode spread, and a per-copy palette shift.
- **Colour**: palette or clip colours, palette cycles per run (`Spread`),
  saturation, brightness.
- **Tempo**: free-running, beat- or bar-locked, or manual (drive `Phase` from
  Resolume's own animation).
- **Audio**: route audio into the `Audio` picker; `Audio Level` lays the
  spectrum along the strokes as a brightness gate, `Audio Break` lets the
  bass shove the strokes off the geometry. The `Waveform` path draws the
  spectrum itself.

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/outrun/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`outrun-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/outrun/releases/download/v0.1.0/outrun-0.1.0-macos-universal.dmg) | 220 KB |
| Universal (Apple Silicon + Intel) · .zip archive | [`outrun-macos-universal.zip`](https://github.com/stoatworks-labs/outrun/releases/latest/download/outrun-macos-universal.zip) | 176 KB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`outrun-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/outrun/releases/download/v0.1.0/outrun-0.1.0-windows-x86_64-setup.exe) | 222 KB |
| x64 · .zip archive | [`outrun-windows-x86_64.zip`](https://github.com/stoatworks-labs/outrun/releases/latest/download/outrun-windows-x86_64.zip) | 117 KB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/outrun/releases](https://github.com/stoatworks-labs/outrun/releases).

The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## Building and testing

C++17 + GLSL 4.10, CMake, FFGL 2.1 (SDK vendored as a submodule). macOS
builds are universal (arm64 + x86_64); Windows needs GLEW via vcpkg.

The offline harness renders the real plugin classes headlessly:

    ./build/outruntest --out /tmp/frame.png            # Engine A over a test card
    ./build/outruntest --set "Engine=1" --out ...      # Engine B, the grid
    ./build/outruntest --paths /tmp/paths.png          # every path, checked distinct
    ./build/outruntest --breaks /tmp/breaks.png        # every break mode, likewise
    ./build/outruntest --palettes                      # GLSL palettes vs the C++ bake
    python3 tools/sweep.py                             # no control is silently dead
    tools/verify.sh                                    # all of it

## Licence

MIT. The palettes are authored for this repo; nothing is imported from
non-MIT palette collections.
