#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# The oracle: mayhem/build.sh built gd_query (GoldenDict's dictionary engine, normal flags). Each test
# loads a committed sample dictionary (made by mayhem/tools/mkseeds.py) through the format's
# makeDictionaries() and checks the article HTML GoldenDict renders for a headword.
Q="$SRC/mayhem/build/test/gd_query"
T="$SRC/mayhem/tests"
[ -x "$Q" ] || { echo "gd_query missing — mayhem/build.sh did not build it" >&2; emit_ctrf gd_query 0 1; exit 1; }

# The StarDict sample's article file is committed as fruits.dict.bin (a *.dict name reads as a fuzzing
# dictionary to tooling); stage the three files under their real names in a scratch dir.
SD="$(mktemp -d)"; trap 'rm -rf "$SD"' EXIT
cp "$T/stardict/fruits.ifo" "$T/stardict/fruits.idx" "$SD/"
cp "$T/stardict/fruits.dict.bin" "$SD/fruits.dict"

passed=0 failed=0
# check <name> <expected substring> <gd_query args...>
check() {
  local name="$1" want="$2"; shift 2
  local got
  got="$("$Q" "$@" 2>&1)"
  if printf '%s' "$got" | grep -qF -- "$want"; then
    passed=$((passed + 1)); echo "PASS $name"
  else
    failed=$((failed + 1)); echo "FAIL $name: expected '$want' in:"; printf '%s\n' "$got" | head -20
  fi
}

check dsl-name      'name: Mayhem Fruits'                                   dsl apple "$T/fruits.dsl"
check dsl-count     'articles: 3'                                           dsl apple "$T/fruits.dsl"
check dsl-article   'A round <b class="dsl_b">fruit</b> of the apple tree.' dsl apple "$T/fruits.dsl"
check dsl-ref       'href="gdlookup://localhost/banana">banana</a>'         dsl apple "$T/fruits.dsl"
check dsl-missing   'article: <none>'                                       dsl zebra "$T/fruits.dsl"
check xdxf-article  'A round fruit of the apple tree.'                      xdxf apple "$T/fruits.xdxf"
# (QDom writes attributes in hash order, which varies per run, so match one attribute.)
check xdxf-kref     'href="bword:banana"'                                   xdxf apple "$T/fruits.xdxf"
check stardict-art  '<div class="sdct_h">A small red stone fruit.</div>'    stardict cherry "$SD/fruits.ifo"
check stardict-name 'name: Mayhem Fruits StarDict'                          stardict cherry "$SD/fruits.ifo"
check mdx-name      'name: Mayhem Fruits MDX'                               mdx banana "$T/fruits.mdx"
check mdx-article   '<div class="mdict">A long yellow fruit.'               mdx banana "$T/fruits.mdx"
check mdx-words     'words: 3'                                              mdx banana "$T/fruits.mdx"
check gls-article   'title="apple-alt">apple</div>A round <b>fruit</b>'     gls apple "$T/fruits.gls"
check bgl-name      'name: Mayhem Fruits BGL'                               bgl banana "$T/fruits.bgl"
check bgl-article   '<h3>Banana</h3><div><span class="bglpos">n.</span> A long yellow fruit.</div>' bgl banana "$T/fruits.bgl"
check bgl-alternate '<h3>apples</h3><div>Plural of apple.</div>'            bgl pomes "$T/fruits.bgl"

emit_ctrf gd_query "$passed" "$failed"
