/*
    project: MIL1553

    realizes: LLR-1553-BIT-008, LLR-1553-BIT-009
*/

#include "bit_types.h"

// clears one per-level result; every field is written explicitly rather than via aggregate copy, so a future field added to
// mil1553_bit_level_result_t is caught by the compiler instead of silently left uninitialised (see review note from the earlier
// cbit = pbit iteration)
static void level_clear(mil1553_bit_level_result_t *l)
{
    l->ever_run = false;
    l->last_passed = false;
    l->failing_test_id = 0U;
    l->failing_item = 0U;
    l->failed_mask = 0U;
    l->timestamp_us = 0U;
}

// implements: LLR-1553-BIT-009
void bit_result_init(mil1553_bit_result_t *result)
{
    level_clear(&result->pbit);
    level_clear(&result->cbit);
    level_clear(&result->ibit);
    result->state = MIL1553_BIT_STATE_POWER_UP;
    result->ignored_event_count = 0U;
}

// implements: LLR-1553-BIT-009 (the complete, explicit transition table)
bool bit_state_apply(mil1553_bit_result_t *result, mil1553_bit_event_t event)
{
    mil1553_bit_state_t from = result->state;
    mil1553_bit_state_t to = from;
    bool legal = true;

    if (event == BIT_EVENT_FATAL)
    {
        if (from == MIL1553_BIT_STATE_POWER_UP) { legal = false; }
        else { to = MIL1553_BIT_STATE_FAIL; }
    }
    else
    {
        switch (from)
        {
            case MIL1553_BIT_STATE_POWER_UP:
                if (event == BIT_EVENT_PBIT_START) { to = MIL1553_BIT_STATE_PBIT; } else { legal = false; }
                break;
            case MIL1553_BIT_STATE_PBIT:
                if (event == BIT_EVENT_PBIT_PASS) { to = MIL1553_BIT_STATE_OPERATIONAL; }
                else if (event == BIT_EVENT_PBIT_FAIL) { to = MIL1553_BIT_STATE_FAIL; }
                else { legal = false; }
                break;
            case MIL1553_BIT_STATE_OPERATIONAL:
                if (event == BIT_EVENT_CBIT_BEGIN) { to = MIL1553_BIT_STATE_CBIT_RUNNING; }
                else if (event == BIT_EVENT_IBIT_BEGIN) { to = MIL1553_BIT_STATE_IBIT_RUNNING; }
                else { legal = false; }
                break;
            case MIL1553_BIT_STATE_CBIT_RUNNING:
                if (event == BIT_EVENT_CBIT_PASS) { to = MIL1553_BIT_STATE_OPERATIONAL; }
                else if (event == BIT_EVENT_CBIT_NONFATAL) { to = MIL1553_BIT_STATE_DEGRADED; }
                else { legal = false; }
                break;
            case MIL1553_BIT_STATE_IBIT_RUNNING:
                if (event == BIT_EVENT_IBIT_PASS) { to = MIL1553_BIT_STATE_OPERATIONAL; }
                else if (event == BIT_EVENT_IBIT_NONFATAL) { to = MIL1553_BIT_STATE_DEGRADED; }
                else { legal = false; }
                break;
            case MIL1553_BIT_STATE_DEGRADED:
                if (event == BIT_EVENT_CBIT_PASS) { to = MIL1553_BIT_STATE_OPERATIONAL; }
                else if (event == BIT_EVENT_CBIT_NONFATAL) { to = MIL1553_BIT_STATE_DEGRADED; }
                else { legal = false; }
                break;
            default: // FAIL: only FATAL (above) is meaningful, and it changes nothing
                legal = false;
                break;
        }
    }

    if (legal) { result->state = to; }
    else { result->ignored_event_count++; }
    return legal;
}

// implements: LLR-1553-BIT-009 (a fatal condition found outside the BIT modules)
bool bit_result_report_fatal(mil1553_bit_result_t *result)
{
    return bit_state_apply(result, BIT_EVENT_FATAL);
}

// implements: LLR-1553-BIT-008
bool bit_result_is_fatal(const mil1553_bit_result_t *result)
{
    // DEGRADED is deliberately not fatal: at least one channel/function is still usable (see bit_ibit_run); in-progress states (POWER_UP, PBIT,
    // IBIT_RUNNING, CBIT_RUNNING) are neither fatal nor degraded
    return (result->state == MIL1553_BIT_STATE_FAIL);
}

// implements: LLR-1553-BIT-008
bool bit_result_is_degraded(const mil1553_bit_result_t *result)
{
    return (result->state == MIL1553_BIT_STATE_DEGRADED);
}

// implements: LLR-1553-BIT-008 (the BIT word returned by the transmit-BIT-word mode command)
uint16_t bit_result_to_word(const mil1553_bit_result_t *result)
{
    uint16_t w = 0U;

    if (result->state == MIL1553_BIT_STATE_FAIL)     { w |= 0x0001U; } // bit 0: state is FAIL
    if (result->state == MIL1553_BIT_STATE_DEGRADED) { w |= 0x0002U; } // bit 1: state is DEGRADED

    // bits 2..7: per-level ran-and-passed / failed, one pair per level
    if (result->pbit.ever_run) { w |= result->pbit.last_passed ? 0x0004U : 0x0008U; }
    if (result->cbit.ever_run) { w |= result->cbit.last_passed ? 0x0010U : 0x0020U; }
    if (result->ibit.ever_run) { w |= result->ibit.last_passed ? 0x0040U : 0x0080U; }

    // bits 8..10: failing PBIT test id (0 if PBIT has not failed)
    if (result->pbit.ever_run && (!result->pbit.last_passed))
    {
        w |= (uint16_t)(((uint16_t)result->pbit.failing_test_id & 0x07U) << 8);
    }
    return w;
}