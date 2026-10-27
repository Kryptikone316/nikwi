/*
 * Nikwi Deluxe
 * Copyright (C) 2005-2012 Kostas Michalopoulos
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Kostas Michalopoulos <badsector@runtimelegend.com>
 */

/*
** Nikwi Engine - Sound
*/

#include "nikwi.h"

#ifdef SF3500
#include <pthread.h>
#include <time.h>
#include "hw/hwaudio.h"
#include "hw/hwdsp.h"
/* Relative to the working directory: run.sh cd's into the port folder
 * before launching, so this follows the port wherever it is installed
 * (the old absolute /mnt/sdcard/ports/nikwi path went stale when ports
 * moved to /mnt/sdcard/roms/ports). */
#define DSP_CONFIG_PATH "hwdsp.txt"
#endif

bool	noSound = false;

static Sample		*sample = NULL;
static SampleQueueEntry	*first = NULL, *last = NULL;
static int		samplesToPlay = 0;

#ifdef SF3500

/* SF3500 port: SDL_OpenAudio's generic callback thread is never used on
 * this device (no usable SDL audio backend - see hwaudio.c). This mixer
 * keeps the original engine's own simple additive-voice-sum logic
 * (findSample/playSample/the sample list are all unchanged) but drives it
 * from a dedicated pthread paced the same way hwaudio.c's own consumer
 * thread is (clock_nanosleep/TIMER_ABSTIME, not a relative sleep), mixing one HWAUDIO_CHUNK (480 frames,
 * 10ms @ 48000Hz) batch per tick and writing it straight to
 * hwaudio_write(). Every voice is resampled from its native 22050Hz mono
 * source to 48000Hz on the fly (linear interpolation, computed in double
 * throughout with only the final mixed sample cast to short/int - never an
 * intermediate narrow cast, the exact bug class that corrupted Atomiks'
 * own first resampler attempt).
 *
 * hwaudio_write() returns the frame count it ACTUALLY accepted (it may
 * silently take less if its ring is near its soft cap) - every voice's
 * source position is only committed by that accepted amount, not the full
 * requested HWAUDIO_CHUNK, per the Bombermaaan lesson: advancing a voice's
 * position by more than what was actually written causes it to silently
 * skip ahead of what's audible. Since every voice shares the same fixed
 * 22050->48000 rate ratio and the same per-tick batch, one shared
 * "accepted" value correctly rescales every voice's position commit in a
 * single pass below - no per-voice rollback math needed. */

#define SND_SRC_RATE	22050
#define TICKFRAMES	HWAUDIO_CHUNK

static pthread_mutex_t	mix_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_t	mixer_thread;
static volatile int	mixer_running = 0;

/* Background music - added 2026-10-13. Replicates the SDL_mixer-based
 * PSP port's own playMusic(level%8)/playMusic(8) trigger scheme (see
 * game.cpp's 3 call sites and main.cpp's title-screen call), but instead
 * of embedding a MOD/XM tracker player on-device (a disproportionate
 * amount of new, hard-to-debug-on-device complexity for a single-core, no-
 * GPU device that already needed 8+ real-hardware rounds to get its own
 * procedural-SFX mixer right), each of the PSP port's 9 tracker modules is
 * pre-rendered ONCE on the host to plain looping mono PCM (with
 * ffmpeg's libopenmpt demuxer)
 * and played back as ONE MORE VOICE through this file's already-proven
 * mixer - same softclip, same honest hwaudio_write()-return-value commit
 * discipline as every SFX voice already gets. Only ONE track is ever
 * resident in memory at a time (loaded fresh on each playMusic() call, old
 * buffer freed first) - this matches the original SDL_mixer-based
 * implementation's own one-at-a-time Mix_LoadMUS()/Mix_FreeMusic()
 * behavior anyway. Looping is a pure buffer-wraparound (modulo on a
 * fixed-length PCM buffer) - there is no decoder state to desync, which
 * was the ENTIRE source of TinySTG's multi-round ADPCM loop-boundary
 * saga; this design gets that correctness for free.
 *
 * Round 6 real-hardware result (2026-10-13): music played but sounded
 * "tinny... with a fair amount of static." Root-caused by a host-side
 * round-trip test (upsample this file's own 22050->48000 linear-
 * interpolation code, then downsample back via a known-good resampler,
 * diff against the original) that PROVED the interpolation math itself
 * was correct (correlation 0.9999, same signature as every other already-
 * validated resampler on this project) - not a stride/indexing bug. Also
 * confirmed, from the actual PCM data, that music alone (even after
 * MUSIC_GAIN) never reached anywhere near softclip's knee on any of the 9
 * tracks - ruled out softclip/gain interaction too. What's left, and well
 * supported by ordinary DSP reasoning once the other two were eliminated:
 * 2-point linear interpolation is objectively lower quality for tracker
 * music's richer high-frequency content than it is for this engine's own
 * smooth, synthetic `.ssfx` waveforms (where the same interpolation is
 * provably fine, already confirmed clean on real hardware) - audible as
 * exactly "tinny... static" on rich content, at only a 2.18x upsample
 * ratio. Rather than writing and re-validating a fancier interpolator,
 * MUSIC_RATE is now 48000 - i.e. the tracks are pre-rendered at the
 * hardware's own fixed output rate directly from libopenmpt (demuxer-side
 * `-sample_rate 48000 -layout mono`, not a generic post-decode resample),
 * so NO runtime interpolation happens for music at all - `step` is exactly
 * 1.0, each output sample is one real source sample, period. This is the
 * simplest fix that actually eliminates the artifact's source rather than
 * papering over it, at the cost of roughly doubling the BGM asset size on
 * disk (~55MB -> ~120MB, still trivial for SD storage; only one track is
 * ever resident in RAM at a time regardless). SFX stays at 22050Hz with
 * its existing, already-proven-clean linear interpolation - unchanged,
 * since nothing about it was ever implicated. */
