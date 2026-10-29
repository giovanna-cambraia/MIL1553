/*
 
   verifies: LLR-1553-ERR-006, LLR-1553-ERR-007, LLR-1553-ERR-008, LLR-1553-ERR-009 (LLR-1553-ERR-005 is checked by check_errinject_exclusion.sh)
   module:   error_inject, config_signature, bus_model_tx_hooked
  
   TEST CONFIGURATION ONLY: built with -DMIL1553_ENABLE_ERROR_INJECTION and -DERRINJECT_HARNESS_TOKEN=..., with bus_model_tx_hooked.c linked in place of
   bus_model_tx.c (the Makefile target `test_errinject` does this)
  
   scenario files are created on the fly, signed with the module's own SHA-256, and loaded through the real loader, so every test goes through the same gates
   a campaign does; fixture addresses: RT1 = 5, RT2 = 6
 */

#ifndef MIL1553_ENABLE_ERROR_INJECTION
#error "test_errinject.c must be built in the test configuration"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "test_support.h"
#include "error_inject.h"
#include "config_signature.h"

#define YAML_PATH "ut_scn.yaml"
#define SIG_PATH  "ut_scn.sig"
#define JSON_PATH "ut_results.json"
#define TOKEN     ((uint32_t)ERRINJECT_HARNESS_TOKEN)

static mil1553_bus_model_context_t bus;

typedef struct { uint16_t field; mil1553_sync_t sync; mil1553_word_class_t cls; mil1553_time_us_t ts; mil1553_terminal_id_t sender; } sw_t;

void setUp(void)
{
    bus_model_init(&bus);
    errinject_reset();
}

void tearDown(void)
{
    (void)remove(YAML_PATH);
    (void)remove(SIG_PATH);
    (void)remove(JSON_PATH);
}

// file helpers

static void write_bytes(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    if (len > 0U) { TEST_ASSERT_EQUAL_UINT(len, fwrite(data, 1U, len, f)); }
    (void)fclose(f);
}

static void hex32(const uint8_t d[32], char out[65])
{
    unsigned i;
    for (i = 0U; i < 32U; i++) { (void)sprintf(&out[i * 2U], "%02x", d[i]); }
    out[64] = '\0';
}

// signature file = SHA-256 of the YAML file's current bytes, followed by `suffix` 
static void write_sig_for_file(const char *yaml_path, const char *sig_path, const char *suffix)
{
    static uint8_t buf[20000];
    uint8_t digest[32]; char hex[65 + 128];
    FILE *f = fopen(yaml_path, "rb");
    size_t n;
    TEST_ASSERT_NOT_NULL(f);
    n = fread(buf, 1U, sizeof(buf), f);
    (void)fclose(f);
    errinject_sha256(buf, (uint32_t)n, digest);
    hex32(digest, hex);
    (void)strcat(hex, suffix);
    write_bytes(sig_path, hex, strlen(hex));
}

static bool load_text(const char *text)
{
    write_bytes(YAML_PATH, text, strlen(text));
    write_sig_for_file(YAML_PATH, SIG_PATH, "\n");
    return errinject_load_scenarios(YAML_PATH, SIG_PATH);
}

static void arm_and_begin(const char *text)
{
    TEST_ASSERT_TRUE(load_text(text));
    errinject_register_rt_address(MIL1553_TERMINAL_RT1, 5U);
    errinject_register_rt_address(MIL1553_TERMINAL_RT2, 6U);
    TEST_ASSERT_TRUE(errinject_arm(TOKEN));
    TEST_ASSERT_TRUE(errinject_begin_scenario(0U, &bus));
}

static unsigned read_all(mil1553_channel_t ch, sw_t *out, unsigned max)
{
    unsigned n = 0U; mil1553_bus_record_t r;
    while ((n < max) && ts_rx(&bus, ch, MIL1553_TERMINAL_BM, &r))
    {
        out[n].cls = ts_decode(r.symbols, &out[n].field, &out[n].sync);
        out[n].ts = r.timestamp_us; out[n].sender = r.sender;
        n++;
    }
    return n;
}

static void send_data(mil1553_terminal_id_t who, mil1553_channel_t ch, uint16_t v)
{
    mil1553_symbols_t s; ts_data(s, v);
    (void)ts_tx(&bus, ch, who, s);
}

static void send_status(mil1553_terminal_id_t who, mil1553_channel_t ch, uint8_t rt, bool sf)
{
    mil1553_symbols_t s; ts_status(s, rt, false, false, sf, false, false);
    (void)ts_tx(&bus, ch, who, s);
}

static void send_cmd(mil1553_terminal_id_t who, mil1553_channel_t ch, uint8_t rt, mil1553_tr_t tr, uint8_t sub, uint8_t wc)
{
    mil1553_symbols_t s; ts_cmd(s, rt, tr, sub, wc);
    (void)ts_tx(&bus, ch, who, s);
}

// a scenario block: the five fields the loader requires, plus extras 
#define SC(fault, extra) "- id: T1\n target: RT1\n fault: " fault "\n" extra " expected_detection: NONE\n"

// LLR-1553-ERR-007: SHA-256

static void expect_sha(const void *data, uint32_t len, const char *want)
{
    uint8_t d[32]; char hex[65];
    errinject_sha256((const uint8_t *)data, len, d);
    hex32(d, hex);
    TEST_ASSERT_EQUAL_STRING(want, hex);
}

