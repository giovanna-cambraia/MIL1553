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

    result->cbit = result->pbit;
    result->ibit = result->pbit;

    result->state = MIL1553_BIT_STATE_POWER_UP;
}

bool bit_result_is_fatal(const mil1553_bit_result_t *result)
{
    return (result->state == MIL1553_BIT_STATE_FAIL);
}

bool bit_result_is_degraded(const mil1553_bit_result_t *result)
{
    return (result->state == MIL1553_BIT_STATE_DEGRADED);
}