/*
    project: MIL1553

    realizes: LLR-1553-BC-001 .. LLR-1553-BC-010, LLR-1553-REDUN-001 .. LLR-1553-REDUN-003

    design simplifications made at this stage (flagged for review at design baseline, SDP 2.2):
        - BC-supplied data words (BC-to-RT, mode code w/ mode_data_from_bc) have no real
        "mission computer" data source in this project's scope (PSAC 2.3 system boundary excludes
        higher-level mission software), a deterministic placeholder pattern (word index value)
        is transmitted instead; this is a stub, not a protocol shortcut - the word level format
        is fully real
        - bc_run_frame checks the no-response timeout only on polls that return no word (LLR-1553-BC-009
        is still met: timeout is declared no earlier than T_NORESP_US and is checked every call while no
        word has arrived, so detection latency is bounded by call frequency, not missed)
        - channel selection for a message's *first* attempt is always ctx->primary_bus (the live REDUN-002
        designation), not each entry's preferred_bus, preferred_bus is reserved for schedule validation
        grouping; this keeps "primary bus" a single BC-wide concept, matching LLR-1553-REDUN-002's per BC
        (not per message) failover
*/

#include "bc_scheduler.h"
#include "mil1553_word.h"

// internal helpers

static mil1553_time_us_t bc_now(const mil1553_bc_context_t *ctx)
{
    return bus_model_now_us(ctx->bus);
}

static mil1553_channel_t bc_alternate_channel(mil1553_channel_t channel)
{
    return (channel == MIL1553_CHANNEL_A) ? MIL1553_CHANNEL_B : MIL1553_CHANNEL_A;
}

static uint8_t bc_channel_index(mil1553_channel_t channel)
{
    return (uint8_t)((channel == MIL1553_CHANNEL_A) ? 0U : 1U);
}

// implements: LLR-1553-ERR-004 (BC side)
static void bc_log_error(mil1553_bc_context_t *ctx, mil1553_channel_t channel,
                         mil1553_error_type_t error_type, uint32_t message_id)
{
    if (ctx->error_log_count < MIL1553_ERROR_LOG_CAPACITY)
    {
        mil1553_error_record_t *rec = &ctx->error_log[ctx->error_log_count];

        rec->timestamp_us = bc_now(ctx);
        rec->terminal = MIL1553_TERMINAL_BC;
        rec->channel = channel;
        rec->error_type = error_type;
        rec->message_id = message_id;
        ctx->error_log_count++;
    }
    else
    {
        ctx->error_log_overflow_count++;
    }
}

static void bc_transmit_word(mil1553_bc_context_t *ctx, mil1553_channel_t channel, const mil1553_symbols_t symbols)
{
    (void)bus_model_transmit(ctx->bus, channel, symbols, MIL1553_TERMINAL_BC);
    bus_model_advance_us(ctx->bus, (mil1553_time_us_t)MIL1553_WORD_TIME_US);
}

// implements: LLR-1553-BC-001 (duration formula per timing_budget.md 4)
uint32_t bc_message_duration_us(const mil1553_bc_schedule_entry_t *entry)
{
    uint32_t duration;
    uint32_t n = (uint32_t)entry->data_word_count;

    switch (entry->msg_type)
    {
    case MIL1553_MSG_BC_TO_RT:
        // cmd + data(N) + resp_delay(max) + status + gap
        duration = MIL1553_WORD_TIME_US + (n * MIL1553_WORD_TIME_US) +
                   MIL1553_T_RESP_MAX_US + MIL1553_WORD_TIME_US + MIL1553_T_GAP_MIN_US;
        break;

    case MIL1553_MSG_RT_TO_BC:
        // cmd + resp_delay(max) + status + data(N) + gap
        duration = MIL1553_WORD_TIME_US + MIL1553_T_RESP_MAX_US +
                   MIL1553_WORD_TIME_US + (n * MIL1553_WORD_TIME_US) + MIL1553_T_GAP_MIN_US;
        break;

    case MIL1553_MSG_RT_TO_RT:
        // recv_cmd + xmit_cmd + resp_delay + status_xmit + data(N) + resp_delay + status_recv + gap
        duration = MIL1553_WORD_TIME_US + MIL1553_WORD_TIME_US +
                   MIL1553_T_RESP_MAX_US + MIL1553_WORD_TIME_US +
                   (n * MIL1553_WORD_TIME_US) + MIL1553_T_RESP_MAX_US + MIL1553_WORD_TIME_US +
                   MIL1553_T_GAP_MIN_US;
        break;

    case MIL1553_MSG_MODE_CODE:
        if (entry->has_data_word)
        {
            // mode_cmd + data(1) + resp_delay + status + gap
            duration = MIL1553_WORD_TIME_US + MIL1553_WORD_TIME_US +
                       MIL1553_T_RESP_MAX_US + MIL1553_WORD_TIME_US + MIL1553_T_GAP_MIN_US;
        }
        else
        {
            // mode_cmd + resp_delay + status + gap
            duration = MIL1553_WORD_TIME_US + MIL1553_T_RESP_MAX_US +
                       MIL1553_WORD_TIME_US + MIL1553_T_GAP_MIN_US;
        }
        break;

    case MIL1553_MSG_BROADCAST:
        // cmd + data (N) + gap, no status phase
        duration = MIL1553_WORD_TIME_US + (n * MIL1553_WORD_TIME_US) + MIL1553_T_GAP_MIN_US;
        break;

    default:
        duration = 0U;
        break;
    }

    return duration;
}