#define MUSIC_RATE	48000	/* == HWAUDIO_RATE: zero runtime resampling */
/* Music is attenuated relative to SFX so it doesn't drown them out, same
 * spirit as a typical game's separate music/SFX volume sliders.
 *
 * Round 6/6b real-hardware result: music was "drowned out... like a
 * whistle in a windstorm" against SFX at the original 0.65 guess - and
 * round 6b's own clipcheck data (re-run here against the real, final
 * 48kHz-native-rendered PCM, all 9 tracks, not just one) already showed
 * WHY that guess was too conservative: at gain=1.0 (i.e. no attenuation
 * at all), the single loudest real sample across all 9 tracks reaches
 * only 91.2% of CLIP_KNEE (track 8) - comfortable margin before softclip
 * even starts engaging, let alone hitting a hard wall, and every other
 * track's own peak sits well below that too (70-81% range for the next
 * loudest few). SFX itself is never gain-reduced at all (full source
 * amplitude straight into the mix) - so 1.0 here simply means music and
 * SFX are finally treated symmetrically (neither artificially boosted
 * nor attenuated relative to its own source), letting the shared
 * softclip() handle the rare moment both stack high at once, exactly
 * what it's there for. This is a real headroom-math result, not a second
 * guess - re-verify with clipcheck2.c against the actual shipped PCM
 * files before changing this again. */
#define MUSIC_GAIN	1.0

static short	*musicWave = NULL;
static long	musicLen = 0;
static long	musicPos = 0;
static long	musicLoopStart = 0;

/* Per-track real loop point, in frames at MUSIC_RATE (48000Hz) - NOT
 * assumed to be 0 (file/order start) for every track. Found by actually
 * tracing each real module's own order/row sequence with libopenmpt's C
 * API (a small host-side tool, looppoint.c) until the first (order,row)
 * repeat - the same "has this song looped yet" detection libopenmpt's own
 * duration estimate uses - and recording what order playback resumes at
 * afterward. 7 of 9 tracks genuinely do loop back to the start (0); two
 * do not: track 0 has a ~19.2s intro (order 0) before settling into its
 * real loop at order 1, and track 3 has an ~8.0s intro (orders 0-1)
 * before its real loop at order 2. Naively wrapping either of those two
 * back to frame 0 would replay the intro every loop, audible as a
 * structural seam each time around - this table is what prevents that. */
static const long musicLoopStartFrame[9] = {
	921856,	/* track 0: real loop starts at order 1 (~19.205s) */
	0,
	0,
	384256,	/* track 3: real loop starts at order 2 (~8.005s) */
	0,
	0,
	0,
	0,
	0
};

