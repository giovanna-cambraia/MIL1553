/*
    verifies: LLR-1553-RT-001 .. LLR-1553-RT-013
    module: rt_service (with bus_model, mil1553_word, manchester)

    fixture: the RT under test has address 5 and terminal id RT1; the test acts as the BC (sender BC) and as other RTs (sender RT2), and reads the RT's replies
    as the BC's listener
    subaddress 1: receive and transmit, 1..32 words
    subaddress 2: receive only, 1..4
    subaddress 3: transmit only, 1..8 words
    all others disabled
*/

#include "test_support.h"
#include "rt_service.h"

#define ME_ADDR (5U)

static mil1553_bus_model_context_t bus;
static mil1553_rt_context_t rt;
static mil1553_rt_subaddr_config_t cfg[RT_SUBADDRESS_COUNT];

typedef struct {
    uint16_t field; mil1553_sync_t sync; mil1553_word_class_t cls; mil1553_time_us_t ts;
} sw_t;

static void build_cfg(void)
{
    (void)memset(cfg, 0, sizeof(cfg));
    cfg[1].enabled = true; cfg[1].allow_receive = true; cfg[1].allow_transmit = true;
    cfg[1].min_word_count = 1U; cfg[1].max_word_count = 32U;
    cfg[2].enabled = true; cfg[2].allow_receive = true; cfg[2].allow_transmit = false;
    cfg[2].min_word_count = 1U; cfg[2].max_word_count = 4U;
    cfg[3].enabled = true; cfg[3].allow_receive = false; cfg[3].allow_transmit = true;
    cfg[3].min_word_count = 1U; cfg[3].max_word_count = 8U;
}

static bool init_rt(uint8_t delay)
{
    bool ok;
    bus_model_init(&bus);
    ok = rt_init(&rt, ME_ADDR, MIL1553_TERMINAL_RT1, cfg, delay, &bus);
    if (ok) { rt_pbit_complete(&rt, true); (void)rt_enable(&rt); }
    return ok;
}

void setUp(void) { build_cfg(); (void)init_rt(8U); }
void tearDown(void) {}

// the test plays the BC / other RTs
static void send_cmd_as(mil1553_terminal_id_t who, mil1553_channel_t ch, uint8_t addr, mil1553_tr_t tr, uint8_t sub, uint8_t wc)
{
    mil1553_symbols_t s;
    ts_cmd(s, addr, tr, sub, wc);
    (void)ts_tx(&bus, ch, who, s);
}

static void send_cmd(mil1553_channel_t ch, uint8_t addr, mil1553_tr_t tr, uint8_t sub, uint8_t wc) {
    send_cmd_as(MIL1553_TERMINAL_BC, ch, addr, tr, sub, wc);
}

static void send_data_as(mil1553_terminal_id_t who, mil1553_channel_t ch, uint16_t v)
{
    mil1553_symbols_t s;
    ts_data(s, v);
    (void)ts_tx(&bus, ch, who, s);
}

static void send_data(mil1553_channel_t ch, uint16_t v) { send_data_as(MIL1553_TERMINAL_BC, ch, v); }

static void send_status_as(mil1553_terminal_id_t who, mil1553_channel_t ch, uint8_t addr, bool me)
{
    mil1553_symbols_t s;
    ts_status(s, addr, me, false, false, false, false);
    (void)ts_tx(&bus, ch, who, s);
}

static void pump(unsigned n)
{
    unsigned i;
    for (i = 0U; i < n; i++) { (void)rt_run(&rt); bus_model_advance_us(&bus, 1U); }
}

static bool pump_until(mil1553_rt_state_t st, unsigned max)
{
    unsigned i;
    for (i = 0U; i < max; i++) { if (rt_get_state(&rt) == st) { return true; } pump(1U); }
    return rt_get_state(&rt) == st;
}

// what the RT put on a channel since the last call, read as the BC
static unsigned replies(mil1553_channel_t ch, sw_t *out, unsigned max)
{
    unsigned n = 0U; mil1553_bus_record_t r;
    while ((n < max) && ts_rx(&bus, ch, MIL1553_TERMINAL_BC, &r))
    {
        if (r.sender != MIL1553_TERMINAL_RT1) { continue; } // words sent as "other RTs" are not replies
        out[n].cls = ts_decode(r.symbols, &out[n].field, &out[n].sync);
        out[n].ts = r.timestamp_us;
        n++;
    }
    return n;
}

static mil1553_status_fields_t status_of(const sw_t *w)
{
    mil1553_status_fields_t s;
    mil1553_word_unpack_status(w->field, &s);
    return s;
}

static unsigned count_errors(mil1553_error_type_t t)
{
    unsigned i, n = 0U;
    for (i = 0U; i < rt.error_log_count; i++) { if (rt.error_log[i].error_type == t) { n++; } }
    return n;
}

// BC-to-RT receive of n words, values base..base+n-1
static void bc_receive(uint8_t sub, uint8_t n, uint16_t base)
{
    uint8_t wc, k;
    (void)mil1553_word_encode_word_count(n, &wc);
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, sub, wc);
    for (k = 0U; k < n; k++) { send_data(MIL1553_CHANNEL_A, (uint16_t)(base + k)); }
}

static mil1553_status_fields_t one_status_reply(void)
{
    sw_t w[4];
    TEST_ASSERT_EQUAL_UINT(1U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, w[0].cls);
    TEST_ASSERT_EQUAL(MIL1553_SYNC_COMMAND_STATUS, w[0].sync);
    return status_of(&w[0]);
}

static void mode_cmd(mil1553_channel_t ch, uint8_t code, mil1553_tr_t tr)
{
    send_cmd(ch, ME_ADDR, tr, 0U, code);
}

// LLR-1553-RT-001
void test_llr_1553_rt_001_commands_for_other_addresses_are_ignored_silently(void)
{
    uint8_t a; sw_t w[8];
    for (a = 0U; a <= 30U; a++)
    {
        if (a == ME_ADDR) { continue; }
        send_cmd(MIL1553_CHANNEL_A, a, MIL1553_TR_RECEIVE, 1U, 1U);
        send_data(MIL1553_CHANNEL_A, 0x1111U);
        pump(40U);
    }
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 8U));
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[1].word_count);
    TEST_ASSERT_EQUAL_UINT(0U, rt.error_log_count);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt));
}

void test_llr_1553_rt_001_command_for_own_address_gets_exactly_one_status(void)
{
    mil1553_status_fields_t st;
    bc_receive(1U, 1U, 0x10U);
    pump(60U);
    st = one_status_reply();
    TEST_ASSERT_EQUAL_UINT8(ME_ADDR, st.rt_address);
    TEST_ASSERT_FALSE(st.message_error);
}

