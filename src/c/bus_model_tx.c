/*
    project: MIL1553
    purpose: RELEASE definition of the public bus_model transmit; forwards directly to the raw transmit; contains no fault-injection hook of any kind

    realizes: LLR-1553-BUS-001, LLR-1553-BUS-002, LLR-1553-ERR-005

    linked in every build EXCEPT the test build, where src/c/errinject/bus_model_tx_hooked.c is linked in its place; exactly one of the two must be in any given link
*/
#include "bus_model.h"
#include "bus_model_internal.h"

// implements: LLR-1553-BUS-001, LLR-1553-BUS-002
bool bus_model_transmit(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, const mil1553_symbols_t symbols, mil1553_terminal_id_t sender)
{
    return bus_model_transmit_raw(ctx, channel, symbols, sender);
}