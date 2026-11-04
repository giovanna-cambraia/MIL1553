/*
    verifies: LLR-1553-BC-001 .. LLR-1553-BC-010, LLR-1553-REDUN-001 .. LLR-1553-REDUN-003
    module: bc_scheduler (with bus_model, mil1553_word, manchester)

    fixture: the BC under test runs against a scripted "fake RT" that uses terminal id RT2 (listener and sender), and a spy that reads the bus as the BM (the BM has its own
    cursor, so it never disturbs the BC)
*/

#include "test_support.h"
#include "bc_scheduler.h"

static mil1553_bus_model_context_t bus;
static mil1553_bc_context_t bc;
static mil1553_bc_schedule_t sched;

static bool auto_a, auto_b; // fake RT answers commands seen on this channel

typedef struct {
    uint16_t field; mil1553_sync_t sync; mil1553_word_class_t cls;
    mil1553_time_us_t ts; mil1553_terminal_id_t sender; mil1553_channel_t ch;
} sw_t;

void setUp(void)
{
    (void)memset(&sched, 0, sizeof(sched));
    sched.minor_frame_count = 1U;
    sched.minor_frame_period_us = 1000U;
    auto_a = false;
    auto_b = false;
    bus_model_init(&bus);
}
void tearDown(void) {}

// fixture helpers

static mil1553_bc_schedule_entry_t *add_entry(mil1553_msg_type_t t, uint8_t rt, uint8_t sub, uint8_t n, uint32_t offset)
{
    mil1553_bc_schedule_entry_t *e = &sched.entries[sched.entry_count];
    (void)memset(e, 0, sizeof(*e));
    e->msg_type = t; e->rt_address = rt; e->subaddress = sub;
    e->data_word_count = n; e->slot_offset_us = offset;
    sched.entry_count++;
    return e;
}

static void start_bc(void)
{
    TEST_ASSERT_TRUE(bc_init(&bc, &sched, &bus, MIL1553_CHANNEL_A, 3U));
    TEST_ASSERT_TRUE(bc_start(&bc));
}

static void fake_rt_poll(void)
{
    mil1553_channel_t ch;
    for (ch = MIL1553_CHANNEL_A; ch <= MIL1553_CHANNEL_B; ch++)
    {
        mil1553_bus_record_t rec;
        bool on = (ch == MIL1553_CHANNEL_A) ? auto_a : auto_b;
        while (ts_rx(&bus, ch, MIL1553_TERMINAL_RT2, &rec))
        {
            uint16_t f; mil1553_sync_t sy; mil1553_symbols_t s;
            mil1553_command_fields_t c;
            if (!on || rec.sender != MIL1553_TERMINAL_BC) { continue; }
            if (ts_decode(rec.symbols, &f, &sy) != MIL1553_WORD_VALID || sy != MIL1553_SYNC_COMMAND_STATUS) { continue; }
            mil1553_word_unpack_command(f, &c);
            if (c.rt_address == MIL1553_RT_ADDR_BROADCAST) { continue; }
            ts_status(s, c.rt_address, false, false, false, false, false);
            (void)ts_tx(&bus, ch, MIL1553_TERMINAL_RT2, s);
            if (c.tr_bit == MIL1553_TR_TRANSMIT)
            {
                uint8_t n = mil1553_word_decode_word_count(c.word_count_field), k;
                for (k = 0U; k < n; k++) { ts_data(s, k); (void)ts_tx(&bus, ch, MIL1553_TERMINAL_RT2, s); }
            }
        }
    }
}

static void step(unsigned n)
{
    unsigned i;
    for (i = 0U; i < n; i++)
    {
        (void)bc_run_frame(&bc);
        fake_rt_poll();
        bus_model_advance_us(&bus, 1U);
    }
}

static bool step_until(mil1553_bc_state_t st, unsigned max)
{
    unsigned i;
    for (i = 0U; i < max; i++)
    {
        if (bc_get_state(&bc) == st) { return true; }
        step(1U);
    }
    return bc_get_state(&bc) == st;
}

// everything on a channel since the last call, as seen by the BM
static unsigned spy(mil1553_channel_t ch, sw_t *out, unsigned max)
{
    unsigned n = 0U;
    mil1553_bus_record_t r;
    while ((n < max) && ts_rx(&bus, ch, MIL1553_TERMINAL_BM, &r))
    {
        out[n].cls = ts_decode(r.symbols, &out[n].field, &out[n].sync);
        out[n].ts = r.timestamp_us; out[n].sender = r.sender; out[n].ch = ch;
        n++;
    }
    return n;
}

static void reply_status(mil1553_channel_t ch, uint8_t rt, bool me, bool busy, bool sf, bool tf)
{
    mil1553_symbols_t s;
    ts_status(s, rt, me, busy, sf, tf, false);
    (void)ts_tx(&bus, ch, MIL1553_TERMINAL_RT2, s);
}

static void reply_data(mil1553_channel_t ch, unsigned n)
{
    mil1553_symbols_t s; unsigned k;
    for (k = 0U; k < n; k++) { ts_data(s, (uint16_t)(0x100U + k)); (void)ts_tx(&bus, ch, MIL1553_TERMINAL_RT2, s); }
}

static unsigned count_errors(mil1553_error_type_t t)
{
    unsigned i, n = 0U;
    for (i = 0U; i < bc.error_log_count; i++) { if (bc.error_log[i].error_type == t) { n++; } }
    return n;
}

static mil1553_command_fields_t as_cmd(const sw_t *w)
{
    mil1553_command_fields_t c;
    mil1553_word_unpack_command(w->field, &c);
    return c;
}

// LLR-1553-BC-001
void test_llr_1553_bc_001_valid_table_is_accepted(void)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 4U, 0U);
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 6U, 2U, 4U, 300U);
    TEST_ASSERT_TRUE(bc_validate_schedule(&sched));
}

