/* See charpicker.h for the full design note. */
#include "charpicker.h"

#include <string.h>

void charpicker_init(CharPicker *p, const char *charset, int maxLen)
{
	int len;

	len = (int)strlen(charset);
	if (len > CHARPICKER_MAX_CHARSET)
		len = CHARPICKER_MAX_CHARSET;
	memcpy(p->charset, charset, len);
	p->charset[len] = 0;
	p->charsetLen = len;

	if (maxLen > CHARPICKER_MAX_LEN)
		maxLen = CHARPICKER_MAX_LEN;
	if (maxLen < 0)
		maxLen = 0;
	p->maxLen = maxLen;

	p->buf[0] = 0;
	p->len = 0;
	p->cursor = 0;
}

void charpicker_up(CharPicker *p)
{
	if (p->charsetLen <= 0)
		return;
	p->cursor = (p->cursor + p->charsetLen - 1) % p->charsetLen;
}

void charpicker_down(CharPicker *p)
{
	if (p->charsetLen <= 0)
		return;
	p->cursor = (p->cursor + 1) % p->charsetLen;
}

int charpicker_confirm(CharPicker *p)
{
	if (p->len >= p->maxLen || p->charsetLen <= 0)
		return 0;

	p->buf[p->len++] = p->charset[p->cursor];
	p->buf[p->len] = 0;
	p->cursor = 0;
	return 1;
}

int charpicker_backspace(CharPicker *p)
{
	if (p->len <= 0)
		return 0;

	p->len--;
	p->buf[p->len] = 0;
	return 1;
}

const char *charpicker_get_string(const CharPicker *p)
{
	return p->buf;
}

int charpicker_is_full(const CharPicker *p)
{
	return p->len >= p->maxLen;
}

char charpicker_current_char(const CharPicker *p)
{
	if (p->charsetLen <= 0)
		return 0;
	return p->charset[p->cursor];
}
