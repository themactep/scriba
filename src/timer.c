/*
 * timer.c
 * Copyright (C) 2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <time.h>

#include "timer.h"

static time_t start_time = 0;
static time_t print_time = 0;

void timer_start(void)
{
	start_time = time(0);
}

void timer_end(void)
{
	time_t end_time = 0, elapsed_seconds = 0;

	time(&end_time);
	elapsed_seconds = difftime(end_time, start_time);
	printf("Elapsed time: %d seconds\n", (int)elapsed_seconds);
	print_time = 0;
}

int timer_progress(void)
{
	time_t end_time = 0;
	int elapsed_seconds = 0;

	if (!print_time)
		print_time = time(0);

	time(&end_time);

	elapsed_seconds = (int)difftime(end_time, print_time);

	if (elapsed_seconds == 1) {
		print_time = 0;
		return 1;
	}
	return 0;
}
/* vim: set ts=8: */