/* Soft-knee limiter - same formula/knee as Atomiks' drv_snd.c softclip(),
 * reused verbatim rather than re-derived: below CLIP_KNEE (~82% of full
 * scale) audio passes through completely untouched (the common case,
 * zero added cost), only genuine peaks above it get a smooth ease-out
 * compression instead of a hard wall. No transcendental math (no tanh/exp)
 * so it stays cheap enough for a single-core device. Replaces the
 * original engine's hard clamp (`if (sample>32760) sample=32760;`), the
 * same textbook "scratchy at peaks" bug Atomiks hit and fixed this way. */
#define CLIP_KNEE 27000
static short softclip(int s)
{
	int	sign = (s < 0) ? -1 : 1;
	int	a = (s < 0) ? -s : s;
	double	over, compressed;
	int	out;

	if (a <= CLIP_KNEE)
		return (short)s;
	if (a >= 32767)
		return (short)(sign * 32767);

	over = (double)(a - CLIP_KNEE) / (double)(32767 - CLIP_KNEE);
	compressed = 1.0 - (1.0 - over) * (1.0 - over);
	out = CLIP_KNEE + (int)(compressed * (32767 - CLIP_KNEE));
	return (short)(sign * out);
}

static void removeSampleEntry(SampleQueueEntry *e)
{
	samplesToPlay--;
	if (e->prev)
		e->prev->next = e->next;
	else
		first = e->next;
	if (e->next)
		e->next->prev = e->prev;
	else
		last = e->prev;

	delete e;
}

