// PLATO terminal engine for the MiSTer PLATO core.
//
// Derived from PTerm 6.0.4 FrameCanvas.cpp,
// Copyright (c) 2005-2018 Paul Koning, Joe Stanton, Dale Sinder,
// Bill Galcher, Steve Zoppi.  See pterm/pterm-license.txt.
//
// ALTERED SOURCE VERSION: wxWidgets GUI removed, rendering goes to a plain
// 512x512 pixel buffer, timers replaced by a millisecond Tick() call.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>

#include "engine.h"
#include "ppt.h"
#include "keytabs.h"

#define RAM     m_context.memory

// PLATO terminal state kept in PPT RAM (same macros as PtermFrame.h)
#define mode        RAM[M_MODE]
#define mt_ksw      RAM[M_KSW]
#define mjobs       RAM[M_JOBS]
#define m_enab      RAM[M_ENAB]
#define key2mtutor  ((mt_ksw & 1) == 1)
#define wemode      (mode & 3)
#define margin      ReadRAMW (M_MARGIN)
#define uncover     ((RAM[M_CCR] & 0x80) != 0)
#define reverse     ((RAM[M_CCR] & 0x40) != 0 )
#define large       ((RAM[M_CCR] & 0x20) != 0)
#define currentCharset ((RAM[M_CCR] & 0x0e) >> 1)
#define vertical    ((RAM[M_CCR] & 0x01) != 0)

#define Parity(x) (x)
#define BOUND(x) (((x < 0) ? 0 : ((x > 511) ? 511 : x)))

enum { restore = 0, save };
enum { micro = 0, host };

static const u32 MAXALPHA = 0xff000000;

// Input ring flow control thresholds (ptermx.h)
#define RINGSIZE        5000
#define RINGXON1        (RINGSIZE / 3)
#define RINGXOFF1       (RINGSIZE - RINGXON1)

// CYBIS workstation windowing defines
#define CWS_SAVE        0
#define CWS_RESTORE     1
#define CWS_EXEC        1012
#define CWS_TERMSAVE    2000
#define CWS_TERMRESTORE 2001

// Tracing (enabled with PLATOD_TRACE=1 in the environment)
static bool traceOn = getenv ("PLATOD_TRACE") != NULL;
#define trace(...) do { if (traceOn) { fprintf (stderr, __VA_ARGS__); \
                                       fputc ('\n', stderr); } } while (0)

