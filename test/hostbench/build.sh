#!/bin/bash
# Build FitzQuake for the host as a headless 32-bit program linked against
# NovaMesa, for profiling. Nothing here is used by the Amiga build.
#
#   ./build.sh                      -> out/fq
#   NOVAMESA=/path/to/NovaMesa TAG=test ./build.sh    -> out/fq_test
#   FQSRC=/path/to/other/src TAG=exp ./build.sh       build a modified copy of the engine
#   CC="m68k-linux-gnu-gcc -m68060 -fno-optimize-sibling-calls" LDFLAGS=-static TAG=m060 ./build.sh
#                                   68k code, to run under NovaMesa's test/emu68model (MODEL=1 run.sh)
#
# Needs gcc with 32-bit support (Ubuntu: gcc-multilib). Quake's progs and
# texture code store pointers in ints, so it must be a 32-bit build.
# -U__i386__ stops quakedef.h selecting the x86 assembler routines.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
FQ=${FQSRC:-$HERE/../../src}
NM=${NOVAMESA:-$HERE/../../../NovaMesa}
TAG=${TAG:-host}
CC=${CC:-gcc -m32}
[ -d "$NM/src/swrast" ] || { echo "NovaMesa not found at $NM (set NOVAMESA=...)"; exit 1; }
HB=$NM/test/hostbench; SRC=$NM/src
MOBJ=$HERE/out/obj_mesa_$TAG; QOBJ=$HERE/out/obj_fq_$TAG
MFLAGS="-O2 -g -fno-strict-aliasing -w -std=gnu99 $MESA_CFLAGS -include $HB/stub/libraries/Picasso96.h -I$HB/stub -I$SRC -I$SRC/amiga"
# __TIME__/__DATE__ are pinned because the engine prints them on the console,
# which would make the first frames of any two builds differ.
QFLAGS="-O2 -g -fno-strict-aliasing -fno-optimize-sibling-calls -w -std=gnu89 -fcommon -U__i386__ -Wno-builtin-macro-redefined -D__TIME__=\"00:00:00\" -D__DATE__=\"hostbench\" $CFLAGS -I$HERE/stub -I$HB/stub"
mkdir -p "$MOBJ" "$QOBJ"
for f in $SRC/*.c $SRC/swrast/*.c $SRC/swrast_setup/*.c $SRC/tnl/*.c $SRC/math/*.c $SRC/array_cache/*.c $SRC/amiga/*.c; do
  b=$(basename "$f" .c); case $b in m_debug_*) continue;; esac
  [ "$MOBJ/$b.o" -nt "$f" ] && continue
  $CC -c $MFLAGS "$f" -o "$MOBJ/$b.o" &
  while [ "$(jobs -r | wc -l)" -ge "$(nproc)" ]; do sleep 0.05; done
done
wait
$CC -c $MFLAGS -I$HERE/stub -I$NM/test/emu68model $HERE/sdl_stub.c -o $QOBJ/sdl_stub.o
$CC -c $QFLAGS $HERE/host_main.c -o $QOBJ/host_main.o
for f in $FQ/*.c; do
  b=$(basename "$f" .c); case $b in amiga_main|sys_file_amiga) continue;; esac
  [ "$QOBJ/$b.o" -nt "$f" ] && continue
  $CC -c $QFLAGS "$f" -o "$QOBJ/$b.o" &
  while [ "$(jobs -r | wc -l)" -ge "$(nproc)" ]; do sleep 0.05; done
done
wait
OUT=$HERE/out/fq; [ "$TAG" = host ] || OUT=$HERE/out/fq_$TAG
$CC -g "$QOBJ"/*.o "$MOBJ"/*.o -lm -Wl,--wrap=Con_Printf $LDFLAGS -o "$OUT"
echo "built $OUT"