void test_llr_1553_err_007_sha256_matches_the_nist_vectors(void)
{
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const char *four = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    expect_sha("", 0U, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect_sha("abc", 3U, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expect_sha(two, (uint32_t)strlen(two), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    expect_sha(four, (uint32_t)strlen(four), "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

void test_llr_1553_err_007_sha256_is_right_at_every_padding_boundary(void)
{
    static uint8_t a[1000];
    static const struct { uint32_t n; const char *hex; } v[] = {
        { 1U,    "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb" },
        { 55U,   "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
        { 56U,   "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" },
        { 63U,   "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34" },
        { 64U,   "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" },
        { 65U,   "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0" },
        { 119U,  "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb" },
        { 120U,  "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c" },
        { 127U,  "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a" },
        { 128U,  "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e" },
        { 1000U, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3" }
    };
    unsigned i;
    (void)memset(a, 'a', sizeof(a));
    for (i = 0U; i < (sizeof(v) / sizeof(v[0])); i++) { expect_sha(a, v[i].n, v[i].hex); }
}

// LLR-1553-ERR-007: signature check 

static errinject_sig_result_t check_with_sig_text(const char *data, const char *sig_text)
{
    write_bytes(SIG_PATH, sig_text, strlen(sig_text));
    return errinject_check_signature((const uint8_t *)data, (uint32_t)strlen(data), SIG_PATH);
}

static void digest_hex_of(const char *data, char out[65])
{
    uint8_t d[32];
    errinject_sha256((const uint8_t *)data, (uint32_t)strlen(data), d);
    hex32(d, out);
}

void test_llr_1553_err_007_a_matching_signature_is_accepted_in_every_common_format(void)
{
    const char *data = "- id: X\n"; char hex[65]; char text[256]; unsigned i;
    digest_hex_of(data, hex);

    TEST_ASSERT_EQUAL(ERRINJECT_SIG_OK, check_with_sig_text(data, hex)); // bare digest 
    (void)sprintf(text, "%s\n", hex);
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_OK, check_with_sig_text(data, text)); // trailing newline 
    (void)sprintf(text, "%s  error_scenarios.yaml\n", hex);
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_OK, check_with_sig_text(data, text)); // sha256sum output 
    (void)sprintf(text, "\r\n  \t%s\r\n", hex);
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_OK, check_with_sig_text(data, text)); // leading whitespace, CRLF 
    for (i = 0U; i < 64U; i++) { text[i] = (char)((hex[i] >= 'a' && hex[i] <= 'f') ? (hex[i] - 32) : hex[i]); }
    text[64] = '\0';
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_OK, check_with_sig_text(data, text)); // upper-case hex 
}

void test_llr_1553_err_007_one_changed_byte_in_the_data_or_the_signature_is_a_mismatch(void)
{
    const char *data = "- id: X\n"; char hex[65]; char bad[65];
    digest_hex_of(data, hex);
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_MISMATCH, check_with_sig_text("- id: Y\n", hex)); // data changed 
    (void)strcpy(bad, hex); bad[63] = (bad[63] == '0') ? '1' : '0';
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_MISMATCH, check_with_sig_text(data, bad)); // last digit changed 
    (void)strcpy(bad, hex); bad[0] = (bad[0] == '0') ? '1' : '0';
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_MISMATCH, check_with_sig_text(data, bad)); // first digit changed 
}

void test_llr_1553_err_007_a_malformed_signature_file_is_a_mismatch_and_a_missing_one_is_missing(void)
{
    const char *data = "- id: X\n"; char hex[65]; char text[100];
    digest_hex_of(data, hex);
    (void)strcpy(text, hex); text[63] = '\0';
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_MISMATCH, check_with_sig_text(data, text)); // 63 digits 
    (void)strcpy(text, hex); text[10] = 'z';
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_MISMATCH, check_with_sig_text(data, text)); // not hex 
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_MISMATCH, check_with_sig_text(data, "")); // empty 
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_MISMATCH, check_with_sig_text(data, "   \n  ")); // only whitespace */
    (void)remove(SIG_PATH);
    TEST_ASSERT_EQUAL(ERRINJECT_SIG_MISSING,
                      errinject_check_signature((const uint8_t *)data, (uint32_t)strlen(data), SIG_PATH));
}

void test_llr_1553_err_007_changing_only_the_line_endings_breaks_the_signature(void)
{
    // why config/error_scenarios.yaml must be kept out of git's newline conversion 
    const char *lf = SC("BUS_A_FAIL", "");
    char crlf[512]; size_t i, j = 0U;
    TEST_ASSERT_TRUE(load_text(lf)); // signed as LF 
    for (i = 0U; lf[i] != '\0'; i++) { if (lf[i] == '\n') { crlf[j++] = '\r'; } crlf[j++] = lf[i]; }
    crlf[j] = '\0';
    write_bytes(YAML_PATH, crlf, strlen(crlf));
    TEST_ASSERT_FALSE(errinject_load_scenarios(YAML_PATH, SIG_PATH));
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_SIG_MISMATCH, errinject_last_reject());
    TEST_ASSERT_TRUE(load_text(crlf)); // re-signed as CRLF: fine 
    TEST_ASSERT_EQUAL_UINT32(1U, errinject_scenario_count());
}

// LLR-1553-ERR-007: loading

void test_llr_1553_err_007_a_signed_file_loads_with_every_field_parsed(void)
{
    const errinject_scenario_t *s;
    TEST_ASSERT_TRUE(load_text(
        "# a comment\n\nscenarios:\n"
        "  - id: ERR-042          # trailing comment\n"
        "    target: RT2\n"
        "    bus: B\n"
        "    fault: STATUS_BIT_INJECT\n"
        "    bit: TF\n"
        "    word_index: 7\n"
        "    count: 3\n"
        "    duration_ms: 50\n"
        "    delay_us: 20\n"
        "    expected_detection: STATUS_TF\n"
        "    expected_recovery: RT_MARKED_FAILED\n"
        "    hlr: HLR-1553-ERR-002\n"
        "    llr: LLR-1553-ERR-002\n"
        "    test_id: TC-ERR-042\n"));
    TEST_ASSERT_EQUAL_UINT32(1U, errinject_scenario_count());
    s = errinject_get_scenario(0U);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING("ERR-042", s->id);
    TEST_ASSERT_EQUAL(MIL1553_TERMINAL_RT2, s->target);
    TEST_ASSERT_EQUAL_UINT8(ERRINJECT_BUS_B, s->bus_sel);
    TEST_ASSERT_EQUAL(MIL1553_FAULT_STATUS_BIT_INJECT, s->fault);
    TEST_ASSERT_EQUAL(ERRINJECT_BIT_TF, s->status_bit);
    TEST_ASSERT_EQUAL_UINT32(7U, s->word_index);
    TEST_ASSERT_EQUAL_UINT32(3U, s->count);
    TEST_ASSERT_EQUAL_UINT32(50000U, s->duration_us); // ms -> us 
    TEST_ASSERT_EQUAL_UINT32(20U, s->delay_us);
    TEST_ASSERT_EQUAL_STRING("STATUS_TF", s->expected_detection);
    TEST_ASSERT_EQUAL_STRING("RT_MARKED_FAILED", s->expected_recovery);
    TEST_ASSERT_EQUAL_STRING("HLR-1553-ERR-002", s->hlr);
    TEST_ASSERT_EQUAL_STRING("LLR-1553-ERR-002", s->llr);
    TEST_ASSERT_EQUAL_STRING("TC-ERR-042", s->test_id);
}

