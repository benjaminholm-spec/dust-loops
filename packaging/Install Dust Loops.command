#!/bin/bash
# Installs Dust Loops (instrument) and Dust Loops FX (effect) for Logic Pro.
# Double-click this file. It copies both plugins into your Audio Units folder and lets macOS load them.

cd "$(dirname "$0")" || exit 1
DEST="$HOME/Library/Audio/Plug-Ins/Components"
PLUGINS=("Dust Loops.component" "Dust Loops FX.component")

echo ""
echo "  Dust Loops installer"
echo "  --------------------"
echo ""

finish() {
    echo ""
    read -n 1 -s -r -p "  Press any key to close this window."
    echo ""
    exit "$1"
}

# Apple silicon only (M1 and later).
if [ "$(uname -m)" != "arm64" ]; then
    echo "  Sorry: Dust Loops only runs on Macs with Apple silicon (M1 or later)."
    echo "  This Mac has an Intel processor."
    finish 1
fi

# macOS 14 Sonoma or newer.
MAJOR="$(sw_vers -productVersion | cut -d. -f1)"
if [ "$MAJOR" -lt 14 ]; then
    echo "  Sorry: Dust Loops needs macOS 14 (Sonoma) or newer. This Mac has $(sw_vers -productVersion)."
    finish 1
fi

for p in "${PLUGINS[@]}"; do
    if [ ! -d "$p" ]; then
        echo "  Can't find \"$p\" next to this installer."
        echo "  Unzip the whole download first, then double-click the installer inside the unzipped folder."
        finish 1
    fi
done

mkdir -p "$DEST"
for p in "${PLUGINS[@]}"; do
    echo "  Installing $p ..."
    rm -rf "$DEST/$p"
    cp -R "$p" "$DEST/"
    # Downloaded files are quarantined, and Logic won't load a quarantined plugin. This plugin isn't
    # notarised by Apple (it's a free passion project), so clear the flag and sign it for this Mac.
    xattr -dr com.apple.quarantine "$DEST/$p" 2>/dev/null
    codesign --force --deep --sign - "$DEST/$p" >/dev/null 2>&1
done

# Make macOS notice the new plugins.
killall -9 AudioComponentRegistrar >/dev/null 2>&1

echo ""
echo "  Done. Dust Loops is installed."
echo ""
echo "  Next:"
echo "   1. Quit Logic Pro if it's open, then open it again."
echo "   2. Make a Software Instrument track and pick:"
echo "      Instrument slot > AU Instruments > Dust Planet Records > Dust Loops"
echo "   3. For the effect on an audio track: Audio FX slot > Audio Units > Dust Planet Records > Dust Loops FX"
echo ""
echo "  If Logic doesn't list it: Logic Pro > Settings > Plug-in Manager, select the Dust Loops entries"
echo "  and click \"Reset & Rescan Selection\"."
finish 0
