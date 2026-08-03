#!/usr/bin/env bash
# Rebuild statsd_ot.o from mac-minivnc's statsd.c (Open Transport UDP emitter).
#
# statsd.c is NOT part of any cmake target: it needs Apple's Universal
# Interfaces (<OpenTransport.h>, in Retro68-build/toolchain/otsdk/CIncludes),
# while add_application() builds against multiversal, which has no OT. So the
# .o is checked in and every build links the PREBUILT object.
#
# The trap: editing statsd.c alone changes nothing -- the stale .o keeps
# linking. Run this after ANY statsd.c edit, then rebuild the app.
#
#   -DSTATSD_OT  force the real OT path (also auto-selected on __ppc__)
#   -std=gnu11   otsdk/MacTypes.h enumerates `false`, which is a keyword from C23
#   -w           the SDK headers use #cpu()/#system() assertions gcc warns about
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
tc="$root/Retro68-build/toolchain"
gcc="$tc/bin/powerpc-apple-macos-gcc"
src="$root/mac-minivnc/mac-cpp-source/statsd.c"

"$gcc" -c -O2 -std=gnu11 -w -DSTATSD_OT \
  -I"$tc/otsdk/CIncludes" "$src" -o "$here/statsd_ot.o"
# ottd-b2/ is the copy the b2/m1/r1 link lines actually reference.
cp "$here/statsd_ot.o" "$root/ottd-b2/statsd_ot.o"
echo "statsd_ot.o rebuilt -> ottd-b1/ and ottd-b2/"
"$tc/bin/powerpc-apple-macos-nm" "$here/statsd_ot.o" | grep ' T \.statsd_'
