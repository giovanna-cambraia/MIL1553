/*
    project: MIL1553

    realizes: LLR-1553-RT-001 .. LLR-1553-RT-013

    design simplifications made at this stage (flagged for review at design baseline, SDP 2.2):
    - RT-supplied data words for mode codes 16 (Transmit Vector Word) and 18 (Transmit Last Command) use placeholder content
    (the subsystem/diagnostic value a real LRU would supply is outside this project's scope, PSAC 2.3); mode code 19 (Transmit BIT Word)
    is the one real content source - it reads the BIT result flags already tracked in ctx (terminal_flag/subsystem_flag), which is
    genuine, not a placeholder
    - a word addressed to another RT is identified by comparing the best-effort decoded rt_address field even when word-level
    classification is not VALID; this means a transmission error that also corrupts the address bits may occasionally be
    mis-filtered as "not addressed to me" rather than logged; this mirrors how a real RT's address comparator (often a separate,
    earlier hardware stage from full word validation) behaves, and is accepted here as a modeling simplification (mil1553_notes.md
    7, host-based environment boundary)
*/

#include "rt_service.h"
#include "mil1553_word.h"

// mode code table, per design/mode_code_table.md (LLR-1553-RT-008)

typedef enum {
    RT_MODE_NONE = 0,            // recognized but not supported
    RT_MODE_SYNC,                // code 1
    RT_MODE_TX_STATUS,           // code 2: the status word is the response
    RT_MODE_SELF_TEST,           // code 3
    RT_MODE_TX_SHUTDOWN,         // code 4
    RT_MODE_OVERRIDE_SHUTDOWN,   // code 5
    RT_MODE_INHIBIT_TF,          // code 6
    RT_MODE_OVERRIDE_INHIBIT_TF, // code 7
    RT_MODE_RESET,               // code 8
    RT_MODE_TX_VECTOR,           // code 16
    RT_MODE_SYNC_DATA,           // code 17
    RT_MODE_TX_LAST_COMMAND,     // code 18
    RT_MODE_TX_BIT               // code 19
} rt_mode_handler_t;

typedef struct
{
    bool supported;
    bool has_data_word;
    bool data_from_bc; // true: BC supplies the word; false: RT supplies it
    rt_mode_handler_t handler;
} mil1553_rt_mode_entry_t;

#define MODE_UNSUPPORTED { false, false, false, RT_MODE_NONE }

static const mil1553_rt_mode_entry_t RT_MODE_TABLE[32] = {
    /*  0 */ MODE_UNSUPPORTED,                                    // Dynamic Bus Control - recognized, unsupported (mode_code_table.md 3)
    /*  1 */ { true, false, false, RT_MODE_SYNC },               // Synchronize, no data
    /*  2 */ { true, false, false, RT_MODE_TX_STATUS },          // Transmit Status Word
    /*  3 */ { true, false, false, RT_MODE_SELF_TEST },          // Initiate Self-Test
    /*  4 */ { true, false, false, RT_MODE_TX_SHUTDOWN },        // Transmitter Shutdown
    /*  5 */ { true, false, false, RT_MODE_OVERRIDE_SHUTDOWN },  // Override Transmitter Shutdown
    /*  6 */ { true, false, false, RT_MODE_INHIBIT_TF },         // Inhibit Terminal Flag
    /*  7 */ { true, false, false, RT_MODE_OVERRIDE_INHIBIT_TF },// Override Inhibit Terminal Flag
    /*  8 */ { true, false, false, RT_MODE_RESET },              // Reset Remote Terminal
    /*  9 */ MODE_UNSUPPORTED, /* 10 */ MODE_UNSUPPORTED, /* 11 */ MODE_UNSUPPORTED,
    /* 12 */ MODE_UNSUPPORTED, /* 13 */ MODE_UNSUPPORTED, /* 14 */ MODE_UNSUPPORTED,
    /* 15 */ MODE_UNSUPPORTED,
    /* 16 */ { true, true, false, RT_MODE_TX_VECTOR },           // Transmit Vector Word
    /* 17 */ { true, true, true, RT_MODE_SYNC_DATA },            // Synchronize, with data
    /* 18 */ { true, true, false, RT_MODE_TX_LAST_COMMAND },     // Transmit Last Command
    /* 19 */ { true, true, false, RT_MODE_TX_BIT },              // Transmit BIT Word
    /* 20 */ MODE_UNSUPPORTED, /* 21 */ MODE_UNSUPPORTED, /* 22 */ MODE_UNSUPPORTED,
    /* 23 */ MODE_UNSUPPORTED, /* 24 */ MODE_UNSUPPORTED, /* 25 */ MODE_UNSUPPORTED,
    /* 26 */ MODE_UNSUPPORTED, /* 27 */ MODE_UNSUPPORTED, /* 28 */ MODE_UNSUPPORTED,
    /* 29 */ MODE_UNSUPPORTED, /* 30 */ MODE_UNSUPPORTED, /* 31 */ MODE_UNSUPPORTED
};

