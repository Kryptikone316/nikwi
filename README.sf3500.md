# Nikwi Deluxe for the Datafrog SF3500

This branch adds a port of Nikwi Deluxe to the Datafrog SF3500 (MIPS32r2
handheld). The game, its scripts and its images are Kostas Michalopoulos's
and are unchanged; see `license.txt` (zlib for `src/`, WTFPL for everything
else).

## What was added

- `src/hw/`: display (RGB565 present), audio (ring plus thread) and gamepad
  wrappers around the device driver. The driver named in `/tmp/tfdevice.env`
  is loaded, with the SF3500 driver as fallback (`tfenv.h`). `hwdsp` is a
  small bass/treble/gain filter configured from `hwdsp.txt`; `bitfont` and
  `charpicker` draw and enter the password with the gamepad.
- `src/nikwi/`: SF3500-only changes (`#ifdef SF3500`) that route video, audio
  and input through those wrappers, and optional background music.
- `Makefile`: `make SF3500=1 ...` builds with those files.

## Building

```sh
TC=/path/to/toolchain/host   # has bin/mips-mti-linux-gnu-{gcc,g++} and the target sysroot with SDL 1.2
make SF3500=1 CC="$TC/bin/mips-mti-linux-gnu-gcc" CXX="$TC/bin/mips-mti-linux-gnu-g++" game
```

The program needs `data/` next to it. The author's `makedata.sh` creates
`data/tiles` and `data/objects` from `images/` with `src/tools/bmp2ut`
(a host SDL 1.2 program, run as in `makedata.sh`).

## Background music

The port can play one of nine tracks per level from `data/audio/bgm0.raw` to
`bgm8.raw`: headerless raw PCM, mono, 48000 Hz, signed 16-bit little endian.
They are not part of this repository. The original game has no music, and if
the files are missing the game simply plays none.
