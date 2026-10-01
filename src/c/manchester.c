/*
    project: MIL1553
    realizes: LLR-1553-WORD-006, LLR-1553-WORD-007

    MISRA C: 2012 notes:
        - no dynamic memory, no recursion, bounded loops only (fixed 40/70 iteration counts, known at compile time)
        - symbol get/set helpers are file-local (static) since they are an implementation detail of the packed symbols representation,
        not part of the public interface (LLR-1553-BUS-002 fixes the backed layout, not these helpers functions)
*/

#include "manchester.h"

#define MANCHESTER_SYNC_SYMBOL_COUNT (6U)
#define MANCHESTER_FIELD_BIT_COUNT (16U)
#define MANCHESTER_PARITY_BIT_COUNT (1U)
#define MANCHESTER_TOTAL_BITS (MANCHESTER_FIELD_BIT_COUNT + MANCHESTER_PARITY_BIT_COUNT)

// sync patterns, per LLR=1553-WORD-006
static const uint8_t MANCHESTER_SYNC_COMMAND_STATUS_PATTERN[MANCHESTER_SYNC_SYMBOL_COUNT] = { 1U, 1U, 1U, 0U, 0U, 0U};
static const uint8_t MANCHESTER_SYNC_DATA_PATTERN[MANCHESTER_SYNC_SYMBOL_COUNT] = { 0U, 0U, 0U, 1U, 1U, 1U};

// implements: LLR-1553-WORD-006
static void manchester_set_symbol(mil1553_symbols_t buf, uint8_t index, uint8_t value)
{
    uint8_t byte_index = (uint8_t)(index / 8U);
    uint8_t bit_in_byte = (uint8_t)(7U - (index % 8U));

    if (value != 0U)
    {
        buf[byte_index] = (uint8_t)(buf[byte_index] | (1U << bit_in_byte));
    }
    else
    {
        buf[byte_index] = (uint8_t)(buf[byte_index] & (uint8_t)(~(uint8_t)(1U << bit_in_byte)));
    }
}

// implements: LLR-1553-WORD-007
static uint8_t manchester_get_symbol(const mil1553_symbols_t buf, uint8_t index)
{
    uint8_t byte_index = (uint8_t)(index / 8U);
    uint8_t bit_in_byte = (uint8_t)(7U - (index % 8U));

    return (uint8_t)((buf[byte_index] >> bit_in_byte) & 0x01U);
}

// implements: LLR-1553-WORD-006
bool manchester_encode_word(mil1553_sync_t sync, uint16_t field, uint8_t parity, mil1553_symbols_t out_symbols)
{
    uint8_t i;
    const uint8_t *sync_pattern;
    bool sync_is_valid;

    // zero the buffer first, regardless of outcome (defensive init)
    for (i = 0U; i < 5U; i++)
    {
        out_symbols[i] = 0U;
    }

    if (sync == MIL1553_SYNC_COMMAND_STATUS)
    {
        sync_pattern = MANCHESTER_SYNC_COMMAND_STATUS_PATTERN; 
        sync_is_valid = true;
    }
    else if (sync == MIL1553_SYNC_DATA)
    {
        sync_pattern = MANCHESTER_SYNC_DATA_PATTERN;
        sync_is_valid = true;
    }
    else
    {
        sync_pattern = MANCHESTER_SYNC_COMMAND_STATUS_PATTERN; // unused, silence warning
        sync_is_valid = false;
    }

    if (!sync_is_valid) {
        return false;
    }

    // symbols 0..5: sync pattern 
    for (i = 0U; i < MANCHESTER_SYNC_SYMBOL_COUNT; i++)
    {
        manchester_set_symbol(out_symbols, i, sync_pattern[i]);
    }

    // symbols 6..39: 17 bits (field MSB..LSB, then parity), each bit as a 2-synbol pair: logic one = (1,0); logic zero = (0,1)
    for (i = 0U; i < MANCHESTER_TOTAL_BITS; i++)
    {
        uint8_t bit_value;
        uint8_t symbol_index_a = (uint8_t)(MANCHESTER_SYNC_SYMBOL_COUNT + (i * 2U));
        uint8_t symbol_index_b = (uint8_t)(symbol_index_a + 1U);

        if (i < MANCHESTER_FIELD_BIT_COUNT)
        {
            uint8_t field_bit_pos = (uint8_t)(15U - i); // MSB first
            bit_value = (uint8_t)((field >> field_bit_pos) & 0x0001U);
        }
        else
        {
            bit_value = (uint8_t)(parity & 0x01U);
        }

        if (bit_value != 0U)
        {
            manchester_set_symbol(out_symbols, symbol_index_a, 1U);
            manchester_set_symbol(out_symbols, symbol_index_b, 0U);
        }
        else
        {
            manchester_set_symbol(out_symbols, symbol_index_a, 0U);
            manchester_set_symbol(out_symbols, symbol_index_b, 1U);
        }
    }

    return true;
}

// implements: LLR-1553-WORD-007
bool manchester_decode_word(const mil1553_symbols_t in_symbols, mil1553_sync_t *out_sync, uint16_t *out_field, 
                            uint8_t *out_parity, bool *out_violation)
{
    uint8_t i;
    bool matches_cmd_status = true;
    bool matches_data = true;
    uint16_t decoded_field = 0U;
    uint8_t decoded_parity = 0U;
    bool violation_found = false;

    // classify sync by exact pattern match (LLR-1553-WORD-007)
    for (i = 0U; i < MANCHESTER_SYNC_SYMBOL_COUNT; i++)
    {
        uint8_t s = manchester_get_symbol(in_symbols, i);

        if (s != MANCHESTER_SYNC_COMMAND_STATUS_PATTERN[i])
        {
            matches_cmd_status = false;
        }
        if (s != MANCHESTER_SYNC_DATA_PATTERN[i])
        {
            matches_data = false;
        }
    }

    if (matches_cmd_status)
    {
        *out_sync = MIL1553_SYNC_COMMAND_STATUS;
    }
    else if (matches_data)
    {
        *out_sync = MIL1553_SYNC_DATA;
    }
    else
    {
        *out_sync = MIL1553_SYNC_INVALID;
    }

    // decode the 17 bit-pairs regardless of sync validity (best-effort, per the header's documented caller-checks-out_sync contract)
    for (i = 0U; i < MANCHESTER_TOTAL_BITS; i++)
    {
        uint8_t symbol_index_a = (uint8_t)(MANCHESTER_SYNC_SYMBOL_COUNT + (i * 2U));
        uint8_t symbol_index_b = (uint8_t)(symbol_index_a + 1U);
        uint8_t a = manchester_get_symbol(in_symbols, symbol_index_a);
        uint8_t b = manchester_get_symbol(in_symbols, symbol_index_b);
        uint8_t bit_value;

        if ((a == 1U) && (b == 0U))
        {
            bit_value = 1U;
        }
        else if ((a == 0U) && (b == 1U))
        {
            bit_value = 0U;
        }
        else
        {
            // 0,0 or 1,1 : missing mid-bit transition
            bit_value = 0U;
            violation_found = true;
        }

        if (i < MANCHESTER_FIELD_BIT_COUNT) 
        {
            uint8_t field_bit_pos = (uint8_t)(15U - i);
            decoded_field = (uint16_t)(decoded_field | (uint16_t)((uint16_t)bit_value << field_bit_pos));
        }
        else{
            decoded_parity = bit_value;
        }
    }

    *out_field = decoded_field;
    *out_parity = decoded_parity;
    *out_violation = violation_found;

    return ((*out_sync != MIL1553_SYNC_INVALID) && (!violation_found));
}