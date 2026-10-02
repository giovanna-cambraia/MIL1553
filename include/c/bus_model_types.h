/*
    project: MIL1553
    purpose: struct definitions shared bus_model.h (full BC/RT interface: transmit + receive + channel control) and
             bus_model_monitor_if.h (receive only interface for bm_monitor, LLR-1553-BM-003)

    design note (SDD 4.8 / LLR-1553-BM-003): this split exists so that bm_monitor.c can include bus_model_monitor_if.h alone
    and never see bus_model_transmit's declaration at all - making "BM cannot transmit" checkable by Inspection of #include graphs
    and link symbols, not just by Test
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

typedef struct {
    mil1553_symbols_t symbols;
    mil1553_time_us_t timestamp_us;
    mil1553_terminal_id_t sender;
} mil1553_bus_record_t;

typedef struct {
    mil1553_bus_record_t queue[BUS_MODEL_QUEUE_CAPACITY];
    uint8_t head_index;
    uint8_t count;
    bool failed;
} mil1553_bus_channel_state_t;

typedef struct {
    mil1553_bus_channel_state_t channel[BUS_MODEL_CHANNEL_COUNT]; // [0]=A, [1]=B
    mil1553_time_us_t virtual_clock_us; // LRR-1553-BUS-003
    uint32_t transport_error_count;
} mil1553_bus_model_context_t;

#ifdef __cplusplus
}
#endif

#endif