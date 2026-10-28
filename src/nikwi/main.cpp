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
** Nikwi - a game by Kostas Michalopoulos
*/

#include "nikwi.h"
#include "timing.h"

#ifdef SF3500
#include "hw/joykey.h"
#include "hw/hwdisp.h"
#include "hw/hwaudio.h"
#include "hw/charpicker.h"
#include <time.h>
#ifdef SF3500
#include <signal.h>
#include <execinfo.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/syscall.h>
#endif
#endif

bool	running = true;
bool	debugMode = false;
int	debugCounter = 0;

static void timedTasks()
{
	activeScreen->timer();
}

static uint	extra = 0;
static uint	lastTicks = 0, ticks = 0;

#ifdef SF3500
/* Frame timing for the log: every 300 frames, how long frames take. */
static void logFrameTiming(const struct timespec &start, const struct timespec &mid, const struct timespec &end)
{
	static struct timespec	last = { 0, 0 };
	static long		frames = 0;
	static double		sumInterval = 0, maxInterval = 0, sumDraw = 0, maxDraw = 0, sumPres = 0, maxPres = 0;
	double			draw = (mid.tv_sec - start.tv_sec) * 1e3 + (mid.tv_nsec - start.tv_nsec) / 1e6;
	double			pres = (end.tv_sec - mid.tv_sec) * 1e3 + (end.tv_nsec - mid.tv_nsec) / 1e6;

	if (last.tv_sec || last.tv_nsec)
	{
		double	interval = (start.tv_sec - last.tv_sec) * 1e3 + (start.tv_nsec - last.tv_nsec) / 1e6;
		sumInterval += interval;
		if (interval > maxInterval)
			maxInterval = interval;
	}
	sumDraw += draw;
	if (draw > maxDraw)
		maxDraw = draw;
	sumPres += pres;
	if (pres > maxPres)
		maxPres = pres;
	last = start;
	if (++frames >= 300)
	{
		static const char	*modes[] = { "none", "raw", "ogg decoding", "ogg ready" };
		fprintf(stderr, "frames: level %d, music %s, %.1f fps, frame interval avg %.1f ms max %.1f ms, drawing avg %.1f ms max %.1f ms, present avg %.1f ms max %.1f ms\n",
			(activeScreen == game) ? (int)game->level : -1, modes[musicMode()],
			1000.0 * (frames - 1) / sumInterval, sumInterval / (frames - 1), maxInterval,
			sumDraw / frames, maxDraw, sumPres / frames, maxPres);
		frames = 0;
		sumInterval = maxInterval = sumDraw = maxDraw = sumPres = maxPres = 0;
	}
}
#endif

#ifdef SF3500
/* Frame pacing. The game's logic steps every 20 ms (50 times a second). If
 * frames are drawn faster than that and not in step with it, some frames
 * contain no movement and the motion hitches. With a frame limit of 50 each
 * frame gets exactly one logic step. nikwi.txt in the port folder sets it
 * ("fps=50"; 0 switches the limiter off). */
static long long	frameLimitNs = 20000000LL;
static long long	tickAccNs = 0, tickLastNs = 0;
static bool		tickStarted = false;
static struct timespec	nextFrame;
static bool		paceStarted = false;

static long long nowNs()
{
	struct timespec	t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void loadSettings()
{
	FILE	*f = fopen("nikwi.txt", "r");
	char	line[128];
	int	fps;

	if (f)
	{
		while (fgets(line, sizeof(line), f))
			if (sscanf(line, "fps=%d", &fps) == 1)
				frameLimitNs = (fps > 0 && fps <= 200) ? 1000000000LL / fps : 0;
		fclose(f);
	}
	if (frameLimitNs)
		fprintf(stderr, "settings: frame limit %lld fps\n", 1000000000LL / frameLimitNs);
	else
		fprintf(stderr, "settings: no frame limit\n");
}

/* Wait for the next frame slot. If the frame was late, start the schedule over
 * from now instead of rushing to catch up. */
static void paceFrame(const struct timespec &frameStart)
{
	if (!frameLimitNs)
		return;
	if (!paceStarted)
	{
		nextFrame = frameStart;
		paceStarted = true;
	}
	nextFrame.tv_nsec += (long)frameLimitNs;
	while (nextFrame.tv_nsec >= 1000000000L)
	{
		nextFrame.tv_nsec -= 1000000000L;
		nextFrame.tv_sec++;
	}
	struct timespec	now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (now.tv_sec > nextFrame.tv_sec ||
		(now.tv_sec == nextFrame.tv_sec && now.tv_nsec >= nextFrame.tv_nsec))
		nextFrame = now;
	else
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &nextFrame, NULL);
}
#endif

