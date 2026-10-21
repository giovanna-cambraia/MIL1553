/*
    project: MIL1553
    purpose: the raw(un-interposed) transmit primitve; declared here, not in bus_model.h, so that application modules (BC, RT, BM, BIT) cannot call it and bypass
    the public bus_model_transmit entry point

    only bus_model.c (defition) and the two bus_model_tx*.c wrappers (src/c/bus_model_tx.c for release, src/c/errinject/bus_model_tx_hooked.c for the test build)
    may include this header

    realizes: LLR-1553-BUS-001, LLR-1553-BUS-002, LLR-1553-ERR-005
*/

#ifndef MIL1553_BUS_MODEL_INTERNAL_H
#define mil1553_BUS_MODEL_INTERNAL_H

#include <stdbool.h>
#include "mil1553_types.h"
#include "bus_model_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool bus_model_transmit_raw(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, const mil1553_symbols_t symbols, mil1553_terminal_id_t sender);

#ifndef __cplusplus
}
#endif

#endif