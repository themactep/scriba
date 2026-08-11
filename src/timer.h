/*
 * timer.h
 * Copyright (C) 2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __TIMER_H__
#define __TIMER_H__

void timer_start(void);
void timer_end(void);

/*
 * Print a rate-limited progress line to stdout.
 * Rate limit: ~1 update per second. Uses \r + ANSI clear-to-EOL.
 * Suppressed in WASM builds (__EMSCRIPTEN__).
 * msg:   label ("Erase", "Read", "Written")
 * current, total: byte counts; percentage computed from these
 */
void timer_progress(const char *msg, unsigned long current, unsigned long total);

#endif /* __TIMER_H__ */
