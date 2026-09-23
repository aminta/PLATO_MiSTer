// PS/2 keyboard to PLATO key translation for the MiSTer PLATO terminal.

#ifndef PLATO_KEYMAP_H
#define PLATO_KEYMAP_H

#include "compat.h"

class PlatoEngine;

class KeyMapper
{
public:
    KeyMapper ();

    // Process one MiSTer ps2_key event: scancode (PS/2 set 2),
    // extended (E0 prefix) and pressed flags.  Sends PLATO keys to the
    // engine.  Returns true if the key was the "NEXT" key (Enter), which
    // the caller may use to reconnect when offline.
    bool Event (PlatoEngine &engine, int scancode, bool extended, bool pressed);

    bool m_numpadArrows;        // numeric keypad acts as PLATO arrow keys

private:
    bool m_lshift, m_rshift, m_lctrl, m_rctrl, m_lalt, m_ralt, m_caps;
};

#endif
