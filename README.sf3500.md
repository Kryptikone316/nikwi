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

The original game has no music. The port can play background music: one track
per level (`data/audio/bgm0` to `bgm7`) and one for the title and menu screens
(`bgm8`). Each is either `bgmN.ogg` (Ogg Vorbis, mono or stereo, 48000 Hz) or
`bgmN.raw` (headerless raw PCM, mono, 48000 Hz, signed 16-bit little endian);
the `.ogg` is used if both exist. An Ogg track is decoded in the background
while it plays (the log prints how long each one took). The level tracks that
exist are used in turn, so one to eight tracks all work, and missing files mean
no music.

The tracks are not part of this repository. A pack of six (five level tracks
and a menu track, arranged specifically for this title and released under
CC0) is a separate download: see the releases of this repository.

To make a track from any audio file:

```sh
ffmpeg -i song.mp3 -ac 1 -ar 48000 -c:a libvorbis -q:a 4 bgm0.ogg
```

`src/hw/stb_vorbis.h` is Sean Barrett's public domain Ogg Vorbis decoder.
