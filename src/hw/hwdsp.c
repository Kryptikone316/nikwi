/* See hwdsp.h for the full design note. Copied from TinySTG's own hwdsp.c
 * (the original implementation) with ONE addition: the `enabled` toggle -
 * everything else (the one-pole shelf filter math, the per-channel
 * continuity state, the config-file format) is unchanged. */
#include "hwdsp.h"
#include "hwaudio.h" /* HWAUDIO_RATE */

#include <stdio.h>
#include <string.h>
#include <math.h>

#define SHELF_CORNER_HZ 400.0

static int g_enabled = 1;
static float g_master_gain = 1.0f;
static float g_bass_gain = 1.0f;
static float g_treble_gain = 1.0f;
static float g_alpha = 0.0f; /* one-pole lowpass coefficient, set from SHELF_CORNER_HZ */

/* Per-channel lowpass filter state (stereo) - persists across calls so the
 * filter is continuous, same reasoning as SoundStream's own prev_sample
 * continuity fix: processing each call's buffer in isolation with no
 * carried state would put an audible step at every call boundary. */
static float g_lp_left = 0.0f;
static float g_lp_right = 0.0f;

void hwdsp_init(const char *config_path) {
    g_alpha = (float)(1.0 - exp(-2.0 * M_PI * SHELF_CORNER_HZ / HWAUDIO_RATE));

    FILE *f = config_path ? fopen(config_path, "r") : NULL;
    if (!f) {
        fprintf(stderr, "hwdsp: no config at %s, using enabled/unity defaults\n", config_path ? config_path : "(null)");
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[64];
        float value;
        if (sscanf(line, "%63[^=]=%f", key, &value) != 2) continue;
        if (strcmp(key, "enabled") == 0) g_enabled = (value != 0.0f);
        else if (strcmp(key, "master_gain") == 0) g_master_gain = value;
        else if (strcmp(key, "bass_gain") == 0) g_bass_gain = value;
        else if (strcmp(key, "treble_gain") == 0) g_treble_gain = value;
    }
    fclose(f);

    fprintf(stderr, "hwdsp: loaded %s - enabled=%d master=%.2f bass=%.2f treble=%.2f\n",
            config_path, g_enabled, g_master_gain, g_bass_gain, g_treble_gain);
}

static short clamp_s16(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (short)v;
}

static float process_sample(float x, float *lp_state) {
    /* One-pole lowpass gives the "bass" band; the remainder (x - lowpass)
     * is the "treble" band. Mixing each back in proportion to its gain
     * is a standard cheap low/high shelf - no biquad coefficients needed. */
    *lp_state += g_alpha * (x - *lp_state);
    float lp = *lp_state;
    float hp = x - lp;
    float y = lp * g_bass_gain + hp * g_treble_gain;
    return y * g_master_gain;
}

void hwdsp_process(short *pcm, int frames) {
    if (!g_enabled) {
        return;
    }
    /* Skip the work entirely when every knob is at unity - the common case
     * if no config file was pushed (or it only sets some keys). */
    if (g_master_gain == 1.0f && g_bass_gain == 1.0f && g_treble_gain == 1.0f) {
        return;
    }
    int i;
    for (i = 0; i < frames; i++) {
        pcm[i * 2]     = clamp_s16(process_sample((float)pcm[i * 2], &g_lp_left));
        pcm[i * 2 + 1] = clamp_s16(process_sample((float)pcm[i * 2 + 1], &g_lp_right));
    }
}