static void draw()
{
#ifdef SF3500
	struct timespec	frameStart, frameMid, frameEnd;
	clock_gettime(CLOCK_MONOTONIC, &frameStart);
	{
		SlowTimer	t("game logic");
		long long	now = nowNs();
		if (!tickStarted)
		{
			tickLastNs = now;
			tickAccNs = 0;
			tickStarted = true;
		}
		tickAccNs += now - tickLastNs;
		tickLastNs = now;
		if (tickAccNs > 200000000LL)
			tickAccNs = 200000000LL;	/* after a long stall do not replay it */
		while (tickAccNs >= 20000000LL)
		{
			timedTasks();
			tickAccNs -= 20000000LL;
		}
	}
#else
	ticks = SDL_GetTicks();
	if (ticks - lastTicks > 20)
	{
		uint	diff = ticks - lastTicks + extra;
		uint	times = diff/20;
		extra = diff;
		for (uint i=0;i<times;i++,extra -= 20)
			timedTasks();
		lastTicks = ticks;
	}
#endif
	
	activeScreen->draw();
#ifdef SF3500
	clock_gettime(CLOCK_MONOTONIC, &frameMid);
#endif
	updateSystemScreen();
#ifdef SF3500
	clock_gettime(CLOCK_MONOTONIC, &frameEnd);
	logFrameTiming(frameStart, frameMid, frameEnd);
	paceFrame(frameStart);
#endif
}

void resetTimer()
{
	ticks = lastTicks = SDL_GetTicks();
#ifdef SF3500
	tickStarted = false;
	paceStarted = false;
#endif
}

#ifdef SF3500

/* SF3500 port: no real SDL joystick/keyboard events exist on this device -
 * input comes from joykey_read()'s shared-memory bitmask (see joykey.h).
 * Edge-detect against the previous poll and synthesize the exact same
 * SDL_Event/SDLK_ values the game's own handleEvent() methods already
 * expect, same technique used on every other SDL1.2/SDL2 port on this
 * device. D-pad left/right/down -> arrows. B -> ESCAPE (pause/back,
 * also doubles as "cancel password entry" since menu.cpp's own pwdMode
 * handling already treats ESCAPE that way, unchanged). START -> RETURN
 * (menu confirm, also "submit password" once 6 letters are entered -
 * also unchanged existing behavior). SELECT+START held together
 * force-quits directly, bypassing the game's own menu - standing
 * convention for this device, since an untested in-game quit path has
 * hung real hardware on a prior port.
 *
 * UP and A are NOT simple static mappings - see the real-hardware
 * "input feels clunky" report (2026-10-13) and the dedicated comments
 * below for why each needs its own combined-state handling. */
static unsigned int prevJoy = 0;

static void pushKeyEvent(Uint8 type, SDLKey sym)
{
	SDL_Event	e;
	memset(&e, 0, sizeof(e));
	e.type = type;
	e.key.keysym.sym = sym;
	activeScreen->handleEvent(e);
}

