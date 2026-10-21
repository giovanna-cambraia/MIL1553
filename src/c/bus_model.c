/*
    project: MIL1553

    realizes: LLR-1553-BUS-001, LLR-1553-BUS-002, LLR-1553-BUS-003

    see bus_model_types.h for the multicast ring-log design rationale

    design notes:
        - the public bus_model_transmit is NOT defined in this file; this file defines bus_model_transmit_raw (the real enqueue);
        the public entry point is defined by exactly one of two translation units, chosen by the build system, never by a runtime branch:
            release : src/c/bus_model_tx.c - forwards to the raw function
            test    : src/c/errinject/bus_model_tx_hooked.c - applies the error injector's filter, then calls the raw function
        consequently nothing in this file, nor in the release build, can reference error injection (LLR-1553-ERR-005): the release
        object code contains no hook table, no weak symbol and no branch that could reach the injector

    MISRA C:2012 notes:
        - fixed capacity ring log, fixed per-listener cursor arrays, no dynamic memory, bounded loops only
*/

#include "bus_model.h"
#include "bus_model_internal.h"

static mil1553_bus_channel_state_t *bus_model_channel_state(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel)
{
    return (channel == MIL1553_CHANNEL_A) ? &ctx->channel[0] : &ctx->channel[1];
}

static const mil1553_bus_channel_state_t *bus_model_channel_state_const(const mil1553_bus_model_context_t *ctx, mil1553_channel_t channel)
{
    return (channel == MIL1553_CHANNEL_A) ? &ctx->channel[0] : &ctx->channel[1];
}

static uint8_t bus_model_channel_index(mil1553_channel_t channel)
{
    return (uint8_t)((channel == MIL1553_CHANNEL_A) ? 0U : 1U);
}

// implements: LLR-1553-BUS-001, LLR-1553-BUS-003
void bus_model_init(mil1553_bus_model_context_t *ctx)
{
    uint8_t c;
    uint8_t l;

    for (c = 0U; c < BUS_MODEL_CHANNEL_COUNT; c++)
    {
        ctx->channel[c].total_written = 0U;
        ctx->channel[c].failed = false;

        for (l = 0U; l < BUS_MODEL_LISTENER_COUNT; l++)
        {
            ctx->listener_read_count[c][l] = 0U;
            ctx->listener_dropped_count[c][l] = 0U;
            ctx->loopback_read_count[c][l] = 0U;
        }
    }

    ctx->virtual_clock_us = 0U;
    ctx->transport_error_count = 0U;
}

// implements: LLR-1553-BUS-001, LLR-1553-BUS-002
bool bus_model_transmit_raw(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, const mil1553_symbols_t symbols, mil1553_terminal_id_t sender)
{
    mil1553_bus_channel_state_t *ch = bus_model_channel_state(ctx, channel);
    uint32_t insert_index;
    uint8_t k;

    if (ch->failed)
    {
        return false;
    }

    insert_index = ch->total_written % BUS_MODEL_QUEUE_CAPACITY;

    for (k = 0U; k < 5U; k++)
    {
        ch->records[insert_index].symbols[k] = symbols[k];
    }
    ch->records[insert_index].timestamp_us = ctx->virtual_clock_us;
    ch->records[insert_index].sender = sender;

    ch->total_written++;

    return true;
}

/*
    implements: LLR-1553-BUS-001, LLR-1553-BUS-002

    advances listener_id's cursor past its own transmissions and past any records it has irrecoverably fallen behind on, returning the next record it has not seen and did
    not send; bounded by BUS_MODEL_QUEUE_CAPACITY iterations (MISRA: bounded loop; a listener can be behind by at most one full ring's worth of records before being
    snapped forward)
*/
bool bus_model_receive(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, mil1553_terminal_id_t listener_id, mil1553_bus_record_t *out_record)
{
    mil1553_bus_channel_state_t *ch = bus_model_channel_state(ctx, channel);
    uint8_t ch_idx = bus_model_channel_index(channel);
    uint8_t l_idx = (uint8_t)listener_id;
    uint32_t iterations;

    if (ch->failed)
    {
        return false;
    }

    for (iterations = 0U; iterations < BUS_MODEL_QUEUE_CAPACITY; iterations++)
    {
        uint32_t read_pos = ctx->listener_read_count[ch_idx][l_idx];

        if (read_pos >= ch->total_written)
        {
            return false; // caught up: nothing new for this listener
        }

        if ((ch->total_written - read_pos) > BUS_MODEL_QUEUE_CAPACITY)
        {
            // fell behind the ring: the unread span beyond capacity is gone; snap forward and count the loss
            uint32_t behind = (ch->total_written - read_pos) - BUS_MODEL_QUEUE_CAPACITY;
            ctx->listener_dropped_count[ch_idx][l_idx] += behind;
            ctx->listener_read_count[ch_idx][l_idx] = ch->total_written - BUS_MODEL_QUEUE_CAPACITY;
            read_pos = ctx->listener_read_count[ch_idx][l_idx];
        }

        {
            uint32_t slot = read_pos % BUS_MODEL_QUEUE_CAPACITY;
            const mil1553_bus_record_t *rec = &ch->records[slot];

            ctx->listener_read_count[ch_idx][l_idx] = read_pos + 1U;

            if (rec->sender != listener_id)
            {
                uint8_t k;

                out_record->timestamp_us = rec->timestamp_us;
                out_record->sender = rec->sender;
                for (k = 0U; k < 5U; k++)
                {
                    out_record->symbols[k] = rec->symbols[k];
                }
                return true;
            }
            // else: this listener's own transmission, skip - loop again
        }
    }

    return false; // exhausted bound without finding a non-self record
}