void test_llr_1553_rt_001_data_sync_word_whose_bits_look_like_my_address_is_not_a_command(void)
{
    mil1553_symbols_t s; sw_t w[4];
    // a DATA word whose top five bits are this RT's address
    ts_data(s, (uint16_t)(ME_ADDR << 11));
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT2, s);
    pump(40U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT(0U, rt.error_log_count);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt));
}

void test_llr_1553_rt_001_both_channels_are_listened_to(void)
{
    sw_t w[4]; mil1553_status_fields_t st;
    bc_receive(1U, 1U, 1U);                                    // on A
    pump(40U);
    TEST_ASSERT_EQUAL_UINT(1U, replies(MIL1553_CHANNEL_A, w, 4U));
    send_cmd(MIL1553_CHANNEL_B, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 1U);
    send_data(MIL1553_CHANNEL_B, 2U);
    pump(40U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));   // the answer goes back on B
    TEST_ASSERT_EQUAL_UINT(1U, replies(MIL1553_CHANNEL_B, w, 4U));
    st = status_of(&w[0]);
    TEST_ASSERT_EQUAL_UINT8(ME_ADDR, st.rt_address);
}

// LLR-1553-RT-002
static void expect_rejected_with_me_only(void)
{
    mil1553_status_fields_t st = one_status_reply();     // exactly one word: no data
    TEST_ASSERT_TRUE(st.message_error);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt));
}

void test_llr_1553_rt_002_disabled_subaddress_is_illegal_and_not_executed(void)
{
    bc_receive(9U, 2U, 0x50U);
    pump(60U);
    expect_rejected_with_me_only();
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[9].word_count);
}

void test_llr_1553_rt_002_wrong_direction_is_illegal_both_ways(void)
{
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 2U, 1U);   // sub 2 is receive-only
    pump(60U);
    expect_rejected_with_me_only();

    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, 3U, 1U);    // sub 3 is transmit-only
    send_data(MIL1553_CHANNEL_A, 7U);
    pump(60U);
    expect_rejected_with_me_only();
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[3].word_count);
}

void test_llr_1553_rt_002_word_count_outside_the_configured_range_is_illegal(void)
{
    uint8_t wc;
    bc_receive(2U, 4U, 1U);                                              // 4 is the maximum: legal
    pump(60U);
    TEST_ASSERT_FALSE(one_status_reply().message_error);

    bc_receive(2U, 5U, 1U);                                              // 5: one too many
    pump(60U);
    expect_rejected_with_me_only();

    (void)mil1553_word_encode_word_count(32U, &wc);                      // field 0 = 32 words
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, 2U, wc);
    pump(60U);
    expect_rejected_with_me_only();
}

void test_llr_1553_rt_002_a_rejected_receive_never_touches_the_buffer(void)
{
    bc_receive(1U, 3U, 0xAAU);
    pump(80U);
    (void)one_status_reply();
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 2U, 1U);   // illegal direction ...
    pump(60U);
    expect_rejected_with_me_only();
    TEST_ASSERT_EQUAL_UINT8(3U, rt.subaddr_buffer[1].word_count);        // ... previous block intact
    TEST_ASSERT_EQUAL_UINT16(0xAAU, rt.subaddr_buffer[1].words[0]);
}

// LLR-1553-RT-003
void test_llr_1553_rt_003_complete_block_is_committed_in_order(void)
{
    uint8_t k;
    bc_receive(1U, 5U, 0x200U);
    pump(80U);
    (void)one_status_reply();
    TEST_ASSERT_EQUAL_UINT8(5U, rt.subaddr_buffer[1].word_count);
    for (k = 0U; k < 5U; k++) { TEST_ASSERT_EQUAL_UINT16((uint16_t)(0x200U + k), rt.subaddr_buffer[1].words[k]); }
}

void test_llr_1553_rt_003_thirty_two_words_are_accepted(void)
{
    bc_receive(1U, 32U, 0x300U);
    pump(120U);
    (void)one_status_reply();
    TEST_ASSERT_EQUAL_UINT8(32U, rt.subaddr_buffer[1].word_count);
    TEST_ASSERT_EQUAL_UINT16(0x31FU, rt.subaddr_buffer[1].words[31]);
}

void test_llr_1553_rt_003_nothing_is_committed_before_the_response_goes_out(void)
{
    bc_receive(1U, 3U, 0x40U);
    TEST_ASSERT_TRUE(pump_until(MIL1553_RT_STATE_RESPOND, 40U));
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[1].word_count);        // still staged
    pump(40U);
    TEST_ASSERT_EQUAL_UINT8(3U, rt.subaddr_buffer[1].word_count);        // committed with the status
}

void test_llr_1553_rt_003_a_corrupt_word_discards_the_message_and_keeps_the_old_block(void)
{
    mil1553_symbols_t bad; sw_t w[4];
    bc_receive(1U, 3U, 0x60U);                                           // block A, committed
    pump(80U);
    (void)one_status_reply();

    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 3U);   // block B ...
    send_data(MIL1553_CHANNEL_A, 0x71U);
    ts_data(bad, 0x72U);
    bad[2] = (uint8_t)(bad[2] ^ 0x80U);                                  // break a Manchester cell
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, bad);
    send_data(MIL1553_CHANNEL_A, 0x73U);
    pump(80U);

    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));       // nothing sent back
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_MANCHESTER));
    TEST_ASSERT_EQUAL_UINT8(3U, rt.subaddr_buffer[1].word_count);        // ... block A intact, word for word
    TEST_ASSERT_EQUAL_UINT16(0x60U, rt.subaddr_buffer[1].words[0]);
    TEST_ASSERT_EQUAL_UINT16(0x61U, rt.subaddr_buffer[1].words[1]);
    TEST_ASSERT_EQUAL_UINT16(0x62U, rt.subaddr_buffer[1].words[2]);
    TEST_ASSERT_TRUE(rt.message_error_latched);
}

void test_llr_1553_rt_003_fewer_words_than_commanded_is_a_count_error(void)
{
    sw_t w[4];
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 4U);
    send_data(MIL1553_CHANNEL_A, 1U);
    send_data(MIL1553_CHANNEL_A, 2U);                                    // two of four, then silence
    pump(60U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_WORD_COUNT_MISMATCH));
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[1].word_count);
    TEST_ASSERT_TRUE(rt.message_error_latched);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt)); // not stuck waiting
}

