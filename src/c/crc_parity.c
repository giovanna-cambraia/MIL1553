/*
    project: MIL1553
    realizes: LLR-1553-WORD-005

    MISRA C: 2012 notes:
        - no dynamic memory, no recursion, no unbounded loops (fixed 16 iterations, bounded in by the field width)
        - defensive: received_parity is masked to its low bit rather than assumed to already be 0/1 (rule 10.x - do not
        trust caller's representation of a "boolean-like" integer parameter)
*/

#include "crc_parity.h"

// implements: LLR-1553-WORD-005
uint8_t crc_parity_compute(uint16_t field)
{
    uint8_t ones_count = 0U;
    uint16_t remaining = field;
    uint8_t bit_index;

    for (bit_index = 0U; bit_index < 16U; bit_index++)
    {
        if ((remaining & 0x0001U) != 0U)
        {
            ones_count++;
        }
        remaining = (uint16_t)(remaining >> 1U);
    }

    // odd parity: total ones (field + parity) must be odd
    // if field's ones_count is already odd, parity bit = 0
    // if field's ones_count is even, parity bit = 1
    return (uint8_t)((ones_count % 2U) == 0U ? 1U : 0U);
}

// implements: LLR-1553-WORD-005
bool crc_parity_check(uint16_t field, uint8_t received_parity)
{
    uint8_t expected_parity = crc_parity_compute(field);
    uint8_t masked_received = (uint8_t)(received_parity & 0x01U);

    return (expected_parity == masked_received);
}