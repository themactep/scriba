/*
 * web/src/main.js — Scriba web frontend entry point.
 * Copyright (C) 2025-2026 Josh at WLTechBlog <wltechblog@wanderlounge.net>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import 'bootstrap/dist/css/bootstrap.min.css'
import 'bootstrap-icons/font/bootstrap-icons.css'
import * as bootstrap from 'bootstrap'
import './app.js'

document.querySelectorAll('[data-bs-toggle="tooltip"]').forEach(
  el => new bootstrap.Tooltip(el)
)
