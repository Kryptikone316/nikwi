/*
 *  Nikwi Deluxe
 *  Copyright (C) 2006  Kostas Michalopoulos
 *  
 *  A game by Kostas "Bad Sector" Michalopoulos
 *  email: badsector@slashstone.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
** Nikwi Engine - Menu screen
*/

#ifndef __MENU_H_INCLUDED__
#define __MENU_H_INCLUDED__

#ifdef SF3500
#include "hw/charpicker.h"
#endif

struct MenuScreen : public Screen
{
	int		opt;
	SDL_Surface	*background;
	SDL_Surface	*paused;
	SDL_Surface	*pwdBack;
	Sprite		*dot;
	char		pwd[7];
	int		pwdLen;
	bool		pwdMode;
#ifdef SF3500
	/* Drives the "which letter is currently highlighted but not yet
	 * committed" cycling - this device has no keyboard, so main.cpp's
	 * joykey input calls charpicker_up()/_down()/_confirm() on this.
	 * Committing still synthesizes the real SDLK_a+offset keydown this
	 * screen's existing handleEvent() pwdMode block already expects
	 * (unchanged) - this struct's own internal buffer/length is just
	 * along for the ride for Nikwi's integration (pwd[]/pwdLen below
	 * remain the real gameplay state); draw() uses it only to render
	 * the live candidate letter. See hw/charpicker.h for the full,
	 * reusable (not Nikwi-specific) design. */
	CharPicker	picker;
#endif
	uint		pausedShow;
	Sprite		*sprite;
	SpriteState	*spriteState;
	float		a;
	int		y, ty;
	
	MenuScreen();
	virtual ~MenuScreen();
	
	virtual void enter();
	
	virtual void timer();
	
	virtual void draw();
	virtual void handleEvent(SDL_Event &e);
};

extern MenuScreen	*menuScreen;

#endif