static void generateTick()
{
	int	mixbuf[TICKFRAMES];
	short	outbuf[TICKFRAMES * 2];
	int	i;
	const double	step = (double)SND_SRC_RATE / (double)HWAUDIO_RATE;

	memset(mixbuf, 0, sizeof(mixbuf));

	pthread_mutex_lock(&mix_mtx);

	/* Snapshot of "the last voice that was actually mixed into mixbuf
	 * this tick", taken while still locked - needed because mix_mtx is
	 * released below, before the slow softclip/hwdsp/hwaudio_write work,
	 * so a NEW voice can get appended by playSample() (main/game thread)
	 * during that gap. Without this, the commit loop further down (which
	 * re-acquires the lock afterward) would walk into that brand-new
	 * entry too and advance its position as if it had already been
	 * mixed this tick - it wasn't, since mixing already happened above,
	 * before the voice existed. Capped at `last` as it stood right here;
	 * the commit loop stops immediately after this entry, never
	 * continuing into anything appended later. NULL is a valid value
	 * (no voices were active this tick). */
	SampleQueueEntry	*commitUpTo = last;

	for (SampleQueueEntry *e=first;e;e = e->next)
	{
		double	begin = e->srcpos;
		long	len = (long)e->sample->length;

		for (i=0;i<TICKFRAMES;i++)
		{
			double	sp = begin + (double)i * step;
			long	idx = (long)sp;

			if (idx >= len)
				break;
			if (idx + 1 >= len)
			{
				mixbuf[i] += e->sample->wave[idx];
			}
			else
			{
				double	frac = sp - (double)idx;
				double	s0 = (double)e->sample->wave[idx];
				double	s1 = (double)e->sample->wave[idx+1];
				mixbuf[i] += (int)(s0 + (s1 - s0) * frac);
			}
		}
	}

	/* Background music - MUSIC_RATE == HWAUDIO_RATE (both 48000), so
	 * there is no resampling here at all: one real source sample per
	 * output sample, plus the MUSIC_GAIN attenuation before summing into
	 * the shared mixbuf (so it goes through the exact same final
	 * softclip as everything else, not a separate limiter).
	 *
	 * Looping wraps to musicLoopStart, NOT unconditionally to 0 - see
	 * musicLoopStartFrame[]'s own comment above for why (2 of the 9
	 * tracks have a real intro before their actual loop point). The
	 * `rawIdx >= len` branch below is the one spot a naive straight
	 * `% len` would silently do the wrong thing for those two tracks,
	 * including the rare case where a single 10ms tick's own window
	 * straddles the exact wrap boundary mid-tick. */
	long	musicBegin = musicPos;
	/* Same unlock-window race as commitUpTo above, for the single global
	 * music voice instead of a list: if playMusic() (main/game thread,
	 * e.g. a level transition) swaps musicWave out during the unlocked
	 * gap below, the commit section must NOT then apply this tick's
	 * now-stale musicBegin/length math on top of the NEW track's own
	 * freshly-reset musicPos. Snapshotting the pointer here and
	 * re-checking it still matches before committing is what detects
	 * that. */
	short	*mixedMusicWave = musicWave;
	if (musicWave && musicLen > 0)
	{
		long	len = musicLen;
		long	loopStart = musicLoopStart;
		long	loopLen = len - loopStart;
		if (loopLen <= 0)
			loopLen = len;

		for (i=0;i<TICKFRAMES;i++)
		{
			long	rawIdx = musicBegin + i;
			long	idx = (rawIdx < len) ? rawIdx
				: loopStart + ((rawIdx - len) % loopLen);
			mixbuf[i] += (int)((double)musicWave[idx] * MUSIC_GAIN);
		}
	}

	/* mix_mtx is released HERE, before softclip/hwdsp/hwaudio_write -
	 * none of those three touch shared state (mixbuf/outbuf are this
	 * call's own locals; hwdsp's filter state and hwaudio's ring have
	 * their own, separate internal synchronization/single-caller
	 * discipline). Added 2026-10-14 after a real-hardware report of SFX
	 * (jump/pickup) playing ~2 seconds late, "catching up" - round 8
	 * added hwdsp_process() INSIDE this same lock, extending how long
	 * playSample() (called from the main/game thread on a jump/pickup)
	 * could be blocked waiting for mix_mtx every single tick, forever,
	 * for the rest of the session. This is a real, unconditional,
	 * zero-downside latency fix regardless of whether hwdsp's own
	 * absolute per-sample cost turns out to be the full story - there
	 * was never a correctness reason for softclip/hwdsp/hwaudio_write to
	 * run while this lock was held. See the periodic hwaudio_get_stats()
	 * log below for the real ring-fill data a future run needs to
	 * confirm whether this alone fully explains the reported delay. */
	pthread_mutex_unlock(&mix_mtx);

	for (i=0;i<TICKFRAMES;i++)
	{
		short	s = softclip(mixbuf[i]);
		outbuf[i*2] = s;
		outbuf[i*2+1] = s;
	}

	/* In-process bass/treble/master DSP stage (hw/hwdsp.h) - runs inline
	 * on this same mixer thread, right before hwaudio_write(), per the
	 * shared primitive's own design (zero extra threads/IPC). Config-
	 * file-tunable without a rebuild; see hwdsp.txt deployed alongside
	 * this binary. The starting gains were picked (grounded in the real
	 * CS4344 DAC/speaker hardware research, not yet listening-tested). */
	hwdsp_process(outbuf, TICKFRAMES);

	int	accepted = hwaudio_write(outbuf, TICKFRAMES);
	double	advanced = (double)accepted * step;

	/* Diagnostic: periodic ring-fill/underrun/dropped snapshot via the
	 * existing (previously unused in this port) hwaudio_get_stats() API -
	 * the same real-measurement approach TinySTG's audiotest saga used to
	 * find its own producer/consumer backlog bugs, reused here instead
	 * of building new instrumentation from scratch. Once every ~2
	 * seconds (200 ticks), cheap enough to leave in permanently - if
	 * `fill` is seen sitting consistently high (not just transiently)
	 * across a run.log, that's the real backlog this round's lock-scope
	 * fix was meant to address; if it stays low/flat, the fix already
	 * worked and the delay was the mutex contention alone. */
	static int	statsTickCounter = 0;
	if (++statsTickCounter >= 200)
	{
		unsigned	underruns, dropped, fill;
		statsTickCounter = 0;
		hwaudio_get_stats(&underruns, &dropped, &fill);
		fprintf(stderr, "sound: ring underruns=%u dropped=%u fill=%u\n",
			underruns, dropped, fill);
	}

	pthread_mutex_lock(&mix_mtx);

	SampleQueueEntry	*next;
	for (SampleQueueEntry *e=first;e;e = next)
	{
		bool	isLast = (e == commitUpTo);

		next = e->next;
		e->srcpos += advanced;
		if ((long)e->srcpos >= (long)e->sample->length)
			removeSampleEntry(e);
		if (isLast)
			break;	/* don't walk into anything appended during the unlocked gap above */
	}

	if (musicWave == mixedMusicWave && musicWave && musicLen > 0)
	{
		/* `accepted` is already in output (== music source) frames at
		 * this rate, so the commit is a plain integer add - no
		 * decoder state to desync (unlike TinySTG's ADPCM BGM), and no
		 * fractional position to carry since there's no resampling.
		 * Same loop-start-aware wrap as the mixing loop above (NOT a
		 * plain `% musicLen` - see musicLoopStartFrame[]'s comment). */
		long	rawPos = musicBegin + accepted;
		if (rawPos < musicLen)
			musicPos = rawPos;
		else
		{
			long	loopLen = musicLen - musicLoopStart;
			if (loopLen <= 0)
				loopLen = musicLen;
			musicPos = musicLoopStart + ((rawPos - musicLen) % loopLen);
		}
	}

	pthread_mutex_unlock(&mix_mtx);
}

