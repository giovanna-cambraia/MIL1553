/*
    verifies: LLR-1553-WORD-001 .. LLR-1553-WORD-008
    modules: mil1553_word, manchester, crc_parity
*/

#include "test_support.h"
#include "crc_parity.h"
#include "manchester.h"

void setUp(void) {}
void tearDown(void) {}

// independent expected-symbol model, written from LLR-1553-WORD-006's text rather than from the implementation 
static uint8_t expected_symbol(bool data_sync, uint16_t field, uint8_t parity, unsigned i)
{
    static const uint8_t cs[6] = { 1, 1, 1, 0, 0, 0 };
    static const uint8_t ds[6] = { 0, 0, 0, 1, 1, 1};
    unsigned bit;
    uint8_t value;

    if (i < 6U) { return data_sync ? ds[i] : cs[i]; }
    bit = (i - 6U) / 2U; // 0..16 : field MSB first, then parity
    value = (bit < 16U) ? (uint8_t)((field >> (15U - bit)) & 1U) : (uint8_t)(parity & 1U);
    return ((i - 6U) % 2U == 0U) ? value : (uint8_t)(1U - value); // one = 1,0 ; zero = 0,1
}

// LLR-1553-WORD-001
void test_llr_1553_001_pack_places_fields_at_documented_bits(void)
{
    mil1553_command_fields_t c = { 5U, MIL1553_TR_TRANSMIT, 3U, 7U };
    mil1553_symbols_t s;
    uint16_t field; mil1553_sync_t sync;

    TEST_ASSERT_TRUE(mil1553_word_pack_command(&c, s));
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, ts_decode(s, &field, &sync));
    TEST_ASSERT_EQUAL(MIL1553_SYNC_COMMAND_STATUS, sync);
    TEST_ASSERT_EQUAL_HEX16(0x2C67U, field); // 5<<11 | 1<<10 | 3<<5 | 7
}

void test_llr_1553_word_001_roundtrip_boundary_values(void)
{
    const mil1553_command_fields_t cases[] = {
        { 0U, MIL1553_TR_RECEIVE, 0U, 0U },
        { 31U, MIL1553_TR_TRANSMIT, 31U, 31U},
        { 30U, MIL1553_TR_RECEIVE, 31U, 0U },
        { 1U, MIL1553_TR_TRANSMIT, 0U, 31U }
    };
    unsigned i;

    for (i = 0U; i < (sizeof(cases) / sizeof(cases[0])); i++)
    {
        mil1553_symbols_t s; uint16_t field; mil1553_command_fields_t out; 
        TEST_ASSERT_TRUE(mil1553_word_pack_command(&cases[i], s));
        TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, ts_decode(s, &field, (mil1553_sync_t *)0));
        mil1553_word_unpack_command(field, &out);
        TEST_ASSERT_EQUAL_UINT8(cases[i].rt_address, out.rt_address);
        TEST_ASSERT_EQUAL_UINT8(cases[i].tr_bit, out.tr_bit);
        TEST_ASSERT_EQUAL_UINT8(cases[i].subaddress, out.subaddress);
        TEST_ASSERT_EQUAL_UINT8(cases[i].word_count_field, out.word_count_field);
    }
}

void test_llr_1553_word_001_unpack_all_ones(void)
{
    mil1553_command_fields_t out;
    mil1553_word_unpack_command(0xFFFFU, &out);
    TEST_ASSERT_EQUAL_UINT8(31U, out.rt_address);
    TEST_ASSERT_EQUAL_UINT8(1U, out.tr_bit);
    TEST_ASSERT_EQUAL_UINT8(31, out.subaddress);
    TEST_ASSERT_EQUAL_UINT8(31U, out.word_count_field);
}

