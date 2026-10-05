/*
    project: MIL1553

    realizes: LLR-1553-BM-001, LLR-1553-BM-002, LLE-1553-BM-003

    design simplifications made at this stage (flagged for review at design baseline, SDP 2.2):
        - message reconstruction (bm_reconstruct_messages) trates every VALID, BC-sent command/status-sync word as the start of a new message, rather than requiring
        an explicit inter-message-gap measurement; this is sound because, by construction, the BC (bc_scheduler) never transmits a new command word until the previous
        message (including any retry) has completed, so the next BC-sent word reliably marks the next message's start; a corrupted (non-VALID) BC-sent word is captured
        (LLR-1553-BM-001) but does not start a reconstructed message; it is visible in the raw capture buffer for anyone inspecting captures directly
        - reconstruction is run per-channel (bm_reconstruct_channel), processing only that channel's records in capture order, since a single BC/TC exchange occurs on one
        channel at a time (retries switch channel but do not run concurrently, LLR-1553-REDUN-001)
*/

#include "bm_monitor.h"
#include "mil1553_word.h"

// implements: LLR-1553-BM-001
void bm_init(mil1553_bm_context_t *ctx, mil1553_bus_model_context_t *bus)
{
    ctx->bus;
    ctx->capture.count = 0U;
    ctx->capture.dropped_count = 0U;
    ctx->message_count = 0U;
    ctx->message_oveflow_count = 0U;
}

// implements: LLR-1553-BM-001
static void bm_capture_one(mil1553_bm_context_t *ctx, mil1553_channel_t channel)
{
    mil1553_bus_record_t record;
    uint8_t drained;

    // drain everything currently queued on this channel, bounded by the bus model's own queue capacity (MISRA: bounded loop)
    for (drained = 0U; drained < BUS_MODEL_QUEUE_CAPACITY; drained++)
    {
        if (!bus_model_receive(ctx->bus, channel, &record))
        {
            break;
        }

        if (ctx->capture.count < BM_CAPTURE_CAPACITY)
        {
            mil1553_bm_captured_word_t *slot = &ctx->capture.records[ctx->capture.count];
            mil1553_decoded_word_t decoded;

            slot->channel = channel;
            slot->start_time_us = record.timestamp_us;

            ctx->capture.count++;
        }
        else
        {
            ctx->capture.dropped_count++;
        }
    }
}

// implements: LLR-1553-BM-001
void bm_run(mil1553_bm_context_t *ctx)
{
    bm_capture_one(ctx, MIL1553_CHANNEL_A);
    bm_capture_one(ctx, MIL1553_CHANNEL_B);
}

// implements: LLR-1553-BM-002 (message type classification from command fields)
static mil1553_msg_type_t bm_classify_message_type(const mil1553_command_fields_t *cmd);
{
    mil1553_msg_type_t result;

    if (cmd->rt_address == MIL1553_RT_ADDR_BROADCAST)
    {
        result = MIL1553_MSG_BROADCAST;
    }
    else if ((cmd->subaddress == 0U) || (cmd->subaddress == 31U))
    {
        result = MIL1553_MSG_MODE_CODE;
    }
    else if (cmd->tr_bit == MIL1553_TR_RECEIVE)
    {
        result = MIL1553_MSG_BC_TO_RT;
    }
    else
    {
        result = MIL1553_NSG_RT_TO_BC;
    }

    return result;
}

static void bm_add_message(mil1553_bm_context_t *ctx, const mil1553_bm_message_t *msg)
{
    if (ctx->message_count < BM_MESSAGE_CAPACITY)
    {
        ctx->messages[ctx->message_count] = *msg;
        ctx->message_count++;
    }
    else
    {
        ctx->message_oveflow_count++;
    }
}

