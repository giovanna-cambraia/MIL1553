/*
    verifies: LLR-1553-BM-001, LLR-1553-BM-002, LLR-1553-BM-003 (test part)
    module: bm_monitor (with bus_model, mil1553_word, manchester)

    the inspection part of LLR-1553-BM-003 (bm_monitor can reach no transmit function) is checked_bm_isolation.sh, run by `make`

    fixture: the test plays the BC (sender BC) and RTs (sender RT1 / RT2) and advances the virtual clock between wods to set response times exactly 
*/

#include "test_support.h"
#include "bm_monitor.h"
#include "bc_scheduler.h"
#include "rt_service.h"

static mil1553_bus_model_context_t bus;
static mil1553_bm_context_t bm;

void setUp(void)
{
    bus_model_init(&bus);
    bm_init(&bm, &bus);
}
void tearDown(void) {}

// helpers

static void bc_cmd(mil1553_channel_t ch, uint8_t rt, mil1553_tr_t tr, uint8_t sub, uint8_t wc)
{
    mil1553_symbols_t s; ts_cmd(s, rt, tr, sub, wc);
    (void)ts_tx(&bus, ch, MIL1553_TERMINAL_BC, s);
}

static void bc_data(mil1553_channel_t ch, uint16_t v)
{
    mil1553_symbols_t s; ts_data(s, v);
    (void)ts_tx(&bus, ch, MIL1553_TERMINAL_BC, s);
}

static void rt_status(mil1553_terminal_id_t who, mil1553_channel_t ch, uint8_t rt)
{
    mil1553_symbols_t s; ts_status(s, rt, false, false, false, false, false);
    (void)ts_tx(&bus, ch, who, s);
}

static void rt_data(mil1553_terminal_id_t who, mil1553_channel_t ch, uint16_t v)
{
    mil1553_symbols_t t; ts_data(s, v);
    (void)ts_tx(&bus, ch, who, s);
}

static void gap(uint32_t us) { bus_model_advance_us(&bus, us); }

static void capture_and_rebuild(void)
{
    bm_run(&bm);
    bm_reconstruct_messages(&bm);
}

static const mil1553_bm_message_t *msg(uint16_t i)
{
    const mil1553_bm_message_t *m = bm_get_message(&bm, i);
    TEST_ASSERT_NOT_NULL(m);
    return m;
}

// LLR-1553-BM-001
void test_llr_1553_bm_001_captures_channel_time_sender_and_symbols_of_every_word(void)
{
    mil1553_symbols_t a, b;
    const mil1553_bm_capture_buffer_t *c;

    ts_data(a, 0x1111U);
    ts_status(b, 3U, false, false, false, false, false);
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, a); // t = 0
    (void)ts_tx(&bus, MIL1553_CHANNEL_B, MIL1553_TERMINAL_RT1, b); // t = 20
    bm_run(&bm);

    c = bm_get_captures(&bm);
    TEST_ASSERT_EQUAL_UINT16(2U, c->count);
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_A, c->records[0].channel);
    TEST_ASSERT_EQUAL_UINT64(0U, c->records[0].start_time_us);
    TEST_ASSERT_EQUAL(MIL1553_TERMINAL_BC, c->records[0].sender);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, c->records[0].symbols, 5);
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_B, c->records[1].channel);
    TEST_ASSERT_EQUAL_UINT64(20U, c->records[1].start_time_us);
    TEST_ASSERT_EQUAL(MIL1553_TERMINAL_RT1, c->records[1].sender);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, c->records[1].symbols, 5);
    TEST_ASSERT_EQUAL(MIL1553_WORD_VALID, c->records[0].classification);
}