// mode codes this BC may schedule, per design/mode_code_table.md (LLR-1553-BC-006); kept as the BC's own
// copy, deliberately not shared with rt_service (SDD 1: no shared source); both are checked against the
// document at design baseline
typedef struct {
    bool supported;
    bool has_data_word;
    bool data_from_bc; // true: BC supplies the data word; false: RT returns it
} bc_mode_entry_t;

static const bc_mode_entry_t BC_MODE_TABLE[32] = {
    /*  0 */ { false, false, false }, // Dynamic Bus Control: recognized, unsupported
    /*  1 */ { true,  false, false }, /*  2 */ { true,  false, false },
    /*  3 */ { true,  false, false }, /*  4 */ { true,  false, false },
    /*  5 */ { true,  false, false }, /*  6 */ { true,  false, false },
    /*  7 */ { true,  false, false }, /*  8 */ { true,  false, false },
    /*  9 */ { false, false, false }, /* 10 */ { false, false, false },
    /* 11 */ { false, false, false }, /* 12 */ { false, false, false },
    /* 13 */ { false, false, false }, /* 14 */ { false, false, false },
    /* 15 */ { false, false, false },
    /* 16 */ { true,  true,  false }, /* 17 */ { true,  true,  true  },
    /* 18 */ { true,  true,  false }, /* 19 */ { true,  true,  false },
    /* 20 */ { false, false, false }, /* 21 */ { false, false, false },
    /* 22 */ { false, false, false }, /* 23 */ { false, false, false },
    /* 24 */ { false, false, false }, /* 25 */ { false, false, false },
    /* 26 */ { false, false, false }, /* 27 */ { false, false, false },
    /* 28 */ { false, false, false }, /* 29 */ { false, false, false },
    /* 30 */ { false, false, false }, /* 31 */ { false, false, false }
};

// implements: LLR-1553-BC-001, LLR-1553-BC-006
bool bc_validate_schedule(const mil1553_bc_schedule_t *schedule)
{
    uint8_t i;
    uint8_t j;

    if ((schedule->entry_count > BC_MAX_SCHEDULE_ENTRIES) ||
        (schedule->minor_frame_count == 0U) ||
        (schedule->minor_frame_count > BC_MAX_MINOR_FRAMES) ||
        (schedule->minor_frame_period_us == 0U))
    {
        return false;
    }

    for (i = 0U; i < schedule->entry_count; i++)
    {
        const mil1553_bc_schedule_entry_t *e = &schedule->entries[i];
        uint32_t duration;

        if ((e->rt_address > MIL1553_RT_ADDR_BROADCAST) ||
            (e->minor_frame_index >= schedule->minor_frame_count) ||
            (e->subaddress > 31U) || (e->subaddress_2 > 31U))
        {
            return false;
        }

        if ((e->msg_type == MIL1553_MSG_RT_TO_RT) && (e->rt_address_2 > MIL1553_RT_ADDR_MAX))
        {
            return false;
        }

        if (e->msg_type == MIL1553_MSG_MODE_CODE)
        {
            // implements: LLR-1553-BC-006: reject an unlisted mode code, or a data-word presence/direction
            // that disagrees with the table
            const bc_mode_entry_t *m;

            if (e->mode_code > 31U)
            {
                return false;
            }
            m = &BC_MODE_TABLE[e->mode_code];
            if ((!m->supported) || (e->has_data_word != m->has_data_word) ||
                (m->has_data_word && (e->mode_data_from_bc != m->data_from_bc)))
            {
                return false;
            }
        }

        if ((e->msg_type == MIL1553_MSG_BC_TO_RT) || (e->msg_type == MIL1553_MSG_RT_TO_BC) ||
            (e->msg_type == MIL1553_MSG_RT_TO_RT) || (e->msg_type == MIL1553_MSG_BROADCAST))
        {
            if ((e->data_word_count == 0U) || (e->data_word_count > MIL1553_MAX_DATA_WORDS))
            {
                // broadcast with 0 data words (e.g. a broadcast mode command carried via MODE_CODE msg_type instead)
                // is valid; non-mode formats require 1-32
                if (e->msg_type != MIL1553_MSG_BROADCAST)
                {
                    return false;
                }
            }
        }

        duration = bc_message_duration_us(e);
        if ((e->slot_offset_us + duration) > schedule->minor_frame_period_us)
        {
            return false;
        }

        // no overlap check against every other entry in the same minor frame (bounded double,
        // i and j both <= BC_MAX_SCHEDULE_ENTRIES, no recursion - MISRA compliant)
        for (j = 0U; j < schedule->entry_count; j++)
        {
            if (j != i)
            {
                const mil1553_bc_schedule_entry_t *other = &schedule->entries[j];

                if (other->minor_frame_index == e->minor_frame_index)
                {
                    uint32_t other_duration = bc_message_duration_us(other);
                    bool e_starts_after_other_ends = (e->slot_offset_us >= (other->slot_offset_us + other_duration));
                    bool other_starts_after_e_ends = (other->slot_offset_us >= (e->slot_offset_us + duration));

                    if ((!e_starts_after_other_ends) && (!other_starts_after_e_ends))
                    {
                        return false; // overlap
                    }
                }
            }
        }
    }

    return true;
}

