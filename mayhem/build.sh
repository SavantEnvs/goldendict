#!/usr/bin/env bash
#
# mayhem/build.sh — build GoldenDict's dictionary engine + one libFuzzer harness per dictionary format.
#
# The full GoldenDict app needs Qt WebKit, which Debian trixie (the base image) no longer packages, so
# this builds the dictionary engine — every local dictionary-format loader plus the index/storage code
# they share (mayhem/engine.pro lists the sources; all of them are upstream files, built unmodified) —
# as a static library with qmake, then links the harnesses against it. GUI-only sources (main window,
# article view, ...) are not built; none of them parses dictionary files.
#
#   mayhem/build/fuzz  engine with $SANITIZER_FLAGS + SanCov  -> /mayhem/fuzz_<fmt> (+ -standalone)
#   mayhem/build/test  engine with normal flags               -> mayhem/build/test/gd_query (test.sh)
# Both build dirs are under build/, which upstream's .gitignore already ignores.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
M="$SRC/mayhem"

QT_PKGS="Qt5Widgets Qt5Gui Qt5Core Qt5Xml Qt5Network Qt5Svg Qt5Sql"
QT_CFLAGS="$(pkg-config --cflags $QT_PKGS)"
QT_LIBS="$(pkg-config --libs $QT_PKGS)"
SYS_LIBS="-lz -lbz2 -llzo2 -llzma -lzstd -lvorbisfile -lvorbis -logg -lhunspell -ltiff -lX11 -lpthread -lm"
DEFS="-DMAKE_ZIM_SUPPORT -DNO_EPWING_SUPPORT -DNDEBUG -DNO_GD_DPRINTF"

# UBSan's `alignment` check is relaxed (ASan and every other UBSan check stay on and halting): the
# Babylon reader decodes every entry with qFromBigEndian(*reinterpret_cast<quint16 *>(data + pos)) at
# an arbitrary byte offset (bgl_babylon.cc), so roughly every second entry of any real .bgl aborted on
# a benign unaligned load and the fuzz_bgl target could not get past its first headword.
FUZZ_SAN_FLAGS="$SANITIZER_FLAGS"
case "$SANITIZER_FLAGS" in *undefined*) FUZZ_SAN_FLAGS="$SANITIZER_FLAGS -fno-sanitize=alignment" ;; esac

# SanCov on the engine (not just the harness) so the fuzzer sees the parsers' coverage.
COV_FLAGS=""
case "$LIB_FUZZING_ENGINE" in *fuzzer*) COV_FLAGS="-fsanitize=fuzzer-no-link" ;; esac

build_engine() { # <dir> <extra c/c++ flags>
  local dir="$1" flags="$2"
  mkdir -p "$dir"
  ( cd "$dir"
    qmake "$M/engine.pro" CONFIG+=release \
      QMAKE_CC="$CC" QMAKE_CXX="$CXX" QMAKE_LINK="$CXX" \
      "QMAKE_CFLAGS_RELEASE=-O1" "QMAKE_CXXFLAGS_RELEASE=-O1" \
      "QMAKE_CFLAGS+=$flags" "QMAKE_CXXFLAGS+=$flags" "DEFINES+=NDEBUG NO_GD_DPRINTF"
    make -j"$MAYHEM_JOBS" )
}

# 1) Sanitized + coverage-instrumented engine.
build_engine "$M/build/fuzz" "$FUZZ_SAN_FLAGS $COV_FLAGS $DEBUG_FLAGS"
ENGINE="$M/build/fuzz/libgdengine.a"
INC="-I$SRC -I$M/build/fuzz/ui $QT_CFLAGS"

# 2) Harnesses: libFuzzer binary + standalone reproducer per format.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -fPIC -c "$M/lsan_off.cc" -o "$M/build/fuzz/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$M/build/fuzz/standalone_main.o"
for fmt in mdx bgl dsl zim slob stardict xdxf aard sdict gls dictd lsa zipsounds; do
  FMT="$(echo "$fmt" | tr a-z A-Z)"
  obj="$M/build/fuzz/fuzz_$fmt.o"
  $CXX $FUZZ_SAN_FLAGS $COV_FLAGS $DEBUG_FLAGS -fPIC -std=gnu++11 $DEFS -DGD_FMT_$FMT $INC \
    -c "$M/harness/fuzz_dict.cc" -o "$obj"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$obj" "$M/build/fuzz/lsan_off.o" \
    "$ENGINE" $QT_LIBS $SYS_LIBS -o "$SRC/fuzz_$fmt"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$M/build/fuzz/standalone_main.o" "$obj" "$M/build/fuzz/lsan_off.o" \
    "$ENGINE" $QT_LIBS $SYS_LIBS -o "$SRC/fuzz_$fmt-standalone"
done

# 3) Functional-oracle build: engine + gd_query with the project's normal flags.
build_engine "$M/build/test" "$COVERAGE_FLAGS"
$CXX -O1 -fPIC -std=gnu++11 $COVERAGE_FLAGS $DEFS -I"$SRC" -I"$M/build/test/ui" $QT_CFLAGS \
  "$M/harness/gd_query.cc" "$M/build/test/libgdengine.a" $QT_LIBS $SYS_LIBS \
  -o "$M/build/test/gd_query"
