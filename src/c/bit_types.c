/*
    project: MIL1553

    realizes: LLR-1553-BIT-008, LLR-1553-BIT-009 (shared helpers)
*/

#include "bit_types.h"

void bit_result_init(mil1553_bit_result_t *result)
{
    result->pbit.ever_run = false;
    result->pbit.last_passed = false;
    result->pbit.failing_test_id = 0U;
    result->pbit.timestamp_us = 0U;

    // assumes cbit and ibit are the same type as pbit (mil1553_bit_subtest_t or equivalent); if either has additional members beyond the four
    // shared ones, this initializes only the shared prefix and leaves the remainder uninitialised; see review note; verified against bit_types.h at design baseline
    result->cbit = result->pbit;
    result->ibit = result->pbit;

    result->state = MIL1553_BIT_STATE_POWER_UP;
}

bool bit_result_is_fatal(const mil1553_bit_result_t *result)
{
    // DEGRADED is deliberately not fatal: at least one channel/function is still usable (see bit_ibit_run); in-progress states (POWER_UP, PBIT,
    // IBIT_RUNNING, CBIT_RUNNING) are neither fatal nor degraded
    return (result->state == MIL1553_BIT_STATE_FAIL);
}

bool bit_result_is_degraded(const mil1553_bit_result_t *result)
{
    return (result->state == MIL1553_BIT_STATE_DEGRADED);
}