// Host connection for the MiSTer PLATO terminal.
//
// Word assembly logic derived from PTerm 6.0.4 PtermConnection.cpp,
// Copyright (c) 2005-2018 Paul Koning, Joe Stanton, Dale Sinder.
// See pterm/pterm-license.txt.  ALTERED SOURCE VERSION.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "net.h"

HostConnection::HostConnection ()
    : m_fd (-1),
      m_state (Idle),
      m_mode (both),
      m_inPos (0),
      m_pending (0),
      m_aborted (false)
{
}

HostConnection::~HostConnection ()
{
    Close ();
}

void HostConnection::Close (void)
{
    if (m_fd >= 0)
    {
        close (m_fd);
        m_fd = -1;
    }
    m_in.clear ();
    m_inPos = 0;
    m_out.clear ();
    m_pending = 0;
    if (m_state != Failed)
    {
        m_state = Closed;
    }
}

void HostConnection::Fail (const std::string &why)
{
    m_error = why;
    if (m_fd >= 0)
    {
        close (m_fd);
        m_fd = -1;
    }
    m_state = Failed;
}

void HostConnection::Connect (const std::string &host, int port, Mode mode)
{
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];
    int r;

    Close ();
    m_ring.clear ();
    m_error.clear ();
    m_mode = mode;
    m_state = Resolving;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf (portstr, sizeof (portstr), "%d", port);
    r = getaddrinfo (host.c_str (), portstr, &hints, &res);
    if (r != 0 || res == NULL)
    {
        Fail (std::string ("cannot resolve ") + host);
        return;
    }

    for (ai = res; ai != NULL; ai = ai->ai_next)
    {
        m_fd = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (m_fd < 0)
        {
            continue;
        }
        fcntl (m_fd, F_SETFL, fcntl (m_fd, F_GETFL) | O_NONBLOCK);
        int one = 1;
        setsockopt (m_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof (one));
        setsockopt (m_fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof (one));
        r = connect (m_fd, ai->ai_addr, ai->ai_addrlen);
        if (r == 0)
        {
            m_state = Connected;
            break;
        }
        if (errno == EINPROGRESS)
        {
            m_state = Connecting;
            break;
        }
        close (m_fd);
        m_fd = -1;
    }
    freeaddrinfo (res);
    if (m_fd < 0)
    {
        Fail (std::string ("cannot connect to ") + host);
    }
}

void HostConnection::OnWritable (void)
{
    if (m_state == Connecting)
    {
        int err = 0;
        socklen_t len = sizeof (err);

        getsockopt (m_fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0)
        {
            Fail (strerror (err));
            return;
        }
        m_state = Connected;
    }
    if (m_state == Connected && !m_out.empty ())
    {
        ssize_t n = write (m_fd, m_out.data (), m_out.size ());
        if (n > 0)
        {
            m_out.erase (m_out.begin (), m_out.begin () + n);
        }
        else if (n < 0 && errno != EAGAIN && errno != EINTR)
        {
            Fail (strerror (errno));
        }
    }
}

void HostConnection::OnReadable (void)
{
    u8 buf[4096];

    if (m_state == Connecting)
    {
        OnWritable ();
    }
    if (m_state != Connected)
    {
        return;
    }
    for (;;)
    {
        ssize_t n = read (m_fd, buf, sizeof (buf));
        if (n > 0)
        {
            m_in.insert (m_in.end (), buf, buf + n);
            if (m_rxTap)
            {
                m_rxTap (buf, (int) n);
            }
            continue;
        }
        if (n == 0)
        {
            Assemble ();
            m_error = "connection closed by host";
            close (m_fd);
            m_fd = -1;
            m_state = Closed;
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }
        if (errno == EINTR)
        {
            continue;
        }
        Fail (strerror (errno));
        return;
    }
    Assemble ();
}

void HostConnection::Inject (const u8 *data, int len)
{
    m_state = Connected;
    m_in.insert (m_in.end (), data, data + len);
    Assemble ();
}

