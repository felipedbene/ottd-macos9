#!/bin/bash
GXX=/Users/felipe/ottd-macos9/Retro68-build/toolchain/bin/powerpc-apple-macos-g++
COMPAT=/Users/felipe/ottd-macos9/compat
OTTD=/Users/felipe/ottd-macos9/openttd-13.4
GEN=$OTTD/build-native/generated
FLAGS=(-std=c++17 -DNO_THREADS -DTTD_ENDIAN=TTD_BIG_ENDIAN -include "$COMPAT/libc_compat.h" -Os)
INCS=(-I"$COMPAT" -I"$OTTD/src" -I"$OTTD/src/3rdparty" -I"$OTTD/src/3rdparty/squirrel/include" -I"$OTTD/src/script/api" -I"$GEN" -I"$GEN/script/api")
mkdir -p obj
for t in "$@"; do
  b=$(basename "$t")
  if $GXX "${FLAGS[@]}" "${INCS[@]}" -c "$OTTD/src/$t.cpp" -o "obj/$b.o" 2>"obj/$b.err"; then
    echo "OK    $t"
  else
    echo "FAIL  $t  ($(grep -c error: obj/$b.err) errors)"
  fi
done
