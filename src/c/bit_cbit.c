/*
    project: MIL1553

    realizes: LLR-1553-BIT-005, LLR-1553-BIT-006, LLR-1553-BIT-008
*/

#include "bit_cbit.h"

static mil1553_bit_cbit_channel_t *bit_cbit_channel(mil1553_bit_cbit_context_t *ctx, mil1553_channel_t channel)
{
    return (channel == MIL1553_CHANNEL_A) ? &ctx->channel[0] : &ctx->channel[1];
}

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
        ctx->channel[c].next_index = 0U;
        ctx->channel[c].filled_count = 0U;
        ctx->channel[c].good_count_total = 0U;
        ctx->channel[c].error_count_total = 0U;
        ctx->channel[c].unhealthy = false;
    }

    ctx->response_time_violations = 0U;
    ctx->word_count_violations = 0U;

    ctx->watchdog_period_us = (watchdog_period_us == 0U) ? 
                              (mil1553_time_us_t)BIT_CBIT_WATCHDOG_PERIOD_US_DEFAULT : 
                              watchdog_period_us;
    ctx->watchdog_last_progress_us = 0U;
    ctx->watchdog_tripped = false;
}

static void bit_cbit_record_outcome(mil1553_bit_cbit_channel_t *ch, bool good)
{
    ch->outcomes[ch->next_index] = good;
    ch->next_index = (uint8_t)((ch->next_index + 1U) % BIT_CBIT_WINDOW_SIZE);

    if (ch->filled_count < BIT_CBIT_WINDOW_SIZE)
    {
        ch->filled_count++;
    }

    if (good)
    {
        ch->good_count_total++;
    }
    else
    {
        ch->error_count_total++;
    }
}

// implements: LLR-1553-BIT-005
void bit_cbit_record_word(mil1553_bit_cbit_context_t *ctx, mil1553_channel_t channel, mil1553_word_class_t classification)
{
    mil1553_bit_cbit_channel_t *ch = bit_cbit_channel(ctx, channel);
    bit_cbit_record_outcome(ch, classification == MIL1553_WORD_VALID);
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
    ctx->watchdog_tripped = false;
}

// implements: LLR-1553-BIT-006
void bit_cbit_tick(mil1553_bit_cbit_context_t *ctx, mil1553_time_us_t now_us)
{
    uint8_t c;
    mil1553_time_us_t since_progress = (now_us >= ctx->watchdog_last_progress_us) ? 
                                       (now_us - ctx->watchdog_last_progress_us) : 0U;

    if (since_progress > ctx->watchdog_period_us)
    {
        ctx->watchdog_tripped = true;
    }

    for (c = 0U; c < BUS_MODEL_CHANNEL_COUNT; c++)
    {
        mil1553_bit_cbit_channel_t *ch = &ctx->channel[c];

        if (ch->filled_count > 0U)
        {
            uint8_t errors_in_window = 0U;
            uint8_t i;

            for (i = 0U; i < ch->filled_count; i++)
            {
                if (!ch->outcomes[i])
                {
                    errors_in_window++;
                }
            }

            {
                uint32_t ratio_percent = ((uint32_t)errors_in_window * 100U) / (uint32_t)ch->filled_count;
                ch->unhealthy = (ratio_percent > (uint32_t)BIT_CBIT_ERROR_RATIO_THRESHOLD_PERCENT);
            }
        }
    }
}

// implements: LLR-1553-BIT-005, LLR-1553-BIT-006, LLR-1553-BIT-008
void bit_cbit_update_result(const mil1553_bit_cbit_context_t *ctx, mil1553_bit_result_t *result)
{
    bool any_unhealthy = ctx->channel[0].unhealthy || ctx->channel[1].unhealthy;
    bool degraded = any_unhealthy || ctx->watchdog_tripped;
    bool overall_pass = (!degraded) && 
                        (ctx->response_time_violations == 0U) && 
                        (ctx->word_count_violations == 0U);

    result->cbit.ever_run = true;
    result->cbit.last_passed = overall_pass;

    if (!overall_pass)
    {
        if (any_unhealthy)
        {
            result->cbit.failing_test_id = BIT_CBIT_TEST_CHANNEL_HEALTH;
        }
        else if (ctx->watchdog_tripped)
        {
            result->cbit.failing_test_id = BIT_CBIT_TEST_WATCHDOG;
        }
        else if ((ctx->response_time_violations > 0U) || (ctx->word_count_violations > 0U))
        {
            result->cbit.failing_test_id = BIT_CBIT_TEST_PROTOCOL;
        }
        else
        {
            result->cbit.failing_test_id = BIT_CBIT_TEST_PARITY;
        }
    }

    // implements: LLR-1553-BIT-009
    if (result->state != MIL1553_BIT_STATE_FAIL)
    {
        result->state = degraded ? MIL1553_BIT_STATE_DEGRADED : MIL1553_BIT_STATE_OPERATIONAL;
    }
}