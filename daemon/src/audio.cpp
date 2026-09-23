// Sound for the MiSTer PLATO terminal: -beep- and the Gooch Synthetic
// Woodwind (GSW) music device.
//
// The GSW synthesis is derived from PTerm 6.0.4 pterm_sdl.c,
// Copyright (c) 2005-2010 Paul Koning.  See pterm/pterm-license.txt.
// ALTERED SOURCE VERSION: SDL replaced by ALSA, loaded at run time with
// dlopen so platod has no build dependency on libasound.

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"

#define RATE        48000
#define CRYSTAL     3872000         // GSW clock crystal frequency
#define LATENCY_US  80000           // ALSA buffer
#define BEEP_HZ     1000
#define BEEP_MS     150

// ALSA constants (alsa/pcm.h)
#define SND_PCM_STREAM_PLAYBACK         0
#define SND_PCM_NONBLOCK                1
#define SND_PCM_FORMAT_S16_LE           2
#define SND_PCM_ACCESS_RW_INTERLEAVED   3

typedef int  (*pcm_open_t) (void **, const char *, int, int);
typedef int  (*pcm_set_params_t) (void *, int, int, unsigned, unsigned, int, unsigned);
typedef long (*pcm_avail_t) (void *);
typedef long (*pcm_writei_t) (void *, const void *, unsigned long);
typedef int  (*pcm_recover_t) (void *, int, int);
typedef int  (*pcm_close_t) (void *);

static pcm_open_t       p_open;
static pcm_set_params_t p_set_params;
static pcm_avail_t      p_avail;
static pcm_writei_t     p_writei;
static pcm_recover_t    p_recover;
static pcm_close_t      p_close;

// Per voice volume (pterm_sdl.c volmap)
static const int volmap[8] = {
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000
};

Audio::Audio ()
    : m_mute (false),
      m_lib (NULL),
      m_pcm (NULL),
      m_rate (RATE),
      m_beepLeft (0),
      m_beepPhase (0),
      m_gswActive (false)
{
    StopGsw ();
}

Audio::~Audio ()
{
    if (m_pcm != NULL)
    {
        p_close (m_pcm);
    }
    if (m_lib != NULL)
    {
        dlclose (m_lib);
    }
}