// implements: LLR-1553-BC-001, LLR-1553-REDUN-001, LLR-1553-REDUN-002
bool bc_init(mil1553_bc_context_t *ctx, const mil1553_bc_schedule_t *schedule,
             mil1553_bus_model_context_t *bus, mil1553_channel_t initial_primary_bus,
             uint8_t fail_threshold)
{
    uint8_t i;

    if (!bc_validate_schedule(schedule))
    {
        ctx->state = MIL1553_BC_STATE_IDLE;
        return false;
    }

    ctx->schedule = *schedule;
    ctx->bus = bus;
    ctx->cbit = (mil1553_bit_cbit_context_t *)0;
    ctx->state = MIL1553_BC_STATE_IDLE;
    ctx->current_minor_frame = 0U;
    ctx->current_entry_index = 0U;

    for (i = 0U; i < BC_MAX_SCHEDULE_ENTRIES; i++)
    {
        ctx->entry_issued[i] = false;
    }

    ctx->primary_bus = initial_primary_bus;
    ctx->consecutive_fail_count[0] = 0U;
    ctx->consecutive_fail_count[1] = 0U;
    ctx->fail_threshold = (fail_threshold == 0U) ? (uint8_t)MIL1553_REDUN_FAIL_THRESHOLD_DEFAULT : fail_threshold;

    ctx->retry_used_for_current_message = false;
    ctx->current_message_bus = initial_primary_bus;
    ctx->message_start_time_us = 0U;
    ctx->last_activity_end_us = 0U;
    ctx->minor_frame_start_time_us = 0U;

    ctx->current_entry_snapshot_index = 0U;
    ctx->response_primary_rt = 0U;
    ctx->response_secondary_rt = 0U;
    ctx->response_is_rt_to_rt = false;
    ctx->response_stage = 0U;
    ctx->response_data_words_expected = 0U;
    ctx->response_data_words_received = 0U;

    ctx->degraded_indication = false;
    ctx->overrun_logged = false;
    ctx->message_ever_sent = false;
    ctx->ignored_event_count = 0U;

    for (i = 0U; i <= MIL1553_RT_ADDR_MAX; i++)
    {
        ctx->rt_failed_this_major_frame[i] = false;
    }

    ctx->error_log_count = 0U;
    ctx->error_log_overflow_count = 0U;

    return true;
}

// implements: LLR-1553-BC-010
bool bc_start(mil1553_bc_context_t *ctx)
{
    if (ctx->state != MIL1553_BC_STATE_IDLE)
    {
        ctx->ignored_event_count++; // start request outside IDLE: ignored and counted, LLR-1553-BC-010
        return false;
    }

    ctx->state = MIL1553_BC_STATE_MAJOR_FRAME;
    return true;
}

// implements: LLR-1553-BC-010
void bc_stop(mil1553_bc_context_t *ctx)
{
    ctx->state = MIL1553_BC_STATE_IDLE;
}

// implements: LLR-1553-BIT-005, LLR-1553-BIT-006
void bc_attach_cbit(mil1553_bc_context_t *ctx, mil1553_bit_cbit_context_t *cbit)
{
    ctx->cbit = cbit;
}

