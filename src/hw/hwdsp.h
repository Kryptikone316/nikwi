/* Lightweight in-process audio DSP stage - gain + simple shelf EQ applied to
 * the already-resampled 48kHz stereo S16 buffer, right before hwaudio_write().
 * Deliberately NOT a separate process: this device is single-core, and
 * TinySTG's own audiotest investigation found that extra scheduled entities
 * contending for the same core around the audio path cause real, measured
 * glitches. This runs inline on the same producer thread/call that already
 * builds the PCM buffer - zero extra threads, zero IPC, cheap per-sample
 * math only (two one-pole filters + a few multiplies).
 *
 * Tunable via a plain text file instead of a rebuild - see hwdsp_init().
 *
 * Copied from TinySTG's own copy (the original implementation) with ONE
 * addition: an explicit `enabled=` toggle (see hwdsp_init()'s doc comment)
 * so a port can turn the whole stage off without having to remember that
 * all-unity-gains already happens to short-circuit to a no-op - a real,
 * obvious on/off switch instead of an implicit one. Copy this file pair
 * in verbatim for any future SF3500 port, same convention as hwdisp.c/
 * hwaudio.c/joykey.c/bitfont.c/charpicker.c. */
#ifndef SF3000_HWDSP_H
#define SF3000_HWDSP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Reads config_path (format below) if present; any missing/unparsable key
 * keeps its default, so a missing file is the same as "enabled, all
 * unity gains" (no audible change) - safe to call even if the file was
 * never pushed.
 *
 *   enabled=1          # 0 disables the whole stage (hwdsp_process() then
 *                       # returns immediately, doing no work at all) -
 *                       # default 1 if the key is absent, for the same
 *                       # "missing file = safe" reasoning as the gains.
 *   master_gain=1.0    # overall linear gain, 1.0 = unity
 *   bass_gain=1.0      # low-shelf gain below ~400Hz, 1.0 = unity
 *   treble_gain=1.0    # high-shelf gain above ~400Hz, 1.0 = unity
 *
 * Lines starting with '#' or that don't parse are ignored. */
void hwdsp_init(const char *config_path);

/* In-place process of an interleaved stereo S16 buffer, `frames` frames
 * (2 * frames shorts). Call once per batch, right before hwaudio_write().
 * Returns immediately, doing nothing, if `enabled=0` was set (or if every
 * gain is exactly unity, the original no-op shortcut, kept for whenever
 * the stage is enabled but genuinely has nothing to do). */
void hwdsp_process(short *pcm, int frames);

#ifdef __cplusplus
}
#endif

#endif