void test_llr_1553_rt_003_a_command_that_interrupts_a_short_message_is_not_lost(void)
{
    sw_t w[8]; unsigned n;
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 4U);
    send_data(MIL1553_CHANNEL_A, 1U);                                    // one of four ...
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 1U, 1U);   // ... then a new command at once
    pump(80U);
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_WORD_COUNT_MISMATCH));
    n = replies(MIL1553_CHANNEL_A, w, 8U);
    TEST_ASSERT_EQUAL_UINT(2U, n);                                       // the new command was answered: status + 1 word
    TEST_ASSERT_EQUAL(MIL1553_SYNC_COMMAND_STATUS, w[0].sync);
    TEST_ASSERT_EQUAL(MIL1553_SYNC_DATA, w[1].sync);
}

void test_llr_1553_rt_003_more_words_than_commanded_is_a_count_error(void)
{
    sw_t w[4];
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 2U);
    send_data(MIL1553_CHANNEL_A, 1U);
    send_data(MIL1553_CHANNEL_A, 2U);
    send_data(MIL1553_CHANNEL_A, 3U);                                    // one too many, contiguous
    pump(60U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_WORD_COUNT_MISMATCH));
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[1].word_count);
}

void test_llr_1553_rt_003_the_next_message_after_a_clean_one_is_not_mistaken_for_an_extra_word(void)
{
    sw_t w[4];
    bc_receive(1U, 2U, 1U);
    pump(80U);
    TEST_ASSERT_EQUAL_UINT(1U, replies(MIL1553_CHANNEL_A, w, 4U));
    bus_model_advance_us(&bus, 10U);                                     // a proper gap, then the next message
    bc_receive(1U, 2U, 9U);
    pump(80U);
    TEST_ASSERT_EQUAL_UINT(1U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT(0U, rt.error_log_count);
    TEST_ASSERT_EQUAL_UINT16(9U, rt.subaddr_buffer[1].words[0]);
}

// LLR-1553-RT-004
void test_llr_1553_rt_004_transmit_sends_status_then_exactly_n_words_from_the_buffer(void)
{
    sw_t w[40]; unsigned n, k;
    bc_receive(1U, 3U, 0x11U);
    pump(80U);
    (void)replies(MIL1553_CHANNEL_A, w, 4U);

    bus_model_advance_us(&bus, 10U);
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 1U, 3U);
    pump(60U);
    n = replies(MIL1553_CHANNEL_A, w, 40U);
    TEST_ASSERT_EQUAL_UINT(4U, n);
    TEST_ASSERT_EQUAL(MIL1553_SYNC_COMMAND_STATUS, w[0].sync);
    for (k = 0U; k < 3U; k++)
    {
        TEST_ASSERT_EQUAL(MIL1553_SYNC_DATA, w[k + 1U].sync);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(0x11U + k), w[k + 1U].field);
        TEST_ASSERT_EQUAL_UINT64(w[k].ts + MIL1553_WORD_TIME_US, w[k + 1U].ts);   // no gap between words
    }
}

void test_llr_1553_rt_004_words_beyond_what_the_buffer_holds_are_zero(void)
{
    sw_t w[40]; unsigned n;
    bc_receive(1U, 2U, 0x21U);
    pump(80U);
    (void)replies(MIL1553_CHANNEL_A, w, 4U);
    bus_model_advance_us(&bus, 10U);
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 1U, 4U);
    pump(80U);
    n = replies(MIL1553_CHANNEL_A, w, 40U);
    TEST_ASSERT_EQUAL_UINT(5U, n);
    TEST_ASSERT_EQUAL_UINT16(0x21U, w[1].field);
    TEST_ASSERT_EQUAL_UINT16(0x22U, w[2].field);
    TEST_ASSERT_EQUAL_UINT16(0U, w[3].field);
    TEST_ASSERT_EQUAL_UINT16(0U, w[4].field);
}

void test_llr_1553_rt_004_thirty_two_words_use_word_count_field_zero(void)
{
    sw_t w[40]; unsigned n;
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 1U, 0U);
    pump(60U + (32U * 20U));
    n = replies(MIL1553_CHANNEL_A, w, 40U);
    TEST_ASSERT_EQUAL_UINT(33U, n);
}

// LLR-1553-RT-005
void test_llr_1553_rt_005_status_starts_exactly_the_configured_delay_after_the_command(void)
{
    const uint8_t delays[3] = { 4U, 8U, 12U };
    unsigned i;
    for (i = 0U; i < 3U; i++)
    {
        sw_t w[4]; mil1553_time_us_t cmd_end;
        TEST_ASSERT_TRUE(init_rt(delays[i]));
        send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 3U, 1U);
        cmd_end = bus_model_now_us(&bus);
        pump(80U);
        TEST_ASSERT_TRUE(replies(MIL1553_CHANNEL_A, w, 4U) >= 1U);
        TEST_ASSERT_EQUAL_UINT64(cmd_end + delays[i], w[0].ts);
    }
}

void test_llr_1553_rt_005_delay_after_a_receive_runs_from_the_last_data_word(void)
{
    sw_t w[4]; mil1553_time_us_t last_end;
    bc_receive(1U, 3U, 1U);
    last_end = bus_model_now_us(&bus);
    pump(80U);
    TEST_ASSERT_EQUAL_UINT(1U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT64(last_end + 8U, w[0].ts);
}

void test_llr_1553_rt_005_delay_is_measured_in_bus_time_not_from_when_the_rt_ran(void)
{
    sw_t w[4]; mil1553_time_us_t cmd_end;
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 3U, 1U);
    cmd_end = bus_model_now_us(&bus);
    bus_model_advance_us(&bus, 30U);                 // the RT was not scheduled for 30 us
    pump(20U);
    TEST_ASSERT_TRUE(replies(MIL1553_CHANNEL_A, w, 4U) >= 1U);
    TEST_ASSERT_TRUE(w[0].ts >= (cmd_end + 8U));     // never earlier than the delay
    TEST_ASSERT_TRUE(w[0].ts <= (cmd_end + 30U + 6U)); // and not delayed a second time
}

void test_llr_1553_rt_005_configured_delay_outside_four_to_twelve_us_is_rejected(void)
{
    TEST_ASSERT_FALSE(init_rt(3U));
    TEST_ASSERT_FALSE(init_rt(13U));
    TEST_ASSERT_FALSE(init_rt(0U));
    TEST_ASSERT_FALSE(init_rt(255U));
    TEST_ASSERT_TRUE(init_rt(4U));
    TEST_ASSERT_TRUE(init_rt(12U));
}

// LLR-1553-RT-006
void test_llr_1553_rt_006_status_always_carries_the_rts_own_address(void)
{
    mil1553_status_fields_t st;
    bc_receive(9U, 1U, 1U);                                              // even an ME status
    pump(60U);
    st = one_status_reply();
    TEST_ASSERT_EQUAL_UINT8(ME_ADDR, st.rt_address);
}