void test_llr_1553_bc_001_out_of_range_fields_are_rejected(void)
{
    mil1553_bc_schedule_t base;

    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 4U, 0U);
    base = sched;
    TEST_ASSERT_TRUE(bc_validate_schedule(&base));

    sched = base; sched.entries[0].rt_address = 32U;           TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
    sched = base; sched.entries[0].minor_frame_index = 1U;     TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
    sched = base; sched.entries[0].subaddress = 32U;           TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
    sched = base; sched.entries[0].data_word_count = 0U;       TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
    sched = base; sched.entries[0].data_word_count = 33U;      TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
    sched = base; sched.entry_count = (uint8_t)(BC_MAX_SCHEDULE_ENTRIES + 1U); TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
    sched = base; sched.minor_frame_count = 0U;                TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
    sched = base; sched.minor_frame_count = (uint8_t)(BC_MAX_MINOR_FRAMES + 1U); TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
    sched = base; sched.minor_frame_period_us = 0U;            TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
}

void test_llr_1553_bc_001_rt_to_rt_second_rt_must_be_a_real_rt(void)
{
    mil1553_bc_schedule_entry_t *e = add_entry(MIL1553_MSG_RT_TO_RT, 1U, 1U, 2U, 0U);
    e->rt_address_2 = 2U; e->subaddress_2 = 2U;
    TEST_ASSERT_TRUE(bc_validate_schedule(&sched));
    e->rt_address_2 = 31U;
    TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
}

void test_llr_1553_bc_001_overlapping_slots_in_one_minor_frame_are_rejected(void)
{
    // BC_TO_RT, 2 words: worst case duration is 96 us
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 2U, 0U);
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 6U, 1U, 2U, 95U);
    TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
    sched.entries[1].slot_offset_us = 96U; // starts exactly when the first ends
    TEST_ASSERT_TRUE(bc_validate_schedule(&sched));
    sched.minor_frame_count = 2U; // same offset in different minor frames is fine
    sched.entries[1].slot_offset_us = 0U;
    sched.entries[1].minor_frame_index = 1U;
    TEST_ASSERT_TRUE(bc_validate_schedule(&sched));
}

void test_llr_1553_bc_001_entry_must_fit_inside_the_minor_frame(void)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 2U, 904U); // 904 + 96 = 1000
    TEST_ASSERT_TRUE(bc_validate_schedule(&sched));
    sched.entries[0].slot_offset_us = 905U;
    TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
}

void test_llr_1553_bc_001_duration_matches_the_timing_budget(void)
{
    mil1553_bc_schedule_entry_t e;
    (void)memset(&e, 0, sizeof(e));

    e.msg_type = MIL1553_MSG_BC_TO_RT; e.data_word_count = 32U;
    TEST_ASSERT_EQUAL_UINT32(696U, bc_message_duration_us(&e));
    e.msg_type = MIL1553_MSG_RT_TO_BC;
    TEST_ASSERT_EQUAL_UINT32(696U, bc_message_duration_us(&e));
    e.msg_type = MIL1553_MSG_RT_TO_RT;
    TEST_ASSERT_EQUAL_UINT32(748U, bc_message_duration_us(&e));
    e.msg_type = MIL1553_MSG_MODE_CODE; e.has_data_word = false;
    TEST_ASSERT_EQUAL_UINT32(56U, bc_message_duration_us(&e));
    e.has_data_word = true;
    TEST_ASSERT_EQUAL_UINT32(76U, bc_message_duration_us(&e));
    e.msg_type = MIL1553_MSG_BROADCAST; e.data_word_count = 3U;
    TEST_ASSERT_EQUAL_UINT32(84U, bc_message_duration_us(&e));   // 20 + 60 + 4
}

void test_llr_1553_bc_001_init_rejects_an_invalid_table_and_stays_idle(void)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 40U, 1U, 2U, 0U); // bad RT address
    TEST_ASSERT_FALSE(bc_init(&bc, &sched, &bus, MIL1553_CHANNEL_A, 3U));
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_IDLE, bc_get_state(&bc));
}

void test_llr_1553_bc_001_threshold_zero_means_default_three(void)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 2U, 0U);
    TEST_ASSERT_TRUE(bc_init(&bc, &sched, &bus, MIL1553_CHANNEL_A, 0U));
    TEST_ASSERT_EQUAL_UINT8(MIL1553_REDUN_FAIL_THRESHOLD_DEFAULT, bc.fail_threshold);
}

// LLR-1553-BC-002
void test_llr_1553_bc_002_message_starts_at_its_slot_offset_not_before(void)
{
    sw_t w[8]; unsigned n;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 300U);
    start_bc();
    step(299U);
    TEST_ASSERT_EQUAL_UINT(0U, spy(MIL1553_CHANNEL_A, w, 8U));
    step(10U);
    n = spy(MIL1553_CHANNEL_A, w, 8U);
    TEST_ASSERT_TRUE(n >= 1U);
    TEST_ASSERT_EQUAL_UINT64(300U, w[0].ts);
}

void test_llr_1553_bc_002_minor_frames_advances_then_the_major_frame_wraps(void)
{
    sw_t w[16]; unsigned n, i, cmds = 0U;
    mil1553_time_us_t t_frame1_cmd = 0U;

    sched.minor_frame_count = 2U;
    sched.minor_frame_period_us = 500U;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 6U, 1U, 1U, 0U);
    sched.entries[1].minor_frame_index = 1U;
    auto_a = true;
    start_bc();
    step(1100U); // a bit more than two minor frames

    n = spy(MIL1553_CHANNEL_A, w, 16U);
    for (i = 0U; i < n; i++)
    {
        if ((w[i].sender == MIL1553_TERMINAL_BC) && (w[i].sync == MIL1553_SYNC_COMMAND_STATUS))
        {
            mil1553_command_fields_t c = as_cmd(&w[i]);
            if (cmds == 1U) { TEST_ASSERT_EQUAL_UINT8(6U, c.rt_address); t_frame1_cmd = w[i].ts; }
            cmds++;
        }
    }
    TEST_ASSERT_TRUE(cmds >= 3U); // frame0, frame1, then frame0 of the next major frame
    TEST_ASSERT_TRUE(t_frame1_cmd >= 500U); // frame-1 message waited for its own minor frame
}

void test_llr_1553_bc_002_message_still_running_at_frame_end_raises_one_overrun(void)
{
    sched.minor_frame_period_us = 100U;
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 2U, 0U); // 96 us, nobody answers
    start_bc();
    while (bus_model_now_us(&bus) < 150U) { step(1U); } // first minor frame, retry still in flight
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_MINOR_FRAME_OVERRUN));
    {
        unsigned i; // and it was raised only once the frame's 100 us had passed
        for (i = 0U; i < bc.error_log_count; i++)
        {
            if (bc.error_log[i].error_type == MIL1553_ERR_MINOR_FRAME_OVERRUN)
            {
                TEST_ASSERT_TRUE(bc.error_log[i].timestamp_us >= 100U);
            }
        }
    }
}

