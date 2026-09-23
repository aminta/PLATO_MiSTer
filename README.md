# PLATO terminal for MiSTer

A hybrid MiSTer core that turns the MiSTer into a PLATO terminal for
[CYBER1](https://www.cyber1.org/) (cyberserv.org), based on the
[PTerm](https://www.cyber1.org/pterm.asp) 6.0.4 terminal emulator.

* **FPGA side** (`core/`): outputs the 512x512 PLATO screen on HDMI (through
  the MiSTer scaler) and on analog VGA (standard VESA 800x600 @ 60 Hz with
  the picture centered), and passes the keyboard to the ARM side.
* **ARM side** (`daemon/`, `platod`): networking and PLATO protocol
  decoding using the PTerm engine (classic NIU protocol on port 5004 with
  auto-detection, or ASCII protocol on port 8005, including colour and the
  PPT/Z80 micro resident). It draws into a frame buffer in DDR3 that the
  FPGA reads.

Both sides communicate through DDR3 at physical address 0x30000000, see
`daemon/src/shared.h`.

## Installation

Copy to the SD card:

| File | Destination |
|------|-------------|
| `PLATO_<date>.rbf` | `/media/fat/_Other/` |
| `platod`, `plato_watch.sh` | `/media/fat/PLATO/` |

and add this line to `/media/fat/linux/user-startup.sh`:

    [ -x /media/fat/PLATO/plato_watch.sh ] && /media/fat/PLATO/plato_watch.sh &

`tools/deploy.sh` does all of this over SSH. `plato_watch.sh` starts
`platod` when the PLATO core is loaded; `platod` exits when another core is
loaded. The screen stays dark blue until `platod` is running.

## Keyboard (as in PTerm)

| PC key | PLATO key |
|--------|-----------|
| Enter | NEXT (Shift: NEXT1) |
| Backspace | ERASE |
| Home / F8 | BACK |
| F10 / Pause | STOP |
| F6 | HELP |
| F9 | DATA |
| F7 | LAB |
| F5 | EDIT |
| F1 / F11 | COPY |
| F2 | ANS |
| F3 | SQUARE |
| F4 | MICRO / FONT |
| Tab | TAB |
| Esc, Ctrl/Alt+Left | ASSIGN (arrow) |
| Page Up / Page Down | SUPER / SUB |
| Delete, keypad * | multiply |
| Insert, keypad / | divide |
| Ctrl + keypad +/- | Sigma / Delta |
| Arrows, keypad (option) | PLATO arrow keys |

Shift works with all of them (e.g. Shift+F10 = SHIFT-STOP).

## OSD

* **Connection**: port 5004 (classic, auto-detect) or port 8005 (ASCII).
* **Colors**: default foreground/background (applies on reconnect).
* **Numeric keypad**: PLATO arrows or numbers.
* **Reconnect**.

`/media/fat/PLATO/platod.ini` can override the host and port, see
`dist/PLATO/platod.ini.example`.

## Building

* ARM program: `make -C daemon arm` (cross compiler in the `plato-armhf`
  Docker image, `tools/Dockerfile.armhf`). `make -C daemon` builds a native
  version; `platod --sim --script FILE` runs it without the FPGA and writes
  screenshots, see `daemon/test/`.
* FPGA core: Quartus 17.0.2 Lite. On Apple Silicon it runs in the
  `theypsilon/quartus-lite-c5:17.0.2-heavy` Docker image with Rosetta; use
  `NUM_PARALLEL_PROCESSORS 1` (already set), parallel synthesis hangs under
  emulation. `tools/build_core.sh` runs the compilation.
* RTL simulation: `iverilog -g2012 -o tb core/sim/tb.sv core/rtl/plato_video.sv core/rtl/plato_ddr.sv && vvp tb`

## Licenses

* `daemon/` contains code derived from PTerm 6.0.4, Copyright (c) 2005-2018
  Paul Koning, Joe Stanton, Dale Sinder et al., under the PTerm license
  (`daemon/pterm/pterm-license.txt`). This is an altered source version:
  the wxWidgets user interface was replaced by the MiSTer frame buffer.
  The Z80 emulator is by Lin Ke-Fong (free to use).
* `core/` is based on the MiSTer Template (GPL-2.0, `core/LICENSE`).