static void pollJoykey()
{
	unsigned int	cur = joykey_read();
	unsigned int	changed = cur ^ prevJoy;
	static const struct { int bit; SDLKey sym; } map[] = {
		{ JK_LEFT,  SDLK_LEFT },
		{ JK_RIGHT, SDLK_RIGHT },
		{ JK_DOWN,  SDLK_DOWN },
		{ JK_B,     SDLK_ESCAPE },
		{ JK_START, SDLK_RETURN },
	};
	bool	pwdMode = (menuScreen && menuScreen->pwdMode);

	if ((cur & ((1u<<JK_SELECT)|(1u<<JK_START))) ==
		((1u<<JK_SELECT)|(1u<<JK_START)))
	{
		hwaudio_deinit();
		hwdisp_deinit();
		exit(0);
	}

	for (uint i=0;i<sizeof(map)/sizeof(map[0]);i++)
	{
		unsigned int	bit = 1u << map[i].bit;
		if (changed & bit)
			pushKeyEvent((cur & bit) ? SDL_KEYDOWN : SDL_KEYUP,
				map[i].sym);
	}

	if (pwdMode)
	{
		/* No keyboard on this device - D-pad up/down cycles a not-yet-
		 * committed candidate letter via the shared CharPicker (see
		 * hw/charpicker.h; MenuScreen::picker), rendered as a real
		 * glyph by menu.cpp's draw() via the shared bitfont renderer
		 * (hw/bitfont.h - the first attempt showed only an
		 * undifferentiated dot count and was reported unusable on
		 * real hardware). A commits the candidate as the real
		 * SDLK_a+offset keydown menu.cpp's existing pwdMode
		 * handleEvent() block already expects (unchanged - the
		 * picker's own internal buffer is not Nikwi's source of
		 * truth, pwd[]/pwdLen still are). X is a DEDICATED backspace
		 * button (SDLK_BACKSPACE) separate from confirm - matching
		 * the user's other ports' own text-cycler convention of never
		 * overloading confirm and delete onto the same button. */
		/* Swapped 2026-10-13 per real-hardware feedback: up/down felt
		 * backwards from what players expect (up = go back/earlier in
		 * the alphabet felt natural, not forward) - pure convention
		 * preference, not a bug. */
		if (changed & (1u<<JK_UP) && (cur & (1u<<JK_UP)))
			charpicker_down(&menuScreen->picker);
		if (changed & (1u<<JK_DOWN) && (cur & (1u<<JK_DOWN)))
			charpicker_up(&menuScreen->picker);
		if (changed & (1u<<JK_A) && (cur & (1u<<JK_A)))
		{
			char	c = charpicker_current_char(&menuScreen->picker);
			pushKeyEvent(SDL_KEYDOWN, (SDLKey)(SDLK_a + (c - 'a')));
			charpicker_confirm(&menuScreen->picker);
		}
		if (changed & (1u<<JK_X) && (cur & (1u<<JK_X)))
			pushKeyEvent(SDL_KEYDOWN, SDLK_BACKSPACE);
	}
	else
	{
		/* Real-hardware "input feels clunky" report (2026-10-13), root
		 * cause: the original PC/joystick build's jbutton0 handler
		 * dynamically mapped the controller's primary action button to
		 * SDLK_UP (jump) during live gameplay, SDLK_SPACE everywhere
		 * else (menu confirm, restart-when-dead) - see the now-#else'd
		 * translateJoyMotionToKey() path below for the original logic
		 * this replaced. The first joykey port of this statically
		 * mapped A -> SDLK_SPACE ALWAYS, silently losing "A also jumps"
		 * during gameplay and leaving jump reachable only from the
		 * d-pad - a real, concrete usability regression, not just a
		 * vague feel issue.
		 *
		 * Fixed by restoring the same dynamic split, but since BOTH
		 * the d-pad's own UP bit and A (in this one context) can
		 * independently want "SDLK_UP is down", a naive per-bit edge
		 * check would send a spurious KEYUP (clearing Game's
		 * wannaJump/jumping flags) if the player releases just ONE of
		 * two simultaneously-held jump inputs. Tracked via the merged
		 * boolean below instead of either raw bit alone. */
		static bool	prevVirtualJump = false;
		bool	jumpContext = (activeScreen == game && !game->dead);
		bool	curVirtualJump = (cur & (1u<<JK_UP)) != 0 ||
			(jumpContext && (cur & (1u<<JK_A)) != 0);

		if (curVirtualJump != prevVirtualJump)
			pushKeyEvent(curVirtualJump ? SDL_KEYDOWN : SDL_KEYUP,
				SDLK_UP);
		prevVirtualJump = curVirtualJump;

		/* A's OTHER role (confirm/restart) only applies outside the
		 * jump context above - same mutual-exclusivity the original
		 * jbutton0 handler had (one physical button, one role at a
		 * time depending on context), so a plain edge check on JK_A
		 * alone is safe here (no merge needed - SDLK_SPACE has no
		 * second source the way SDLK_UP does). */
		if (!jumpContext && (changed & (1u<<JK_A)))
			pushKeyEvent((cur & (1u<<JK_A)) ? SDL_KEYDOWN : SDL_KEYUP,
				SDLK_SPACE);
	}

	prevJoy = cur;
}

