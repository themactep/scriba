/**
 * @file timer.c
 * @brief Timing and progress tracking utilities
 *
 * This module provides:
 * - Operation timing (start/end)
 * - Rate-limited progress display (1 update/sec)
 * - Elapsed time calculation
 *
 * All functions are no-ops in WASM builds (__EMSCRIPTEN__).
 */

#include <stdio.h>
#include <time.h>

#include "timer.h"

#ifndef __EMSCRIPTEN__
static time_t start_time = 0;
static time_t print_time = 0;
#endif

void timer_start(void)
{
#ifndef __EMSCRIPTEN__
	start_time = time(0);
#endif
}

void timer_end(void)
{
#ifndef __EMSCRIPTEN__
	time_t end_time;
	time(&end_time);
	printf("\rElapsed time: %d seconds\n", (int)difftime(end_time, start_time));
	print_time = 0;
#endif
}

/*
 * Print progress if at least 1 second has elapsed since last print.
 * Uses carriage-return to overwrite the same line, then ANSI clear-to-EOL.
 */
void timer_progress(const char *msg, unsigned long current, unsigned long total)
{
#ifndef __EMSCRIPTEN__
	time_t now;
	time(&now);

	if (print_time && difftime(now, print_time) < 1.0)
		return;

	print_time = now;

	/* \r = go to column 0, \e[K = clear to end of line */
	if (total > 0)
		printf("\r%s %lu%% [%lu] of [%lu] bytes",
		       msg, 100 * current / total, current, total);
	else
		printf("\r%s %lu bytes", msg, current);

	fflush(stdout);
#endif
}