void test_llr_1553_rt_006_me_is_set_on_rejection_and_clears_on_the_next_valid_command(void)
{
    bc_receive(9U, 1U, 1U);                                              // rejected: ME=1
    pump(60U);
    TEST_ASSERT_TRUE(one_status_reply().message_error);
    bus_model_advance_us(&bus, 10U);
    bc_receive(1U, 1U, 1U);                                              // valid: ME clears
    pump(60U);
    TEST_ASSERT_FALSE(one_status_reply().message_error);
}

void test_llr_1553_rt_006_busy_follows_the_subsystem(void)
{
    rt_set_busy(&rt, true);
    bc_receive(1U, 1U, 1U);   // a valid one-word receive: answered with a status word only
    pump(60U);
    TEST_ASSERT_TRUE(one_status_reply().busy);
    rt_set_busy(&rt, false);
    bus_model_advance_us(&bus, 10U);
    bc_receive(1U, 1U, 1U);   // a valid one-word receive: answered with a status word only
    pump(60U);
    TEST_ASSERT_FALSE(one_status_reply().busy);
}

void test_llr_1553_rt_006_subsystem_flag_and_terminal_flag_come_from_subsystem_and_bit(void)
{
    mil1553_status_fields_t st;
    rt_set_subsystem_flag(&rt, true);
    rt_report_bit_result(&rt, false, true, false);                       // non-fatal BIT result, TF
    bc_receive(1U, 1U, 1U);   // a valid one-word receive: answered with a status word only
    pump(60U);
    st = one_status_reply();
    TEST_ASSERT_TRUE(st.subsystem_flag);
    TEST_ASSERT_TRUE(st.terminal_flag);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt)); // non-fatal: still serving
}

void test_llr_1553_rt_006_bcr_is_set_by_a_broadcast_and_cleared_by_the_next_normal_command(void)
{
    mil1553_status_fields_t st;
    send_cmd(MIL1553_CHANNEL_A, 31U, MIL1553_TR_RECEIVE, 1U, 1U);
    send_data(MIL1553_CHANNEL_A, 5U);
    pump(60U);
    TEST_ASSERT_TRUE(rt.broadcast_received_latched);

    bus_model_advance_us(&bus, 10U);
    mode_cmd(MIL1553_CHANNEL_A, 2U, MIL1553_TR_TRANSMIT);                // transmit status word: reports it
    pump(60U);
    st = one_status_reply();
    TEST_ASSERT_TRUE(st.broadcast_received);

    bus_model_advance_us(&bus, 10U);
    bc_receive(1U, 1U, 1U);   // a valid one-word receive: answered with a status word only   // a normal command clears it
    pump(60U);
    TEST_ASSERT_FALSE(one_status_reply().broadcast_received);
}

void test_llr_1553_rt_006_an_error_the_rt_could_not_answer_is_reported_by_transmit_status_word(void)
{
    mil1553_symbols_t bad; sw_t w[4];
    ts_cmd(bad, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 1U);
    bad[4] = (uint8_t)(bad[4] ^ 0x03U);                                  // swap the parity cell
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, bad);
    pump(60U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));       // silent

    bus_model_advance_us(&bus, 10U);
    mode_cmd(MIL1553_CHANNEL_A, 2U, MIL1553_TR_TRANSMIT);
    pump(60U);
    TEST_ASSERT_TRUE(one_status_reply().message_error);                  // the BC finds out here

    bus_model_advance_us(&bus, 10U);
    mode_cmd(MIL1553_CHANNEL_A, 2U, MIL1553_TR_TRANSMIT);                // asking again does not clear it
    pump(60U);
    TEST_ASSERT_TRUE(one_status_reply().message_error);

    bus_model_advance_us(&bus, 10U);
    bc_receive(1U, 1U, 1U);   // a valid one-word receive: answered with a status word only   // a real command clears it
    pump(60U);
    TEST_ASSERT_FALSE(one_status_reply().message_error);
}

// LLR-1553-RT-007
static void rt_to_rt_receive_commands(uint8_t xmit_rt)
{
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 2U);        // receive command to me
    send_cmd(MIL1553_CHANNEL_A, xmit_rt, MIL1553_TR_TRANSMIT, 2U, 2U);       // transmit command to the other RT
}

void test_llr_1553_rt_007_receiving_rt_waits_for_the_other_rts_status_then_data_then_answers(void)
{
    rt_to_rt_receive_commands(9U);
    send_status_as(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 9U, false);
    send_data_as(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 0x81U);
    send_data_as(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 0x82U);
    pump(80U);
    TEST_ASSERT_FALSE(one_status_reply().message_error);
    TEST_ASSERT_EQUAL_UINT8(2U, rt.subaddr_buffer[1].word_count);
    TEST_ASSERT_EQUAL_UINT16(0x81U, rt.subaddr_buffer[1].words[0]);
    TEST_ASSERT_EQUAL_UINT16(0x82U, rt.subaddr_buffer[1].words[1]);
}

void test_llr_1553_rt_007_status_from_the_wrong_rt_discards_the_message(void)
{
    sw_t w[4];
    rt_to_rt_receive_commands(9U);
    send_status_as(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 10U, false);     // not RT 9
    send_data_as(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 1U);
    send_data_as(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 2U);
    pump(80U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_STATUS_ADDRESS_MISMATCH));
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[1].word_count);
}

void test_llr_1553_rt_007_status_with_message_error_from_the_other_rt_discards_the_message(void)
{
    sw_t w[4];
    rt_to_rt_receive_commands(9U);
    send_status_as(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 9U, true);
    pump(60U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_STATUS_ME));
}

void test_llr_1553_rt_007_a_silent_transmitting_rt_times_out_without_a_status_from_me(void)
{
    sw_t w[4];
    rt_to_rt_receive_commands(9U);
    pump(40U);                                                           // nobody answers
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_NO_RESPONSE));
    TEST_ASSERT_TRUE(rt.message_error_latched);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt));
}

void test_llr_1553_rt_007_transmitting_rt_answers_its_own_transmit_command_and_ignores_the_receive_command(void)
{
    sw_t w[8]; unsigned n;
    bc_receive(1U, 2U, 0x91U);                                           // put data in sub 1 first
    pump(80U);
    (void)replies(MIL1553_CHANNEL_A, w, 4U);
    bus_model_advance_us(&bus, 10U);

    send_cmd(MIL1553_CHANNEL_A, 9U, MIL1553_TR_RECEIVE, 1U, 2U);         // receive command for ANOTHER RT
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 1U, 2U);   // transmit command for me
    pump(80U);
    n = replies(MIL1553_CHANNEL_A, w, 8U);
    TEST_ASSERT_EQUAL_UINT(3U, n);                                       // status + 2 data
    TEST_ASSERT_EQUAL_UINT16(0x91U, w[1].field);
    TEST_ASSERT_EQUAL_UINT16(0x92U, w[2].field);
}

