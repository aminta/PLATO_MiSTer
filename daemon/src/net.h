// Host connection for the MiSTer PLATO terminal.
//
// Word assembly logic derived from PTerm 6.0.4 PtermConnection.cpp,
// Copyright (c) 2005-2018 Paul Koning, Joe Stanton, Dale Sinder.
// See pterm/pterm-license.txt.  ALTERED SOURCE VERSION: wxWidgets and the
// dtnetsubs network thread replaced by a single-threaded non-blocking socket.

#ifndef PLATO_NET_H
#define PLATO_NET_H

#include <string>
#include <deque>
#include <vector>

#include "compat.h"

class HostConnection
{
public:
    enum State { Idle, Resolving, Connecting, Connected, Failed, Closed };
    enum Mode { niu, ascii, both };

    HostConnection ();
    ~HostConnection ();

    // Start a connection.  "mode" both means auto-detect like PTerm.
    void Connect (const std::string &host, int port, Mode mode);
    void Close (void);

    int Fd (void) const { return m_fd; }
    State GetState (void) const { return m_state; }
    bool WantWrite (void) const
    {
        return m_state == Connecting || !m_out.empty ();
    }
    bool Ascii (void) const { return m_mode == ascii; }
    bool Detected (void) const { return m_mode != both; }
    const std::string &Error (void) const { return m_error; }

    // Called by the main loop when poll() reports activity.
    void OnReadable (void);
    void OnWritable (void);

    void SendData (const void *data, int len);

    // Next word for the display, with PTerm's delay encoding (bits 19+)
    // or C_NODATA.
    int NextWord (void);
    // Next word without delay encoding (for the GSW), or C_NODATA
    int PopRaw (void)
    {
        if (m_ring.empty ()) return C_NODATA;
        int w = m_ring.front ();
        m_ring.pop_front ();
        return w;
    }
    int RingCount (void) const { return (int) m_ring.size (); }
    void StoreWord (int word) { m_ring.push_back (word); }

private:
    int         m_fd;
    State       m_state;
    Mode        m_mode;
    std::string m_error;
    std::vector<u8> m_in;           // unassembled received bytes
    size_t      m_inPos;
    std::vector<u8> m_out;          // bytes waiting to be sent
    std::deque<int> m_ring;         // assembled display words
    int         m_pending;

    void Fail (const std::string &why);
    void Assemble (void);
    int AssembleNiuWord (void);
    int AssembleAsciiWord (void);
    int Avail (void) const { return (int) (m_in.size () - m_inPos); }
    int ReadByte (void) { return m_in[m_inPos++]; }
};

#endif