#else

static void translateJoyMotionToKey(SDL_Event &e)
{
	static int	prevXAxis = 0;
	static int	prevYAxis = 0;
	
	if (e.jaxis.axis == 0)	// x-axis
	{
		if (e.jaxis.value > 32000)
		{
			prevXAxis = e.jaxis.value;
			e.type = SDL_KEYDOWN;
			e.key.keysym.sym = SDLK_RIGHT;
		}
		else if (e.jaxis.value < -32000)
		{
			prevXAxis = e.jaxis.value;
			e.type = SDL_KEYDOWN;
			e.key.keysym.sym = SDLK_LEFT;
		}
		else
		{
			e.type = SDL_KEYUP;
			if (prevXAxis > 32000)
				e.key.keysym.sym = SDLK_RIGHT;
			else
				e.key.keysym.sym = SDLK_LEFT;
		}
	}
	if (e.jaxis.axis == 1)	// y-axis
	{
		if (e.jaxis.value > 32000)
		{
			prevYAxis = e.jaxis.value;
			e.type = SDL_KEYDOWN;
			e.key.keysym.sym = SDLK_DOWN;
		}
		else if (e.jaxis.value < -32000)
		{
			if (game == activeScreen && !game->dead)
				return;
			prevYAxis = e.jaxis.value;
			e.type = SDL_KEYDOWN;
			e.key.keysym.sym = SDLK_UP;
		}
		else
		{
			e.type = SDL_KEYUP;
			if (prevYAxis > 32000)
				e.key.keysym.sym = SDLK_DOWN;
			else
				e.key.keysym.sym = SDLK_UP;
		}
	}
}

#endif /* SF3500 */

static void mainLoop()
{
	while (running)
	{
		{
			SlowTimer	t("scripts");
			usRunScripts();
		}
		draw();

#ifdef SF3500
		{
			SlowTimer	t("input handling");
			pollJoykey();
		}
#else
		SDL_Event	e;
		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
				case SDL_JOYAXISMOTION:
					translateJoyMotionToKey(e);
					break;
				case SDL_JOYBUTTONDOWN:
					if (e.jbutton.button == 0)
					{
						e.type = SDL_KEYDOWN;
						e.key.keysym.sym =
							(activeScreen == game &&
								!game->dead)?
								SDLK_UP:
								SDLK_SPACE;
					}
					break;
				case SDL_JOYBUTTONUP:
					if (e.jbutton.button == 0)
					{
						e.type = SDL_KEYUP;
						e.key.keysym.sym =
							(activeScreen == game &&
								!game->dead)?
								SDLK_UP:
								SDLK_SPACE;
					}
					break;
				case SDL_KEYDOWN:
					switch (e.key.keysym.sym)
					{
						case SDLK_s:
							if (debugCounter < 2)
								debugCounter++;
							break;
						case SDLK_d:
							if (debugCounter == 2)
								debugCounter++;
							break;
						case SDLK_b:
							if (debugCounter == 3)
								debugCounter++;
							break;
						case SDLK_g:
							if (debugCounter == 4)
							{
								debugMode =
								     !debugMode;
								debugCounter =0;
								if (!debugMode)
					    SDL_WM_SetCaption("Nikwi", "Nikwi");
							}
							break;
						case SDLK_F10:
							running = false;
						default:;
					}
					break;
				case SDL_QUIT:
					running = false;
					break;
			}
			activeScreen->handleEvent(e);
		}
#endif
	}
}

#ifdef __APPLE__
extern "C" int SDL_main(int argn, char** argv)
#else
#ifdef SF3500
/* Crash report for the log: which signal, the faulting address, the program
 * counter and return address (as offsets from main(), so they can be matched
 * against an unstripped build), whether it was the main thread, and the top of
 * the stack. The device has no debugger, so this is how a crash gets found. */
