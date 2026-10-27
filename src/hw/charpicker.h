/* sf3000-charpicker - tiny reusable "cycle a candidate character with a
 * d-pad, confirm commits it and advances, backspace steps back" state
 * machine, the classic retro password/name-entry/high-score-initials
 * pattern, for devices with no keyboard (this whole device family).
 * Built 2026-10-13 alongside bitfont.c/.h for Nikwi Deluxe's password
 * screen - see that file's own header for the font half of this pair,
 * Copy this file pair in verbatim for any future
 * SF3500 port needing name/code entry, same convention as hwdisp.c/
 * hwaudio.c/joykey.c/bitfont.c.
 *
 * Deliberately just the STATE (which candidate is highlighted, what's
 * been committed so far, cursor/slot bookkeeping) - no input polling, no
 * rendering. A caller drives it from its own joykey_read() edge-detection
 * (up/down -> charpicker_up/_down, a confirm button -> charpicker_confirm,
 * a dedicated backspace button -> charpicker_backspace - never overload
 * confirm and delete onto one button, this device's own convention) and
 * renders it with bitfont_draw_string()/bitfont_draw_char() (or its own
 * renderer, nothing here depends on bitfont.c). Nikwi's own menu.cpp only
 * uses the cycling/current-char half of this API, since it already has a
 * working committed-letters path (synthesizes the real SDLK_a+offset
 * keydown its existing pwdMode logic expects) - a future port with no
 * such existing path would drive its whole UI off charpicker_get_string()
 * instead; both are legitimate ways to use this. */
#ifndef SF3000_CHARPICKER_H
#define SF3000_CHARPICKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Hard cap on committed length and charset size - fixed-size, no malloc,
 * matching this device's other shared files. 16 is generous for any
 * realistic password/name/initials field; raise it here (not per-caller)
 * if a future port genuinely needs more. */
#define CHARPICKER_MAX_LEN     16
#define CHARPICKER_MAX_CHARSET 64

typedef struct {
	char	charset[CHARPICKER_MAX_CHARSET + 1];
	int	charsetLen;
	int	maxLen;
	char	buf[CHARPICKER_MAX_LEN + 1];
	int	len;	/* characters committed so far */
	int	cursor;	/* index into charset[] of the current, NOT-yet-
			 * committed candidate - this is what a caller
			 * renders alongside the committed string */
} CharPicker;

/* `charset` is a plain NUL-terminated string of selectable characters in
 * cycling order, e.g. "abcdefghijklmnopqrstuvwxyz" or
 * "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 " for a name-entry screen with a
 * space. Truncated to CHARPICKER_MAX_CHARSET-1 if longer. `maxLen` is the
 * number of characters this session should collect, clamped to
 * CHARPICKER_MAX_LEN. Starts empty, candidate cursor at charset[0]. */
void charpicker_init(CharPicker *p, const char *charset, int maxLen);

/* Cycles the current (uncommitted) candidate forward/backward through the
 * charset, wrapping at either end. */
void charpicker_up(CharPicker *p);
void charpicker_down(CharPicker *p);

/* Commits the current candidate character to the buffer, resets the
 * candidate cursor back to charset[0] for the next slot, and returns 1 -
 * or does nothing and returns 0 if already at maxLen (caller should give
 * feedback, e.g. a sound, the same way Nikwi's own pwdMode already does
 * for "password already full"). */
int charpicker_confirm(CharPicker *p);

/* Removes the last committed character and returns 1 - or does nothing
 * and returns 0 if already empty. Does not touch the candidate cursor. */
int charpicker_backspace(CharPicker *p);

/* The committed string so far, always NUL-terminated - owned by the
 * picker, valid until the next call that mutates it (confirm/backspace/
 * init). Length is always <= maxLen. */
const char *charpicker_get_string(const CharPicker *p);

/* 1 once maxLen characters have been committed. */
int charpicker_is_full(const CharPicker *p);

/* The actual candidate character currently highlighted (not yet
 * committed) - what a caller renders next to the committed string, e.g.
 * via bitfont_draw_char(). */
char charpicker_current_char(const CharPicker *p);

#ifdef __cplusplus
}
#endif

#endif
