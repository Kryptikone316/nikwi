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
** Nikwi Engine - GFX
*/

#include "nikwi.h"

#ifdef SF3500
#include "hw/hwdisp.h"
#include "hw/joykey.h"
#endif

//#define HALF_SIZED_SCREEN

SDL_Surface	*screen = NULL;
#ifdef HALF_SIZED_SCREEN
SDL_Surface	*rscreen = NULL;
#endif
bool		fullscreen = false;

#ifndef SF3500
static SDL_Joystick	*joy = NULL;
#endif

#ifdef SF3500
/* SF3500 port: byte-order/alignment-safe 16-bit little-endian read, lifted
 * from PortMaster's own aarch64 patch to this exact file (portmaster/
 * nikwi.zip's src/gfx.cpp) - avoids both the strict-aliasing UB of casting
 * a raw byte pointer to unsigned short* and any unaligned-load risk, same
 * reasoning that patch applied for aarch64. Our MIPS target is also
 * little-endian so the original code likely would have worked as-is, but
 * this is a free, zero-risk portability fix already proven on real
 * hardware by another port of the same engine - apply it here too. */
static unsigned short read_ushort_le(unsigned char *data)
{
	return data[0] | (data[1] << 8);
}
#endif

SDL_Surface *createSurface(int width, int height, bool colorKey)
{
	SDL_Surface	*surf;
	Uint32		rm, gm, bm;

	#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	#error makeit
	rm = 0xFF000000;
	gm = 0x00FF0000;
	bm = 0x0000FF00;
	#elif defined(SF3500)
	/* SF3500 port: standard RGB565 (R in the high bits), matching what
	 * driver_sf3500.so's video_driver_disp_frame() expects (confirmed by
	 * how Manic Miner's own palette - built via SDL_MapRGB against an
	 * SDL-assigned, not hand-rolled, surface format - displays correctly
	 * on real hardware). The ORIGINAL masks below are reversed (B in the
	 * high bits) - harmless on a normal SDL blit path where every pixel
	 * is produced via SDL_MapRGB(surf->format, ...) consistently (as this
	 * engine always does - confirmed by grep, no raw 16-bit color literals
	 * anywhere), but would swap red/blue once we hand screen->pixels
	 * directly to the hardware present call instead of SDL_Flip(). */
	rm = 0x0000F800;
	gm = 0x000007E0;
	bm = 0x0000001F;
	#else
	rm = 0x0000001F;
	gm = 0x000007E0;
	bm = 0x0000F800;
	#endif

	if (screen && screen->format)
	{
		rm = screen->format->Rmask;
		gm = screen->format->Gmask;
		bm = screen->format->Bmask;
	}

	surf = SDL_CreateRGBSurface(SDL_HWSURFACE|(colorKey?SDL_SRCCOLORKEY:0),
		width, height, 16, rm, gm, bm, 0);
	if (!surf)
		return NULL;
		

	if (colorKey)
	{
		SDL_SetColorKey(surf, SDL_SRCCOLORKEY, SDL_MapRGB(surf->format,
			248, 0, 248));
	}
	
	return surf;
}

SDL_Surface *loadImage(String file)
{
	uint		len;
	unsigned char	*data = (unsigned char*)getData(file, len);
	unsigned short	width;
	unsigned short	height;
	unsigned short	*pixels;
	unsigned short	*spixels;
	SDL_Surface	*surf;
	if (!data)
	{
		return NULL;
	}
	pixels = (unsigned short*)&data[8];
	#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	#error redo
	width = (data[0] << 8)|data[1];
	height = (data[2] << 8)|data[3];
	#elif defined(SF3500)
	width = read_ushort_le(data + 4);
	height = read_ushort_le(data + 6);
	#else
	width = *((unsigned short*)(data + 4));
	height = *((unsigned short*)(data + 6));
	#endif
	
	surf = createSurface(width, height);
	if (!surf)
	{
		free(data);
		return NULL;
	}
	
	if (SDL_MUSTLOCK(surf))
		SDL_LockSurface(surf);
	uint	index = 0;
	for (uint h=0;h<height;h++)
	{
		spixels = &((unsigned short*)surf->pixels)[h*surf->pitch/2];
		for (uint w=0;w<width;w++)
		{
			/*
			int	rv = pixels[index++];
			int	gv = pixels[index++];
			int	bv = pixels[index++];
			*/
			unsigned int	pixel = pixels[index++];
			int		rv, gv, bv;
			
			bv = (pixel&31) << 3;
			gv = ((pixel >> 5)&63) << 2;
			rv = ((pixel >> 11)&31) << 3;
			*(spixels++) = SDL_MapRGB(surf->format, rv, gv, bv);
/*			*(spixels++) = rv;
			*(spixels++) = gv;
			*(spixels++) = bv;
			*(spixels++) = 0;*/
		}
	}
	if (SDL_MUSTLOCK(surf))
		SDL_UnlockSurface(surf);
	
	free(data);
	
	return surf;
}

