/*
    project: MIL1553
    purpose: power-up build-in test: RAM pattern test, ROM checksum, encoder/decoder loopback, timer sanity, bus idle check

    realizes: LLR-1553-BIT-001, LLR-1553-BIT-002, LLR-1553-BIT-003, LLR-1553-BIT-004
    parent HLR: HLR-1553-BIT-001

    design simplification (flagged for review, SDP 2.2): this is a host-based simulation (PSAC 2.2) with no real target RAM/ROM to fault-inject into; the RAM test
    exercises a designated scratch region (not live operational data, matching real PBIT practice of not testing memory currently in use) with the real walking-ones/zeros
    algorithm; the ROM checksum is computed once at bit_pbit_init time (standing in for a build/flash-time reference value) and re-verified at run time; both will always
    pass in this host environment absent an injected fault (via error_inject's test-build-only books); the algorithms are real, the capacity for a genuine hardwar fault
    to occur is not
*/

#ifndef MIL1553_BIT_PBIT_H
#define MIL1553_BIT_PBIT_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"
#include "bit_types.h"
#include "bus_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BIT_PBIT_RAM_TEST_REGION_SIZE (64U)

typedef struct {
    uint8_t ram_test_region[BIT_PBIT_RAM_TEST_REGION_SIZE];
    uint32_t rom_reference_checksum; // captured at bit_pbit_ibit
    bool rom_reference_captured;
} mil1553_bit_pbit_context_t;

// implements: LLR-1553-BIT-001 (captures the ROM reference checksum)
void bit_pbit_init(mil1553_bit_pbit_context_t *ctx);

/*
    implements: LLR-1553-BIT-001, LLR-1553-BIT-004

    runs all five PBIT checks (RAM, ROM, codec loopback, timer, bus idle) in order, stopping at the first failure (failling_test_id records which one), and updates
    result->pbit and result->state: MIL1553_BIT_STATE_PBIT -> OPERATIONAL on full pass, -> FAIL on any failure (LLR-1553-BIT-004: a PBIT failure blocks entry to
    OPERATIONAL, the state machine update is the blocking mechanism; rt_service/bc_scheduler must check result->state before calling rt_enable/bc_start)

    bus and listener_id are used for the bus-idle check (LLR-1553-BIT-001) and the coded loopback check re-uses bus_model only indirectly (thorugh mil1553_word/manchester,
    not the bus itself)
*/
bool bit_pbit_run(mil1553_bit_pbit_context_t *ctx, mil1553_bit_result_t *result, mil1553_bus_model_context_t *bus, mil1553_terminal_id_t listener_id);

#ifdef __cplusplus
}
#endif

#endif