bool Audio::Open (void)
{
    m_lib = dlopen ("libasound.so.2", RTLD_NOW);
    if (m_lib == NULL)
    {
        return false;
    }
    p_open = (pcm_open_t) dlsym (m_lib, "snd_pcm_open");
    p_set_params = (pcm_set_params_t) dlsym (m_lib, "snd_pcm_set_params");
    p_avail = (pcm_avail_t) dlsym (m_lib, "snd_pcm_avail_update");
    p_writei = (pcm_writei_t) dlsym (m_lib, "snd_pcm_writei");
    p_recover = (pcm_recover_t) dlsym (m_lib, "snd_pcm_recover");
    p_close = (pcm_close_t) dlsym (m_lib, "snd_pcm_close");
    if (!p_open || !p_set_params || !p_avail || !p_writei || !p_recover || !p_close)
    {
        return false;
    }
    if (p_open (&m_pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0)
    {
        m_pcm = NULL;
        return false;
    }
    if (p_set_params (m_pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                      1, RATE, 1, LATENCY_US) < 0)
    {
        p_close (m_pcm);
        m_pcm = NULL;
        return false;
    }
    return true;
}

void Audio::Beep (void)
{
    m_beepLeft = m_rate * BEEP_MS / 1000;
}

// ptermOpenGsw
void Audio::StartGsw (WordFn next)
{
    // RC filter is the summing network resistor (100k) and the
    // associated capacitor (.01 uF).  We'll model that using an IIR.
    const double r = 100e3;
    const double c = .01e-6;
    const double t = 1.0 / m_rate;

    StopGsw ();
    m_next = next;
    m_phaseStep = (double) CRYSTAL / m_rate;
    m_clocksPerWord = m_rate / 60;
    m_a = t / (t + r * c);
    m_z = 0.0;
    m_gswActive = true;
}

void Audio::StopGsw (void)
{
    m_gswActive = false;
    m_next = nullptr;
    memset (m_phase, 0, sizeof (m_phase));
    memset (m_step, 0, sizeof (m_step));
    memset (m_vol, 0, sizeof (m_vol));
    m_voices = m_voice = 0;
    m_cis = false;
    m_clocksLeft = 0;
}

// Process one -extout- word (gswCallback)
void Audio::GswWord (int word)
{
    int voice;
    double dph;

    if ((word >> 16) != 3)
    {
        return;
    }
    if (word & 0100000)
    {
        // voice word
        word &= 077777;
        voice = m_voice;
        if (word < 2)
        {
            // rest, set step to zero to indicate silence
            m_step[voice] = 0;
        }
        else
        {
            dph = m_phaseStep / (4 * word + 2);
            if (dph > 0.5)
            {
                m_step[voice] = 0;
            }
            else
            {
                // delta phase per audio clock, binary point to the right
                // of the top bit
                m_step[voice] = (u32) (dph * 2147483648.0);
            }
        }
        if (!m_cis)
        {
            if (--voice < 0)
            {
                voice = m_voices;
            }
            m_voice = voice;
        }
    }
    else
    {
        // Mode word
        m_cis = (word & 040000) != 0;
        m_voice = m_voices = (word >> 12) & 3;
        m_vol[0] = volmap[(word >> 9) & 7];
        m_vol[1] = volmap[(word >> 6) & 7];
        m_vol[2] = volmap[(word >> 3) & 7];
        m_vol[3] = volmap[word & 7];
    }
}

void Audio::Render (int16_t *buf, int n)
{
    for (int s = 0; s < n; s++)
    {
        int audio = 0;

        if (m_gswActive)
        {
            if (m_clocksLeft == 0)
            {
                // Finished the current word worth of data (1/60th of a
                // second), get the next word from the main emulation.
                int word = m_next ? m_next () : C_GSWEND;

                if (word == C_GSWEND)
                {
                    StopGsw ();
                }
                else
                {
                    m_clocksLeft = m_clocksPerWord;
                    if (word != C_NODATA)
                    {
                        GswWord (word);
                    }
                }
            }
            if (m_gswActive)
            {
                // Square wave per voice, then the RC filter
                for (int i = 0; i < 4; i++)
                {
                    if (m_step[i] != 0)
                    {
                        audio += (((m_phase[i] >> 30) & 1) ? -1 : 1) * m_vol[i];
                        m_phase[i] += m_step[i];
                    }
                }
                --m_clocksLeft;
                m_z = m_z * (1.0 - m_a) + audio * m_a;
                audio = (int) m_z;
            }
        }

        if (m_beepLeft > 0)
        {
            m_beepPhase += (u32) ((double) BEEP_HZ / m_rate * 4294967296.0);
            audio += (m_beepPhase & 0x80000000) ? -6000 : 6000;
            m_beepLeft--;
        }

        if (audio > 32767) audio = 32767;
        if (audio < -32767) audio = -32767;
        buf[s] = m_mute ? 0 : (int16_t) audio;
    }
}

void Audio::Pump (void)
{
    int16_t buf[1024];

    if (m_pcm == NULL)
    {
        // No ALSA (e.g. simulation on a PC): still consume GSW words at
        // roughly the right pace is not needed, just drop the sound.
        m_beepLeft = 0;
        return;
    }
    for (;;)
    {
        long avail = p_avail (m_pcm);

        if (avail < 0)
        {
            p_recover (m_pcm, (int) avail, 1);
            continue;
        }
        if (avail < 256)
        {
            break;
        }
        if (avail > 1024)
        {
            avail = 1024;
        }
        Render (buf, (int) avail);
        long w = p_writei (m_pcm, buf, avail);
        if (w < 0)
        {
            if (p_recover (m_pcm, (int) w, 1) < 0)
            {
                break;
            }
        }
    }
}
