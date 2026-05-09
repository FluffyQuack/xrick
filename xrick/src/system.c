/*
 * xrick/src/system.c
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#include <SDL.h>

#include <stdarg.h>   /* args for sys_panic */
#include <fcntl.h>    /* fcntl in sys_panic */
#include <stdio.h>    /* printf */
#include <stdlib.h>

#include "system.h"

/*
 * Panic
 */
void
sys_panic(char *err, ...)
{
	va_list argptr;
	char s[1024];

	/* FIXME what is this? */
	/* change stdin to non blocking */
	/*fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);*/
	/* NOTE HPUX: use ... is it OK on Linux ? */
	/* fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~O_NDELAY); */

	/* prepare message */
	va_start(argptr, err);
	vsprintf(s, err, argptr);
	va_end(argptr);

	/* print message and die */
	printf("%s\npanic!\n", s);
	exit(1);
}


/*
 * Print a message
 */
void
sys_printf(char *msg, ...)
{
#ifdef ENABLE_LOG
	va_list argptr;
	char s[1024];

	/* FIXME what is this? */
	/* change stdin to non blocking */
	/* fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY); */
	/* NOTE HPUX: use ... is it OK on Linux ? */
	/* fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~O_NDELAY); */

	/* prepare message */
	va_start(argptr, msg);
	vsprintf(s, msg, argptr);
	va_end(argptr);
	printf(s);
#endif
}

/*
 * High-resolution timing.
 *
 * SDL_GetTicks() only has ms resolution, and SDL_Delay() on Windows is at the
 * mercy of the OS scheduler granularity (~15.6 ms by default). With a 75 ms
 * game period that produced visible jitter: a sleep would overrun by 5-15 ms,
 * the next iteration would see "tmx >= period" and skip the wait entirely,
 * then the iteration after that would sleep almost a full period -- so the
 * effective tick spacing alternated between ~1 display frame and ~5 frames.
 *
 * Fix:
 *   - measure with SDL_GetPerformanceCounter (sub-microsecond)
 *   - on Windows, request 1 ms timer resolution via timeBeginPeriod
 *   - sleep coarsely for the bulk of the wait, then busy-wait the last ~2 ms
 *
 * The public API is still milliseconds for compatibility with the rest of
 * the codebase.
 */

#if defined(_WIN32) && !defined(EMSCRIPTEN)
#include <windows.h>
#define XRICK_WIN_TIMER 1
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib") /* timeBeginPeriod / timeEndPeriod */
#endif
#endif

static Uint64 perf_freq = 0;
static Uint64 perf_base = 0;
static int    timer_period_set = 0;

static void timing_init(void)
{
	if (perf_freq == 0)
	{
		perf_freq = SDL_GetPerformanceFrequency();
		perf_base = SDL_GetPerformanceCounter();
#ifdef XRICK_WIN_TIMER
		/* Request 1ms scheduler granularity so SDL_Delay/Sleep is not
		 * rounded up to ~15.6 ms. SDL2 does this internally for some
		 * paths but it is cheap insurance to do it ourselves. */
		if (timeBeginPeriod(1) == TIMERR_NOERROR)
			timer_period_set = 1;
#endif
	}
}

void sys_timing_shutdown(void)
{
#ifdef XRICK_WIN_TIMER
	if (timer_period_set)
	{
		timeEndPeriod(1);
		timer_period_set = 0;
	}
#endif
}

/* Sub-millisecond elapsed time since first call, returned in microseconds. */
U32
sys_gettime_us(void)
{
	timing_init();
	Uint64 now = SDL_GetPerformanceCounter();
	/* (now - base) * 1e6 / freq, computed to avoid overflow on 32-bit. */
	return (U32)(((now - perf_base) * 1000000ULL) / perf_freq);
}

/*
 * Return number of milliseconds elapsed since first call.
 */
U32
sys_gettime(void)
{
	return sys_gettime_us() / 1000U;
}

/*
 * Sleep a number of milliseconds, accurately.
 *
 * Strategy: coarse-sleep until ~2 ms before the deadline (to absorb scheduler
 * jitter on Windows), then spin-wait the remainder. The spin is bounded so
 * worst-case CPU burn per call is ~2 ms.
 */
void
sys_sleep(int ms)
{
	if (ms <= 0) return;
	timing_init();

	const U32 spin_threshold_us = 2000; /* 2 ms */
	U32 deadline = sys_gettime_us() + (U32)ms * 1000U;

	if ((U32)ms * 1000U > spin_threshold_us)
	{
		int coarse_ms = ms - 2;
		if (coarse_ms > 0) SDL_Delay((Uint32)coarse_ms);
	}

	/* spin to the deadline */
	while ((S32)(deadline - sys_gettime_us()) > 0)
	{
		/* yield briefly so we are not a 100% busy-wait */
#ifdef XRICK_WIN_TIMER
		YieldProcessor();
#endif
	}
}

/* eof */