static const u8 asciiM0[] =
{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0x2d, 0x38, 0x37, 0x80, 0x2b, 0x33, 0xab, 0x36,
0x29, 0x2a, 0x27, 0x25, 0x2e, 0x26, 0x2f, 0x28,
0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22,
0x23, 0x24, 0x00, 0x39, 0x3a, 0x2c, 0x3b, 0x3d,
0xbd, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
0x98, 0x99, 0x9a, 0x31, 0xbe, 0x32, 0x9d, 0x3c,
0x9f, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
0x18, 0x19, 0x1a, 0xa9, 0xae, 0xaa, 0xa4, 0xff
};
static const u8 asciiM1[] =
{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0x2d, 0x28, 0xb0, 0x9b, 0x35, 0xac, 0xa0, 0xa1,
0xa2, 0xa3, 0x34, 0xa5, 0xa6, 0xa7, 0xa8, 0x30,
0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8,
0xb9, 0xba, 0xbb, 0xbc, 0xc0, 0xaf, 0xc1, 0x3e,
0xc2, 0x9c, 0xc3, 0xc8, 0xc4, 0xc5, 0x9e, 0xc9,
0xc6, 0xc7, 0xae, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static const u8 asciiKeycodes[] =
{ 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
0x38, 0x39, 0x26, 0x60, 0x0a, 0x5e, 0x2b, 0x2d,
0x13, 0x04, 0x07, 0x08, 0x7b, 0x0b, 0x0d, 0x1a,
0x02, 0x12, 0x01, 0x03, 0x7d, 0x0c, 0xff, 0xff,
0x3c, 0x3e, 0x5b, 0x5d, 0x24, 0x25, 0x5f, 0x7c,
0x2a, 0x28, 0x40, 0x27, 0x1c, 0x5c, 0x23, 0x7e,
0x17, 0x05, 0x14, 0x19, 0x7f, 0x09, 0x1e, 0x18,
0x0e, 0x1d, 0x11, 0x16, 0x00, 0x0f, 0xff, 0xff,
0x20, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
0x78, 0x79, 0x7a, 0x3d, 0x3b, 0x2f, 0x2e, 0x2c,
0x1f, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
0x58, 0x59, 0x5a, 0x29, 0x3a, 0x3f, 0x21, 0x22
};

// Conversion from ascii mode codes to classic codes
static const u8 ascmode[] = { 0, 3, 2, 1 };

/* data for plato font, set 0. */
const unsigned short plato_m0[] = {
    0x0000, 0x0000, 0x0330, 0x0330, 0x0000, 0x0000, 0x0000, 0x0000, // :
    0x0060, 0x0290, 0x0290, 0x0290, 0x0290, 0x01e0, 0x0010, 0x0000, // a
    0x1ff0, 0x0120, 0x0210, 0x0210, 0x0210, 0x0120, 0x00c0, 0x0000, // b
    0x00c0, 0x0120, 0x0210, 0x0210, 0x0210, 0x0210, 0x0120, 0x0000, // c
    0x00c0, 0x0120, 0x0210, 0x0210, 0x0210, 0x0120, 0x1ff0, 0x0000, // d
    0x00c0, 0x01a0, 0x0290, 0x0290, 0x0290, 0x0290, 0x0190, 0x0000, // e
    0x0000, 0x0000, 0x0210, 0x0ff0, 0x1210, 0x1000, 0x0800, 0x0000, // f
    0x01a8, 0x0254, 0x0254, 0x0254, 0x0254, 0x0194, 0x0208, 0x0000, // g
    0x1000, 0x1ff0, 0x0100, 0x0200, 0x0200, 0x0200, 0x01f0, 0x0000, // h
    0x0000, 0x0000, 0x0210, 0x13f0, 0x0010, 0x0000, 0x0000, 0x0000, // i
    0x0000, 0x0002, 0x0202, 0x13fc, 0x0000, 0x0000, 0x0000, 0x0000, // j
    0x1010, 0x1ff0, 0x0080, 0x0140, 0x0220, 0x0210, 0x0010, 0x0000, // k
    0x0000, 0x0000, 0x1010, 0x1ff0, 0x0010, 0x0000, 0x0000, 0x0000, // l
    0x03f0, 0x0200, 0x0200, 0x01f0, 0x0200, 0x0200, 0x01f0, 0x0000, // m
    0x0200, 0x03f0, 0x0100, 0x0200, 0x0200, 0x0200, 0x01f0, 0x0000, // n
    0x00c0, 0x0120, 0x0210, 0x0210, 0x0210, 0x0120, 0x00c0, 0x0000, // o
    0x03fe, 0x0120, 0x0210, 0x0210, 0x0210, 0x0120, 0x00c0, 0x0000, // p
    0x00c0, 0x0120, 0x0210, 0x0210, 0x0210, 0x0120, 0x03fe, 0x0000, // q
    0x0200, 0x03f0, 0x0100, 0x0200, 0x0200, 0x0200, 0x0100, 0x0000, // r
    0x0120, 0x0290, 0x0290, 0x0290, 0x0290, 0x0290, 0x0060, 0x0000, // s
    0x0200, 0x0200, 0x1fe0, 0x0210, 0x0210, 0x0210, 0x0000, 0x0000, // t
    0x03e0, 0x0010, 0x0010, 0x0010, 0x0010, 0x03e0, 0x0010, 0x0000, // u
    0x0200, 0x0300, 0x00c0, 0x0030, 0x00c0, 0x0300, 0x0200, 0x0000, // v
    0x03e0, 0x0010, 0x0020, 0x01c0, 0x0020, 0x0010, 0x03e0, 0x0000, // w
    0x0200, 0x0210, 0x0120, 0x00c0, 0x00c0, 0x0120, 0x0210, 0x0000, // x
    0x0382, 0x0044, 0x0028, 0x0010, 0x0020, 0x0040, 0x0380, 0x0000, // y
    0x0310, 0x0230, 0x0250, 0x0290, 0x0310, 0x0230, 0x0000, 0x0000, // z
    0x0010, 0x07e0, 0x0850, 0x0990, 0x0a10, 0x07e0, 0x0800, 0x0000, // 0
    0x0000, 0x0000, 0x0410, 0x0ff0, 0x0010, 0x0000, 0x0000, 0x0000, // 1
    0x0000, 0x0430, 0x0850, 0x0890, 0x0910, 0x0610, 0x0000, 0x0000, // 2
    0x0000, 0x0420, 0x0810, 0x0910, 0x0910, 0x06e0, 0x0000, 0x0000, // 3
    0x0000, 0x0080, 0x0180, 0x0280, 0x0480, 0x0ff0, 0x0080, 0x0000, // 4
    0x0000, 0x0f10, 0x0910, 0x0910, 0x0920, 0x08c0, 0x0000, 0x0000, // 5
    0x0000, 0x03e0, 0x0510, 0x0910, 0x0910, 0x00e0, 0x0000, 0x0000, // 6
    0x0000, 0x0800, 0x0830, 0x08c0, 0x0b00, 0x0c00, 0x0000, 0x0000, // 7
    0x0000, 0x06e0, 0x0910, 0x0910, 0x0910, 0x06e0, 0x0000, 0x0000, // 8
    0x0000, 0x0700, 0x0890, 0x0890, 0x08a0, 0x07c0, 0x0000, 0x0000, // 9
    0x0000, 0x0080, 0x0080, 0x03e0, 0x0080, 0x0080, 0x0000, 0x0000, // +
    0x0000, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0000, 0x0000, // -
    0x0000, 0x0240, 0x0180, 0x0660, 0x0180, 0x0240, 0x0000, 0x0000, // *
    0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200, 0x0400, 0x0000, // /
    0x0000, 0x0000, 0x0000, 0x0000, 0x07e0, 0x0810, 0x1008, 0x0000, // (
    0x1008, 0x0810, 0x07e0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // )
    0x0640, 0x0920, 0x0920, 0x1ff0, 0x0920, 0x0920, 0x04c0, 0x0000, // $
    0x0000, 0x0140, 0x0140, 0x0140, 0x0140, 0x0140, 0x0000, 0x0000, // =
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // space
    0x0000, 0x0000, 0x0034, 0x0038, 0x0000, 0x0000, 0x0000, 0x0000, // ,
    0x0000, 0x0000, 0x0030, 0x0030, 0x0000, 0x0000, 0x0000, 0x0000, // .
    0x0000, 0x0080, 0x0080, 0x02a0, 0x0080, 0x0080, 0x0000, 0x0000, // divide
    0x0000, 0x0000, 0x0000, 0x0000, 0x1ff8, 0x1008, 0x1008, 0x0000, // [
    0x1008, 0x1008, 0x1ff8, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // ]
    0x0c20, 0x1240, 0x0c80, 0x0100, 0x0260, 0x0490, 0x0860, 0x0000, // %
    0x0000, 0x0000, 0x0240, 0x0180, 0x0180, 0x0240, 0x0000, 0x0000, // multiply
    0x0080, 0x0140, 0x0220, 0x0770, 0x0140, 0x0140, 0x0140, 0x0000, // assign
    0x0000, 0x0000, 0x0000, 0x1c00, 0x0000, 0x0000, 0x0000, 0x0000, // '
    0x0000, 0x0000, 0x1c00, 0x0000, 0x1c00, 0x0000, 0x0000, 0x0000, // "
    0x0000, 0x0000, 0x0000, 0x1f90, 0x0000, 0x0000, 0x0000, 0x0000, // !
    0x0000, 0x0000, 0x0334, 0x0338, 0x0000, 0x0000, 0x0000, 0x0000, // ;
    0x0000, 0x0080, 0x0140, 0x0220, 0x0410, 0x0000, 0x0000, 0x0000, // <
    0x0000, 0x0000, 0x0410, 0x0220, 0x0140, 0x0080, 0x0000, 0x0000, // >
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, // _
    0x0000, 0x0c00, 0x1000, 0x10d0, 0x1100, 0x0e00, 0x0000, 0x0000, // ?
    0x1c1c, 0x1224, 0x0948, 0x0490, 0x0220, 0x0140, 0x0080, 0x0000, // arrow
    0x0000, 0x0000, 0x0000, 0x000a, 0x0006, 0x0000, 0x0000, 0x0000, // cedilla
};

/* data for plato font, set 1. */
const unsigned short plato_m1[] = {
    0x0500, 0x0500, 0x1fc0, 0x0500, 0x1fc0, 0x0500, 0x0500, 0x0000, // #
    0x07f0, 0x0900, 0x1100, 0x1100, 0x1100, 0x0900, 0x07f0, 0x0000, // A
    0x1ff0, 0x1210, 0x1210, 0x1210, 0x1210, 0x0e10, 0x01e0, 0x0000, // B
    0x07c0, 0x0820, 0x1010, 0x1010, 0x1010, 0x1010, 0x0820, 0x0000, // C
    0x1ff0, 0x1010, 0x1010, 0x1010, 0x1010, 0x0820, 0x07c0, 0x0000, // D
    0x1ff0, 0x1110, 0x1110, 0x1110, 0x1010, 0x1010, 0x1010, 0x0000, // E
    0x1ff0, 0x1100, 0x1100, 0x1100, 0x1000, 0x1000, 0x1000, 0x0000, // F
    0x07c0, 0x0820, 0x1010, 0x1010, 0x1090, 0x1090, 0x08e0, 0x0000, // G
    0x1ff0, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x1ff0, 0x0000, // H
    0x0000, 0x1010, 0x1010, 0x1ff0, 0x1010, 0x1010, 0x0000, 0x0000, // I
    0x0020, 0x0010, 0x1010, 0x1010, 0x1fe0, 0x1000, 0x1000, 0x0000, // J
    0x1ff0, 0x0080, 0x0100, 0x0280, 0x0440, 0x0820, 0x1010, 0x0000, // K
    0x1ff0, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0000, // L
    0x1ff0, 0x0800, 0x0400, 0x0200, 0x0400, 0x0800, 0x1ff0, 0x0000, // M
    0x1ff0, 0x0800, 0x0600, 0x0100, 0x00c0, 0x0020, 0x1ff0, 0x0000, // N
    0x07c0, 0x0820, 0x1010, 0x1010, 0x1010, 0x0820, 0x07c0, 0x0000, // O
    0x1ff0, 0x1100, 0x1100, 0x1100, 0x1100, 0x1100, 0x0e00, 0x0000, // P
    0x07c0, 0x0820, 0x1010, 0x1018, 0x1014, 0x0824, 0x07c0, 0x0000, // Q
    0x1ff0, 0x1100, 0x1100, 0x1180, 0x1140, 0x1120, 0x0e10, 0x0000, // R
    0x0e20, 0x1110, 0x1110, 0x1110, 0x1110, 0x1110, 0x08e0, 0x0000, // S
    0x1000, 0x1000, 0x1000, 0x1ff0, 0x1000, 0x1000, 0x1000, 0x0000, // T
    0x1fe0, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x1fe0, 0x0000, // U
    0x1800, 0x0700, 0x00c0, 0x0030, 0x00c0, 0x0700, 0x1800, 0x0000, // V
    0x1fe0, 0x0010, 0x0020, 0x03c0, 0x0020, 0x0010, 0x1fe0, 0x0000, // W
    0x1830, 0x0440, 0x0280, 0x0100, 0x0280, 0x0440, 0x1830, 0x0000, // X
    0x1800, 0x0400, 0x0200, 0x01f0, 0x0200, 0x0400, 0x1800, 0x0000, // Y
    0x1830, 0x1050, 0x1090, 0x1110, 0x1210, 0x1410, 0x1830, 0x0000, // Z
    0x0000, 0x1000, 0x2000, 0x2000, 0x1000, 0x1000, 0x2000, 0x0000, // ~
    0x0000, 0x0000, 0x1000, 0x0000, 0x1000, 0x0000, 0x0000, 0x0000, // dieresis
    0x0000, 0x1000, 0x2000, 0x4000, 0x2000, 0x1000, 0x0000, 0x0000, // circumflex
    0x0000, 0x0000, 0x0000, 0x1000, 0x2000, 0x4000, 0x0000, 0x0000, // acute
    0x0000, 0x4000, 0x2000, 0x1000, 0x0000, 0x0000, 0x0000, 0x0000, // grave
    0x0000, 0x0100, 0x0300, 0x07f0, 0x0300, 0x0100, 0x0000, 0x0000, // uparrow
    0x0080, 0x0080, 0x0080, 0x0080, 0x03e0, 0x01c0, 0x0080, 0x0000, // rtarrow 
    0x0000, 0x0040, 0x0060, 0x07f0, 0x0060, 0x0040, 0x0000, 0x0000, // downarrow
    0x0080, 0x01c0, 0x03e0, 0x0080, 0x0080, 0x0080, 0x0080, 0x0000, // leftarrow
    0x0000, 0x0080, 0x0100, 0x0100, 0x0080, 0x0080, 0x0100, 0x0000, // low tilde
    0x1010, 0x1830, 0x1450, 0x1290, 0x1110, 0x1010, 0x1010, 0x0000, // Sigma
    0x0030, 0x00d0, 0x0310, 0x0c10, 0x0310, 0x00d0, 0x0030, 0x0000, // Delta
    0x0000, 0x0380, 0x0040, 0x0040, 0x0040, 0x0380, 0x0000, 0x0000, // union
    0x0000, 0x01c0, 0x0200, 0x0200, 0x0200, 0x01c0, 0x0000, 0x0000, // intersect
    0x0000, 0x0000, 0x0000, 0x0080, 0x0f78, 0x1004, 0x1004, 0x0000, // {
    0x1004, 0x1004, 0x0f78, 0x0080, 0x0000, 0x0000, 0x0000, 0x0000, // }
    0x00e0, 0x0d10, 0x1310, 0x0c90, 0x0060, 0x0060, 0x0190, 0x0000, // &
    0x0150, 0x0160, 0x0140, 0x01c0, 0x0140, 0x0340, 0x0540, 0x0000, // not equal
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // space
    0x0000, 0x0000, 0x0000, 0x1ff0, 0x0000, 0x0000, 0x0000, 0x0000, // |
    0x0000, 0x0c00, 0x1200, 0x1200, 0x0c00, 0x0000, 0x0000, 0x0000, // degree
    0x0000, 0x02a0, 0x02a0, 0x02a0, 0x02a0, 0x02a0, 0x0000, 0x0000, // equiv
    0x01e0, 0x0210, 0x0210, 0x01a0, 0x0060, 0x0090, 0x0310, 0x0000, // alpha
    0x0002, 0x03fc, 0x0510, 0x0910, 0x0910, 0x0690, 0x0060, 0x0000, // beta
    0x0000, 0x0ce0, 0x1310, 0x1110, 0x0890, 0x0460, 0x0000, 0x0000, // delta
    0x0000, 0x1030, 0x0cc0, 0x0300, 0x00c0, 0x0030, 0x0000, 0x0000, // lambda
    0x0002, 0x0002, 0x03fc, 0x0010, 0x0010, 0x03e0, 0x0010, 0x0000, // mu
    0x0100, 0x0200, 0x03f0, 0x0200, 0x03f0, 0x0200, 0x0400, 0x0000, // pi
    0x0006, 0x0038, 0x00e0, 0x0110, 0x0210, 0x0220, 0x01c0, 0x0000, // rho
    0x00e0, 0x0110, 0x0210, 0x0310, 0x02e0, 0x0200, 0x0200, 0x0000, // sigma
    0x01e0, 0x0210, 0x0010, 0x00e0, 0x0010, 0x0210, 0x01e0, 0x0000, // omega
    0x0220, 0x0220, 0x0520, 0x0520, 0x08a0, 0x08a0, 0x0000, 0x0000, // less/equal
    0x0000, 0x08a0, 0x08a0, 0x0520, 0x0520, 0x0220, 0x0220, 0x0000, // greater/equal
    0x07c0, 0x0920, 0x1110, 0x1110, 0x1110, 0x0920, 0x07c0, 0x0000, // theta
    0x01e0, 0x0210, 0x04c8, 0x0528, 0x05e8, 0x0220, 0x01c0, 0x0000, // @
    0x0400, 0x0200, 0x0100, 0x0080, 0x0040, 0x0020, 0x0010, 0x0000, /* \ */
    0x01e0, 0x0210, 0x0210, 0x01e0, 0x0290, 0x0290, 0x01a0, 0x0000, // oe

    // "Special" character patterns: these are beyond the regular
    // 6-bit character indices, and are used when "special" ASCII
    // mode characters are encountered.  Rather than display them
    // from pieces, we just treat them as additional character
    // patterns.
    0x0000, 0x0080, 0x0140, 0x0220, 0x07f0, 0x0810, 0x1008, 0x0000, // l-embed
    0x1008, 0x0810, 0x07f0, 0x0220, 0x0140, 0x0080, 0x0000, 0x0000, // r-embed
    0x2184, 0x2244, 0x2424, 0x2424, 0x2424, 0x2424, 0x2244, 0x2004, // copyright
    0x0000, 0x03c0, 0x0240, 0x0240, 0x0240, 0x03c0, 0x0000, 0x0000, // box
    0x0080, 0x01c0, 0x03e0, 0x07f0, 0x03e0, 0x01c0, 0x0080, 0x0000, // diamond
    0x0410, 0x0220, 0x0140, 0x0080, 0x0140, 0x0220, 0x0410, 0x0000, // cross
    0x0000, 0x4000, 0x2000, 0x1000, 0x2000, 0x4000, 0x0000, 0x0000, // hacek
    0x0000, 0x0140, 0x0360, 0x07f0, 0x0360, 0x0140, 0x0000, 0x0000, // delim
    0x0000, 0x0180, 0x0240, 0x0240, 0x0180, 0x0000, 0x0000, 0x0000, // dot prod
    0x0000, 0x0000, 0x0000, 0x0002, 0x0004, 0x0008, 0x0000, 0x0000, // cedilla
};

const PlatoEngine::mptr PlatoEngine::modePtr[8] =
{
    &PlatoEngine::mode0, &PlatoEngine::mode1,
    &PlatoEngine::mode2, &PlatoEngine::mode3,
    &PlatoEngine::mode4, &PlatoEngine::mode5,
    &PlatoEngine::mode6, &PlatoEngine::mode7
};

static u32 MakePix (int r, int g, int b)
{
    return MAXALPHA | ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
}

// ----------------------------------------------------------------------------
// Construction / reset
// ----------------------------------------------------------------------------

PlatoEngine::PlatoEngine ()
    : m_pendingEcho (-1),
      m_ignoreDelay (false),
      m_dumbTty (true),
      m_flowCtrl (false),
      m_beep (false),
      m_touchEnabled (false),
      m_ringCount (0),
      m_noColor (false),
      m_ascii (true),
      m_now (0),
      m_z80ResumeAt (0),
      m_mclockNext (0),
      m_dclockNext (0)
{
    m_pixels = new u32[512 * 512];
    ClearDirty ();
    for (int i = 0; i < 10; i++)
    {
        cwswindow[i].ok = false;
        cwswindow[i].bm = NULL;
    }
    m_defFg = MakePix (255, 144, 0);
    m_defBg = MakePix (0, 0, 0);
    Reset ();
}

PlatoEngine::~PlatoEngine ()
{
    for (int i = 0; i < 10; i++)
    {
        delete [] cwswindow[i].bm;
    }
    delete [] m_pixels;
}

void PlatoEngine::SetDefaultColors (u32 fg, u32 bg)
{
    m_defFg = fg | MAXALPHA;
    m_defBg = bg | MAXALPHA;
}

// Swap the default colours in place (OSD colour change without a reset):
// pixels and colour settings that use the old default foreground or
// background get the new ones; colours chosen by the host are kept.
void PlatoEngine::ChangeDefaultColors (u32 fg, u32 bg)
{
    const u32 oldFg = m_defFg, oldBg = m_defBg;

    fg |= MAXALPHA;
    bg |= MAXALPHA;
    if (fg == oldFg && bg == oldBg)
    {
        return;
    }

    auto remap = [&] (u32 c) -> u32
    {
        if ((c | MAXALPHA) == oldFg) return fg;
        if ((c | MAXALPHA) == oldBg) return bg;
        return c;
    };

    for (int i = 0; i < 512 * 512; i++)
    {
        m_pixels[i] = remap (m_pixels[i]);
    }
    for (int w = 0; w < 10; w++)
    {
        if (cwswindow[w].bm != NULL)
        {
            for (int i = 0; i < 512 * 512; i++)
            {
                cwswindow[w].bm[i] = remap (cwswindow[w].bm[i]);
            }
        }
    }
    m_currentFg = remap (m_currentFg);
    m_currentBg = remap (m_currentBg);
    m_currentFgHost = remap (m_currentFgHost);
    m_currentBgHost = remap (m_currentBgHost);
    m_currentFgLocal = remap (m_currentFgLocal);
    m_currentBgLocal = remap (m_currentBgLocal);
    m_defFg = fg;
    m_defBg = bg;
    SetColors (m_currentFg, m_currentBg);
    MarkDirty (0, 511);
}

// Equivalent of the PtermFrame constructor state initialization.
void PlatoEngine::Reset (void)
{
    memset (RAM, 0, sizeof (m_context.memory));
    Z80Reset ();
    ppt_running = false;
    in_r_exec = false;
    m_giveupz80 = false;
    m_mtutorBoot = false;
    m_z80ResumeAt = 0;

    m_pendingEcho = -1;
    m_ignoreDelay = false;
    m_dumbTty = true;
    m_flowCtrl = false;
    m_sendFgt = false;
    mt_key = -1;
    modexor = false;
    currentX = 0;
    currentY = 496;
    memaddr = 0;
    memset (plato_m23, 0, sizeof (plato_m23));
    wc = 0;
    seq = 0;
    modewords = 0;
    mode4start = 0;
    m_ascState = none;
    m_ascBytes = 0;
    m_assembler = 0;
    lastX = lastY = 0;
    m_lastKey = -1;
    m_loadingPMD = false;
    m_PMD.clear ();
    m_fontPMD = m_fontinfo = m_osinfo = false;
    m_fontwidth = 8;
    m_fontheight = 16;
    m_indev = m_outdev = m_mtincnt = 0;
    m_touchEnabled = false;
    cwsmode = cwsfun = cwscnt = cwswin = 0;
    m_name.clear ();
    m_group.clear ();
    m_system.clear ();
    m_station.clear ();

    mode = 017;             // default to character mode, rewrite
    mt_ksw = 0;             // route input to terminal
    mjobs = 0;
    setMargin (0);
    RAM[M_CCR] = 0;
    RAM[M_TYPE] = 0x3c;

    // Set default character set origins (for PPT that is; ASCII is different)
    RAM[C2ORIGIN] = M2ADDR & 0xff;
    RAM[C2ORIGIN + 1] = M2ADDR >> 8;
    RAM[C3ORIGIN] = M3ADDR & 0xff;
    RAM[C3ORIGIN + 1] = M3ADDR >> 8;

    m_currentFg = m_defFg;
    m_currentBg = m_defBg;
    m_currentFgHost = m_currentFgLocal = m_defFg;
    m_currentBgHost = m_currentBgLocal = m_defBg;
    SetColors (m_currentFg, m_currentBg);
    ptermFullErase ();
}

// ----------------------------------------------------------------------------
// CCR helpers (from PtermFrame.h)
// ----------------------------------------------------------------------------

void PlatoEngine::setMargin (int i)
{
    RAM[M_MARGIN] = i;
    RAM[M_MARGIN + 1] = i >> 8;
}

void PlatoEngine::setUncover (bool u)
{
    if (u) RAM[M_CCR] |= 0x80; else RAM[M_CCR] &= ~0x80;
}

void PlatoEngine::setReverse (bool u)
{
    if (u) RAM[M_CCR] |= 0x40; else RAM[M_CCR] &= ~0x40;
}

void PlatoEngine::setLarge (bool u)
{
    if (u) RAM[M_CCR] |= 0x20; else RAM[M_CCR] &= ~0x20;
}

void PlatoEngine::setCmem (int i)
{
    RAM[M_CCR] = (RAM[M_CCR] & ~0x0e) | (i << 1);
}

void PlatoEngine::setVertical (bool u)
{
    trace ("vertical %d", u);
    if (u) RAM[M_CCR] |= 0x01; else RAM[M_CCR] &= ~0x01;
}

void PlatoEngine::SetColors (u32 fg, u32 bg)
{
    m_fgpix = fg | MAXALPHA;
    m_bgpix = bg | MAXALPHA;
}

// ----------------------------------------------------------------------------
// Drawing primitives
// ----------------------------------------------------------------------------

void PlatoEngine::MarkDirty (int y1, int y2)
{
    if (y1 < m_dirtyTop) m_dirtyTop = y1;
    if (y2 > m_dirtyBottom) m_dirtyBottom = y2;
}

// PLATO coordinates have y == 0 at the bottom; the pixel buffer has row 0
// at the top (YMADJUST in PTerm).
inline void PlatoEngine::ptermUpdatePoint (int x, int y, u32 pixval, bool xor_p)
{
    const int row = 511 - (y & 0777);
    u32 *pmap = &m_pixels[row * 512 + (x & 0777)];

    if (xor_p)
    {
        *pmap = (*pmap ^ pixval) | MAXALPHA;
    }
    else
    {
        *pmap = pixval;
    }
    MarkDirty (row, row);
}

void PlatoEngine::ptermDrawChar (int x, int y, int snum, int cnum)
{
    u32 fpix, bpix;
    const u16 *charp;

    // Drawing a character is done simply by drawing the dots one by one.
    if (snum == 0)
    {
        charp = plato_m0;
    }
    else if (snum == 1)
    {
        charp = plato_m1;
    }
    else
    {
        charp = plato_m23 + (snum - 2) * (8 * 64);
    }
    charp += 8 * cnum;

    if (modexor || (wemode & 1))
    {
        // mode rewrite or write
        fpix = m_fgpix;
        bpix = m_bgpix;
    }
    else
    {
        // mode inverse or erase
        fpix = m_bgpix;
        bpix = m_fgpix;
    }

    static bool charlog = getenv ("PLATOD_CHARLOG") != NULL;
    if (charlog && x >= 160 && x < 360 && y >= 200 && y < 340)
    {
        fprintf (stderr, "%llu C set %d char %02o at %d,%d wemode %d ccr %02x\n",
                 (unsigned long long) time (NULL), snum, cnum, x, y, wemode, RAM[M_CCR]);
    }
    ptermDrawCharInto (x, y, charp, fpix, bpix, mode, modexor);
}

void PlatoEngine::ptermDrawCharInto (int x, int y, const u16 *charp,
                                     u32 fpix, u32 bpix, int cmode,
                                     bool xor_p)
{
    int &cx = (vertical) ? y : x;
    int &cy = (vertical) ? x : y;
    int i, j, saveY, dx, dy, sdy;
    u16 charw;

    saveY = cy;
    dx = dy = (large) ? 2 : 1;
    sdy = 1;
    if (vertical)
    {
        sdy = -1;
        dy = -dy;
    }

    for (j = 0; j < 8; j++)
    {
        cy = saveY;
        charw = *charp++;
        for (i = 0; i < 16; i++)
        {
            if ((charw & 1) == 0)
            {
                // background, do we erase it?
                if ((cmode & 2) == 0)
                {
                    ptermUpdatePoint (x, y, bpix, false);
                    if (large)
                    {
                        ptermUpdatePoint (x + 1, y, bpix, false);
                        ptermUpdatePoint (x, y + sdy, bpix, false);
                        ptermUpdatePoint (x + 1, y + sdy, bpix, false);
                    }
                }
            }
            else
            {
                ptermUpdatePoint (x, y, fpix, xor_p);
                if (large)
                {
                    ptermUpdatePoint (x + 1, y, fpix, xor_p);
                    ptermUpdatePoint (x, y + sdy, fpix, xor_p);
                    ptermUpdatePoint (x + 1, y + sdy, fpix, xor_p);
                }
            }
            charw >>= 1;
            cy += dy;
        }
        cx += dx;
    }
}

void PlatoEngine::ptermDrawPoint (int x, int y)
{
    if (modexor || (wemode & 1))
    {
        // mode rewrite or write
        ptermUpdatePoint (x, y, m_fgpix, modexor);
    }
    else
    {
        // mode inverse or erase
        ptermUpdatePoint (x, y, m_bgpix, false);
    }
}

void PlatoEngine::ptermDrawLine (int x1, int y1, int x2, int y2)
{
    int dx, dy;
    int stepx, stepy;

    dx = x2 - x1;
    dy = y2 - y1;
    if (dx < 0) { dx = -dx;  stepx = -1; } else { stepx = 1; }
    if (dy < 0) { dy = -dy;  stepy = -1; } else { stepy = 1; }
    dx <<= 1;
    dy <<= 1;

    // draw first point
    ptermDrawPoint (x1, y1);

    //check for shallow line
    if (dx > dy)
    {
        int fraction = dy - (dx >> 1);
        while (x1 != x2)
        {
            if (fraction >= 0)
            {
                y1 += stepy;
                fraction -= dx;
            }
            x1 += stepx;
            fraction += dy;
            ptermDrawPoint (x1, y1);
        }
    }
    //otherwise steep line
    else
    {
        int fraction = dx - (dy >> 1);
        while (y1 != y2)
        {
            if (fraction >= 0)
            {
                x1 += stepx;
                fraction -= dy;
            }
            y1 += stepy;
            fraction += dx;
            ptermDrawPoint (x1, y1);
        }
    }
}

void PlatoEngine::ptermFullErase (void)
{
    const bool savexor = modexor;
    const int savemode = mode;

    // We'll simply handle this as a mode-erase block erase operation
    // for the whole screen (0..512 in x and y).
    modexor = false;
    mode = 2;   // erase
    ptermBlockErase (0, 0, 511, 511);
    modexor = savexor;
    mode = savemode;
}

void PlatoEngine::ptermBlockErase (int x1, int y1, int x2, int y2)
{
    int t;
    int x, y;
    u32 pix;

    if (x1 > x2)
        t = x1, x1 = x2, x2 = t;
    if (y1 > y2)
        t = y1, y1 = y2, y2 = t;

    if (modexor || (wemode & 1))
    {
        // mode rewrite or write
        pix = m_fgpix;
    }
    else
    {
        // mode inverse or erase
        pix = m_bgpix;
    }

    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            ptermUpdatePoint (x, y, pix, modexor);
        }
    }
}

