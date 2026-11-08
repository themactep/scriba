/*
 * flash_layout.h - identify the partitions of a raw SPI NOR flash image
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __FLASH_LAYOUT_H__
#define __FLASH_LAYOUT_H__

#include <stddef.h>

#define FL_MAX_PARTS 32

struct fl_part {
  unsigned long start;
  unsigned long size;
  char name[32];  /* from mtdparts; empty when only detected */
  char what[160]; /* what the partition holds */
};

struct fl_layout {
  int nparts;
  struct fl_part parts[FL_MAX_PARTS];
  char source[160]; /* where the boundaries came from */
  char note[200];   /* e.g. not a raw flash image */
};

/* Analyze an image of `len` bytes from a flash of `flash_size` bytes (pass
 * len when unknown). Boundaries come from an mtdparts= string (in a U-Boot
 * environment, else anywhere in the image), otherwise from detected
 * headers; each partition's contents are identified from its headers.
 * Returns the number of partitions found. */
int flash_layout_analyze(const unsigned char *img, unsigned long len,
                         unsigned long flash_size, struct fl_layout *out);

/* Print an analyzed layout, one partition per line. */
void flash_layout_print(const struct fl_layout *l);

/* An analyzed layout as JSON, for the web UI:
 * {"source":"...","note":"...","parts":[{"start":N,"size":N,"name":"...",
 * "what":"..."},...]}. Returns the length written, or -1 (and an empty
 * string) when it does not fit in n bytes. */
int flash_layout_json(const struct fl_layout *l, char *out, size_t n);

/* Analyze and print. */
void flash_layout_report(const unsigned char *img, unsigned long len,
                         unsigned long flash_size);

#endif /* __FLASH_LAYOUT_H__ */