void HostConnection::SendData (const void *data, int len)
{
    const u8 *p = (const u8 *) data;

    if (m_state != Connected && m_state != Connecting)
    {
        return;
    }
    if (m_fd < 0)
    {
        return;     // replay
    }
    m_out.insert (m_out.end (), p, p + len);
    if (m_state == Connected)
    {
        OnWritable ();
    }
}

// Assemble words from the received bytes (PtermHostConnection::dataCallback)
void HostConnection::Assemble (void)
{
    int platowd;
    // Words still waiting from earlier reads: only these may be discarded
    // by an abort marker.  PTerm assembles in a network thread while the
    // display consumes the ring, so in practice it only drops output that
    // the display had not caught up with; the words that arrive together
    // with the marker are always shown.  Dropping them here lost "load
    // mode" commands and garbled the display.
    size_t old = m_ring.size ();

    for (;;)
    {
        if (m_mode == both)
        {
            // Auto-detect the protocol from the first 3 bytes
            // (PtermHostConnection::AssembleAutoWord)
            if (Avail () < 3)
            {
                break;
            }
            const u8 *b = &m_in[m_inPos];
            if ((b[0] & 0200) == 0 &&
                (b[1] & 0300) == 0200 &&
                (b[2] & 0300) == 0300)
            {
                m_mode = niu;
            }
            else
            {
                m_mode = ascii;
            }
        }
        if (m_mode == niu)
        {
            platowd = AssembleNiuWord ();
        }
        else
        {
            platowd = AssembleAsciiWord ();
        }

        if (platowd == C_NODATA)
        {
            break;
        }
        else if (m_mode == niu && platowd == 2)
        {
            // erase abort marker -- discard the output from earlier reads
            // that has not been displayed yet
            if (getenv ("PLATOD_WORDLOG"))
            {
                fprintf (stderr, "%llu ABORT (dropped %d of %d words)\n",
                         (unsigned long long) time (NULL), (int) old, (int) m_ring.size ());
            }
            m_ring.erase (m_ring.begin (), m_ring.begin () + old);
            old = 0;
            m_aborted = true;
        }
        m_ring.push_back (platowd);
    }

    // Drop consumed bytes
    if (m_inPos > 0)
    {
        m_in.erase (m_in.begin (), m_in.begin () + m_inPos);
        m_inPos = 0;
    }
}

int HostConnection::AssembleNiuWord (void)
{
    int i, j, k;

    for (;;)
    {
        if (Avail () < 3)
        {
            return C_NODATA;
        }
        i = ReadByte ();
        if (i & 0200)
        {
            continue;       // out of sync byte 0
        }
newj:
        if (Avail () < 2)
        {
            m_inPos--;
            return C_NODATA;
        }
        j = ReadByte ();
        if ((j & 0300) != 0200)
        {
            if ((j & 0200) == 0)
            {
                i = j;
                goto newj;
            }
            continue;
        }
        k = ReadByte ();
        if ((k & 0300) != 0300)
        {
            if ((k & 0200) == 0)
            {
                i = k;
                goto newj;
            }
            continue;
        }
        return (i << 12) | ((j & 077) << 6) | (k & 077);
    }
}

int HostConnection::AssembleAsciiWord (void)
{
    int i;

    for (;;)
    {
        if (Avail () < 1)
        {
            return C_NODATA;
        }
        i = ReadByte ();
        if (m_pending == 0 && i == 0377)
        {
            // 0377 is used by Telnet to introduce commands (IAC).
            // We recognize only IAC IAC for now.
            m_pending = 0377;
            continue;
        }

        i &= 0177;
        if (i == 033)
        {
            m_pending = 033;
            continue;
        }
        if (m_pending == 033)
        {
            m_pending = 0;
            return (033 << 8) + i;
        }
        else
        {
            m_pending = 0;
            if (i == 0)
            {
                // NUL is for -delay-
                i = 1 << 19;
            }
            return i;
        }
    }
}

int HostConnection::NextWord (void)
{
    int word;

    if (m_ring.empty ())
    {
        return C_NODATA;
    }
    word = m_ring.front ();
    m_ring.pop_front ();

    // The -delay- NOP code in classic mode
    if (m_mode == niu && word == 1)
    {
        word |= (1 << 19);
    }
    return word;
}
