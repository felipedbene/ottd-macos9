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

/* macdir.c — classic Mac OS File Manager backing for POSIX opendir/readdir/closedir
 * plus a minimal stat() shim. Seam #3 of the OpenTTD -> Mac OS 9 port: this is what
 * fios.cpp (the savegame / scenario browser) and the data-file scan call to enumerate
 * a directory and to test dir-vs-file. Retro68's newlib ships a <dirent.h> that #errors,
 * so the game side sees compat/dirent.h; the bodies live here, against the File Manager
 * (PBGetCatInfo indexed enumeration + by-name resolution). <sys/stat.h> from newlib is
 * usable as-is (struct stat / S_ISDIR / S_IFDIR), so we just implement the stat() body.
 *
 * v2 adds: arbitrary path -> dirID resolution, a stat()/S_ISDIR shim, and a conservative
 * POSIX('/')  <->  HFS(':') path translator. Compile-verified for PowerPC/Retro68 only;
 * runtime behaviour of the by-name PBGetCatInfo resolution must still be checked on real
 * hardware. */
#include <Files.h>
#include <sys/stat.h>
#include <string.h>

extern void ottd_log(const char *fmt, ...);

/* Layout MUST match compat/dirent.h (the header OpenTTD's game TUs include). */
struct dirent { char d_name[256]; };

typedef struct DIR {
    short vRefNum;
    long  dirID;
    short index;        /* 1-based ioFDirIndex cursor */
    struct dirent ent;
} DIR;

static DIR  g_dir;      /* single active handle (OpenTTD scans one dir at a time) */
static char g_busy;

/* ioFlAttrib bit 4 (0x10) set => the catalog item is a directory, not a file. */
#define kFileMgrDirBit 0x10

/* Default working directory = the app's folder (where relative fopen resolves). */
static int default_dir(short *vref, long *dir)
{
    WDPBRec pb;
    memset(&pb, 0, sizeof(pb));
    if (PBHGetVolSync(&pb) != noErr) return -1;
    *vref = pb.ioWDVRefNum;
    *dir  = pb.ioWDDirID;
    return 0;
}

/* Names that mean "the default working directory itself". OpenTTD hands us "." for the
 * base scan; "", ":" and "/" are treated the same defensively. */
static int is_default_name(const char *name)
{
    if (name == (const char *)0) return 1;
    if (name[0] == '\0') return 1;
    if (name[0] == '.' && name[1] == '\0') return 1;
    if (name[0] == ':' && name[1] == '\0') return 1;
    if (name[0] == '/' && name[1] == '\0') return 1;
    return 0;
}

/* Translate a POSIX-ish path into an HFS partial pathname (Str255), always relative to
 * the base directory passed to PBGetCatInfo via ioDirID.
 *
 * Assumptions / conservative rules (OpenTTD only ever hands us base-relative paths built
 * from its own known roots, so we do NOT attempt full HFS "Volume:folder" resolution):
 *   - A leading "./" is stripped.
 *   - Any leading '/' is stripped: POSIX-absolute paths are NOT supported and are treated
 *     as relative to the default dir (documented limitation; revisit if fios ever feeds an
 *     absolute path).
 *   - Every '/' becomes ':'. Runs of '/' collapse; a trailing '/' is dropped.
 *   - The result always begins with a single ':' so the File Manager reads it as a
 *     partial pathname relative to ioDirID (":name" = item in dir, ":a:b" = b in a in dir).
 * out must be a Str255 (256 bytes); out[0] receives the Pascal length byte. */
static void posix_to_hfs(const char *posix, Str255 out)
{
    const char *p = posix;
    int n = 0;                          /* chars written so far (excludes length byte) */

    if (p[0] == '.' && p[1] == '/') p += 2;
    while (*p == '/') p++;              /* drop leading slashes */

    out[++n] = ':';                     /* force relative-to-ioDirID interpretation */
    while (*p != '\0' && n < 255) {
        char c = *p++;
        if (c == '/') {
            if (out[n] == ':') continue;   /* collapse "//" and avoid "::" */
            c = ':';
        }
        out[++n] = (unsigned char)c;
    }
    while (n > 1 && out[n] == ':') n--;    /* strip trailing separators */
    out[0] = (unsigned char)n;
}

/* Resolve a directory name to (vRefNum, dirID). Returns 0 on success, -1 if the item does
 * not exist or is not a directory. Default names short-circuit to PBHGetVolSync's dir. */
static int resolve_dir(const char *name, short *vref, long *dir)
{
    short dvref; long ddir;
    CInfoPBRec pb;
    Str255 path;

    if (default_dir(&dvref, &ddir) != 0) return -1;
    if (is_default_name(name)) { *vref = dvref; *dir = ddir; return 0; }

    posix_to_hfs(name, path);
    memset(&pb, 0, sizeof(pb));
    pb.dirInfo.ioNamePtr   = path;
    pb.dirInfo.ioVRefNum   = dvref;
    pb.dirInfo.ioFDirIndex = 0;         /* 0 => resolve by ioNamePtr (not indexed) */
    pb.dirInfo.ioDrDirID   = ddir;      /* base dir for the partial pathname (input) */
    if (PBGetCatInfoSync(&pb) != noErr) return -1;
    if (!(pb.hFileInfo.ioFlAttrib & kFileMgrDirBit)) return -1;  /* it's a file */
    *vref = pb.dirInfo.ioVRefNum;
    *dir  = pb.dirInfo.ioDrDirID;       /* on output: the directory's own ID */
    return 0;
}