void test_llr_1553_bc_002_no_overrun_when_every_message_finishes_in_time(void)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 2U, 0U);
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 6U, 2U, 2U, 300U);
    auto_a = true;
    start_bc();
    step(3100U);
    TEST_ASSERT_EQUAL_UINT(0U, count_errors(MIL1553_ERR_MINOR_FRAME_OVERRUN));
    TEST_ASSERT_EQUAL_UINT(0U, bc.error_log_count);
}

// LLR-1553-BC-003
void test_llr_1553_bc_003_bc_to_rt_sends_command_then_n_data_words(void)
{
    sw_t w[40]; unsigned n, i;
    mil1553_command_fields_t c;

    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 3U, 3U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    n = spy(MIL1553_CHANNEL_A, w, 40U);
    TEST_ASSERT_EQUAL_UINT(4U, n);
    c = as_cmd(&w[0]);
    TEST_ASSERT_EQUAL(MIL1553_SYNC_COMMAND_STATUS, w[0].sync);
    TEST_ASSERT_EQUAL_UINT8(5U, c.rt_address);
    TEST_ASSERT_EQUAL_UINT8(MIL1553_TR_RECEIVE, c.tr_bit);
    TEST_ASSERT_EQUAL_UINT8(3U, c.subaddress);
    TEST_ASSERT_EQUAL_UINT8(3U, c.word_count_field);
    for (i = 1U; i < 4U; i++) { TEST_ASSERT_EQUAL(MIL1553_SYNC_DATA, w[i].sync); }
}

void test_llr_1553_bc_003_thirty_two_words_use_word_count_field_zero(void)
{
    sw_t w[40]; unsigned n;
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 3U, 32U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    n = spy(MIL1553_CHANNEL_A, w, 40U);
    TEST_ASSERT_EQUAL_UINT(33U, n);
    TEST_ASSERT_EQUAL_UINT8(0U, as_cmd(&w[0]).word_count_field);
}

void test_llr_1553_bc_003_valid_status_completes_the_message(void)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 3U, 2U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    step(1U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
    TEST_ASSERT_EQUAL_UINT(0U, bc.error_log_count);
}

void test_llr_1553_bc_003_status_from_the_wrong_rt_is_an_error(void)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 3U, 2U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 6U, false, false, false, false);
    step(1U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_TIMEOUT, bc_get_state(&bc));
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_STATUS_ADDRESS_MISMATCH));
}

// LLR-1553-BC-004
void test_llr_1553_bc_004_rt_to_bc_sends_only_the_transmit_command(void)
{
    sw_t w[8]; unsigned n; mil1553_command_fields_t c;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 2U, 4U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    n = spy(MIL1553_CHANNEL_A, w, 8U);
    TEST_ASSERT_EQUAL_UINT(1U, n);
    c = as_cmd(&w[0]);
    TEST_ASSERT_EQUAL_UINT8(MIL1553_TR_TRANSMIT, c.tr_bit);
    TEST_ASSERT_EQUAL_UINT8(4U, c.word_count_field);
}

void test_llr_1553_bc_004_status_plus_exactly_n_data_words_completes(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 2U, 3U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    reply_data(MIL1553_CHANNEL_A, 3U);
    step(6U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
    TEST_ASSERT_EQUAL_UINT(0U, bc.error_log_count);
}

void test_llr_1553_bc_004_fewer_data_words_than_commanded_is_a_word_count_mismatch(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 2U, 3U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    reply_data(MIL1553_CHANNEL_A, 2U); // one short
    step(40U);
    TEST_ASSERT_TRUE(bc.error_log_count >= 1U);
    TEST_ASSERT_EQUAL(MIL1553_ERR_WORD_COUNT_MISMATCH, bc.error_log[0].error_type); // reported as a count error ...
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_WORD_COUNT_MISMATCH));
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_A, bc.error_log[0].channel); // ... on the bus it happened on
}

void test_llr_1553_bc_004_silence_after_the_command_is_no_response_not_a_count_error(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 2U, 3U, 0U);
    start_bc();
    step(40U);
    TEST_ASSERT_TRUE(count_errors(MIL1553_ERR_NO_RESPONSE) >= 1U);
    TEST_ASSERT_EQUAL_UINT(0U, count_errors(MIL1553_ERR_WORD_COUNT_MISMATCH));
}

void test_llr_1553_bc_004_status_with_message_error_is_rejected(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 2U, 1U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, true, false, false, false);
    step(1U);
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_STATUS_ME));
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_TIMEOUT, bc_get_state(&bc));
}

// LLR-1553-BC-005
static void add_rt_to_rt(void)
{
    mil1553_bc_schedule_entry_t *e = add_entry(MIL1553_MSG_RT_TO_RT, 1U, 1U, 2U, 0U);
    e->rt_address_2 = 2U; e->subaddress_2 = 2U;
}

void test_llr_1553_bc_005_rt_to_rt_sends_receive_command_then_transmit_command_back_to_back(void)
{
    sw_t w[8]; unsigned n; mil1553_command_fields_t c1, c2;
    add_rt_to_rt();
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    n = spy(MIL1553_CHANNEL_A, w, 8U);
    TEST_ASSERT_EQUAL_UINT(2U, n);
    c1 = as_cmd(&w[0]); c2 = as_cmd(&w[1]);
    TEST_ASSERT_EQUAL_UINT8(1U, c1.rt_address); TEST_ASSERT_EQUAL_UINT8(MIL1553_TR_RECEIVE, c1.tr_bit);
    TEST_ASSERT_EQUAL_UINT8(2U, c2.rt_address); TEST_ASSERT_EQUAL_UINT8(MIL1553_TR_TRANSMIT, c2.tr_bit);
    TEST_ASSERT_EQUAL_UINT8(1U, c1.subaddress); TEST_ASSERT_EQUAL_UINT8(2U, c2.subaddress);
    TEST_ASSERT_EQUAL_UINT8(c1.word_count_field, c2.word_count_field);
    TEST_ASSERT_EQUAL_UINT64(MIL1553_WORD_TIME_US, w[1].ts - w[0].ts); // no gap between the two
}

