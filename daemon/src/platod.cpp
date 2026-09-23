// platod: ARM side of the MiSTer PLATO terminal core.
//
// Connects to a PLATO host (by default cyberserv.org, as PTerm does),
// decodes the display stream with the PTerm engine and renders it into a
// frame buffer in DDR3 that the FPGA core scans out to HDMI and VGA.
// Keyboard events come from the FPGA core (MiSTer ps2_key) through a small
// event ring in the same memory region, see shared.h.
//
// Usage:
//   platod [options]
//     --host NAME        PLATO host (default cyberserv.org)
//     --port N           TCP port (default from the OSD: 5004 or 8005)
//     --sim              run without /dev/mem (for testing on a PC)
//     --script FILE      (sim) run a test script: "wait MS", "type TEXT",
//                        "key NAME", "snap FILE.ppm", "quit"
//     --verbose          log to stderr

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include <string>
#include <vector>

#include "engine.h"
#include "net.h"
#include "keymap.h"
#include "keytabs.h"
#include "shared.h"

#define RINGSIZE        5000
#define RINGXON1        (RINGSIZE / 3)
#define RINGXOFF1       (RINGSIZE - RINGXON1)

#define INI_FILE        "/media/fat/PLATO/platod.ini"
#define CORE_NAME_FILE  "/tmp/CORENAME"

static volatile bool running = true;
static bool verbose = false;

#define logf(...) do { if (verbose) { fprintf (stderr, "platod: " __VA_ARGS__); \
                                      fputc ('\n', stderr); } } while (0)