DIR *opendir(const char *name)
{
    short vref; long dir;
    if (g_busy) return (DIR *)0;
    if (resolve_dir(name, &vref, &dir) != 0) return (DIR *)0;
    g_dir.vRefNum = vref; g_dir.dirID = dir; g_dir.index = 1;
    g_busy = 1;
    return &g_dir;
}

struct dirent *readdir(DIR *d)
{
    CInfoPBRec pb;
    Str255 nm;
    int len;
    if (d == (DIR *)0) return (struct dirent *)0;
    memset(&pb, 0, sizeof(pb));
    nm[0] = 0;
    pb.hFileInfo.ioNamePtr   = nm;
    pb.hFileInfo.ioVRefNum   = d->vRefNum;
    pb.hFileInfo.ioFDirIndex = d->index;
    pb.hFileInfo.ioDirID     = d->dirID;   /* reset each call: PBGetCatInfo overwrites it */
    if (PBGetCatInfoSync(&pb) != noErr) return (struct dirent *)0;  /* fnfErr = past last entry */
    d->index++;
    len = nm[0]; if (len > 255) len = 255;
    memcpy(d->ent.d_name, &nm[1], (size_t)len);
    d->ent.d_name[len] = '\0';
    return &d->ent;
}

int  closedir(DIR *d) { if (d) g_busy = 0; return 0; }
void rewinddir(DIR *d) { if (d) d->index = 1; }

/* Minimal stat(): populate st_mode (S_IFDIR/S_IFREG) and st_size. OpenTTD's fios uses
 * stat()+S_ISDIR to classify catalog entries. Matches newlib's <sys/stat.h> prototype
 * (the top-level restrict qualifiers are dropped from the function type, so this is a
 * compatible redefinition; newlib's own stat.o is simply not pulled from libc). */
int stat(const char *path, struct stat *buf)
{
    short dvref; long ddir;
    CInfoPBRec pb;
    Str255 hpath;

    if (buf == (struct stat *)0) return -1;
    if (default_dir(&dvref, &ddir) != 0) return -1;

    memset(&pb, 0, sizeof(pb));
    memset(buf, 0, sizeof(*buf));

    if (is_default_name(path)) {
        /* Info about the default directory itself: negative ioFDirIndex ignores
         * ioNamePtr and reports on ioDrDirID. */
        pb.dirInfo.ioNamePtr   = (StringPtr)0;
        pb.dirInfo.ioVRefNum   = dvref;
        pb.dirInfo.ioFDirIndex = -1;
        pb.dirInfo.ioDrDirID   = ddir;
    } else {
        posix_to_hfs(path, hpath);
        pb.hFileInfo.ioNamePtr   = hpath;
        pb.hFileInfo.ioVRefNum   = dvref;
        pb.hFileInfo.ioFDirIndex = 0;      /* resolve by ioNamePtr */
        pb.hFileInfo.ioDirID     = ddir;   /* base dir for the partial pathname */
    }
    if (PBGetCatInfoSync(&pb) != noErr) return -1;

    if (pb.hFileInfo.ioFlAttrib & kFileMgrDirBit) {
        buf->st_mode = (mode_t)(S_IFDIR | 0755);
        buf->st_size = 0;
    } else {
        buf->st_mode = (mode_t)(S_IFREG | 0644);
        buf->st_size = (off_t)pb.hFileInfo.ioFlLgLen;   /* data-fork logical length */
    }
    return 0;
}

/* Self-test wired from main(): enumerate the app folder to the UDP sink and exercise the
 * v2 additions (default-dir stat, a bogus-path opendir that must fail). */
void macdir_selftest(void)
{
    DIR *d;
    struct dirent *e;
    struct stat sb;
    int n = 0;

    d = opendir(".");
    if (d == (DIR *)0) { ottd_log("macdir: opendir failed"); return; }
    ottd_log("macdir: app dir vRef=%d dirID=%ld", (int)g_dir.vRefNum, g_dir.dirID);
    while ((e = readdir(d)) != (struct dirent *)0 && n < 40) {
        ottd_log("macdir: [%d] %s", n, e->d_name);
        n++;
    }
    closedir(d);
    ottd_log("macdir: %d entries listed", n);

    /* stat the default dir: must classify as a directory. */
    if (stat(".", &sb) == 0)
        ottd_log("macdir: stat('.') mode=0%o isdir=%d size=%ld",
                 (unsigned)sb.st_mode, (int)(S_ISDIR(sb.st_mode) ? 1 : 0),
                 (long)sb.st_size);
    else
        ottd_log("macdir: stat('.') FAILED");

    /* A path that should not exist: opendir must return NULL (proves by-name resolve
     * rejects non-directories / missing items rather than silently listing default). */
    if (opendir("no_such_dir_zz") == (DIR *)0)
        ottd_log("macdir: opendir('no_such_dir_zz') correctly NULL");
    else {
        ottd_log("macdir: opendir('no_such_dir_zz') UNEXPECTEDLY non-NULL");
        closedir(&g_dir);
    }
}
