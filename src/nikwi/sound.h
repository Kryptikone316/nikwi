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

#ifndef __SOUND_H_INCLUDED__
#define __SOUND_H_INCLUDED__

struct Sample
{
	uint		code;
	short		*wave;
	uint		length;
	
	Sample		*next;
};

struct SampleQueueEntry
{
	Sample			*sample;
#ifdef SF3500
	double			srcpos;	/* fractional position, source-rate (22050Hz) frames */
#else
	uint			pos;
#endif
	SampleQueueEntry	*prev;
	SampleQueueEntry	*next;
};

extern bool	noSound;

void initSound();
void shutdownSound();

Sample *findSample(uint code);

void playSample(Sample *sample);

#ifdef SF3500
/* Background music - track 0-8 (8 is the title/menu music). See sound.cpp's
 * own comment block for the full design (pre-rendered looping PCM, not an
 * on-device tracker player). playLevelMusic() picks among the level tracks
 * (bgm0.raw, bgm1.raw, ...) that exist. Missing files mean no music. */
void playMusic(int track);
void playLevelMusic(int level);
int musicMode();
#endif

#endif

