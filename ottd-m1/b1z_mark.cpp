/* b1z_mark.cpp — R1-183 Classic ctor-chain FINAL bisection marker. GPL v2,
 * part of ottd-macos9. Remove after the hunt. */
#include <cstdio>
__attribute__((constructor)) static void b1z_mark_ctor(void)
{
    FILE *f = fopen("premain.txt", "a");
    if (f != nullptr) { fputs("b1z\n", f); fclose(f); }
}
