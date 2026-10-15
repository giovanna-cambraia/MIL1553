/*
    verifies: LLR-1553-BUS-001, LLR-1553-BUS-003, and the in-process part of LLR-1553-BUS-002 (record fidelity)

    NOT covered here: the wire serialization and version/layout rejection of LLR-1553-BUS-002 (design/bus_transport_contract.md 3); that behavior is for the peer-process
    topology and is not implemented in the C sources yet, so there is nothing to test; it stays an open item
*/

#include "test_support.h"

static mil1553_bus_model_t bus;

void setUp(void) { bus_model_init(&bus); }
void tearDown(void) {}

// LLR-1553-BUS-001
void test_llr_1553_bus_001_word_reaches_every_other_listener_unchanged(void)
{
    mil1553_symbols_t s, got_sym;
    mil1553_bus_record_t r;
    const mil1553_terminal_id_t listeners[3] = { MIL1553_TERMINAL_BC, MIL1553_TERMINAL_RT2, MIL1553_TERMINAL_BM };
    unsigned i;

    ts_data(s, 0xC0DEU);
    TEST_ASSERT_TRUE(bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_RT1));

    for (i = 0U; i < 3U; i++)
    {
        TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, listeners[i], &r));
        (void)memcpy(got_sym, r.symbols, 5U);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(s, got_sym, 5);
        TEST_ASSERT_EQUAL(MIL1553_TERMINAL_RT1, r.sender);
        TEST_ASSERT_FALSE(ts_rx(&bus, MIL1553_CHANNEL_A, listeners[i], &r)); // each gets it once
    }
}

void test_llr_1553_bus_001_sender_does_not_receive_its_own_word(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r;
    ts_data(s, 1U);
    (void)bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_BC);
    TEST_ASSERT_FALSE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, &r)); 
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r));
}

void test_llr_1553_bus_001_one_listener_reading_does_not_consume_for_others(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r;
    ts_data(s, 7U);
    (void)bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_BC);
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_B, MIL1553_TERMINAL_BM, &r));
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_B, MIL1553_TERMINAL_RT1, &r));
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_B, MIL1553_TERMINAL_RT2, &r));
}

void test_llr_1553_bus_001_words_arrive_in_transmission_order(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r; uint16_t field; unsigned i;
    for (i = 0U; i < 5U; i++) { ts_data(s, (uint16_t)(100U + i)); (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, s); }
    for (i = 0U; i < 5U; i++)
    {
        TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r));
        (void)ts.decode(r.symbols, &field, (mil1553_sync_t *)0);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(100U + i), field);
    }
}

void test_llr_1553_bus_001_channels_are_independent(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r;
    ts_data(s, 5U);
    (void)bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_BC);
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_B, MIL1553_TERMINAL_RT1, &r));
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r));
}

void test_llr_1553_bus_001_failed_channel_delivers_nothing_and_accepts_nothing(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r;
    ts_data(s, 9U);
    bus_model_set_channel_failed(&bus, MIL1553_CHANNEL_A, true);
    TEST_ASSERT_TRUE(bus_model_is_channel_failed(&bus, MIL1553_CHANNEL_A));
    TEST_ASSERT_FALSE(bus_model_is_channel_failed(&bus, MIL1553_CHANNEL_B));
    TEST_ASSERT_FALSE(bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_BC));
    TEST_ASSERT_FALSE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r));
}

void test_llr_1553_bus_001_failing_a_channel_discards_backlog_and_restore_does_not_replay(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r;
    ts_data(s, 11U);
    (void)bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_BC); // unread by RT1
    bus_model_set_channel_failed(&bus, MIL1553_CHANNEL_A, true);
    bus_model_set_channel_failed(&bus, MIL1553_CHANNEL_A, false);
    TEST_ASSERT_FALSE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r));
    TEST_ASSERT_TRUE(bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_BC)); // works again
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r));
}