void test_llr_1553_bm_001_invalid_words_are_captured_with_their_error_class(void)
{
    mil1553_symbols_t parity, manch, sync;
    const mil1553_bm_capture_buffer_t *c;

    ts_data(parity, 7U); parity[4] = (uint8_t)(parity[4] ^ 0x03U); // swap the parity cell
    ts_data(manch, 7U); manch[3] = (uint8_t)(manch[3] ^0x80U); // break one data cell
    ts_data(sync, 7U); sync[0] = *uint8_t(sync[0] ^ 0x080U); // corrupt the sync
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, parity);
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, manch);
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, sync);
    bm_run(&bm);

    c = bm_get_captures(&bm);
    TEST_ASSERT_EQUAL_UINT16(3U, c->count);
    TEST_ASSERT_EQUAL(MIL1553_WORD_PARITY_ERROR, c->records[0].classification);
    TEST_ASSERT_EQUAL(MIL1553_WORD_MANCHESTER_ERROR, c->records[1].classification);
    TEST_ASSERT_EQUAL(MIL1553_WORD_SYNC_ERROR, c->records[2].classification);
}

void test_llr_1553_bm_001_watching_does_not_take_words_away_from_the_real_recipents(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r;
    ts_data(s, 0x42U);
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, s);
    bm_run(&bm);
    TEST_ASSERT_EQUAL_UINT16(1U, bm_get_captures(&bm)->count);
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r)); // still there for RT1
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT2, &r));
}

void test_llr_1553_bm_001_each_run_captures_only_what_is_new(void)
{
    mil1553_symbols_t s;
    ts_data(s, 1U);
    bm_run(&bm);
    TEST_ASSERT_EQUAL_UINT16(0U, bm_get_captures(&bm)->count);
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, s);
    bm_run(&bm); 
    bm_run(&bm); // second run: nothing new
    TEST_ASSERT_EQUAL_UINT16(1U, bm_get_captures(&bm)->count);
    (void)ts_tx(&bus, MIL1553_CHANNEL_B, MIL1553_TERMINAL_BC, s);
    bm_run(&bm);
    TEST_ASSERT_EQUAL_UINT16(2U, bm_get_captures(&bm)->count);
}

void test_llr_1553_bm_001_when_the_buffer_is_full_new_words_are_dropped_and_counted(void)
{
    mil1553_symbols_t s; unsigned i, batch;
    mil1553_bm_stats_t st;
    const mil1553_bm_capture_buffer_t *c;

    for (i = 0U; i < 300U; i += 50U) // 300 words, drained in batches of 50
    {
        for (batch = 0U; batch < 50U; batch++) { ts_data(s, (uint16_t)(i + batch)); (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, s); }
        bm_run(&bm);
    }
    bm_get_stats(&bm, &st);
    c = bm_get_captures(&bm);
    TEST_ASSERT_EQUAL_UINT16(BM_CAPTURE_CAPACITY, st.captured_count);
    TEST_ASSERT_EQUAL_UINT32(300U - BM_CAPTURE_CAPACITY, st.dropped_count);
    TEST_ASSERT_EQUAL_UINT64(0U, c->records[0].start_time_us); // the earliest words are the ones kept
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(BM_CAPTURE_CAPACITY - 1U) * MIL1553_WORD_TIME_US, c->records[BM_CAPTURE_CAPACITY - 1U].start_time_us);
}

// LLR-1553-BM-002
void test_llr_1553_bm_002_bc_to_rt_response_time_runs_from_the_last_data_word(void)
{
    const mil1553_bm_message_t *m;
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_RECEIVE, 1U, 2U); // 0..20
    bc_data(MIL1553_CHANNEL_A, 1U); // 20..40
    bc_data(MIL1553_CHANNEL_A, 2U); // 40..60
    gap(8U);
    rt_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U); // starts at 68
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(1U, bm_get_message_count(&bm));
    m = msg(0U);

    TEST_ASSERT_EQUAL(MIL1553_MSG_BC_TO_RT, m->msg_type);
    TEST_ASSERT_EQUAL_UINT8(5U, m->rt_address);
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_A, m->channel);
    TEST_ASSERT_TRUE(m->response_seen);
    TEST_ASSERT_EQUAL_UINT64(60U, m->command_end_time_us);
    TEST_ASSERT_EQUAL_UINT64(68U, m->response_start_time_us);
    TEST_ASSERT_EQUAL_UINT32(8U, m->response_time_us);
}