/* Matches the PSP port's own playMusic(level%8) / playMusic(8) scheme -
 * track is 0-8, pre-rendered PCM files named to match
 * (data/audio/bgm0.raw .. bgm8.raw). Only one track is ever resident in
 * memory - the previous one (if any) is freed before the new one loads,
 * same one-at-a-time behavior the original Mix_LoadMUS()/Mix_FreeMusic()
 * calls had. Safe to call redundantly with the same track already
 * playing (matches upstream: it just restarts from the beginning, same
 * as upstream's own stopMusic()-then-reload-every-call behavior). */
void playMusic(int track)
{
	char	path[32];
	uint	dataLen;
	short	*wave;

	if (noSound || track < 0 || track > 8)
		return;

	sprintf(path, "data/audio/bgm%d.raw", track);
	wave = (short*)getData(path, dataLen);
	if (!wave)
		return;

	pthread_mutex_lock(&mix_mtx);
	if (musicWave)
		free(musicWave);
	musicWave = wave;
	musicLen = dataLen / 2;
	musicPos = 0;
	musicLoopStart = musicLoopStartFrame[track];
	pthread_mutex_unlock(&mix_mtx);
}

static void *mixerThreadFn(void*)
{
	struct timespec	next_wake;
	long	period_ns = (long)((long long)TICKFRAMES * 1000000000LL / HWAUDIO_RATE);

	clock_gettime(CLOCK_MONOTONIC, &next_wake);
	while (mixer_running)
	{
		next_wake.tv_nsec += period_ns;
		if (next_wake.tv_nsec >= 1000000000L)
		{
			next_wake.tv_nsec -= 1000000000L;
			next_wake.tv_sec += 1;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wake, NULL);
		generateTick();
	}
	return NULL;
}

void initSound()
{
	BadCFGNode	*samples = loadCFG("data/samples.txt");
	if (!samples)
		fatal("Cannot load audio samples configuration file");

	for (BadCFGNode *smp=samples->child;smp;smp=smp->next)
	{
		String	file;
		uint	code = makeCode(getBadCFGValue(smp, "code", "????"));

		file = (String)malloc(strlen(smp->value) + 17);
		sprintf(file, "data/audio/%s.ssfx", smp->value);

		uint	dataLen;
		char	*ssfx = (char*)getData(file, dataLen);
		free(file);

		if (!ssfx)
			continue;

		Sample	*sam = new Sample;
		sam->code = code;
		if (!ssfxLoadEffectFromMemory(ssfx, &sam->wave, &sam->length))
		{
			delete sam;
			continue;
		}
		free(ssfx);

		sam->next = sample;
		sample = sam;
	}

	destroyBadCFGNode(samples);

	hwdsp_init(DSP_CONFIG_PATH);

	if (hwaudio_init() != 0)
	{
		noSound = true;
	}
	else
	{
		mixer_running = 1;
		if (pthread_create(&mixer_thread, NULL, mixerThreadFn, NULL) != 0)
		{
			mixer_running = 0;
			noSound = true;
		}
	}

	playSample(findSample(makeCode("lgbl")));
}

void shutdownSound()
{
	Sample	*next;

	if (mixer_running)
	{
		mixer_running = 0;
		pthread_join(mixer_thread, NULL);
	}
	if (hwaudio_active())
		hwaudio_deinit();

	if (musicWave)
	{
		free(musicWave);
		musicWave = NULL;
	}

	while (sample)
	{
		next = sample->next;
		free(sample->wave);
		delete sample;
		sample = next;
	}
}

