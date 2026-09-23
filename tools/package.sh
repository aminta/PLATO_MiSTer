#!/bin/sh
# Build the release files from the compiled core and platod:
#   releases/PLATO_<date>.rbf           FPGA core (MiSTer releases convention)
#   releases/PLATO_MiSTer_<date>.zip    everything for the SD card
set -e
TOP=$(cd "$(dirname "$0")/.." && pwd)
DATE=${1:-$(date +%Y%m%d)}
RBF=$TOP/core/output_files/PLATO.rbf
PLATOD=$TOP/daemon/build-arm/platod
[ -f "$RBF" ] || { echo "missing $RBF (tools/build_core.sh)"; exit 1; }
[ -f "$PLATOD" ] || { echo "missing $PLATOD (make -C daemon arm)"; exit 1; }

STAGE=$(mktemp -d)
mkdir -p "$STAGE/_Other" "$STAGE/PLATO" "$STAGE/Scripts"
cp "$RBF" "$STAGE/_Other/PLATO_$DATE.rbf"
cp "$PLATOD" "$STAGE/PLATO/platod"
cp -R "$TOP/dist/PLATO/." "$STAGE/PLATO/"
cp "$TOP/dist/Scripts/plato_install.sh" "$STAGE/Scripts/"
cp "$TOP/README.md" "$STAGE/PLATO/README.md"
chmod +x "$STAGE/PLATO/platod" "$STAGE/PLATO/plato_watch.sh" "$STAGE/Scripts/plato_install.sh"

mkdir -p "$TOP/releases"
cp "$RBF" "$TOP/releases/PLATO_$DATE.rbf"
rm -f "$TOP/releases/PLATO_MiSTer_$DATE.zip"
(cd "$STAGE" && zip -qr -X "$TOP/releases/PLATO_MiSTer_$DATE.zip" _Other PLATO Scripts)
rm -rf "$STAGE"
ls -la "$TOP/releases"
unzip -l "$TOP/releases/PLATO_MiSTer_$DATE.zip"
