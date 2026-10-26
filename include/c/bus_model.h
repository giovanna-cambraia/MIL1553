/*
    project: MIL1553
    purpose: software dual-redundant bus model: two independent channels, virtual clock, multicast delivery to every listener (BC, RT1, RT2, BM)

    realizes: LLR-1553-BUS-001, LLR-1553-BUS-002, LLR-1553-BUS-003
    parent: HLR-1553-BUS-001

    see bus_model_types.h for the revision note on why each channel is a multicast ring log with one read cursor per listener, rather than a single consume-once queue:
    a 1553 channel is a broadcast medium, and every connected terminal/monitor must see the same word

    MISRA C:2012 notes:
        - fixed-capacity structures, no dynamic memory
        - bus_model_transmit / bus_model_receive are the only sanctioned access points; error_inject's hook table
        (SDD 4.5, test build only) interposes inside bus_model_transmit's implementation, not in this header
*/

#ifndef MIL1553_BUS_MODEL_H
#define MIL1553_BUS_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"
#include "bus_model_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// implements: LLR-1553-BUS-001, LLR-1553-BUS-003
void bus_model_init(mil1553_bus_model_context_t *ctx);

/*
    implements: LLR-1553-BUS-001, LLR-1553-BUS-002

    writes one word (already Manchester-encoded symbols) into the named channel's ring log, timestamped from the virtual clock, visible independently to every
    listener's own read cursor (bus_model_receive); returns false if the channel is marked failed; the write itself never fails for capacity reasons (the ring
    overwrites its oldest entry once full, a slow listener loses history, tracked per-listener via listener_dropped_count, not a transmit-side failure)

    this is the interposition point for error_inject's word-corrupting and response-suppression/delay fault primitives in the test build (SDD 4.5); the release
    build contains no hook here
*/
bool bus_model_transmit(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, const mil1553_symbols_t symbols, mil1553_terminal_id_t sender);

/*
    implements: LLR-1553-BUS-001, LLR-1553-BUS-002

    non-blocking poll for one listener: returns the next record on the named channel that this listener has not yet read and did not send itself (a terminal's
    own transmissions are automatically skipped on its own cursor, it already knows what it sent); returns false if the channel is failed or the listener has
    caught up to everything available; listener_id must be the caller's own terminal identity (MIL1553_TERMINAL_BC/RT1/RT2/BM), each listener's cursor is tracked
    independently, so BC, RT1, RT2 and BM may all call this on the same channel without interfering with one another
*/
bool bus_model_receive(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, mil1553_terminal_id_t listener_id, mil1553_bus_record_t *out_record);

/*
    implements: LLR-1553-BUS-001

    marks a channel failed/restored; on transition to failed, every listener's read cursor on that channel is snapped forward to the channel's current write
    position (LLR-1553-BUS-001: "a failed channel delivers nothing", no backlog is queued up for delivery once the channel is marked failed, and none is replayed
    when it is restored)
*/
void bus_model_set_channel_failed(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, bool failed);

// implements: LLR-1553-BUS-001
bool bus_model_is_channel_failed(const mil1553_bus_model_context_t *ctx, mil1553_channel_t channel);

/*
    implements: LLR-1553-BUS-001

    returns how many records have been irrecoverably skipped for the named listener on the named channel because it fell behind the ring log by more than
    BUS_MODEL_QUEUE_CAPACITY entries; intended for health/diagnostic use (e.g. CBIT channel health counters, LLR-1553-BIT-006); zero in normal operation since
    every listener in this project polls every step
*/
uint32_t bus_model_get_listener_dropped_count(const mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, mil1553_terminal_id_t listener_id);

/*
    implements: LLR-1553-BUS-001, LLR-1553-BUS-002 (loopback variant for LLR-1553-BIT-007)

    the loopback counterpart of bus_model_receive: returns the next word sent by terminal_id ITSELF (the opposite filter), skipping words sent by other terminals,
    and uses a cursor dedicated to loopback use, independent of terminal_id's normal listener_read_count on this channel; intended for bit_ibit's self-loopback
    test: it must read back its own pattern through the real channel even when other traffic is already queued, which the self-filtering of bus_model_receive
    deliberately prevents for every other caller; words skipped here are only skipped on the loopback cursor, the terminal's normal receive position is untouched
*/
bool bus_model_receive_loopback(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, mil1553_terminal_id_t terminal_id, mil1553_bus_record_t *out_record);

// implements: LLR-1553-BUS-003
mil1553_time_us_t bus_model_now_us(const mil1553_bus_model_context_t *ctx);

/*
    implements: LLR-1553-BUS-003

    advances the virtual clock by delta_us; the only sanctioned way the clock moves (LLR-1553-BUS-003: "advanced only by transmitted word durations and explicit
    waits"); callers (bc_scheduler, rt_service, the HSI test harness driving same-language runs) call this after each word transmission/wait rather than reading
    the host clock
*/
void bus_model_advance_us(mil1553_bus_model_context_t *ctx, mil1553_time_us_t delta_us);

#ifdef __cplusplus
}
#endif

#endif 