void test_llr_1553_bc_005_both_status_valid_completes(void)
{
    add_rt_to_rt();
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 2U, false, false, false, false); // transmitting RT first
    reply_data(MIL1553_CHANNEL_A, 2U);
    reply_status(MIL1553_CHANNEL_A, 1U, false, false, false, false); // then the receiving RT
    step(8U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
    TEST_ASSERT_EQUAL_UINT(0U, bc.error_log_count);
}

void test_llr_1553_bc_005_wrong_receiving_rt_status_is_an_error(void)
{
    add_rt_to_rt();
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 2U, false, false, false, false);
    reply_data(MIL1553_CHANNEL_A, 2U);
    reply_status(MIL1553_CHANNEL_A, 9U, false, false, false, false); // not RT1
    step(8U);
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_STATUS_ADDRESS_MISMATCH));
}

void test_llr_1553_bc_005_missing_receiving_rt_status_is_no_response(void)
{
    add_rt_to_rt();
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 2U, false, false, false, false);
    reply_data(MIL1553_CHANNEL_A, 2U); // RT1 never answers
    step(40U);
    TEST_ASSERT_TRUE(count_errors(MIL1553_ERR_NO_RESPONSE) >= 1U);
}

// LLR-1553-BC-006
static mil1553_bc_schedule_entry_t *add_mode(uint8_t code, bool has_data, bool from_bc)
{
    mil1553_bc_schedule_entry_t *e = add_entry(MIL1553_MSG_MODE_CODE, 5U, 0U, 0U, 0U);
    e->mode_code = code; e->has_data_word = has_data; e->mode_data_from_bc = from_bc;
    return e;
}

void test_llr_1553_bc_006_mode_command_without_data_is_a_single_word(void)
{
    sw_t w[4]; unsigned n; mil1553_command_fields_t c;
    (void)add_mode(2U, false, false);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    n = spy(MIL1553_CHANNEL_A, w, 4U);
    TEST_ASSERT_EQUAL_UINT(1U, n);
    c = as_cmd(&w[0]);
    TEST_ASSERT_EQUAL_UINT8(0U, c.subaddress); // mode indicator
    TEST_ASSERT_EQUAL_UINT8(2U, c.word_count_field); // the mode code
}

void test_llr_1553_bc_006_mode_command_with_data_from_bc_sends_one_data_word(void)
{
    sw_t w[4]; unsigned n;
    (void)add_mode(17U, true, true);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    n = spy(MIL1553_CHANNEL_A, w, 4U);
    TEST_ASSERT_EQUAL_UINT(2U, n);
    TEST_ASSERT_EQUAL_UINT8(MIL1553_TR_RECEIVE, as_cmd(&w[0]).tr_bit);
    TEST_ASSERT_EQUAL(MIL1553_SYNC_DATA, w[1].sync);
}

void test_llr_1553_bc_006_mode_command_with_data_from_rt_waits_for_status_and_one_word(void)
{
    sw_t w[4]; unsigned n;
    (void)add_mode(19U, true, false);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    n = spy(MIL1553_CHANNEL_A, w, 4U);
    TEST_ASSERT_EQUAL_UINT(1U, n); // BC sends no data word
    TEST_ASSERT_EQUAL_UINT8(MIL1553_TR_TRANSMIT, as_cmd(&w[0]).tr_bit);
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    step(2U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_WAIT_RESPONSE, bc_get_state(&bc)); // still owes the data word
    reply_data(MIL1553_CHANNEL_A, 1U);
    step(2U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
}

void test_llr_1553_bc_006_unlisted_or_inconsistent_mode_codes_are_rejected(void)
{
    const uint8_t unlisted[] = { 0U, 9U, 15U, 20U, 31U };
    unsigned i;

    for (i = 0U; i < sizeof(unlisted); i++)
    {
        setUp();
        (void)add_mode(unlisted[i], false, false);
        TEST_ASSERT_FALSE(bc_validate_schedule(&sched));
    }
    setUp(); (void)add_mode(32U, false, false); TEST_ASSERT_FALSE(bc_validate_schedule(&sched)); // out of range
    setUp(); (void)add_mode(1U, true, true); TEST_ASSERT_FALSE(bc_validate_schedule(&sched)); // table says no data
    setUp(); (void)add_mode(17U, false, false); TEST_ASSERT_FALSE(bc_validate_schedule(&sched)); // table says data
    setUp(); (void)add_mode(17U, true, false); TEST_ASSERT_FALSE(bc_validate_schedule(&sched)); // wrong direction
    setUp(); (void)add_mode(19U, true, true); TEST_ASSERT_FALSE(bc_validate_schedule(&sched)); // wrong direction
}

void test_llr_1553_bc_006_every_listed_mode_code_is_accepted_with_its_table_shape(void)
{
    const uint8_t no_data[] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };
    unsigned i;
    for (i = 0U; i < sizeof(no_data); i++) { setUp(); (void)add_mode(no_data[i], false, false); TEST_ASSERT_TRUE(bc_validate_schedule(&sched)); }
    setUp(); (void)add_mode(16U, true, false); TEST_ASSERT_TRUE(bc_validate_schedule(&sched));
    setUp(); (void)add_mode(17U, true, true); TEST_ASSERT_TRUE(bc_validate_schedule(&sched));
    setUp(); (void)add_mode(18U, true, false); TEST_ASSERT_TRUE(bc_validate_schedule(&sched));
    setUp(); (void)add_mode(19U, true, false); TEST_ASSERT_TRUE(bc_validate_schedule(&sched));
}

// LLR-1553-BC-007
void test_llr_1553_bc_007_broadcast_sends_command_and_data_and_waits_for_nothing(void)
{
    sw_t w[8]; unsigned n; mil1553_command_fields_t c;
    (void)add_entry(MIL1553_MSG_BROADCAST, 31U, 4U, 2U, 0U);
    start_bc();
    step(60U);
    n = spy(MIL1553_CHANNEL_A, w, 8U);
    TEST_ASSERT_EQUAL_UINT(3U, n); // command + 2 data, nothing else
    c = as_cmd(&w[0]);
    TEST_ASSERT_EQUAL_UINT8(31U, c.rt_address);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
    TEST_ASSERT_EQUAL_UINT(0U, bc.error_log_count); // no false no-response
}

