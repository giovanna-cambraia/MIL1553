/*
    project: MIL1553

    realizes: LLR-1553-WORD-001, LLR-1553-WORD-002, LLR-1553-WORD-004, LLR-1553-WORD-008

    MISRA C:2012 notes:
        - no dynamic memory, no recursion, bounded loops only
        - classify_word is the single precedence-ordered implementation
        (SDD 4.2); do not duplicate this logic elsewhere
*/

#include "mil1553_word.h"
#include "manchester.h"
#include "crc_parity.h"

// bit positions within the 16-bit information field, MSB = bit 15
#define WORD_CMD_RT_ADDR_SHIFT (11U)
#define WORD_CMD_TR_SHIFT (10U)
#define WORD_CMD_SUBADDR_SHIFT (5U)
#define WORD_CMD_WC_SHIFT (0U)

#define WORD_STATUS_RT_ADDR_SHIFT (11U)
#define WORD_STATUS_ME_SHIFT (10U)
#define WORD_STATUS_INSTR_SHIFT (9U)
#define WORD_STATUS_SR_SHIFT (8U)
#define WORD_STATUS_RESERVED_SHIFT (5U)
#define WORD_STATUS_RESERVED_MASK (0x07U) // 3 bits 7..5
#define WORD_STATUS_BCR_SHIFT (4U)
#define WORD_STATUS_BUSY_SHIFT (3U)
#define WORD_STATUS_SF_SHIFT (2U)
#define WORD_STATUS_DBCA_SHIFT (1U)
#define WORD_STATUS_TF_SHIFT (0U)

#define WORD_FIELD_5BIT_MASK (0x1FU)
#define WORD_FIELD_1BIT_MASK (0x01U)

// word count encoding, LLR-1553-WORD-002

// implements: LLR-1553-WORD-002
bool mil1553_word_encode_word_count(uint8_t count, mil1553_word_count_t *out_field_value)
{
    bool result;

    if ((count == 0U) || (count > MIL1553_MAX_DATA_WORDS))
    {
        result = false;
    }
    else if (count == MIL1553_MAX_DATA_WORDS)
    {
        *out_field_value = 0U;
        result = true;
    }
    else
    {
        *out_field_value = (mil1553_word_count_t)count;
        result = true;
    }

    return result;
}

// implements: LLR-1553-WORD-002
uint8_t mil1553_word_decode_word_count(mil1553_word_count_t field_value)
{
    uint8_t masked = (uint8_t)(field_value & WORD_FIELD_5BIT_MASK);
    uint8_t result;

    if (masked == 0U)
    {
        result = (uint8_t)MIL1553_MAX_DATA_WORDS;
    }
    else
    {
        result = masked;
    }

    return result;
}

// command word, LLR-1553-WORD-001

// implements: LLR-1553-WORD-001
bool mil1553_word_pack_command(const mil1553_command_fields_t *fields, mil1553_symbols_t out_symbols)
{
    bool valid;
    uint16_t packed_field;
    uint8_t parity;

    valid = (fields->rt_address <= 31U) &&
            ((fields->tr_bit == MIL1553_TR_RECEIVE) || (fields->tr_bit == MIL1553_TR_TRANSMIT)) &&
            (fields->subaddress <= 31U) && (fields->word_count_field <= 31U);

    if (!valid)
    {
        (void)manchester_encode_word(MIL1553_SYNC_INVALID, 0U, 0U, out_symbols);
        return false;
    }

    packed_field = (uint16_t)(((uint16_t)((uint16_t)fields->rt_address & WORD_FIELD_5BIT_MASK) << WORD_CMD_RT_ADDR_SHIFT) |
                              ((uint16_t)((uint16_t)fields->tr_bit & WORD_FIELD_1BIT_MASK) << WORD_CMD_TR_SHIFT) |
                              ((uint16_t)((uint16_t)fields->subaddress & WORD_FIELD_5BIT_MASK) << WORD_CMD_SUBADDR_SHIFT) |
                              ((uint16_t)((uint16_t)fields->word_count_field & WORD_FIELD_5BIT_MASK) << WORD_CMD_WC_SHIFT));

    parity = crc_parity_compute(packed_field);

    return manchester_encode_word(MIL1553_SYNC_COMMAND_STATUS, packed_field, parity, out_symbols);
}

// implements: LLR-1553-WORD-001
void mil1553_word_unpack_command(uint16_t field, mil1553_command_fields_t *out_fields)
{
    out_fields->rt_address = (mil1553_rt_addr_t)((field >> WORD_CMD_RT_ADDR_SHIFT) & WORD_FIELD_5BIT_MASK);
    out_fields->tr_bit = (mil1553_tr_t)((field >> WORD_CMD_TR_SHIFT) & WORD_FIELD_1BIT_MASK);
    out_fields->subaddress = (mil1553_subaddr_t)((field >> WORD_CMD_SUBADDR_SHIFT) & WORD_FIELD_5BIT_MASK);
    out_fields->word_count_field = (mil1553_word_count_t)((field >> WORD_CMD_WC_SHIFT) & WORD_FIELD_5BIT_MASK);
}

// status word, LLR-1553-WORD-003

