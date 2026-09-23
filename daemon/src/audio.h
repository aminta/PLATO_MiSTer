// Sound for the MiSTer PLATO terminal: -beep- and the Gooch Synthetic
// Woodwind (GSW) music device.
//
// The GSW synthesis is derived from PTerm 6.0.4 pterm_sdl.c,
// Copyright (c) 2005-2010 Paul Koning.  See pterm/pterm-license.txt.
// ALTERED SOURCE VERSION: SDL replaced by ALSA (MiSTer mixes Linux ALSA
// output into the HDMI and analog audio), libsndfile output removed.

#ifndef PLATO_AUDIO_H
#define PLATO_AUDIO_H

#include <functional>

#include "compat.h"

class Audio
{
public:
    // Supplies the next GSW word (one per 1/60 s), or C_NODATA.
    typedef std::function<int (void)> WordFn;

    Audio ();
    ~Audio ();

    bool Open (void);           // false if ALSA is not available
    bool Ok (void) const { return m_pcm != NULL; }

    void Beep (void);
    void StartGsw (WordFn next);
    void StopGsw (void);
    bool GswActive (void) const { return m_gswActive; }

    // Keep the ALSA buffer filled; call from the main loop.
    void Pump (void);

    bool m_mute;

private:
    void    *m_lib;
    void    *m_pcm;
    int     m_rate;

    // beep
    int     m_beepLeft;         // samples
    u32     m_beepPhase;

    // GSW (gswState in pterm_sdl.c)
    bool    m_gswActive;
    WordFn  m_next;
    double  m_phaseStep;
    u32     m_phase[4];
    u32     m_step[4];
    int     m_vol[4];
    int     m_voices;
    int     m_voice;
    bool    m_cis;
    int     m_clocksLeft;
    int     m_clocksPerWord;
    double  m_a;                // RC filter coefficient
    double  m_z;                // RC filter state

    void GswWord (int word);
    void Render (int16_t *buf, int n);
};

#endif
