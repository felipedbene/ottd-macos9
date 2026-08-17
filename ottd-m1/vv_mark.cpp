/* vv_mark.cpp — R1-180 Classic ctor-chain bisection marker (auto-generated
 * shape; sorts into the obj glob alphabetically to timestamp ctor progress).
 * GPL v2, part of ottd-macos9. Remove after the hunt. */
#include <cstdio>
__attribute__((constructor)) static void vv_mark_ctor(void)
{
    FILE *f = fopen("premain.txt", "a");
    if (f != nullptr) { fputs("vv\n", f); fclose(f); }
}