void test_llr_1553_err_007_omitted_optional_fields_take_their_defaults(void)
{
    const errinject_scenario_t *s;
    TEST_ASSERT_TRUE(load_text(SC("PARITY_BIT_FLIP", "")));
    s = errinject_get_scenario(0U);
    TEST_ASSERT_EQUAL_UINT8(ERRINJECT_BUS_ANY, s->bus_sel);
    TEST_ASSERT_EQUAL_UINT32(1U, s->count);
    TEST_ASSERT_EQUAL_UINT32(0U, s->word_index);
    TEST_ASSERT_EQUAL_STRING("NONE", s->expected_recovery);
    TEST_ASSERT_EQUAL_STRING("", s->hlr);
}

void test_llr_1553_err_007_scenarios_keep_file_order_and_out_of_range_lookup_is_null(void)
{
    TEST_ASSERT_TRUE(load_text(SC("BUS_A_FAIL", "") "- id: T2\n target: BC\n fault: BUS_B_FAIL\n expected_detection: NONE\n"
                               "- id: T3\n target: BM\n fault: CORRUPT_SYNC\n expected_detection: SYNC\n"));
    TEST_ASSERT_EQUAL_UINT32(3U, errinject_scenario_count());
    TEST_ASSERT_EQUAL_STRING("T1", errinject_get_scenario(0U)->id);
    TEST_ASSERT_EQUAL_STRING("T2", errinject_get_scenario(1U)->id);
    TEST_ASSERT_EQUAL_STRING("T3", errinject_get_scenario(2U)->id);
    TEST_ASSERT_NULL(errinject_get_scenario(3U));
}

void test_llr_1553_err_007_thirty_two_scenarios_are_accepted_and_thirty_three_are_not(void)
{
    static char big[8192]; unsigned i; size_t n = 0U;
    for (i = 0U; i < 32U; i++)
    {
        n += (size_t)sprintf(&big[n], "- id: S%u\n target: RT1\n fault: BUS_A_FAIL\n expected_detection: NONE\n", i);
    }
    TEST_ASSERT_TRUE(load_text(big));
    TEST_ASSERT_EQUAL_UINT32(32U, errinject_scenario_count());
    n += (size_t)sprintf(&big[n], "- id: S32\n target: RT1\n fault: BUS_A_FAIL\n expected_detection: NONE\n");
    TEST_ASSERT_FALSE(load_text(big));
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_YAML_PARSE, errinject_last_reject());
    TEST_ASSERT_EQUAL_UINT32(0U, errinject_scenario_count());
}

static void expect_parse_reject(const char *text)
{
    errinject_reset();
    TEST_ASSERT_FALSE(load_text(text));
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_YAML_PARSE, errinject_last_reject());
    TEST_ASSERT_EQUAL_UINT32(0U, errinject_scenario_count());
    TEST_ASSERT_FALSE(errinject_arm(TOKEN)); // nothing loaded: cannot arm 
}

void test_llr_1553_err_007_every_kind_of_malformed_scenario_file_is_rejected_whole(void)
{
    const char *base_tail = " expected_detection: NONE\n";
    expect_parse_reject("- id: A\n target: RT1\n fault: BOGUS\n" "expected_detection: NONE\n"); // unknown fault 
    expect_parse_reject("- id: A\n target: RT9\n fault: BUS_A_FAIL\n expected_detection: NONE\n"); // unknown target 
    expect_parse_reject("- id: A\n target: RT1\n bus: C\n fault: BUS_A_FAIL\n expected_detection: NONE\n");
    expect_parse_reject("- id: A\n target: RT1\n fault: STATUS_BIT_INJECT\n bit: XYZ\n expected_detection: NONE\n");
    expect_parse_reject("- id: A\n target: RT1\n fault: BUS_A_FAIL\n word_index: abc\n expected_detection: NONE\n");
    expect_parse_reject("- id: A\n target: RT1\n fault: BUS_A_FAIL\n count: -1\n expected_detection: NONE\n");
    expect_parse_reject("- id: A\n target: RT1\n fault: BUS_A_FAIL\n expected_detection: SOMETHING\n");
    expect_parse_reject("- id: A\n target: RT1\n fault: BUS_A_FAIL\n expected_detection: NONE\n expected_recovery: MAYBE\n");
    expect_parse_reject("- id: A\n target: RT1\n fault: BUS_A_FAIL\n colour: red\n expected_detection: NONE\n"); // unknown key 
    expect_parse_reject("- target: RT1\n fault: BUS_A_FAIL\n expected_detection: NONE\n"); // no id 
    expect_parse_reject("- id: A\n fault: BUS_A_FAIL\n expected_detection: NONE\n"); // no target 
    expect_parse_reject("- id: A\n target: RT1\n expected_detection: NONE\n"); // no fault 
    expect_parse_reject("- id: A\n target: RT1\n fault: BUS_A_FAIL\n"); // no expectation 
    expect_parse_reject("- id: ABCDEFGHIJKLMNOP\n target: RT1\n fault: BUS_A_FAIL\n expected_detection: NONE\n"); // id too long 
    expect_parse_reject("- id:\n target: RT1\n fault: BUS_A_FAIL\n expected_detection: NONE\n"); // empty value 
    expect_parse_reject("- id: A\n target: RT1\n justwords\n fault: BUS_A_FAIL\n expected_detection: NONE\n"); // no colon 
    expect_parse_reject("target: RT1\n- id: A\n target: RT1\n fault: BUS_A_FAIL\n expected_detection: NONE\n"); // field before an item 
    expect_parse_reject("# nothing but a comment\n"); // no scenarios
    expect_parse_reject(""); // empty file 
    expect_parse_reject("- id: A\n target: RT1\n- id: B\n target: RT1\n fault: BUS_A_FAIL\n" "expected_detection: NONE\n"); // first incomplete 
    (void)base_tail;
}

void test_llr_1553_err_007_a_file_over_the_size_limit_is_rejected(void)
{
    static char big[20000];
    unsigned i;
    (void)strcpy(big, SC("BUS_A_FAIL", ""));
    for (i = (unsigned)strlen(big); i < 17000U; i++) { big[i] = '#'; }
    big[17000] = '\0';
    TEST_ASSERT_FALSE(load_text(big));
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_YAML_TOO_LARGE, errinject_last_reject());
}

