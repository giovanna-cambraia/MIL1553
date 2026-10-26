/*
    project: MIL1553

    realizes: LLR-1553-BIT-005, LLR-1553-BIT-006, LLR-1553-BIT-008
*/

#include "bit_cbit.h"

static mil1553_bit_cbit_channel_t *bit_cbit_channel(mil1553_bit_cbit_context_t *ctx, mil1553_channel_t channel)
{
    return (channel == MIL1553_CHANNEL_A) ? &ctx->channel[0] : &ctx->channel[1];
}

// implements: LLR-1553-BIT-002
void bit_cbit_init(mil1553_bit_cbit_context_t *ctx, mil1553_time_us_t watchdog_period_us)
{
    uint8_t c;
    uint8_t i;

    for (c = 0U; c < BUS_MODEL_CHANNEL_COUNT; c++)
    {
        for (i = 0U; i < BIT_CBIT_WINDOW_SIZE; i++)
        {
            ctx->channel[c].outcomes[i] = true;
        }
        for (i = 0U; i < BIT_CBIT_WORD_CLASS_COUNT; i++)
        {
            ctx->channel[c].class_count[i] = 0U;
        }
        ctx->channel[c].next_index = 0U;
        ctx->channel[c].filled_count = 0U;
        ctx->channel[c].good_count_total = 0U;
        ctx->channel[c].error_count_total = 0U;
        ctx->channel[c].unhealthy = false;
    }

    ctx->response_time_violations = 0U;
    ctx->word_count_violations = 0U;
    ctx->response_time_reported = 0U;
    ctx->word_count_reported = 0U;

    ctx->watchdog_period_us = (watchdog_period_us == 0U) ?
                              (mil1553_time_us_t)BIT_CBIT_WATCHDOG_PERIOD_US_DEFAULT :
                              watchdog_period_us;
    ctx->watchdog_last_progress_us = 0U;
    ctx->watchdog_armed = false;
    ctx->watchdog_stalled = false;
    ctx->watchdog_tripped = false;
    ctx->watchdog_trip_count = 0U;
}

// implements: LLR-1553-BIT-005
void bit_cbit_record_word(mil1553_bit_cbit_context_t *ctx, mil1553_channel_t channel, mil1553_word_class_t classification)
{
    mil1553_bit_cbit_channel_t *ch = bit_cbit_channel(ctx, channel);
    bool good = (classification == MIL1553_WORD_VALID);

    ch->outcomes[ch->next_index] = good;
    ch->next_index = (uint8_t)((ch->next_index + 1U) % BIT_CBIT_WINDOW_SIZE);
    if (ch->filled_count < BIT_CBIT_WINDOW_SIZE) { ch->filled_count++; }

    if ((uint32_t)classification < BIT_CBIT_WORD_CLASS_COUNT) { ch->class_count[classification]++; }
    if (good) { ch->good_count_total++; } else { ch->error_count_total++; }
}

// implements: LLR-1553-BIT-005
void bit_cbit_record_response_time(mil1553_bit_cbit_context_t *ctx, uint32_t measured_us)
{
    if ((measured_us < MIL1553_T_RESP_MIN_US) || (measured_us > MIL1553_T_RESP_MAX_US))
    {
        ctx->response_time_violations++;
    }
}

// implements: LLR-1553-BIT-005
void bit_cbit_record_word_count_violation(mil1553_bit_cbit_context_t *ctx)
{
    ctx->word_count_violations++;
}

// implements: LLR-1553-BIT-006
void bit_cbit_watchdog_kick(mil1553_bit_cbit_context_t *ctx, mil1553_time_us_t now_us)
{
    ctx->watchdog_last_progress_us = now_us;
    ctx->watchdog_armed = true;
    ctx->watchdog_stalled = false;   // the loop is moving again; a trip already latched stays latched
}

