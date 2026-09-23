#!/bin/sh
# Copy the PLATO core to the MiSTer over SSH.
#
#   tools/deploy.sh [mister-host]      (default: 192.168.0.135)
#
# Installs:
#   /media/fat/_Other/PLATO_<date>.rbf    FPGA core
#   /media/fat/PLATO/platod               ARM program
#   /media/fat/PLATO/plato_watch.sh       starts platod when the core loads
# and adds plato_watch.sh to /media/fat/linux/user-startup.sh (once,
# with a backup of the original file).

set -e
HOST=${1:-192.168.0.135}
TOP=$(cd "$(dirname "$0")/.." && pwd)
SSH="ssh -o BatchMode=yes root@$HOST"

RBF=$TOP/core/output_files/PLATO.rbf
PLATOD=$TOP/daemon/build-arm/platod

[ -f "$PLATOD" ] || { echo "missing $PLATOD (run: make -C daemon arm)"; exit 1; }

$SSH 'mkdir -p /media/fat/PLATO /media/fat/_Other'
if [ -f "$RBF" ]; then
    DATE=$(date +%Y%m%d)
    $SSH 'rm -f /media/fat/_Other/PLATO_*.rbf'
    scp -q "$RBF" "root@$HOST:/media/fat/_Other/PLATO_$DATE.rbf"
    echo "core:   /media/fat/_Other/PLATO_$DATE.rbf"
fi

# Replace platod even while it runs
scp -q "$PLATOD" "root@$HOST:/media/fat/PLATO/platod.new"
$SSH 'mv -f /media/fat/PLATO/platod.new /media/fat/PLATO/platod && chmod +x /media/fat/PLATO/platod'
scp -q "$TOP/dist/PLATO/plato_watch.sh" "$TOP/dist/PLATO/platod.ini.example" "root@$HOST:/media/fat/PLATO/"
$SSH 'chmod +x /media/fat/PLATO/plato_watch.sh'
echo "platod: /media/fat/PLATO/platod"

$SSH 'f=/media/fat/linux/user-startup.sh
if ! grep -q plato_watch.sh $f; then
    cp $f $f.bak-plato
    printf "\n# PLATO core: start platod when the core is loaded\n[ -x /media/fat/PLATO/plato_watch.sh ] && /media/fat/PLATO/plato_watch.sh &\n" >> $f
    echo "user-startup.sh updated (backup: user-startup.sh.bak-plato)"
fi
if ! ps | grep -q "[{]plato_watch.sh[}]"; then
    (setsid /media/fat/PLATO/plato_watch.sh </dev/null >/dev/null 2>&1 &)
    echo "watcher started"
fi'