void test_llr_1553_err_007_tampering_missing_files_and_wrong_signatures_keep_the_injector_inert(void)
{
    TEST_ASSERT_TRUE(load_text(SC("BUS_A_FAIL", ""))); // signed 
    write_bytes(YAML_PATH, SC("BUS_B_FAIL", ""), strlen(SC("BUS_B_FAIL", ""))); // edited after signing 
    TEST_ASSERT_FALSE(errinject_load_scenarios(YAML_PATH, SIG_PATH));
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_SIG_MISMATCH, errinject_last_reject());
    TEST_ASSERT_EQUAL_UINT32(0U, errinject_scenario_count());
    TEST_ASSERT_FALSE(errinject_arm(TOKEN));

    (void)remove(SIG_PATH);
    TEST_ASSERT_FALSE(errinject_load_scenarios(YAML_PATH, SIG_PATH));
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_SIG_MISSING, errinject_last_reject());

    (void)remove(YAML_PATH);
    TEST_ASSERT_FALSE(errinject_load_scenarios(YAML_PATH, SIG_PATH));
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_YAML_MISSING, errinject_last_reject());
    TEST_ASSERT_EQUAL_UINT32(3U, errinject_reject_count() - 1U); // each refusal was logged (+1: the arm) 
}

void test_llr_1553_err_007_a_failed_reload_discards_what_was_loaded_and_disarms(void)
{
    TEST_ASSERT_TRUE(load_text(SC("BUS_A_FAIL", "")));
    TEST_ASSERT_TRUE(errinject_arm(TOKEN));
    TEST_ASSERT_TRUE(errinject_is_armed());
    TEST_ASSERT_FALSE(errinject_load_scenarios("no_such_file.yaml", SIG_PATH));
    TEST_ASSERT_FALSE(errinject_is_armed());
    TEST_ASSERT_EQUAL_UINT32(0U, errinject_scenario_count());
    TEST_ASSERT_FALSE(errinject_begin_scenario(0U, &bus));
}

// LLR-1553-ERR-006: arming 

void test_llr_1553_err_006_the_injector_is_inert_at_start_and_after_a_reset(void)
{
    TEST_ASSERT_FALSE(errinject_is_armed());
    TEST_ASSERT_EQUAL_UINT32(0U, errinject_scenario_count());
    TEST_ASSERT_EQUAL_UINT32(0U, errinject_reject_count());
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_NONE, errinject_last_reject());
    TEST_ASSERT_TRUE(load_text(SC("BUS_A_FAIL", "")));
    TEST_ASSERT_TRUE(errinject_arm(TOKEN));
    errinject_reset();
    TEST_ASSERT_FALSE(errinject_is_armed());
    TEST_ASSERT_EQUAL_UINT32(0U, errinject_scenario_count());
}

void test_llr_1553_err_006_arming_needs_the_token_and_a_signed_load_and_each_refusal_is_logged(void)
{
    TEST_ASSERT_FALSE(errinject_arm(TOKEN)); // nothing loaded 
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_NOT_LOADED, errinject_last_reject());
    TEST_ASSERT_TRUE(load_text(SC("BUS_A_FAIL", "")));
    TEST_ASSERT_FALSE(errinject_arm(TOKEN + 1U)); // wrong token 
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_BAD_TOKEN, errinject_last_reject());
    TEST_ASSERT_FALSE(errinject_arm(0U));
    TEST_ASSERT_FALSE(errinject_arm(0xFFFFFFFFU));
    TEST_ASSERT_FALSE(errinject_is_armed());
    TEST_ASSERT_EQUAL_UINT32(4U, errinject_reject_count());
    TEST_ASSERT_TRUE(errinject_arm(TOKEN));
    TEST_ASSERT_TRUE(errinject_is_armed());
    TEST_ASSERT_EQUAL_UINT32(4U, errinject_reject_count()); // success logs nothing 
}

void test_llr_1553_err_006_a_scenario_cannot_start_unless_armed_and_one_runs_at_a_time(void)
{
    TEST_ASSERT_TRUE(load_text(SC("BUS_A_FAIL", "")));
    TEST_ASSERT_FALSE(errinject_begin_scenario(0U, &bus)); // loaded but not armed 
    TEST_ASSERT_EQUAL(ERRINJECT_REJECT_NOT_ARMED, errinject_last_reject());
    TEST_ASSERT_FALSE(bus_model_is_channel_failed(&bus, MIL1553_CHANNEL_A)); // and nothing happened 
    TEST_ASSERT_TRUE(errinject_arm(TOKEN));
    TEST_ASSERT_FALSE(errinject_begin_scenario(1U, &bus)); // no such scenario 
    TEST_ASSERT_TRUE(errinject_begin_scenario(0U, &bus));
    TEST_ASSERT_FALSE(errinject_begin_scenario(0U, &bus)); // one at a time 
    TEST_ASSERT_TRUE(bus_model_is_channel_failed(&bus, MIL1553_CHANNEL_A));
}

void test_llr_1553_err_006_disarming_stops_the_scenario_at_once(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r; uint16_t f;
    arm_and_begin(SC("PARITY_BIT_FLIP", ""));
    errinject_disarm();
    TEST_ASSERT_FALSE(errinject_is_armed());
    ts_data(s, 0x77U);
    (void)bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_RT1);
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BM, &r));
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, ts_decode(r.symbols, &f, (mil1553_sync_t *)0)); // passed through untouched 
    TEST_ASSERT_FALSE(errinject_end_scenario(&bus, &(errinject_observation_t){ 0 })); // nothing active to end 
}

void test_llr_1553_err_006_words_pass_through_unchanged_when_loaded_but_unarmed_or_with_no_scenario_running(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r; uint16_t f;
    ts_data(s, 0x1234U);
    TEST_ASSERT_TRUE(load_text(SC("PARITY_BIT_FLIP", "")));
    (void)bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_RT1); // loaded, not armed 
    TEST_ASSERT_TRUE(errinject_arm(TOKEN));
    (void)bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_RT1); // armed, nothing begun 
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BM, &r));
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, ts_decode(r.symbols, &f, (mil1553_sync_t *)0));
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BM, &r));
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, ts_decode(r.symbols, &f, (mil1553_sync_t *)0));
    TEST_ASSERT_EQUAL_UINT16(0x1234U, f);
}

// LLR-1553-ERR-008: fault primitives  

