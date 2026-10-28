#!/bin/bash
# Builds Dust Loops and packages a download for a GitHub Release:
#   dist/Dust-Loops-<version>-macOS-AppleSilicon.zip
# containing both plugins, the double-click installer and uninstaller, a read-me and the licence.
#   tools/make_release.sh            build (with tests and auval) and package
#   SKIP_BUILD=1 tools/make_release.sh   package the existing build
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="$(sed -n 's/^project(DUST_LOOPS VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
NAME="Dust Loops $VERSION"
STAGE="dist/$NAME"
ZIP="dist/Dust-Loops-$VERSION-macOS-AppleSilicon.zip"

if [ "${SKIP_BUILD:-0}" != "1" ]; then
    ./build.sh
fi

rm -rf "$STAGE" "$ZIP"
mkdir -p "$STAGE"
ditto "build/DustLoops_artefacts/Release/AU/Dust Loops.component" "$STAGE/Dust Loops.component"
ditto "build/DustLoopsFX_artefacts/Release/AU/Dust Loops FX.component" "$STAGE/Dust Loops FX.component"
cp "packaging/Install Dust Loops.command" "packaging/Uninstall Dust Loops.command" "packaging/READ ME FIRST.txt" "$STAGE/"
cp LICENSE "$STAGE/LICENSE.txt"
chmod +x "$STAGE/"*.command

# Apple silicon only: refuse to ship anything else by accident.
for bin in "$STAGE/Dust Loops.component/Contents/MacOS/Dust Loops" "$STAGE/Dust Loops FX.component/Contents/MacOS/Dust Loops FX"; do
    [ "$(lipo -archs "$bin")" = "arm64" ] || { echo "Unexpected architecture in $bin"; exit 1; }
done

ditto -c -k --sequesterRsrc --keepParent "$STAGE" "$ZIP"
echo "Packaged: $ZIP ($(du -h "$ZIP" | cut -f1))"
