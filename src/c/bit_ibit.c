/*
    project: MIL1553
    realizes: LLR-1553-BIT-007
*/

#include "bit_ibit.h"
#include "mil1553_word.h"

static bool bit_ibit_loopback_channel(mil1553_bus_model_context_t *bus,
                                       mil1553_channel_t channel,
                                       mil1553_terminal_id_t terminal_id,
                                       mil1553_data_t *out_value)
{
    mil1553_symbols_t symbols;
    mil1553_bus_record_t record;
    mil1553_decoded_word_t decoded;

    mil1553_word_pack_data(BIT_IBIT_PATTERN_VALUE, symbols);

    if (!bus_model_transmit(bus, channel, symbols, terminal_id))
    {
        return false; // channel failed, cannot even attempt loopback
    }
    bus_model_advance_us(bus, (mil1553_time_us_t)MIL1553_WORD_TIME_US);

    if (!bus_model_receive_loopback(bus, channel, terminal_id, &record))
    {
        return false;
    }
    if (mil1553_word_classify(record.symbols, &decoded) != MIL1553_WORD_VALID)
    {
        return false;
    }

    *out_value = mil1553_word_unpack_data(decoded.field);
    return (*out_value == BIT_IBIT_PATTERN_VALUE);
}

// implements: LLR-1553-BIT-007, LLR-1553-BIT-009
bool bit_ibit_run(mil1553_bit_result_t *result,
                   mil1553_bus_model_context_t *bus,
                   mil1553_terminal_id_t terminal_id)
{
    mil1553_data_t value_a = 0U;
    mil1553_data_t value_b = 0U;
    uint32_t mask = 0U;
    uint32_t item = 0U;
    uint8_t first_id = 0U;
    bool ok_a;
    bool ok_b;
    bool pass;
    uint32_t k;

    // IBIT only starts from OPERATIONAL; from any other state (POWER_UP, PBIT, CBIT_RUNNING, IBIT_RUNNING, FAIL) the
    // request is counted as an ignored event by bit_state_apply and nothing is transmitted
    if (!bit_state_apply(result, BIT_EVENT_IBIT_BEGIN))
    {
        return false;
    }

    ok_a = bit_ibit_loopback_channel(bus, MIL1553_CHANNEL_A, terminal_id, &value_a);
    ok_b = bit_ibit_loopback_channel(bus, MIL1553_CHANNEL_B, terminal_id, &value_b);

    // all three tests run unconditionally; each failure contributes a bit to mask, so failed_mask reports every test
    // that failed, not just the first one
    if (!ok_a) { mask |= (1U << BIT_IBIT_TEST_LOOPBACK_A); }
    if (!ok_b) { mask |= (1U << BIT_IBIT_TEST_LOOPBACK_B); }
    if (!(ok_a && ok_b && (value_a == value_b))) { mask |= (1U << BIT_IBIT_TEST_CROSS_CHANNEL); }

    pass = (mask == 0U);
    if (!pass)
    {
        // failing_test_id is the first failure in the LLR's test order; failing_item is the first failed channel (0 = A, 1 = B)
        for (k = 0U; k < 3U; k++)
        {
            if ((mask & (1U << k)) != 0U) { first_id = (uint8_t)k; break; }
        }
        item = (!ok_a) ? 0U : 1U;
    }

    result->ibit.ever_run = true;
    result->ibit.last_passed = pass;
    result->ibit.failing_test_id = first_id;
    result->ibit.failing_item = pass ? 0U : item;
    result->ibit.failed_mask = mask;
    result->ibit.timestamp_us = bus_model_now_us(bus);

    // outcome: pass -> back to OPERATIONAL; both channels lost -> FATAL (the terminal can no longer test itself);
    // exactly one channel failed or the cross-channel check disagrees -> DEGRADED
    if (pass)                    { (void)bit_state_apply(result, BIT_EVENT_IBIT_PASS); }
    else if ((!ok_a) && (!ok_b)) { (void)bit_state_apply(result, BIT_EVENT_FATAL); }
    else                         { (void)bit_state_apply(result, BIT_EVENT_IBIT_NONFATAL); }
    return pass;
}