static uint64_t NowMs (void)
{
    struct timespec ts;

    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void OnSignal (int)
{
    running = false;
}

// ----------------------------------------------------------------------------
// Colour schemes selectable from the OSD (foreground, background)
// ----------------------------------------------------------------------------

static const u32 colorSchemes[8][2] =
{
    { 0xff9000, 0x000000 },     // PLATO plasma orange (PTerm default)
    { 0xffffff, 0x000000 },     // white
    { 0x00ff40, 0x000000 },     // green phosphor
    { 0xffb000, 0x000000 },     // amber
    { 0x40c0ff, 0x000000 },     // blue
    { 0x000000, 0xffffff },     // black on white (paper)
    { 0xff9000, 0x000000 },
    { 0xff9000, 0x000000 },
};

// ----------------------------------------------------------------------------
// Shared memory with the FPGA
// ----------------------------------------------------------------------------

class SharedMem
{
public:
    SharedMem () : m_fb (NULL), m_ctl (NULL), m_sim (false) {}

    bool Open (bool sim)
    {
        m_sim = sim;
        if (sim)
        {
            m_fb = (u8 *) calloc (1, FB_SIZE);
            m_ctl = (u8 *) calloc (1, CTL_SIZE);
            return true;
        }
        int fd = open ("/dev/mem", O_RDWR | O_SYNC);
        if (fd < 0)
        {
            perror ("platod: /dev/mem");
            return false;
        }
        m_fb = (u8 *) mmap (NULL, FB_SIZE, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, FB_PHYS);
        m_ctl = (u8 *) mmap (NULL, CTL_SIZE, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, CTL_PHYS);
        close (fd);
        if (m_fb == MAP_FAILED || m_ctl == MAP_FAILED)
        {
            perror ("platod: mmap");
            return false;
        }
        return true;
    }

    // Copy rows [top, bottom] of the engine screen to the frame buffer.
    void Flush (const u32 *pixels, int top, int bottom)
    {
        if (top < 0) top = 0;
        if (bottom > 511) bottom = 511;
        if (top > bottom)
        {
            return;
        }
        u32 *dst = (u32 *) m_fb + top * 512;
        const u32 *src = pixels + top * 512;
        int n = (bottom - top + 1) * 512;

        // The FPGA ignores the top byte (used by the engine as a marker).
        for (int i = 0; i < n; i++)
        {
            dst[i] = src[i] & 0x00ffffff;
        }
    }

    u32 Status (void) const { return Read32 (CTL_STATUS); }
    u32 Head (void) const { return Read32 (CTL_HEAD); }
    void Event (u32 index, u32 &event, u32 &seq) const
    {
        u32 off = CTL_RING + (index % EVT_SLOTS) * 8;

        event = Read32 (off);
        seq = Read32 (off + 4);
    }

    bool Sim (void) const { return m_sim; }
    void SetAlive (bool on) { Write32 (CTL_ALIVE, on ? ALIVE_MAGIC : 0); }

    // Simulation helpers: inject events and write a PPM snapshot.
    void SimEvent (u32 event)
    {
        u32 head = Head () + 1;
        u32 off = CTL_RING + (head % EVT_SLOTS) * 8;

        Write32 (off, event);
        Write32 (off + 4, head);
        Write32 (CTL_HEAD, head);
    }

    bool Snapshot (const char *fn) const
    {
        FILE *f = fopen (fn, "wb");
        if (f == NULL)
        {
            return false;
        }
        fprintf (f, "P6\n512 512\n255\n");
        const u32 *p = (const u32 *) m_fb;
        for (int i = 0; i < 512 * 512; i++)
        {
            u8 rgb[3] = { (u8) (p[i] >> 16), (u8) (p[i] >> 8), (u8) p[i] };
            fwrite (rgb, 1, 3, f);
        }
        fclose (f);
        return true;
    }

private:
    u8      *m_fb;
    u8      *m_ctl;
    bool    m_sim;

    u32 Read32 (u32 off) const { return *(volatile u32 *) (m_ctl + off); }
    void Write32 (u32 off, u32 v) { *(volatile u32 *) (m_ctl + off) = v; }
};

// ----------------------------------------------------------------------------
// Test script (simulation mode)
// ----------------------------------------------------------------------------

struct ScriptLine
{
    std::string cmd, arg;
};

static std::vector<ScriptLine> LoadScript (const char *fn)
{
    std::vector<ScriptLine> v;
    FILE *f = fopen (fn, "r");
    char line[512];

    if (f == NULL)
    {
        perror (fn);
        exit (1);
    }
    while (fgets (line, sizeof (line), f))
    {
        char *nl = strchr (line, '\n');
        if (nl) *nl = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;
        ScriptLine s;
        char *sp = strchr (line, ' ');
        if (sp)
        {
            *sp = '\0';
            s.arg = sp + 1;
        }
        s.cmd = line;
        v.push_back (s);
    }
    fclose (f);
    return v;
}

// Map a character to PS/2 set 2 events for the simulated keyboard.
struct SimKey { char c; u8 code; bool shift; };
static const SimKey simKeys[] =
{
    { 'a', 0x1c, 0 }, { 'b', 0x32, 0 }, { 'c', 0x21, 0 }, { 'd', 0x23, 0 },
    { 'e', 0x24, 0 }, { 'f', 0x2b, 0 }, { 'g', 0x34, 0 }, { 'h', 0x33, 0 },
    { 'i', 0x43, 0 }, { 'j', 0x3b, 0 }, { 'k', 0x42, 0 }, { 'l', 0x4b, 0 },
    { 'm', 0x3a, 0 }, { 'n', 0x31, 0 }, { 'o', 0x44, 0 }, { 'p', 0x4d, 0 },
    { 'q', 0x15, 0 }, { 'r', 0x2d, 0 }, { 's', 0x1b, 0 }, { 't', 0x2c, 0 },
    { 'u', 0x3c, 0 }, { 'v', 0x2a, 0 }, { 'w', 0x1d, 0 }, { 'x', 0x22, 0 },
    { 'y', 0x35, 0 }, { 'z', 0x1a, 0 },
    { '0', 0x45, 0 }, { '1', 0x16, 0 }, { '2', 0x1e, 0 }, { '3', 0x26, 0 },
    { '4', 0x25, 0 }, { '5', 0x2e, 0 }, { '6', 0x36, 0 }, { '7', 0x3d, 0 },
    { '8', 0x3e, 0 }, { '9', 0x46, 0 }, { ' ', 0x29, 0 }, { '-', 0x4e, 0 },
    { '.', 0x49, 0 }, { ',', 0x41, 0 }, { '/', 0x4a, 0 }, { '=', 0x55, 0 },
    { 0, 0, 0 }
};

static void SimPress (SharedMem &shm, int code, bool ext, bool shift)
{
    // MiSTer ps2_key: [9] pressed, [8] extended, [7:0] scancode
    if (shift) shm.SimEvent ((1 << 9) | 0x12);
    shm.SimEvent ((1 << 9) | (ext ? (1 << 8) : 0) | code);
    shm.SimEvent ((ext ? (1 << 8) : 0) | code);
    if (shift) shm.SimEvent (0x12);
}

// ----------------------------------------------------------------------------
// Main program
// ----------------------------------------------------------------------------

struct Config
{
    std::string host;
    int         port;           // 0 = from OSD
    bool        sim;
    const char  *script;

    Config () : host (DEFAULTHOST), port (0), sim (false), script (NULL) {}
};

static void LoadIni (Config &cfg)
{
    FILE *f = fopen (INI_FILE, "r");
    char line[256], key[64], val[192];

    if (f == NULL)
    {
        return;
    }
    while (fgets (line, sizeof (line), f))
    {
        if (sscanf (line, " %63[^= ] = %191s", key, val) == 2)
        {
            if (strcmp (key, "host") == 0)
            {
                cfg.host = val;
            }
            else if (strcmp (key, "port") == 0)
            {
                cfg.port = atoi (val);
            }
        }
    }
    fclose (f);
}

int main (int argc, char **argv)
{
    Config cfg;
    SharedMem shm;
    PlatoEngine engine;
    HostConnection conn;
    KeyMapper keys;

    LoadIni (cfg);
    for (int i = 1; i < argc; i++)
    {
        if (strcmp (argv[i], "--host") == 0 && i + 1 < argc)
            cfg.host = argv[++i];
        else if (strcmp (argv[i], "--port") == 0 && i + 1 < argc)
            cfg.port = atoi (argv[++i]);
        else if (strcmp (argv[i], "--sim") == 0)
            cfg.sim = true;
        else if (strcmp (argv[i], "--script") == 0 && i + 1 < argc)
            cfg.script = argv[++i];
        else if (strcmp (argv[i], "--verbose") == 0)
            verbose = true;
        else
        {
            fprintf (stderr, "usage: platod [--host NAME] [--port N] "
                     "[--sim] [--script FILE] [--verbose]\n");
            return 1;
        }
    }

    signal (SIGINT, OnSignal);
    signal (SIGTERM, OnSignal);
    signal (SIGPIPE, SIG_IGN);

    if (!shm.Open (cfg.sim))
    {
        return 1;
    }

    std::vector<ScriptLine> script;
    size_t scriptPos = 0;
    uint64_t scriptWait = 0;
    if (cfg.script)
    {
        script = LoadScript (cfg.script);
    }

    engine.SetSender ([&conn] (const void *d, int len) { conn.SendData (d, len); });
    engine.SetLocalEcho ([&conn] (int key) { conn.StoreWord (key); });

    u32 status = shm.Status ();
    u32 lastHead = shm.Head ();
    u32 colorScheme = STATUS_COLOR (status);
    u32 reconnCount = STATUS_RECONN (status);
    engine.SetDefaultColors (colorSchemes[colorScheme][0],
                             colorSchemes[colorScheme][1]);
    engine.Reset ();
    keys.m_numpadArrows = STATUS_NUMPAD (status) == 0;

    bool wantConnect = true;
    bool online = false;
    bool xoff = false;
    int nextWord = C_NODATA;
    uint64_t delayUntil = 0;
    uint64_t lastFlush = 0;
    uint64_t lastCoreCheck = 0;

    while (running)
    {
        uint64_t now = NowMs ();

        // ---- Connection management ----
        if (wantConnect)
        {
            int port = cfg.port;
            HostConnection::Mode mode = HostConnection::both;

            if (port == 0)
            {
                port = STATUS_PORT (status) ? DefAsciiPort : DefNiuPort;
            }
            if (port == DefAsciiPort)
            {
                mode = HostConnection::ascii;
            }
            wantConnect = false;
            engine.Reset ();
            char msg[200];
            snprintf (msg, sizeof (msg),
                      "PLATO terminal for MiSTer (PTerm engine)\n\n"
                      "Connecting to %s port %d...\n",
                      cfg.host.c_str (), port);
            engine.LocalText (msg);
            shm.Flush (engine.m_pixels, 0, 511);
            engine.ClearDirty ();
            shm.SetAlive (true);
            logf ("connecting to %s:%d", cfg.host.c_str (), port);
            conn.Connect (cfg.host, port, mode);
            online = true;
            xoff = false;
            nextWord = C_NODATA;
            delayUntil = 0;
        }

        if (online && (conn.GetState () == HostConnection::Failed ||
                       (conn.GetState () == HostConnection::Closed &&
                        conn.RingCount () == 0 && nextWord == C_NODATA)))
        {
            std::string msg = "\n\nDisconnected: " + conn.Error () +
                "\nPress NEXT (Enter) or use the OSD to reconnect.\n";
            engine.LocalText (msg.c_str ());
            logf ("offline: %s", conn.Error ().c_str ());
            online = false;
        }

        // ---- Display data processing (PtermFrame::procDataLoop) ----
        if (delayUntil != 0 && now >= delayUntil)
        {
            delayUntil = 0;
        }
        engine.SetAscii (conn.Ascii ());
        if (delayUntil == 0 && !engine.PptBusy ())
        {
            uint64_t budget = now + 12;
            engine.m_ringCount = conn.RingCount ();

            engine.m_ignoreDelay = false;
            if (nextWord != C_NODATA)
            {
                engine.procPlatoWord (nextWord, conn.Ascii ());
                nextWord = C_NODATA;
            }
            for (int n = 0; ; n++)
            {
                if ((n & 255) == 255 && NowMs () >= budget)
                {
                    break;
                }
                int word = conn.NextWord ();
                if (word == C_NODATA)
                {
                    break;
                }
                engine.m_ringCount = conn.RingCount ();
                if (word >= 0 && (word >> 19) != 0)
                {
                    if (engine.m_ignoreDelay &&
                        (word == 02000000 ||
                         (word == 02000001 && !conn.Ascii ())))
                    {
                        // Ignore delay NOPs that follow a block erase
                        continue;
                    }
                    // PTerm paces -delay- at 8 ms (ASCII) or 17 ms
                    // (classic) per unit.
                    delayUntil = now + (word >> 19) * (conn.Ascii () ? 8 : 17);
                    nextWord = word & 01777777;
                    break;
                }
                engine.m_ignoreDelay = false;
                engine.procPlatoWord (word, conn.Ascii ());
                if (engine.PptBusy ())
                {
                    break;
                }
            }
            engine.AfterData ();
        }
        engine.Tick (now);

        // Echo pacing and flow control (PtermHostConnection::NextRingWord)
        int ring = conn.RingCount ();
        if (engine.m_pendingEcho != -1 && ring < RINGXOFF1)
        {
            engine.ptermSendKey1 (engine.m_pendingEcho);
            engine.m_pendingEcho = -1;
        }
        if (!xoff && ring >= RINGXOFF1)
        {
            engine.ptermSendKey1 (xofkey);
            xoff = true;
        }
        else if (xoff && ring <= RINGXON1)
        {
            engine.ptermSendKey1 (xonkey);
            xoff = false;
        }

        // ---- Events and options from the FPGA ----
        status = shm.Status ();
        u32 head = shm.Head ();
        if (head - lastHead > EVT_SLOTS)
        {
            lastHead = head - EVT_SLOTS;    // overrun, drop the oldest
        }
        while (lastHead != head)
        {
            u32 event, seq;

            lastHead++;
            shm.Event (lastHead, event, seq);
            if (seq != lastHead)
            {
                continue;
            }
            if (EVT_TYPE (event) == EVT_KEY)
            {
                bool next = keys.Event (engine, event & 0xff,
                                        (event >> 8) & 1, (event >> 9) & 1);
                if (next && !online)
                {
                    wantConnect = true;
                }
            }
        }
        if (STATUS_RECONN (status) != reconnCount)
        {
            reconnCount = STATUS_RECONN (status);
            conn.Close ();
            wantConnect = true;
        }
        if (STATUS_COLOR (status) != colorScheme)
        {
            colorScheme = STATUS_COLOR (status);
            engine.SetDefaultColors (colorSchemes[colorScheme][0],
                                     colorSchemes[colorScheme][1]);
            // Takes effect on the next (re)connection, as in PTerm.
        }
        keys.m_numpadArrows = STATUS_NUMPAD (status) == 0;

        // ---- Screen update ----
        if (engine.Dirty () &&
            (now - lastFlush >= 16 || conn.RingCount () == 0))
        {
            shm.Flush (engine.m_pixels, engine.m_dirtyTop, engine.m_dirtyBottom);
            engine.ClearDirty ();
            lastFlush = now;
        }

        // ---- Stop when another core is loaded ----
        if (!cfg.sim && now - lastCoreCheck >= 1000)
        {
            char name[64] = "";
            FILE *f = fopen (CORE_NAME_FILE, "r");

            lastCoreCheck = now;
            if (f != NULL)
            {
                if (fgets (name, sizeof (name), f) == NULL)
                {
                    name[0] = '\0';
                }
                fclose (f);
                if (strncmp (name, "PLATO", 5) != 0)
                {
                    logf ("core changed to %s, exiting", name);
                    break;
                }
            }
        }

        // ---- Test script (simulation) ----
        if (!script.empty () && now >= scriptWait)
        {
            if (scriptPos >= script.size ())
            {
                break;
            }
            const ScriptLine &s = script[scriptPos++];
            if (s.cmd == "wait")
            {
                scriptWait = now + atoi (s.arg.c_str ());
            }
            else if (s.cmd == "type")
            {
                for (char c : s.arg)
                {
                    bool shift = isupper ((unsigned char) c);
                    char lc = tolower ((unsigned char) c);
                    for (const SimKey *k = simKeys; k->c; k++)
                    {
                        if (k->c == lc)
                        {
                            SimPress (shm, k->code, false, shift || k->shift);
                        }
                    }
                }
            }
            else if (s.cmd == "key")
            {
                static const struct { const char *name; u8 code; bool ext; }
                named[] = {
                    { "next", 0x5a, false }, { "erase", 0x66, false },
                    { "back", 0x0a, false }, { "stop", 0x09, false },
                    { "help", 0x0b, false }, { "tab", 0x0d, false },
                    { "data", 0x01, false }, { "lab", 0x83, false },
                    { "copy", 0x05, false }, { "edit", 0x03, false },
                    { "shift-stop", 0x09, false },
                    { NULL, 0, false }
                };
                for (int k = 0; named[k].name; k++)
                {
                    if (s.arg == named[k].name)
                    {
                        SimPress (shm, named[k].code, named[k].ext,
                                  s.arg.compare (0, 6, "shift-") == 0);
                    }
                }
            }
            else if (s.cmd == "snap")
            {
                shm.Flush (engine.m_pixels, 0, 511);
                shm.Snapshot (s.arg.c_str ());
                logf ("snapshot %s", s.arg.c_str ());
            }
            else if (s.cmd == "quit")
            {
                break;
            }
        }

        // ---- Wait for something to do ----
        struct pollfd pfd;
        int timeout = 10;

        if (conn.RingCount () > 0 && delayUntil == 0)
        {
            timeout = 0;
        }
        else if (delayUntil != 0)
        {
            int64_t d = (int64_t) (delayUntil - NowMs ());
            timeout = d < 0 ? 0 : (d < 10 ? (int) d : 10);
        }
        if (engine.PptBusy ())
        {
            timeout = 0;
        }
        pfd.fd = conn.Fd ();
        pfd.events = POLLIN | (conn.WantWrite () ? POLLOUT : 0);
        pfd.revents = 0;
        if (poll (&pfd, pfd.fd >= 0 ? 1 : 0, timeout) > 0)
        {
            if (pfd.revents & (POLLIN | POLLHUP | POLLERR))
            {
                conn.OnReadable ();
            }
            if (pfd.revents & POLLOUT)
            {
                conn.OnWritable ();
            }
        }
    }

    conn.Close ();
    shm.SetAlive (false);
    logf ("exit");
    return 0;
}
