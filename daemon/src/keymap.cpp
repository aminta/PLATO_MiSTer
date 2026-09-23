// PS/2 keyboard to PLATO key translation for the MiSTer PLATO terminal.
//
// The key assignments follow PTerm 6.0.4 (PtermCanvas::OnCharHook and
// OnChar in FrameCanvas.cpp), Copyright (c) 2005-2018 Paul Koning,
// Joe Stanton, Dale Sinder.  See pterm/pterm-license.txt.
// ALTERED SOURCE VERSION: wxWidgets key events replaced by PS/2 scancodes.

#include <ctype.h>

#include "keymap.h"
#include "engine.h"
#include "keytabs.h"

// Keycode translation for ALT-keypress (from PTerm).  -1 means not valid.
static const i8 altKeyToPlato[128] =
{
    /*                                                                         */
    /* 000- */ -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
    /*                                                                         */
    /* 010- */ -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
    /*                                                                         */
    /* 020- */ -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
    /*                                                                         */
    /* 030- */ -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
    /*          space   !       "       #       $       %       &       '      */
    /* 040- */ -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
    /*          (       )       *       +       ,       -       .       /      */
    /* 050- */ -1,     -1,     -1,      0056,  -1,      0057,  -1,     -1,
    /*          0       1       2       3       4       5       6       7      */
    /* 060- */ -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
    /*          8       9       :       ;       <       =       >       ?      */
    /* 070- */ -1,     -1,     -1,     -1,     -1,      0015,  -1,     -1,
    /*          @       A       B       C       D       E       F       G      */
    /* 100- */ -1,      0062,   0070,   0073,   0071,   0067,   0064,  0053,
    /*          H       I       J       K       L       M       N       O      */
    /* 110- */  0065,  -1,     -1,     -1,      0075,   0064,   0066,  -1,
    /*          P       Q       R       S       T       U       V       W      */
    /* 120- */ 0060,    0074,   0063,   0072,   0062,  -1,     -1,     -1,
    /*          X       Y       Z       [       \       ]       ^       _      */
    /* 130- */ 0052,    0061,  -1,     -1,     -1,     -1,     -1,     -1,
    /*          `       a       b       c       d       e       f       g      */
    /* 140- */ -1,      0022,   0030,   0033,   0031,   0027,   0064,  0013,
    /*          h       i       j       k       l       m       n       o      */
    /* 150- */  0025,  -1,     -1,     -1,      0035,   0024,   0026,  -1,
    /*          p       q       r       s       t       u       v       w      */
    /* 160- */ 0020,    0034,   0023,   0032,   0062,  -1,     -1,     -1,
    /*          x       y       z       {       |       }       ~              */
    /* 170- */ 0012,    0021,  -1,     -1,     -1,     -1,     -1,     -1
};

// Special (non-character) keys
enum
{
    K_NONE = 0,
    K_BACK = 0x100, K_ENTER, K_TAB, K_ESC, K_SPACE,
    K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11,
    K_UP, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN,
    K_INS, K_DEL, K_PAUSE,
    K_KP0, K_KP1, K_KP2, K_KP3, K_KP4, K_KP5, K_KP6, K_KP7, K_KP8, K_KP9,
    K_KPDOT, K_KPPLUS, K_KPMINUS, K_KPSTAR, K_KPSLASH, K_KPENTER,
    K_LSHIFT, K_RSHIFT, K_LCTRL, K_RCTRL, K_LALT, K_RALT, K_CAPS
};

// PS/2 set 2 scancodes, US layout: { code, unshifted, shifted }
struct ScanEntry
{
    u8  code;
    int normal;
    int shifted;
};

