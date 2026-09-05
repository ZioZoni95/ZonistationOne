#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2025-2026 ZioZoni95
#
# The golden-trace harness.
#
#   tools/golden_trace.sh record [game...]   capture reference traces into traces/
#   tools/golden_trace.sh verify [game...]   re-capture and diff against traces/
#   tools/golden_trace.sh list               show the games this knows about
#
# `record` on a build you trust, `verify` after every change to the CPU, the bus,
# the event scheduler or the timing model. A verify that passes means the machine
# executed the same instructions in the same order with the same register
# contents at the same emulated cycle — which is the claim a boot and a CPI
# reading cannot make, and the claim the LWL/LWR bug needed somebody to be able
# to make (docs/TESTING_PLAN_2026-08-20.md).
#
# ZS1_CD_SYNC=1 is set for every capture and is not optional: without it the
# drive comes back later when the host's file I/O is slow, so the same read lands
# at a different emulated cycle on every run. See cdrom_disc.c.
#
# A failed verify prints the first differing checkpoint. The instruction count on
# that line is the interval the divergence happened in; re-run with a smaller
# ZS1_TRACE_EVERY to narrow it.
set -u

cd "$(dirname "$0")/.." || exit 1

BIN=./ZoniStation_One
BIOS=${ZS1_TRACE_BIOS:-roms/SCPH-7502.BIN}
DIR=traces
STOP=${ZS1_TRACE_STOP:-700000000}
EVERY=${ZS1_TRACE_EVERY:-50000000}
TIMEOUT=${ZS1_TRACE_TIMEOUT:-400}

# Every disc here is PAL, so they all run under the PAL BIOS; a region mismatch
# is rejected exactly as on hardware and would look like a harness failure.
game_path() {
    case "$1" in
        bios)     echo "" ;;
        ace)      echo "games/Ace Combat 2 (Europe).bin" ;;
        crash)    echo "games/Crash Bandicoot 3 - Warped (E) [SCES-01420].bin.ecm" ;;
        dino)     echo "games/Dino Crisis (E) (Track 1) [SLES-02207].bin.ecm" ;;
        monsters) echo "games/Disney-Pixar Monsters & Co. - L'Isola dello Spavento (Italy).bin" ;;
        *)        return 1 ;;
    esac
}
ALL="bios ace crash dino monsters"

# Memory cards are guest state the guest reads back, and the working copies in
# the repository root change every time anybody plays: a save written between a
# record and a verify sends the guest down a different path and the trace
# diverges for a reason that has nothing to do with the code. Each capture gets
# an empty directory instead, so the guest always meets an unformatted card.
MCDIR=$DIR/.memcard

capture() {  # $1 game key, $2 output path
    local g=$1 out=$2 path
    path=$(game_path "$g") || { echo "unknown game '$g'"; return 1; }
    if [ -n "$path" ] && [ ! -f "$path" ]; then
        echo "  $g: SKIP (no image at $path)"
        return 2
    fi
    rm -rf "$MCDIR" && mkdir -p "$MCDIR" || return 1
    if [ -n "$path" ]; then
        ZS1_CD_SYNC=1 ZS1_NO_INPUT=1 ZS1_MEMCARD_DIR="$MCDIR" \
            ZS1_TRACE="$out" ZS1_TRACE_EVERY="$EVERY" ZS1_TRACE_STOP="$STOP" \
            timeout "$TIMEOUT" "$BIN" "$BIOS" --game="$path" >/dev/null 2>&1
    else
        ZS1_CD_SYNC=1 ZS1_NO_INPUT=1 ZS1_MEMCARD_DIR="$MCDIR" \
            ZS1_TRACE="$out" ZS1_TRACE_EVERY="$EVERY" ZS1_TRACE_STOP="$STOP" \
            timeout "$TIMEOUT" "$BIN" "$BIOS" >/dev/null 2>&1
    fi
    local rc=$?
    # The run ends itself once the trace is complete, so a timeout means the
    # machine stopped executing — a hang is a failure, not a slow run.
    if [ $rc -ne 0 ]; then
        echo "  $g: FAIL (exit $rc — timed out or crashed before $STOP instructions)"
        return 1
    fi
    # A window closed by hand also exits 0 and still writes a "# end" line, so
    # the marker alone is not enough: the run has to have reached the instruction
    # count it was asked for. Without this an interrupted capture reports as a
    # divergence, which sends you looking for a bug in the CPU.
    local got
    got=$(sed -n 's/^# end instr=//p' "$out")
    if [ -z "$got" ]; then
        echo "  $g: FAIL (trace incomplete — no end marker)"
        return 1
    fi
    if [ "$got" != "$STOP" ]; then
        echo "  $g: FAIL (run ended early at $got of $STOP instructions — window closed, or the machine stopped)"
        return 1
    fi
    return 0
}

games=${*:2}
[ -z "${games// }" ] && games=$ALL

case "${1:-}" in
record)
    mkdir -p "$DIR"
    for g in $games; do
        capture "$g" "$DIR/$g.trace" && echo "  $g: recorded ($(grep -c '^[0-9]' "$DIR/$g.trace") checkpoints)"
    done
    ;;
verify)
    tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
    fail=0
    for g in $games; do
        if [ ! -f "$DIR/$g.trace" ]; then
            echo "  $g: SKIP (no reference — run 'record' first)"
            continue
        fi
        capture "$g" "$tmp/$g.trace" || { [ $? -eq 1 ] && fail=1; continue; }
        if diff -q "$DIR/$g.trace" "$tmp/$g.trace" >/dev/null; then
            echo "  $g: OK"
        else
            fail=1
            echo "  $g: DIVERGED"
            diff "$DIR/$g.trace" "$tmp/$g.trace" | head -6 | sed 's/^/      /'
        fi
    done
    exit $fail
    ;;
list)
    for g in $ALL; do printf '  %-9s %s\n' "$g" "$(game_path "$g")"; done
    ;;
*)
    sed -n '4,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