void test_llr_1553_err_008_parity_flip_hits_exactly_the_configured_word_window_of_the_target(void)
{
    sw_t w[16]; unsigned n, i; const mil1553_word_class_t want[6] = {
        MIL1553_WORD_VALID, MIL1553_WORD_VALID, MIL1553_WORD_PARITY_ERROR, MIL1553_WORD_PARITY_ERROR,
        MIL1553_WORD_VALID, MIL1553_WORD_VALID };
    arm_and_begin(SC("PARITY_BIT_FLIP", " word_index: 2\n count: 2\n"));
    for (i = 0U; i < 6U; i++)
    {
        send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, (uint16_t)(0x10U + i));
        send_data(MIL1553_TERMINAL_BC, MIL1553_CHANNEL_A, (uint16_t)(0x90U + i)); // others are never touched 
    }
    n = read_all(MIL1553_CHANNEL_A, w, 16U);
    TEST_ASSERT_EQUAL_UINT(12U, n);
    for (i = 0U; i < 6U; i++)
    {
        TEST_ASSERT_EQUAL(MIL1553_TERMINAL_RT1, w[i * 2U].sender);
        TEST_ASSERT_EQUAL(want[i], w[i * 2U].cls);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(0x10U + i), w[i * 2U].field); // data bits unchanged: only parity 
        TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, w[(i * 2U) + 1U].cls);
    }
}

void test_llr_1553_err_008_only_the_selected_bus_is_touched_and_counted(void)
{
    sw_t a[4], b[4];
    arm_and_begin(SC("PARITY_BIT_FLIP", " bus: A\n"));
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_B, 1U); // bus B: not selected, not counted 
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 2U); // first word on A: word_index 0 
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 3U);
    TEST_ASSERT_EQUAL_UINT(1U, read_all(MIL1553_CHANNEL_B, b, 4U));
    TEST_ASSERT_EQUAL_UINT(2U, read_all(MIL1553_CHANNEL_A, a, 4U));
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, b[0].cls);
    TEST_ASSERT_EQUAL(MIL1553_WORD_PARITY_ERROR, a[0].cls);
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, a[1].cls);
}

void test_llr_1553_err_008_corrupt_sync_makes_an_invalid_sync_in_the_window_only(void)
{
    sw_t w[4];
    arm_and_begin(SC("CORRUPT_SYNC", " word_index: 1\n"));
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 1U);
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 2U);
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 3U);
    TEST_ASSERT_EQUAL_UINT(3U, read_all(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, w[0].cls);
    TEST_ASSERT_EQUAL(MIL1553_WORD_SYNC_ERROR, w[1].cls);
    TEST_ASSERT_EQUAL(MIL1553_SYNC_INVALID, w[1].sync);
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, w[2].cls);
}

void test_llr_1553_err_008_suppress_response_drops_the_targets_words_for_the_duration_only(void)
{
    sw_t w[8]; unsigned i, n; mil1553_symbols_t s; bool all_reported_sent = true;
    arm_and_begin(SC("SUPPRESS_RESPONSE", " duration_ms: 1\n")); // 1000 us from t = 0 
    send_data(MIL1553_TERMINAL_BC, MIL1553_CHANNEL_A, 0xBCU); // t = 0: not the target 
    ts_data(s, 0x55U);
    for (i = 0U; i < 49U; i++) // t = 20 .. 980: inside the window 
    {
        if (!bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_RT1)) { all_reported_sent = false; }
        bus_model_advance_us(&bus, 20U);
    }
    TEST_ASSERT_EQUAL_UINT64(1000U, bus_model_now_us(&bus));
    TEST_ASSERT_TRUE(all_reported_sent); // the sender is told it went out 
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 0x61U); //  t = 1000: window over 
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 0x62U);
    n = read_all(MIL1553_CHANNEL_A, w, 8U);
    TEST_ASSERT_EQUAL_UINT(3U, n); // BC's word + the last two 
    TEST_ASSERT_EQUAL(MIL1553_TERMINAL_BC, w[0].sender);
    TEST_ASSERT_EQUAL_UINT64(1000U, w[1].ts);
}

void test_llr_1553_err_008_delay_response_delays_only_the_first_status_words(void)
{
    sw_t w[8]; mil1553_time_us_t t0;
    arm_and_begin(SC("DELAY_RESPONSE", " delay_us: 20\n"));
    send_cmd(MIL1553_TERMINAL_BC, MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 1U, 2U); // t = 0: a command is not delayed 
    t0 = bus_model_now_us(&bus); // 20 
    send_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U, false); // the status: delayed 20 us 
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 1U); // its data: not delayed further 
    send_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U, false); // count = 1 used up 
    TEST_ASSERT_EQUAL_UINT(4U, read_all(MIL1553_CHANNEL_A, w, 8U));
    TEST_ASSERT_EQUAL_UINT64(0U, w[0].ts);
    TEST_ASSERT_EQUAL_UINT64(t0 + 20U, w[1].ts);
    TEST_ASSERT_EQUAL_UINT64(t0 + 40U, w[2].ts);  // 20 delay + 20 for the status word 
    TEST_ASSERT_EQUAL_UINT64(t0 + 60U, w[3].ts); // back to normal spacing 
}

void test_llr_1553_err_008_wrong_word_count_drops_the_indexed_data_word_of_each_response(void)
{
    sw_t w[16]; unsigned n, r, k;
    arm_and_begin(SC("WRONG_WORD_COUNT", " word_index: 1\n count: 2\n"));
    for (r = 0U; r < 3U; r++) // three responses of status + 3 data 
    {
        send_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U, false);
        for (k = 0U; k < 3U; k++) { send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, (uint16_t)(0x100U + (r * 16U) + k)); }
    }
    n = read_all(MIL1553_CHANNEL_A, w, 16U);
    TEST_ASSERT_EQUAL_UINT(3U + 3U + 4U, n); // 2 + 2 + 4 data-bearing words 
    TEST_ASSERT_EQUAL_UINT16(0x100U, w[1].field);   TEST_ASSERT_EQUAL_UINT16(0x102U, w[2].field);  // response 1 lost word 1 
    TEST_ASSERT_EQUAL_UINT16(0x110U, w[4].field);   TEST_ASSERT_EQUAL_UINT16(0x112U, w[5].field); // response 2 too 
    TEST_ASSERT_EQUAL_UINT16(0x120U, w[7].field);   TEST_ASSERT_EQUAL_UINT16(0x121U, w[8].field); // response 3 intact 
    TEST_ASSERT_EQUAL_UINT16(0x122U, w[9].field);
}

