#!/bin/bash
# PLATO terminal core for MiSTer - installer
#
# Run once from the MiSTer "Scripts" menu after copying the release files
# to the SD card.  It makes /media/fat/linux/user-startup.sh start
# /media/fat/PLATO/plato_watch.sh at boot; that script starts platod (the
# ARM side of the core) whenever the PLATO core is loaded.
#
# Run it again with the argument "uninstall" to remove the startup line.

STARTUP=/media/fat/linux/user-startup.sh
WATCH=/media/fat/PLATO/plato_watch.sh
LINE="[ -x $WATCH ] && $WATCH &"

if [ "$1" = "uninstall" ]; then
    if [ -f "$STARTUP" ] && grep -q "plato_watch.sh" "$STARTUP"; then
        cp "$STARTUP" "$STARTUP.bak-plato"
        grep -v -e "plato_watch.sh" -e "^# PLATO core" "$STARTUP.bak-plato" > "$STARTUP"
        echo "Removed PLATO from $STARTUP (backup: $STARTUP.bak-plato)"
    fi
    kill $(pidof platod) 2>/dev/null
    for p in $(ps | grep "[{]plato_watch.sh[}]" | awk '{print $1}'); do kill $p; done
    echo "Done."
    exit 0
fi

if [ ! -x /media/fat/PLATO/platod ] || [ ! -f "$WATCH" ]; then
    echo "Missing /media/fat/PLATO/platod or plato_watch.sh."
    echo "Copy the PLATO folder of the release to the SD card first."
    exit 1
fi
chmod +x /media/fat/PLATO/platod "$WATCH"

if [ ! -f "$STARTUP" ]; then
    printf '#!/bin/sh\n\necho "***" $1 "***"\n' > "$STARTUP"
fi
if grep -q "plato_watch.sh" "$STARTUP"; then
    echo "PLATO is already in $STARTUP"
else
    cp "$STARTUP" "$STARTUP.bak-plato"
    printf "\n# PLATO core: start platod when the core is loaded\n%s\n" "$LINE" >> "$STARTUP"
    echo "Added PLATO to $STARTUP (backup: $STARTUP.bak-plato)"
fi

# Start the watcher now, so no reboot is needed
if ! ps | grep -q "[{]plato_watch.sh[}]"; then
    (setsid "$WATCH" </dev/null >/dev/null 2>&1 &)
fi

echo
echo "PLATO terminal installed."
echo "Load it from the menu: Other -> PLATO"
exit 0
