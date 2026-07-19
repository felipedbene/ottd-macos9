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
#include <cstring>

extern "C" {

// Reliable trace across a CRASH: on classic Mac + AFP, fflush only reaches the File
// Manager cache (write-behind), so a Type-2 loses the tail. fclose() fully commits to
// the server, so we open-append-close PER LINE. Slower, but every committed line is on
// the share even if the very next statement bombs.
static char g_path[256];

void ottd_log_init(const char *path)
{
    std::strncpy(g_path, path, sizeof(g_path) - 1);
    g_path[sizeof(g_path) - 1] = '\0';
    FILE *f = fopen(g_path, "w");
    if (f) { fputs("[ottd] log open\n", f); fclose(f); }
}

void ottd_log(const char *fmt, ...)
{
    if (g_path[0] == '\0') return;
    FILE *f = fopen(g_path, "a");
    if (!f) return;
    fputs("[ottd] ", f);
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);       // commit to the AFP server now, so a crash can't swallow this line
}

void ottd_log_close(void)
{
    if (g_path[0] == '\0') return;
    FILE *f = fopen(g_path, "a");
    if (f) { fputs("[ottd] log close\n", f); fclose(f); }
    g_path[0] = '\0';
}

} // extern "C"
