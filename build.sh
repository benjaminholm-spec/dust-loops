#!/bin/zsh
# Build Dust Loops (instrument + FX + Standalone), install both AUs, ad-hoc sign them, validate them
# with auval and run the unit tests. Any failure stops the script.
#   ./build.sh                 Release build
#   CONFIG=Debug ./build.sh    Debug build (separate build-debug folder)
#   SKIP_AUVAL=1 ./build.sh    skip auval (faster iteration)
set -euo pipefail
cd "${0:A:h}"

CONFIG=${CONFIG:-Release}
BUILD=build; [[ "$CONFIG" == Debug ]] && BUILD=build-debug
JUCE_TAG=9.0.4
COMPONENTS=~/Library/Audio/Plug-Ins/Components

# Read identity from CMakeLists.txt so this script never needs editing on a rename.
name=$(sed -n 's/^set(DL_PRODUCT_NAME *"\(.*\)")/\1/p' CMakeLists.txt)
mfr=$(sed -n 's/^set(DL_MANUFACTURER *\([A-Za-z0-9]\{4\}\).*/\1/p' CMakeLists.txt)
instr=$(sed -n 's/^set(DL_CODE_INSTR *\([A-Za-z0-9]\{4\}\).*/\1/p' CMakeLists.txt)
fx=$(sed -n 's/^set(DL_CODE_FX *\([A-Za-z0-9]\{4\}\).*/\1/p' CMakeLists.txt)

if [[ ! -f JUCE/CMakeLists.txt ]]; then
    echo "==> Fetching JUCE $JUCE_TAG"
    git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git JUCE
fi

echo "==> Building ($CONFIG)"
[[ -f $BUILD/build.ninja ]] || cmake -S . -B $BUILD -G Ninja -DCMAKE_BUILD_TYPE=$CONFIG
cmake --build $BUILD --target DustLoops_All DustLoopsFX_All DustLoopsTests

echo "==> Unit tests"
$BUILD/DustLoopsTests_artefacts/$CONFIG/DustLoopsTests

echo "==> Installing to $COMPONENTS"
mkdir -p $COMPONENTS
for c in "$name" "$name FX"; do
    rm -rf "$COMPONENTS/$c.component"
    target=DustLoops; [[ "$c" == *FX ]] && target=DustLoopsFX
    cp -R "$BUILD/${target}_artefacts/$CONFIG/AU/$c.component" "$COMPONENTS/"
    codesign --force --deep --sign - "$COMPONENTS/$c.component"
done
# Make the AU registrar notice the new builds without logging out.
killall -9 AudioComponentRegistrar 2>/dev/null || true

if [[ "${SKIP_AUVAL:-0}" != 1 ]]; then
    echo "==> auval: instrument (aumu $instr $mfr)"
    auval -strict -v aumu $instr $mfr > $BUILD/auval-instrument.log 2>&1 || { tail -40 $BUILD/auval-instrument.log; exit 1; }
    tail -3 $BUILD/auval-instrument.log
    echo "==> auval: effect (aumf $fx $mfr)"
    auval -strict -v aumf $fx $mfr > $BUILD/auval-fx.log 2>&1 || { tail -40 $BUILD/auval-fx.log; exit 1; }
    tail -3 $BUILD/auval-fx.log
fi

echo "==> Done. Standalone app: $BUILD/DustLoops_artefacts/$CONFIG/Standalone/$name.app"
