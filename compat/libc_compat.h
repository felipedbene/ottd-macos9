// Small libc shims for functions absent from Retro68's newlib but assumed by OpenTTD.
// Force-included on the command line (-include compat/libc_compat.h).
#ifndef OTTD_LIBC_COMPAT_H
#define OTTD_LIBC_COMPAT_H

#include <cstddef>

// alloca — OpenTTD's AllocaM macro; map to the GCC builtin (no <alloca.h> in newlib).
#ifndef alloca
#define alloca __builtin_alloca
#endif

// POSIX <strings.h> case-insensitive compares — not in newlib.
static inline int ottd_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static inline int strcasecmp(const char *a, const char *b)
{
	while (*a && *b) {
		int d = ottd_tolower((unsigned char)*a) - ottd_tolower((unsigned char)*b);
		if (d != 0) return d;
		++a; ++b;
	}
	return ottd_tolower((unsigned char)*a) - ottd_tolower((unsigned char)*b);
}

static inline int strncasecmp(const char *a, const char *b, size_t n)
{
	while (n-- > 0) {
		int d = ottd_tolower((unsigned char)*a) - ottd_tolower((unsigned char)*b);
		if (d != 0) return d;
		if (*a == 0) return 0;
		++a; ++b;
	}
	return 0;
}

#endif // OTTD_LIBC_COMPAT_H