void test_llr_1553_word_001_rejects_out_of_range_without_producing_a_word(void)
{
    const mil1553_symbols_t zero = { 0, 0, 0, 0, 0};
    mil1553_command_fields_t bad_rt = { 32U, MIL1553_TR_RECEIVE, 0U, 0U };
    mil1553_command_fields_t bad_tr = { 1U, 2U, 0U, 0U };
    mil1553_command_fields_t bad_sub = { 1U, MIL1553_TR_RECEIVE, 32U, 0U };
    mil1553_command_fields_t bad_wc = { 1U, MIL1553_TR_RECEIVE, 0U, 32U };
    const mil1553_command_fields_t *bad[4] = { &bad_rt, &bad_tr, &bad_sub, &bad_wc };
    unsigned i;

    for (i = 0U; i < 4U; i++)
    {
        mil1553_symbols_t s = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        TEST_ASSERT_FALSE(mil1553_word_pack_command(bad[i], s));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, s, 5);
    }
}

// LLR-1553-WORD-002
void test_llr_1553_word_002_encode_maps_32_to_zero_and_identity_otherwise(void)
{
    mil1553_word_count_t f;
    uint8_t n;

    for (n = 1U; n <= 31U; n++)
    {
        TEST_ASSERT_TRUE(mil1553_word_encode_word_count(n, &f));
        TEST_ASSERT_EQUAL_UINT8(n, f);
    }
    TEST_ASSERT_TRUE(mil1553_word_encode_word_count(32U, &f));
    TEST_ASSERT_EQUAL_UINT8(0U, f);
}

void test_llr_1553_word_002_encode_rejects_0_and_over_32_leaving_output_untouched(void)
{
    const uint8_t bad[] = { 0U, 33U, 64U, 255U };
    unsigned i;

    for (i = 0U; i < sizeof(bad); i++)
    {
        mil1553_word_count_t f = 99U;
        TEST_ASSERT_FALSE(mil1553_word_encode_word_count(bad[i], &f));
        TEST_ASSERT_EQUAL_UINT8(99U, f);
    }
}

void test_llr_1553_word_002_decode_maps_zero_to_32_and_masks_to_five_bits(void)
{
    TEST_ASSERT_EQUAL_UINT8(32U, mil1553_word_decode_word_count(0U));
    TEST_ASSERT_EQUAL_UINT8(1U, mil1553_word_decode_word_count(1U));
    TEST_ASSERT_EQUAL_UINT8(31U, mil1553_word_decode_word_count(31U));
    TEST_ASSERT_EQUAL_UINT(32U, mil1553_word_decode_word_count(32U)); // 0x20 masks to 0
    TEST_ASSERT_EQUAL_UINT8(1U, mil1553_word_decode_word_count(33U));
}

void test_llr_1553_word_002_encode_decode_roundtrip_all_counts(void)
{
    uint8_t n;
    for (n = 1U; n <= 32U; n++)
    {
        mil1553_word_count_t f;
        TEST_ASSERT_TRUE(mil1553_word_encode_word_count(n, &f));
        TEST_ASSERT_EQUAL_UINT8(n, mil1553_word_decode_word_count(f));
    }
}

// LLR-1553-WORD-003
void test_llr_1553_003_each_status_bit_lands_at_its_position(void)
{
    const uint16_t mask[8] = { 0x0400U, 0x0200U, 0x0100U, 0x0010U, 0x0008U, 0x0004U, 0x0002U, 0x0001U };
    unsigned i;

    for (i = 0U; i < 8U; i++)
    {
        mil1553_status_fields_t s; mil1553_symbols_t sym; uint16_t field;
        (void)memset(&s, 0, sizeof(s));
        s.rt_address = 0U;
        switch (i)
        {
            case 0: s.message_error = true; break;
            case 1: s.instrumentation = true; break;
            case 2: s.service_request = true; break;
            case 3: s.broadcast_received = true; break;
            case 4: s.busy = true; break;
            case 5: s.subsystem_flag = true; break;
            case 6: s.dbc_acceptance = true; break;
            default: s.terminal_flag = true; break;
        }
        TEST_ASSERT_TRUE(mil1553_word_pack_status(&s, sym));
        (void)ts_decode(sym, &field, (mil1553_sync_t *)0);
        TEST_ASSERT_EQUAL_HEX16(mask[i], field);
    }
}