Sample *findSample(uint code)
{
	for (Sample *smp=sample;smp;smp = smp->next)
		if (smp->code == code)
			return smp;
	return NULL;
}

void playSample(Sample *sample)
{
	SampleQueueEntry	*e;

	if (!sample || samplesToPlay >= 48 || noSound)
		return;

	for (e=first;e;e = e->next)
		if (e->sample == sample && e->srcpos < 128.0)
			return;

	pthread_mutex_lock(&mix_mtx);
	e = new SampleQueueEntry;
	e->sample = sample;
	e->srcpos = 0.0;
	e->prev = last;
	e->next = NULL;
	if (first)
		last->next = e;
	else
		first = e;
	last = e;

	samplesToPlay++;

	pthread_mutex_unlock(&mix_mtx);
}

#else /* !SF3500 - original engine code, untouched */

static bool		soundIsShuttingDown = false;

static void removeSampleEntry(SampleQueueEntry *e)
{
	samplesToPlay--;
	if (e->prev)
		e->prev->next = e->next;
	else
		first = e->next;
	if (e->next)
		e->next->prev = e->prev;
	else
		last = e->prev;

	delete e;
}

static void sdlCallback(void*, Uint8 *stream, int len)
{
	short			*buff = (short*)stream;
	SampleQueueEntry	*next;

	len /= 2;
	if (!first || soundIsShuttingDown)
	{
		memset(stream, 0, len);
		return;
	}

	for (int i=0;i<len;i++)
	{
		int	sample = 0;

		for (SampleQueueEntry *e=first;e;e = next)
		{
			next = e->next;
			sample += e->sample->wave[e->pos++];

			if (e->pos >= e->sample->length)
				removeSampleEntry(e);
		}

		if (sample > 32760)
			sample = 32760;
		else if (sample < -32760)
			sample = -32760;


		buff[i] = sample;
	}
}

void initSound()
{
	BadCFGNode	*samples = loadCFG("data/samples.txt");
	if (!samples)
		fatal("Cannot load audio samples configuration file");

	for (BadCFGNode *smp=samples->child;smp;smp=smp->next)
	{
		String	file;
		uint	code = makeCode(getBadCFGValue(smp, "code", "????"));

		file = (String)malloc(strlen(smp->value) + 17);
		sprintf(file, "data/audio/%s.ssfx", smp->value);

		uint	dataLen;
		char	*ssfx = (char*)getData(file, dataLen);
		free(file);

		if (!ssfx)
			continue;

		Sample	*sam = new Sample;
		sam->code = code;
		if (!ssfxLoadEffectFromMemory(ssfx, &sam->wave, &sam->length))
		{
			delete sam;
			continue;
		}
		free(ssfx);

		sam->next = sample;
		sample = sam;
	}

	destroyBadCFGNode(samples);


	SDL_AudioSpec	spec;
        spec.freq = 22050;
        spec.channels = 1;
        spec.format = AUDIO_S16;
        spec.samples = 1024;
        spec.callback = sdlCallback;
        if (SDL_OpenAudio(&spec, NULL) < 0)
                noSound = true;
		//fprintf(stderr, "SNDError: %s\n", SDL_GetError());
	else
		SDL_PauseAudio(0);

	playSample(findSample(makeCode("lgbl")));
}

void shutdownSound()
{
	Sample	*next;
	SDL_LockAudio();
	soundIsShuttingDown = true;
	while (sample)
	{
		next = sample->next;
		free(sample->wave);
		delete sample;
		sample = next;
	}
	SDL_UnlockAudio();
}

Sample *findSample(uint code)
{
	for (Sample *smp=sample;smp;smp = smp->next)
		if (smp->code == code)
			return smp;
	return NULL;
}

void playSample(Sample *sample)
{
	SampleQueueEntry	*e;

	if (!sample || samplesToPlay >= 48 || noSound)
		return;

	for (e=first;e;e = e->next)
		if (e->sample == sample && e->pos < 128)
			return;

	SDL_LockAudio();
	e = new SampleQueueEntry;
	e->sample = sample;
	e->pos = 0;
	e->prev = last;
	e->next = NULL;
	if (first)
		last->next = e;
	else
		first = e;
	last = e;

	samplesToPlay++;

	SDL_UnlockAudio();
}

#endif /* SF3500 */