void drawLine(int x1, int y1, int x2, int y2, int color)
{
	unsigned short	*pixels = (unsigned short*)screen->pixels;
	float	len = hypot(x2 - x1, y2 - y1);
	float	x, y, dx, dy;
	
	dx = (x2 - x1)/len;
	dy = (y2 - y1)/len;
	
	x = x1;
	y = y1;
	
	for (int i=0;i<(int)len;i++,x+=dx,y+=dy)
		if (x >= 0 && x <= 639 && y >= 0 && y <= 479)
			pixels[(int)y*640 + (int)x] = color;
}

void drawBox(int x1, int y1, int x2, int y2, int color)
{
	unsigned short	*pixels = (unsigned short*)screen->pixels;
	for (int x=x1;x<x2;x++)
	{
		if (x < 0 || x > 639)
			continue;
		if (y1 >= 0 && y1 < 480)
			pixels[y1*640 + x] = color;
		if (y2 >= 0 && y2 < 480)
			pixels[y2*640 + x] = color;
	}
	for (int y=y1;y<y2;y++)
	{
		if (y < 0 || y > 479)
			continue;
		if (x1 >= 0 && x1 < 640)
			pixels[y*640 + x1] = color;
		if (x2 >= 0 && x2 < 640)
			pixels[y*640 + x2] = color;
	}
}

bool initGfx(String winCaption)
{
	#ifdef SF3500
	/* SF3500 port: audio/joystick never go through SDL on this device (no
	 * usable backend for either - see hwaudio.c/joykey.c). hwdisp_init()
	 * and joykey_init() MUST run immediately after SDL_Init(), strictly
	 * before any SDL_SetVideoMode() call - confirmed real mechanism (not
	 * just convention) in hcfb.c: whichever subsystem opens /dev/fb0
	 * first does the real hardware mode-set, the other just inherits
	 * whatever's already there. Getting this ordering wrong caused a real
	 * black-screen/input-dead hang on Atomiks' first real-hardware run. */
	SDL_Init(SDL_INIT_VIDEO);
	hwdisp_init();
	hwdisp_set_target_aspect(4, 3);
	joykey_init();
	#else
	SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK);
	#endif
	#ifdef HALF_SIZED_SCREEN
	rscreen = SDL_SetVideoMode(320, 240, 16, (fullscreen?SDL_FULLSCREEN:0)|
		SDL_SWSURFACE|SDL_DOUBLEBUF);
	if (!rscreen)
		rscreen = SDL_SetVideoMode(320, 240, 16,
			(fullscreen?SDL_FULLSCREEN:0));
	if (!rscreen)
		rscreen = SDL_SetVideoMode(320, 240, 16, 0);
	
	screen = createSurface(640, 480, false);

	screen = SDL_CreateRGBSurface(SDL_HWSURFACE, 640, 480, 16,
		rscreen->format->Rmask,
		rscreen->format->Gmask,
		rscreen->format->Bmask,
		rscreen->format->Amask);
	#elif defined(SF3500)
	/* SF3500 port, second real-hardware round (2026-10-13): the first
	 * attempt trusted whatever pixel format SDL_SetVideoMode() negotiated
	 * for `screen` itself (same technique Manic Miner's own actualScreen/
	 * palette setup uses) and fed that surface's raw pixels straight to
	 * hwdisp_present(). Real-hardware result: a green color-cast PLUS a
	 * background layer that cycled through 4 rotated quadrant positions -
	 * the second symptom in particular is the signature of a buffer that
	 * is not the single, stable, software-owned memory our own draw calls
	 * think it is (a real SDL_HWSURFACE|SDL_DOUBLEBUF video-mode surface
	 * can be backed by more than one physical page, flipped independently
	 * of our code since we never call SDL_Flip() - every other real path
	 * in this project only ever hands hwdisp_present() a plain malloc'd/
	 * SDL_SWSURFACE buffer, never a live hardware-backed one).
	 *
	 * Fix: stop using the real video-mode surface as our render target at
	 * all. Still set a real video mode once (`realVideoMode`, below) only
	 * for whatever side effect SDL's fbcon driver ties to having an active
	 * mode (same reason Manic Miner's own comment gives for keeping its
	 * SDL_SetVideoMode call even though its own real present path also
	 * bypasses SDL_Flip) - but its surface is never read from or written
	 * to again. `screen` - the surface every draw call, loadImage(),
	 * World::drawStaticPart()'s cache, and Transition's grab/blit all
	 * target - is instead a plain SDL_SWSURFACE with EXPLICIT, fixed
	 * RGB565 masks, matching exactly what driver_sf3500.so's
	 * video_driver_disp_frame() is documented (and already proven on this
	 * device by every other port) to expect from a raw buffer. This also
	 * means createSurface()'s own SF3500 mask branch now actually takes
	 * effect for the FIRST time `screen` exists (previously dead code,
	 * since `screen` was always already non-NULL via SDL_SetVideoMode by
	 * the time anything else ran) - both now agree on the same masks. */
	{
		SDL_Surface *realVideoMode = SDL_SetVideoMode(640, 480, 16,
			(fullscreen?SDL_FULLSCREEN:0)|SDL_SWSURFACE);
		if (!realVideoMode)
			realVideoMode = SDL_SetVideoMode(640, 480, 16, 0);
		(void)realVideoMode;
	}
	screen = SDL_CreateRGBSurface(SDL_SWSURFACE, 640, 480, 16,
		0x0000F800, 0x000007E0, 0x0000001F, 0);
	if (!screen)
		return false;
	#else
	screen = SDL_SetVideoMode(640, 480, 16, (fullscreen?SDL_FULLSCREEN:0)|
		SDL_HWSURFACE|SDL_DOUBLEBUF);
	if (!screen)
		screen = SDL_SetVideoMode(640, 480, 16,
			(fullscreen?SDL_FULLSCREEN:0));
	if (!screen)
		screen = SDL_SetVideoMode(640, 480, 16, 0);

	if (!screen)
		return false;
	#endif
	
	SDL_WM_SetCaption(winCaption, winCaption);
	SDL_ShowCursor(false);

	#ifdef SF3500
	/* One-shot diagnostic so a future run.log has hard data instead of
	 * another guess, if color still looks wrong after this round's fix. */
	fprintf(stderr, "gfx: screen %dx%d bpp=%d Rmask=0x%08x Gmask=0x%08x "
		"Bmask=0x%08x pitch=%d\n", screen->w, screen->h,
		screen->format->BitsPerPixel, screen->format->Rmask,
		screen->format->Gmask, screen->format->Bmask, screen->pitch);
	#endif

	#ifndef SF3500
	joy = SDL_JoystickOpen(0);
	#endif

	return true;
}