void test_llr_1553_bm_002_rt_to_bc_response_time_runs_from_the_command_and_ignores_rt_data_words(void)
{
    const mil1553_bm_message_t *m;
    bc_md(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 1U, 3U); // 0..20
    gap(10U);
    rt_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U); // 30
    rt_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 1U);
    rt_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 2U);
    rt_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 3U);
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(1U, bm_get_message_count(&bm));
    m = msg(0U);
    TEST_ASSERT_EQUAL(MIL1553_MSG_RT_TO_BC, m->msg_type);
    TEST_ASSERT_EQUAL_UINT32(10U, m->response_time_us);
}

void test_llr_1553_bm_002_rt_to_rt_pair_is_one_message_timed_from_the_second_command(void)
{
    const mil1553_bm_message_t *m;
    bc_cmd(MIL1553_CHANNEL_A, 1U, MIL1553_TR_RECEIVE, 1U, 2U); // 0..20 receive command
    bc_cmd(MIL1553_CHANNEL_A, 2U, MIL1553_TR_TRANSMIT, 2U, 2U); // 20..40 transmit command
    gap(9U);
    rt_status(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 2U); // transmitting RT answers at 49
    rt_data(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 1U);
    rt_data(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 2U); 
    gap(8U);
    rt_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 1U); // receiving RT's status
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(1U, bm_get_message_count(&bm));
    m = msg(0U);
    TEST_ASSERT_EQUAL(MIL1553_MSG_RT_TO_RT, m->msg_type);
    TEST_ASSERT_EQUAL_UINT8(1U, m->rt_address); // receiving RT
    TEST_ASSERT_EQUAL_UINT8(2U, m->rt_address_2); // transmitting RT
    TEST_ASSERT_EQUAL_UINT64(40U, m->command_end_time_us);
    TEST_ASSERT_EQUAL_UINT32(9U, m->response_time_us); // first status: the transmitting RT's
}

void test_llr_1553_bm_002_mode_commands_on_either_mode_subaddress_are_mode_code_messages(void)
{
     bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 0U, 2U);
    gap(8U);
    rt_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U);
    gap(10U);
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 31U, 2U);
    gap(8U);
    rt_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U);
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(2U, bm_get_message_count(&bm));
    TEST_ASSERT_EQUAL(MIL1553_MSG_MODE_CODE, msg(0U)->msg_type);
    TEST_ASSERT_EQUAL(MIL1553_MSG_MODE_CODE, msg(1U)->msg_type);
}

void test_llr_1553_bm_002_broadcast_is_recognised_and_has_no_response(void)
{
    const mil1553_bm_message_t *m;
    bc_cmd(MIL1553_CHANNEL_A, 31U, MIL1553_TR_RECEIVE, 1U, 1U);
    bc_data(MIL1553_CHANNEL_A, 9U);
    capture_and_rebuild();
    TEST_ASSERT_EQUAL_UINT16(1U, bm_get_message_count(&bm));
    m = msg(0U);
    TEST_ASSERT_EQUAL(MIL1553_MSG_BROADCAST, m->msg_type);
    TEST_ASSERT_FALSE(m->response_seen);
}

void test_llr_1553_bm_002_a_broadcast_mode_command_is_not_merged_with_the_next_message(void)
{
    // broadcast mode command (no data), a normal 4 us gap, then an ordinary RT-TO-BC message
    bc_cmd(MIL1553_CHANNEL_A, 31U, MIL1553_TR_TRANSMIT, 0U, 1U);
    gap(4U);
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 1U, 1U);
    gap(8U);
    rt_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U);
    rt_data(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 1U);
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(2U, bm_get_message_count(&bm));
    TEST_ASSERT_EQUAL(MIL1553_MSG_BROADCAST, msg(0U)->msg_type);
    TEST_ASSERT_EQUAL(MIL1553_MSG_RT_TO_BC, msg(1U)->msg_type);
    TEST_ASSERT_EQUAL_UINT32(8U, msg(1U)->response_time_us);
}

