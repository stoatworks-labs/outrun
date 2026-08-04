#!/usr/bin/env python3
"""Move every parameter and fail if any of them made no difference to the frame.

**This is the only thing in the repo that catches a dead control**, and it is
not a theoretical risk. A GLSL uniform whose name does not match the C++ is
silently ignored -- `glGetUniformLocation` returns -1 and `glUniform` on -1 is
a documented no-op -- so a slider can be stone dead while everything compiles,
links, loads and renders. The contact sheets will not catch it either: they
only ever exercise one setting of everything else.

## Why there is a context table

Many parameters are *supposed* to do nothing in the default configuration, and
a naive sweep would report a stack of false failures:

- `Break Amount` does nothing in Break Mode None; `Break Mode` does nothing at
  Break Amount zero. Each is the other's context.
- The colour swatches are ignored unless the Palette is one of the two
  swatch-driven entries.
- `Direction` steers the Linear trace and the Rays mode; neither is on by
  default.
- `Stability` filters the edge signal over *time*, so on a still card it
  provably does nothing -- its context adds per-frame noise, which is the
  situation the control exists for.
- `Speed` and `Sync` need the clock actually running, which is what
  `--time` is for; everything else runs against a pinned phase so that two
  renders of the same settings are the same picture.

Every parameter is swept on the engine it belongs to: the Engine A group and
the clip-facing output controls run on the default engine over the synthetic
card; the Engine B group runs with Engine=1; everything shared runs on A.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose]
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

# Applied to every render. Nothing needed: the defaults already show strokes
# in both variants, which was a design goal of the defaults.
BASE = []

# What else has to be true for a parameter to be able to do anything. Entries
# are extra `--set` assignments, except entries starting with "--", which are
# passed through as raw harness arguments.
CONTEXT = {
    # The breakaway pair gate each other.
    "Break Mode":   ["Break Amount=0.7"],
    "Break Amount": ["Break Mode=1"],
    "Break Spread": ["Break Mode=1", "Break Amount=0.7"],
    "Break Hue":    ["Break Mode=1", "Break Amount=0.7"],

    # Audio Break feeds the same effective amount, from the bass of the
    # harness's synthetic spectrum.
    "Audio Break":  ["Break Mode=1"],

    # The swatches are read by the two swatch-driven palettes only.
    "Colour 1":       ["Palette=0"],
    "Colour1_Green":  ["Palette=0"],
    "Colour1_Blue":   ["Palette=0"],
    "Colour 2":       ["Palette=1"],
    "Colour2_Green":  ["Palette=1"],
    "Colour2_Blue":   ["Palette=1"],

    # Direction steers the Linear trace.
    "Direction":    ["Trace=2"],

    # The Engine B group.
    "Path":         ["Engine=1"],
    "Path Size":    ["Engine=1"],
    "Path Detail":  ["Engine=1"],
    "Horizon":      ["Engine=1"],

    # Dim only dims the Dimmed Source background.
    "Dim":          ["Background=2"],

    # A temporal filter provably does nothing on a still frame. The noise
    # pushes marginal pixels back and forth across the threshold, which is
    # the situation the control exists for.
    "Stability":    ["--noise", "0.12", "--frames", "40"],
}

# Parameters that need the host clock running rather than a pinned phase.
NEEDS_CLOCK = {"Speed", "Sync"}

# Parameter *kinds* that have no scalar value to sweep. "Audio" is an FFT
# buffer: its float value is meaningless, so sweeping it proves nothing and
# reports a false dead. outruntest feeds every render a synthetic spectrum,
# and the two Audio knobs are the sweepable proof the buffer is wired through.
SKIP_KINDS = {"buffer"}

# The values every non-option parameter is swept across. The awkward numbers
# are load-bearing: patterns periodic in a parameter can land 0, 0.5 and 1 on
# pixel-identical frames and report a working slider as dead. 0.137 and 0.611
# are not rational multiples of anything swept here.
SWEEP_VALUES = [0.0, 0.137, 0.611, 1.0]

# Option parameters are swept across their elements instead.
OPTION_RANGE = {
    "Engine": 2,
    "Detect On": 4,
    "Path": 8,
    "Trace": 4,
    "Break Mode": 6,
    "Colour Mode": 3,
    "Palette": 16,
    "Sync": 4,
    "Background": 5,
    # Custom plus every entry in source/Presets.h. --list reports a
    # parameter's kind but not its element count, so this one number tracks
    # that table by hand; the --presets contact sheet is what actually
    # renders all of them, so a stale count here under-sweeps rather than
    # letting a preset go unchecked.
    "Preset": 1 + 16,
}


def read_png(path):
    """Decode a PNG to raw bytes. Enough of the format for our own writer's
    output -- 8-bit RGBA, one IDAT, filter 0 on every row."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")

    pos = 8
    idat = b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IDAT":
            idat += body
        pos += 12 + length

    return zlib.decompress(idat)


def render(harness, out, settings, raw, clock, verbose):
    args = [str(harness), "--out", str(out), "--size", "480x270"]
    if clock:
        args += ["--time", "2.0"]
    else:
        args += ["--phase", "0.37"]
    args += raw
    for setting in settings:
        args += ["--set", setting]

    if verbose:
        print("   ", " ".join(args))

    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"outruntest failed: {result.stderr.strip()}")

    return read_png(out)


def parameters(harness):
    """Name and kind of every parameter, in declaration order."""
    args = [str(harness), "--list"]

    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"outruntest --list failed: {result.stderr.strip()}")

    found = []
    for line in result.stdout.splitlines()[1:]:
        # id, name (may contain spaces), kind, default
        parts = line.split()
        if len(parts) < 4:
            continue
        kind = parts[-2]
        name = " ".join(parts[1:-2])
        found.append((name, kind))

    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    harness = REPO / args.build / "outruntest"
    if not harness.exists():
        print(f"no outruntest at {harness} -- build first", file=sys.stderr)
        return 2

    dead = []
    checked = 0

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)

        for name, kind in parameters(harness):
            if kind in SKIP_KINDS:
                continue

            # Context entries that start "--" are raw harness arguments
            # (a value follows each, also in the list); the rest are
            # --set assignments.
            context = CONTEXT.get(name, [])
            extra = []
            sets = []
            raw = iter(context)
            for token in raw:
                if token.startswith("--"):
                    extra += [token, next(raw)]
                else:
                    sets.append(token)

            clock = name in NEEDS_CLOCK
            base = BASE + sets

            if name in OPTION_RANGE:
                values = [float(i) for i in range(OPTION_RANGE[name])]
            else:
                values = SWEEP_VALUES

            frames = []
            for value in values:
                out = tmp / "sweep.png"
                frames.append(
                    render(harness, out, base + [f"{name}={value}"],
                           extra, clock, args.verbose))

            checked += 1
            if all(f == frames[0] for f in frames[1:]):
                dead.append(f"{name} ({kind})")
                print(f"  DEAD {name}")
            elif args.verbose:
                print(f"  ok   {name}")

    print()
    if dead:
        print(f"sweep: {checked} parameters, {len(dead)} made no difference:")
        for entry in dead:
            print(f"  - {entry}")
        return 1

    print(f"sweep: {checked} parameters, all live")
    return 0


if __name__ == "__main__":
    sys.exit(main())
