#!/usr/bin/env bash
#
# Everything, in the order that fails fastest.
#
# The build is universal on purpose. An arm64-only bundle builds and tests
# perfectly well here and then fails to load in an Intel Resolume, and the
# build log calls it a success either way -- so the architecture is checked
# with lipo, never with the log.
#
#     tools/verify.sh [BUILD_DIR]
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$REPO/build-verify}"

cd "$REPO"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
fail() { printf '\033[31mFAIL\033[0m %s\n' "$1"; exit 1; }

#---------------------------------------------------------------------------
step "Submodule"
#---------------------------------------------------------------------------
if [[ ! -f external/ffgl/CMakeLists.txt ]]; then
	fail "FFGL SDK missing -- run: git submodule update --init --recursive"
fi
echo "ok   FFGL SDK present at $(git -C external/ffgl rev-parse --short HEAD)"

#---------------------------------------------------------------------------
step "Build (universal)"
#---------------------------------------------------------------------------
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)" >/dev/null
echo "ok   built"

#---------------------------------------------------------------------------
step "Bundles"
#---------------------------------------------------------------------------
for name in "Outrun"; do
	binary="$BUILD/$name.bundle/Contents/MacOS/$name"

	[[ -f "$binary" ]] || fail "$name: no binary at $binary"

	# Universal. The failure this catches ships a plugin that simply does not
	# appear in half the Resolume installs it is given to.
	arches="$(lipo -archs "$binary")"
	[[ "$arches" == *arm64* ]]  || fail "$name: no arm64 slice (got: $arches)"
	[[ "$arches" == *x86_64* ]] || fail "$name: no x86_64 slice (got: $arches)"

	# The entry point. A bundle whose registration got dropped by the linker
	# still loads and still exports this -- the OBJECT-library note in
	# CMakeLists.txt is what actually guards the registration; this catches a
	# build that produced no module at all.
	nm -gU "$binary" | grep -q '_plugMain' || fail "$name: plugMain not exported"

	echo "ok   $name: $arches, plugMain exported"
done

#---------------------------------------------------------------------------
step "Checks"
#---------------------------------------------------------------------------
# The palette lookup is the one piece of GLSL with a CPU mirror; the contact
# sheets prove every path and break mode is alive and distinct -- which
# exercises the field producers, the warps, the width normalisation and the
# glow, all at once, in both variants.
"$BUILD/outruntest" --palettes
"$BUILD/outruntest" --paths "$BUILD/paths-sheet.png"
"$BUILD/outruntest" --breaks "$BUILD/breaks-sheet.png"
# The factory presets get the same treatment, and need it more: a preset is a
# table of numbers with nothing to stop two entries drifting onto the same
# picture, and this is also the only check that drives applyPreset at all.
"$BUILD/outruntest" --presets "$BUILD/presets-sheet.png"

#---------------------------------------------------------------------------
step "Dead controls"
#---------------------------------------------------------------------------
# The only thing that catches a uniform whose name does not match the C++.
python3 tools/sweep.py --build "$(basename "$BUILD")"

printf '\n\033[32mall green\033[0m\n'
