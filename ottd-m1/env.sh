# M1 build environment. Source this.
export TC=/Users/felipe/ottd-macos9/Retro68-build/toolchain/bin
export GXX=$TC/powerpc-apple-macos-g++
export GCC=$TC/powerpc-apple-macos-gcc
export NM=$TC/powerpc-apple-macos-nm
export LD=$TC/powerpc-apple-macos-ld
export COMPAT=/Users/felipe/ottd-macos9/compat
export OTTD=/Users/felipe/ottd-macos9/openttd-13.4
export GEN=$OTTD/build-native/generated
export M1=/Users/felipe/ottd-macos9/ottd-m1

export CXXDEFS="-std=c++17 -DNO_THREADS -DTTD_ENDIAN=TTD_BIG_ENDIAN -include $COMPAT/libc_compat.h"
export INCS="-I$COMPAT -I$OTTD/src -I$OTTD/src/3rdparty -I$OTTD/src/3rdparty/squirrel/include -I$OTTD/src/script/api -I$GEN -I$GEN/script/api"

# compile one OpenTTD src TU: cxx_src <relative-src-path> <out.o>
# (flags match the prebuilt b1/b2 game objects — no -fno-exceptions to keep ABI identical)
cxx_src() {
  $GXX $CXXDEFS $INCS -Os -c "$OTTD/src/$1" -o "$2"
}
