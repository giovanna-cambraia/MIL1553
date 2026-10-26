/*
    project: MIL1553

    realizes: LLR-1553-BIT-001, LLR-1553-BIT-002, LLR-1553-BIT-003, LLR-1553-BIT-004
*/

#include "bit_pbit.h"
#include "mil1553_word.h"

// default code/constant region (stands in for e.g. the mode code table)
static const uint8_t BIT_PBIT_DEFAULT_ROM[32] = {
    0x1AU, 0x2BU, 0x3CU, 0x4DU, 0x5EU, 0x6FU, 0x70U, 0x81U,
    0x92U, 0xA3U, 0xB4U, 0xC5U, 0xD6U, 0xE7U, 0xF8U, 0x09U,
    0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U,
    0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU, 0x00U};

// order-dependent, so a swapped pair of bytes changes it as well
static uint32_t bit_pbit_checksum(const uint8_t *data, uint32_t length)
{
    uint32_t sum = 0U;
    uint32_t i;

    for (i = 0U; i < length; i++)
    {
        sum += (uint32_t)data[i];
        sum = (sum << 1) | (sum >> 31); // rotate, cheap mixing
    }
    return sum;
}

// implements: LLR-1553-BIT-001/002
void bit_pbit_init(mil1553_bit_pbit_context_t *ctx)
{
    uint32_t i;

    for (i = 0U; i < BIT_PBIT_RAM_TEST_REGION_SIZE; i++)
    {
        ctx->ram_test_region[i] = 0U;
    }
    ctx->mem_ops = (const mil1553_bit_mem_ops_t *)0;
    ctx->rom_reference_captured = false;
    (void)bit_pbit_set_rom_region(ctx, BIT_PBIT_DEFAULT_ROM, (uint32_t)sizeof(BIT_PBIT_DEFAULT_ROM));
}

// implements: LLR-1553-BIT-001 (NULL restores direct access)
void bit_pbit_set_memory_ops(mil1553_bit_pbit_context_t *ctx, const mil1553_bit_mem_ops_t *ops)
{
    ctx->mem_ops = ops;
}

// implements: LLR-1553-BIT-002
bool bit_pbit_set_rom_region(mil1553_bit_pbit_context_t *ctx, const uint8_t *data, uint32_t length)
{
    if ((data == (const uint8_t *)0) || (length == 0U))
    {
        return false;
    }
    ctx->rom_region = data;
    ctx->rom_length = length;
    ctx->rom_reference_checksum = bit_pbit_checksum(data, length);
    ctx->rom_reference_captured = true;
    return true;
}

// all RAM test reads and writes go through these: NULL mem_ops means direct volatile access to the region (target); non-NULL means the
// test build supplied ops that can model a stuck bit (so the test is falsifiable on a host)
static uint8_t mem_read(const mil1553_bit_pbit_context_t *ctx, const uint8_t *address)
{
    return (ctx->mem_ops != (const mil1553_bit_mem_ops_t *)0) ? ctx->mem_ops->read(address)
                                                              : *(const volatile uint8_t *)address;
}

static void mem_write(const mil1553_bit_pbit_context_t *ctx, uint8_t *address, uint8_t value)
{
    if (ctx->mem_ops != (const mil1553_bit_mem_ops_t *)0)
    {
        ctx->mem_ops->write(address, value);
    }
    else
    {
        *(volatile uint8_t *)address = value;
    }
}

/*
    implements: LLR-1553-BIT-001

    walking ones then walking zeros through every bit of every byte; the original content of each byte is restored whether or not it passed;
    reports the offset of the first mismatch
*/
static bool bit_pbit_check_ram(mil1553_bit_pbit_context_t *ctx, uint32_t *first_fail_offset)
{
    uint32_t i;
    uint32_t bit;

    for (i = 0U; i < BIT_PBIT_RAM_TEST_REGION_SIZE; i++)
    {
        uint8_t *addr = &ctx->ram_test_region[i];
        uint8_t saved = mem_read(ctx, addr);
        bool byte_ok = true;

        for (bit = 0U; bit < 8U; bit++)
        {
            uint8_t ones = (uint8_t)(1U << bit);
            uint8_t zeros = (uint8_t)(~ones);

            mem_write(ctx, addr, ones);
            if (mem_read(ctx, addr) != ones)
            {
                byte_ok = false;
            }
            mem_write(ctx, addr, zeros);
            if (mem_read(ctx, addr) != zeros)
            {
                byte_ok = false;
            }
        }

        mem_write(ctx, addr, saved);
        if (!byte_ok)
        {
            *first_fail_offset = i;
            return false;
        }
    }
    return true;
}