void test_llr_1553_bc_007_broadcast_never_enters_wait_response(void)
{
    unsigned i;
    (void)add_entry(MIL1553_MSG_BROADCAST, 31U, 4U, 1U, 0U);
    start_bc();
    for (i = 0U; i < 100U; i++)
    {
        step(1U);
        TEST_ASSERT_NOT_EQUAL(MIL1553_BC_STATE_WAIT_RESPONSE, bc_get_state(&bc));
    }
}

// LLR-1553-BC-008
void test_llr_1553_bc_008_next_command_waits_four_us_after_the_previous_message_ends(void)
{
    sw_t w[16]; unsigned n, i; mil1553_time_us_t status_end = 0U, second_cmd = 0U; bool seen_first = false;

    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 2U, 0U);
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 6U, 1U, 2U, 96U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    bus_model_advance_us(&bus, 30U); // the RT answers late: past the second slot
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    step(40U);

    n = spy(MIL1553_CHANNEL_A, w, 16U);
    for (i = 0U; i < n; i++)
    {
        if (w[i].sender == MIL1553_TERMINAL_RT2) { status_end = w[i].ts + MIL1553_WORD_TIME_US; }
        if ((w[i].sender == MIL1553_TERMINAL_BC) && (w[i].sync == MIL1553_SYNC_COMMAND_STATUS))
        {
            if (seen_first) { second_cmd = w[i].ts; break; }
            seen_first = true;
        }
    }
    TEST_ASSERT_TRUE(second_cmd != 0U);
    TEST_ASSERT_EQUAL_UINT64(status_end + MIL1553_T_GAP_MIN_US, second_cmd);
}

void test_llr_1553_bc_008_the_retry_after_a_timeout_respects_the_gap(void)
{
    sw_t a[8], b[8]; unsigned na, nb;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    start_bc();
    step(60U);
    na = spy(MIL1553_CHANNEL_A, a, 8U);
    nb = spy(MIL1553_CHANNEL_B, b, 8U);
    TEST_ASSERT_TRUE((na >= 1U) && (nb >= 1U));
    // timeout expires T_NORESP after the first command's last word; the retry waits the gap after that
    TEST_ASSERT_TRUE(b[0].ts >= (a[0].ts + MIL1553_WORD_TIME_US + MIL1553_T_NORESP_US + MIL1553_T_GAP_MIN_US));
}

// LLR-1553-BC-009
static mil1553_time_us_t wait_start(void) { return bc.message_start_time_us; }

void test_llr_1553_bc_009_no_response_is_declared_at_fourteen_us_not_before(void)
{
    mil1553_time_us_t t0, detect = 0U;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    t0 = wait_start();
    while (bc_get_state(&bc) == MIL1553_BC_STATE_WAIT_RESPONSE)
    {
        detect = bus_model_now_us(&bus);
        step(1U);
    }
    TEST_ASSERT_EQUAL_UINT64(MIL1553_T_NORESP_US, detect - t0);
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_NO_RESPONSE));
}

void test_llr_1553_bc_009_status_at_twelve_and_thirteen_us_is_accepted(void)
{
    const unsigned delays[2] = { 12U, 13U };
    unsigned i;
    for (i = 0U; i < 2U; i++)
    {
        setUp();
        (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 1U, 0U);
        start_bc();
        TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
        bus_model_advance_us(&bus, delays[i]);
        reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
        step(2U);
        TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
        TEST_ASSERT_EQUAL_UINT(0U, count_errors(MIL1553_ERR_NO_RESPONSE));
    }
}

void test_llr_1553_bc_009_timeout_restarts_after_each_received_word(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 3U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    step(1U);
    bus_model_advance_us(&bus, 10U); // 10 us of silence: inside the window
    reply_data(MIL1553_CHANNEL_A, 1U);
    step(1U);
    bus_model_advance_us(&bus, 10U);
    reply_data(MIL1553_CHANNEL_A, 2U);
    step(8U);
    TEST_ASSERT_EQUAL_UINT(0U, bc.error_log_count);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
}

// LLR-1553-BC-010
void test_llr_1553_bc_010_idle_major_minor_wait_minor_path(void)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 1U, 0U);
    TEST_ASSERT_TRUE(bc_init(&bc, &sched, &bus, MIL1553_CHANNEL_A, 3U));
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_IDLE, bc_get_state(&bc));
    TEST_ASSERT_TRUE(bc_start(&bc));
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MAJOR_FRAME, bc_get_state(&bc));
    step(1U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
    step(1U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_WAIT_RESPONSE, bc_get_state(&bc));
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    step(1U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
}

void test_llr_1553_bc_010_last_minor_frame_returns_to_major_frame(void)
{
    sched.minor_frame_period_us = 200U;
    (void)add_entry(MIL1553_MSG_BROADCAST, 31U, 1U, 1U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_MAJOR_FRAME, 1U)); // initial state after start
    step(2U);
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_MAJOR_FRAME, 400U)); // wraps after the period
}

void test_llr_1553_bc_010_timeout_failover_retry_wait_timeout_minor_path(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_TIMEOUT, 40U));
    step(1U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_FAILOVER, bc_get_state(&bc));
    step(1U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_WAIT_RESPONSE, bc_get_state(&bc)); // retry
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_TIMEOUT, 40U));
    step(1U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc)); // retry used: give up on this RT
}

void test_llr_1553_bc_010_stop_returns_to_idle_from_every_state(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    start_bc();
    bc_stop(&bc); TEST_ASSERT_EQUAL(MIL1553_BC_STATE_IDLE, bc_get_state(&bc)); // from MAJOR_FRAME
    TEST_ASSERT_TRUE(bc_start(&bc)); step(1U);
    bc_stop(&bc); TEST_ASSERT_EQUAL(MIL1553_BC_STATE_IDLE, bc_get_state(&bc)); // from MINOR_FRAME
    TEST_ASSERT_TRUE(bc_start(&bc)); step(2U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_WAIT_RESPONSE, bc_get_state(&bc));
    bc_stop(&bc); TEST_ASSERT_EQUAL(MIL1553_BC_STATE_IDLE, bc_get_state(&bc)); // from WAIT_RESPONSE
    TEST_ASSERT_TRUE(bc_start(&bc)); TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_TIMEOUT, 80U));
    bc_stop(&bc); TEST_ASSERT_EQUAL(MIL1553_BC_STATE_IDLE, bc_get_state(&bc)); // from TIMEOUT
}