void test_llr_1553_bm_002_a_broadcast_or_mode_receive_command_is_never_the_first_half_of_an_rt_to_rt(void)
{
    // even when a transmit command follows at once, a broadcast / mode command is not an RT-to-RT start
    bc_cmd(MIL1553_CHANNEL_A, 31U, MIL1553_TR_RECEIVE, 1U, 1U); // broadcast, receive direction 
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 1U, 1U); // immediately followed ... 
    gap(10U);
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_RECEIVE, 0U, 17U); // mode command (sync with data)
    bc_cmd(MIL1553_CHANNEL_A, 6U, MIL1553_TR_TRANSMIT, 1U, 1U); // ... likewise
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(4U, bm_get_message_count(&bm));
    TEST_ASSERT_EQUAL(MIL1553_MSG_BROADCAST, msg(0U)->msg_type);
    TEST_ASSERT_EQUAL(MIL1553_MSG_RT_TO_BC, msg(1U)->msg_type);
    TEST_ASSERT_EQUAL(MIL1553_MSG_MODE_CODE, msg(2U)->msg_type);
    TEST_ASSERT_EQUAL(MIL1553_MSG_RT_TO_BC, msg(3U)->msg_type);
}

void test_llr_1553_bm_002_a_transmit_command_after_a_gap_does_not_complete_an_earlier_receive_command(void)
{
    // a receive command whose data words never appeared, then a normal gap, then a transmit command 
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_RECEIVE, 1U, 2U);
    gap(MIL1553_T_GAP_MIN_US);
    bc_cmd(MIL1553_CHANNEL_A, 6U, MIL1553_TR_TRANSMIT, 1U, 1U);
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(2U, bm_get_message_count(&bm));
    TEST_ASSERT_EQUAL(MIL1553_MSG_BC_TO_RT, msg(0U)->msg_type);
    TEST_ASSERT_EQUAL(MIL1553_MSG_RT_TO_BC, msg(1U)->msg_type);
}

void test_llr_1553_bm_002_two_contiguous_transmit_commands_are_not_an_rt_to_rt_pair(void)
{
    // RT-to-RT is a RECEIVE command then a TRANSMIT command; transmit + transmit is not that
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 1U, 1U);
    bc_cmd(MIL1553_CHANNEL_A, 6U, MIL1553_TR_TRANSMIT, 1U, 1U);
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(2U, bm_get_message_count(&bm));
    TEST_ASSERT_EQUAL(MIL1553_MSG_RT_TO_BC, msg(0U)->msg_type);
    TEST_ASSERT_EQUAL(MIL1553_MSG_RT_TO_BC, msg(1U)->msg_type);
}

void test_llr_1553_bm_002_a_pair_naming_the_same_rt_twice_is_not_rt_to_rt(void)
{
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_RECEIVE, 1U, 2U);
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 1U, 2U); // contiguous, but the same RT
    capture_and_rebuild();
    TEST_ASSERT_EQUAL_UIN16(2U, bm_get_message_count(&bm));
}

void test_llr_1553_bm_002_two_mode_commands_separated_by_a_gap_are_two_messages(void)
{
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 0U, 2U);
    gap(8U);
    rt_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U); 
    gap(4U);
    bc_cmd(MIL1553_CHANNEL_A, 6U, MIL1553_TR_TRANSMIT, 0U, 2U); // transmit-direction, but a new message
    gap(8U);
    rt_status(MIL1553_TERMINAL_RT2, MIL1553_CHANNEL_A, 6U);
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(2U, bm_get_message_count(&bm));
    TEST_ASSERT_EQUAL(MIL1553_MSG_MODE_CODE, msg(1U)->msg_type);
}