void test_llr_1553_rt_007_a_paired_command_addressed_to_myself_is_not_rt_to_rt(void)
{
    sw_t w[4];
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 2U);
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 3U, 2U);   // transmit command naming me too
    pump(80U);
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_WORD_COUNT_MISMATCH));
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U) > 4U);
}

// LLR-1553-RT-008
void test_llr_1553_rt_008_synchronize_without_data_is_handled_and_answered_with_status_only(void)
{
    mode_cmd(MIL1553_CHANNEL_A, 1U, MIL1553_TR_TRANSMIT);
    pump(60U);
    TEST_ASSERT_FALSE(one_status_reply().message_error);
    TEST_ASSERT_EQUAL_UINT32(1U, rt.sync_count);
}

void test_llr_1553_rt_008_synchronize_with_data_stores_the_data_word(void)
{
    mode_cmd(MIL1553_CHANNEL_A, 17U, MIL1553_TR_RECEIVE);
    send_data(MIL1553_CHANNEL_A, 0x1234U);
    pump(80U);
    TEST_ASSERT_FALSE(one_status_reply().message_error);
    TEST_ASSERT_EQUAL_UINT32(1U, rt.sync_count);
    TEST_ASSERT_EQUAL_UINT16(0x1234U, rt.last_sync_data);
}

void test_llr_1553_rt_008_transmit_status_word_answers_with_status_only(void)
{
    mode_cmd(MIL1553_CHANNEL_A, 2U, MIL1553_TR_TRANSMIT);
    pump(60U);
    TEST_ASSERT_EQUAL_UINT8(ME_ADDR, one_status_reply().rt_address);
}

void test_llr_1553_rt_008_initiate_self_test_raises_one_request(void)
{
    TEST_ASSERT_FALSE(rt_take_self_test_request(&rt));
    mode_cmd(MIL1553_CHANNEL_A, 3U, MIL1553_TR_TRANSMIT);
    pump(60U);
    (void)one_status_reply();
    TEST_ASSERT_TRUE(rt_take_self_test_request(&rt));
    TEST_ASSERT_FALSE(rt_take_self_test_request(&rt));                   // taken: cleared
}

void test_llr_1553_rt_008_transmitter_shutdown_silences_the_other_bus_until_overridden(void)
{
    sw_t w[4];
    mode_cmd(MIL1553_CHANNEL_A, 4U, MIL1553_TR_TRANSMIT);               // received on A: shuts down B
    pump(60U);
    (void)one_status_reply();                                           // the answer on A still goes out
    TEST_ASSERT_TRUE(rt.transmitter_shutdown[1]);
    TEST_ASSERT_FALSE(rt.transmitter_shutdown[0]);

    bus_model_advance_us(&bus, 10U);
    send_cmd(MIL1553_CHANNEL_B, ME_ADDR, MIL1553_TR_TRANSMIT, 3U, 1U); // a command on B gets no answer
    pump(60U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_B, w, 4U));

    bus_model_advance_us(&bus, 10U);
    mode_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT);               // override
    pump(60U);
    (void)one_status_reply();
    TEST_ASSERT_FALSE(rt.transmitter_shutdown[1]);
    bus_model_advance_us(&bus, 10U);
    send_cmd(MIL1553_CHANNEL_B, ME_ADDR, MIL1553_TR_TRANSMIT, 3U, 1U);
    pump(60U);
    TEST_ASSERT_TRUE(replies(MIL1553_CHANNEL_B, w, 4U) >= 1U);
}

void test_llr_1553_rt_008_inhibit_terminal_flag_hides_tf_until_overridden(void)
{
    rt_report_bit_result(&rt, false, true, false);                       // TF is genuinely set
    bc_receive(1U, 1U, 1U);   // a valid one-word receive: answered with a status word only
    pump(60U);
    TEST_ASSERT_TRUE(one_status_reply().terminal_flag);

    bus_model_advance_us(&bus, 10U);
    mode_cmd(MIL1553_CHANNEL_A, 6U, MIL1553_TR_TRANSMIT);               // inhibit
    pump(60U);
    TEST_ASSERT_FALSE(one_status_reply().terminal_flag);

    bus_model_advance_us(&bus, 10U);
    mode_cmd(MIL1553_CHANNEL_A, 7U, MIL1553_TR_TRANSMIT);               // override inhibit
    pump(60U);
    TEST_ASSERT_TRUE(one_status_reply().terminal_flag);
}

void test_llr_1553_rt_008_reset_sends_its_status_then_enters_reset_and_clears_state(void)
{
    rt_set_subsystem_flag(&rt, true);
    rt_report_bit_result(&rt, false, true, false);
    mode_cmd(MIL1553_CHANNEL_A, 8U, MIL1553_TR_TRANSMIT);
    pump(60U);
    (void)one_status_reply();                                           // answered first ...
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_RESET, rt_get_state(&rt));       // ... then reset
    TEST_ASSERT_FALSE(rt.terminal_flag);
    TEST_ASSERT_FALSE(rt_run(&rt));                                     // nothing runs until PBIT completes

    rt_pbit_complete(&rt, true);
    TEST_ASSERT_TRUE(rt_enable(&rt));
    bus_model_advance_us(&bus, 10U);
    bc_receive(1U, 1U, 1U);   // a valid one-word receive: answered with a status word only
    pump(60U);
    TEST_ASSERT_EQUAL_UINT8(ME_ADDR, one_status_reply().rt_address);    // serving again
}

