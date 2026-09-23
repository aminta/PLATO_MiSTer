// Types and constants taken from PTerm 6.0.4 (types.h, CommonHeader.h,
// ptermx.h), without the wxWidgets dependencies.
// Copyright (c) 2005-2018 Paul Koning, Joe Stanton, Dale Sinder.
// See pterm/pterm-license.txt.  Altered source version.

#ifndef PLATO_COMPAT_H
#define PLATO_COMPAT_H

#include <stdint.h>

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define None 0xffffffff
#define KEY(a, b, c, d) (((a) & 0xff) + (((b) & 0xff) << 8) + \
                         (((c) & 0xff) << 16) + (((d) & 0xff) << 24))
#define KEY1(a) KEY ((a), None, None, None)

#define RESIDENTMSEC    30      // msec to give resident before return to z80

#define xonkey          01606
#define xofkey          01607
#define ascxon          0x11
#define ascxof          0x13

#define C_NODATA        -1      // No more pending data in processing loop
#define C_CONNFAIL1     -2      // Failed to connect, hostname unknown
#define C_DISCONNECT    -3      // Connection was dropped
#define C_GSWEND        -4      // Stop GSW
#define C_CONNECTING    -5      // Connect request is in progress
#define C_CONNECTED     -6      // Connection has been established
#define C_CONNFAIL2     -7      // Failed to connect, no address worked

#define TERMTYPE        10
#define ASCTYPE         12
#define SUBTYPE         16
#define TERMCONFIG      0x60    // touch panel present, 32k

#define ASC_ZFGT        0x01
#define ASC_ZPCKEYS     0x02
#define ASC_ZKERMIT     0x04
#define ASC_ZWINDOW     0x08
#define ASCFEATURES     (ASC_ZFGT | ASC_ZWINDOW)

#define NOP_MASK        01700000
#define NOP_MASKDATA      077000
#define NOP_SETSTAT       042000
#define NOP_PMDSTART      043000
#define NOP_PMDSTREAM     044000
#define NOP_PMDSTOP       045000
#define NOP_FONTTYPE      050000
#define NOP_FONTSIZE      051000
#define NOP_FONTFLAG      052000
#define NOP_FONTINFO      053000
#define NOP_OSINFO        054000

#define DefNiuPort      5004
#define DefAsciiPort    8005
#define DEFAULTHOST     "cyberserv.org"

#endif