void test_llr_1553_bm_002_an_unanswered_command_has_no_response(void)
{
    const mil1553_bm_message_t *m;
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_RECEIVE, 1U, 1U);
    bc_data(MIL1553_CHANNEL_A, 1U);
    gap(30U);
    bc_cmd(MIL1553_CHANNEL_A, 6U, MIL1553_TR_RECEIVE, 1U, 1U); // the next message starts, no status seen 
    bc_data(MIL1553_CHANNEL_A, 1U);
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(2U, bm_get_message_count(&bm));
    m = msg(0U);
    TEST_ASSERT_FALSE(m->response_seen);
    TEST_ASSERT_EQUAL_UINT32(0U, m->response_start_time_us);
}

void test_llr_1553_bm_002_response_times_are_exact_at_the_window_edges(void)
{
    const uint32_t d[] = { 4U, 12U, 20U };
    unsigned i;
    for (i = 0U; i < 3U; i++)
    {
        setUp();
        bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 1U, 1U);
        gap(d[i]);
        rt_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U);
        capture_and_rebuild();

        TEST_ASSERT_EQUAL_UINT32(d[i], msg(0U)->response_time_us);
    }
}

void test_llr_1553_bm_002_a_retry_on_the_other_bus_is_a_separate_message_on_that_channel(void)
{
    const mil1553_bm_message_t *a, *b;
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 1U, 1U); // no answer on A
    gap(30U);
    bc_cmd(MIL1553_CHANNEL_B, 5U, MIL1553_TR_TRANSMIT, 1U, 1U); // retry on B
    gap(8U);
    rt_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_B, 5U);
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(2U, bm_get_message_count(&bm));
    a = msg(0U); b = msg(1U);
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_A, a->channel);
    TEST_ASSERT_FALSE(a->response_seen);
    TEST_ASSERT_EQUAL(MIL1553_CHANNEL_B, b->channel);
    TEST_ASSERT_TRUE(b->response_seen);
}

void test_llr_1553_bm_002_a_corrupt_command_starts_no_message_and_its_data_makes_none(void)
{
    mil1553_symbols_t bad;
    ts_cmd(bad, 5U, MIL1553_TR_RECEIVE, 1U, 2U);
    bad[4] = (uint8_t)(bad[4] ^ 0x03U); // wrong parity
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, bad);
    bc_data(MIL1553_CHANNEL_A, 1U);
    bc_data(MIL1553_CHANNEL_A, 2U);
    capture_and_rebuild();
    
    TEST_ASSERT_EQUAL_UINT16(3U, bm_get_captures(&bm)->count); // all three words were captured ...
    TEST_ASSERT_EQUAL_UINT16(0U, bm_get_message_count(&bm)); // ... but none is a message
}

void test_llr_1553_bm_002_a_status_word_with_a_transmission_error_is_not_a_response(void)
{
    mil1553_symbols_t bad;
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 1U, 1U);
    gap(8U);
    ts_status(bad, 5U, false, false, false, false, false);
    bad[4] = (uint8_t)(bad[4] ^ 0x03U);
    (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, bad);
    capture_and_rebuild();

    TEST_ASSERT_FALSE(msg(0U)->response_seen);
}

void test_llr_1553_bm_002_rebuilding_is_repeatable_and_keeps_new_words(void);
{
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_CHANNEL_A, 1U, 1U);
    gap(8U);
    rt_status(MIL1553_TERMINAL_RT1, MIL1553_CHANNEL_A, 5U);
    capture_and_rebuild();

    bm_reconstruct_messages(&bm);
    bm_reconstruct_messages(&bm);
    TEST_ASSERT_EQUAL_UINT16(1U, bm_get_message_count(&bm)); // not 3

    gap(10U);
    bc_cmd(MIL1553_CHANNEL_A, 6U, MIL1553_TR_TRANSMIT, 1U, 1U);
    capture_and_rebuild();

    TEST_ASSERT_EQUAL_UINT16(2U, bm_get_message_count(&bm));
}