void test_llr_1553_rt_008_transmit_vector_word_returns_the_configured_word(void)
{
    sw_t w[4];
    rt_set_vector_word(&rt, 0xBEEFU);
    mode_cmd(MIL1553_CHANNEL_A, 16U, MIL1553_TR_TRANSMIT);
    pump(60U);
    TEST_ASSERT_EQUAL_UINT(2U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL(MIL1553_SYNC_DATA, w[1].sync);
    TEST_ASSERT_EQUAL_UINT16(0xBEEFU, w[1].field);
}

void test_llr_1553_rt_008_transmit_last_command_returns_the_command_before_this_one(void)
{
    sw_t w[4]; mil1553_symbols_t s; uint16_t expected;
    ts_cmd(s, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 1U);                      // the command that will be "last"
    (void)ts_decode(s, &expected, (mil1553_sync_t *)0);
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, s);
    send_data(MIL1553_CHANNEL_A, 5U);
    pump(60U);
    (void)replies(MIL1553_CHANNEL_A, w, 4U);

    bus_model_advance_us(&bus, 10U);
    mode_cmd(MIL1553_CHANNEL_A, 18U, MIL1553_TR_TRANSMIT);
    pump(60U);
    TEST_ASSERT_EQUAL_UINT(2U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT16(expected, w[1].field);
}

void test_llr_1553_rt_008_transmit_bit_word_returns_the_word_built_from_the_bit_result(void)
{
    sw_t w[4];
    mode_cmd(MIL1553_CHANNEL_A, 19U, MIL1553_TR_TRANSMIT);
    pump(60U);
    TEST_ASSERT_EQUAL_UINT(2U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT16(0U, w[1].field);                            // nothing applied yet

    rt_apply_bit_state(&rt, MIL1553_BIT_STATE_DEGRADED, 0x0A52U);
    bus_model_advance_us(&bus, 10U);
    mode_cmd(MIL1553_CHANNEL_A, 19U, MIL1553_TR_TRANSMIT);
    pump(60U);
    TEST_ASSERT_EQUAL_UINT(2U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT16(0x0A52U, w[1].field);
}

void test_llr_1553_rt_006_a_degraded_bit_state_is_reported_as_the_subsystem_flag_and_clears(void)
{
    rt_apply_bit_state(&rt, MIL1553_BIT_STATE_DEGRADED, 0U);
    bc_receive(1U, 1U, 1U);
    pump(60U);
    {
        mil1553_status_fields_t st = one_status_reply();
        TEST_ASSERT_TRUE(st.subsystem_flag);
        TEST_ASSERT_FALSE(st.terminal_flag);                              // non-fatal: TF stays clear
    }
    rt_apply_bit_state(&rt, MIL1553_BIT_STATE_OPERATIONAL, 0U);          // a clean CBIT cycle
    bus_model_advance_us(&bus, 10U);
    bc_receive(1U, 1U, 1U);
    pump(60U);
    TEST_ASSERT_FALSE(one_status_reply().subsystem_flag);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt));
}

void test_llr_1553_rt_013_a_fail_bit_state_puts_the_rt_in_fail_safe_and_operational_does_not_undo_it(void)
{
    rt_apply_bit_state(&rt, MIL1553_BIT_STATE_FAIL, 0x0001U);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_FAIL_SAFE, rt_get_state(&rt));
    rt_apply_bit_state(&rt, MIL1553_BIT_STATE_OPERATIONAL, 0U);          // only a reset leaves FAIL_SAFE
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_FAIL_SAFE, rt_get_state(&rt));
    TEST_ASSERT_TRUE(rt.fail_safe);
}

void test_llr_1553_rt_013_in_progress_bit_states_change_no_flag(void)
{
    rt_apply_bit_state(&rt, MIL1553_BIT_STATE_CBIT_RUNNING, 0U);
    rt_apply_bit_state(&rt, MIL1553_BIT_STATE_IBIT_RUNNING, 0U);
    rt_apply_bit_state(&rt, MIL1553_BIT_STATE_PBIT, 0U);
    TEST_ASSERT_FALSE(rt.bit_degraded);
    TEST_ASSERT_FALSE(rt.fail_safe);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt));
}

void test_llr_1553_rt_008_unsupported_mode_codes_are_illegal_and_have_no_effect(void)
{
    const uint8_t codes[] = { 0U, 9U, 15U, 20U, 31U };
    unsigned i;
    for (i = 0U; i < sizeof(codes); i++)
    {
        setUp();
        mode_cmd(MIL1553_CHANNEL_A, codes[i], MIL1553_TR_TRANSMIT);
        pump(60U);
        TEST_ASSERT_TRUE(one_status_reply().message_error);
        TEST_ASSERT_EQUAL_UINT32(0U, rt.sync_count);
        TEST_ASSERT_FALSE(rt.self_test_requested);
        TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt));
    }
}

void test_llr_1553_rt_008_subaddress_31_is_also_a_mode_indicator(void)
{
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 31U, 1U);
    pump(60U);
    (void)one_status_reply();
    TEST_ASSERT_EQUAL_UINT32(1U, rt.sync_count);
}

// LLR-1553-RT-009
void test_llr_1553_rt_009_valid_broadcast_is_executed_but_never_answered(void)
{
    sw_t w[4];
    send_cmd(MIL1553_CHANNEL_A, 31U, MIL1553_TR_RECEIVE, 1U, 2U);
    send_data(MIL1553_CHANNEL_A, 0x61U);
    send_data(MIL1553_CHANNEL_A, 0x62U);
    pump(100U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_B, w, 4U));
    TEST_ASSERT_EQUAL_UINT8(2U, rt.subaddr_buffer[1].word_count);        // but it was executed
    TEST_ASSERT_EQUAL_UINT16(0x62U, rt.subaddr_buffer[1].words[1]);
    TEST_ASSERT_TRUE(rt.broadcast_received_latched);
}

void test_llr_1553_rt_009_an_illegal_broadcast_is_not_answered_either(void)
{
    sw_t w[4];
    send_cmd(MIL1553_CHANNEL_A, 31U, MIL1553_TR_RECEIVE, 9U, 1U);        // disabled subaddress
    send_data(MIL1553_CHANNEL_A, 1U);
    pump(100U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_TRUE(rt.message_error_latched);
}

void test_llr_1553_rt_009_a_broadcast_transmit_command_is_illegal_and_silent(void)
{
    sw_t w[4];
    send_cmd(MIL1553_CHANNEL_A, 31U, MIL1553_TR_TRANSMIT, 1U, 1U);
    pump(100U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_TRUE(rt.message_error_latched);
}

void test_llr_1553_rt_009_a_busy_rt_does_not_answer_a_broadcast(void)
{
    sw_t w[4];
    rt_set_busy(&rt, true);
    send_cmd(MIL1553_CHANNEL_A, 31U, MIL1553_TR_RECEIVE, 1U, 1U);
    send_data(MIL1553_CHANNEL_A, 1U);
    pump(100U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
}

void test_llr_1553_rt_009_broadcast_mode_command_is_executed_silently(void)
{
    sw_t w[4];
    send_cmd(MIL1553_CHANNEL_A, 31U, MIL1553_TR_TRANSMIT, 0U, 1U);       // broadcast synchronize
    pump(100U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT32(1U, rt.sync_count);
}

// LLR-1553-RT-010
static void corrupt_and_send_cmd(uint8_t cell_byte, uint8_t mask)
{
    mil1553_symbols_t s;
    ts_cmd(s, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 2U);
    s[cell_byte] = (uint8_t)(s[cell_byte] ^ mask);
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, s);
}

void test_llr_1553_rt_010_corrupt_command_is_discarded_silently_with_me_set(void)
{
    sw_t w[4];
    corrupt_and_send_cmd(4U, 0x03U);                                     // parity cell
    send_data(MIL1553_CHANNEL_A, 1U);
    send_data(MIL1553_CHANNEL_A, 2U);
    pump(100U);
    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_PARITY));
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[1].word_count);        // its data words were not taken
    TEST_ASSERT_TRUE(rt.message_error_latched);
}

