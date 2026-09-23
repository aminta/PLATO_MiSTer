// Credits page shown while the MiSTer OSD is open ("Pause when OSD is
// open", as in the Jotego cores).  It is drawn by a second PLATO engine
// with ordinary PLATO ASCII protocol commands, so it uses the real PLATO
// character set.

#include <string.h>

#include "credits.h"
#include "engine.h"

static void Byte (PlatoEngine &e, int c)
{
    e.procPlatoWord (c & 0177, true);
}

static void Esc (PlatoEngine &e, int c)
{
    e.procPlatoWord ((033 << 8) | c, true);
}

// PLATO ASCII coordinate: high Y, low Y, high X, low X (y = 0 at the bottom)
static void Coord (PlatoEngine &e, int x, int y)
{
    Byte (e, 040 | ((y >> 5) & 037));
    Byte (e, 0140 | (y & 037));
    Byte (e, 040 | ((x >> 5) & 037));
    Byte (e, 0100 | (x & 037));
}

static void Text (PlatoEngine &e, int x, int y, const char *s, bool big = false)
{
    Esc (e, '2');               // load coordinate
    Coord (e, x, y);
    Byte (e, 037);              // text mode
    if (big)
    {
        Esc (e, 'O');           // double size
    }
    for (; *s; s++)
    {
        Byte (e, *s);
    }
    if (big)
    {
        Esc (e, 'N');
    }
}

// Centered text on text row "row" (0 = top row, 32 rows of 16 dots)
static void Center (PlatoEngine &e, int row, const char *s, bool big = false)
{
    int w = (int) strlen (s) * (big ? 16 : 8);

    Text (e, (512 - w) / 2, 512 - 16 * (row + 1) - (big ? 16 : 0), s, big);
}

static void Box (PlatoEngine &e, int x1, int y1, int x2, int y2)
{
    Byte (e, 035);              // line mode, first coordinate is a move
    Coord (e, x1, y1);
    Coord (e, x2, y1);
    Coord (e, x2, y2);
    Coord (e, x1, y2);
    Coord (e, x1, y1);
}

void DrawCredits (PlatoEngine &e)
{
    e.Reset ();
    Esc (e, 002);               // enter PLATO terminal mode
    Esc (e, 014);               // full screen erase
    Esc (e, 024);               // mode rewrite

    Box (e, 4, 4, 507, 507);
    Box (e, 8, 8, 503, 503);

    Center (e, 1, "PLATO for MiSTer", true);

    Center (e, 5, "This core is dedicated to the unforgettable");
    Center (e, 7, "Federico", true);
    Center (e, 9, "\"Dottor Zonk\" Della Zonca,", true);
    Center (e, 12, "founding father of");
    Center (e, 13, "\"12 Bit - Retrogaming Associazione Culturale\"");
    Center (e, 14, "of Trieste.");

    Box (e, 64, 512 - 16 * 16 + 4, 447, 512 - 16 * 16 + 6);

    Center (e, 18, "Core by aminta");
    Center (e, 20, "PLATO terminal engine: PTerm 6.0.4 by");
    Center (e, 21, "Paul Koning, Joe Stanton, Dale Sinder et al.");
    Center (e, 23, "Z80 emulator by Lin Ke-Fong");
    Center (e, 24, "MiSTer framework by Sorgelig and contributors");
    Center (e, 26, "Thanks to CYBER1 (cyber1.org)");
    Center (e, 27, "for keeping PLATO alive");

    Center (e, 30, "Close the OSD to return to PLATO");
}