// message issuance, LLR-1553-BC-003 .. LLR-1553-BC-007
static void bc_begin_response_tracking(mil1553_bc_context_t *ctx, const mil1553_bc_schedule_entry_t *e)
{
    ctx->response_stage = 0U;
    ctx->response_data_words_received = 0U;

    switch (e->msg_type)
    {
    case MIL1553_MSG_BC_TO_RT:
        ctx->response_primary_rt = e->rt_address;
        ctx->response_is_rt_to_rt = false;
        ctx->response_data_words_expected = 0U;
        break;

    case MIL1553_MSG_RT_TO_BC:
        ctx->response_primary_rt = e->rt_address;
        ctx->response_is_rt_to_rt = false;
        ctx->response_data_words_expected = e->data_word_count;
        break;

    case MIL1553_MSG_RT_TO_RT:
        // transmitting RT's status arrives first (LLR-1553-BC-005)
        ctx->response_primary_rt = e->rt_address_2;
        ctx->response_secondary_rt = e->rt_address;
        ctx->response_is_rt_to_rt = true;
        ctx->response_data_words_expected = e->data_word_count;
        break;

    case MIL1553_MSG_MODE_CODE:
        ctx->response_primary_rt = e->rt_address;
        ctx->response_is_rt_to_rt = false;
        ctx->response_data_words_expected = (e->has_data_word && (!e->mode_data_from_bc)) ? 1U : 0U;
        break;

    default:
        ctx->response_primary_rt = e->rt_address;
        ctx->response_is_rt_to_rt = false;
        ctx->response_data_words_expected = 0U;
        break;
    }
}

// implements: LLR-1553-BC-003
static void bc_issue_bc_to_rt(mil1553_bc_context_t *ctx, const mil1553_bc_schedule_entry_t *e, mil1553_channel_t channel)
{
    mil1553_command_fields_t cmd;
    mil1553_word_count_t wc_field;
    mil1553_symbols_t symbols;
    uint8_t i;

    cmd.rt_address = e->rt_address;
    cmd.tr_bit = MIL1553_TR_RECEIVE;
    cmd.subaddress = e->subaddress;
    (void)mil1553_word_encode_word_count(e->data_word_count, &wc_field);
    cmd.word_count_field = wc_field;

    (void)mil1553_word_pack_command(&cmd, symbols);
    bc_transmit_word(ctx, channel, symbols);

    // BC-supplied data words: placeholder source, see file header note
    for (i = 0U; i < e->data_word_count; i++)
    {
        mil1553_word_pack_data((mil1553_data_t)i, symbols);
        bc_transmit_word(ctx, channel, symbols);
    }
}

// implements: LLR-1553-BC-004
static void bc_issue_rt_to_bc(mil1553_bc_context_t *ctx, const mil1553_bc_schedule_entry_t *e, mil1553_channel_t channel)
{
    mil1553_command_fields_t cmd;
    mil1553_word_count_t wc_field;
    mil1553_symbols_t symbols;

    cmd.rt_address = e->rt_address;
    cmd.tr_bit = MIL1553_TR_TRANSMIT;
    cmd.subaddress = e->subaddress;
    (void)mil1553_word_encode_word_count(e->data_word_count, &wc_field);
    cmd.word_count_field = wc_field;

    (void)mil1553_word_pack_command(&cmd, symbols);
    bc_transmit_word(ctx, channel, symbols);
}

// implements: LLR-1553-BC-005
static void bc_issue_rt_to_rt(mil1553_bc_context_t *ctx, const mil1553_bc_schedule_entry_t *e, mil1553_channel_t channel)
{
    mil1553_command_fields_t recv_cmd;
    mil1553_command_fields_t xmit_cmd;
    mil1553_word_count_t wc_field;
    mil1553_symbols_t symbols;

    recv_cmd.rt_address = e->rt_address; // receiving RT
    recv_cmd.tr_bit = MIL1553_TR_RECEIVE;
    recv_cmd.subaddress = e->subaddress;
    (void)mil1553_word_encode_word_count(e->data_word_count, &wc_field);
    recv_cmd.word_count_field = wc_field;

    xmit_cmd.rt_address = e->rt_address_2; // transmitting RT
    xmit_cmd.tr_bit = MIL1553_TR_TRANSMIT;
    xmit_cmd.subaddress = e->subaddress_2;
    xmit_cmd.word_count_field = wc_field;

    // issues back to back, get LLR-1553-BC-005
    (void)mil1553_word_pack_command(&recv_cmd, symbols);
    bc_transmit_word(ctx, channel, symbols);

    (void)mil1553_word_pack_command(&xmit_cmd, symbols);
    bc_transmit_word(ctx, channel, symbols);
}

// implements: LLR-1553-BC-006
static void bc_issue_mode_code(mil1553_bc_context_t *ctx, const mil1553_bc_schedule_entry_t *e, mil1553_channel_t channel)
{
    mil1553_command_fields_t cmd;
    mil1553_symbols_t symbols;

    cmd.rt_address = e->rt_address;
    cmd.tr_bit = e->has_data_word && e->mode_data_from_bc ? MIL1553_TR_RECEIVE : MIL1553_TR_TRANSMIT;
    cmd.subaddress = 0U; // mode indicator, 00000 convention (mil1553_notes.md 5)
    cmd.word_count_field = (mil1553_word_count_t)e->mode_code;

    (void)mil1553_word_pack_command(&cmd, symbols);
    bc_transmit_word(ctx, channel, symbols);

    if (e->has_data_word && e->mode_data_from_bc)
    {
        mil1553_word_pack_data((mil1553_data_t)0, symbols); // placeholder, see header note
        bc_transmit_word(ctx, channel, symbols);
    }
}