void test_llr_1553_bc_010_run_frame_in_idle_does_nothing_and_reports_false(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    TEST_ASSERT_TRUE(bc_init(&bc, &sched, &bus, MIL1553_CHANNEL_A, 3U));
    TEST_ASSERT_FALSE(bc_run_frame(&bc));
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_IDLE, bc_get_state(&bc));
}

void test_llr_1553_bc_010_start_outside_idle_is_ignored_and_counted(void)
{
    mil1553_bc_health_t h;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    start_bc();
    TEST_ASSERT_FALSE(bc_start(&bc));
    TEST_ASSERT_FALSE(bc_start(&bc));
    bc_get_health(&bc, &h);
    TEST_ASSERT_EQUAL_UINT32(2U, h.ignored_events);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MAJOR_FRAME, bc_get_state(&bc));
}

void test_llr_1553_bc_010_unsolicited_words_are_ignored_and_counted(void)
{
    mil1553_symbols_t s; mil1553_bc_health_t h;
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 1U, 0U);
    start_bc();
    ts_data(s, 1U);
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT2, s);
    (void)ts_tx(&bus, MIL1553_CHANNEL_B, MIL1553_TERMINAL_RT2, s);
    step(3U); // the BC issues its message and discards the strays first
    bc_get_health(&bc, &h);
    TEST_ASSERT_EQUAL_UINT32(2U, h.ignored_events);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_WAIT_RESPONSE, bc_get_state(&bc));
}

// LLR-1553-REDUN-001
void test_llr_1553_redun_001_a_failed_message_is_retried_once_on_the_other_bus(void)
{
    sw_t a[40], b[40]; unsigned na, nb;
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 2U, 0U);
    start_bc();
    step(120U);
    na = spy(MIL1553_CHANNEL_A, a, 40U);
    nb = spy(MIL1553_CHANNEL_B, b, 40U);
    TEST_ASSERT_EQUAL_UINT(3U, na); // cmd + 2 data, once
    TEST_ASSERT_EQUAL_UINT(3U, nb); // the same message, once, on B
    TEST_ASSERT_EQUAL_HEX16(a[0].field, b[0].field); // identical command word
}

void test_llr_1553_redun_001_no_retry_when_the_other_bus_is_failed(void)
{
    sw_t b[8];
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    bus_model_set_channel_failed(&bus, MIL1553_CHANNEL_B, true);
    start_bc();
    step(60U);
    TEST_ASSERT_EQUAL_UINT(0U, spy(MIL1553_CHANNEL_B, b, 8U));
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_NO_RESPONSE));
}

void test_llr_1553_redun_001_retry_result_becomes_the_message_result(void)
{
    mil1553_bc_health_t h;
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 1U, 0U);
    auto_b = true; // answers only on bus B
    start_bc();
    step(120U);
    bc_get_health(&bc, &h);
    TEST_ASSERT_FALSE(h.rt_failed[5]); // the retry succeeded: RT not failed
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_NO_RESPONSE));
}

void test_llr_1553_redun_001_no_third_attempt_after_a_failed_retry(void)
{
    sw_t a[40], b[40]; unsigned na, nb;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    start_bc();
    step(150U); // well past both timeouts, still in major frame 0
    na = spy(MIL1553_CHANNEL_A, a, 40U);
    nb = spy(MIL1553_CHANNEL_B, b, 40U);
    TEST_ASSERT_EQUAL_UINT(1U, na);
    TEST_ASSERT_EQUAL_UINT(1U, nb);
}

// LLR-1553-REDUN-002

// redundancy tests wait for events, never for a step count: every word the BC or the fake RT sends also advances
// the clock, so N steps is not N microseconds

static bool run_until_fail_count_a(uint8_t n, unsigned max)
{
    unsigned i;
    for (i = 0U; i < max; i++) { if (bc.consecutive_fail_count[0] == n) { return true; } step(1U); }
    return bc.consecutive_fail_count[0] == n;
}

static bool run_until_primary(mil1553_channel_t ch, unsigned max)
{
    unsigned i;
    for (i = 0U; i < max; i++) { if (bc.primary_bus == ch) { return true; } step(1U); }
    return bc.primary_bus == ch;
}

static void setup_single_rt_to_bc_a_dead(uint8_t threshold)
{
    sched.minor_frame_period_us = 200U;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    auto_a = false; auto_b = true; // A is dead, B works
    TEST_ASSERT_TRUE(bc_init(&bc, &sched, &bus, MIL1553_CHANNEL_A, threshold));
    TEST_ASSERT_TRUE(bc_start(&bc));
}

void test_llr_1553_redun_002_primary_bus_switches_when_the_threshold_is_reached(void)
{
    mil1553_bc_health_t h;

    setup_single_rt_to_bc_a_dead(3U);
    TEST_ASSERT_TRUE(run_until_fail_count_a(2U, 3000U)); // threshold - 1 failures on A
    bc_get_health(&bc, &h);
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_A, h.primary_bus);
    TEST_ASSERT_FALSE(h.degraded);

    TEST_ASSERT_TRUE(run_until_primary(MIL1553_CHANNEL_B, 3000U)); // the third failure reaches the threshold
    bc_get_health(&bc, &h);
    TEST_ASSERT_TRUE(h.degraded);
    TEST_ASSERT_EQUAL_UINT8(0U, bc.consecutive_fail_count[0]); // counters reset on the switch
    TEST_ASSERT_EQUAL_UINT8(0U, bc.consecutive_fail_count[1]);
}

void test_llr_1553_redun_002_degraded_indication_stays_after_the_bc_resumes(void)
{
    mil1553_bc_health_t h;
    setup_single_rt_to_bc_a_dead(3U);
    TEST_ASSERT_TRUE(run_until_primary(MIL1553_CHANNEL_B, 3000U));
    step(1500U); // plenty of normal running afterwards
    bc_get_health(&bc, &h);
    TEST_ASSERT_TRUE(h.degraded);
    TEST_ASSERT_NOT_EQUAL(MIL1553_BC_STATE_IDLE, bc_get_state(&bc));
}

