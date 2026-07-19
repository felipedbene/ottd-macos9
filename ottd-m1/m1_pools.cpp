/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from and/or built against OpenTTD, Copyright (c) the OpenTTD
 * Development Team. Modified for the Mac OS 9 / PowerPC port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

// M1: superseded at build 6. The real town_cmd.cpp is now compiled, so it
// provides _town_pool, INSTANTIATE_POOL_METHODS(Town), Town::~Town and
// Town::PostDestructor for real. This TU is intentionally empty (kept in the
// build to avoid churning CMakeLists/build.sh object lists).