// -paint- (flood fill) with foreground color if "pat" is zero, or
// the character with code "pat" otherwise.  See PTerm for the encoding.
void PlatoEngine::ptermPaint (int pat)
{
    ptermPaintWalker (currentX, 511 - currentY, pat, 0);
    ptermPaintWalker (currentX, 511 - currentY, pat, 1);
    MarkDirty (0, 511);
}

// Pass 0 means: stop if you hit background pixels; set all other pixels
// as visited.
// Pass 1 means: stop if you hit a pixel that's not been marked as visited;
// for a visited pixel, set it to foreground if it is supposed to be
// painted, or back to not visited if it is supposed to be unchanged.
//
// Since normal pixel values all have alpha == 255, we use alpha == 0
// as the marker value.  Coordinates here are buffer coordinates (row 0
// at the top), as in PTerm.
#define wdone(pmap, pass) ((pass && (*pmap & MAXALPHA) != 0) ||   \
                           (!pass && (*pmap == m_bgpix ||         \
                                      (*pmap & MAXALPHA) == 0)))

static u8 walkstack[512 * 512 + 2];

void PlatoEngine::ptermPaintWalker (int x, int y, int pat, int pass)
{
    u32 *pmap;
    int sp;
#define PUSH walkstack[++sp] = 0
    int w, i, d;
    const u16 *cp = NULL;

    if (pat)
    {
        if (pat < 256)
        {
            pat += 32;
            if (pat < 128)
            {
                d = asciiM0[pat];
            }
            else
            {
                d = asciiM1[pat - 128];
            }
        }
        else
        {
            d = pat;
        }

        if (d == 0xff)
        {
            // Trying to do char fill with a non-character
            return;
        }

        // i is the set, d is the offset within the set
        i = d >> 7;
        d &= 0x7f;

        // Form the offset to the character pattern
        if (i == 0)
        {
            cp = plato_m0;
        }
        else if (i == 1)
        {
            cp = plato_m1;
        }
        else
        {
            cp = plato_m23 + (i - 2) * (8 * 64);
        }
        cp += 8 * d;
    }

    sp = -1;
    PUSH;
    while (sp >= 0)
    {
        if (sp >= (int) sizeof (walkstack) - 1)
        {
            break;
        }
        pmap = &m_pixels[y * 512 + x];

        // Each time through the loop we increment the top of stack
        // value, to reflect progress in the walk
        w = walkstack[sp];
        walkstack[sp]++;

        switch (w)
        {
        case 0:
            // If the pixel is already filled, leave this level.
            // Otherwise, fill the pixel and explore to the left.
            if (wdone (pmap, pass))
            {
                walkstack[sp] = 4;
                break;
            }
            if (pass)
            {
                if (cp != NULL)
                {
                    // Character fill.  We draw the characters on
                    // coarse grid boundaries.
                    const int cx = x & 7;
                    const int cy = y & 15;

                    if ((cp[cx] & (0x8000 >> cy)) != 0)
                    {
                        // Foreground pixel, paint it
                        *pmap = m_fgpix;
                    }
                    else
                    {
                        // Backround pixel, unmark it
                        *pmap |= MAXALPHA;
                    }
                }
                else
                {
                    // Plain flood fill
                    *pmap = m_fgpix;
                }
            }
            else
            {
                // Mark the pixel by setting alpha to zero
                *pmap &= ~MAXALPHA;
            }
            if (x > 0)
            {
                x--;
                PUSH;
            }
            break;
        case 1:
            // Explore to the right
            if (x < 511)
            {
                x++;
                PUSH;
            }
            break;
        case 2:
            // Explore up
            if (y < 511)
            {
                y++;
                PUSH;
            }
            break;
        case 3:
            // Explore down
            if (y > 0)
            {
                y--;
                PUSH;
            }
            break;
        case 4:
            // Done exploring at this pixel.  Pop the stack and undo the
            // coordinate change made by the previous level.
            --sp;
            if (sp < 0)
            {
                break;
            }
            switch (walkstack[sp])
            {
            case 1:
                x++;
                break;
            case 2:
                x--;
                break;
            case 3:
                y--;
                break;
            case 4:
                y++;
                break;
            }
        }
    }
#undef PUSH
}