// implements: LLR-1553-BC-007
static void bc_issue_broadcast(mil1553_bc_context_t *ctx, const mil1553_bc_schedule_entry_t *e, mil1553_channel_t channel)
{
    mil1553_command_fields_t cmd;
    mil1553_word_count_t wc_field;
    mil1553_symbols_t symbols;
    uint8_t i;

    cmd.rt_address = MIL1553_RT_ADDR_BROADCAST;
    cmd.tr_bit = MIL1553_TR_RECEIVE;
    cmd.subaddress = e->subaddress;
    (void)mil1553_word_encode_word_count(e->data_word_count, &wc_field);
    cmd.word_count_field = wc_field;

    (void)mil1553_word_pack_command(&cmd, symbols);
    bc_transmit_word(ctx, channel, symbols);

    for (i = 0U; i < e->data_word_count; i++)
    {
        mil1553_word_pack_data((mil1553_data_t)i, symbols);
        bc_transmit_word(ctx, channel, symbols);
    }
}

/*
    implements: LLR-1553-BC-009, LLR-1553-REDUN-001

    words still unread on either channel when a new command is about to go out belong to an earlier, finished
    or aborted transaction (e.g. the data words that followed a status word the BC rejected); they must not be
    mistaken for the response to the new command, so they are discarded first; bounded by the bus queue capacity
    (MISRA: bounded loop)
*/
static void bc_discard_stale_words(mil1553_bc_context_t *ctx)
{
    mil1553_bus_record_t scratch;
    uint8_t n;

    for (n = 0U; n < BUS_MODEL_QUEUE_CAPACITY; n++)
    {
        if (!bus_model_receive(ctx->bus, MIL1553_CHANNEL_A, MIL1553_TERMINAL_BC, &scratch))
        {
            break;
        }
        ctx->ignored_event_count++; // unsolicited word ignored and counted, LLR-1553-BC-010
    }
    for (n = 0U; n < BUS_MODEL_QUEUE_CAPACITY; n++)
    {
        if (!bus_model_receive(ctx->bus, MIL1553_CHANNEL_B, MIL1553_TERMINAL_BC, &scratch))
        {
            break;
        }
        ctx->ignored_event_count++; // unsolicited word ignored and counted, LLR-1553-BC-010
    }
}

static void bc_issue_message(mil1553_bc_context_t *ctx, const mil1553_bc_schedule_entry_t *e, mil1553_channel_t channel)
{
    bc_discard_stale_words(ctx);

    switch (e->msg_type)
    {
    case MIL1553_MSG_BC_TO_RT:
        bc_issue_bc_to_rt(ctx, e, channel);
        break;

    case MIL1553_MSG_RT_TO_BC:
        bc_issue_rt_to_bc(ctx, e, channel);
        break;

    case MIL1553_MSG_RT_TO_RT:
        bc_issue_rt_to_rt(ctx, e, channel);
        break;

    case MIL1553_MSG_MODE_CODE:
        bc_issue_mode_code(ctx, e, channel);
        break;

    case MIL1553_MSG_BROADCAST:
        bc_issue_broadcast(ctx, e, channel);
        break;

    default: // unreachable given bc_validate_schedule
        break;
    }
}

// redundancy handling, LLR-1553-REDUN-001 .. 003

/*
    implements: LLR-1553-REDUN-003

    for RT-to-RT the stage at which the response stalled identifies the culprit: stage 2 means the transmitting RT
    answered (status + data) and only the receiving RT's closing status is missing; stage 0 or 1 means the transmitting
    RT is the one that stopped; blaming both would needlessly drop a healthy RT's other messages for the major frame
*/
static void bc_mark_rt_failed(mil1553_bc_context_t *ctx, const mil1553_bc_schedule_entry_t *e)
{
    mil1553_rt_addr_t culprit;

    if (e->msg_type == MIL1553_MSG_RT_TO_RT)
    {
        culprit = (ctx->response_stage == 2U) ? e->rt_address : e->rt_address_2;
    }
    else
    {
        culprit = e->rt_address;
    }

    if (culprit <= MIL1553_RT_ADDR_MAX)
    {
        ctx->rt_failed_this_major_frame[culprit] = true;
    }
}

// implements LLR-1553-BC-010 (state machine), one step per call

