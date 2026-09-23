#!/bin/sh
# Starts platod (the ARM side of the PLATO core) whenever the PLATO core is
# loaded from the MiSTer menu.  platod exits by itself when another core is
# loaded.  Started from /media/fat/linux/user-startup.sh.

DIR=/media/fat/PLATO

check() {
    if [ "$(cat /tmp/CORENAME 2>/dev/null)" = "PLATO" ] && ! pidof platod >/dev/null; then
        "$DIR/platod" >>/tmp/platod.log 2>&1 &
    fi
}

while [ ! -e /tmp/CORENAME ]; do sleep 1; done
check
if command -v inotifywait >/dev/null; then
    while inotifywait -qq -e modify -e create /tmp/CORENAME; do
        sleep 0.2
        check
    done
fi
# fallback without inotify
while true; do check; sleep 1; done
