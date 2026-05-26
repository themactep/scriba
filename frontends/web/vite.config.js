/*
 * web/vite.config.js — Vite build config for scriba web frontend.
 * Copyright (C) 2025-2026 Josh at WLTechBlog <wltechblog@wanderlounge.net>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import { defineConfig } from 'vite'

export default defineConfig({
  base: './',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
})
