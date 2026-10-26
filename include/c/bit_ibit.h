/*
    project: MIL1553
    purpose: initiated built-in test: full loopback on both channels, cross-channel comparison, known-pattern injection with echo verification

    realizes: LLR-1553-BIT-007
    parent HLR: HLR-1553-BIT-003

    isolation from other terminals (LLR-1553-BIT-007): IBIT reads its own pattern back through the dedicated loopback cursor
    (bus_model_receive_loopback), which never disturbs a terminal's normal receive position; the pattern is sent as a DATA-sync word, and
    rt_service ignores any word whose sync is not COMMAND_STATUS when looking for commands, so other RTs do not read it as one
*/

#ifndef MIL1553_BIT_IBIT_H
#define MIL1553_BIT_IBIT_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"
#include "bit_types.h"
#include "bus_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BIT_IBIT_PATTERN_VALUE ((mil1553_data_t)0x5AA5U) // known pattern, LLR-1553-BIT-007

/*
    implements: LLR-1553-BIT-007, LLR-1553-BIT-009

    runs the IBIT sequence: sends the known pattern as a data word on channel A and reads it back (BIT_IBIT_TEST_LOOPBACK_A), the same on channel B
    (BIT_IBIT_TEST_LOOPBACK_B), then checks the two channels agree (BIT_IBIT_TEST_CROSS_CHANNEL); all three are run and combined in
    result->ibit.failed_mask; failing_item is the first failed channel (0 = A, 1 = B)

    IBIT may only start from OPERATIONAL (OPERATIONAL -> IBIT_RUNNING); from any other state nothing is sent, the request is counted as an ignored
    event and false is returned - in particular IBIT can never clear a FAIL

    outcome: both channels fail -> FATAL (FAIL, the terminal can no longer test itself); exactly one channel fails or the channels disagree ->
    DEGRADED; everything passes -> back to OPERATIONAL

    only the bus ring log and the loopback cursor are touched: no subaddress buffer, schedule or other terminal state, so the terminal is as it was
    afterwards ("restore to its previous state")
*/
bool bit_ibit_run(mil1553_bit_result_t *result,
                   mil1553_bus_model_context_t *bus,
                   mil1553_terminal_id_t terminal_id);

#ifdef __cplusplus
}
#endif

#endif 