#!/usr/bin/env bash
# Rebuild netlog.o from netlog.c.
#
# netlog.c is NOT part of any cmake target: it needs Apple's Universal
# Interfaces (<OpenTransport.h>, in Retro68-build/toolchain/otsdk/CIncludes),
# while add_application() builds against multiversal, which has no OT. So the
# .o is checked in and every build links the PREBUILT object.
#
# The trap: editing netlog.c alone changes nothing -- the stale .o keeps
# linking, and if you added a symbol you get "undefined reference to
# .ottd_log_set_tag" at link time (or, worse, silently old behaviour). Run this
# after ANY netlog.c edit, then rebuild the app.
#
#   -std=gnu11  otsdk/MacTypes.h enumerates `false`, which is a keyword from C23
#   -w          the SDK headers use #cpu()/#system() assertions gcc warns about
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tc="$here/../Retro68-build/toolchain"
gcc="$tc/bin/powerpc-apple-macos-gcc"

"$gcc" -c -O2 -std=gnu11 -w -I"$tc/otsdk/CIncludes" "$here/netlog.c" -o "$here/netlog.o"
# ottd-b2/ is the copy the b2/m1/r1 link lines actually reference (${B2}/netlog.o).
cp "$here/netlog.o" "$here/../ottd-b2/netlog.o"
echo "netlog.o rebuilt -> ottd-b1/ and ottd-b2/"
"$tc/bin/powerpc-apple-macos-nm" "$here/netlog.o" | grep ' T \.ottd_log'
