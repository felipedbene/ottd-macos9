/* ac_mark.cpp — R1-180 Classic ctor-chain bisection marker (auto-generated
 * shape; sorts into the obj glob alphabetically to timestamp ctor progress).
 * R1-185: logs MaxBlock/FreeMem to test ctor-phase heap exhaustion on Classic.
 * GPL v2, part of ottd-macos9. Remove after the hunt. */
#include <cstdio>
extern "C" long b2_maxblock(void);
extern "C" long b2_freemem(void);
__attribute__((constructor)) static void ac_mark_ctor(void)
{
    FILE *f = fopen("premain.txt", "a");
    if (f != nullptr) { fprintf(f, "ac mb=%ld fm=%ld\n", b2_maxblock(), b2_freemem()); fclose(f); }
}