// internal helpers

static mil1553_time_us_t rt_now(const mil1553_rt_context_t *ctx)
{
    return bus_model_now_us(ctx->bus);
}

// implements: LLR-1553-ERR-004 (RT side)
static void rt_log_error(mil1553_rt_context_t *ctx, mil1553_channel_t channel, mil1553_error_type_t error_type, uint32_t message_id)
{
    if (ctx->error_log_count < MIL1553_ERROR_LOG_CAPACITY)
    {
        mil1553_error_record_t *rec = &ctx->error_log[ctx->error_log_count];
        rec->timestamp_us = rt_now(ctx);
        rec->terminal = ctx->own_terminal_id;
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

// implements: LLR-1553-RT-003, LLR-1553-RT-007, LLR-1553-RT-010
// discards the whole in-progress message: logs the error, latches ME, marks the message as a transmission
// error (so ERROR transmits nothing) and enters ERROR
static void rt_fail_message(mil1553_rt_context_t *ctx, mil1553_error_type_t error_type)
{
    rt_log_error(ctx, ctx->pending_channel, error_type, 0U);
    ctx->message_error_latched = true;
    ctx->pending_transmission_error = true;
    ctx->commit_pending = false;
    ctx->state = MIL1553_RT_STATE_ERROR;
}

static uint8_t rt_channel_index(mil1553_channel_t channel)
{
    return (uint8_t)((channel == MIL1553_CHANNEL_A) ? 0U : 1U);
}

static void rt_transmit_word(mil1553_rt_context_t *ctx, const mil1553_symbols_t symbols)
{
    if (ctx->transmitter_shutdown[rt_channel_index(ctx->pending_channel)])
    {
        return; // implements: LLR-1553-RT-008: transmitter shut down on this channel
    }
    (void)bus_model_transmit(ctx->bus, ctx->pending_channel, symbols, ctx->own_terminal_id);
    bus_model_advance_us(ctx->bus, (mil1553_time_us_t)MIL1553_WORD_TIME_US);
}

// implements: LLR-1553-RT-008: the data word a transmit-type mode command returns
static mil1553_data_t rt_mode_response_word(const mil1553_rt_context_t *ctx)
{
    mil1553_data_t value;

    switch (RT_MODE_TABLE[ctx->pending_cmd.word_count_field & 0x1FU].handler)
    {
        case RT_MODE_TX_VECTOR:
            value = ctx->vector_word;
            break;
        case RT_MODE_TX_LAST_COMMAND:
            value = (mil1553_data_t)ctx->last_command_field;
            break;
        case RT_MODE_TX_BIT:
            value = (mil1553_data_t)ctx->bit_word; // built from the BIT result structure (bit_result_to_word)
            break;
        default:
            value = 0U;
            break;
    }
    return value;
}

// A word that belongs to the next message is kept, not discarded.
static void rt_replay(mil1553_rt_context_t *ctx, mil1553_channel_t ch, const mil1553_bus_record_t *rec)
{
    ctx->replay_record = *rec;
    ctx->replay_channel = ch;
    ctx->replay_valid = true;
}

// The next word on a channel for this RT: a replayed word first, else the bus.
static bool rt_next_word(mil1553_rt_context_t *ctx, mil1553_channel_t ch, mil1553_bus_record_t *rec)
{
    bool got;

    if (ctx->replay_valid && (ctx->replay_channel == ch))
    {
        *rec = ctx->replay_record;
        ctx->replay_valid = false;
        got = true;
    }
    else
    {
        got = bus_model_receive(ctx->bus, ch, ctx->own_terminal_id, rec);
    }
    return got;
}

// implements: LLR-1553-RT-008 (handler dispatch), LLR-1553-RT-013 (reset)
static void rt_execute_mode_command(mil1553_rt_context_t *ctx, mil1553_data_t data)
{
    // transmitter shutdown commands act on the bus the command did NOT arrive on
    uint8_t other = (uint8_t)((ctx->pending_channel == MIL1553_CHANNEL_A) ? 1U : 0U);

    switch (RT_MODE_TABLE[ctx->pending_cmd.word_count_field & 0x1FU].handler)
    {
        case RT_MODE_SYNC:
            ctx->sync_count++;
            break;
        case RT_MODE_SYNC_DATA:
            ctx->sync_count++;
            ctx->last_sync_data = data;
            break;
        case RT_MODE_SELF_TEST:
            ctx->self_test_requested = true;
            break;
        case RT_MODE_TX_SHUTDOWN:
            ctx->transmitter_shutdown[other] = true;
            break;
        case RT_MODE_OVERRIDE_SHUTDOWN:
            ctx->transmitter_shutdown[other] = false;
            break;
        case RT_MODE_INHIBIT_TF:
            ctx->tf_inhibited = true;
            break;
        case RT_MODE_OVERRIDE_INHIBIT_TF:
            ctx->tf_inhibited = false;
            break;
        case RT_MODE_RESET:
            ctx->reset_pending = true; // performed once the status word has been sent
            break;
        default:
            break; // transmit-type codes act through the response word
    }
}

// implements: LLR-1553-RT-003: the whole block has arrived valid and no extra word
// followed, so it is committed now (or the data-carrying mode command executed).
static void rt_commit_received(mil1553_rt_context_t *ctx)
{
    if (ctx->pending_is_mode_code)
    {
        ctx->mode_data_word = ctx->staging[0];
        rt_execute_mode_command(ctx, ctx->mode_data_word);
    }
    else
    {
        uint8_t k;
        mil1553_rt_subaddr_buffer_t *buf = &ctx->subaddr_buffer[ctx->pending_cmd.subaddress];

        for (k = 0U; k < ctx->pending_data_received; k++)
        {
            buf->words[k] = ctx->staging[k];
        }
        buf->word_count = ctx->pending_data_received;
    }
    ctx->commit_pending = false;
}

// implements: LLR-1553-RT-013: a message is over; honour a pending reset, else
// return to WAIT_COMMAND (or stay in FAIL_SAFE, which only a reset leaves).
static void rt_finish_message(mil1553_rt_context_t *ctx)
{
    ctx->message_end_time_us = 0U;
    if (ctx->reset_pending)
    {
        rt_reset(ctx);
    }
    else
    {
        ctx->state = ctx->fail_safe ? MIL1553_RT_STATE_FAIL_SAFE : MIL1553_RT_STATE_WAIT_COMMAND;
    }
}

// implements: LLR-1553-RT-006
static void rt_build_and_send_status(mil1553_rt_context_t *ctx, bool suppress_data, uint8_t data_count_override)
{
    mil1553_status_fields_t status;
    mil1553_symbols_t symbols;
    uint8_t i;

    status.rt_address = ctx->own_address;
    status.message_error = ctx->message_error_latched;
    status.instrumentation = false; // not exercised, SDD 4.7; mil1553_notes.md 4
    status.service_request = false; // not exercised
    status.broadcast_received = ctx->broadcast_received_latched;
    status.busy = ctx->subsystem_busy;
    status.subsystem_flag = ctx->subsystem_flag || ctx->bit_degraded; // LLR-1553-BIT-008: SF on a non-fatal BIT result
    status.dbc_acceptance = false; // Dynamic Bus Control not implemented, SDD 4.6
    // TF: BIT-driven, suppressed by inhibit-TF (mode 6), forced on in FAIL_SAFE (RT-013)
    status.terminal_flag = (ctx->terminal_flag && (!ctx->tf_inhibited)) || ctx->fail_safe;

    (void)mil1553_word_pack_status(&status, symbols);
    rt_transmit_word(ctx, symbols);

    if (!suppress_data)
    {
        uint8_t count = data_count_override;
        for (i = 0U; i < count; i++)
        {
            mil1553_symbols_t data_symbols;
            mil1553_data_t value;

            if (ctx->pending_is_mode_code)
            {
                value = rt_mode_response_word(ctx);
            }
            else if (i < ctx->subaddr_buffer[ctx->pending_cmd.subaddress].word_count)
            {
                value = ctx->subaddr_buffer[ctx->pending_cmd.subaddress].words[i];
            }
            else
            {
                value = 0U; // the subaddress buffer holds fewer words than commanded
            }

            mil1553_word_pack_data(value, data_symbols);
            rt_transmit_word(ctx, data_symbols);
        }
    }
}

// lifecycle, LLR-1553-RT-013
bool rt_init(mil1553_rt_context_t *ctx, mil1553_rt_addr_t own_address, mil1553_terminal_id_t own_terminal_id,
             const mil1553_rt_subaddr_config_t subaddr_config[RT_SUBADDRESS_COUNT], uint8_t response_delay_us,
             mil1553_bus_model_context_t *bus)
{
    uint8_t i;

    if ((own_address > MIL1553_RT_ADDR_MAX) || (response_delay_us < MIL1553_T_RESP_MIN_US) || (response_delay_us > MIL1553_T_RESP_MAX_US))
    {
        return false;
    }

    ctx->own_address = own_address;
    ctx->own_terminal_id = own_terminal_id;
    ctx->bus = bus;
    ctx->cbit = (mil1553_bit_cbit_context_t *)0;

    for (i = 0U; i < RT_SUBADDRESS_COUNT; i++)
    {
        ctx->subaddr_config[i] = subaddr_config[i];
        ctx->subaddr_buffer[i].word_count = 0U;
    }

    ctx->response_delay_us = response_delay_us;
    ctx->state = MIL1553_RT_STATE_RESET;

    ctx->message_error_latched = false;
    ctx->broadcast_received_latched = false;
    ctx->subsystem_busy = false;
    ctx->subsystem_flag = false;
    ctx->terminal_flag = false;

    ctx->pending_is_broadcast = false;
    ctx->pending_is_mode_code = false;
    ctx->pending_transmission_error = false;
    ctx->pending_is_rt_to_rt = false;
    ctx->pending_rt_to_rt_status_pending = false;
    ctx->pending_rt_to_rt_xmit_addr = 0U;
    ctx->pending_rt_to_rt_wait_start_us = 0U;
    ctx->pending_last_word_end_us = 0U;
    ctx->mode_data_word = 0U;
    ctx->commit_pending = false;
    ctx->replay_valid = false;
    ctx->fail_safe = false;
    ctx->reset_pending = false;
    ctx->tf_inhibited = false;
    ctx->transmitter_shutdown[0] = false;
    ctx->transmitter_shutdown[1] = false;
    ctx->self_test_requested = false;
    ctx->sync_count = 0U;
    ctx->last_sync_data = 0U;
    ctx->vector_word = 0U;
    ctx->latest_command_field = 0U;
    ctx->last_command_field = 0U;
    ctx->bit_degraded = false;
    ctx->bit_word = 0U;
    ctx->pending_data_expected = 0U;
    ctx->pending_data_received = 0U;
    ctx->pending_data_to_send = 0U;
    ctx->message_end_time_us = 0U;

    ctx->error_log_count = 0U;
    ctx->error_log_overflow_count = 0U;

    return true;
}

// implements: LLR-1553-BIT-005, LLR-1553-BIT-006
void rt_attach_cbit(mil1553_rt_context_t *ctx, mil1553_bit_cbit_context_t *cbit)
{
    ctx->cbit = cbit;
}

void rt_pbit_complete(mil1553_rt_context_t *ctx, bool passed)
{
    ctx->fail_safe = !passed;
    ctx->state = passed ? MIL1553_RT_STATE_IDLE : MIL1553_RT_STATE_FAIL_SAFE;
}

bool rt_enable(mil1553_rt_context_t *ctx)
{
    if (ctx->state != MIL1553_RT_STATE_IDLE)
    {
        return false;
    }
    ctx->state = MIL1553_RT_STATE_WAIT_COMMAND;
    return true;
}

void rt_reset(mil1553_rt_context_t *ctx)
{
    ctx->state = MIL1553_RT_STATE_RESET;
    ctx->message_error_latched = false;
    ctx->broadcast_received_latched = false;
    ctx->terminal_flag = false;          // PBIT re-evaluates it
    ctx->bit_degraded = false;
    ctx->bit_word = 0U;
    ctx->fail_safe = false;
    ctx->reset_pending = false;
    ctx->tf_inhibited = false;
    ctx->transmitter_shutdown[0] = false;
    ctx->transmitter_shutdown[1] = false;
    ctx->pending_transmission_error = false;
    ctx->pending_data_expected = 0U;
    ctx->pending_data_received = 0U;
    ctx->pending_data_to_send = 0U;
    ctx->message_end_time_us = 0U;
    ctx->commit_pending = false;
    ctx->replay_valid = false;
}

// implements: LLR-1553-RT-008
bool rt_take_self_test_request(mil1553_rt_context_t *ctx)
{
    bool requested = ctx->self_test_requested;
    ctx->self_test_requested = false;
    return requested;
}

// implements: LLR-1553-BIT-008, LLR-1553-RT-006, LLR-1553-RT-008
void rt_apply_bit_state(mil1553_rt_context_t *ctx, mil1553_bit_state_t state, uint16_t bit_word)
{
    ctx->bit_word = bit_word;

    if (state == MIL1553_BIT_STATE_FAIL)
    {
        rt_report_bit_result(ctx, true, true, false);
    }
    else if (state == MIL1553_BIT_STATE_DEGRADED)
    {
        ctx->bit_degraded = true;
    }
    else if (state == MIL1553_BIT_STATE_OPERATIONAL)
    {
        ctx->bit_degraded = false;
    }
    else
    {
        // POWER_UP / PBIT / CBIT_RUNNING / IBIT_RUNNING: a test is in progress, no verdict yet
    }
}

// implements: LLR-1553-RT-008
void rt_set_vector_word(mil1553_rt_context_t *ctx, mil1553_data_t value)
{
    ctx->vector_word = value;
}

void rt_set_busy(mil1553_rt_context_t *ctx, bool busy)
{
    ctx->subsystem_busy = busy;
}

void rt_set_subsystem_flag(mil1553_rt_context_t *ctx, bool flag)
{
    ctx->subsystem_flag = flag;
}

void rt_report_bit_result(mil1553_rt_context_t *ctx, bool fatal, bool set_terminal_flag, bool set_subsystem_flag)
{
    if (set_terminal_flag)
    {
        ctx->terminal_flag = true;
    }
    if (set_subsystem_flag)
    {
        ctx->subsystem_flag = true;
    }
    if (fatal)
    {
        ctx->fail_safe = true;
        ctx->state = MIL1553_RT_STATE_FAIL_SAFE; // any state -> FAIL_SAFE, LLR-1553-RT-013
    }
}

// implements: LLR-1553-RT-013 (state machine), one step per call
bool rt_run(mil1553_rt_context_t *ctx)
{
    if ((ctx->state == MIL1553_RT_STATE_RESET) || (ctx->state == MIL1553_RT_STATE_IDLE))
    {
        return false;
    }

    if (ctx->cbit != (mil1553_bit_cbit_context_t *)0)
    {
        // implements: LLR-1553-BIT-006: tick first, then kick
        bit_cbit_tick(ctx->cbit, rt_now(ctx));
        bit_cbit_watchdog_kick(ctx->cbit, rt_now(ctx));
    }

    switch (ctx->state)
    {
    case MIL1553_RT_STATE_WAIT_COMMAND:
    case MIL1553_RT_STATE_FAIL_SAFE:
    {
        // FAIL_SAFE still listens for commands addressed to self (LLR-1553-RT-013); both channels are polled, command
        // channel preferred_bus becomes whichever one the word arrived on
        mil1553_bus_record_t record;
        mil1553_channel_t ch;
        bool got_word = false;

        for (ch = MIL1553_CHANNEL_A; (ch <= MIL1553_CHANNEL_B) && (!got_word); ch++)
        {
            if (rt_next_word(ctx, ch, &record))
            {
                got_word = true;

                mil1553_decoded_word_t decoded;
                mil1553_word_class_t cls = mil1553_word_classify(record.symbols, &decoded);

                if (ctx->cbit != (mil1553_bit_cbit_context_t *)0)
                {
                    bit_cbit_record_word(ctx->cbit, ch, cls); // LLR-1553-BIT-005
                }

                // implements: LLR-1553-RT-001 - best-effort address even on a non-VALID word; see file header
                // note on this simplification
                mil1553_command_fields_t cmd_fields;
                mil1553_word_unpack_command(decoded.field, &cmd_fields);

                // a VALID word with DATA sync is never a command candidate (e.g. another terminal's IBIT pattern
                // word, LLR-1553-BIT-007); a word with a corrupted sync still falls through to the best-effort
                // transmission-error path below
                bool is_command_candidate = (cls != MIL1553_WORD_VALID) ||
                                             (decoded.sync == MIL1553_SYNC_COMMAND_STATUS);

                bool addressed_to_me = is_command_candidate &&
                                       ((cmd_fields.rt_address == ctx->own_address) ||
                                        (cmd_fields.rt_address == MIL1553_RT_ADDR_BROADCAST));

                if (!addressed_to_me)
                {
                    // implements: LLR-1553-RT-001 - silently ignore
                }
                else
                {
                    ctx->pending_channel = ch;
                    ctx->pending_cmd = cmd_fields;
                    ctx->pending_last_word_end_us = (mil1553_time_us_t)(record.timestamp_us + MIL1553_WORD_TIME_US);
                    if (cls == MIL1553_WORD_VALID)
                    {
                        ctx->last_command_field = ctx->latest_command_field;  // mode code 18 returns the one before this
                        ctx->latest_command_field = decoded.field;
                    }
                    ctx->pending_is_broadcast = (cmd_fields.rt_address == MIL1553_RT_ADDR_BROADCAST);
                    ctx->pending_is_mode_code = ((cmd_fields.subaddress == 0U) || (cmd_fields.subaddress == 31U));
                    ctx->pending_is_rt_to_rt = false;
                    ctx->pending_rt_to_rt_status_pending = false;

                    // implements: LLR-1553-RT-006. ME clears on the next valid command word and BCR on the next valid
                    // non-broadcast one - except the transmit-status-word and transmit-last-command mode commands,
                    // which must report the status as it stands. This is how a BC learns of an error the RT could not answer.
                    {
                        bool preserves_status = ctx->pending_is_mode_code &&
                            ((RT_MODE_TABLE[cmd_fields.word_count_field & 0x1FU].handler == RT_MODE_TX_STATUS) ||
                             (RT_MODE_TABLE[cmd_fields.word_count_field & 0x1FU].handler == RT_MODE_TX_LAST_COMMAND));

                        if ((cls == MIL1553_WORD_VALID) && (!preserves_status))
                        {
                            ctx->message_error_latched = false;
                            if (!ctx->pending_is_broadcast)
                            {
                                ctx->broadcast_received_latched = false;
                            }
                        }
                    }

                    if (cls != MIL1553_WORD_VALID)
                    {
                        // implements: LLR-1553-RT-010
                        ctx->pending_transmission_error = true;
                        rt_log_error(ctx, ch,
                                     (cls == MIL1553_WORD_SYNC_ERROR) ? MIL1553_ERR_SYNC : (cls == MIL1553_WORD_MANCHESTER_ERROR) ? MIL1553_ERR_MANCHESTER
                                                                                                                                  : MIL1553_ERR_PARITY,
                                     0U);
                        ctx->state = (ctx->state == MIL1553_RT_STATE_FAIL_SAFE) ? MIL1553_RT_STATE_FAIL_SAFE : MIL1553_RT_STATE_ERROR;
                    }
                    else
                    {
                        ctx->pending_transmission_error = false;
                        ctx->state = (ctx->state == MIL1553_RT_STATE_FAIL_SAFE) ? MIL1553_RT_STATE_RESPOND : MIL1553_RT_STATE_VALIDATE;
                        // FAIL_SAFE short-circuits straight to RESPOND: TF-only status, no execution (LLR-1553-RT-013)
                        if (ctx->state == MIL1553_RT_STATE_RESPOND)
                        {
                            ctx->message_end_time_us = ctx->pending_last_word_end_us;
                            ctx->pending_data_to_send = 0U;
                            // FAIL_SAFE executes nothing except a reset: it is the only way out.
                            if (ctx->pending_is_mode_code &&
                                (RT_MODE_TABLE[ctx->pending_cmd.word_count_field & 0x1FU].handler == RT_MODE_RESET))
                            {
                                ctx->reset_pending = true;
                            }
                        }
                    }
                }
            }
        }
        break;
    }

    case MIL1553_RT_STATE_VALIDATE:
    {
        if (ctx->pending_transmission_error)
        {
            // should not normally reach here (handled in WAIT_COMMAND), defensive fallback
            ctx->state = MIL1553_RT_STATE_ERROR;
            break;
        }

        if (ctx->subsystem_busy)
        {
            // implements: LLR-1553-RT-012 (busy takes precedence)
            ctx->state = MIL1553_RT_STATE_BUSY;
            break;
        }

        if (ctx->pending_is_broadcast && (!ctx->pending_is_mode_code) &&
            (ctx->pending_cmd.tr_bit == MIL1553_TR_TRANSMIT))
        {
            // implements: LLR-1553-RT-009/011: a broadcast cannot ask for data back
            ctx->pending_validation = MIL1553_CMD_ILLEGAL_DIRECTION;
            ctx->state = MIL1553_RT_STATE_ERROR;
            break;
        }

        if (ctx->pending_is_mode_code)
        {
            uint8_t code = ctx->pending_cmd.word_count_field; // mode code occupies this field
            const mil1553_rt_mode_entry_t *m = &RT_MODE_TABLE[code & 0x1FU];

            if (!m->supported)
            {
                ctx->pending_validation = MIL1553_CMD_ILLEGAL_SUBADDRESS; // unsupported mode code
                ctx->state = MIL1553_RT_STATE_ERROR;
            }
            else if (m->has_data_word && m->data_from_bc)
            {
                ctx->pending_data_expected = 1U;
                ctx->pending_data_received = 0U;
                ctx->state = MIL1553_RT_STATE_EXECUTE;
            }
            else
            {
                ctx->pending_data_expected = 0U;
                ctx->pending_data_to_send = (m->has_data_word && (!m->data_from_bc)) ? 1U : 0U;
                rt_execute_mode_command(ctx, 0U);
                ctx->state = MIL1553_RT_STATE_EXECUTE;
            }
        }
        else
        {
            const mil1553_rt_subaddr_config_t *cfg = &ctx->subaddr_config[ctx->pending_cmd.subaddress];
            uint8_t count = mil1553_word_decode_word_count(ctx->pending_cmd.word_count_field);
            bool direction_ok = (ctx->pending_cmd.tr_bit == MIL1553_TR_RECEIVE) ? cfg->allow_receive : cfg->allow_transmit;

            if (!cfg->enabled)
            {
                ctx->pending_validation = MIL1553_CMD_ILLEGAL_SUBADDRESS;
                ctx->state = MIL1553_RT_STATE_ERROR;
            }
            else if (!direction_ok)
            {
                ctx->pending_validation = MIL1553_CMD_ILLEGAL_DIRECTION;
                ctx->state = MIL1553_RT_STATE_ERROR;
            }
            else if ((count < cfg->min_word_count) || (count > cfg->max_word_count))
            {
                ctx->pending_validation = MIL1553_CMD_ILLEGAL_WORD_COUNT;
                ctx->state = MIL1553_RT_STATE_ERROR;
            }
            else
            {
                ctx->pending_validation = MIL1553_CMD_VALID;

                if (ctx->pending_cmd.tr_bit == MIL1553_TR_RECEIVE)
                {
                    ctx->pending_data_expected = count;
                    ctx->pending_data_received = 0U;
                    ctx->pending_data_to_send = 0U;
                }
                else
                {
                    ctx->pending_data_expected = 0U;
                    ctx->pending_data_to_send = count;
                }
                ctx->state = MIL1553_RT_STATE_EXECUTE;
            }
        }
        break;
    }

    case MIL1553_RT_STATE_EXECUTE:
    {
        if (ctx->pending_data_expected > ctx->pending_data_received)
        {
            mil1553_bus_record_t record;

            if (rt_next_word(ctx, ctx->pending_channel, &record))
            {
                mil1553_decoded_word_t decoded;
                mil1553_word_class_t cls = mil1553_word_classify(record.symbols, &decoded);

                if (ctx->cbit != (mil1553_bit_cbit_context_t *)0)
                {
                    bit_cbit_record_word(ctx->cbit, ctx->pending_channel, cls); // LLR-1553-BIT-005
                }

                ctx->pending_last_word_end_us = (mil1553_time_us_t)(record.timestamp_us + MIL1553_WORD_TIME_US);

                if (cls != MIL1553_WORD_VALID)
                {
                    // implements: LLR-1553-RT-003/010 - any word error mid-message discards the whole message
                    rt_fail_message(ctx,
                                    (cls == MIL1553_WORD_SYNC_ERROR) ? MIL1553_ERR_SYNC :
                                    (cls == MIL1553_WORD_MANCHESTER_ERROR) ? MIL1553_ERR_MANCHESTER :
                                    MIL1553_ERR_PARITY);
                }
                else if (ctx->pending_is_rt_to_rt && ctx->pending_rt_to_rt_status_pending)
                {
                    // implements: LLR-1553-RT-007 (receiving RT): the next word must be the transmitting RT's status
                    mil1553_status_fields_t xmit_status;
                    mil1553_word_unpack_status(decoded.field, &xmit_status);

                    if (decoded.sync != MIL1553_SYNC_COMMAND_STATUS)
                    {
                        rt_fail_message(ctx, MIL1553_ERR_WORD_COUNT_MISMATCH);
                    }
                    else if (xmit_status.rt_address != ctx->pending_rt_to_rt_xmit_addr)
                    {
                        rt_fail_message(ctx, MIL1553_ERR_STATUS_ADDRESS_MISMATCH);
                    }
                    else if (xmit_status.message_error)
                    {
                        rt_fail_message(ctx, MIL1553_ERR_STATUS_ME);
                    }
                    else
                    {
                        ctx->pending_rt_to_rt_status_pending = false;
                    }
                }
                else if (decoded.sync == MIL1553_SYNC_COMMAND_STATUS)
                {
                    // implements: LLR-1553-RT-007: a command/status-sync word where a data word was expected;
                    // legitimate only as the very first word after a receive command: the BC's paired transmit
                    // command to another RT (RT-to-RT); anywhere else it means the message is short, i.e. a word count error
                    mil1553_command_fields_t paired;
                    mil1553_word_unpack_command(decoded.field, &paired);

                    if ((ctx->pending_data_received == 0U) &&
                        (!ctx->pending_is_mode_code) &&
                        (!ctx->pending_is_broadcast) &&
                        (!ctx->pending_is_rt_to_rt) &&
                        (paired.tr_bit == MIL1553_TR_TRANSMIT) &&
                        (paired.rt_address <= MIL1553_RT_ADDR_MAX) &&
                        (paired.rt_address != ctx->own_address))
                    {
                        ctx->pending_is_rt_to_rt = true;
                        ctx->pending_rt_to_rt_status_pending = true;
                        ctx->pending_rt_to_rt_xmit_addr = paired.rt_address;
                        ctx->pending_rt_to_rt_wait_start_us = rt_now(ctx);
                    }
                    else
                    {
                        // The message ended short and a new command followed at once:
                        // report the count error, then give the command its own turn.
                        rt_fail_message(ctx, MIL1553_ERR_WORD_COUNT_MISMATCH);
                        rt_replay(ctx, ctx->pending_channel, &record);
                    }
                }
                else
                {
                    if (ctx->pending_data_received < MIL1553_MAX_DATA_WORDS)
                    {
                        // implements: LLR-1553-RT-003: staged, not written into the subaddress buffer until
                        // every word has arrived.
                        ctx->staging[ctx->pending_data_received] = mil1553_word_unpack_data(decoded.field);
                    }
                    ctx->pending_data_received++;

                    if (ctx->pending_data_received == ctx->pending_data_expected)
                    {
                        ctx->commit_pending = true; // committed when the response goes out, RT-003
                        ctx->message_end_time_us = ctx->pending_last_word_end_us;
                        ctx->state = MIL1553_RT_STATE_RESPOND;
                    }
                    // else: remain in EXECUTE, awaiting more data
                }
            }
            else if (ctx->pending_is_rt_to_rt && ctx->pending_rt_to_rt_status_pending &&
                     ((mil1553_time_us_t)(rt_now(ctx) - ctx->pending_rt_to_rt_wait_start_us) >=
                      (mil1553_time_us_t)MIL1553_T_NORESP_US))
            {
                // implements: LLR-1553-RT-007: the transmitting RT never answered; discard, ME set, no status sent
                rt_fail_message(ctx, MIL1553_ERR_NO_RESPONSE);
            }
            else if ((!ctx->pending_rt_to_rt_status_pending) &&
                     ((mil1553_time_us_t)(rt_now(ctx) - ctx->pending_last_word_end_us) >=
                      (mil1553_time_us_t)MIL1553_T_GAP_MIN_US))
            {
                // implements: LLR-1553-RT-003: the words of one message are contiguous; a
                // gap with words still owed means the message is shorter than commanded.
                rt_fail_message(ctx, MIL1553_ERR_WORD_COUNT_MISMATCH);
            }
            else
            {
                // the next word is not here yet; remain in EXECUTE
            }
        }
        else
        {
            ctx->message_end_time_us = ctx->pending_last_word_end_us;
            ctx->state = MIL1553_RT_STATE_RESPOND;
        }
        break;
    }

    case MIL1553_RT_STATE_RESPOND:
    {
        mil1553_time_us_t elapsed;

        // implements: LLR-1553-RT-003: "more" words than commanded is a count error too.
        // Checked on every pass, not only while the delay is running: the words may all
        // have been on the bus before this RT was scheduled. A contiguous extra word
        // belongs to this message; a later one belongs to the next message and is kept.
        if (ctx->commit_pending && (!ctx->replay_valid))
        {
            mil1553_bus_record_t extra;

            if (rt_next_word(ctx, ctx->pending_channel, &extra))
            {
                if (extra.timestamp_us < (mil1553_time_us_t)(ctx->pending_last_word_end_us + MIL1553_T_GAP_MIN_US))
                {
                    rt_fail_message(ctx, MIL1553_ERR_WORD_COUNT_MISMATCH);
                    break;
                }
                rt_replay(ctx, ctx->pending_channel, &extra);
            }
        }

        elapsed = (mil1553_time_us_t)(rt_now(ctx) - ctx->message_end_time_us);

        if (elapsed < (mil1553_time_us_t)ctx->response_delay_us)
        {
            // still within the mandated delay window, keep waiting
        }
        else if (ctx->pending_is_broadcast)
        {
            // implements: LLR-1553-RT-006/009: no transmission, BCR latched for the next real response
            if (ctx->commit_pending) { rt_commit_received(ctx); }
            ctx->broadcast_received_latched = true;
            rt_finish_message(ctx);
        }
        else
        {
            if (ctx->commit_pending) { rt_commit_received(ctx); }
            rt_build_and_send_status(ctx, false, ctx->pending_data_to_send);
            rt_finish_message(ctx);
        }
        break;
    }

    case MIL1553_RT_STATE_ERROR:
    {
        if (ctx->pending_transmission_error)
        {
            // implements: LLR-1553-RT-010: ME set, transmit nothing
            ctx->message_error_latched = true;
            ctx->pending_transmission_error = false;
            rt_finish_message(ctx);
        }
        else
        {
            // implements: LLR-1553-RT-011: illegal command, valid transmission - status word (no data) after delay
            mil1553_time_us_t elapsed;

            if (ctx->message_end_time_us == 0U)
            {
                ctx->message_end_time_us = ctx->pending_last_word_end_us;
            }
            elapsed = (mil1553_time_us_t)(rt_now(ctx) - ctx->message_end_time_us);

            if (elapsed < (mil1553_time_us_t)ctx->response_delay_us)
            {
                // keep waiting
            }
            else
            {
                ctx->message_error_latched = true;
                if (!ctx->pending_is_broadcast) { rt_build_and_send_status(ctx, true, 0U); } // a broadcast is never answered, LLR-1553-RT-009
                rt_finish_message(ctx);
            }
        }
        break;
    }

    case MIL1553_RT_STATE_BUSY:
    {
        // implements: LLR-1553-RT-012: status with BUSY set, no data
        mil1553_time_us_t elapsed;

        if (ctx->message_end_time_us == 0U)
        {
            ctx->message_end_time_us = ctx->pending_last_word_end_us;
        }
        elapsed = (mil1553_time_us_t)(rt_now(ctx) - ctx->message_end_time_us);

        if (elapsed < (mil1553_time_us_t)ctx->response_delay_us)
        {
            // keep waiting
        }
        else
        {
            if (!ctx->pending_is_broadcast) { rt_build_and_send_status(ctx, true, 0U); } // a broadcast is never answered, LLR-1553-RT-009
            rt_finish_message(ctx);
        }
        break;
    }

    default:
        // RESET/IDLE handled above; unreachable here
        break;
    }

    return true;
}

mil1553_rt_state_t rt_get_state(const mil1553_rt_context_t *ctx)
{
    return ctx->state;
}

void rt_get_health(const mil1553_rt_context_t *ctx, mil1553_rt_health_t *out_health)
{
    out_health->state = ctx->state;
    out_health->busy = ctx->subsystem_busy;
    out_health->message_error = ctx->message_error_latched;
    out_health->fail_safe = (ctx->state == MIL1553_RT_STATE_FAIL_SAFE);
}