void test_llr_1553_err_008_status_bit_inject_sets_exactly_the_chosen_bit_and_keeps_the_rest(void)
{
    const char *bits[4] = { "BUSY", "ME", "SF", "TF" };
    unsigned i;
    for (i = 0U; i < 4U; i++)
    {
        sw_t w[4]; mil1553_status_fields_t st; char text[160];
        setUp();
        (void)sprintf(text, "- id: T1\n target: RT1\n fault: STATUS_BIT_INJECT\n bit: %s\n expected_detection: NONE\n", bits[i]);
        arm_and_begin(text);
        send_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U, true); // SF already set by the RT 
        send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 0x77U);
        send_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U, false); // second status: count was 1 
        TEST_ASSERT_EQUAL_UINT(3U, read_all(MIL1553_CHANNEL_A, w, 4U));

        TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, w[0].cls); // parity recomputed: still valid 
        mil1553_word_unpack_status(w[0].field, &st);
        TEST_ASSERT_EQUAL_UINT8(5U, st.rt_address);
        TEST_ASSERT_EQUAL(i == 0U, st.busy);
        TEST_ASSERT_EQUAL(i == 1U, st.message_error);
        TEST_ASSERT_TRUE(st.subsystem_flag); // the RT's own SF survives 
        TEST_ASSERT_EQUAL(i == 3U, st.terminal_flag);
        TEST_ASSERT_FALSE(st.reserved_nonzero);
        TEST_ASSERT_EQUAL_UINT16(0x77U, w[1].field); // data untouched 
        mil1553_word_unpack_status(w[2].field, &st);
        TEST_ASSERT_FALSE(st.busy || st.message_error || st.terminal_flag); // not injected again 
    }
}

void test_llr_1553_err_008_illegal_command_rewrites_only_the_targets_first_data_command(void)
{
    sw_t w[16]; unsigned n; mil1553_command_fields_t c;
    arm_and_begin(SC("ILLEGAL_COMMAND", ""));
    send_cmd(MIL1553_TERMINAL_BC, MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 0U, 2U); // mode command to RT1: left alone 
    send_cmd(MIL1553_TERMINAL_BC, MIL1553_CHANNEL_A, 6U, MIL1553_TR_RECEIVE, 1U, 3U); // a different RT: left alone 
    send_cmd(MIL1553_TERMINAL_BC, MIL1553_CHANNEL_A, 5U, MIL1553_TR_RECEIVE, 1U, 3U); // the one: rewritten 
    send_data(MIL1553_TERMINAL_BC, MIL1553_CHANNEL_A, 0x31U); // its data words: left alone 
    send_cmd(MIL1553_TERMINAL_BC, MIL1553_CHANNEL_A, 5U, MIL1553_TR_RECEIVE, 1U, 3U); // count used up: left alone 
    send_cmd(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U, MIL1553_TR_RECEIVE, 1U, 3U); // sent BY the RT: left alone 
    n = read_all(MIL1553_CHANNEL_A, w, 16U);
    TEST_ASSERT_EQUAL_UINT(6U, n);

    mil1553_word_unpack_command(w[0].field, &c);  TEST_ASSERT_EQUAL_UINT8(0U, c.subaddress);
    mil1553_word_unpack_command(w[1].field, &c);  TEST_ASSERT_EQUAL_UINT8(1U, c.subaddress);
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, w[2].cls); // parity recomputed 
    mil1553_word_unpack_command(w[2].field, &c);
    TEST_ASSERT_EQUAL_UINT8(ERRINJECT_ILLEGAL_SUBADDRESS, c.subaddress);
    TEST_ASSERT_EQUAL_UINT8(5U, c.rt_address); // everything else preserved 
    TEST_ASSERT_EQUAL_UINT8(MIL1553_TR_RECEIVE, c.tr_bit);
    TEST_ASSERT_EQUAL_UINT8(3U, c.word_count_field);
    TEST_ASSERT_EQUAL_UINT16(0x31U, w[3].field);
    mil1553_word_unpack_command(w[4].field, &c);  TEST_ASSERT_EQUAL_UINT8(1U, c.subaddress);
    mil1553_word_unpack_command(w[5].field, &c);  TEST_ASSERT_EQUAL_UINT8(1U, c.subaddress);
}

void test_llr_1553_err_008_bus_failures_apply_at_scenario_start_and_are_undone_at_its_end(void)
{
    mil1553_symbols_t s; errinject_observation_t obs;
    ts_data(s, 1U);

    arm_and_begin(SC("BUS_A_FAIL", ""));
    TEST_ASSERT_TRUE(bus_model_is_channel_failed(&bus, MIL1553_CHANNEL_A));
    TEST_ASSERT_FALSE(bus_model_is_channel_failed(&bus, MIL1553_CHANNEL_B));
    TEST_ASSERT_FALSE(bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_BC));
    TEST_ASSERT_TRUE(bus_model_transmit(&bus, MIL1553_CHANNEL_B, s, MIL1553_TERMINAL_BC));
    errinject_observation_clear(&obs);
    (void)errinject_end_scenario(&bus, &obs);
    TEST_ASSERT_FALSE(bus_model_is_channel_failed(&bus, MIL1553_CHANNEL_A));

    setUp();
    arm_and_begin(SC("BUS_B_FAIL", ""));
    TEST_ASSERT_TRUE(bus_model_is_channel_failed(&bus, MIL1553_CHANNEL_B));
    TEST_ASSERT_FALSE(bus_model_is_channel_failed(&bus, MIL1553_CHANNEL_A));
    (void)errinject_end_scenario(&bus, &obs);
    TEST_ASSERT_FALSE(bus_model_is_channel_failed(&bus, MIL1553_CHANNEL_B));
}

void test_llr_1553_err_008_other_terminals_words_are_never_modified_by_target_faults(void)
{
    sw_t w[8]; unsigned i;
    arm_and_begin(SC("PARITY_BIT_FLIP", " count: 6\n"));
    for (i = 0U; i < 3U; i++) { send_data(MIL1553_TERMINAL_BC, MIL1553_CHANNEL_A, (uint16_t)i); }
    for (i = 0U; i < 3U; i++) { send_data(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, (uint16_t)i); }
    TEST_ASSERT_EQUAL_UINT(6U, read_all(MIL1553_CHANNEL_A, w, 8U));
    for (i = 0U; i < 6U; i++) { TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, w[i].cls); }
}

// LLR-1553-ERR-009: observations, results, output 

static errinject_observation_t obs;

static void run_scenario_with(const char *text, const errinject_observation_t *o, bool expect_pass)
{
    arm_and_begin(text);
    TEST_ASSERT_EQUAL(expect_pass, errinject_end_scenario(&bus, o));
}

