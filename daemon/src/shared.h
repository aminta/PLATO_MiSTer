// Memory contract between the PLATO FPGA core and platod (ARM side).
//
// All of this lives in the DDR3 region reserved for FPGA cores
// (physical 0x30000000 and up, not used by Linux).
//
// Frame buffer (written by platod, scanned out by the FPGA):
//   FB_PHYS + (row * 512 + col) * 4, row 0 = top of the PLATO screen.
//   Pixel = 0x00RRGGBB, little endian.  One 64-bit DDR word holds two
//   pixels: bits [31:0] = even column, bits [63:32] = odd column.
//
// Control block (written by the FPGA):
//   CTL_PHYS + 0x000  u32 status    OSD status bits [31:0], see STATUS_*
//   CTL_PHYS + 0x004  u32 head      sequence number of the last event
// Written by platod:
//   CTL_PHYS + 0x008  u32 alive     ALIVE_MAGIC while platod is running;
//                                   until then the FPGA shows a blue screen
//   CTL_PHYS + 0x00c  u32 flags     FLAG_* bits for the FPGA
//
//   CTL_PHYS + 0x100  event ring, EVT_SLOTS entries of 8 bytes:
//                       u32 event   EVT_* type in [31:30], payload below
//                       u32 seq     sequence number of this entry
//   Entry n is stored in slot (n % EVT_SLOTS).  The FPGA writes the entry
//   first and then the header, so every seq <= head is valid.

#ifndef PLATO_SHARED_H
#define PLATO_SHARED_H

#define FB_PHYS         0x30000000u
#define FB_SIZE         (512 * 512 * 4)
#define CTL_PHYS        0x30100000u
#define CTL_SIZE        0x1000
#define CTL_STATUS      0x000
#define CTL_HEAD        0x004
#define CTL_ALIVE       0x008
#define CTL_FLAGS       0x00c
#define CTL_RING        0x100
#define FLAG_TOUCH      0x01            // touch panel on: show the pointer
#define ALIVE_MAGIC     0x504C4154u     // "PLAT"
#define EVT_SLOTS       256

#define EVT_TYPE(e)     (((e) >> 30) & 3)
#define EVT_KEY         0       // payload [10:0] = MiSTer ps2_key
#define EVT_MOUSE       1       // left button change: [19] pressed,
                                // [17:9] x, [8:0] row (0 = top)

// OSD options (must match CONF_STR in PLATO.sv)
#define STATUS_PORT(s)      (((s) >> 1) & 1)    // 0 = 5004 auto, 1 = 8005 ASCII
#define STATUS_COLOR(s)     (((s) >> 2) & 7)    // colour scheme
#define STATUS_NUMPAD(s)    (((s) >> 5) & 1)    // 0 = arrows, 1 = numbers
#define STATUS_KBD(s)       (((s) >> 8) & 1)    // 0 = US, 1 = Italian
#define STATUS_BEEP(s)      (((s) >> 9) & 1)    // 0 = on, 1 = off
#define STATUS_NOPAUSE(s)   (((s) >> 10) & 1)   // 0 = credits while OSD open
#define STATUS_OSD(s)       (((s) >> 27) & 1)   // OSD is open
#define STATUS_RECONN(s)    (((s) >> 28) & 15)  // counts OSD "Reset" requests

#endif
