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