// implements: LLR-1553-BUS-001, LLR-1553-BUS-002, LLR-1553-BIT-007
bool bus_model_receive_loopback(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, mil1553_terminal_id_t terminal_id, mil1553_bus_record_t *out_record)
{
    mil1553_bus_channel_state_t *ch = bus_model_channel_state(ctx, channel);
    uint8_t ch_idx = bus_model_channel_index(channel);
    uint8_t l_idx = (uint8_t)terminal_id;
    uint32_t read_pos;

    if (ch->failed)
    {
        return false;
    }

    read_pos = ctx->loopback_read_count[ch_idx][l_idx];

    if (read_pos >= ch->total_written)
    {
        return false;
    }

    if ((ch->total_written - read_pos) > BUS_MODEL_QUEUE_CAPACITY)
    {
        // fell behind: snap forward (no separate drop counter kept for the loopback cursor - IBIT is expected to poll for its own just sent word promptly, so this path
        // is a defensive bound, not a normal occurrence).
        read_pos = ch->total_written - BUS_MODEL_QUEUE_CAPACITY;
    }

    {
        uint32_t slot = read_pos % BUS_MODEL_QUEUE_CAPACITY;
        const mil1553_bus_record_t *rec = &ch->records[slot];
        uint8_t k;

        ctx->loopback_read_count[ch_idx][l_idx] = read_pos + 1U;

        out_record->timestamp_us = rec->timestamp_us;
        out_record->sender = rec->sender;
        for (k = 0U; k < 5U; k++)
        {
            out_record->symbols[k] = rec->symbols[k];
        }
        return true;
    }
}

// implements: LLR-1553-BUS-001
void bus_model_set_channel_failed(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, bool failed)
{
    mil1553_bus_channel_state_t *ch = bus_model_channel_state(ctx, channel);
    uint8_t ch_idx = bus_model_channel_index(channel);

    ch->failed = failed;

    if (failed)
    {
        // a failed channel delivers nothing (LLR-1553-BUS-001): snap every listener's cursor to the current write position so no backlog is delivered once failed, and
        // none is replayed when later restored
        uint8_t l;
        for (l = 0U; l < BUS_MODEL_LISTENER_COUNT; l++)
        {
            ctx->listener_read_count[ch_idx][l] = ch->total_written;
            ctx->loopback_read_count[ch_idx][l] = ch->total_written;
        }
    }
}

// implements: LLR-1553-BUS-001
bool bus_model_is_channel_failed(const mil1553_bus_model_context_t *ctx, mil1553_channel_t channel)
{
    const mil1553_bus_channel_state_t *ch = bus_model_channel_state_const(ctx, channel);
    return ch->failed;
}

uint32_t bus_model_get_listener_dropped_count(const mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, mil1553_terminal_id_t listener_id)
{
    uint8_t ch_idx = bus_model_channel_index(channel);
    return ctx->listener_dropped_count[ch_idx][(uint8_t)listener_id];
}

// implements: LLR-1553-BUS-003
mil1553_time_us_t bus_model_now_us(const mil1553_bus_model_context_t *ctx)
{
    return ctx->virtual_clock_us;
}

// implements: LLR-1553-BUS-003
void bus_model_advance_us(mil1553_bus_model_context_t *ctx, mil1553_time_us_t delta_us)
{
    ctx->virtual_clock_us += delta_us;
}