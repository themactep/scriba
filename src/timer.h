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

/*
 * Print the final "<msg> 100% [current] of [total] bytes" line of an
 * operation. Suppressed in WASM builds: the web UI calls the flash layers once
 * per 64 KB chunk, so a per-call completion line would print once per chunk;
 * the UI logs overall progress itself.
 */
void timer_done(const char *msg, unsigned long current, unsigned long total);

#endif /* __TIMER_H__ */