void test_llr_1553_redun_002_a_success_on_a_bus_resets_that_buses_counter(void)
{
    unsigned i;
    setup_single_rt_to_bc_a_dead(3U);
    for (i = 0U; i < 4U; i++)
    {
        auto_a = false; // A fails (B answers) ...
        TEST_ASSERT_TRUE(run_until_fail_count_a(1U, 3000U));
        auto_a = true; // ... then A answers: its counter resets
        TEST_ASSERT_TRUE(run_until_fail_count_a(0U, 3000U));
    }
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_A, bc.primary_bus); // never three in a row: no switch
    TEST_ASSERT_FALSE(bc.degraded_indication);
}

void test_llr_1553_redun_002_custom_threshold_is_honoured(void)
{
    setup_single_rt_to_bc_a_dead(2U);
    TEST_ASSERT_TRUE(run_until_fail_count_a(1U, 3000U));
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_A, bc.primary_bus);
    TEST_ASSERT_TRUE(run_until_primary(MIL1553_CHANNEL_B, 3000U));
}

// LLR-1553-REDUN-003

void test_llr_1553_redun_003_failed_rt_is_flagged_and_its_other_messages_are_skipped(void)
{
    sw_t a[48], b[48]; unsigned na, nb, i; unsigned rt5_cmds = 0U, rt6_cmds = 0U;
    mil1553_bc_health_t h;

    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 200U);     // same RT: must be skipped
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 6U, 1U, 1U, 400U);     // different RT: still issued
    auto_a = false; auto_b = false;
    start_bc();
    step(120U);
    bc_get_health(&bc, &h);
    TEST_ASSERT_TRUE(h.rt_failed[5]);
    TEST_ASSERT_FALSE(h.rt_failed[6]);

    step(500U); // through the rest of this major frame
    na = spy(MIL1553_CHANNEL_A, a, 48U);
    nb = spy(MIL1553_CHANNEL_B, b, 48U);
    for (i = 0U; i < na; i++) { if (a[i].sender == MIL1553_TERMINAL_BC) { (as_cmd(&a[i]).rt_address == 5U) ? rt5_cmds++ : rt6_cmds++; } }
    for (i = 0U; i < nb; i++) { if (b[i].sender == MIL1553_TERMINAL_BC) { (as_cmd(&b[i]).rt_address == 5U) ? rt5_cmds++ : rt6_cmds++; } }
    TEST_ASSERT_EQUAL_UINT(2U, rt5_cmds); // first message + its one retry, nothing more
    TEST_ASSERT_TRUE(rt6_cmds >= 1U);
}

void test_llr_1553_redun_003_flags_clear_at_the_next_major_frame(void)
{
    mil1553_bc_health_t h;
    unsigned i;

    sched.minor_frame_period_us = 300U;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    start_bc();
    for (i = 0U; (i < 400U) && !bc.rt_failed_this_major_frame[5]; i++) { step(1U); }
    bc_get_health(&bc, &h);
    TEST_ASSERT_TRUE(h.rt_failed[5]); // failed in this major frame

    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_MAJOR_FRAME, 400U));
    TEST_ASSERT_TRUE(bc.rt_failed_this_major_frame[5]); // still set until the new major frame starts
    step(1U); // the call that starts the new major frame
    bc_get_health(&bc, &h);
    TEST_ASSERT_FALSE(h.rt_failed[5]);
}

// LLR-1553-ERR-001 / ERR-002 / ERR-003 / ERR-004 (BC side)

static mil1553_bit_cbit_context_t cbit_ut;

void test_llr_1553_err_001_a_corrupt_word_in_the_response_is_logged_by_type_and_channel_and_rejects_the_message(void)
{
    mil1553_symbols_t bad;
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 1U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    ts_status(bad, 5U, false, false, false, false, false);
    bad[4] = (uint8_t)(bad[4] ^ 0x03U);                              // parity cell swapped
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT2, bad);
    step(1U);
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_PARITY));
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_A, bc.error_log[0].channel);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_TIMEOUT, bc_get_state(&bc));   // the message was rejected
}

void test_llr_1553_err_001_each_kind_of_word_error_is_logged_as_that_kind(void)
{
    const uint8_t cell[3] = { 4U, 3U, 0U };
    const uint8_t mask[3] = { 0x03U, 0x80U, 0x80U };
    const mil1553_error_type_t want[3] = { MIL1553_ERR_PARITY, MIL1553_ERR_MANCHESTER, MIL1553_ERR_SYNC };
    unsigned i;
    for (i = 0U; i < 3U; i++)
    {
        mil1553_symbols_t bad;
        setUp();
        (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 1U, 0U);
        start_bc();
        TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
        ts_status(bad, 5U, false, false, false, false, false);
        bad[cell[i]] = (uint8_t)(bad[cell[i]] ^ mask[i]);
        (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT2, bad);
        step(1U);
        TEST_ASSERT_EQUAL_UINT(1U, count_errors(want[i]));
    }
}

static void expect_status_flag_reported_without_a_retry(bool busy, bool sf, bool tf, mil1553_error_type_t t)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 1U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, false, busy, sf, tf);
    step(1U);
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(t));
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));   // reported only: the message completed
}

void test_llr_1553_err_002_busy_is_reported_but_does_not_trigger_a_retry(void)
{
    expect_status_flag_reported_without_a_retry(true, false, false, MIL1553_ERR_STATUS_BUSY);
}

void test_llr_1553_err_002_subsystem_flag_is_reported_but_does_not_trigger_a_retry(void)
{
    expect_status_flag_reported_without_a_retry(false, true, false, MIL1553_ERR_STATUS_SF);
}

void test_llr_1553_err_002_terminal_flag_is_reported_but_does_not_trigger_a_retry(void)
{
    expect_status_flag_reported_without_a_retry(false, false, true, MIL1553_ERR_STATUS_TF);
}

void test_llr_1553_err_002_all_three_flags_together_give_three_reports(void)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 1U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, false, true, true, true);
    step(1U);
    TEST_ASSERT_EQUAL_UINT(3U, bc.error_log_count);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
}

void test_llr_1553_err_002_message_error_and_a_wrong_address_do_trigger_the_retry(void)
{
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 1U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, true, false, false, false);   // ME
    step(1U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_TIMEOUT, bc_get_state(&bc));
    step(1U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_FAILOVER, bc_get_state(&bc));

    setUp();
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 1U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 9U, false, false, false, false);  // wrong RT address
    step(2U);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_FAILOVER, bc_get_state(&bc));
}