void PlatoEngine::ptermSaveWindow (int d)
{
    // We'll just copy the whole screen worth of bitmap, at restore time
    // we'll do a selective restore.
    trace ("CWS: process save; window %d", d);
    if (cwswindow[d].bm == NULL)
    {
        cwswindow[d].bm = new u32[512 * 512];
    }
    memcpy (cwswindow[d].bm, m_pixels, 512 * 512 * sizeof (u32));
    cwswindow[d].ok = true;
}

void PlatoEngine::ptermRestoreWindow (int d)
{
    int x, y, w, h, row;

    x = BOUND (cwswindow[d].data[0]);
    y = 511 - BOUND (cwswindow[d].data[1]);
    w = BOUND (cwswindow[d].data[2] - cwswindow[d].data[0]);
    h = BOUND (cwswindow[d].data[1] - cwswindow[d].data[3]);
    if (cwswindow[d].ok && cwswindow[d].bm != NULL)
    {
        trace ("CWS: process restore; window %d, region %d %d %d %d",
               d, x, y, w, h);
        for (row = y; row < y + h && row < 512; row++)
        {
            int n = (x + w > 512) ? 512 - x : w;
            memcpy (&m_pixels[row * 512 + x], &cwswindow[d].bm[row * 512 + x],
                    n * sizeof (u32));
        }
        MarkDirty (y, y + h);
        cwswindow[d].ok = false;
    }
}

void PlatoEngine::DumpCharset (const char *fn) const
{
    // 2 sets x 64 characters, 16 per row, each 8x16 pixels scaled 2x
    const int cw = 8 * 2 + 4, ch = 16 * 2 + 4, cols = 16, rows = 8;
    FILE *f = fopen (fn, "wb");
    if (f == NULL) return;
    fprintf (f, "P6\n%d %d\n255\n", cols * cw, rows * ch);
    for (int y = 0; y < rows * ch; y++)
    {
        for (int x = 0; x < cols * cw; x++)
        {
            int c = (y / ch) * cols + x / cw;
            int px = (x % cw) / 2, py = (y % ch) / 2;
            u8 v = 40;
            if (px < 8 && py < 16)
            {
                u16 col = plato_m23[c * 8 + px];
                v = (col & (1 << (15 - py))) ? 255 : 0;
            }
            fputc (v, f); fputc (v * 9 / 16, f); fputc (0, f);
        }
    }
    fclose (f);
}

// ----------------------------------------------------------------------------
// Local messages on the dumb TTY (used for connection status)
// ----------------------------------------------------------------------------

void PlatoEngine::LocalText (const char *s)
{
    // Leave PLATO terminal mode, like ESC ETX.
    if (!m_dumbTty)
    {
        m_dumbTty = true;
        m_flowCtrl = false;
        m_sendFgt = false;
        currentX = 0;
        currentY = 496;
    }
    for (; *s; s++)
    {
        if (*s == '\n')
        {
            procPlatoWord (015, true);
            procPlatoWord (012, true);
        }
        else
        {
            procPlatoWord ((u8) *s, true);
        }
    }
}

// ----------------------------------------------------------------------------
// Protocol decoder
// ----------------------------------------------------------------------------

