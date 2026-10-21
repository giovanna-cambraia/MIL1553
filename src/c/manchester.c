/*
    project: MIL1553
    realizes: LLR-1553-WORD-006, LLR-1553-WORD-007

    MISRA C:2012 notes:
        - no dynamic memory, no recursion, bounded loops only (fixed 40 iteration counts)
        - symbol get/set helpers are file-local (static)
*/

#include "mil1553_manchester.h"

#define MANCHESTER_SYNC_SYMBOL_COUNT    (6U)
#define MANCHESTER_FIELD_BIT_COUNT      (16U)
#define MANCHESTER_PARITY_BIT_COUNT     (1U)
#define MANCHESTER_TOTAL_BITS           (MANCHESTER_FIELD_BIT_COUNT + MANCHESTER_PARITY_BIT_COUNT)

// sync patterns, per LLR-1553-WORD-006 
static const uint8_t MANCHESTER_SYNC_COMMAND_STATUS_PATTERN[MANCHESTER_SYNC_SYMBOL_COUNT] = { 1U, 1U, 1U, 0U, 0U, 0U };
static const uint8_t MANCHESTER_SYNC_DATA_PATTERN[MANCHESTER_SYNC_SYMBOL_COUNT]           = { 0U, 0U, 0U, 1U, 1U, 1U };

// implements: LLR-1553-WORD-006 
static void manchester_set_symbol(mil1553_symbols_t buf, uint8_t index, uint8_t value)
{
    uint8_t byte_index = (uint8_t)(index / 8U);
    uint8_t bit_in_byte = (uint8_t)(7U - (index % 8U));
    uint8_t mask = (uint8_t)(1U << bit_in_byte);

    if (value != 0U)
    {
        buf[byte_index] = (uint8_t)(buf[byte_index] | mask);
    }
    else
    {
        buf[byte_index] = (uint8_t)(buf[byte_index] & (uint8_t)(~mask));
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

    if (out_symbols == NULL)
    {
        return false;
    }

    // defensive zeroing across the symbol array 
    for (i = 0U; i < (uint8_t)sizeof(mil1553_symbols_t); i++)
    {
        out_symbols[i] = 0U;
    }

    if (sync == MIL1553_SYNC_COMMAND_STATUS)
    {
        sync_pattern = MANCHESTER_SYNC_COMMAND_STATUS_PATTERN;
    }
    else if (sync == MIL1553_SYNC_DATA)
    {
        sync_pattern = MANCHESTER_SYNC_DATA_PATTERN;
    }
    else
    {
        // invalid or non-transmittable sync (e.g., MIL1553_SYNC_INVALID) 
        return false;
    }

    // symbols 0..5: sync pattern 
    for (i = 0U; i < MANCHESTER_SYNC_SYMBOL_COUNT; i++)
    {
        manchester_set_symbol(out_symbols, i, sync_pattern[i]);
    }

    // symbols 6..39: 17 bits (field MSB..LSB, then parity), logic 1 = (1, 0); Logic 0 = (0, 1)
    for (i = 0U; i < MANCHESTER_TOTAL_BITS; i++)
    {
        uint8_t bit_value;
        uint8_t symbol_index_a = (uint8_t)(MANCHESTER_SYNC_SYMBOL_COUNT + (i * 2U));
        uint8_t symbol_index_b = (uint8_t)(symbol_index_a + 1U);

        if (i < MANCHESTER_FIELD_BIT_COUNT)
        {
            uint8_t field_bit_pos = (uint8_t)(15U - i);
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
bool manchester_decode_word(const mil1553_symbols_t in_symbols,
                            mil1553_sync_t *out_sync,
                            uint16_t *out_field,
                            uint8_t *out_parity,
                            bool *out_violation)
{
    uint8_t i;
    bool matches_cmd_status = true;
    bool matches_data = true;
    uint16_t decoded_field = 0U;
    uint8_t decoded_parity = 0U;
    bool violation_found = false;
    mil1553_sync_t local_sync;

    if (in_symbols == NULL)
    {
        if (out_sync != NULL)      { *out_sync = MIL1553_SYNC_INVALID; }
        if (out_field != NULL)     { *out_field = 0U; }
        if (out_parity != NULL)    { *out_parity = 0U; }
        if (out_violation != NULL) { *out_violation = true; }
        return false;
    }

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
        local_sync = MIL1553_SYNC_COMMAND_STATUS;
    }
    else if (matches_data)
    {
        local_sync = MIL1553_SYNC_DATA;
    }
    else
    {
        local_sync = MIL1553_SYNC_INVALID;
    }

    // decode 17 bit-pairs regardless of sync validity (best-effort) 
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
            // 0,0 or 1,1: missing mid-bit transition (Manchester violation) 
            bit_value = 0U;
            violation_found = true;
        }

        if (i < MANCHESTER_FIELD_BIT_COUNT)
        {
            uint8_t field_bit_pos = (uint8_t)(15U - i);
            decoded_field = (uint16_t)(decoded_field | (uint16_t)((uint16_t)bit_value << field_bit_pos));
        }
        else
        {
            decoded_parity = bit_value;
        }
    }

    // write outputs defensively if non-NULL pointers were provided 
    if (out_sync != NULL)
    {
        *out_sync = local_sync;
    }
    if (out_field != NULL)
    {
        *out_field = decoded_field;
    }
    if (out_parity != NULL)
    {
        *out_parity = decoded_parity;
    }
    if (out_violation != NULL)
    {
        *out_violation = violation_found;
    }

    return ((local_sync != MIL1553_SYNC_INVALID) && (!violation_found));
}