void test_llr_1553_bus_001_listener_that_falls_behind_the_ring_loses_oldest_and_counts_them(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r; unsigned i;
    const unsigned total = BUS_MODEL_QUEUE_CAPACITY + 6U;

    ts_data(s, 1U);
    for (i = 0U; i < total; i++) { (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, s); }

    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINl_RT1, &r));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)6U * MIL1553_WORD_TIME_US, r.timestamp_us); // 7th word onward
    TEST_ASSERT_EQUAL_UINT32(6U, bus_model_get_listener_dropped_count(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1));
    TEST_ASSERT_EQUAL_UINT32(0U, bus_model_get_listener_dropped_count(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BM));
}

void test_llr_1553_bus_001_loopback_read_sees_own_word_without_disturbing_normal_cursor(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r;
    ts_data(s, 0x77U);
    (void)bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_RT1);

    TEST_ASSERT_FALSE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r)); // normal read skips own 
    TEST_ASSERT_TRUE(bus_model_receive_loopback(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, *r));
    TEST_ASSERT_EQUAL(MIL1553_TERMINAL_RT1, r.sender);
    TEST_ASSERT_FALSE(bus_model_receive_loopback(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r));
    TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, &r)); // others unaffected 
}

void test_llr_1553_bus_001_loopback_on_failed_channel_returns_nothing(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r;
    ts_data(s, 1U);
    (void)bus_model_set_channel_failed(&bus, MIL1553_CHANNEL_B, true);
    bus_model_set_channel_failed(&bus, MIL1553_CHANNEL_B, true);
    TEST_ASSERT_FALSE(bus_model_receive_loopback(&bus, MIL1553_CHANNEL_B, MIL1553_TERMINAL_RT1, &r));
}

// LLR-1553-BUS-002 (in-process part)
void test_lle_1553_bus_002_record_carries_timestamp_sender_and_symbols(void)
{
    mil1553_symbols_t s; mil1553_bus_record_t r;
    ts_cmd(s, 3U, MIL1553_TR_RECEIVE, 4U, 5U);
    bus_model_advance_us(&bus, 123U);
    (void)bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_BC);

    TEST_ASSERT_TRUE(ts_rc(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BM, &r));
    TEST_ASSERT_EQUAL_UINT64(123U, r.timestamp_us);
    TEST_ASSERT_EQUAL(MIL1553_TERMINAL_BC, r.sender);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(s, r.symbols, 5);
}

// LLR-1553-BUS-003
void test_llr_1553_bus_003_clock_starts_at_zero_and_moves_only_when_advanced(void)
{
    mil1553_symbols_t s;
    TEST_ASSERT_EQUAL_UINT64(0U, bus_model_now_us(&bus));
    ts_data(s, 1U);
    (void)bus_model_transmit(&bus, MIL1553_CHANNEL_A, s, MIL1553_TERMINAL_BC);
    TEST_ASSERT_EQUAL_UINT64(0U, bus_model_now_us(&bus)); // transmit alone does not move time
    bus_model_advance_us(&bus, 20U);
    bus_model_advance_us(&bus, 5U);
    TEST_ASSERT_EQUAL_UINT64(25U, bus_model_now_us(&bus));
}

void_test_llr_1553_bus_003_identical_runs_give_identical_timestamps(void)
{
    mil1553_bus_model_context_t other;
    mil1553_symbols_t s; mil1553_bus_record_t r1, r2; unsigned i;

    bus_model_init(&other);
    ts_data(s, 3U);
    for (i = 0U; i < 10U; i++)
    {
        (void)ts_tx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, s);
        (void)ts_tx(&other, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, s);
        bus_model_advance_us(&bus, (mil1553_time_us_t)(i + 1U));
        bus_model_advance_us(&other, (mil1553_time_us_t)(i + 1U));
    }
    for (i = 0U; i < 10U; i++)
    {
        TEST_ASSERT_TRUE(ts_rx(&bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r1));
        TEST_ASSERT_TRUE(ts_rx(&other, MIL1553_CHANNEL_A, MIL1553_TERMINAL_RT1, &r2));
        TEST_ASSERT_EQUAL_UINT64(r1.timestamp_us, r2.timestamp_us);
    }
}