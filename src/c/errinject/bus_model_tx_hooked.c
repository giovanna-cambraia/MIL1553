/*
    project: MIL1553
    purpose: TEST-BUILD definition of the public bus_model transmit: runs every transmitted word through the error injector's filter before it reaches the channel

    realizes: LLR-1553-ERR-008 (interposition point, SDD 4.5)

    linked ONLY in the test build, in place of src/c/bus_model_tx.c
*/
#ifndef MIL1553_ENABLE_ERROR_INJECTION
#error "bus_model_tx_hooked.c must not be built in a release configuration (LLR-1553-ERR-005)"
#endif

#include "bus_model.h"
#include "bus_model_internal.h"
#include "error_inject.h"

// implements: LLR-1553-ERR-008
bool bus_model_transmit(mil1553_bus_model_context_t *ctx, mil1553_channel_t channel, const mil1553_symbols_t symbols, mil1553_terminal_id_t sender)
{
    mil1553_symbols_t working;
    bool drop = false;
    uint8_t i;

    for (i = 0U; i < 5U; i++)
    {
        working[i] = symbols[i];
    }

    errinject_filter_transmit(ctx, channel, sender, working, &drop);

    if (drop)
    {
        // the sender believes the word sent out; it never reached the channel (SUPPRESS_RESPONSE / WRONG_WORD_COUNT)
        return true; 
    }

    return bus_model_transmit_raw(ctx, channel, working, sender);
}