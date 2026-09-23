// PLATO terminal engine for the MiSTer PLATO core.
//
// Derived from PTerm 6.0.4 (FrameCanvas.cpp / PtermFrame.h),
// Copyright (c) 2005-2018 Paul Koning, Joe Stanton, Dale Sinder,
// Bill Galcher, Steve Zoppi.  See pterm/pterm-license.txt.
//
// ALTERED SOURCE VERSION: the wxWidgets user interface has been removed;
// the protocol decoder, drawing primitives and PPT (Z80) resident were
// kept and now render into a plain 512x512 32-bit pixel buffer.

#ifndef PLATO_ENGINE_H
#define PLATO_ENGINE_H

#include <stdint.h>
#include <string>
#include <functional>

#include "compat.h"
#include "Z80.h"

class PlatoEngine : public Z80
{
public:
    // Callback used to transmit bytes to the host.
    typedef std::function<void (const void *data, int len)> SendFn;

    PlatoEngine ();
    ~PlatoEngine ();

    void SetSender (SendFn fn) { m_send = fn; }
    // Callback used for local echo while in dumb TTY mode.
    void SetLocalEcho (std::function<void (int)> fn) { m_localEcho = fn; }
    void SetAscii (bool ascii) { m_ascii = ascii; }
    bool Ascii (void) const { return m_ascii; }
    void SetDefaultColors (u32 fg, u32 bg);
    void Reset (void);

    // Process one word from the host.  Returns true if the screen changed.
    bool procPlatoWord (u32 d, bool ascii);

    // Keyboard input, PLATO key codes (see KEY macro)
    void ptermSendKey (u32 keys);
    void ptermSendKey1 (int key);
    void ptermSendTouch (int x, int y);
    void ptermSendExt (int key);

    // Local text output on the dumb TTY (connection messages).
    void LocalText (const char *s);
    void ptermFullErase (void);

    // Periodic housekeeping: runs the Z80 when a PPT program is active.
    // "now_ms" is a monotonic millisecond clock.
    void Tick (uint64_t now_ms);
    void AfterData (void);
    bool PptBusy (void) const;

    // Number of words waiting in the input ring (for echo pacing)
    int         m_ringCount;
    bool        m_noColor;          // ignore host color commands

    // True if a pending echo must be sent once input has drained.
    int         m_pendingEcho;
    bool        m_ignoreDelay;
    bool        m_dumbTty;
    bool        m_flowCtrl;

    // Screen: 512x512, row 0 is the TOP of the display.  Pixels are
    // 0xAARRGGBB with AA == 0xff for normal pixels.
    u32         *m_pixels;
    int         m_dirtyTop, m_dirtyBottom;   // dirty row range, inclusive
    void        ClearDirty (void) { m_dirtyTop = 512; m_dirtyBottom = -1; }
    bool        Dirty (void) const { return m_dirtyTop <= m_dirtyBottom; }

    // Session information from PLATO meta data
    std::string m_name, m_group, m_system, m_station;

    bool        m_beep;             // set when the host asked for a beep

private:
    SendFn      m_send;
    std::function<void (int)> m_localEcho;
    bool        m_ascii;
    uint64_t    m_now;
    uint64_t    m_z80ResumeAt;      // replaces the m_MReturnz80 timer
    uint64_t    m_mclockNext;       // replaces the m_Mclock timer (60 Hz)
    uint64_t    m_dclockNext;       // replaces the m_Dclock timer (1 Hz)

    void SendData (const void *data, int len)
    {
        if (m_send)
        {
            m_send (data, len);
        }
    }

    // PLATO terminal emulation state
    i16         mt_key;
    bool        modexor;
    int         currentX;
    int         currentY;
    int         memaddr;
    u16         plato_m23[128 * 8];
    int         wc;
    int         seq;
    int         modewords;
    int         mode4start;
    typedef enum {
        none, ldc, lde, lda, ssf, fg, bg, gsfg, paint,
        pni_rs, ext, pmd
    } AscState;
    AscState    m_ascState;
    int         m_ascBytes;
    int         m_assembler;
    int         lastX;
    int         lastY;
    bool        m_sendFgt;
    int         m_lastKey;

    u32         m_fgpix;
    u32         m_bgpix;
    u32         m_defFg, m_defBg;
    u32         m_currentFg, m_currentBg;
    u32         m_currentFgHost, m_currentBgHost;
    u32         m_currentFgLocal, m_currentBgLocal;

    bool        m_loadingPMD;
    std::string m_PMD;
    bool        m_fontPMD;
    bool        m_fontinfo;
    bool        m_osinfo;
    int         m_fontwidth, m_fontheight;

    u8          m_indev, m_outdev, m_mtincnt;

    // CYBIS workstation windowing
    int         cwsmode, cwsfun, cwscnt, cwswin;
    struct cws
    {
        bool        ok;
        int         data[4];
        u32         *bm;
    };
    cws         cwswindow[10];

    void setMargin (int i);
    void setUncover (bool u);
    void setReverse (bool u);
    void setLarge (bool u);
    void setCmem (int i);
    void setVertical (bool u);

    void SetColors (u32 fg, u32 bg);
    u32  GetColor (u16 loc);
    void SaveRestoreColors (u8 action, u8 target);

    // Drawing primitives
    inline void ptermUpdatePoint (int x, int y, u32 pixval, bool xor_p);
    void ptermDrawChar (int x, int y, int snum, int cnum);
    void ptermDrawCharInto (int x, int y, const u16 *charp,
                            u32 fpix, u32 bpix, int cmode, bool xor_p);
    void ptermDrawPoint (int x, int y);
    void ptermDrawLine (int x1, int y1, int x2, int y2);
    void ptermBlockErase (int x1, int y1, int x2, int y2);
    void ptermPaint (int pat);
    void ptermPaintWalker (int x, int y, int pat, int pass);
    void ptermSaveWindow (int d);
    void ptermRestoreWindow (int d);
    void MarkDirty (int y1, int y2);

    void plotChar (int c);
    void mode0 (u32 d);
    void mode1 (u32 d);
    void mode2 (u32 d);
    void mode3 (u32 d);
    void mode4 (u32 d);
    void mode5 (u32 d);
    void mode6 (u32 d);
    void mode7 (u32 d);
    void progmode (u32 d, int origin);
    typedef void (PlatoEngine::*mptr) (u32);
    static const mptr modePtr[8];

    bool AssembleCoord (int d);
    int AssemblePaint (int d);
    int AssembleData (int d);
    int AssembleColor (int d);
    int AssembleGrayScale (int d);
    int AssembleAsciiPlatoMetaData (int d);
    bool AssembleClassicPlatoMetaData (int d);
    void ProcessPlatoMetaData (void);
    void SendOsInfo (void);

    void MicroEmulate (void);
    void RunZ80 (void);
    void Mz80Waiter (int msec) { m_z80ResumeAt = m_now + msec; }

    // z80 emulation support
    unsigned char inputZ80 (unsigned char data);
    void outputZ80 (unsigned char data, unsigned char acc);
    int check_pcZ80 (void);
};

#endif
