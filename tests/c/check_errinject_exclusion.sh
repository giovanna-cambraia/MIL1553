#!/bin/sh
# tests/c/check_errinject_exclusion.sh

# inspection for LLR-1553-ERR-005: the release build contains no error injection code or data. The certification argument (PSAC 8) is that the
# injector is ABSENT from the certified object code, not merely disabled, so this checks the objects, a linked release image and its link map

#   1. no release source or header includes an errinject header
#   2. the release configuration (every src/c/*.c except main_*.c, built without the test define) compiles, and none of its objects mentions an
#      injector symbol
#   3. a release image linked from those objects, and its link map, contain no injector symbol either
#   4. every file under src/c/errinject/ refuses to compile in a release configuration (#error), so the injector cannot be pulled in by accident

# usage: check_errinject_exclusion.sh <include/c dir> <src/c dir> [compiler]

INC=${1:?include dir}
SRC=${2:?source dir}
CC=${3:-gcc}
FAIL=0
TMPD=$(mktemp -d 2>/dev/null || { mkdir -p /tmp/errinject_excl.$$ && echo /tmp/errinject_excl.$$; })
PATTERN='errinject|error_inject|config_signature|ERRINJECT'

echo "== LLR-1553-ERR-005 inspection =="

# 1. include graph
BAD=$(grep -nE '#[[:space:]]*include[[:space:]]*"(error_inject|config_signature)\.h"' "$SRC"/*.c "$INC"/*.h 2>/dev/null)
if [ -n "$BAD" ]; then
    echo "  [FAIL] a release file includes an errinject header:"; echo "$BAD"; FAIL=1
else
    echo "  [PASS] no release source or header includes an errinject header"
fi

# 2. release objects
OBJS=""
BUILD_OK=1
for f in "$SRC"/*.c; do
    case "$(basename "$f")" in main_*) continue ;; esac
    o="$TMPD/$(basename "$f" .c).o"
    if $CC -std=c11 -I"$INC" -I"$SRC" -c "$f" -o "$o" 2>"$TMPD/err.txt"; then
        OBJS="$OBJS $o"
    else
        echo "  [FAIL] release build does not compile: $f"; cat "$TMPD/err.txt"; BUILD_OK=0; FAIL=1
    fi
done
if [ $BUILD_OK -eq 1 ]; then
    HITS=$(nm $OBJS 2>/dev/null | grep -i -E "$PATTERN")
    if [ -n "$HITS" ]; then
        echo "  [FAIL] injector symbols in release objects:"; echo "$HITS"; FAIL=1
    else
        echo "  [PASS] no injector symbol in any release object ($(echo $OBJS | wc -w | tr -d ' ') objects)"
    fi

    # 3. a linked release image and its map
    cat > "$TMPD/probe_main.c" << 'PROBE'
#include "bus_model.h"
int main(void)
{
    mil1553_bus_model_context_t ctx;
    mil1553_symbols_t s = { 0U, 0U, 0U, 0U, 0U };
    bus_model_init(&ctx);
    return bus_model_transmit(&ctx, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_BC) ? 0 : 1;
}
PROBE
    if $CC -std=c11 -I"$INC" -I"$SRC" "$TMPD/probe_main.c" $OBJS -Wl,-Map="$TMPD/release.map" -o "$TMPD/release_image" 2>"$TMPD/err.txt"; then
        HITS=$( (nm "$TMPD/release_image"; cat "$TMPD/release.map") 2>/dev/null | grep -i -E "$PATTERN")
        if [ -n "$HITS" ]; then
            echo "  [FAIL] injector symbols in the release image or link map:"; echo "$HITS" | head -5; FAIL=1
        else
            echo "  [PASS] no injector symbol in the linked release image or its link map"
        fi
    else
        echo "  [FAIL] release image does not link"; cat "$TMPD/err.txt"; FAIL=1
    fi
fi

# 4. each injector file refuses a release configuration
for f in "$SRC"/errinject/*.c; do
    if $CC -std=c11 -I"$INC" -I"$SRC" -I"$SRC/errinject" -DERRINJECT_HARNESS_TOKEN=1U -c "$f" -o "$TMPD/x.o" 2>"$TMPD/err.txt"; then
        echo "  [FAIL] $(basename "$f") compiled WITHOUT MIL1553_ENABLE_ERROR_INJECTION"; FAIL=1
    elif grep -q "must not be built in a release configuration" "$TMPD/err.txt"; then
        echo "  [PASS] $(basename "$f") refuses to build in a release configuration (#error)"
    else
        echo "  [FAIL] $(basename "$f") failed to compile for some other reason:"; head -3 "$TMPD/err.txt"; FAIL=1
    fi
done

rm -rf "$TMPD"
[ $FAIL -eq 0 ] && echo "Error injection exclusion: PASSED" || echo "Error injection exclusion: FAILED"
exit $FAIL