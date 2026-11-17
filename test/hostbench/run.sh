#!/bin/bash
# Run the headless build with a given set of console commands.
#
#   ./run.sh "timedemo demo1"                         time a demo (wall clock, noisy)
#   PROFILE=1 ./run.sh "timedemo demo1"               instruction count under callgrind (deterministic)
#   QUITFRAMES=150 ./run.sh "map e1m5\n+lookup"       stand at a spawn point looking up (sky)
#   ./run.sh "gl_polyblend 0\ntimedemo demo1"         any cvars go before the demo
#   BIN=out/fq_exp DIR=exp ./run.sh "..."             another binary / its own run directory
#   DUMPEVERY=60 DUMPPREFIX=before ./run.sh "..."     PPM screenshots, for diffing two builds
#   SOUND=1 AUDIODUMP=/tmp/a.raw ./run.sh "..."       run the sound mixer too and save its output
#   FRAMEHASH=/tmp/a.txt ./run.sh "..."              one hash per frame; diff two builds' files
#
# Put pak0.pak (shareware is fine) in run/id1/ first. The commands go into
# id1/autoexec.cfg; anything after the first argument is passed to the engine
# as command line arguments (e.g. -nomtex).
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${BIN:-out/fq}; D=$HERE/run${DIR:+_$DIR}
[ -f "$HERE/run/id1/pak0.pak" ] || { echo "put pak0.pak in $HERE/run/id1/"; exit 1; }
mkdir -p "$D/id1"; [ -e "$D/id1/pak0.pak" ] || ln -s "$HERE/run/id1/pak0.pak" "$D/id1/pak0.pak"
printf "%b\n" "$1" > "$D/id1/autoexec.cfg"; shift
cd "$D"
SND="-nosound"; [ -n "$SOUND" ] && SND=""   # SOUND=1 with AUDIODUMP=file: see sdl_stub.c
ARGS="$SND -nocdaudio -width ${W:-320} -height ${H:-240} $*"
export FAKECLOCK=${FAKECLOCK:-50}
if [ -n "$PROFILE" ]; then
  valgrind --tool=callgrind --callgrind-out-file=callgrind.out "$HERE/$BIN" $ARGS > log.txt 2>&1
  echo "$(grep -E 'frames|quit after' log.txt | tail -1) | instructions: $(grep Collected log.txt | awk '{print $NF}')"
  echo "profile: callgrind_annotate --inclusive=yes $D/callgrind.out | less"
else
  time "$HERE/$BIN" $ARGS
fi