void test_llr_1553_word_003_rt_address_in_top_five_bits_and_reserved_bits_zero(void)
{
    mil1553_status_fields_t s; mil1553_symbols_t sym; uint16_t field;
    (void)memset(&s, 0, sizeof(s));
    s.rt_address = 31U;
    s.message_error = s.busy = s.subsystem_flag = s.terminal_flag = true;
    s.broadcast_received = s.instrumentation = s.service_request = s.dbc_acceptance = true;
    TEST_ASSERT_TRUE(mil1553_word_pack_status(&s, sym));
    (void)ts_decode(sym, &field, (mil1553_sync_t *)0);
    TEST_ASSERT_EQUAL_HEX16(0xF800U, (uint16_t)(field & 0xF800U));
    TEST_ASSERT_EQUAL_HEX16(0x0000U, (uint16_t)(field & 0x00E0U)); // bits 7..5
}

void test_llr_1553_word_003_unpack_flags_nonzero_reserved_bits(void)
{
    mil1553_status_fields_t out;
    mil1553_word_unpack_status(0x0000U, &out);
    TEST_ASSERT_FALSE(out.reserved_nonzero);
    mil1553_word_unpack_status(0x0020U, &out);
    TEST_ASSERT_TRUE(out.reserved_nonzero);
    mil1553_word_unpack_status(0x0080U, &out);
    TEST_ASSERT_TRUE(out.reserved_nonzero);
    mil1553_word_unpack_status(0x0100U, &out); // SR, not reserved 
    TEST_ASSERT_FALSE(out.reserved_nonzero);
    TEST_ASSERT_TRUE(out.service_request);
}

void test_llr_1553_word_003_pack_rejects_rt_address_over_31(void)
{
    mil1553_status_fields_t s; mil1553_symbols_t sym = { 1, 2, 3, 4, 5};
    const mil1553_symbols_t zero = { 0, 0, 0, 0, 0 };
    (void)memset(&s, 0, sizeof(s));
    s.rt_address = 32U;
    TEST_ASSERT_FALSE(mil1553_word_pack_status(&s, sym));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, sym, 5);
}

// LLR-1553-WORD-004
void test_llr_1553_word_004_data_value_is_bit_exact_for_patterns(void)
{
    uint16_t v;
    uint32_t k;
    for (k = 0U; k < 16U; k++)
    {
        mil1553_symbols_t s; uint16_t field;
        v = (uint16_t)(1U << k);
        mil1553_word_pack_data(v, s);
        TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, ts_decode(s, &field, (mil1553_sync_t *)0));
        TEST_ASSERT_EQUAL_HEX16(v, mil1553_word_unpack_data(field));
    }
    {
        const uint16_t special[] = { 0x0000U, 0xFFFFU, 0xA5A5U, 0x5A5AU };
        for (k = 0U; k < 4U; k++)
        {
            mil1553_symbols_t s; uint16_t field;
            mil1553_word_pack_data(special[k], s);
            (void)ts_decode(s, &field, (mil1553_sync_t *)0);
            TEST_ASSERT_EQUAL_HEX16(special[k], mil1553_word_unpack_data(field));
        }
    }
}

void test_llr_1553_word_004_data_word_carries_data_sync(void)
{
    mil1553_symbols_t s; mil1553_sync_t sync;
    mil1553_word_pack_data(0x1234U, s);
    (void)ts_decode(s, (uint16_t *)0, &sync);
    TEST_ASSERT_EQUAL(MIL1553_SYNC_DATA, sync);
}

// LLR-1553-WORD-005

void test_llr_1553_word_005_parity_makes_total_ones_odd_exhaustive(void)
{
    uint32_t v;
    for (v = 0U; v <= 0xFFFFU; v++)
    {
        uint8_t p = crc_parity_compute((uint16_t)v);
        unsigned ones = (unsigned)p, k;
        for (k = 0U; k < 16U; k++) { ones += (v >> k) & 1U; }
        TEST_ASSERT_TRUE_MESSAGE((p <= 1U) && ((ones % 2U) == 1U), "parity not odd");
    }
}

