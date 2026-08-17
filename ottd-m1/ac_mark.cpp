/* ac_mark.cpp — R1-183 Classic ctor-chain FINAL bisection marker. GPL v2,
 * part of ottd-macos9. Remove after the hunt. */
#include <cstdio>
__attribute__((constructor)) static void ac_mark_ctor(void)
{
    FILE *f = fopen("premain.txt", "a");
    if (f != nullptr) { fputs("ac\n", f); fclose(f); }
}