void test_llr_1553_rt_010_each_kind_of_word_error_is_logged_as_that_kind(void)
{
    corrupt_and_send_cmd(4U, 0x03U);   pump(40U);                       // parity
    corrupt_and_send_cmd(3U, 0x80U);   pump(40U);                       // Manchester violation in a data cell
    corrupt_and_send_cmd(0U, 0x80U);   pump(40U);                       // sync
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_PARITY));
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_MANCHESTER));
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_SYNC));
}

void test_llr_1553_rt_010_the_error_record_names_the_channel_and_this_terminal(void)
{
    corrupt_and_send_cmd(4U, 0x03U);
    pump(40U);
    TEST_ASSERT_EQUAL_UINT(1U, rt.error_log_count);
    TEST_ASSERT_EQUAL(MIL1553_TERMINAL_RT1, rt.error_log[0].terminal);
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_A, rt.error_log[0].channel);
}

void test_llr_1553_rt_010_the_error_log_is_bounded_and_counts_overflow(void)
{
    unsigned i;
    for (i = 0U; i < (MIL1553_ERROR_LOG_CAPACITY + 5U); i++)
    {
        corrupt_and_send_cmd(4U, 0x03U);
        pump(30U);
    }
    TEST_ASSERT_EQUAL_UINT(MIL1553_ERROR_LOG_CAPACITY, rt.error_log_count);
    TEST_ASSERT_EQUAL_UINT32(5U, rt.error_log_overflow_count);
}

// LLR-1553-RT-011
void test_llr_1553_rt_011_illegal_command_answers_with_me_status_after_the_normal_delay(void)
{
    sw_t w[4]; mil1553_time_us_t end;
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 9U, 1U);   // disabled subaddress
    end = bus_model_now_us(&bus);
    pump(80U);
    TEST_ASSERT_EQUAL_UINT(1U, replies(MIL1553_CHANNEL_A, w, 4U));
    TEST_ASSERT_TRUE(status_of(&w[0]).message_error);
    TEST_ASSERT_EQUAL_UINT64(end + 8U, w[0].ts);
}

// LLR-1553-RT-012
void test_llr_1553_rt_012_busy_rt_answers_status_only_and_does_not_execute_a_receive(void)
{
    rt_set_busy(&rt, true);
    bc_receive(1U, 2U, 0x77U);
    pump(80U);
    TEST_ASSERT_TRUE(one_status_reply().busy);
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[1].word_count);
}

void test_llr_1553_rt_012_busy_rt_sends_no_data_for_a_transmit_command(void)
{
    sw_t w[8]; unsigned n;
    rt_set_busy(&rt, true);
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 1U, 3U);
    pump(80U);
    n = replies(MIL1553_CHANNEL_A, w, 8U);
    TEST_ASSERT_EQUAL_UINT(1U, n);
    TEST_ASSERT_TRUE(status_of(&w[0]).busy);
}

void test_llr_1553_rt_012_busy_takes_precedence_over_an_illegal_command(void)
{
    rt_set_busy(&rt, true);
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 9U, 1U);
    pump(80U);
    {
        mil1553_status_fields_t st = one_status_reply();
        TEST_ASSERT_TRUE(st.busy);
        TEST_ASSERT_FALSE(st.message_error);
    }
}

// LLR-1553-RT-013
void test_llr_1553_rt_013_reset_idle_wait_command_path(void)
{
    mil1553_rt_context_t r2;
    bus_model_init(&bus);
    TEST_ASSERT_TRUE(rt_init(&r2, ME_ADDR, MIL1553_TERMINAL_RT1, cfg, 8U, &bus));
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_RESET, rt_get_state(&r2));
    TEST_ASSERT_FALSE(rt_run(&r2));                                      // RESET: nothing runs
    TEST_ASSERT_FALSE(rt_enable(&r2));                                   // cannot enable from RESET
    rt_pbit_complete(&r2, true);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_IDLE, rt_get_state(&r2));
    TEST_ASSERT_FALSE(rt_run(&r2));                                      // IDLE: nothing runs
    TEST_ASSERT_TRUE(rt_enable(&r2));
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&r2));
}

void test_llr_1553_rt_013_pbit_failure_goes_to_fail_safe(void)
{
    mil1553_rt_context_t r2;
    bus_model_init(&bus);
    (void)rt_init(&r2, ME_ADDR, MIL1553_TERMINAL_RT1, cfg, 8U, &bus);
    rt_pbit_complete(&r2, false);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_FAIL_SAFE, rt_get_state(&r2));
    TEST_ASSERT_FALSE(rt_enable(&r2));
}

void test_llr_1553_rt_013_valid_command_walks_validate_execute_respond_wait(void)
{
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 3U, 1U);
    pump(1U);  TEST_ASSERT_EQUAL(MIL1553_RT_STATE_VALIDATE, rt_get_state(&rt));
    pump(1U);  TEST_ASSERT_EQUAL(MIL1553_RT_STATE_EXECUTE, rt_get_state(&rt));
    pump(1U);  TEST_ASSERT_EQUAL(MIL1553_RT_STATE_RESPOND, rt_get_state(&rt));
    TEST_ASSERT_TRUE(pump_until(MIL1553_RT_STATE_WAIT_COMMAND, 30U));
}

void test_llr_1553_rt_013_illegal_command_goes_through_error(void)
{
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 9U, 1U);
    pump(1U);  TEST_ASSERT_EQUAL(MIL1553_RT_STATE_VALIDATE, rt_get_state(&rt));
    pump(1U);  TEST_ASSERT_EQUAL(MIL1553_RT_STATE_ERROR, rt_get_state(&rt));
    TEST_ASSERT_TRUE(pump_until(MIL1553_RT_STATE_WAIT_COMMAND, 30U));
}

void test_llr_1553_rt_013_busy_subsystem_goes_through_busy(void)
{
    rt_set_busy(&rt, true);
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 3U, 1U);
    pump(2U);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_BUSY, rt_get_state(&rt));
    TEST_ASSERT_TRUE(pump_until(MIL1553_RT_STATE_WAIT_COMMAND, 30U));
}

void test_llr_1553_rt_013_transmission_error_goes_through_error_and_stays_silent(void)
{
    mil1553_symbols_t s;
    ts_cmd(s, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 1U);
    s[4] = (uint8_t)(s[4] ^ 0x03U);
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, s);
    pump(1U);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_ERROR, rt_get_state(&rt));
    pump(1U);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt));
}

