#!/bin/sh
# tests/c/check_bm_isolation.sh

# inspection for LLR-1553-BM-003: bm_monitor can reach no transmit-family function of the bus model (no call, no reference).

#   1. bm_monitor.c / bm_monitor.h must not include bus_model.h or bus_model_internal.h (the headers that declare transmit and channel
#      control); they must use bus_model_monitor_if.h
#   2. The compiled bm_monitor object must have no undefined reference to any bus model function other than the two receive-side ones it is allowed

# usage: check_bm_isolation.sh <include/c dir> <src/c dir> [compiler]

INC=${1:?include dir}
SRC=${2:?source dir}
CC=${3:-gcc}
FAIL=0
TMP=$(mktemp 2>/dev/null || echo /tmp/bm_isolation.$$.o)

echo "== LLR-1553-BM-003 inspection =="

# 1. include graph
BAD=$(grep -nE '#[[:space:]]*include[[:space:]]*"(bus_model|bus_model_internal)\.h"' \
        "$SRC/bm_monitor.c" "$INC/bm_monitor.h" 2>/dev/null)
if [ -n "$BAD" ]; then
    echo "  [FAIL] bm_monitor includes a header that declares transmit:"; echo "$BAD"; FAIL=1
else
    echo "  [PASS] bm_monitor includes neither bus_model.h nor bus_model_internal.h"
fi
if grep -q 'bus_model_monitor_if.h' "$INC/bm_monitor.h"; then
    echo "  [PASS] bm_monitor.h uses the receive-only interface (bus_model_monitor_if.h)"
else
    echo "  [FAIL] bm_monitor.h does not include bus_model_monitor_if.h"; FAIL=1
fi

# 2. undefined symbols of the compiled object
if ! $CC -std=c11 -I"$INC" -I"$SRC" -c "$SRC/bm_monitor.c" -o "$TMP" 2>/dev/null; then
    echo "  [FAIL] bm_monitor.c does not compile"; FAIL=1
else
    UNDEF=$(nm "$TMP" | awk '$1 == "U" {print $2}' | sed 's/^_//' | grep -E '^(bus_model|errinject|error_inject)' )
    EXTRA=$(echo "$UNDEF" | grep -v -E '^(bus_model_receive|bus_model_now_us)$' | grep -v '^$')
    if [ -n "$EXTRA" ]; then
        echo "  [FAIL] bm_monitor references bus model functions beyond receive/now:"; echo "$EXTRA"; FAIL=1
    else
        echo "  [PASS] bm_monitor's only bus model references are: $(echo $UNDEF | tr '\n' ' ')"
    fi
fi
rm -f "$TMP"

[ $FAIL -eq 0 ] && echo "BM isolation: PASSED" || echo "BM isolation: FAILED"
exit $FAIL