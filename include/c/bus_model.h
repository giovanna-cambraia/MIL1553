/*
    project: MIL1553
    purpose: software dual-redundant bus model: two independent channels, virtual clock, transport records

    realizes: LLR-1553-BUS-001, LLR-1553-BUS-002, LLR-1553-BUS-003
    parent: HLR-1553-BUS-001

    NOTE ON SEQUENCING: bc_scheduler (Increment I2) is defined against this interface before bus_model's own body (bus_model.c)
    is written; this header is the real, final public interface per SDD 4.5 /design/bus_transport_contact.md; only the .c
    implementation is deferred to its own step; the internal queue representation below is concrete (not opaque) because
    MISRA C:2012 / no-dynamic-memory discipline require callers to be able to allocate the context on the stack or in static storage;
    callers must still only touch it through the functions declared here (SDD 4.8's "treceive-only subset" restriction for
    bm_monitor, LLR-1553-BM-003, is enforced by a *separate* header, bus_model_monitor_if.h, not by hiding fields here)

    MISRA C:2012 notes:
        - fixed-capacity queues, no dynamic memory 
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


/*
    mil1553_bus_record_t, mil1553_bus_channel_state_t, mil1553_bus_model_context_t, BUS_MODEL_QUEUE_CAPACITY and 
    BUS_MODEL_CHANNEL_COUNT are defined in bus_model_types.h, shared with bus_model_monitor_if.h (see that file's
    header comment / SDD 4.8)

    implememts: LLR-1553-BUS-001, LLR-1553-BUS-003
*/
void bus_model_init(mil1553_bus_model_context_t *ctx);

/*
    implements: LLR-1553-BUS-001, LLR-1553-BUS-002

    enqueues one word (already Manchester-encoded symbols) onto the named channel, timestamped from the virtual clock, visible to
    every other connected terminal/monitor on that channel; returns false (and enqueues nothing) if the channel is marked or the
    queue is full (queue-full is a design-capacity error, logged by the caller as MIL1533_ERR_TRANSPORT, not a protocol-level error)

    this is the interposition point for error_inject's word-corrupting and response suppression/delay fault primitives in the test build
    (SDD 4.5); the release build contains no hook here
*/

bool bus_model_transmit(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, const mil1553_symbols_t symbols,
                        mil1553_terminal_id_t sender);

/* 
    implements: LLR-1553-BUS-001, LLR-1553-BUS-002

    non-blocking poll: dequeues the oldest unread record on the named channel into *out_record; returns false if the channeç
    is empty or failed
*/
bool bus_model_receive(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, mil1553_bus_record_t *out_record);

// implements: LLR-1553-BUS-001
void bus_model_set_channel_failed(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, bool failed);

// implements: LLR-1553-BUS-001
bool bus_model_is_channel_failed(const mil1553_bus_model_context_t *ctx, mil1553_channel_t channel);

// implements: LLR-1553-BUS-003
mil1553_time_us_t bus_model_now_us(const mil1553_bus_model_context_t *ctx);

/*
    implements: LLR-1553-BUS-003

    advances the virtual clock by delta_us; the only sactioned way the clock moves (LLR-1553-BUS-003: "advanced only by 
    transmitted word durations and explicit waits"); callers (bs_scheduler, rt_service, the HSI test harness driving
    same-language runs) call this after each word transmission/wait rather than reading the host clock 
*/
void bus_model_advances_us(mil1553_bus_model_context_t *ctx, mil1553_time_us_t delta_us);

#ifdef __cplusplus
}
#endif

#endif