// implements: LLR-1553-BIT-006
void bit_cbit_tick(mil1553_bit_cbit_context_t *ctx, mil1553_time_us_t now_us)
{
    uint8_t c;

    // watchdog: only meaningful once the loop has kicked at least once; the stall flag is "currently late", the trip
    // flag is "was late at some point since the last update_result" (latched until consumed)
    if (ctx->watchdog_armed && (now_us >= ctx->watchdog_last_progress_us) &&
        ((mil1553_time_us_t)(now_us - ctx->watchdog_last_progress_us) > ctx->watchdog_period_us))
    {
        if (!ctx->watchdog_stalled)
        {
            ctx->watchdog_stalled = true;
            ctx->watchdog_trip_count++;
        }
        ctx->watchdog_tripped = true;
    }

    for (c = 0U; c < BUS_MODEL_CHANNEL_COUNT; c++)
    {
        mil1553_bit_cbit_channel_t *ch = &ctx->channel[c];

        // a channel is judged only once BIT_CBIT_MIN_SAMPLES words are in the window; below that the ratio is not meaningful
        if (ch->filled_count >= BIT_CBIT_MIN_SAMPLES)
        {
            uint32_t errors = 0U;
            uint8_t i;

            for (i = 0U; i < ch->filled_count; i++)
            {
                if (!ch->outcomes[i]) { errors++; }
            }
            ch->unhealthy = (((errors * 100U) / (uint32_t)ch->filled_count) >
                             (uint32_t)BIT_CBIT_ERROR_RATIO_THRESHOLD_PERCENT);
        }
        else
        {
            ch->unhealthy = false; // too few words to judge
        }
    }
}

// implements: LLR-1553-BIT-005, LLR-1553-BIT-006, LLR-1553-BIT-008, LLR-1553-BIT-009
void bit_cbit_update_result(mil1553_bit_cbit_context_t *ctx, mil1553_bit_result_t *result, mil1553_time_us_t now_us)
{
    bool any_unhealthy = ctx->channel[0].unhealthy || ctx->channel[1].unhealthy;
    bool new_response = (ctx->response_time_violations != ctx->response_time_reported);
    bool new_count = (ctx->word_count_violations != ctx->word_count_reported);
    bool failed = any_unhealthy || ctx->watchdog_tripped || new_response || new_count;
    uint32_t mask = 0U;
    bool legal;

    if (any_unhealthy)             { mask |= (1U << BIT_CBIT_TEST_CHANNEL_HEALTH); }
    if (ctx->watchdog_tripped)     { mask |= (1U << BIT_CBIT_TEST_WATCHDOG); }
    if (new_response || new_count) { mask |= (1U << BIT_CBIT_TEST_PROTOCOL); }

    // OPERATIONAL goes through CBIT_RUNNING (OPERATIONAL -> CBIT_RUNNING -> OPERATIONAL / DEGRADED), which is what the
    // LLR-1553-BIT-009 state machine expects for a fresh cycle; DEGRADED takes the outcome directly (DEGRADED -> OPERATIONAL
    // on a pass, DEGRADED -> DEGRADED on a non-fatal fail); every other state (POWER_UP, PBIT, IBIT_RUNNING, FAIL) ignores
    // the cycle, and bit_state_apply counts it
    if (result->state == MIL1553_BIT_STATE_OPERATIONAL)
    {
        legal = bit_state_apply(result, BIT_EVENT_CBIT_BEGIN);
        if (legal) { (void)bit_state_apply(result, failed ? BIT_EVENT_CBIT_NONFATAL : BIT_EVENT_CBIT_PASS); }
    }
    else
    {
        legal = bit_state_apply(result, failed ? BIT_EVENT_CBIT_NONFATAL : BIT_EVENT_CBIT_PASS);
    }

    if (legal)
    {
        uint8_t id = 0U;
        uint8_t k;

        for (k = 0U; k < 4U; k++) { if ((mask & (1U << k)) != 0U) { id = k; break; } }

        result->cbit.ever_run = true;
        result->cbit.last_passed = !failed;
        result->cbit.failing_test_id = id;
        result->cbit.failing_item = 0U;
        result->cbit.failed_mask = mask;
        result->cbit.timestamp_us = now_us;

        // what has now been reported is consumed, so the next cycle's "new violation" comparison starts from here
        ctx->watchdog_tripped = false;
        ctx->response_time_reported = ctx->response_time_violations;
        ctx->word_count_reported = ctx->word_count_violations;
    }
}