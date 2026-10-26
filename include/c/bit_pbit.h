/*
    project: MIL1553
    purpose: power-up built-in test: RAM walking-bit test, ROM checksum, encoder/decoder loopback, timer sanity, bus idle check

    realizes: LLR-1553-BIT-001, LLR-1553-BIT-002, LLR-1553-BIT-003, LLR-1553-BIT-004
    parent HLR: HLR-1553-BIT-001

    host-based environment (PSAC 2.2): there is no physical RAM or ROM to fail; both tests therefore work through small interfaces so that they CAN be shown
    to detect a fault:
      - RAM: reads and writes go through mil1553_bit_mem_ops_t; on a target these would be volatile accesses to the real region; a unit test supplies ops that
        model a stuck bit
      - ROM: the checked region is configurable; the reference checksum is captured when the region is set (standing in for a value fixed at build/flash time),
        so a test can corrupt the region afterwards
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
#define BIT_PBIT_IDLE_LISTEN_STEPS    (5U)   // polls of both channels ...
#define BIT_PBIT_IDLE_LISTEN_STEP_US  (10U)  // ... this far apart: a 50 us listen period
#define BIT_PBIT_TIMER_CHECK_US       (100U)

typedef struct {
    uint8_t (*read)(const uint8_t *address);
    void    (*write)(uint8_t *address, uint8_t value);
} mil1553_bit_mem_ops_t;

typedef struct {
    uint8_t                      ram_test_region[BIT_PBIT_RAM_TEST_REGION_SIZE];
    const mil1553_bit_mem_ops_t *mem_ops;           // NULL = direct access
    const uint8_t               *rom_region;
    uint32_t                     rom_length;
    uint32_t                     rom_reference_checksum; // captured at bit_pbit_init / set_rom_region
    bool                         rom_reference_captured;
} mil1553_bit_pbit_context_t;

// implements: LLR-1553-BIT-001/002. selects the default ROM region and captures its reference
void bit_pbit_init(mil1553_bit_pbit_context_t *ctx);

// implements: LLR-1553-BIT-001. NULL restores direct access
void bit_pbit_set_memory_ops(mil1553_bit_pbit_context_t *ctx, const mil1553_bit_mem_ops_t *ops);

/*
    implements: LLR-1553-BIT-002

    selects the code/constant region to checksum and captures its reference checksum now; returns false (and changes nothing) for a NULL region or a zero length
*/
bool bit_pbit_set_rom_region(mil1553_bit_pbit_context_t *ctx, const uint8_t *data, uint32_t length);

/*
    implements: LLR-1553-BIT-001, LLR-1553-BIT-004

    runs all five PBIT tests and combines the results: result->pbit.failed_mask has a bit per failed test, failing_test_id is the first failed test, and
    failing_item is the RAM offset of the first mismatch (RAM) or the channel (bus idle); on a pass the state goes PBIT -> OPERATIONAL, on any failure
    PBIT -> FAIL (the blocking of LLR-1553-BIT-004: the owner must not enable the terminal unless this returns true)

    PBIT may only run from POWER_UP (LLR-1553-BIT-009); from any other state nothing runs, the call is counted as an ignored event, and false is returned;
    starting over after a reset means bit_result_init first

    tests 1-5 are, in order: RAM, ROM, codec loopback, timer, bus idle; the bus-idle check listens on both channels for 50 us of virtual time with
    listener_id's cursor; this advances the shared virtual clock, and any word heard is consumed
*/
bool bit_pbit_run(mil1553_bit_pbit_context_t *ctx,
                   mil1553_bit_result_t *result,
                   mil1553_bus_model_context_t *bus,
                   mil1553_terminal_id_t listener_id);

#ifdef __cplusplus
}
#endif

#endif 