void test_llr_1553_bm_002_message_list_is_bounded_and_counts_what_did_not_fit(void)
{
    mil1553_bm_stats_t st; unsigned i;
    for (i = 0U; i < (BM_MESSAGE_CAPACITY + 2U); i++)
    {
        bm_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 0U, 1U);
        gap(4U);
        ig ((i % 40U) == 39U) { bm_run(&bm); } // keep the bus ring drained
    }
    capture_and_rebuild();

    bm_get_stats(&bm, &st);
    TEST_ASSERT_EQUAL_UINT16(BM_CAPTURE_CAPACITY, st.message_count);
    TEST_ASSERT_EQUAL_UINT32(2U, st.message_overflow_count);
}

void test_llr_1553_bm_002_out_of_range_message_gives_null(void)
{
    TEST_ASSERT_NULL(bm_get_message_count(&bm, 0U));
    bc_cmd(MIL1553_CHANNEL_A, 5U, MIL1553_TR_TRANSMIT, 1U, 1U);
    capture_and_rebuild();

    TEST_ASSERT_NOT_NULL(bm_get_message(&bm, 0U));
    TEST_ASSERT_NULL(bm_get_message(&bm, 1U));
}

// LLR-1553-BM-003 (test part)
static mil1553_bus_model_context_t sc_bus;
static mil1553_bc_context_t sc_bc;
static mil1553_rt_context_t sc_rt1, sc_rt2;
static mil1553_bm_context_t sc_bm;
static mil1553_bc_schedule_t sc_sched;

static void run_scenario(bool with_bm)
{
    mil1553_rt_subaddr_config_t cfg[RT_SUBADDRESS_COUNT];
    mil1553_bc_schedule_entry_t *e;
    int k;

    (void)memset(cfg, 0, sizeof(cfg));
    cfg[1].enabled = true; cfg[1].allow_receive = true; cfg[1].allow_transmit = true;
    cfg[1].min_word_count = 1U; cfg[1].max_word_count = 32U;
    cfg[2] = cfg[1];
    (void)memset(&sc_sched, 0, sizeof(sc_sched));
    sc_sched.minor_frame_count = 1U; sc_sched.minor_frame_period_us = 1000U; sc_sched.entry_count = 3U;
    e = &sc_sched.entries[0]; e->msg_type = MIL1553_MSG_BC_TO_RT; e->rt_address = 1U; e->subaddress = 1U; e->data_word_count = 2U; e->slot_offset_us = 0U;
    e = &sc_sched.entries[1]; e->msg_type = MIL1553_MSG_RT_TO_BC; e->rt_address = 2U; e->subaddress = 2U; e->data_word_count = 2U; e->slot_offset_us = 300U;
    e = &sc_sched.entries[2]; e->msg_type = MIL1553_MSG_RT_TO_RT; e->rt_address = 1U; e->subaddress = 1U; e->rt_address_2 = 2U; e->subaddress_2 = 2U; e->data_word_count = 2U; e->slot_offset_us = 600U;

    bus_model_init(&sc_bus);
    TEST_ASSERT_TRUE(bc_init(&sc_bc, &sc_sched, &sc_bus, MIL1553_CHANNEL_A, 3U));
    TEST_ASSERT_TRUE(rt_init(&sc_rt1, 1U, MIL1553_TERMINAL_RT1, cfg, 8U, &sc_bus));
    TEST_ASSERT_TRUE(rt_init(&sc_rt2, 2U, MIL1553_TERMINAL_RT2, cfg, 8U, &sc_bus));
    bm_init(&sc_bm, &sc_bus);
    rt_pbit_complete(&sc_rt1, true); (void)rt_enable(&sc_rt1);
    rt_pbit_complete(&sc_rt2, true); (void)rt_enable(&sc_rt2);
    (void)bc_start(&sc_bc);

    while (bus_model_now_us(&sc_bus) < 900U) // one major frame: before the BC starts the next one 
    {
        for (k = 0; k < 4; k++)
        {
            (void)bc_run_frame(&sc_bc);
            (void)rt_run(&sc_rt1);
            (void)rt_run(&sc_rt2);
            if (with_bm) { bm_run(&sc_bm); }
        }
        bus_model_advance_us(&sc_bus, 1U);
    }
}

