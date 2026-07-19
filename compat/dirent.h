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

// POSIX <dirent.h> shim for Retro68 (newlib ships a stub that #errors).
// Declarations only for now; the implementation will be backed by the classic
// Mac OS File Manager (PBGetCatInfo / FSpGetCatInfo) in a later mac_dir.cpp seam.
#ifndef OTTD_COMPAT_DIRENT_H
#define OTTD_COMPAT_DIRENT_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
	char d_name[256]; // only d_name is used by OpenTTD's fios.cpp
};

typedef struct DIR DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
void rewinddir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif // OTTD_COMPAT_DIRENT_H