// implements: LLR-1553-BIT-002
static bool bit_pbit_check_rom(const mil1553_bit_pbit_context_t *ctx)
{
    if ((!ctx->rom_reference_captured) || (ctx->rom_region == (const uint8_t *)0))
    {
        return false;
    }
    return bit_pbit_checksum(ctx->rom_region, ctx->rom_length) == ctx->rom_reference_checksum;
}

// implements: LLR-1553-BIT-003 (command, status and data words round trip, with edge values)
static bool bit_pbit_check_codec(void)
{
    const uint8_t rts[3] = {0U, 7U, 31U};
    const uint16_t datas[4] = {0x0000U, 0xFFFFU, 0xA5A5U, 0x5A5AU};
    mil1553_symbols_t symbols;
    unsigned i;

    // command word round trip: three cases covering rt_address 0, 7, 31; tr_bit both values; subaddress and wc_field edges
    for (i = 0U; i < 3U; i++)
    {
        mil1553_command_fields_t in;
        mil1553_command_fields_t out;
        mil1553_decoded_word_t d;

        in.rt_address = rts[i];
        in.tr_bit = (i == 1U) ? MIL1553_TR_TRANSMIT : MIL1553_TR_RECEIVE;
        in.subaddress = (mil1553_subaddr_t)(i * 15U);
        in.word_count_field = (mil1553_word_count_t)(31U - (i * 15U));
        if ((!mil1553_word_pack_command(&in, symbols)) ||
            (mil1553_word_classify(symbols, &d) != MIL1553_WORD_VALID))
        {
            return false;
        }
        mil1553_word_unpack_command(d.field, &out);
        if ((out.rt_address != in.rt_address) || (out.tr_bit != in.tr_bit) ||
            (out.subaddress != in.subaddress) || (out.word_count_field != in.word_count_field))
        {
            return false;
        }
    }

    // status word round trip: three cases exercising the latched flags
    for (i = 0U; i < 3U; i++)
    {
        mil1553_status_fields_t in;
        mil1553_status_fields_t out;
        mil1553_decoded_word_t d;

        in.rt_address = rts[i];
        in.message_error = (i == 0U);
        in.instrumentation = false;
        in.service_request = false;
        in.broadcast_received = (i == 1U);
        in.busy = (i == 2U);
        in.subsystem_flag = (i != 1U);
        in.dbc_acceptance = false;
        in.terminal_flag = (i == 2U);
        in.reserved_nonzero = false;
        if ((!mil1553_word_pack_status(&in, symbols)) ||
            (mil1553_word_classify(symbols, &d) != MIL1553_WORD_VALID))
        {
            return false;
        }
        mil1553_word_unpack_status(d.field, &out);
        if ((out.rt_address != in.rt_address) || (out.message_error != in.message_error) ||
            (out.broadcast_received != in.broadcast_received) || (out.busy != in.busy) ||
            (out.subsystem_flag != in.subsystem_flag) || (out.terminal_flag != in.terminal_flag))
        {
            return false;
        }
    }

    // data word round trip: four values covering zeros, ones, and the two alternating patterns
    for (i = 0U; i < 4U; i++)
    {
        mil1553_decoded_word_t d;

        mil1553_word_pack_data(datas[i], symbols);
        if ((mil1553_word_classify(symbols, &d) != MIL1553_WORD_VALID) ||
            (mil1553_word_unpack_data(d.field) != datas[i]))
        {
            return false;
        }
    }
    return true;
}