static mil1553_bus_record_t snap[2][BUS_MODEL_QUEUE_CAPACITY];
static uint32_t snap_total[2];
static uint64_t snap_clock;

static void take_snapshot(void)
{
    unsigned c, i;
    for (c = 0U; c < 2U; c++)
    {
        snap_total[c] = sc_bus.channel[c].total_written;
        for (i = 0U; i < BUS_MODEL_QUEUE_CAPACITY; i++) { snap[c][i] = sc_bus.channel[c].records[i]; }
    }
    snap_clock = sc_bus.virtual_clock_us;
}

void test_llr_1553_bm_003_traffic_is_identical_with_and_without_a_bus_monitor(void)
{
    static mil1553_bus_record_t without[2][BUS_MODEL_QUEUE_CAPACITY];
    uint32_t total_without[2]; uint64_t clock_without;
    unsigned c, i;

    run_scenario(false);
    take_snapshot();
    (void)memcpy(without, snap, sizeof(without)); total_without[0] = snap_total[0]; total_without[1] = snap_total[1]; clock_without = snap_clock;
    TEST_ASSERT_TRUE(total_without[0] > 10U); // the scenario really put traffic on the bus 

    run_scenario(true);
    take_snapshot();
    TEST_ASSERT_TRUE(bm_get_captures(&sc_bm)->count > 10U); // and the BM really watched it 
    TEST_ASSERT_EQUAL_UINT64(clock_without, snap_clock);
    for (c = 0U; c < 2U; c++)
    {
        TEST_ASSERT_EQUAL_UINT32(total_without[c], snap_total[c]);
         for (i = 0U; i < BUS_MODEL_QUEUE_CAPACITY; i++)
        {
            TEST_ASSERT_EQUAL_UINT64(without[c][i].timestamp_us, snap[c][i].timestamp_us);
            TEST_ASSERT_EQUAL(without[c][i].sender, snap[c][i].sender);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(without[c][i].symbols, snap[c][i].symbols, 5);
        }
    }
}

void test_llr_1553_bm_003_the_real_scenario_is_reconstructed_as_three_messages_with_legal_response_times(void)
{
    uint16_t i;
    run_scenario(true);
    bm_reconstruct_messages(&sc_bm);
    TEST_ASSERT_EQUAL_UINT16(3U, bm_get_message_count(&sc_bm));
    for (i = 0U; i < 3U; i++)
    {
        const mil1553_bm_message_t *m = bm_get_message(&sc_bm, i);
        TEST_ASSERT_TRUE(m->response_seen);
        TEST_ASSERT_TRUE(m->response_time_us >= MIL1553_T_RESP_MIN_US);
        TEST_ASSERT_TRUE(m->response_time_us <= MIL1553_T_RESP_MAX_US);
    }
    TEST_ASSERT_EQUAL(MIL1553_MSG_BC_TO_RT, bm_get_message(&sc_bm, 0U)->msg_type);
    TEST_ASSERT_EQUAL(MIL1553_MSG_RT_TO_BC, bm_get_message(&sc_bm, 1U)->msg_type);
    TEST_ASSERT_EQUAL(MIL1553_MSG_RT_TO_RT, bm_get_message(&sc_bm, 2U)->msg_type);
}