#define BUSFAIL(det, rec) "- id: R1\n target: RT1\n fault: BUS_A_FAIL\n expected_detection: " det "\n expected_recovery: " rec \
                          "\n hlr: H1\n llr: L1\n test_id: TC1\n"

void test_llr_1553_err_009_the_expected_detection_and_recovery_observed_is_a_pass_with_a_full_record(void)
{
    const errinject_result_t *r;
    errinject_observation_clear(&obs);
    obs.error_type_count[MIL1553_ERR_NO_RESPONSE] = 2U;
    obs.failover_occurred = true;
    run_scenario_with(BUSFAIL("NO_RESPONSE", "BUS_FAILOVER"), &obs, true);
    TEST_ASSERT_EQUAL_UINT32(1U, errinject_result_count());
    r = errinject_get_result(0U);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("R1", r->scenario_id);
    TEST_ASSERT_EQUAL(MIL1553_TERMINAL_RT1, r->terminal);
    TEST_ASSERT_EQUAL_STRING("ANY", r->bus);
    TEST_ASSERT_EQUAL_STRING("BUS_A_FAIL", r->fault);
    TEST_ASSERT_EQUAL_STRING("detect=NO_RESPONSE;recover=BUS_FAILOVER", r->expected);
    TEST_ASSERT_EQUAL_STRING("NO_RESPONSE;recover=BUS_FAILOVER", r->observed);
    TEST_ASSERT_TRUE(r->pass);
    TEST_ASSERT_EQUAL_STRING("H1", r->hlr);
    TEST_ASSERT_EQUAL_STRING("L1", r->llr);
    TEST_ASSERT_EQUAL_STRING("TC1", r->test_id);
}

void test_llr_1553_err_009_an_expected_detection_that_was_not_observed_is_a_failure_never_a_default_pass(void)
{
    errinject_observation_clear(&obs);
    run_scenario_with(BUSFAIL("NO_RESPONSE", "NONE"), &obs, false);
    TEST_ASSERT_EQUAL_STRING("NONE;recover=NONE", errinject_get_result(0U)->observed);
    TEST_ASSERT_FALSE(errinject_get_result(0U)->pass);

    errinject_reset();
    errinject_observation_clear(&obs);
    obs.error_type_count[MIL1553_ERR_PARITY] = 5U; // a different detection is not enough 
    run_scenario_with(BUSFAIL("NO_RESPONSE", "NONE"), &obs, false);
}

void test_llr_1553_err_009_an_expected_recovery_that_did_not_happen_is_a_failure(void)
{
    errinject_observation_clear(&obs);
    obs.error_type_count[MIL1553_ERR_NO_RESPONSE] = 1U;
    run_scenario_with(BUSFAIL("NO_RESPONSE", "BUS_FAILOVER"), &obs, false); // detected, but no failover 

    errinject_reset();
    obs.rt_marked_failed = true; // the wrong recovery 
    run_scenario_with(BUSFAIL("NO_RESPONSE", "BUS_FAILOVER"), &obs, false);

    errinject_reset();
    run_scenario_with(BUSFAIL("NO_RESPONSE", "RT_MARKED_FAILED"), &obs, true); // the right one 
}

void test_llr_1553_err_009_expecting_none_passes_only_when_nothing_was_detected(void)
{
    errinject_observation_clear(&obs);
    run_scenario_with(BUSFAIL("NONE", "NONE"), &obs, true); // the negative control 
    errinject_reset();
    obs.error_type_count[MIL1553_ERR_STATUS_ME] = 1U;
    run_scenario_with(BUSFAIL("NONE", "NONE"), &obs, false);
    errinject_reset();
    errinject_observation_clear(&obs);
    obs.cbit_response_time_violations = 1U; // CBIT counts as a detection too */
    run_scenario_with(BUSFAIL("NONE", "NONE"), &obs, false);
}

void test_llr_1553_err_009_a_cbit_response_time_violation_satisfies_that_expectation(void)
{
    errinject_observation_clear(&obs);
    obs.cbit_response_time_violations = 2U;
    run_scenario_with(BUSFAIL("CBIT_RESPONSE_TIME", "NONE"), &obs, true);
    TEST_ASSERT_EQUAL_STRING("CBIT_RESPONSE_TIME;recover=NONE", errinject_get_result(0U)->observed);
}

void test_llr_1553_err_009_observed_detections_are_listed_in_a_fixed_order_with_both_recoveries(void)
{
    errinject_observation_clear(&obs);
    obs.error_type_count[MIL1553_ERR_NO_RESPONSE] = 1U;
    obs.error_type_count[MIL1553_ERR_SYNC] = 1U;
    obs.error_type_count[MIL1553_ERR_PARITY] = 1U;
    obs.failover_occurred = true;
    obs.rt_marked_failed = true;
    run_scenario_with(BUSFAIL("NONE", "NONE"), &obs, false);
    TEST_ASSERT_EQUAL_STRING("PARITY,SYNC,NO_RESPONSE;recover=BUS_FAILOVER,RT_MARKED_FAILED",
                             errinject_get_result(0U)->observed);
}

void test_llr_1553_err_009_a_fault_that_never_fired_is_a_failure_even_if_the_detection_matches(void)
{
    errinject_observation_clear(&obs);
    obs.error_type_count[MIL1553_ERR_PARITY] = 1U;
    arm_and_begin("- id: R2\n target: RT1\n fault: PARITY_BIT_FLIP\n expected_detection: PARITY\n");
    bus_model_advance_us(&bus, 300U);
    TEST_ASSERT_FALSE(errinject_end_scenario(&bus, &obs)); // RT1 never sent a word 
    TEST_ASSERT_NOT_NULL(strstr(errinject_get_result(0U)->observed, "FAULT_NOT_APPLIED"));
    TEST_ASSERT_EQUAL_UINT64(0U, errinject_get_result(0U)->timestamp_us); // no injection: the scenario start 
}

void test_llr_1553_err_009_the_timestamp_is_the_first_injection(void)
{
    errinject_observation_clear(&obs);
    obs.error_type_count[MIL1553_ERR_PARITY] = 1U;
    arm_and_begin("- id: R3\n target: RT1\n fault: PARITY_BIT_FLIP\n count: 3\n expected_detection: PARITY\n");
    bus_model_advance_us(&bus, 100U);
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 1U); // first injection at t = 100 
    send_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 2U);
    TEST_ASSERT_TRUE(errinject_end_scenario(&bus, &obs));
    TEST_ASSERT_EQUAL_UINT64(100U, errinject_get_result(0U)->timestamp_us);
}

