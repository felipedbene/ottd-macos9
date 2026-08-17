/* cb_mark.cpp — R1-181 Classic ctor-chain bisection marker (sorts into the
 * obj glob alphabetically). GPL v2, part of ottd-macos9. Remove after hunt. */
#include <cstdio>
__attribute__((constructor)) static void cb_mark_ctor(void)
{
    FILE *f = fopen("premain.txt", "a");
    if (f != nullptr) { fputs("cb\n", f); fclose(f); }
}
