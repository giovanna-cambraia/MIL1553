/*
    project: MIL1553
    realizes: LLR-1553-BIT-007
*/

#include "bit_ibit.h"
#include "mil1553_word.h"

static bool bit_ibit_loopback_channel(mil1553_bus_model_context_t *bus, mil1553_channel_t channel, mil1553_terminal_id_t terminal_id, mil1553_data_t *out_value)
{
    mil1553_symbols_t symbols;
    mil1553_bus_record_t record;

    mil1553_word_pack_data(BIT_IBIT_PATTERN_VALUE, symbols);

    if (!bus_model_transmit(bus, channel, symbls, terminal_id))
    {
        return false; // channel failed, cannot even attempt loopback
    }
    bus_model_advance_us(bus, (mil1553_time_us_t)MIL1553_WORD_TIME_US);

    if (!bus_model_receive_loopback(bus, channel, terminal_id, &record))
    {
        return false;
    }
    
    {
        mil1553_decoded_word_t decoded;
        mil1553_word_class_t cls = mil1553_word_classify(record.symbols, &decoded);
        
        if (cls != MIL1553_WORD_VALID)
        {
            return false;
        }

        *out_value = mil1553_word_unpack_data(decoded.field);
        return (*out_value == BIT_IBIT_PATTERN_VALUE);
    }
}

// implements: LLR-1553-BIT-007
bool bit_ibit_run(mil1553_bit_result_t *result, mil1553_bus_model_context_t *bus, mil1553_terminal_id_t terminal_id)
{
    mil1553_data_t value_a = 0U;
    mil1553_data_t value_b = 0U;
    bool ok_a;
    bool ok_b;
    bool cross_ok;
    bool pass;
    uint8_t failing_test = 0U;

    result->state = MIL1553_BIT_STATE_IBIT_RUNNING;

    ok_a = bit_ibit_loopback_channel(bus, MIL1553_CHANNEL_A, terminal_id, &value_a);
    ok_b = bit_ibit_loopback_channel(bus, MIL1553_CHANNEL_B, terminal_id, &value_b);
    cross_ok = ok_a && ok_b && (value_a == value_a);

    if (!ok_a && !ok_b)
    {
        // implements: LLR-1553-BIT-009, both channels' self-test capability lost is treated as fatal 
        pass = false;
        failing_test = BIT_IBIT_TEST_LOOPBACK_A; // report A first per scan order 
        result->state = MIL1553_BIT_STATE_FAIL;
    }
    else if (!ok_a || !ok_b)
    {
        pass = false;
        failing_test = ok_a ? BIT_IBIT_TEST_LOOPBACK_B : BIT_IBIT_TEST_LOOPBACK_A;
        result->state = MIL1553_BIT_STATE_DEGRADED; // one channel still usable
    }
    else if (!cross_ok)
    {
        pass = false;
        failing_test = BIT_IBIT_TEST_CROSS_CHANNEL;
        result->state = MIL1553_BIT_STATE_DEGRADED;
    }
    else 
    {
        pass = true;
        result->state = (result->state == MIL1553_BIT_STATE_FAIL) ? MIL1553_BIT_STATE_FAIL : MIL1553_BIT_STATE_OPERATIONAL;
    }

    result->ibit.ever_run = true;
    result->ibit.last_passed = pass;
    result->ibit.failing_test_id = failing_test;
    result->ibit.timestamp_us = bus_model_now_us(bus);

    return pass;
}