static const ScanEntry scanNormal[] =
{
    { 0x1c, 'a', 'A' }, { 0x32, 'b', 'B' }, { 0x21, 'c', 'C' },
    { 0x23, 'd', 'D' }, { 0x24, 'e', 'E' }, { 0x2b, 'f', 'F' },
    { 0x34, 'g', 'G' }, { 0x33, 'h', 'H' }, { 0x43, 'i', 'I' },
    { 0x3b, 'j', 'J' }, { 0x42, 'k', 'K' }, { 0x4b, 'l', 'L' },
    { 0x3a, 'm', 'M' }, { 0x31, 'n', 'N' }, { 0x44, 'o', 'O' },
    { 0x4d, 'p', 'P' }, { 0x15, 'q', 'Q' }, { 0x2d, 'r', 'R' },
    { 0x1b, 's', 'S' }, { 0x2c, 't', 'T' }, { 0x3c, 'u', 'U' },
    { 0x2a, 'v', 'V' }, { 0x1d, 'w', 'W' }, { 0x22, 'x', 'X' },
    { 0x35, 'y', 'Y' }, { 0x1a, 'z', 'Z' },
    { 0x45, '0', ')' }, { 0x16, '1', '!' }, { 0x1e, '2', '@' },
    { 0x26, '3', '#' }, { 0x25, '4', '$' }, { 0x2e, '5', '%' },
    { 0x36, '6', '^' }, { 0x3d, '7', '&' }, { 0x3e, '8', '*' },
    { 0x46, '9', '(' },
    { 0x0e, '`', '~' }, { 0x4e, '-', '_' }, { 0x55, '=', '+' },
    { 0x5d, '\\', '|' }, { 0x54, '[', '{' }, { 0x5b, ']', '}' },
    { 0x4c, ';', ':' }, { 0x52, '\'', '"' }, { 0x41, ',', '<' },
    { 0x49, '.', '>' }, { 0x4a, '/', '?' }, { 0x61, '\\', '|' },
    { 0x66, K_BACK, K_BACK }, { 0x5a, K_ENTER, K_ENTER },
    { 0x0d, K_TAB, K_TAB }, { 0x76, K_ESC, K_ESC },
    { 0x29, K_SPACE, K_SPACE },
    { 0x05, K_F1, K_F1 }, { 0x06, K_F2, K_F2 }, { 0x04, K_F3, K_F3 },
    { 0x0c, K_F4, K_F4 }, { 0x03, K_F5, K_F5 }, { 0x0b, K_F6, K_F6 },
    { 0x83, K_F7, K_F7 }, { 0x0a, K_F8, K_F8 }, { 0x01, K_F9, K_F9 },
    { 0x09, K_F10, K_F10 }, { 0x78, K_F11, K_F11 },
    { 0x70, K_KP0, K_KP0 }, { 0x69, K_KP1, K_KP1 }, { 0x72, K_KP2, K_KP2 },
    { 0x7a, K_KP3, K_KP3 }, { 0x6b, K_KP4, K_KP4 }, { 0x73, K_KP5, K_KP5 },
    { 0x74, K_KP6, K_KP6 }, { 0x6c, K_KP7, K_KP7 }, { 0x75, K_KP8, K_KP8 },
    { 0x7d, K_KP9, K_KP9 }, { 0x71, K_KPDOT, K_KPDOT },
    { 0x79, K_KPPLUS, K_KPPLUS }, { 0x7b, K_KPMINUS, K_KPMINUS },
    { 0x7c, K_KPSTAR, K_KPSTAR },
    { 0x12, K_LSHIFT, K_LSHIFT }, { 0x59, K_RSHIFT, K_RSHIFT },
    { 0x14, K_LCTRL, K_LCTRL }, { 0x11, K_LALT, K_LALT },
    { 0x58, K_CAPS, K_CAPS },
    { 0, 0, 0 }
};

static const ScanEntry scanExtended[] =
{
    { 0x14, K_RCTRL, K_RCTRL }, { 0x11, K_RALT, K_RALT },
    { 0x5a, K_KPENTER, K_KPENTER }, { 0x4a, K_KPSLASH, K_KPSLASH },
    { 0x70, K_INS, K_INS }, { 0x71, K_DEL, K_DEL },
    { 0x6c, K_HOME, K_HOME }, { 0x69, K_END, K_END },
    { 0x7d, K_PGUP, K_PGUP }, { 0x7a, K_PGDN, K_PGDN },
    { 0x75, K_UP, K_UP }, { 0x72, K_DOWN, K_DOWN },
    { 0x6b, K_LEFT, K_LEFT }, { 0x74, K_RIGHT, K_RIGHT },
    { 0x77, K_PAUSE, K_PAUSE }, { 0x7e, K_PAUSE, K_PAUSE },
    { 0, 0, 0 }
};

static const ScanEntry *Lookup (int scancode, bool extended)
{
    const ScanEntry *e = extended ? scanExtended : scanNormal;

    for (; e->code != 0 || e->normal != 0; e++)
    {
        if (e->code == scancode)
        {
            return e;
        }
    }
    return NULL;
}

KeyMapper::KeyMapper ()
    : m_numpadArrows (true),
      m_lshift (false), m_rshift (false),
      m_lctrl (false), m_rctrl (false),
      m_lalt (false), m_ralt (false),
      m_caps (false)
{
}