bool bc_run_frame(mil1553_bc_context_t *ctx)
{
    if (ctx->state == MIL1553_BC_STATE_IDLE)
    {
        return false;
    }

    if (ctx->cbit != (mil1553_bit_cbit_context_t *)0)
    {
        // implements: LLR-1553-BIT-006: tick first (detects a stall since the previous call), then kick (this call is progress)
        bit_cbit_tick(ctx->cbit, bc_now(ctx));
        bit_cbit_watchdog_kick(ctx->cbit, bc_now(ctx));
    }

    // implements: LLR-1553-BC-002; a message still in flight when its minor frame's period has elapsed means the
    // frame overran; raised once per minor frame
    if (((ctx->state == MIL1553_BC_STATE_WAIT_RESPONSE) ||
         (ctx->state == MIL1553_BC_STATE_TIMEOUT) ||
         (ctx->state == MIL1553_BC_STATE_FAILOVER)) &&
        (!ctx->overrun_logged) &&
        ((mil1553_time_us_t)(bc_now(ctx) - ctx->minor_frame_start_time_us) >=
         (mil1553_time_us_t)ctx->schedule.minor_frame_period_us))
    {
        bc_log_error(ctx, ctx->primary_bus, MIL1553_ERR_MINOR_FRAME_OVERRUN,
                     (uint32_t)ctx->current_entry_snapshot_index);
        ctx->overrun_logged = true;
    }

    switch (ctx->state)
    {
    case MIL1553_BC_STATE_MAJOR_FRAME:
    {
        uint8_t i;

        for (i = 0U; i < BC_MAX_SCHEDULE_ENTRIES; i++)
        {
            ctx->entry_issued[i] = false;
        }
        for (i = 0U; i <= MIL1553_RT_ADDR_MAX; i++)
        {
            ctx->rt_failed_this_major_frame[i] = false; // LLR-1553-REDUN-003
        }
        ctx->current_minor_frame = 0U;
        ctx->overrun_logged = false;
        ctx->minor_frame_start_time_us = bc_now(ctx);
        ctx->state = MIL1553_BC_STATE_MINOR_FRAME;
        break;
    }

    case MIL1553_BC_STATE_DEGRADED:
    {
        // continue on remaining capability, per LLR-1553-BC-010; degraded_indication stays sticky;
        // health reporting uses it, so the DEGRADED event remains visible after returning to normal operation
        ctx->state = MIL1553_BC_STATE_MAJOR_FRAME;
        break;
    }

    case MIL1553_BC_STATE_MINOR_FRAME:
    {
        uint8_t i;
        bool issued_one = false;
        mil1553_time_us_t elapsed = (mil1553_time_us_t)(bc_now(ctx) - ctx->minor_frame_start_time_us);

        for (i = 0U; (i < ctx->schedule.entry_count) && (!issued_one); i++)
        {
            const mil1553_bc_schedule_entry_t *e = &ctx->schedule.entries[i];
            bool rt_is_down = ((e->rt_address <= MIL1553_RT_ADDR_MAX) && ctx->rt_failed_this_major_frame[e->rt_address]);

            if ((e->minor_frame_index == ctx->current_minor_frame) && (!ctx->entry_issued[i]) &&
                (!rt_is_down) && (elapsed >= (mil1553_time_us_t)e->slot_offset_us))
            {
                ctx->entry_issued[i] = true;
                ctx->current_entry_snapshot_index = i;
                ctx->current_message_bus = ctx->primary_bus;
                ctx->retry_used_for_current_message = false;

                // implements: LLR-1553-BC-008: no command word earlier than T_GAP_MIN_US after the end of the
                // previous message (or after its no-response timeout expiry, which is what last_activity_end_us
                // holds after a timeout); the BC idles the gap out
                if (ctx->message_ever_sent &&
                    (bc_now(ctx) < (mil1553_time_us_t)(ctx->last_activity_end_us + MIL1553_T_GAP_MIN_US)))
                {
                    bus_model_advance_us(ctx->bus,
                        (mil1553_time_us_t)((ctx->last_activity_end_us + MIL1553_T_GAP_MIN_US) - bc_now(ctx)));
                }

                bc_issue_message(ctx, e, ctx->current_message_bus);
                ctx->message_ever_sent = true;
                ctx->message_start_time_us = bc_now(ctx);
                ctx->last_activity_end_us = ctx->message_start_time_us;

                if (e->msg_type == MIL1553_MSG_BROADCAST)
                {
                    // no status expected, per LLR-1553-BC-007; message is already complete, stay in MINOR_FRAME to scan for the next entry
                }
                else
                {
                    bc_begin_response_tracking(ctx, e);
                    ctx->state = MIL1553_BC_STATE_WAIT_RESPONSE;
                }

                issued_one = true;
            }
        }

        if (!issued_one)
        {
            if (elapsed >= (mil1553_time_us_t)ctx->schedule.minor_frame_period_us)
            {
                // every eligible entry has been issued and the period has elapsed: advance; a frame that ran late
                // is detected while the message is still in flight, at the top of bc_run_frame (LLR-1553-BC-002)
                ctx->overrun_logged = false;
                ctx->current_minor_frame++;
                if (ctx->current_minor_frame >= ctx->schedule.minor_frame_count)
                {
                    ctx->state = MIL1553_BC_STATE_MAJOR_FRAME;
                }
                else
                {
                    ctx->minor_frame_start_time_us = bc_now(ctx);
                }
            }
            // else: not yet time for the next entry nor frame end; remain in MINOR_FRAME and wait for the next call
        }
        break;
    }

    case MIL1553_BC_STATE_WAIT_RESPONSE:
    {
        mil1553_bus_record_t record;

        if (bus_model_receive(ctx->bus, ctx->current_message_bus, MIL1553_TERMINAL_BC, &record))
        {
            mil1553_decoded_word_t decoded;
            mil1553_word_class_t cls = mil1553_word_classify(record.symbols, &decoded);

            {
                // implements: LLR-1553-BC-009: the timeout window restarts from the end of each word actually received
                mil1553_time_us_t word_end = (mil1553_time_us_t)(record.timestamp_us + MIL1553_WORD_TIME_US);
                if (word_end > ctx->last_activity_end_us)
                {
                    ctx->last_activity_end_us = word_end;
                }
            }

            if (ctx->cbit != (mil1553_bit_cbit_context_t *)0)
            {
                // implements: LLR-1553-BIT-005
                bit_cbit_record_word(ctx->cbit, ctx->current_message_bus, cls);
                if ((cls == MIL1553_WORD_VALID) && (ctx->response_stage == 0U))
                {
                    bit_cbit_record_response_time(ctx->cbit,
                        (uint32_t)(record.timestamp_us - ctx->message_start_time_us));
                }
            }

            if (cls != MIL1553_WORD_VALID)
            {
                mil1553_error_type_t et = (cls == MIL1553_WORD_SYNC_ERROR) ? MIL1553_ERR_SYNC : (cls == MIL1553_WORD_MANCHESTER_ERROR) ? MIL1553_ERR_MANCHESTER
                                                                                                                                       : MIL1553_ERR_PARITY;
                bc_log_error(ctx, ctx->current_message_bus, et, (uint32_t)ctx->current_entry_snapshot_index);
                ctx->state = MIL1553_BC_STATE_TIMEOUT;
            }
            else if (ctx->response_stage == 0U)
            {
                // expect a status word (primary RT)
                mil1553_status_fields_t status;
                mil1553_word_unpack_status(decoded.field, &status);

                if (status.rt_address != ctx->response_primary_rt)
                {
                    bc_log_error(ctx, ctx->current_message_bus, MIL1553_ERR_STATUS_ADDRESS_MISMATCH, 
                                 (uint32_t)ctx->current_entry_snapshot_index);
                    
                    ctx->state = MIL1553_BC_STATE_TIMEOUT;
                }
                else if (status.message_error)
                {
                    bc_log_error(ctx, ctx->current_message_bus, MIL1553_ERR_STATUS_ME,
                                     (uint32_t)ctx->current_entry_snapshot_index);
                        ctx->state = MIL1553_BC_STATE_TIMEOUT;
                    }
                    else
                    {
                        if (status.busy)
                        {
                            bc_log_error(ctx, ctx->current_message_bus, MIL1553_ERR_STATUS_BUSY,
                                         (uint32_t)ctx->current_entry_snapshot_index);
                        }
                        if (status.subsystem_flag)
                        {
                            bc_log_error(ctx, ctx->current_message_bus, MIL1553_ERR_STATUS_SF,
                                         (uint32_t)ctx->current_entry_snapshot_index);
                        }
                        if (status.terminal_flag)
                        {
                            bc_log_error(ctx, ctx->current_message_bus, MIL1553_ERR_STATUS_TF,
                                         (uint32_t)ctx->current_entry_snapshot_index);
                        }
 
                        if (ctx->response_data_words_expected > 0U)
                        {
                            ctx->response_stage = 1U;
                        }
                        else if (ctx->response_is_rt_to_rt)
                        {
                            // no data words but RT-RT still owes a second status (zero-length transfer) 
                            ctx->response_stage = 2U;
                        }
                        else
                        {
                            ctx->consecutive_fail_count[bc_channel_index(ctx->current_message_bus)] = 0U;
                            ctx->state = MIL1553_BC_STATE_MINOR_FRAME;
                        }
                    }
                }
                else if (ctx->response_stage == 1U)
                {
                    // expect a data word 
                    ctx->response_data_words_received++;
                    if (ctx->response_data_words_received >= ctx->response_data_words_expected)
                    {
                        if (ctx->response_is_rt_to_rt)
                        {
                            ctx->response_stage = 2U;
                        }
                        else
                        {
                            ctx->consecutive_fail_count[bc_channel_index(ctx->current_message_bus)] = 0U;
                            ctx->state = MIL1553_BC_STATE_MINOR_FRAME;
                        }
                    }
                    // else remain in stage 1, awaiting more data words 
                }
                else
                {
                    // response_stage == 2: RT-RT secondary status 
                    mil1553_status_fields_t status2;
                    mil1553_word_unpack_status(decoded.field, &status2);
 
                    if ((status2.rt_address != ctx->response_secondary_rt) || status2.message_error)
                    {
                        bc_log_error(ctx, ctx->current_message_bus, MIL1553_ERR_STATUS_ADDRESS_MISMATCH,
                                     (uint32_t)ctx->current_entry_snapshot_index);
                        ctx->state = MIL1553_BC_STATE_TIMEOUT;
                    }
                    else
                    {
                        ctx->consecutive_fail_count[bc_channel_index(ctx->current_message_bus)] = 0U;
                        ctx->state = MIL1553_BC_STATE_MINOR_FRAME;
                    }
                }
            }
            else
            {
                mil1553_time_us_t elapsed = (mil1553_time_us_t)(bc_now(ctx) - ctx->last_activity_end_us);
 
                if (elapsed >= (mil1553_time_us_t)MIL1553_T_NORESP_US)
                {
                    // implements: LLR-1553-ERR-003: a stall in the middle of the data phase means fewer valid data words
                    // arrived than the command word count promised; a stall before any status is a plain no-response
                    bool short_data = (ctx->response_stage == 1U) &&
                                      (ctx->response_data_words_received < ctx->response_data_words_expected);

                    bc_log_error(ctx, ctx->current_message_bus,
                                 short_data ? MIL1553_ERR_WORD_COUNT_MISMATCH : MIL1553_ERR_NO_RESPONSE,
                                 (uint32_t)ctx->current_entry_snapshot_index);
                    // the gap (LLR-1553-BC-008) runs from the timeout expiry
                    ctx->last_activity_end_us = (mil1553_time_us_t)(ctx->last_activity_end_us + MIL1553_T_NORESP_US);
                    ctx->state = MIL1553_BC_STATE_TIMEOUT;
                }
                // else: still within the response window, keep waiting 
            }
            break;
        }
 
        case MIL1553_BC_STATE_TIMEOUT:
        {
            uint8_t idx = bc_channel_index(ctx->current_message_bus);
            const mil1553_bc_schedule_entry_t *e = &ctx->schedule.entries[ctx->current_entry_snapshot_index];
 
            ctx->consecutive_fail_count[idx]++;
 
            if (ctx->consecutive_fail_count[idx] >= ctx->fail_threshold)
            {
                // implements: LLR-1553-REDUN-002 
                ctx->primary_bus = bc_alternate_channel(ctx->primary_bus);
                ctx->consecutive_fail_count[0] = 0U;
                ctx->consecutive_fail_count[1] = 0U;
                ctx->degraded_indication = true;
                ctx->retry_used_for_current_message = false;
                ctx->state = MIL1553_BC_STATE_DEGRADED;
            }
            else if ((!ctx->retry_used_for_current_message) &&
                     (!bus_model_is_channel_failed(ctx->bus, bc_alternate_channel(ctx->current_message_bus))))
            {
                ctx->state = MIL1553_BC_STATE_FAILOVER;
            }
            else
            {
                // implements: LLR-1553-REDUN-003 
                bc_mark_rt_failed(ctx, e);
                ctx->retry_used_for_current_message = false;
                ctx->state = MIL1553_BC_STATE_MINOR_FRAME;
            }
            break;
        }
 
        case MIL1553_BC_STATE_FAILOVER:
        {
            // implements: LLR-1553-REDUN-001 
            const mil1553_bc_schedule_entry_t *e = &ctx->schedule.entries[ctx->current_entry_snapshot_index];
 
            ctx->retry_used_for_current_message = true;
            ctx->current_message_bus = bc_alternate_channel(ctx->current_message_bus);
 
            bus_model_advance_us(ctx->bus, (mil1553_time_us_t)MIL1553_T_GAP_MIN_US);
            bc_issue_message(ctx, e, ctx->current_message_bus);
            ctx->message_start_time_us = bc_now(ctx);
            ctx->last_activity_end_us = ctx->message_start_time_us;
            bc_begin_response_tracking(ctx, e);
 
            ctx->state = MIL1553_BC_STATE_WAIT_RESPONSE;
            break;
        }
 
        default:
            // MIL1553_BC_STATE_IDLE handled above; unreachable here
            break;
    }
 
    return true;
}
 
// implements: LLR-1553-BC-010 
mil1553_bc_state_t bc_get_state(const mil1553_bc_context_t *ctx)
{
    return ctx->state;
}
 
// implements: LLR-1553-REDUN-002, LLR-1553-REDUN-003 
void bc_get_health(const mil1553_bc_context_t *ctx, mil1553_bc_health_t *out_health)
{
    uint8_t i;
 
    out_health->degraded = ctx->degraded_indication;
    out_health->primary_bus = ctx->primary_bus;
    out_health->ignored_events = ctx->ignored_event_count;
 
    for (i = 0U; i <= MIL1553_RT_ADDR_MAX; i++)
    {
        out_health->rt_failed[i] = ctx->rt_failed_this_major_frame[i];
    }
}