/*--------------------------------------------------------------------------
**  Purpose:        Process word of PLATO output data
**
**  Parameters:     Name        Description.
**                  d           19-bit word
**                  ascii       true if using ASCII protocol
**
**  Returns:        true if the bitmap has changed, false otherwise.
**
**------------------------------------------------------------------------*/
bool PlatoEngine::procPlatoWord (u32 d, bool ascii)
{
    const char *msg = "";
    int i, n = 0;
    AscState    ascState;
    bool changed = false;

    // used in load coordinate
    int &coord = (d & 01000) ? currentY : currentX;
    int &cx = (vertical) ? currentY : currentX;
    int &cy = (vertical) ? currentX : currentY;

    int deltax, deltay, supdelta;

    bool settitleflag = false;

    deltax = (reverse) ? -8 : 8;
    deltay = (vertical) ? -16 : 16;
    if (large)
    {
        deltax *= 2;
        deltay *= 2;
    }
    supdelta = (deltay / 16) * 5;

    seq++;
    static bool wordlog = getenv ("PLATOD_WORDLOG") != NULL;
    if (wordlog)
    {
        fprintf (stderr, "%llu W %07o mode %d ccr %02x x %d y %d\n", (unsigned long long) time (NULL), d, mode >> 2, RAM[M_CCR],
                 currentX, currentY);
    }
    if (ascii)
    {
        if (m_dumbTty)
        {
            if (d == (033 << 8) + 002)   // ESC STX
            {
                trace ("Entering PLATO terminal mode");
                m_dumbTty = false;
                mode = (3 << 2) + 1;    // set character mode, rewrite
            }
            else if ((d >> 8) == 0)
            {
                changed = true;
                if (d >= 32 && d < 127)
                {
                    d = asciiM0[d];
                    if (d != 0xff)
                    {
                        // Force mode rewrite
                        mode = (3 << 2) + 1;
                        i = (d & 0x80) >> 7;
                        d &= 0x7f;
                        ptermDrawChar (currentX, currentY, i, d);
                        currentX = (currentX + 8) & 0777;
                    }
                }
                else if (d == 015)
                {
                    currentX = 0;
                }
                else if (d == 012)
                {
                    if (currentY != 0)
                    {
                        currentY -= 16;
                    }
                    else
                    {
                        // On the bottom line... scroll the display up
                        // by one text line.
                        memmove (m_pixels, m_pixels + 16 * 512,
                                 (512 - 16) * 512 * sizeof (u32));
                        MarkDirty (0, 511);
                    }
                    // Erase the line we just moved to.
                    mode = (3 << 2) + 2;    // set character mode, erase
                    ptermBlockErase (0, currentY, 511, currentY + 15);
                    mode = (3 << 2) + 1;    // set character mode, rewrite
                }
            }
        }
        else if (m_ascState == pni_rs)
        {
            // We just want to ignore 3 command codes.  Note that escape
            // sequences count for one, not two.
            if (++m_ascBytes == 3)
            {
                m_ascBytes = 0;
                m_ascState = none;
            }
        }
        else if (m_ascState == pmd)
        {
            n = AssembleAsciiPlatoMetaData (d);
            if (n == 0)
            {
                if (m_fontPMD)
                {
                    m_fontPMD = false;
                }
                else if (m_fontinfo)
                {
                    m_fontinfo = false;
                }
                else if (m_osinfo)
                {
                    m_osinfo = false;
                }
                else
                {
                    trace ("plato meta data complete: %s", m_PMD.c_str ());
                    ProcessPlatoMetaData ();
                }
                m_PMD.clear ();
            }
        }
        else if ((d >> 8) == 033)
        {
            // Escape sequence, the second character is in the low byte
            d &= 0377;
            switch (d)
            {
            case 002:   // ESC STX
                trace ("Still in PLATO terminal mode");
                m_dumbTty = false;
                break;
            case 003:   // ESC ETX
                trace ("Leaving PLATO terminal mode");
                m_dumbTty = true;
                m_flowCtrl = false;
                m_sendFgt = false;
                currentX = 0;
                currentY = 496;
                break;
            case 014:   // ESC FF
                trace ("Full screen erase");
                ptermFullErase ();
                changed = true;
                break;
            case 026:
                // mode xor (also sets mode write for off-screen DC operations)
                trace ("load mode xor");
                modexor = true;
                mode = (mode & ~3) + 2;
                break;
            case 021:   // ESC DC1
            case 022:   // ESC DC2
            case 023:   // ESC DC3
            case 024:   // ESC DC4
                // modes inverse, write, erase, rewrite
                modexor = false;
                mode = (mode & ~3) + ascmode[d - 021];
                trace ("load mode %d", mode);
                break;
            case '2':
                // Load coordinate
                m_ascState = ldc;
                m_ascBytes = 0;
                break;
            case '@':
                // superscript
                cy = (cy + supdelta) & 0777;
                break;
            case 'A':
                // subscript
                cy = (cy - supdelta) & 0777;
                break;
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
            case 'G':
            case 'H':
            case 'I':
                trace ("select memory M%d", d - 'B');
                setCmem (d - 'B');
                break;
            case 'J':
                setVertical (false);
                break;
            case 'K':
                setVertical (true);
                break;
            case 'L':
                setReverse (false);
                break;
            case 'M':
                setReverse (true);
                break;
            case 'N':
                setLarge (false);
                break;
            case 'O':
                setLarge (true);
                break;
            case 'P':
                modexor = false;
                mode = (mode & 3) + (2 << 2);
                break;
            case 'Q':
                m_ascState = ssf;
                m_ascBytes = 0;
                break;
            case 'R':
                // external data
                m_ascState = ext;
                m_ascBytes = 0;
                break;
            case 'S':
                modexor = false;
                mode = (mode & 3) + (2 << 2);
                break;
            case 'T':
                modexor = false;
                mode = (mode & 3) + (5 << 2);
                break;
            case 'U':
                modexor = false;
                mode = (mode & 3) + (6 << 2);
                break;
            case 'V':
                modexor = false;
                mode = (mode & 3) + (7 << 2);
                break;
            case 'W':
                // Load memory address
                m_ascState = lda;
                m_ascBytes = 0;
                break;
            case 'X':
                m_ascState = pmd;
                m_ascBytes = 0;
                break;
            case 'Y':
                // load echo
                m_ascState = lde;
                m_ascBytes = 0;
                break;
            case 'Z':
                // set margin
                setMargin (cx);
                break;
            case 'a':
                // set foreground color
                m_ascState = fg;
                m_ascBytes = 0;
                break;
            case 'b':
                // set background color
                m_ascState = bg;
                m_ascBytes = 0;
                break;
            case 'c':
                // paint
                m_ascState = paint;
                m_ascBytes = 0;
                break;
            case 'g':
                // set gray-scale foreground color
                m_ascState = gsfg;
                m_ascBytes = 0;
                break;
            default:
                trace ("Other unknown ESCAPE sequence: %d", d);
                break;
            }
        }
        else
        {
            switch (d)
            {
            case 010:   // backspace
                cx = (cx - deltax) & 0777;
                break;
            case 011:   // tab
                cx = (cx + deltax) & 0777;
                break;
            case 012:   // linefeed
                cy = (cy - deltay) & 0777;
                break;
            case 013:   // vertical tab
                cy = (cy + deltay) & 0777;
                break;
            case 014:   // form feed
                if (vertical)
                {
                    cx = deltay - 1;
                    cy = reverse ? 512 - deltax : 0;
                }
                else
                {
                    cy = 512 - deltay;
                    cx = reverse ? 512 - deltax : 0;
                }
                break;
            case 015:   // carriage return
                cx = margin;
                cy = (cy - deltay) & 0777;
                break;
            case 031:   // EM
                mode = (mode & 3) + (4 << 2);
                modewords = 0;              // words since entering mode
                break;
            case 034:   // FS
                mode = (mode & 3) + (0 << 2);
                break;
            case 035:   // GS
                mode = (mode & 3) + (1 << 2);
                m_ascState = ldc; // to have first coordinate be "dark"
                break;
            case 036:   // RS -- used by PNI in connect handshake
                m_ascState = pni_rs;
                break;
            case 037:   // US
                mode = (mode & 3) + (3 << 2);
                break;
            }
            if (d >= 040)
            {
                switch (m_ascState)
                {
                case ldc:
                    if (AssembleCoord (d))
                    {
                        currentX = lastX;
                        currentY = lastY;
                        trace ("load coordinate %d %d", currentX, currentY);
                    }
                    break;
                case paint:
                    n = AssemblePaint (d);
                    if (n != -1)
                    {
                        changed = true;
                        ptermPaint (n);
                    }
                    break;
                case lde:
                    n = AssembleData (d);
                    if (n != -1)
                    {
                        n &= 0177;
                        switch (n)
                        {
                        case 0160:
                            // 160 is terminal type query
                            n = 0160 + ASCTYPE;
                            break;
                        case 0x71:
                            n = SUBTYPE;
                            break;
                        case 0x72:
                            n = 0;
                            break;
                        case 0x73:
                            // hex 73 is report terminal config
                            n = TERMCONFIG;
                            break;
                        case 0x7b:
                            // hex 7b is beep
                            m_beep = true;
                            break;
                        case 0x7d:
                            // hex 7d is report MAR
                            n = memaddr;
                            break;
                        case 0x52:
                            // hex 52 is enable flow control
                            trace ("enable flow control");
                            m_flowCtrl = true;
                            n = 0x53;
                            break;
                        case 0x60:
                            // hex 60 is inquire features
                            n += ASCFEATURES;
                            m_sendFgt = true;
                            break;
                        default:
                            trace ("load echo %d (0x%02x)", n, n);
                        }
                        if (n == 0x7b)
                        {
                            // -beep- does NOT send an echo code in reply
                            break;
                        }

                        n += 0200;
                        if (m_ringCount > RINGXOFF1)
                        {
                            m_pendingEcho = n;
                        }
                        else
                        {
                            ptermSendKey1 (n);
                            m_pendingEcho = -1;
                        }
                    }
                    break;
                case lda:
                    n = AssembleData (d);
                    if (n != -1)
                    {
                        trace ("load memory address %04x", n);
                        memaddr = n & 077777;
                    }
                    break;
                case ext:
                    n = AssembleData (d);
                    switch (n)
                    {
                    case -1:
                        break;
                    // check for special TERM area save/restore
                    case CWS_TERMSAVE:
                        // data items are, in order: xleft, ytop, xright, ybot
                        cwswindow[0].data[0] = 0;
                        cwswindow[0].data[1] = 48;
                        cwswindow[0].data[2] = 511;
                        cwswindow[0].data[3] = 0;
                        ptermSaveWindow (0);
                        break;
                    case CWS_TERMRESTORE:
                        ptermRestoreWindow (0);
                        changed = true;
                        break;
                    default:
                        // check if in cws mode
                        switch (cwsmode)
                        {
                        // not in cws; font data is not supported
                        case 0:
                            cwscnt = 0;
                            break;
                        // check for cws data mode
                        case 1:
                            cwscnt++;
                            if (cwscnt == 1 && n == CWS_SAVE)
                            {
                                cwsfun = CWS_SAVE;
                            }
                            else if (cwscnt == 1 && n == CWS_RESTORE)
                            {
                                cwsfun = CWS_RESTORE;
                            }
                            else if (cwscnt == 1 || cwscnt > 6)
                            {
                                // unknown function; terminate cws mode
                                cwsmode = 0;
                                cwsfun = 0;
                                cwscnt = 0;
                            }
                            else if (cwscnt == 2)
                            {
                                if ((unsigned) n < sizeof (cwswindow) / sizeof (cwswindow[0]))
                                {
                                    cwswin = n;
                                }
                                else
                                {
                                    cwsmode = 0;
                                    cwsfun = 0;
                                    cwscnt = 0;
                                }
                            }
                            else if (cwscnt < 7)
                            {
                                cwswindow[cwswin].data[cwscnt - 3] = n;
                            }
                            break;
                        // check for cws execute mode
                        case 2:
                            cwscnt = 0;
                            if (n == CWS_EXEC)
                            {
                                switch (cwsfun)
                                {
                                case CWS_SAVE:
                                    ptermSaveWindow (cwswin);
                                    break;
                                case CWS_RESTORE:
                                    ptermRestoreWindow (cwswin);
                                    changed = true;
                                    break;
                                }
                            }
                            else
                            {
                                // unknown function; terminate cws mode
                                cwsmode = 0;
                                cwsfun = 0;
                                cwscnt = 0;
                            }
                            break;
                        }
                    }
                    break;
                case ssf:
                    n = AssembleData (d);
                    if (n != -1)
                    {
                        // Touch panel control (PtermCanvas::ptermTouchPanel)
                        m_touchEnabled = (n & 0x20) != 0;
                    }
                    switch (n)
                    {
                    case 0x1f00:    // xin 7; means start CWS functions
                        cwsmode = 1;
                        break;
                    case 0x1d00:    // xout 7; means stop CWS functions
                        cwsmode = 2;
                        break;
                    case -1:
                        break;
                    default:
                        trace ("ssf %04x", n);
                        break;
                    }
                    break;
                case fg:
                case bg:
                    ascState = m_ascState;
                    n = AssembleColor (d);
                    if (n != -1 && !m_noColor)
                    {
                        u32 c = MakePix ((n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff);
                        if (ascState == fg)
                        {
                            m_currentFg = c;
                        }
                        else
                        {
                            m_currentBg = c;
                        }
                        SetColors (m_currentFg, m_currentBg);
                    }
                    break;
                case gsfg:
                    ascState = m_ascState;
                    n = AssembleGrayScale (d);
                    if (n != -1 && !m_noColor)
                    {
                        m_currentFg = MakePix (n, n, n);
                        SetColors (m_currentFg, m_currentBg);
                    }
                    break;
                case pmd:
                    break; // handled above
                case none:
                    switch (mode >> 2)
                    {
                    case 0:
                        if (AssembleCoord (d))
                        {
                            mode0 ((lastX << 9) + lastY);
                            changed = true;
                        }
                        break;
                    case 1:
                        if (AssembleCoord (d))
                        {
                            mode1 ((lastX << 9) + lastY);
                            changed = true;
                        }
                        break;
                    case 2:
                        n = AssembleData (d);
                        if (n != -1)
                        {
                            mode2 (n);
                        }
                        break;
                    case 3: // text mode
                        m_ascState = none;
                        m_ascBytes = 0;
                        changed = true;
                        i = currentCharset;
                        if (i == 0)
                        {
                            d = asciiM0[d];
                            // The ROM vs. RAM choice is given by the
                            // current character set.
                            // For the ROM characters, the even vs. odd
                            // (M0 vs. M1) choice is given by the top bit
                            // of the ASCII translation table.
                            i = (d & 0x80) >> 7;
                        }
                        else if (i == 1)
                        {
                            d = asciiM1[d];
                            i = (d & 0x80) >> 7;
                        }
                        else
                        {
                            // RAM characters are indexed by printable
                            // ASCII characters; the RAM character offset
                            // is simply the character code - 32.
                            // The set choice is simply what the host sent.
                            d = (d - 040) & 077;
                        }
                        if (d != 0xff)
                        {
                            d &= 0x7f;
                            ptermDrawChar (currentX, currentY, i, d);
                            cx = (cx + deltax) & 0777;
                        }
                        break;
                    case 4:
                        if (AssembleCoord (d))
                        {
                            if (modewords & 1)
                            {
                                changed = true;
                            }
                            modewords++;
                            mode4 ((lastX << 9) + lastY);
                        }
                        break;
                    case 5:
                        n = AssembleData (d);
                        if (n != -1)
                        {
                            changed = true;
                            mode5 (n);
                        }
                        break;
                    case 6:
                        n = AssembleData (d);
                        if (n != -1)
                        {
                            changed = true;
                            mode6 (n);
                        }
                        break;
                    case 7:
                        n = AssembleData (d);
                        if (n != -1)
                        {
                            changed = true;
                            mode7 (n);
                        }
                        break;
                    }
                    break;
                case pni_rs:
                    break;
                }
            }
        }
    }
    else
    {
        if ((d & NOP_MASK) == 0)
        {
            // NOP command...
            if (d & 1)
            {
                wc = (wc + 1) & 0177;
            }
        }
        else
        {
            wc = (wc + 1) & 0177;
        }

        if (d & 01000000)
        {
            const int dmode = mode >> 2;

            modewords++;
            mptr mp = modePtr[dmode];
            (this->*mp) (d);
            // Modes 0, 1, 3, 4 change the screen.  Mode 5-7 we can't tell so
            // we assume it does.  So only mode 2 leaves "changed" untouched.
            changed |= dmode != 2;
        }
        else
        {
            switch ((d >> 15) & 7)
            {
            case 0:     // nop
                settitleflag = false;
                // special code to tell pterm the station number
                if ((d & NOP_MASKDATA) == NOP_SETSTAT)
                {
                    char buf[32];

                    d &= 0777;
                    snprintf (buf, sizeof (buf), "%d-%d", d >> 5, d & 31);
                    m_station = buf;
                }
                else if ((d & NOP_MASKDATA) == NOP_FONTTYPE ||
                         (d & NOP_MASKDATA) == NOP_FONTSIZE ||
                         (d & NOP_MASKDATA) == NOP_FONTFLAG)
                {
                    // Host fonts are not supported, the PLATO ROM
                    // characters are always used.
                }
                else if ((d & NOP_MASKDATA) == NOP_FONTINFO)
                {
                    ptermSendExt (m_fontwidth);
                    ptermSendExt (m_fontheight);
                }
                else if ((d & NOP_MASKDATA) == NOP_OSINFO)
                {
                    SendOsInfo ();
                }
                // otherwise check for plato meta data codes
                else
                {
                    if ((d & NOP_MASKDATA) == NOP_PMDSTART && !m_loadingPMD)
                    {
                        m_loadingPMD = true;
                        m_PMD.clear ();
                        settitleflag = AssembleClassicPlatoMetaData (d & 077);
                    }
                    else if ((d & NOP_MASKDATA) == NOP_PMDSTREAM && m_loadingPMD)
                    {
                        settitleflag = AssembleClassicPlatoMetaData (d & 077);
                    }
                    else if ((d & NOP_MASKDATA) == NOP_PMDSTOP && m_loadingPMD)
                    {
                        m_loadingPMD = false;
                        AssembleClassicPlatoMetaData (d & 077);
                        settitleflag = true;
                    }
                    if (settitleflag)
                    {
                        ProcessPlatoMetaData ();
                    }
                }
                break;

            case 1:     // load mode
                modewords = 0;              // words since entering mode
                if ((d & 020000) != 0)
                {
                    // load wc bit is set
                    wc = (d >> 6) & 0177;
                }
                modexor = false;
                mode = (d >> 1) & 037;
                if (d & 1)
                {
                    // full screen erase
                    ptermFullErase ();
                    changed = true;
                }
                trace ("load mode %d screen %d", mode, (d & 1));
                break;

            case 2:     // load coordinate
                if (d & 04000)
                {
                    // Add or subtract from current coordinate
                    if (d & 02000)
                    {
                        coord -= d & 0777;
                    }
                    else
                    {
                        coord += d & 0777;
                    }
                }
                else
                {
                    coord = d & 0777;
                }

                if (d & 010000)
                {
                    setMargin (coord);
                    msg = "margin";
                }
                trace ("load coord %c %d %s", (d & 01000) ? 'Y' : 'X',
                       d & 0777, msg);
                break;
            case 3:     // echo
                d &= 0177;
                switch (d)
                {
                case 0160:
                    // 160 is terminal type query
                    d = TERMTYPE;
                    break;
                case 0x7b:
                    // hex 7b is beep
                    m_beep = true;
                    break;
                case 0x7d:
                    // hex 7d is report MAR
                    d = memaddr;
                    break;
                default:
                    trace ("load echo %d", d);
                }
                if (d == 0x7b)
                {
                    break;          // -beep- does NOT send an echo code in reply
                }
                d += 0200;
                if (m_ringCount > RINGXOFF1)
                {
                    m_pendingEcho = d;
                }
                else
                {
                    ptermSendKey1 (d);
                    m_pendingEcho = -1;
                }
                break;

            case 4:     // load address
                memaddr = d & 077777;
                break;

            case 5:     // SSF on PPT
                if (((d >> 10) & 037) == 1)
                {
                    // Touch panel control
                    m_touchEnabled = (d & 040) != 0;
                }
                trace ("ssf %o", d);
                break;

            case 6:
            case 7:
                // -extout- (GSW sound): not supported, ignored
                break;

            default:    // ignore
                break;
            }
        }
    }

    return changed;
}

/*--------------------------------------------------------------------------
**  Assemblers for multi-byte ASCII protocol items (unchanged from PTerm)
**------------------------------------------------------------------------*/
int PlatoEngine::AssemblePaint (int d)
{
    if (m_ascBytes == 0)
    {
        m_assembler = 0;
    }
    m_assembler |= ((d & 077) << (m_ascBytes * 6));
    if (++m_ascBytes == 2)
    {
        m_ascBytes = 0;
        m_ascState = none;
        return m_assembler;
    }
    return -1;
}

int PlatoEngine::AssembleData (int d)
{
    if (m_ascBytes == 0)
    {
        m_assembler = 0;
    }
    m_assembler |= ((d & 077) << (m_ascBytes * 6));
    if (++m_ascBytes == 3)
    {
        m_ascBytes = 0;
        m_ascState = none;
        return m_assembler;
    }
    return -1;
}

int PlatoEngine::AssembleColor (int d)
{
    if (m_ascBytes == 0)
    {
        m_assembler = 0;
    }
    m_assembler |= ((d & 077) << (m_ascBytes * 6));
    if (++m_ascBytes == 4)
    {
        m_ascBytes = 0;
        m_ascState = none;
        return m_assembler;
    }
    return -1;
}

int PlatoEngine::AssembleGrayScale (int d)
{
    if (m_ascBytes == 0)
    {
        m_assembler = 0;
    }
    m_assembler = (d & 077) << 2;
    if (++m_ascBytes == 1)
    {
        m_ascBytes = 0;
        m_ascState = none;
        return m_assembler;
    }
    return -1;
}

bool PlatoEngine::AssembleCoord (int d)
{
    int c = d & 037;

    switch (d >> 5)
    {
    case 1: // High X or high Y
        if (m_ascBytes == 0)
        {
            // High Y
            lastY = (lastY & 037) | (c << 5);
            m_ascBytes = 2;
        }
        else
        {
            lastX = (lastX & 037) | (c << 5);
        }
        break;
    case 2:
        lastX = (lastX & 0740) | c;
        m_assembler = (lastX << 16) + lastY;
        m_ascBytes = 0;
        m_ascState = none;
        return true;
    case 3:
        lastY = (lastY & 0740) | c;
        m_ascBytes = 2;
        break;
    }
    return false;
}

static void AppendPmdChar (std::string &s, int d)
{
    if (d >= 1 && d <= 26)
    {
        s += (char) ('a' + d - 1);
    }
    else if (d >= 27 && d <= 36)
    {
        s += (char) ('0' + d - 27);
    }
    else if (d == 38)
    {
        s += '-';
    }
    else if (d == 40)
    {
        s += '/';
    }
    else if (d == 44)
    {
        s += '=';
    }
    else if (d == 45)
    {
        s += ' ';
    }
    else if (d == 63)
    {
        s += ';';
    }
}

void PlatoEngine::SendOsInfo (void)
{
    // sends 3 external keys, OS, major version, minor version.
    // PTerm reports wxOperatingSystemId; 0x80 is wxOS_UNIX_LINUX... we
    // simply report "Linux 3.0" as in wxGetOsVersion on Linux.
    ptermSendExt (0x1000 >> 6);
    ptermSendExt (3);
    ptermSendExt (0);
}

int PlatoEngine::AssembleAsciiPlatoMetaData (int d)
{
    int od = d;

    d &= 077;
    if (m_ascBytes == 0)
        m_PMD.clear ();
    m_ascBytes++;
    // check for start font mode
    if (od == 'F' && m_ascBytes == 1)
        m_fontPMD = true;
    // check for request font character info
    else if (od == 'f' && m_ascBytes == 1)
    {
        m_fontinfo = true;
        m_ascBytes = 0;
        m_ascState = none;
        ptermSendExt (m_fontwidth);
        ptermSendExt (m_fontheight);
        return 0;
    }
    // check for request operating system info
    else if (od == 'o' && m_ascBytes == 1)
    {
        SendOsInfo ();
        m_osinfo = true;
        m_ascBytes = 0;
        m_ascState = none;
        return 0;
    }
    // check if in font mode
    else if (m_fontPMD && m_ascBytes == 2)
    {
        if (d == 0)
        {
            m_ascBytes = 0;
            m_ascState = none;
            return 0;
        }
    }
    else if (m_fontPMD && m_ascBytes == 3)
    {
        // font size, ignored
    }
    else if (m_fontPMD && m_ascBytes == 4)
    {
        m_ascBytes = 0;
        m_ascState = none;
        return 0;
    }
    // check if done / full
    else if (d == 0 || m_ascBytes == 1001)
    {
        m_ascBytes = 0;
        m_ascState = none;
        return 0;
    }
    // otherwise keep assembling
    else
    {
        AppendPmdChar (m_PMD, d);
    }
    return -1;
}

bool PlatoEngine::AssembleClassicPlatoMetaData (int d)
{
    if (m_PMD.length () == 1000)
    {
        m_loadingPMD = false;
        return true;
    }
    AppendPmdChar (m_PMD, d & 077);
    return false;
}

static std::string PmdItem (const std::string &pmd, const char *key)
{
    size_t fnd = pmd.find (key);

    if (fnd == std::string::npos)
    {
        return std::string ();
    }
    std::string v = pmd.substr (fnd + strlen (key));
    size_t semi = v.find (';');
    if (semi != std::string::npos)
    {
        v.erase (semi);
    }
    return v;
}

void PlatoEngine::ProcessPlatoMetaData (void)
{
    if (m_PMD.find ("name=") != std::string::npos)
        m_name = PmdItem (m_PMD, "name=");
    if (m_PMD.find ("group=") != std::string::npos)
        m_group = PmdItem (m_PMD, "group=");
    if (m_PMD.find ("system=") != std::string::npos)
        m_system = PmdItem (m_PMD, "system=");
    if (m_PMD.find ("station=") != std::string::npos)
        m_station = PmdItem (m_PMD, "station=");
    trace ("meta data: name=%s group=%s system=%s station=%s",
           m_name.c_str (), m_group.c_str (), m_system.c_str (),
           m_station.c_str ());
}

// ----------------------------------------------------------------------------
// Character plotting and the mode handlers
// ----------------------------------------------------------------------------

void PlatoEngine::plotChar (int c)
{
    int &cx = (vertical) ? currentY : currentX;
    int &cy = (vertical) ? currentX : currentY;

    int deltax, deltay, supdelta;

    deltax = (reverse) ? -8 : 8;
    deltay = (vertical) ? -16 : 16;
    if (large)
    {
        deltax *= 2;
        deltay *= 2;
    }
    supdelta = (deltay / 16) * 5;

    // check for uncover code and fast exit
    c &= 077;
    if (c == 077)
    {
        setUncover (true);
        return;
    }

    if (uncover)
    {
        setUncover (false);
        switch (c)
        {
        case 010:   // backspace
            cx = (cx - deltax) & 0777;
            break;
        case 011:   // tab
            cx = (cx + deltax) & 0777;
            break;
        case 012:   // linefeed
            cy = (cy - deltay) & 0777;
            break;
        case 013:   // vertical tab
            cy = (cy + deltay) & 0777;
            break;
        case 014:   // form feed
            if (vertical)
            {
                cx = deltay - 1;
                cy = reverse ? 512 - deltax : 0;
            }
            else
            {
                cy = 512 - deltay;
                cx = reverse ? 512 - deltax : 0;
            }
            break;
        case 015:   // carriage return
            cx = margin;
            cy = (cy - deltay) & 0777;
            break;
        case 016:   // superscript
            cy = (cy + supdelta) & 0777;
            break;
        case 017:   // subscript
            cy = (cy - supdelta) & 0777;
            break;
        case 020:   // select M0
        case 021:   // select M1
        case 022:   // select M2
        case 023:   // select M3
        case 024:   // select M4
        case 025:   // select M5
        case 026:   // select M6
        case 027:   // select M7
            setCmem (c - 020);
            break;
        case 030:   // horizontal writing
            setVertical (false);
            break;
        case 031:   // vertical writing
            setVertical (true);
            break;
        case 032:   // forward writing
            setReverse (false);
            break;
        case 033:   // reverse writing
            setReverse (true);
            break;
        case 034:   // normal size writing
            setLarge (false);
            break;
        case 035:   // double size writing
            setLarge (true);
            break;
        default:
            break;
        }
    }
    else
    {
        ptermDrawChar (currentX, currentY, currentCharset, c);
        cx = (cx + deltax) & 0777;
    }
}

void PlatoEngine::mode0 (u32 d)
{
    int x, y;

    x = (d >> 9) & 0777;
    y = d & 0777;
    ptermDrawPoint (x, y);
    currentX = x;
    currentY = y;
}

void PlatoEngine::mode1 (u32 d)
{
    int x, y;

    x = (d >> 9) & 0777;
    y = d & 0777;
    ptermDrawLine (currentX, currentY, x, y);
    currentX = x;
    currentY = y;
}

void PlatoEngine::mode2 (u32 d)
{
    int chaddr;

    // Store the word in PPT RAM
    WriteRAM (memaddr, d);
    WriteRAM (memaddr + 1, d >> 8);

    // memaddr is a PPT RAM address; convert it to a character memory address
    chaddr = memaddr - ReadRAMW (C2ORIGIN);
    if (chaddr >= 0 && chaddr <= 127 * 16)
    {
        chaddr /= 2;
        if (((d >> 16) & 3) == 0)
        {
            // load data
            plato_m23[chaddr] = d & 0xffff;
        }
    }
    memaddr += 2;
}

void PlatoEngine::mode3 (u32 d)
{
    plotChar (d >> 12);
    plotChar (d >> 6);
    plotChar (d);
}

void PlatoEngine::mode4 (u32 d)
{
    int x1, y1, x2, y2;

    if (modewords & 1)
    {
        mode4start = d;
        return;
    }
    x1 = (mode4start >> 9) & 0777;
    y1 = mode4start & 0777;
    x2 = (d >> 9) & 0777;
    y2 = d & 0777;

    ptermBlockErase (x1, y1, x2, y2);
    m_ignoreDelay = true;      // Ignore any NOPs supplied by framat
    currentX = x1;
    currentY = y1 - 15;
}

void PlatoEngine::mode5 (u32 d)
{
    trace ("mode5 %06o", d);
    m_z80ResumeAt = 0;

    // Load C/D/E with data word
    state->registers.byte[Z80_C] = d >> 16;
    state->registers.byte[Z80_D] = d >> 8;
    state->registers.byte[Z80_E] = d;

    if (!(((state->registers.byte[Z80_C] & 3) == 0) &&
          (state->registers.byte[Z80_D] > 0)))
    {
        progmode (d, M5ORIGIN);
        return;
    }

    // Push the return address onto the stack, as if we just did a CALL
    WriteRAM (--state->registers.word[Z80_SP], ((state->pc) >> 8) & 0xff);
    WriteRAM (--state->registers.word[Z80_SP], (state->pc) & 0xff);

    // Set the start PC for the requested mode
    state->pc = ReadRAMW (M5ORIGIN);

    MicroEmulate ();
}

void PlatoEngine::mode6 (u32 d)
{
    trace ("mode6 %06o", d);
    m_z80ResumeAt = 0;

    // Load C/D/E with data word
    state->registers.byte[Z80_C] = d >> 16;
    state->registers.byte[Z80_D] = d >> 8;
    state->registers.byte[Z80_E] = d;

    // Push the return address onto the stack, as if we just did a CALL
    WriteRAM (--state->registers.word[Z80_SP], ((state->pc) >> 8) & 0xff);
    WriteRAM (--state->registers.word[Z80_SP], (state->pc) & 0xff);

    // Set the start PC for the requested mode
    state->pc = ReadRAMW (M6ORIGIN);
    MicroEmulate ();
}

void PlatoEngine::mode7 (u32 d)
{
    trace ("mode7 %06o", d);
    progmode (d, M7ORIGIN);
}

void PlatoEngine::progmode (u32 d, int origin)
{
    trace ("progmode pc=%04x", ReadRAMW (origin));
    // Load C/D/E with data word
    state->registers.byte[Z80_C] = d >> 16;
    state->registers.byte[Z80_D] = d >> 8;
    state->registers.byte[Z80_E] = d;

    // Initialize the stack
    state->registers.word[Z80_SP] = INITSP;

    // Push the fake return address for "return to main loop" onto the
    // stack, as if we just did a CALL instruction
    WriteRAM (--state->registers.word[Z80_SP], R_MAIN >> 8);
    WriteRAM (--state->registers.word[Z80_SP], R_MAIN);

    // Set the start PC for the requested mode
    state->pc = ReadRAMW (origin);
    MicroEmulate ();
}

// ----------------------------------------------------------------------------
// Keyboard output
// ----------------------------------------------------------------------------

void PlatoEngine::ptermSendKey (u32 keys)
{
    int i, key;

    for (i = 0; i < 32; i += 8)
    {
        key = (keys >> i) & 0xff;
        if (key != (None & 0xff))
        {
            ptermSendKey1 (key);
        }
    }
}

void PlatoEngine::ptermSendKey1 (int key)
{
    char data[5];
    int len;
    bool isStop1 = (key == 0x3a);

    trace ("key to plato %03o", key);

    if (key2mtutor || m_ascii)
    {
        // Assume one byte key code
        len = 1;
        if (key < 0200)
        {
            // Regular keyboard key
            key = asciiKeycodes[key];
            if (key == 0xff)
            {
                return;
            }
            if (m_flowCtrl && !key2mtutor)
            {
                // Do the keycode translation for the
                // "flow control enabled" coding rules.
                switch (key)
                {
                case 0x00:              // access
                    key = 0x1d;
                    len = 2;
                    break;
                case 0x05:              // shift-sub
                    key = 0x04;
                    len = 2;
                    break;
                case 0x0a:              // tab
                    key = 0x09;
                    break;
                case 0x09:              // shift-help
                    key = 0x0a;
                    break;
                case 0x11:              // shift-stop
                    key = 0x05;
                    break;
                case 0x17:              // shift-super
                    len = 2;
                    // fall through
                case 0x13:              // super
                    key = 0x17;
                    break;
                case 0x7c:              // apostrophe
                    key = 0x27;
                    break;
                case 0x27:              // #
                    key = 0x7c;
                    break;
                }
                data[0] = 033;          // store esc for 2 byte codes
            }
            data[len - 1] = Parity (key);

            if (!key2mtutor)
            {
                SendData (data, len);
                if (m_dumbTty && m_localEcho)
                {
                    // do local echoing
                    m_localEcho (key);
                }
            }
            else
            {
                // do mtutor zkey conversion
                key = (key < 0x80) ? mtutorcvt[key & 0x7f] : key;

                if (m_lastKey == key && key == 0x3a)
                {
                    m_lastKey = -1;
                    return;     // de-bounce stop1
                }
                m_lastKey = key;
                mt_key = key;
                if (isStop1)
                {
                    len = 1;
                    data[0] = Parity (5);
                    SendData (data, len);
                }
            }
        }
        else if (!m_dumbTty)
        {
            if (key == xofkey)
            {
                if (!m_flowCtrl)
                {
                    return;
                }
                data[0] = Parity (ascxof);
            }
            else if (key == xonkey)
            {
                if (!m_flowCtrl)
                {
                    return;
                }
                data[0] = Parity (ascxon);
            }
            else
            {
                len = 3;
                data[0] = 033;
                data[1] = Parity (0100 + (key & 077));
                data[2] = Parity (0140 + (key >> 6));
            }
            SendData (data, len);
        }
        else if ((key2mtutor) && key > 0x0ff)
            mt_key = key;  // touch/ext?
    }
    else
    {
        if (!key2mtutor)
        {
            data[0] = key >> 7;
            data[1] = 0200 | key;
            SendData (data, 2);
        }
        else
        {
            mt_key = key;
        }
    }
}

void PlatoEngine::ptermSendTouch (int x, int y)
{
    char data[6];

    if (m_sendFgt)
    {
        // Send fine grid touch code first
        data[0] = 033;
        data[1] = 0x1f;
        data[2] = 0x40 + (x & 0x1f);
        data[3] = 0x40 + ((x >> 5) & 0x0f);
        data[4] = 0x40 + (y & 0x1f);
        data[5] = 0x40 + ((y >> 5) & 0x0f);
        SendData (data, 6);
    }

    x /= 32;
    y /= 32;

    ptermSendKey1 (0x100 | (x << 4) | y);
}

void PlatoEngine::ptermSendExt (int key)
{
    char data[3];

    if (m_ascii)
    {
        // Send external key
        data[0] = 033;
        data[1] = 0x40 | (key & 0x3f);
        data[2] = 0x68 | ((key >> 6) & 0x03);
        SendData (data, 3);
    }
    else
    {
        data[0] = 0x04 | ((key >> 7) & 0x01);
        data[1] = 0x80 | (key & 0x7f);
        SendData (data, 2);
    }
}

// ----------------------------------------------------------------------------
// PPT (Z80) resident
// ----------------------------------------------------------------------------

void PlatoEngine::MicroEmulate (void)
{
    Z80Emulate (500000000);
}

bool PlatoEngine::PptBusy (void) const
{
    return ppt_running && !in_r_exec;
}

// Replaces the OnIdle / OnTimer / OnMz80 / OnMclock / OnDclock handlers.
void PlatoEngine::Tick (uint64_t now_ms)
{
    m_now = now_ms;

    // ppt m.clock, 60 Hz
    if (m_mclockNext == 0)
    {
        m_mclockNext = now_ms + 17;
        m_dclockNext = now_ms + 1000;
    }
    while (now_ms >= m_mclockNext)
    {
        u16 temp = ReadRAMW (M_CLOCK);
        WriteRAMW (M_CLOCK, ++temp);
        m_mclockNext += 17;
    }
    // ppt d.clock, 1 Hz
    while (now_ms >= m_dclockNext)
    {
        m_zclock++;
        m_dclockNext += 1000;
    }

    if (PptBusy ())
    {
        RunZ80 ();
    }
    else if (m_z80ResumeAt != 0 && now_ms >= m_z80ResumeAt)
    {
        // resume z80 execution after it gave up control to the resident
        m_z80ResumeAt = 0;
        RunZ80 ();
    }
}

// Called after a batch of host data has been processed: PTerm resumes a
// waiting Z80 program right away in that case.
void PlatoEngine::AfterData (void)
{
    if (m_z80ResumeAt != 0)
    {
        m_z80ResumeAt = 0;
        RunZ80 ();
    }
}

void PlatoEngine::RunZ80 (void)
{
    SaveRestoreColors (save, host);
    SaveRestoreColors (restore, micro);
    MicroEmulate ();
    SaveRestoreColors (save, micro);
    SaveRestoreColors (restore, host);
}

u32 PlatoEngine::GetColor (u16 loc)
{
    u8 exp = RAM[loc + 1];

    u32 cb = RAM[loc + 2] << 16;
    cb |= RAM[loc + 3] << 8;
    cb |= RAM[loc + 4];

    cb = cb >> (0x18 - exp);

    return MakePix ((cb >> 16) & 0xff, (cb >> 8) & 0xff, cb & 0xff);
}

void PlatoEngine::SaveRestoreColors (u8 action, u8 target)
{
    if (target == host)
    {
        if (action == restore)
        {
            m_currentFg = m_currentFgHost;
            m_currentBg = m_currentBgHost;
            SetColors (m_currentFg, m_currentBg);
        }
        else
        {
            m_currentFgHost = m_currentFg;
            m_currentBgHost = m_currentBg;
        }
    }
    else
    {
        if (action == restore)
        {
            m_currentFg = m_currentFgLocal;
            m_currentBg = m_currentBgLocal;
            SetColors (m_currentFg, m_currentBg);
        }
        else
        {
            m_currentFgLocal = m_currentFg;
            m_currentBgLocal = m_currentBg;
        }
    }
}

// Check the Z80 PC for a call into the PPT resident.
//  Returns 0 to keep executing, 1 to emulate a RET, 2 to stop emulation.
int PlatoEngine::check_pcZ80 (void)
{
    int x, y, cp, c, x2, y2;
    u8 L;

    switch (state->pc)
    {
    case R_MAIN:
        // "r.main" -- fake return address value used as the return
        // address for invocations of the mode 5/6/7 handler code
        return 2;

    case R_INIT:
        return 2;

    case R_DOT:
        x = state->registers.word[Z80_HL] & 0x1ff;
        y = state->registers.word[Z80_DE] & 0x1ff;
        ptermDrawPoint (x, y);
        currentX = x;
        currentY = y;
        return 1;

    case R_LINE:
        x = state->registers.word[Z80_HL] & 0x1ff;
        y = state->registers.word[Z80_DE] & 0x1ff;
        ptermDrawLine (currentX, currentY, x, y);
        currentX = x;
        currentY = y;
        return 1;

    case R_CHARS:
        // draw chars ending at 07700
        cp = state->registers.word[Z80_HL];
        c = RAM[cp++];
        for (;;)
        {
            if (c == 077 && RAM[cp] == 0)
            {
                break;
            }

            u8 savedccr = RAM[M_CCR];
            u16 charM = (RAM[M_CCR] & 0x0e) >> 1; // Current M slot

            if (c > 0x3F)
            {
                // advance M slot by one
                RAM[M_CCR] = (RAM[M_CCR] & ~0x0e) | (charM + 1) << 1;
            }

            plotChar (c & 0x3f);

            if (c > 0x3F)
            {
                // restore M slot
                RAM[M_CCR] = savedccr;
            }

            c = RAM[cp++];
        }
        return 1;

    case R_BLOCK:
        // block erase
        cp = state->registers.word[Z80_HL];
        x = ReadRAMW (cp) & 0x1ff;
        y = ReadRAMW (cp + 2) & 0x1ff;
        x2 = ReadRAMW (cp + 4) & 0x1ff;
        y2 = ReadRAMW (cp + 6) & 0x1ff;
        ptermBlockErase (x, y, x2, y2);
        return 1;

    case R_INPX:
        state->registers.word[Z80_HL] = currentX;
        return 1;

    case R_INPY:
        state->registers.word[Z80_HL] = currentY;
        return 1;

    case R_OUTX:
        currentX = state->registers.word[Z80_HL] & 0x01ff;
        return 1;

    case R_OUTY:
        currentY = state->registers.word[Z80_HL] & 0x01ff;
        return 1;

    case R_XMIT:
        // send key in HL
    {
        int k = state->registers.word[Z80_HL];
        u8 temp_hold = mt_ksw;
        if (k != 0x3a)
            mt_ksw = 0;
        ptermSendKey1 (k);
        mt_ksw = temp_hold;
    }
        return 1;

    case R_MODE:
        // set mode from L
        L = state->registers.byte[Z80_L];
        if (L & 1)
        {
            ptermFullErase ();
        }
        modexor = false;
        mode = (L >> 1) & 037;
        return 1;

    case R_STEPX:
        currentX = (currentX + ((RAM[M_DIR] & 2) ? -1 : 1)) & 0777;
        return 1;

    case R_STEPY:
        currentY = (currentY + ((RAM[M_DIR] & 1) ? -1 : 1)) & 0777;
        return 1;

    case R_WE:
        ptermDrawPoint (currentX, currentY);
        return 1;

    case R_DIR:
        L = state->registers.byte[Z80_L];
        RAM[M_DIR] = L & 3;
        return 1;

    case R_INPUT:
        state->registers.word[Z80_HL] = mt_key & 0xffff;
        mt_key = -1;
        return 1;

    case R_SSF:
        // r.ssf
    {
        int n = state->registers.word[Z80_HL];
        int device = (n >> 10) & 0x1f;
        int writ = (n >> 9) & 0x1;
        int inter = (n >> 8) & 0x1;
        int data = n & 0xff;

        m_enab = data;

        // remember devices
        if (writ == 1)
        {
            m_indev = device;
        }
        else
        {
            m_outdev = device;
        }

        if (device == 15 && writ == 1 && inter == 0)
        {
            static const u8 resp[4] = { 0xcc, 0x63, 0x33, 0x40 };
            state->registers.byte[Z80_L] = resp[m_mtincnt & 3];
            m_mtincnt++;
        }

        switch (n)
        {
        case 0x1f00:    // xin 7; means start CWS functions
            cwsmode = 1;
            break;
        case 0x1d00:    // xout 7; means stop CWS functions
            cwsmode = 2;
            break;
        default:
            if (device == 1 && writ == 0)
            {
                m_touchEnabled = (data & 0x20) != 0;
            }
            break;
        }
    }
        return 1;

    case R_CCR:
        trace ("R_CCR %02x", state->registers.byte[Z80_L]);
        RAM[M_CCR] = state->registers.byte[Z80_L];
        return 1;

    case R_EXTOUT:
        return 1;

    case R_EXEC:
        // r.exec
        Mz80Waiter (RESIDENTMSEC);
        m_giveupz80 = true;
        return 1;

    case R_GJOB:
    case R_XJOB:
    case R_RETURN: //obsolete
        return 1;

    case R_CHRCV:
    {
        u16 src = state->registers.word[Z80_DE];
        u16 cnt = state->registers.word[Z80_HL] * 8;
        u16 slot = (state->registers.word[Z80_DE] - ReadRAMW (C2ORIGIN)) / 16;
        memaddr = 16 * slot + ReadRAMW (C2ORIGIN);
        for (int i = 0; i < cnt; i++)
        {
            mode2 (ReadRAMW (src));
            src = src + 2;
        }
    }
        return 1;

    case R_ALARM:
        m_beep = true;
        return 1;

    case R_FCOLOR:      // for standard use with h, l, d
        m_currentFg = MakePix (state->registers.byte[Z80_H],
                               state->registers.byte[Z80_L],
                               state->registers.byte[Z80_D]);
        SetColors (m_currentFg, m_currentBg);
        return 1;

    case R_FCOLOR + 1:      // for mtutor ccode use with de
        m_currentFg = MakePix (RAM[state->registers.word[Z80_DE]],
                               RAM[state->registers.word[Z80_DE] + 1],
                               RAM[state->registers.word[Z80_DE] + 2]);
        SetColors (m_currentFg, m_currentBg);
        return 1;

    case R_FCOLOR + 2:  // for mtutor floating color
        m_currentFg = GetColor (state->registers.word[Z80_HL]);
        SetColors (m_currentFg, m_currentBg);
        return 1;

    case R_BCOLOR:      // for standard use with h, l, d
        m_currentBg = MakePix (state->registers.byte[Z80_H],
                               state->registers.byte[Z80_L],
                               state->registers.byte[Z80_D]);
        SetColors (m_currentFg, m_currentBg);
        return 1;

    case R_BCOLOR + 1:      // for mtutor ccode use with de
        m_currentBg = MakePix (RAM[state->registers.word[Z80_DE]],
                               RAM[state->registers.word[Z80_DE] + 1],
                               RAM[state->registers.word[Z80_DE] + 2]);
        SetColors (m_currentFg, m_currentBg);
        return 1;

    case R_BCOLOR + 2:
        m_currentBg = GetColor (state->registers.word[Z80_HL]);
        SetColors (m_currentFg, m_currentBg);
        return 1;

    case R_PAINT:       // standard
        ptermPaint (state->registers.word[Z80_HL]);
        return 1;

    case R_PAINT + 1:   // mtutor ccode
        ptermPaint (RAM[state->registers.word[Z80_DE]] |
                    (RAM[state->registers.word[Z80_DE] + 1] << 8));
        return 1;

    case R_PAINT + 2:   // mtutor ccode - usefull for debugging
        return 1;

    case R_WAIT16:  // 0x0097
        // for use with mtutor timed -pause-
        usleep (15 * 1000);
        return 1;

    case R_WAIT16 + 1:
        // standard interface with HL
        usleep (state->registers.word[Z80_HL] * 1000);
        return 1;

    case R_WAIT16 + 2:
        // interface with DE for use with mtutor -ccode-
        usleep (ReadRAMW (state->registers.word[Z80_DE]) * 1000);
        return 1;

    case R_DUMMY2:
    case R_DUMMY3:
        return 1;

    default:
        if (state->pc < WORKRAM)
        {
            // Wild jump into ROM resident, quit
            fprintf (stderr, "platod: Z80 wild jump to %04x\n", state->pc);

            // no longer send keys to mtutor; it's dead
            mt_ksw &= 0xfe;
            m_mtutorBoot = false;

            return 2;
        }
        else
        {
            // Plain old RAM PC -- keep executing
            return 0;
        }
    }
}

// Z80 IN instruction.  Floppy (micro-TUTOR) support is not included.
unsigned char PlatoEngine::inputZ80 (unsigned char data)
{
    switch (data)
    {
    case 0x2a:
        return 0x37;
    case 0x2b:
        return 1;
    case 0xaa:  // new in level 4
        return 0x37 + 1;
    case 0xab:  // new in level 4
        return 0x01;
    default:
        return 0;
    }
}

void PlatoEngine::outputZ80 (unsigned char, unsigned char)
{
}