bool KeyMapper::Event (PlatoEngine &engine, int scancode, bool extended,
                       bool pressed)
{
    const ScanEntry *e = Lookup (scancode, extended);
    int key, shift;
    bool ctrl, alt, shiftDown;
    u32 pc = None;

    if (e == NULL)
    {
        return false;
    }
    key = e->normal;

    // Modifier state
    switch (key)
    {
    case K_LSHIFT: m_lshift = pressed; return false;
    case K_RSHIFT: m_rshift = pressed; return false;
    case K_LCTRL:  m_lctrl = pressed;  return false;
    case K_RCTRL:  m_rctrl = pressed;  return false;
    case K_LALT:   m_lalt = pressed;   return false;
    case K_RALT:   m_ralt = pressed;   return false;
    case K_CAPS:
        if (pressed)
        {
            m_caps = !m_caps;
        }
        return false;
    }
    if (!pressed)
    {
        return false;
    }

    shiftDown = m_lshift || m_rshift;
    ctrl = m_lctrl || m_rctrl;
    alt = m_lalt || m_ralt;
    shift = shiftDown ? 040 : 0;

    // Special case: ALT-left or Ctrl-left is assignment arrow
    if ((alt || ctrl) && key == K_LEFT)
    {
        engine.ptermSendKey1 (015 | shift);
        return false;
    }

    if (key < 0200 && alt)
    {
        if (shift != 0 && key == '=')
        {
            key = '+';
        }
        if (altKeyToPlato[key] != -1)
        {
            engine.ptermSendKey1 (altKeyToPlato[key] | shift);
        }
        return false;
    }

    if (key < 0200 && ctrl)
    {
        // Control plus letter: look up the translate table entry
        //  for the matching control code.
        // Control plus non-letter: look up the translate table entry
        //  for that character, and set the "shift" bit.
        if (isalpha (key))
        {
            pc = asciiToPlato[key & 037];
        }
        else
        {
            if (shift != 0 && key == '=')
            {
                key = '+';
            }
            pc = asciiToPlato[key];
            if (pc != KEY1 (pc))
            {
                // If this entry is for more than one keycode,
                // ignore the keystroke.
                pc = None;
            }
            shift = 040;
        }
        if (pc != None)
        {
            engine.ptermSendKey (pc | shift);
        }
        return false;
    }

    // Numeric keypad as the 8-way arrow keys of the PLATO main keyboard
    if (m_numpadArrows)
    {
        switch (key)
        {
        case K_KP7: pc = 0121; break;   // up left (q)
        case K_KP8: pc = 0127; break;   // up arrow (w)
        case K_KP9: pc = 0105; break;   // up right (e)
        case K_KP4: pc = 0101; break;   // left arrow (a)
        case K_KP6: pc = 0104; break;   // right arrow (d)
        case K_KP1: pc = 0132; break;   // down left (z)
        case K_KP2: pc = 0130; break;   // down arrow (x)
        case K_KP3: pc = 0103; break;   // down right (c)
        }
        if (pc != None)
        {
            engine.ptermSendKey (KEY1 (pc) | shift);
            return false;
        }
    }

    switch (key)
    {
    case K_SPACE:
        pc = 0100;      // space
        break;
    case K_BACK:
        pc = 023;       // erase
        break;
    case K_ENTER:
    case K_KPENTER:
        pc = 026;       // next
        break;
    case K_HOME:
    case K_F8:
        pc = 030;       // back
        break;
    case K_PAUSE:
    case K_F10:
        pc = 032;       // stop
        break;
    case K_TAB:
        pc = 014;       // tab
        break;
    case K_ESC:
        pc = 015;       // assign
        break;
    case K_KPPLUS:
        pc = ctrl ? 056 : 016;  // Sigma / +
        break;
    case K_KPMINUS:
        pc = ctrl ? 057 : 017;  // Delta / -
        break;
    case K_KPSTAR:
    case K_DEL:
        pc = 012;       // multiply sign
        break;
    case K_KPSLASH:
    case K_INS:
        pc = 013;       // divide sign
        break;
    case K_LEFT:
        pc = 0101;      // left arrow (a)
        break;
    case K_RIGHT:
        pc = 0104;      // right arrow (d)
        break;
    case K_UP:
        pc = 0127;      // up arrow (w)
        break;
    case K_DOWN:
        pc = 0130;      // down arrow (x)
        break;
    case K_PGUP:
        pc = 020;       // super
        break;
    case K_PGDN:
        pc = 021;       // sub
        break;
    case K_F3:
        pc = 034;       // square
        break;
    case K_F2:
        pc = 022;       // ans
        break;
    case K_F1:
    case K_F11:
        pc = 033;       // copy
        break;
    case K_F9:
        pc = 031;       // data
        break;
    case K_F5:
        pc = 027;       // edit
        break;
    case K_F4:
        pc = 024;       // micro/font
        break;
    case K_F6:
        pc = 025;       // help
        break;
    case K_F7:
        pc = 035;       // lab
        break;
    case K_KPDOT:
        pc = 0136;      // .
        break;
    case K_KP0: case K_KP1: case K_KP2: case K_KP3: case K_KP4:
    case K_KP5: case K_KP6: case K_KP7: case K_KP8: case K_KP9:
        pc = key - K_KP0;
        if (ctrl)
        {
            shift = 040;
        }
        break;
    default:
        // Regular printable character (PtermCanvas::OnChar): the shifted
        // character comes from the US layout, caps lock affects letters.
        if (key < 0200)
        {
            int ch = shiftDown ? e->shifted : e->normal;

            if (m_caps && isalpha (ch))
            {
                ch = shiftDown ? tolower (ch) : toupper (ch);
            }
            pc = asciiToPlato[ch];
            if (pc != None)
            {
                engine.ptermSendKey (pc);
            }
        }
        return false;
    }

    engine.ptermSendKey (KEY1 (pc) | shift);
    return key == K_ENTER || key == K_KPENTER;
}
