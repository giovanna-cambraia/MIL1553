/*
    project: MIL1553

    realizes: LLR-1553-BIT-001, LLR-1553-BIT-002, LLR-1553-BIT-003, LLR-1553-BIT-004
*/

#include "bit_pbit.h"
#include "mil1553_word.h"

// "a fixed, known reference block standing in for a "code/constant region" (e.g. the mode code table), see file header note; the checksum is captured once at init
// (standing in for a build-time reference) and re-verified at run time
static const uint8_t BIT_PBIT_ROM_TEST_BLOCK[32] = {
    0x1AU, 0x2BU, 0x3CU, 0x4DU, 0x5EU, 0x6FU, 0x70U, 0x81U,
    0x92U, 0xA3U, 0xB4U, 0xC5U, 0xD6U, 0xE7U, 0xF8U, 0x09U,
    0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U,
    0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU, 0x00U};

static uint32_t bit_pbit_checksum(const uint8_t *data, uint8_t length)
{
    uint32_t sum = 0U;
    uint8_t i;

    for (i = 0U; i < length; i++)
    {
        sum += (uint32_t)data[i];
        sum = (sum << 1) | (sum >> 31); // rotate, cheap mixing
    }

    return sum;
}

// implements: LLR-1553-BIT-001
void bit_pbit_init(mil1553_bit_pbit_context_t *ctx)
{
    uint8_t i;

    for (i = 0U; i < BIT_PBIT_RAM_TEST_REGION_SIZE; i++)
    {
        ctx->ram_test_region[i] = 0U;
    }

    ctx->rom_reference_checksum = bit_pbit_checksum(BIT_PBIT_ROM_TEST_BLOCK, 32U);
    ctx->rom_reference_captured = true;
}

// implements: LLR-1553-BIT-001
// NOTE: the loops below write 0xFF / 0x00 to every cell and read it back, which is an all-ones / all-zeros stuck-bit test, not a walking-1s/0s
// test as the LLR text names it, behaviour is retained as-is; see review note in the LLR-1553-BIT-001 baseline
static bool bit_pbit_check_ram(mil1553_bit_pbit_context_t *ctx)
{
    uint8_t saved[BIT_PBIT_RAM_TEST_REGION_SIZE];
    uint8_t i;
    bool ok = true;

    for (i = 0U; i < BIT_PBIT_RAM_TEST_REGION_SIZE; i++)
    {
        saved[i] = ctx->ram_test_region[i];
    }

    // all ones
    for (i = 0U; i < BIT_PBIT_RAM_TEST_REGION_SIZE; i++)
    {
        ctx->ram_test_region[i] = 0xFFU;
        if (ctx->ram_test_region[i] != 0xFFU)
        {
            ok = false;
        }
    }

    // all zeros
    for (i = 0U; i < BIT_PBIT_RAM_TEST_REGION_SIZE; i++)
    {
        ctx->ram_test_region[i] = 0x00U;
        if (ctx->ram_test_region[i] != 0x00U)
        {
            ok = false;
        }
    }

    for (i = 0U; i < BIT_PBIT_RAM_TEST_REGION_SIZE; i++)
    {
        ctx->ram_test_region[i] = saved[i]; // restore, per LLR-1553-BIT-001
    }

    return ok;
}

// implements: LLR-1553-BIT-001 (ROM checksum)
static bool bit_pbit_check_rom(const mil1553_bit_pbit_context_t *ctx)
{
    uint32_t current = bit_pbit_checksum(BIT_PBIT_ROM_TEST_BLOCK, 32U);
    return ctx->rom_reference_captured && (current == ctx->rom_reference_checksum);
}

