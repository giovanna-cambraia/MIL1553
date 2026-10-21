/*
    project: MIL1553
    purpose: small helpers shared by the C unit tests: build encoded words, push words onto the software bus as a named sender, and read them back as a named listener

    test-only code; naming ruled used by every test: a test that verifies LLR-1553-XXX-NNN is named test_llr_1553_xxx_nnn<case>, so traceability/code_to_test.csv can be
    generated from the test names
*/

#ifndef MIL1553_TEST_SUPPORT_H
#define MIL1553_TEST_SUPPORT_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "mil1553_types.h"
#include "mil1553_word.h"
#include "bus_model.h"

static inline void ts_cmd(mil1553_symbols_t out, uint8_t rt, mil1553_tr_t tr, uint8_t sub, uint8_t wc_field)
{
    mil1553_command_fields_t c;
    c.rt_address = rt; c.tr_bit = tr; c.subaddress = sub; c.word_count_field = wc_field; 
    (void)mil1553_word_pack_command(&c, out);
}

static inline void ts_status(mil1553_symbols_t out, uint8_t rt, bool me, bool busy, bool sf, bool tf, bool bcr)
{
    mil1553_status_fields_t s;
    (void)memset(&s, 0, sizeof(s));
    s.rt_address = rt; s.message_error = me; s.busy = busy;
    s.subsystem_flag = sf; s.terminal_flag = tf; s.broadcast_received = bcr;
    (void)mil1553_word_pack_status(&s, out);
}

static inline void ts_data(mil1553_symbols_t out, uint16_t value)
{
    mil1553_word_pack_data(value, out);
}

// transmit one word as `sender` and advance the clock by one word time
static inline bool ts_tx(mil1553_bus_model_context_t *bus, mil1553_channel_t ch, mil1553_terminal_id_t sender, const mil1553_symbols_t sym)
{
    bool ok = bus_model_transmit(bus, ch, sym, sender);
    bus_model_advance_us(bus, (mil1553_time_us_t)MIL1553_WORD_TIME_US);
    return ok;
}

static inline bool ts_rx(mil1553_bus_model_context_t *bus, mil1553_channel_t ch, mil1553_terminal_id_t listener, mil1553_bus_record_t *rec)
{
    return bus_model_receive(bus, ch, listener, rec);
}

// decode a symbol buffer: classification, and the 16-bit field and sync 
static inline mil1553_word_class_t ts_decode(const mil1553_symbols_t sym, uint16_t *field, mil1553_sync_t *sync)
{
    mil1553_decoded_word_t d;
    mil1553_word_class_t c = mil1553_word_classify(sym, &d);
    if (field != (uint16_t *)0) { *field = d.field; }
    if (sync != (mil1553_sync_t *)0) { *sync = d.sync; }
    return c;
}

static inline uint8_t ts_symbol(const mil1553_symbols_t s, unsigned i)
{
    return (uint8_t)((s[i / 8U] >> (7U - (i % 8U))) & 1U);
}

#endif