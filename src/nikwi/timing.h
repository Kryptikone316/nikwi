/*
 * SF3500 port: timing helpers for the log (no effect on other platforms).
 */
#ifndef NIKWI_TIMING_H
#define NIKWI_TIMING_H

#ifdef SF3500
#include <stdio.h>
#include <time.h>

/* Logs "slow: <name> took N ms" when the enclosing scope takes at least
 * limitMs. Put one at the top of a scope you want to watch. */
class SlowTimer
{
public:
	SlowTimer(const char *what, double limitMs = 150.0) : name(what), limit(limitMs)
	{
		clock_gettime(CLOCK_MONOTONIC, &start);
	}
	~SlowTimer()
	{
		struct timespec	end;
		clock_gettime(CLOCK_MONOTONIC, &end);
		double	ms = (end.tv_sec - start.tv_sec) * 1e3 + (end.tv_nsec - start.tv_nsec) / 1e6;
		if (ms >= limit)
			fprintf(stderr, "slow: %s took %.0f ms\n", name, ms);
	}
private:
	const char	*name;
	double		limit;
	struct timespec	start;
};
#else
class SlowTimer
{
public:
	SlowTimer(const char *, double = 0.0) {}
};
#endif

#endif