void test_llr_1553_word_005_check_accepts_correct_and_reject_flipped_exhaustive(void)
{
    uint32_t v;
    for (v = 0U; v <= 0xFFFFU; v++)
    {
        uint8_t p = crc_parity_compute((uint16_t)v);
        TEST_ASSERT_TRUE(crc_parity_check((uint16_t)v, p));
        TEST_ASSERT_FALSE(crc_parity_check((uint16_t)v, (uint8_t)(p ^ 1U)));
    }
}

void test_llr_1553_1553_word_005_check_examines_only_bit_zero_of_received_parity(void)
{
    uint8_t p = crc_parity_compute(0x0001U); // one 1-bit -> parity
    TEST_ASSERT_EQUAL_UINT8(0U, p);
    TEST_ASSERT_TRUE(crc_parity_check(0x0001U, 0x02U)); // bit0 = 0: accepted
    TEST_ASSERT_FALSE(crc_parity_check(0x0001U, 0x03U)); // bit0 = 1: rejected 
}

// LLR-1553-WORD-006
void test_llr_1553_word_006_encodes_matches_independent_symbol_model(void)
{
    const uint16_t fields[] = { 0x0000U, 0xFFFFU, 0x2C67U, 0x8001U, 0x5A5AU };
    unsigned f, i, d; 

    for (f = 0U; f < (sizeof(fields) / sizeof(fields[0])); f++)
    {
        for (d = 0U; d < 2U; d++)
        {
            mil1553_symbols_t s;
            uint8_t parity = crc_parity_compute(fields[f]);
            TEST_ASSERT_TRUE(manchester_encode_word(d ? MIL1553_SYNC_DATA : MIL1553_SYNC_COMMAND_STATUS, fields[f], parity, s));

            for (i = 0U; i < 40U; i++)
            {
                TEST_ASSERT_EQUAL_UINT8(expected_symbol(d != 0U, fields[f], parity, i), ts_symbol(s, i));
            }
        }
    }
}

void test_llr_1553_word_006_encode_rejects_non_transmittable_sync_and_zeroes_output(void)
{
    mil1553_symbols_t s = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };
    const mil1553_symbols_t zero = {0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(manchester_encode_word(MIL1553_SYNC_INVALID, 0x1234U, 0U, s));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, s, 5);
    TEST_ASSERT_FALSE(manchester_encode_word(77U, 0x1234U, 0U, s));
}

void test_llr_1553_word_006_parity_argument_uses_only_bit_zero(void)
{
    mil1553_symbols_t a, b;
     (void)manchester_encode_word(MIL1553_SYNC_DATA, 0x1234U, 0x01U, a);
    (void)manchester_encode_word(MIL1553_SYNC_DATA, 0x1234U, 0xFFU, b);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, b, 5);
}

// LLR-1553-WORD-007
void test_llr_1553_word_007_decode_valid_word_recovers_all_parts(void)
{
    mil1553_symbols_t s; mil1553_sync_t sync; uint16_t field; uint8_t parity; bool viol;
    (void)manchester_encode_word(MIL1553_SYNC_COMMAND_STATUS, 0xBEEFU, 1U, s);
    TEST_ASSERT_TRUE(manchester_decode_word(s, &sync, &field, &parity, &viol));
    TEST_ASSERT_EQUAL(MIL1553_SYNC_COMMAND_STATUS, sync);
    TEST_ASSERT_EQUAL_HEX16(0xBEEFU, field);
    TEST_ASSERT_EQUAL_UINT8(1U, parity);
    TEST_ASSERT_FALSE(viol);
}

