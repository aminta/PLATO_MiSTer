# PLATO terminal for MiSTer

> **Beta.** Video, keyboard, network and the PLATO protocol are tested on
> real hardware. The mouse (touch panel) and the sound (beep, GSW music)
> are implemented but not tested on hardware yet: reports are welcome.

A hybrid MiSTer core that turns the MiSTer into a PLATO terminal for
[CYBER1](https://www.cyber1.org/) (cyberserv.org), the running PLATO/CYBIS
system with thousands of original lessons and games (Empire, Avatar, dnd,
Moria, Oubliette...), and for [IRATA.ONLINE](https://irata.online/). It is based on the [PTerm](https://www.cyber1.org/pterm.asp)
6.0.4 terminal emulator.

* **FPGA side** (`core/`): shows the 512x512 PLATO screen on HDMI and VGA,
  reads the keyboard and the mouse.
* **ARM side** (`daemon/`, `platod`): network connection and PLATO protocol
  decoding with the PTerm engine, sound. It draws into a frame buffer in
  DDR3 that the FPGA reads.

## Features

* Classic protocol (port 5004, with auto-detection, as PTerm) and ASCII
  protocol (port 8005, with colour).
* PPT micro (Z80) resident, used by programs downloaded to the terminal.
* PLATO keyboard mapped as in PTerm, US or Italian layout.
* Mouse as the PLATO touch panel.
* Sound: `-beep-` and the Gooch Synthetic Woodwind (GSW) 4-voice music
  device (classic protocol), through MiSTer's Linux audio (HDMI and analog).
* Native 512x512 picture, inside a standard VESA 800x600 @ 60 Hz signal on
  analog VGA; aspect ratio and integer scaling options for the scaler.
* Colour schemes (plasma orange, white, green, amber, blue, paper) that can
  be changed at any time.

## Installation

1. Get `PLATO_MiSTer_<date>.zip` from the `releases` folder and copy its
   content to the root of the SD card:

   | File | Destination |
   |------|-------------|
   | `_Other/PLATO_<date>.rbf` | `/media/fat/_Other/` |
   | `PLATO/` (platod, plato_watch.sh, ...) | `/media/fat/PLATO/` |
   | `Scripts/plato_install.sh` | `/media/fat/Scripts/` |

2. On the MiSTer, run **Scripts → plato_install** once. It adds one line
   to `/media/fat/linux/user-startup.sh` (keeping a backup), so that
   `platod` is started whenever the PLATO core is loaded.
3. Load **Other → PLATO**. The MiSTer needs a network connection.

`plato_install uninstall` removes the startup line again.

## Using it

CYBER1 needs an account (name, group and password): see
[cyber1.org](https://www.cyber1.org/) to sign up.

### Keyboard (as in PTerm)

| PLATO key | PC key |
|-----------|--------|
| NEXT | Enter |
| ERASE | Backspace |
| BACK | F8 or Home |
| STOP | F10 or Pause (also Ctrl-S, Alt-S) |
| HELP | F6 |
| DATA | F9 |
| LAB | F7 |
| EDIT | F5 |
| COPY | F1 or F11 |
| ANS | F2 |
| SQUARE | F3 |
| MICRO / FONT | F4 |
| TAB | Tab |
| ASSIGN (arrow) | Esc, Ctrl/Alt+Left |
| SUPER / SUB | Page Up / Page Down |
| multiply / divide | Delete, keypad * / Insert, keypad / |
| Sigma / Delta | Ctrl + keypad + / - |
| arrow keys | cursor keys, numeric keypad (option) |

Shift works with all of them: SHIFT-STOP is Shift+F10, NEXT1 is
Shift+Enter. With the Italian layout AltGr gives `@ # [ ] { } ~` and the
accented letters (è é à ò ù ì ç) are sent as PLATO accent sequences.

### Mouse

The PLATO touch panel is emulated with the mouse: point and click with the
left button (the touch is sent on release, as in PTerm). The pointer is
only shown while the lesson has the touch panel enabled and the mouse has
been moved recently.

### OSD options

* **Server**: CYBER1 with the classic protocol (cyberserv.org port 5004,
  auto-detect), CYBER1 with the ASCII protocol (port 8005) or IRATA.ONLINE
  (irata.online port 8005, ASCII). Changing it reconnects.
* **Colors**: default foreground/background, applied immediately.
* **Numeric keypad**: PLATO arrows or numbers.
* **Keyboard layout**: US or Italian.
* **Sound**: on or off.
* **Aspect ratio** and **Scale** (normal, integer scaling).
* **Credits**: shows the credits page; any key returns to PLATO (the
  session keeps running meanwhile, the sound is muted).
* **Reset**: resets the terminal and reconnects (also the MiSTer USER button).

`/media/fat/PLATO/platod.ini` can set another host and port, which then
take the place of the Server option; see `platod.ini.example`.

### Video settings

The core outputs the original 512x512 screen, so any MiSTer.ini setup
works. Some examples for a `[PLATO]` section:

```ini
; VGA monitor, square pixel perfect 2x through the scaler (1024x1024 @ 60 Hz)
[PLATO]
vga_scaler=1
video_mode=1024,48,112,248,1024,1,3,38,91588
vscale_mode=1

; VGA monitor without the scaler: native 800x600 @ 60 Hz, 512x512 centered
[PLATO]
vga_scaler=0
```

## Troubleshooting

* **Dark blue screen**: the FPGA core runs but `platod` is not running.
  Run `plato_install` again; the log is in `/tmp/platod.log`.
* **"Disconnected"**: check the network; press NEXT (Enter) to reconnect.

## Known issues

* **Moria: garbled 3D maze the first time.** Sometimes the first rooms of
  the Moria maze are drawn with the wrong graphic characters, and the maze
  becomes clean later (for example on the next game start). This is not an
  emulation error: the host draws the room with a loadable character set
  it believes is already in the terminal, without sending it again. The
  original PTerm 6.0.4, fed with the same recorded data, draws exactly the
  same garbled room.

## Building

* ARM program: `make -C daemon arm` (Debian bullseye cross compiler in
  Docker, `tools/Dockerfile.armhf`, same glibc as the MiSTer). `make -C
  daemon` builds a native version; `platod --sim --script FILE` runs it
  without the FPGA and writes screenshots (see `daemon/test/`).
* FPGA core: `tools/build_core.sh`, Quartus 17.0.2 Lite in the
  `theypsilon/quartus-lite-c5:17.0.2-heavy` Docker image. It also works on
  Apple Silicon under Rosetta: synthesis runs on one processor (parallel
  synthesis hangs under emulation), the fitter on eight.
* RTL simulation: `iverilog -g2012 -o tb core/sim/tb.sv core/rtl/*.sv && vvp tb`
* Release files: `tools/package.sh`. `tools/deploy.sh` copies a build to a
  MiSTer over SSH.

The FPGA and ARM sides share DDR3 at physical address 0x30000000; the
layout is described in `daemon/src/shared.h`.

## Licenses

* `daemon/` is derived from PTerm 6.0.4, Copyright (c) 2005-2018 Paul
  Koning, Joe Stanton, Dale Sinder et al., under the PTerm license
  (`daemon/pterm/pterm-license.txt`). It is an altered source version: the
  wxWidgets user interface was replaced by the MiSTer frame buffer,
  keyboard, mouse and ALSA sound. The Z80 emulator is by Lin Ke-Fong.
* `core/` is based on the MiSTer Template and is under GPL-2.0
  (`core/LICENSE`).

## Dedication

This core is dedicated to the unforgettable Federico "Dottor Zonk" Della Zonca,
founding father of "12 Bit - Retrogaming Associazione Culturale" of Trieste.