void shutdownGfx()
{
	#ifdef SF3500
	hwdisp_deinit();
	#else
	if (joy)
		SDL_JoystickClose(joy);
	joy = NULL;
	#endif
	SDL_Quit();
}

void updateSystemScreen()
{
	#ifdef HALF_SIZED_SCREEN
	
	if (SDL_MUSTLOCK(screen))
		SDL_LockSurface(screen);
	if (SDL_MUSTLOCK(rscreen))
		SDL_LockSurface(rscreen);
	
	unsigned short	*spixels = (unsigned short*)screen->pixels;
	unsigned short	*rpixels = (unsigned short*)rscreen->pixels;
	unsigned int	spitch = screen->pitch;
	unsigned int	rpitch = rscreen->pitch >> 1;
	unsigned int	sindex, rindex, sstart = 0, rstart = 0;
	
	for (int y=0;y<240;y++)
	{
		sindex = sstart;
		rindex = rstart;
		
		for (int x=0;x<320;x++)
		{
			rpixels[rindex] = spixels[sindex];
			
			sindex += 2;
			++rindex;
		}
		
		sstart += spitch;
		rstart += rpitch;
	}
	
	if (SDL_MUSTLOCK(rscreen))
		SDL_UnlockSurface(rscreen);
	if (SDL_MUSTLOCK(screen))
		SDL_UnlockSurface(screen);
	
	SDL_Flip(rscreen);
	
	#elif defined(SF3500)
	/* Real present path: hand the already-640x480/RGB565 surface straight
	 * to the driver instead of SDL_Flip() - SDL1.2 has no usable video
	 * backend on this device (fbcon's own rotation renders solid black,
	 * confirmed on this device family). screen is already exactly the
	 * driver's own 640x480/4:3 internal canvas size, so this is a
	 * straight copy, no scaling/padding math needed (same free ride
	 * Manic Miner's already-4:3 256x192... source got, just at a
	 * different resolution). */
	if (SDL_MUSTLOCK(screen))
		SDL_LockSurface(screen);
	hwdisp_present(screen->pixels, screen->w, screen->h, screen->pitch);
	if (SDL_MUSTLOCK(screen))
		SDL_UnlockSurface(screen);
	#else
	SDL_Flip(screen);
	#endif
}

