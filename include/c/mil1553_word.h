/*
    project: MIL1553
    purpose: command/status/data word field packing and unpacking, word count encoding, and the single ordered word classification 
    function used by every module that receives bus traffic (RT, BC, BM, CBIT)

    realizes: LLR-1553-WORD-001, LLR-1553-WORD-002, LLR-1553-WORD-003, LLR-1553-WORD-004, LLR-1553-WORD-008
    parent HLRs: HLR-1553-WORD-001, HLR-1553-WORD-002, HLR-1553-WORD-003, HLR-155-WORD-005

    design note (SDD 4.2): classify_word is the single point implementing the precedence rule of LLR-1553-WORD-008
    (sync -> Manchester -> parity); every caller uses this function rather than re-implementing classification, so
    precedence cannot drift between modules

    MISRA C:2012 notes:
        - no dynamic memory, no recursion, bounded loops only
        - pack functions reject out-of-range fields without producing a word (LLR-1553-WORD-001): on rejecting the output symbols
        buffer is zeroed and the function returns false 
*/

#ifndef MIL1553_WORD_H
#define MIL1553_WORD_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// decoded word result, output of mil1553_word_classify
typedef struct {
    mil1553_word_class_t classification; // LLR-1553-WORD-008   
    mil1553_sync_t sync; // best-effort; see classify doc 
    uint16_t field; // best-effort; see classify doc 
    uint8_t parity; // best-effort; see classify doc 
} mil1553_decoded_word_t;

// word count encoding, LLR-1553-WORD-002

/*
    implements: LLR-1553-WORD-002
    encodes a logical data word count (1-32) into the wire word count field value (1-31, or 0 for 32)

    returns true and writes *out_field_value on success; returns false (and does not write *out_field_value) if count 0 or greater
    than MIL1553_MAX_DATA_WORD
*/
bool mil1553_word_encode_word_count(uint8_t count, mil1553_word_count_t *out_field_value);

/*
    implements: LLR-1553-WORD-002

    decodes a wire word count field value (0-31) into the logical data word count (1-32; 0 maps to 32); field_value is masked to 5 bits
    (0-31) defensively; all 32 possible values are valid on decode (unlike encode, decode never fails)
*/
uint8_t mil1553_word_decode_word_count(mil1553_word_count_t field_value);

// command word, LLR-1553-WORD-001

/*
    implements: LLR-1553-WORD-001

    packs command word fields into a 16-bit information field, computes parity, and Manchester-encodes the result with command/status
    sync into out_symbols

    validates: rt_address <= 31, tr_bit is TR_RECEIVE or TR_TRANSMIT, subaddress <= 31, word_count_field <= 31 (caller is responsible
    for having already run the count through mil1553_word_encode_word_count)

    on validation failure, out_symbols is zeroed and the function returns false without producing a word (LLR-1553-WORD-001)
*/
bool mil1553_word_pack_command(const mil1553_command_fields_t *fields, mil1553_symbols_t out_symbols);

/*
    implements: LLR-1553-WORD-001

    unpacks a 16-bit command information field into its component fields; always suceeds (every 16-bit pattern decodes to some
    rt_adress/tr_bit/subaddress/word_count_field combination); callers needing to know if the word wsa received without transmission
    error should use mil1553_word_classify first
*/
void mil1553_word_unpack_command(uint16_t field, mil1553_command_fields_t *out_fields);

// status word, LLR-1553-WORD-003

/*
    implements: LLR-1553-WORD-003

    packs status word fiels into a 16-bit information field with the reserved bits (7..5) forced to zero, computes parity, and
    Manchester encodes the result with command/status sync into out_symbols

    validates: rt_address <= 31; there is no other invalid combination of boolean status bits, so this is the only rejection case
*/
bool mil1553_word_pack_status(const mil1553_status_fields_t *fields, mil1553_symbols_t out_symbols);

/*
    implements: LLR-1553-WORD-003

    unpacks a 16-bit status information field into its component fields; sets out_fields->reserved_nonzero true if any of bits 7..5
    was nonzero in the received field; always succeeds
*/
void mil1553_word_unpack_status(uint16_t field, mil1553_status_fields_t *out_fields);

// data word, LLR-1553-WORD-004

/*
    implements: LLR-1553-WORD-004

    packs a 16-bit data value, computes parity, and Manchester-encodes the result with data sync into out_symbols;
    never fails (every 16-bit value is a valid data word)
*/
void mil1553_word_pack_data(mil1553_data_t value, mil1553_symbols_t out_symbols);

/*
    implements: LLR-1553-WORD-004

    unpacks a 16-bit data field to its value, unchanged (bit_exact transparency - LLR-1553-WORD-004)
*/
mil1553_data_t mil1553_word_unpack_data(uint16_t field);

// classification, LLR-1553-WORD-008

/*
    classifies a received word (40 Manchester symbols) as exactly one of MIL1553_WORD_VALID,  MIL1553_WORD_SYNC_ERROR,
    MIL1553_WORD_MANCHESTER_ERROR or MIL1553_WORD_PARITY_ERROR, evaluated in that precedence order:
    1. sync must classify as COMMAND_STATUS or DATA (else SYNC_ERROR)
    2. no Manchester violation may be present (else MANCHESTER_ERROR)
    3. parity must check out (else PARITY_ERROR)

    out_decoded->field and -> parity are populated in every case (best-effirt, taken from manchester_decode_word's best-effort
    output) but are only meaningful for the caller's use when classification is MIL1553_WORD_VALID
*/
mil1553_word_class_t mil1553_word_classify(const mil1553_symbols_t in_symbols, mil1553_decoded_word_t *out_decoded);

#ifdef __cplusplus
}
#endif

#endif