void test_llr_1553_rt_013_fatal_bit_result_forces_fail_safe_from_any_state(void)
{
    rt_report_bit_result(&rt, true, true, false);                        // from WAIT_COMMAND
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_FAIL_SAFE, rt_get_state(&rt));

    setUp();                                                             // from EXECUTE, mid-message
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 3U);
    pump(2U);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_EXECUTE, rt_get_state(&rt));
    rt_report_bit_result(&rt, true, true, false);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_FAIL_SAFE, rt_get_state(&rt));
}

void test_llr_1553_rt_013_fail_safe_answers_with_tf_and_no_data_and_executes_nothing(void)
{
    sw_t w[4]; mil1553_status_fields_t st;
    rt_report_bit_result(&rt, true, true, false);
    bc_receive(1U, 2U, 0x44U);
    pump(80U);
    TEST_ASSERT_EQUAL_UINT(1U, replies(MIL1553_CHANNEL_A, w, 4U));
    st = status_of(&w[0]);
    TEST_ASSERT_TRUE(st.terminal_flag);
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[1].word_count);        // nothing was executed
}

void test_llr_1553_rt_013_fail_safe_is_latched_across_many_commands(void)
{
    sw_t w[4]; unsigned i;
    rt_report_bit_result(&rt, true, true, false);
    for (i = 0U; i < 3U; i++)
    {
        send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 3U, 1U);
        pump(60U);
        TEST_ASSERT_EQUAL(MIL1553_RT_STATE_FAIL_SAFE, rt_get_state(&rt));
        TEST_ASSERT_EQUAL_UINT(1U, replies(MIL1553_CHANNEL_A, w, 4U));
        TEST_ASSERT_TRUE(status_of(&w[0]).terminal_flag);
        bus_model_advance_us(&bus, 10U);
    }
}

void test_llr_1553_rt_013_fail_safe_tf_cannot_be_hidden_by_inhibit(void)
{
    sw_t w[4];
    mode_cmd(MIL1553_CHANNEL_A, 6U, MIL1553_TR_TRANSMIT);               // inhibit TF while healthy
    pump(60U);
    (void)replies(MIL1553_CHANNEL_A, w, 4U);
    rt_report_bit_result(&rt, true, false, false);                       // then a fatal fault
    bus_model_advance_us(&bus, 10U);
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_TRANSMIT, 3U, 1U);
    pump(60U);
    TEST_ASSERT_TRUE(one_status_reply().terminal_flag);
}

void test_llr_1553_rt_013_reset_mode_command_is_honoured_in_fail_safe(void)
{
    rt_report_bit_result(&rt, true, true, false);
    mode_cmd(MIL1553_CHANNEL_A, 8U, MIL1553_TR_TRANSMIT);
    pump(60U);
    TEST_ASSERT_TRUE(one_status_reply().terminal_flag);                 // answered with TF ...
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_RESET, rt_get_state(&rt));       // ... then reset
    rt_pbit_complete(&rt, true);
    TEST_ASSERT_TRUE(rt_enable(&rt));
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_WAIT_COMMAND, rt_get_state(&rt)); // fail-safe is gone
}

void test_llr_1553_rt_013_rt_reset_works_from_every_state(void)
{
    send_cmd(MIL1553_CHANNEL_A, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 3U);
    pump(2U);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_EXECUTE, rt_get_state(&rt));
    rt_reset(&rt);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_RESET, rt_get_state(&rt));
    TEST_ASSERT_FALSE(rt.message_error_latched);

    rt_report_bit_result(&rt, true, true, false);
    rt_reset(&rt);
    TEST_ASSERT_EQUAL(MIL1553_RT_STATE_RESET, rt_get_state(&rt));
    TEST_ASSERT_FALSE(rt.fail_safe);
}

// LLR-1553-ERR-001 / ERR-004 (RT side)
void test_llr_1553_err_001_a_corrupt_command_is_rejected_logged_by_type_and_channel_and_counted_by_cbit(void)
{
    mil1553_symbols_t bad; sw_t w[4];
    mil1553_bit_cbit_context_t cb;
    bit_cbit_init(&cb, 0U);
    rt_attach_cbit(&rt, &cb);

    ts_cmd(bad, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 1U);
    bad[4] = (uint8_t)(bad[4] ^ 0x03U);
    (void)ts_tx(&bus, MIL1553_CHANNEL_B, MIL1553_TERMINAL_BC, bad);
    send_data(MIL1553_CHANNEL_B, 0x55U);
    pump(60U);

    TEST_ASSERT_EQUAL_UINT(0U, replies(MIL1553_CHANNEL_B, w, 4U));            // message rejected: no answer
    TEST_ASSERT_EQUAL_UINT8(0U, rt.subaddr_buffer[1].word_count);             // ... and nothing stored
    TEST_ASSERT_EQUAL_UINT8(1U, rt_error_log_count(&rt));
    TEST_ASSERT_EQUAL(MIL1553_ERR_PARITY, rt_error_log_get(&rt, 0U)->error_type);
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_B, rt_error_log_get(&rt, 0U)->channel);
    TEST_ASSERT_EQUAL_UINT32(1U, cb.channel[1].class_count[MIL1553_WORD_PARITY_ERROR]);   // per type, per channel
    TEST_ASSERT_EQUAL_UINT32(0U, cb.channel[0].class_count[MIL1553_WORD_PARITY_ERROR]);
}

void test_llr_1553_err_004_the_rt_log_view_is_read_only_bounded_and_keeps_the_earliest(void)
{
    mil1553_symbols_t bad; unsigned i;
    mil1553_error_record_t first;

    ts_cmd(bad, ME_ADDR, MIL1553_TR_RECEIVE, 1U, 1U);
    bad[4] = (uint8_t)(bad[4] ^ 0x03U);
    for (i = 0U; i < (MIL1553_ERROR_LOG_CAPACITY + 4U); i++)
    {
        (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, bad);
        pump(30U);
        if (i == 0U) { first = *rt_error_log_get(&rt, 0U); }
    }
    TEST_ASSERT_EQUAL_UINT8(MIL1553_ERROR_LOG_CAPACITY, rt_error_log_count(&rt));
    TEST_ASSERT_EQUAL_UINT32(4U, rt_error_log_overflow_count(&rt));
    TEST_ASSERT_EQUAL_UINT64(first.timestamp_us, rt_error_log_get(&rt, 0U)->timestamp_us);   // earliest kept
    TEST_ASSERT_EQUAL(MIL1553_TERMINAL_RT1, rt_error_log_get(&rt, 0U)->terminal);
    TEST_ASSERT_NULL(rt_error_log_get(&rt, MIL1553_ERROR_LOG_CAPACITY));
}