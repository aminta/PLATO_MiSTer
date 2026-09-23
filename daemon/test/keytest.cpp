// Unit test: PS/2 scancodes (Italian layout) -> PLATO key codes
#include <stdio.h>
#include <vector>
#include "engine.h"
#include "keymap.h"

static std::vector<int> sent;

static void press (KeyMapper &k, PlatoEngine &e, int code, bool ext = false)
{
    k.Event (e, code, ext, true);
    k.Event (e, code, ext, false);
}

int main ()
{
    PlatoEngine e;
    KeyMapper k;
    k.m_italian = true;
    e.SetAscii (false);     // classic: 2 bytes per key, key = (b0 << 7) | (b1 & 0177)
    e.SetSender ([] (const void *d, int len) {
        const unsigned char *p = (const unsigned char *) d;
        for (int i = 0; i + 1 < len; i += 2) sent.push_back ((p[i] << 7) | (p[i + 1] & 0177));
    });
    struct { const char *name; int code; bool shift, altgr; } t[] = {
        { "e grave", 0x54, 0, 0 }, { "e acute", 0x54, 1, 0 }, { "a grave", 0x52, 0, 0 },
        { "@ (AltGr+o)", 0x4c, 0, 1 }, { "# (AltGr+a)", 0x52, 0, 1 }, { "[ (AltGr+e)", 0x54, 0, 1 },
        { "{ (AltGr+Sh+e)", 0x54, 1, 1 }, { "+", 0x5b, 0, 0 }, { "*", 0x5b, 1, 0 },
        { "\" (Sh+2)", 0x1e, 1, 0 }, { "/ (Sh+7)", 0x3d, 1, 0 }, { "= (Sh+0)", 0x45, 1, 0 },
        { "? (Sh+')", 0x4e, 1, 0 }, { "a", 0x1c, 0, 0 }, { "A", 0x1c, 1, 0 },
    };
    for (auto &x : t)
    {
        sent.clear ();
        if (x.shift) k.Event (e, 0x12, false, true);
        if (x.altgr) k.Event (e, 0x11, true, true);
        press (k, e, x.code);
        if (x.altgr) k.Event (e, 0x11, true, false);
        if (x.shift) k.Event (e, 0x12, false, false);
        printf ("%-16s ->", x.name);
        for (int v : sent) printf (" %04o", v);
        printf ("\n");
    }
    return 0;
}
