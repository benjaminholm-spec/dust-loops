#!/bin/bash
# Removes Dust Loops and Dust Loops FX. Your presets and settings in ~/Music/Dust Loops are kept.

DEST="$HOME/Library/Audio/Plug-Ins/Components"

echo ""
echo "  Dust Loops uninstaller"
echo "  ----------------------"
echo ""
for p in "Dust Loops.component" "Dust Loops FX.component"; do
    if [ -d "$DEST/$p" ]; then
        rm -rf "$DEST/$p"
        echo "  Removed $p"
    fi
done
killall -9 AudioComponentRegistrar >/dev/null 2>&1

echo ""
echo "  Done. Your own presets and settings are still in: ~/Music/Dust Loops"
echo "  (delete that folder too if you want them gone)."
echo ""
read -n 1 -s -r -p "  Press any key to close this window."
echo ""