// implements: LLR-1553-BIT-001 (codec loopback: command, status, data)
static bool bit_pbit_check_codec(void)
{
    mil1553_symbols_t symbols;
    bool ok = true;

    // command word round trip
    {
        mil1553_command_fields_t cmd_in;
        mil1553_command_fields_t cmd_out;
        mil1553_decoded_word_t decoded;

        cmd_in.rt_address = 7U;
        cmd_in.tr_bit = MIL1553_TR_RECEIVE;
        cmd_in.subaddress = 3U;
        cmd_in.word_count_field = 5U;

        if (!mil1553_word_pack_command(&cmd_in, symbols))
        {
            ok = false;
        }
        else if (mil1553_word_classify(symbols, &decoded) != MIL1553_WORD_VALID)
        {
            ok = false;
        }
        else
        {
            mil1553_word_unpack_command(decoded.field, &cmd_out);
            if ((cmd_out.rt_address != cmd_in.rt_address) ||
                (cmd_out.tr_bit != cmd_in.tr_bit) ||
                (cmd_out.subaddress != cmd_in.subaddress) ||
                (cmd_out.word_count_field != cmd_in.word_count_field))
            {
                ok = false;
            }
        }
    }

    // status word round trip
    if (ok)
    {
        mil1553_status_fields_t st_in;
        mil1553_status_fields_t st_out;
        mil1553_decoded_word_t decoded;

        st_in.rt_address = 12U;
        st_in.message_error = false;
        st_in.instrumentation = false;
        st_in.service_request = false;
        st_in.broadcast_received = true;
        st_in.busy = false;
        st_in.subsystem_flag = true;
        st_in.dbc_acceptance = false;
        st_in.terminal_flag = false;

        if (!mil1553_word_pack_status(&st_in, symbols))
        {
            ok = false;
        }
        else if (mil1553_word_classify(symbols, &decoded) != MIL1553_WORD_VALID)
        {
            ok = false;
        }
        else
        {
            mil1553_word_unpack_status(decoded.field, &st_out);
            if ((st_out.rt_address != st_in.rt_address) || (st_out.broadcast_received != st_in.broadcast_received) || (st_out.subsystem_flag != st_in.subsystem_flag))
            {
                ok = false;
            }
        }
    }

    if (ok)
    {
        mil1553_decoded_word_t decoded;
        mil1553_data_t value = 0xA5A5U;

        mil1553_word_pack_data(value, symbols);
        if (mil1553_word_classify(symbols, &decoded) != MIL1553_WORD_VALID)
        {
            ok = false;
        }
        else if (mil1553_word_unpack_data(decoded.field) != value)
        {
            ok = false;
        }
    }

    return ok;
}

// implements: LLR-1553-BIT-001 (timer sanity, 1us tolerance)
static bool bit_pbit_check_timer(mil1553_bus_model_context_t *bus)
{
    mil1553_time_us_t before = bus_model_now_us(bus);
    mil1553_time_us_t expected_delta = 100U;

    bus_model_advance_us(bus, expected_delta);

    {
        mil1553_time_us_t after = bus_model_now_us(bus);
        mil1553_time_us_t actual_delta = (mil1553_time_us_t)(after - before);
        mil1553_time_us_t diff = (actual_delta >= expected_delta) ? (actual_delta - expected_delta) : (expected_delta - actual_delta);

        return (diff <= 1U); // 1us tolerance, LLR-1553-BIT-001
    }
}

// implements: LLR-1553-BIT-001 (bus a/b idle check)
static bool bit_pbit_check_bus_idle(mil1553_bus_model_context_t *bus, mil1553_terminal_id_t listener_id)
{
    mil1553_bus_record_t record;
    bool idle_a = !bus_model_receive(bus, MIL1553_CHANNEL_A, listener_id, &record);
    bool idle_b = !bus_model_receive(bus, MIL1553_CHANNEL_B, listener_id, &record);

    return idle_a && idle_b;
}

// implements: LLR-1553-BIT-001, LLR-1553-BIT-004
bool bit_pbit_run(mil1553_bit_pbit_context_t *ctx, mil1553_bit_result_t *result, mil1553_bus_model_context_t *bus, mil1553_terminal_id_t listener_id)
{
    bool pass = true;
    uint8_t failing_test = 0U;

    result->state = MIL1553_BIT_STATE_PBIT;

    if (!bit_pbit_check_ram(ctx))
    {
        pass = false;
        failing_test = BIT_PBIT_TEST_RAM;
    }
    else if (!bit_pbit_check_rom(ctx))
    {
        pass = false;
        failing_test = BIT_PBIT_TEST_ROM;
    }
    else if (!bit_pbit_check_codec())
    {
        pass = false;
        failing_test = BIT_PBIT_TEST_CODEC;
    }
    else if (!bit_pbit_check_timer(bus))
    {
        pass = false;
        failing_test = BIT_PBIT_TEST_TIMER;
    }
    else if (!bit_pbit_check_bus_idle(bus, listener_id))
    {
        pass = false;
        failing_test = BIT_PBIT_TEST_BUS_IDLE;
    }
    else
    {
        // all passed
    }

    result->pbit.ever_run = true;
    result->pbit.last_passed = pass;
    result->pbit.failing_test_id = failing_test;
    result->pbit.timestamp_us = bus_model_now_us(bus);

    result->state = pass ? MIL1553_BIT_STATE_OPERATIONAL : MIL1553_BIT_STATE_FAIL;

    return pass;
}