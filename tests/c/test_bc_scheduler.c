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

static bool auto_a, auto_b; // fake RT answer commands seen on this channel 

typedef struct {
    uint16_t field; mil1553_sync_t; mil1553_word_class_t cls;
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
    mil1553_bc_schedule_entry_t *e = &sched.entries(sched.entry_count);
    (void)memset(e, 0, sizeof(*e));
    e->msg_type = t; e->rt_address = rt; e->subaddress = sub;
    e->data_word_count = n; e->slot_offset_us = offset;
    sched.entry_count++;
    return e;
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

static void step(unsiged n)
{
    unsigned i;
    for (i = 0U; i < n; i++)
    {
        (void)bc_run_frame(&bc);
        fake_rt_poll();
        bus_model_advance_us(&bus, 1U);
    }
}

static bool step_until(mil1553_bc_state_t st, unsiged max)
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
        out[n].cls = ts.decode(r.symbols, &out[n].field, &out[n].sync);
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
    mil1553_bc_schedule_entry_t *e;

    e = add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 4U, 0U);
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
    (void)add_entry(MIL1553_MSG_BC_RT, 40U, 1U, 2U, 0U); // bad at address
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
    sw_t[8]; unsigned n;
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
    sw_t[16]; unsigned n, i, cmds = 0U;
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
    TEST_ASSERT_TRUE(cmds >= 3U); // frame0. frame1, then frame0 of the next major frame
    TEST_ASSERT_TRUE(t_frame1_cmd >= 500U); // frame-1 message waited for its own minor frame
}

void test_llr_1553_bc_002_message_still_running_at_frame_end_raises_one_overrun(void)
{
    sched.minor_frame_period_us = 100U;
    (void)add_entry(MIL1553_MSG_BC_TO_RT, 5U, 1U, 2U. 0U); // 96 us, nobody answers
    start_bc();
    step(200U);
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_MINOR_FRAME_OVERRUN));
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
    c = as_cmd(&w[0]);
    TEST_ASSERT_EQUAL(MIL1553_SYNC_COMMAND_STATUS, w[0].sync);
    TEST_ASSERT_EQUAL_UINT8(5U, c.rt_address);
    TEST_ASSERT_EQUAL_UINT8(MIL1553_TR_RECEIVE, c.tr_bit);
    TEST_ASSERT_EQUAL_UINT8(3U, c.subaddress);
    TEST_ASSERT_EQUAL_UINT8(3U, c.word_count_field);
    for (i = 1U; i < 4U; i++) { TEST_ASSERT_EQUAL(MIL1553_SYNC_DATA, w[i].sync); }
}

void test_llr_1553_bc_003_thiry_two_words_use_word_count_field_zero(void)
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
void test_llr_bc_004_rt_to_bc_sends_only_the_transmit_command(void)
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
    TEST_ASSERT_EQUAL_UINT(1U, count_errors(MIL1553_ERR_NO_WORD_COUNT_MISMATCH));
    TEST_ASSERT_EQUAL_UINT(0U, count_errors(MIL1553_ERR_NO_RESPONSE));
}

void test_llr_1553_bc_004_silence_after_the_command_is_no_response_not_a_count_error(void)
{
    (void)add_entry(MIL1553_RT_TO_BC, 5U, 2U, 3U, 0U);
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