int main(int, char **);
static void crashHandler(int sig, siginfo_t *info, void *context)
{
	ucontext_t	*uc = (ucontext_t*)context;
	unsigned long	mainAddr = (unsigned long)&main;
	unsigned long	pc = (unsigned long)uc->uc_mcontext.pc;
	unsigned long	ra = (unsigned long)uc->uc_mcontext.gregs[31];
	unsigned long	sp = (unsigned long)uc->uc_mcontext.gregs[29];
	void		*frames[24];
	int		n;

	fprintf(stderr, "CRASH: signal %d (code %d), fault address %p, %s thread\n",
		sig, info->si_code, info->si_addr,
		(syscall(SYS_gettid) == getpid()) ? "main" : "other");
	fprintf(stderr, "CRASH: main() is at 0x%lx; pc = main%+ld, ra = main%+ld, sp = 0x%lx\n",
		mainAddr, (long)(pc - mainAddr), (long)(ra - mainAddr), sp);
	fprintf(stderr, "CRASH: stack words (as offsets from main when they look like code):");
	for (int i=0;i<48;i++)
	{
		unsigned long	w = ((unsigned long*)sp)[i];
		if (w > mainAddr - 0x100000 && w < mainAddr + 0x100000)
			fprintf(stderr, " %+ld", (long)(w - mainAddr));
	}
	fprintf(stderr, "\n");
	n = backtrace(frames, 24);
	fprintf(stderr, "CRASH: backtrace (offsets from main):");
	for (int i=0;i<n;i++)
		fprintf(stderr, " %+ld", (long)((unsigned long)frames[i] - mainAddr));
	fprintf(stderr, "\n");
	_exit(128 + sig);
}

static void installCrashHandler()
{
	struct sigaction	sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = crashHandler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
}
#endif

int main(int argn, char *argv[])
#endif
{
	bool	editMode = false;
#ifdef SF3500
	installCrashHandler();
	loadSettings();
#endif
	uint	startLevel = 0;
	
	for (int i=0;i<argn;i++)
	{
		if (!strcmp(argv[i], "--enable-editor"))
			editMode = true;
		else if (!strcmp(argv[i], "--start-level"))
			startLevel = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--last-level"))
			startLevel = 30;
		else if (!strcmp(argv[i], "--fullscreen"))
			fullscreen = true;
		else if (!strcmp(argv[i], "--nosound"))
			noSound = true;
	}
	
	if (!initGfx("Nikwi"))
	{
		#ifdef WIN32
		MessageBox(NULL, "Cannot initialize graphics mode 640x480 in "
			"32 bits per pixel mode. Try to change the color depth "
			"if your screen from Desktop properties.", "Error",
			MB_ICONERROR|MB_OK);
		#else
		fprintf(stderr, "Cannot initialize graphics mode 640x480 in "
			"32 bits per pixel mode. Are you under X Windows?");
		#endif
		return 1;
	}
	
	usInitialize();
	
	initNikIO();
	initSound();
#ifdef SF3500
	playMusic(8);	/* title/menu track - matches the PSP port's own
			 * playMusic(8) call at the same point in main() */
#endif
	initSprites();
	initTiles();
	initObjects();
	initScreens();

	world = new World;
	editor = new Editor;
	game = new Game;
	game->level = startLevel;
	editor->aWorld = startLevel;
	
	menuScreen = new MenuScreen;
	imageScreen = new ImageScreen;
	ending = new EndingScreen;
	
	SDL_Delay(500);
	
	resetTimer();
	if (editMode)
		editor->activate();
	else
	{
		if (startLevel)
		{
			game->newGame();
			game->level = startLevel;
			game->restartLevel();
			game->activate();
		}
		else
			imageScreen->showImage(menuScreen,
				"data/objects/slashstone.ut", 160);
	}
	mainLoop();
	activeScreen->leave();
	
	delete ending;
	delete imageScreen;
	delete menuScreen;
	delete game;
	delete editor;
	delete world;
	
	shutdownScreens();
	shutdownObjects();
	shutdownTiles();
	shutdownSprites();
	shutdownSound();
	shutdownNikIO();
	
	usShutdown();
	
	shutdownGfx();
	
	return 0;
}

