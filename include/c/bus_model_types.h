/*
    project: MIL1553
    purpose: struct definitions shared by bus_model.h (full BC/RT interface: transmit + receive + channel control) and bus_model_monitor_if.h (receive only interface
    for bm_monitor, LLR-1553-BM-003)

    REVISION NOTE: a 1553 channel is a broadcast medium - a transmitted word must be independently visible to every listener (BC, RT1, RT2, BM), not consumed by 
    whichever one happens to call receive first; the channel is therefore modeled as an append only ring log (total_written monotonically increasing, records overwritten
    oldest-first once the ring wraps) with one independent read cursor per listener (indexed by mil1553_terminal_id_t, which is already a small dense 0..3 range
    in mil1553_types.h: BC, RT1, RT2, BM); each listener advances its own cursor and automatically skips records it sent itself (a terminal does not need to process
    its own transmission); if a listener falls behind the ring by more than its capacity, the unread records are gone for that listener and the loss is counted 
    per-listener (listener_dropped_count), this is a bus-model backpressure concern, independent of bm_monitor's own capture buffer full counter (bm_monitor.h), which
    is a separate, higher layer 

    design note (SDD 4.8 / LLR-1553-BM-003): this split exists so that bm_monitor.c can include bus_model_monitor_if.h alone and never see bus_model_transmit's 
    declaration at all, making "BM cannot transmit" checkable by Inspection of #include graphs and link symbols, not just by Test
*/

#ifndef MIL1553_BUS_MODEL_TYPES_H
#define MIL1553_BUS_MODEL_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUS_MODEL_QUEUE_CAPACITY (64U)
#define BUS_MODEL_CHANNEL_COUNT (2U) // A, B
#define BUS_MODEL_LISTENER_COUNT (4U) // BC, RT1, RT2, BM - see mil1553_terminal_id_t

typedef struct {
    mil1553_symbols_t symbols;
    mil1553_time_us_t timestamp_us;
    mil1553_terminal_id_t sender;
} mil1553_bus_record_t;

typedef struct {
    mil1553_bus_record_t[BUS_MODEL_QUEUE_CAPACITY]; // ring log
    uint32_t total_written; // monotonically increasing count ever written
    bool failed;
} mil1553_bus_channel_state_t;

typedef struct {
    mil1553_bus_channel_state_t channel[BUS_MODEL_CHANNEL_COUNT]; // [0]=A, [1]=B
    uint32_t listener_read_count[BUS_MODEL_CHANNEL_COUNT][BUS_MODEL_LISTENER_COUNT];
    uint32_t listener_dropped_count[BUS_MODEL_CHANNEL_COUNT][BUS_MODEL_LISTENER_COUNT];
    /*
        dedicated loopback cursors, separate from listener_read_count: IBIT (LLR-1553-BIT-007) needs to read back its OWN transmission through the real channel path
        to prove the transmit+channel+receive pipeline words end to end; the normal listener cursor auto-skips self-sent records (bus_model_receive); the loopback
        cursor does not, and is tracked independently so running IBIT never disturbs a terminal's normal traffic read position
    */
   uint32_t loopback_read_count[BUS_MODEL_CHANNEL_COUNT][BUS_MODEL_LISTENER_COUNT];
   mil1553_time_us_t virtual_clock_us; // LRR-1553-BUS-003
   uint32_t transport_error_count;
} mil1553_bus_model_context_t;

#ifdef __cplusplus
}
#endif

#endif


