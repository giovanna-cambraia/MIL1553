/*
    project: MIL1553
    purpose: receive only subset of the bus_model interface, for bm_monitor exclusively 

    realizes: LLR-1553-BM-003 ("no transmit function reachable from bm_monitor - no call, no reference")

    bm_monitor.c includes THIS header, never bus_model.h bus_model_transmit, bus_model_set_channel_failed and bus_model_is_channel_failed
    are not declared here, so they are not just unused but enreachable from any translation unit that includes only this file,
    making LLR-1553-BM-003 checkable by Inspection (grep the #include list; the symbols are simply not visible) rather than only by Test

    the underlying mil1553_bus_model_context_t is the same object bus_model.h operates on (bus_model_types.h); bm_monitor is given 
    a pointer to the live BC/RT bus context, it just cannot call transmit family functions on it through this header
*/

#ifndef MIL1553_BUS_MODEL_MONITOR_IF_H
#define MIL1553_BUS_MODEL_MONITOR_IF_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"
#include "bus_model_types.h"

#ifdef __cplusplus
extern "C" {
    #endif

    // implements: LLR-1553-BM-001, LLR-1553-BUS-002 (receive side only)
    bool bus_model_receive(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, mil1553_bus_record_t *out_record);

    // implements: LLR-1553-BUS-003
    mil1553_time_us_t bus_model_now_us(const mil1553_bus_model_context_t *ctx);

    #ifdef __cplusplus
}
#endif

#endif