void test_llr_1553_err_003_more_words_than_commanded_is_a_word_count_mismatch(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 2U, 2U, 0U);
    bit_cbit_init(&cbit_ut, 0U);
    TEST_ASSERT_TRUE(bc_init(&bc, &sched, &bus, MIL1553_CHANNEL_A, 3U));
    bc_attach_cbit(&bc, &cbit_ut);
    TEST_ASSERT_TRUE(bc_start(&bc));
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    reply_data(MIL1553_CHANNEL_A, 3U);                                 // three where two were commanded
    step(4U);
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_WORD_COUNT_MISMATCH));
    TEST_ASSERT_EQUAL_UINT32(1U, cbit_ut.word_count_violations);       // and CBIT's counter saw it
    TEST_ASSERT_NOT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
}

void test_llr_1553_err_003_fewer_words_also_reach_the_cbit_word_count_counter(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 2U, 3U, 0U);
    bit_cbit_init(&cbit_ut, 0U);
    TEST_ASSERT_TRUE(bc_init(&bc, &sched, &bus, MIL1553_CHANNEL_A, 3U));
    bc_attach_cbit(&bc, &cbit_ut);
    TEST_ASSERT_TRUE(bc_start(&bc));
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    reply_data(MIL1553_CHANNEL_A, 2U);
    step(40U);
    TEST_ASSERT_EQUAL_UINT32(1U, cbit_ut.word_count_violations);
}

void test_llr_1553_err_003_exactly_the_commanded_number_is_not_an_error(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 2U, 2U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    reply_data(MIL1553_CHANNEL_A, 2U);
    step(6U);
    TEST_ASSERT_EQUAL_UINT(0U, bc.error_log_count);
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
}

void test_llr_1553_err_003_a_stray_word_after_a_gap_is_not_part_of_the_message(void)
{
    mil1553_bc_health_t h;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 2U, 2U, 0U);
    start_bc();
    TEST_ASSERT_TRUE(step_until(MIL1553_BC_STATE_WAIT_RESPONSE, 10U));
    reply_status(MIL1553_CHANNEL_A, 5U, false, false, false, false);
    reply_data(MIL1553_CHANNEL_A, 2U);
    bus_model_advance_us(&bus, 10U);                                    // a proper gap, then something else
    reply_data(MIL1553_CHANNEL_A, 1U);
    step(4U);
    bc_get_health(&bc, &h);
    TEST_ASSERT_EQUAL_UINT(0U, bc.error_log_count);                     // no false count error
    TEST_ASSERT_EQUAL_UINT32(1U, h.ignored_events);                     // the stray was ignored and counted
    TEST_ASSERT_EQUAL(MIL1553_BC_STATE_MINOR_FRAME, bc_get_state(&bc));
}

void test_llr_1553_err_004_a_record_carries_time_terminal_channel_type_and_message_id(void)
{
    const mil1553_error_record_t *r;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);             // nobody answers
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 6U, 1U, 1U, 400U);
    sched.minor_frame_period_us = 1000U;
    start_bc();
    step(60U);
    TEST_ASSERT_TRUE(bc_error_log_count(&bc) >= 1U);
    r = bc_error_log_get(&bc, 0U);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(MIL1553_TERMINAL_BC, r->terminal);
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_A, r->channel);
    TEST_ASSERT_EQUAL(MIL1553_ERR_NO_RESPONSE, r->error_type);
    TEST_ASSERT_EQUAL_UINT32(0U, r->message_id);                        // schedule entry 0
    TEST_ASSERT_TRUE(r->timestamp_us >= (bc.message_start_time_us > 0U ? 14U : 0U));
    TEST_ASSERT_TRUE(r->timestamp_us <= bus_model_now_us(&bus));
}

void test_llr_1553_err_004_out_of_range_index_gives_null_and_the_view_is_read_only(void)
{
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);
    start_bc();
    step(60U);
    TEST_ASSERT_NULL(bc_error_log_get(&bc, bc_error_log_count(&bc)));
    TEST_ASSERT_NULL(bc_error_log_get(&bc, 255U));
    {
        const mil1553_error_record_t *r = bc_error_log_get(&bc, 0U);
        TEST_ASSERT_TRUE(r == &bc.error_log[0]);                        // a view of the log, not a copy to edit
    }
}

void test_llr_1553_err_004_a_full_log_keeps_the_earliest_records_and_counts_the_rest(void)
{
    mil1553_error_record_t first;
    unsigned guard = 0U;

    sched.minor_frame_period_us = 200U;
    (void)add_entry(MIL1553_MSG_RT_TO_BC, 5U, 1U, 1U, 0U);             // never answered: errors every major frame
    start_bc();
    while ((bc_error_log_count(&bc) < MIL1553_ERROR_LOG_CAPACITY) && (guard++ < 200000U)) { step(1U); }
    TEST_ASSERT_EQUAL_UINT8(MIL1553_ERROR_LOG_CAPACITY, bc_error_log_count(&bc));
    TEST_ASSERT_EQUAL_UINT32(0U, bc_error_log_overflow_count(&bc));
    first = *bc_error_log_get(&bc, 0U);

    guard = 0U;
    while ((bc_error_log_overflow_count(&bc) < 3U) && (guard++ < 200000U)) { step(1U); }
    TEST_ASSERT_EQUAL_UINT8(MIL1553_ERROR_LOG_CAPACITY, bc_error_log_count(&bc));     // capacity, no more
    TEST_ASSERT_TRUE(bc_error_log_overflow_count(&bc) >= 3U);
    TEST_ASSERT_EQUAL_UINT64(first.timestamp_us, bc_error_log_get(&bc, 0U)->timestamp_us);   // the earliest is still there
    TEST_ASSERT_EQUAL(first.error_type, bc_error_log_get(&bc, 0U)->error_type);
    TEST_ASSERT_NOT_NULL(bc_error_log_get(&bc, (uint8_t)(MIL1553_ERROR_LOG_CAPACITY - 1U)));
    TEST_ASSERT_NULL(bc_error_log_get(&bc, MIL1553_ERROR_LOG_CAPACITY));
}