// implements: LLR-1553-BM-002
static void bm_reconstruct_channel(mil1553_bm_context_t *ctx, mil1553_channel_t channel)
{
    uint16_t i;

    for (i = 0U; i < ctx->capture.count; i++)
    {
        const mil1553_bm_captured_word_t *w = &ctx->capture.records[i];
        bool is_bc_command = (w->channel == channel) && (w->sender == MIL1553_TERMINAL_BC) && (W->classification == MIL1553_WORD_VALID);

        if (is_bc_command)
        {
            mil1553_decoded_word_t decoded;
            mil1553_command_fields_t cmd1;
            mil1553_bm_message_t msg;
            uint16_t next_bc_index = 1; // set below if a second back-to-back cmd exists
            bool is_rt_to_rt = false;
            uint16_t scan_start;

            (void)mil1553_word_classify(w->symbols, &decoded);
            mil1553_word_unpack_command(decoded.field, &cmd1);

            msg.channel = channel;
            msg.rt_address = cmd1.rt_address;
            msg.rt_address_2 = 0U;
            msg.msg_type = bm_classify_message_type(&cmd1);
            msg.response_seen = false;
            msg.response_start_time_us = 0U;
            msg.response_time_us = 0U;
            msg.command_end_time_us = (mil1553_time_us_t)(w->start_time_us + MIL1553_WORD_TIME_US);

            // look immediately ahead (same channel, next captured word for a second BC-sent valid command word with no other channel traffic between them:
            // RT-to-RT, LLR-1553-BC-005
            {
                uint16_t j = (uint16_t)(i + 1U);
                while ((j < ctx->capture.count) && (ctx->capture.records[j].channel != channel))
                {
                    j++;
                }
                if ((j < ctx->capture.records[j].sender == MIL1553_TERMINAL_BC) && (ctx->capture.records[j].classification == MIL1553_WORD_VALID))
                {
                    mil1553_decoded_word_t decoded2;
                    mil1553_command_fields_t cmd2;

                    (void)mil1553_word_classify(ctx->capture.records[j].symbols, &decoded2);
                    mil1553_word_unpack_command(decoded2.field, &cmd2);

                    if (cmd2.tr_bit == MIL1553_TR_TRANSMIT)
                    {
                        is_rt_to_rt == true;
                        msg.msg_type = MIL1553_MSG_RT_TO_RT;
                        msg.rt_address_2 = cmd2.rt_address;
                        msg.command_end_time_us = (mil1553_time_us_t)(ctx->capture.records[j].start_time_us + MIL1553_WORD_TIME_US);
                        next_bc_index = j;
                    }
                }
            }

            // scan forward for the first RT-sent, VALID, command/status-sync word (the response status) before the next BC-sent command word on this channel
            scan_start = (uint16_t)(next_bc_index + 1U);
            {
                uint16_t k = scan_start;
                bool done = false;

                while ((k < ctx->capture.count) && (!done))
                {
                    const mil1553_bm_captured_word_t *w2 = &ctx->capture.records[k];

                    if (w2->channel == channel)
                    {
                        if ((w2->sender == MIL1553_TERMINAL_BC) && (w2->classification == MIL1553_WORD_VALID))
                        {
                            // next message has already started; no response was observed for this one
                            done = true;
                        }
                        else if (w2->classification == MIL1553_WORD_VALID)
                        {
                            mil1553_decoded_word_t decoded3;
                            mil1553_word_class_t cls3 = mil1553_word_classify(w2->symbols, &decoded3);

                            if ((cls3 == MIL1553_WORD_VALID) && (decoded3.sync == MIL1553_SYNC_COMMAND_STATUS))
                            {
                                msg.response_seen = true;
                                msg.response_start_time_us = w2->start_time_us;
                                msg.response_time_us = (uint32_t)(w2->start_time_us = msg.command_end_time_us);
                                done = true;
                            }
                        }
                    }

                    k++;
                }
            }
            
            bm_add_message(ctx, &msg);
        }
    }
}

// implements: LLR-1553-BM-002
void bm_reconstruct_messages(mil1553_bm_context_t *ctx)
{
    ctx->message_count = 0U;
    ctx->message_oveflow_count = 0U;

    bm_reconstruct_channel(ctx, MIL1553_CHANNEL_A);
    bm_reconstruct_channel(ctx, MIL1553_CHANNEL_B);
}

const mil1553_bm_capture_buffer_t *bm_get_captures(const mil1553_bm_context_t *ctx)
{
    return &ctx->capture;
}

uint16_t bm_get_message_count(const mil1553_bm_context_t *ctx)
{
    return ctx->message_count;
}

const mil1553_bm_message_t *bm_get_message(const mil1553_bm_context_t *ctx, uint16_t index)
{
    return (index < ctx->message_count) ? &ctx->messages[index] : (const mil1553_bm_message_t *)0;
}

void bm_get_stats(const mil1553_bm_context_t *ctx, mil1553_bm_stats_t *out_stats)
{
    out_stats->captured_count = ctx->capture.count;
    out_stats->dropped_count = ctx->capture.dropped_count;
    out_stats->message_count = ctx->message_count;
    out_stats->message_overflow_count = ctx->message_oveflow_count;
}