// Feed a PTerm .trc test file ("octal-word seq n" lines) to the engine
// and write the final screen as PPM: same input as "Pterm file.trc".
#include <stdio.h>
#include <string.h>
#include "engine.h"
int main (int argc, char **argv)
{
    PlatoEngine e;
    FILE *f = fopen (argv[1], "r");
    char line[200];
    unsigned w;
    int limit = argc > 3 ? atoi (argv[3]) : 1 << 30, n = 0;
    e.SetAscii (false);
    e.m_dumbTty = false;
    while (fgets (line, sizeof line, f) && n < limit)
    {
        if (sscanf (line, "%o", &w) == 1) { e.procPlatoWord (w, false); n++; }
    }
    FILE *o = fopen (argv[2], "wb");
    fprintf (o, "P6\n512 512\n255\n");
    for (int i = 0; i < 512 * 512; i++)
    {
        unsigned p = e.m_pixels[i];
        fputc (p >> 16, o); fputc (p >> 8, o); fputc (p, o);
    }
    fclose (o);
    printf ("%d words\n", n);
    return 0;
}