void test_llr_1553_word_007_every_single_symbol_flip_is_detected(void)
{
    mil1553_symbols_t good, bad; mil1553_sync_t sync; uint16_t field; uint8_t parity; bool viol;
    unsigned i;
    (void)manchester_encode_word(MIL1553_SYNC_DATA, 0x3C3CU, 0U, good);

    for (i = 0U; i < 40U; i++)
    {
        (void)memcpy(bad, good, 5U);
        bad[i / 8U] = (uint8_t)(bad[i / 8U] ^ (uint8_t)(1U << (7U - (i % 8U))));
        TEST_ASSERT_FALSE(manchester_decode_word(bad, &sync, &field, &parity, &viol));
        if (i < 6U) { TEST_ASSERT_EQUAL(MIL1553_SYNC_INVALID, sync); TEST_ASSERT_FALSE(viol); }
        else        { TEST_ASSERT_TRUE(viol); TEST_ASSERT_NOT_EQUAL(MIL1553_SYNC_INVALID, sync); }
    }
}

void test_llr_1553_word_007_distinguishes_command_status_from_data_sync(void)
{
    mil1553_symbols_t a, b; mil1553_sync_t sa, sb; uint16_t f; uint8_t p; bool v;
    (void)manchester_encode_word(MIL1553_SYNC_COMMAND_STATUS, 0x1111U, 0U, a);
    (void)manchester_encode_word(MIL1553_SYNC_DATA, 0x1111U, 0U, b);
    (void)manchester_decode_word(a, &sa, &f, &p, &v);
    (void)manchester_decode_word(b, &sb, &f, &p, &v);
    TEST_ASSERT_EQUAL(MIL1553_SYNC_COMMAND_STATUS, sa);
    TEST_ASSERT_EQUAL(MIL1553_SYNC_DATA, sb);
}

// LLR-1553-WORD-008
void test_llr_1553_word_008_valid_word_classified_valid_with_fields(void)
{
    mil1553_symbols_t s; mil1553_decoded_word_t d;
    mil1553_word_pack_data(0x4242U, s);
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, mil1553_word_classify(s, &d));
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, d.classification);
    TEST_ASSERT_EQUAL_HEX16(0x4242U, d.field);
}

void test_llr_1553_word_008_wrong_parity_is_parity_error(void)
{
    mil1553_symbols_t s; mil1553_decoded_word_t d;
    uint8_t wrong = (uint8_t)(crc_parity_compute(0x4242U) ^ 1U);
    (void)manchester_encode_word(MIL1553_SYNC_DATA, 0x4242U, wrong, s);
    TEST_ASSERT_EQUAL(MIL1553_WORD_PARITY_ERROR, mil1553_word_classify(s, &d));
}

void test_llr_1553_word_008_manchester_violation_outranks_parity_error(void)
{
    mil1553_symbols_t s; mil1553_decoded_word_t d;
    uint8_t wrong = (uint8_t)(crc_parity_compute(0x4242U) ^ 1U);
    (void)manchester_encode_word(MIL1553_SYNC_DATA, 0x4242U, wrong, s);
    s[2] = (uint8_t)(s[2] ^ 0x80U); /* symbol 16: break one data bit cell */
    TEST_ASSERT_EQUAL(MIL1553_WORD_MANCHESTER_ERROR, mil1553_word_classify(s, &d));
}

void test_llr_1553_word_008_sync_error_outranks_manchester_and_parity(void)
{
    mil1553_symbols_t s; mil1553_decoded_word_t d;
    uint8_t wrong = (uint8_t)(crc_parity_compute(0x4242U) ^ 1U);
    (void)manchester_encode_word(MIL1553_SYNC_DATA, 0x4242U, wrong, s);
    s[2] = (uint8_t)(s[2] ^ 0x80U);   /* Manchester violation ... */
    s[0] = (uint8_t)(s[0] ^ 0x80U);   /* ... and a corrupted sync symbol */
    TEST_ASSERT_EQUAL(MIL1553_WORD_SYNC_ERROR, mil1553_word_classify(s, &d));
    TEST_ASSERT_EQUAL(MIL1553_SYNC_INVALID, d.sync);
}