// implements: LLR-1553-WORD-003
bool mil1553_word_pack_status(const mil1553_status_fields_t *fields,
                              mil1553_symbols_t out_symbols)
{
    uint16_t packed_field;
    uint8_t parity;

    if (fields->rt_address > 31U)
    {
        (void)manchester_encode_word(MIL1553_SYNC_INVALID, 0U, 0U, out_symbols);
        return false;
    }

    packed_field = (uint16_t)(((uint16_t)((uint16_t)fields->rt_address & WORD_FIELD_5BIT_MASK) << WORD_STATUS_RT_ADDR_SHIFT) |
                              ((uint16_t)(fields->message_error ? 1U : 0U) << WORD_STATUS_ME_SHIFT) |
                              ((uint16_t)(fields->instrumentation ? 1U : 0U) << WORD_STATUS_INSTR_SHIFT) |
                              ((uint16_t)(fields->service_request ? 1U : 0U) << WORD_STATUS_SR_SHIFT) |
                              // bits 7..5 reserved: forced to zero on encode, per LLR-1553-WORD-003
                              ((uint16_t)(fields->broadcast_received ? 1U : 0U) << WORD_STATUS_BCR_SHIFT) |
                              ((uint16_t)(fields->busy ? 1U : 0U) << WORD_STATUS_BUSY_SHIFT) |
                              ((uint16_t)(fields->subsystem_flag ? 1U : 0U) << WORD_STATUS_SF_SHIFT) |
                              ((uint16_t)(fields->dbc_acceptance ? 1U : 0U) << WORD_STATUS_DBCA_SHIFT) |
                              ((uint16_t)(fields->terminal_flag ? 1U : 0U) << WORD_STATUS_TF_SHIFT));

    parity = crc_parity_compute(packed_field);

    return manchester_encode_word(MIL1553_SYNC_COMMAND_STATUS, packed_field, parity, out_symbols);
}

// implements: LLR-1553-WORD-003
void mil1553_word_unpack_status(uint16_t field, mil1553_status_fields_t *out_fields)
{
    uint16_t reserved_bits = (uint16_t)((field >> WORD_STATUS_RESERVED_SHIFT) & WORD_STATUS_RESERVED_MASK);

    out_fields->rt_address = (mil1553_rt_addr_t)((field >> WORD_STATUS_RT_ADDR_SHIFT) & WORD_FIELD_5BIT_MASK);
    out_fields->message_error = (((field >> WORD_STATUS_ME_SHIFT) & WORD_FIELD_1BIT_MASK) != 0U);
    out_fields->instrumentation = (((field >> WORD_STATUS_INSTR_SHIFT) & WORD_FIELD_1BIT_MASK) != 0U);
    out_fields->service_request = (((field >> WORD_STATUS_SR_SHIFT) & WORD_FIELD_1BIT_MASK) != 0U);
    out_fields->broadcast_received = (((field >> WORD_STATUS_BCR_SHIFT) & WORD_FIELD_1BIT_MASK) != 0U);
    out_fields->busy = (((field >> WORD_STATUS_BUSY_SHIFT) & WORD_FIELD_1BIT_MASK) != 0U);
    out_fields->subsystem_flag = (((field >> WORD_STATUS_SF_SHIFT) & WORD_FIELD_1BIT_MASK) != 0U);
    out_fields->dbc_acceptance = (((field >> WORD_STATUS_DBCA_SHIFT) & WORD_FIELD_1BIT_MASK) != 0U);
    out_fields->terminal_flag = (((field >> WORD_STATUS_TF_SHIFT) & WORD_FIELD_1BIT_MASK) != 0U);
    out_fields->reserved_nonzero = (reserved_bits != 0U);
}

// data word, LLR-1553-WORD-004

// implements: LLR-1553-WORD-004
void mil1553_word_pack_data(mil1553_data_t value, mil1553_symbols_t out_symbols)
{
    uint8_t parity = crc_parity_compute((uint16_t)value);

    (void)manchester_encode_word(MIL1553_SYNC_DATA, (uint16_t)value, parity, out_symbols);
}

// implements: LLR-1553-WORD-004
mil1553_data_t mil1553_word_unpack_data(uint16_t field)
{
    return (mil1553_data_t)field;
}

// classification, LLR-1553-WORD-008

// implements: LLR-1553-WORD-008
mil1553_word_class_t mil1553_word_classify(const mil1553_symbols_t in_symbols, mil1553_decoded_word_t *out_decoded)
{
    mil1553_sync_t sync;
    uint16_t field;
    uint8_t parity;
    bool violation;
    mil1553_word_class_t result;

    (void)manchester_decode_word(in_symbols, &sync, &field, &parity, &violation);

    out_decoded->sync = sync;
    out_decoded->field = field;
    out_decoded->parity = parity;

    // precedence order per LLR-1553-WORD-008: sync -> Manchester -> parity
    if (sync == MIL1553_SYNC_INVALID)
    {
        result = MIL1553_WORD_SYNC_ERROR;
    }
    else if (violation)
    {
        result = MIL1553_WORD_MANCHESTER_ERROR;
    }
    else if (!crc_parity_check(field, parity))
    {
        result = MIL1553_WORD_PARITY_ERROR;
    }
    else
    {
        result = MIL1553_WORD_VALID;
    }

    out_decoded->classification = result;

    return result;
}