# Notes

Working notes for this repo: status, decisions, and the traps that have actually bitten.
Migrated out of Claude Code's memory on 2026-08-24, so they are written in the first
person and dated by when each thing was learned — that date is usually the useful part.

Cross-cutting notes that are not specific to this repo live in
[fleet-notes](https://github.com/stoatworks-labs/fleet-notes).

*outrun — neon synthwave strokes as ONE FFGL effect plugin with an Engine dropdown (A traces the clip, B generates paths). PUBLIC MIT, verify green*

**outrun** (`~/Projects/outrun`, PUBLIC MIT, `stoatworks-labs/outrun`) — neon
synthwave strokes as **ONE** FFGL effect plugin (`OU01`) with an Engine
dropdown (restructured 2026-08-04 at the user's request from the original
source+effect pair): **Engine A (Trace)** = [tinsel](https://github.com/stoatworks-labs/tinsel/blob/main/docs/NOTES.md) (`tinsel`)'s edge pipeline
into breakaway neon tubes (Echo/Angular/Scan/Flow/Rays); **Engine B (Paths)**
= 8 generators (perspective grid + striped sun flagship, Lissajous, hex,
circuit, skyline, rings, star, FFT waveform). One shader program, Engine as a
uniform branch; edge/stabilise passes run only under A; history resets on
engine change. **v0.1.0 RELEASED 2026-08-04** with all five homes done: GitHub
release (dmg/zips/NSIS), gen-downloads pass, website page + video-plugins
suite + /web-tools entry deployed, YouTube video `x1Qe6OMdTlQ` (rendered via
outruntest --pipe, embeds in README + projects.json), Instagram reel
published, and a **live WebGL2 demo at outrun.stoatworks-labs.com** (web/
in-repo, static-assets Worker; shaders.js follows Shaders.cpp, palette
texture is the harness's own bake). Still **never loaded in real Resolume**.
OFX port and USER-GUIDE remain future work.

**Why:** `NE01` was taken by [nesolume](https://github.com/stoatworks-labs/nesolume/blob/main/docs/NOTES.md) (`nesolume`) so the IDs are `OU01/OU02`.

**How it differs from tinsel:** unquantised stroke field (mass +
along-coordinate contract), width from `|d|/|∇d|` screen-space gradient; only
the palette lookup is CPU-mirrored (`--palettes`); paths/breakaway proven by
contact-sheet assertions + both-variant sweep. Two repo-specific traps beyond
the fleet's, both in AGENTS.md: subpixel lattices must drive distance to zero
past Nyquist (dFdx reads noise → speckle band), and continuous tubes expose
glow-blur tap ghosting that tinsel's dot lamps hide (three 1.55× pairs, not
two 1.8× ones).
