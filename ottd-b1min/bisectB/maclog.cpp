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

// OpenTTD-on-Mac-OS-9 debug log bridge.
//
// MiniVNC ships its trace over Open Transport UDP, but that build uses Apple's
// Universal Interfaces (CodeWarrior/macg5). The Retro68 toolchain here has the
// OT *libraries* but not the OT *headers*, and hand-writing OT structs risks
// silent ABI corruption in the very tool meant to help us debug. So we use the
// seam we've proven instead: the File Manager. ottd_log() appends to a trace
// file next to the app; when the app is run from the mounted AFP share, that
// file appears on the dev machine and we read the Mac's trace directly.
//
// (A true UDP-to-the-MiniVNC-sink path becomes available for free once the
// Open Transport networking seam is implemented — that's the networking brick.)
#include <cstdio>
#include <cstdarg>

extern "C" {

static FILE *g_log = nullptr;

void ottd_log_init(const char *path)
{
    g_log = fopen(path, "w");
    if (g_log) { fputs("[ottd] log open\n", g_log); fflush(g_log); }
}

void ottd_log(const char *fmt, ...)
{
    if (!g_log) return;
    fputs("[ottd] ", g_log);
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);   // land each line promptly so the share sees partial traces
}

void ottd_log_close(void)
{
    if (g_log) { fputs("[ottd] log close\n", g_log); fclose(g_log); g_log = nullptr; }
}

} // extern "C"