// implements: LLR-1553-BIT-003 (the clock must advance by exactly what it was told, within 1 us)
static bool bit_pbit_check_timer(mil1553_bus_model_context_t *bus)
{
    mil1553_time_us_t before = bus_model_now_us(bus);
    mil1553_time_us_t after;
    mil1553_time_us_t delta;
    mil1553_time_us_t diff;

    bus_model_advance_us(bus, (mil1553_time_us_t)BIT_PBIT_TIMER_CHECK_US);
    after = bus_model_now_us(bus);
    if (after < before)
    {
        return false;
    }

    delta = (mil1553_time_us_t)(after - before);
    diff = (delta >= BIT_PBIT_TIMER_CHECK_US) ? (delta - BIT_PBIT_TIMER_CHECK_US) : (BIT_PBIT_TIMER_CHECK_US - delta);
    return diff <= 1U; // 1us tolerance, LLR-1553-BIT-001
}

// implements: LLR-1553-BIT-004 (listens on both channels for the whole period)
static bool bit_pbit_check_bus_idle(mil1553_bus_model_context_t *bus, mil1553_terminal_id_t listener_id,
                                    uint32_t *busy_channel)
{
    mil1553_bus_record_t record;
    uint32_t step;
    bool idle = true;

    for (step = 0U; step < BIT_PBIT_IDLE_LISTEN_STEPS; step++)
    {
        if (bus_model_receive(bus, MIL1553_CHANNEL_A, listener_id, &record))
        {
            if (idle)
            {
                *busy_channel = 0U;
            }
            idle = false;
        }
        if (bus_model_receive(bus, MIL1553_CHANNEL_B, listener_id, &record))
        {
            if (idle)
            {
                *busy_channel = 1U;
            }
            idle = false;
        }
        bus_model_advance_us(bus, (mil1553_time_us_t)BIT_PBIT_IDLE_LISTEN_STEP_US);
    }
    return idle;
}

// implements: LLR-1553-BIT-001, LLR-1553-BIT-004
bool bit_pbit_run(mil1553_bit_pbit_context_t *ctx,
                  mil1553_bit_result_t *result,
                  mil1553_bus_model_context_t *bus,
                  mil1553_terminal_id_t listener_id)
{
    uint32_t mask = 0U;
    uint32_t item = 0U;
    uint32_t first_item = 0U;
    uint32_t ram_offset = 0U;
    uint32_t idle_channel = 0U;
    uint8_t first_id = 0U;
    bool pass;

    if (!bit_state_apply(result, BIT_EVENT_PBIT_START))
    {
        return false; // PBIT only runs from POWER_UP
    }

    // all five tests run unconditionally; each failure sets a bit in mask, so failed_mask reports every test that failed,
    // not just the first one
    if (!bit_pbit_check_ram(ctx, &ram_offset))
    {
        mask |= (1U << BIT_PBIT_TEST_RAM);
    }
    if (!bit_pbit_check_rom(ctx))
    {
        mask |= (1U << BIT_PBIT_TEST_ROM);
    }
    if (!bit_pbit_check_codec())
    {
        mask |= (1U << BIT_PBIT_TEST_CODEC);
    }
    if (!bit_pbit_check_timer(bus))
    {
        mask |= (1U << BIT_PBIT_TEST_TIMER);
    }
    if (!bit_pbit_check_bus_idle(bus, listener_id, &idle_channel))
    {
        mask |= (1U << BIT_PBIT_TEST_BUS_IDLE);
    }

    pass = (mask == 0U);
    if (!pass)
    {
        for (item = 0U; item < 5U; item++)
        {
            if ((mask & (1U << item)) != 0U)
            {
                first_id = (uint8_t)item;
                break;
            }
        }
        if (first_id == BIT_PBIT_TEST_RAM)
        {
            first_item = ram_offset;
        }
        else if (first_id == BIT_PBIT_TEST_BUS_IDLE)
        {
            first_item = idle_channel;
        }
        else
        {
            first_item = 0U;
        }
    }

    result->pbit.ever_run = true;
    result->pbit.last_passed = pass;
    result->pbit.failing_test_id = first_id;
    result->pbit.failing_item = first_item;
    result->pbit.failed_mask = mask;
    result->pbit.timestamp_us = bus_model_now_us(bus);

    (void)bit_state_apply(result, pass ? BIT_EVENT_PBIT_PASS : BIT_EVENT_PBIT_FAIL);
    return pass;
}