void test_llr_1553_err_009_ending_without_a_scenario_is_refused_and_results_keep_run_order(void)
{
    errinject_observation_clear(&obs);
    TEST_ASSERT_FALSE(errinject_end_scenario(&bus, &obs));
    TEST_ASSERT_TRUE(load_text("- id: A1\n target: RT1\n fault: BUS_A_FAIL\n expected_detection: NONE\n"
                               "- id: B2\n target: RT2\n fault: BUS_B_FAIL\n expected_detection: NONE\n"));
    TEST_ASSERT_TRUE(errinject_arm(TOKEN));
    TEST_ASSERT_TRUE(errinject_begin_scenario(1U, &bus));  TEST_ASSERT_TRUE(errinject_end_scenario(&bus, &obs));
    TEST_ASSERT_TRUE(errinject_begin_scenario(0U, &bus));  TEST_ASSERT_TRUE(errinject_end_scenario(&bus, &obs));
    TEST_ASSERT_EQUAL_UINT32(2U, errinject_result_count());
    TEST_ASSERT_EQUAL_STRING("B2", errinject_get_result(0U)->scenario_id);
    TEST_ASSERT_EQUAL_STRING("A1", errinject_get_result(1U)->scenario_id);
    TEST_ASSERT_NULL(errinject_get_result(2U));
}

void test_llr_1553_err_009_the_result_store_is_bounded(void)
{
    unsigned i;
    errinject_observation_clear(&obs);
    TEST_ASSERT_TRUE(load_text("- id: A1\n target: RT1\n fault: BUS_A_FAIL\n expected_detection: NONE\n"));
    TEST_ASSERT_TRUE(errinject_arm(TOKEN));
    for (i = 0U; i < ERRINJECT_MAX_SCENARIOS; i++)
    {
        TEST_ASSERT_TRUE(errinject_begin_scenario(0U, &bus));
        TEST_ASSERT_TRUE(errinject_end_scenario(&bus, &obs));
    }
    TEST_ASSERT_TRUE(errinject_begin_scenario(0U, &bus));
    TEST_ASSERT_FALSE(errinject_end_scenario(&bus, &obs)); // no room for a 33rd 
    TEST_ASSERT_EQUAL_UINT32(ERRINJECT_MAX_SCENARIOS, errinject_result_count());
}

void test_llr_1553_err_009_observation_tally_counts_by_type_and_ignores_unknown_types(void)
{
    mil1553_error_record_t recs[4];
    (void)memset(recs, 0, sizeof(recs));
    recs[0].error_type = MIL1553_ERR_PARITY;
    recs[1].error_type = MIL1553_ERR_PARITY;
    recs[2].error_type = MIL1553_ERR_TRANSPORT;
    recs[3].error_type = (mil1553_error_type_t)99;
    errinject_observation_clear(&obs);
    obs.error_type_count[MIL1553_ERR_SYNC] = 7U;
    errinject_observation_clear(&obs);
    TEST_ASSERT_EQUAL_UINT32(0U, obs.error_type_count[MIL1553_ERR_SYNC]);
    errinject_observation_add_log(&obs, recs, 4U);
    TEST_ASSERT_EQUAL_UINT32(2U, obs.error_type_count[MIL1553_ERR_PARITY]);
    TEST_ASSERT_EQUAL_UINT32(1U, obs.error_type_count[MIL1553_ERR_TRANSPORT]);
    errinject_observation_add_log(&obs, recs, 0U);
    TEST_ASSERT_EQUAL_UINT32(2U, obs.error_type_count[MIL1553_ERR_PARITY]);
}

static void slurp(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "rb"); size_t n;
    TEST_ASSERT_NOT_NULL(f);
    n = fread(out, 1U, cap - 1U, f);
    out[n] = '\0';
    (void)fclose(f);
}

void test_llr_1553_err_009_the_json_output_has_every_field_of_every_record_in_exact_form(void)
{
    char text[2048];
    const char *expected =
        "[\n"
        "  {\"scenario_id\": \"R1\", \"timestamp_us\": 0, \"terminal\": \"RT1\", \"bus\": \"ANY\", "
        "\"fault\": \"BUS_A_FAIL\", \"expected\": \"detect=NO_RESPONSE;recover=BUS_FAILOVER\", "
        "\"observed\": \"NO_RESPONSE;recover=BUS_FAILOVER\", \"pass_fail\": \"PASS\", "
        "\"HLR\": \"H1\", \"LLR\": \"L1\", \"test_id\": \"TC1\"},\n"
        "  {\"scenario_id\": \"R1\", \"timestamp_us\": 0, \"terminal\": \"RT1\", \"bus\": \"ANY\", "
        "\"fault\": \"BUS_A_FAIL\", \"expected\": \"detect=NO_RESPONSE;recover=BUS_FAILOVER\", "
        "\"observed\": \"NONE;recover=NONE\", \"pass_fail\": \"FAIL\", "
        "\"HLR\": \"H1\", \"LLR\": \"L1\", \"test_id\": \"TC1\"}\n"
        "]\n";

    TEST_ASSERT_TRUE(load_text(BUSFAIL("NO_RESPONSE", "BUS_FAILOVER")));
    TEST_ASSERT_TRUE(errinject_arm(TOKEN));
    errinject_observation_clear(&obs);
    obs.error_type_count[MIL1553_ERR_NO_RESPONSE] = 1U; obs.failover_occurred = true;
    TEST_ASSERT_TRUE(errinject_begin_scenario(0U, &bus));
    TEST_ASSERT_TRUE(errinject_end_scenario(&bus, &obs));
    errinject_observation_clear(&obs);
    TEST_ASSERT_TRUE(errinject_begin_scenario(0U, &bus));
    TEST_ASSERT_FALSE(errinject_end_scenario(&bus, &obs));

    TEST_ASSERT_TRUE(errinject_write_results_json(JSON_PATH));
    slurp(JSON_PATH, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING(expected, text);
}

void test_llr_1553_err_009_with_no_results_the_output_is_an_empty_json_array_and_an_unwritable_path_fails(void)
{
    char text[64];
    TEST_ASSERT_TRUE(errinject_write_results_json(JSON_PATH));
    slurp(JSON_PATH, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("[\n]\n", text);
    TEST_ASSERT_FALSE(errinject_write_results_json("/no_such_directory_xyz/out.json"));
}