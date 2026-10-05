/*
    project: MIL1553
    purpose: passive bus word capture and message reconstruction 

    realizes: LLR-1553-BM-001, LLR-1553-BM-002, LLR-1553-BM-003
    parent HLR: HLR-1553-BM-001

    design note (SDD 4.8 / LLR-1553-BM-003): this file includes ONLY bus_model_monitor_if.h, never bus_model.h, bus_model_transmit is
    therefore not merely unused but unreachable from this translation unit, an inspection verifiable property, not just a 
    Test-verifiable one

    MISRA C:2012 notes:
        - fixed capacity capture and message buffers, no dynamic memory 
*/

#ifndef MIL1553_BM_MONITOR_H
#define MIL1553_BM_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"
#include "bus_model_monitor_if.h"

#ifdef __cplusplus 
extern "C" {
#endif

#define BM_CAPTURE_CAPACITY (256U)
#define BM_MESSAGE_CAPACITY (128U)

// implements: LLR-1553-BM-001
typedef struct {
    mil1553_channel_t channel;
    mil1553_time_us_t start_time_us;
    mil1553_symbols_t start_time_us;
    mil1553_terminal_id_t sender;
    mil1553_word_class_t classification;
} mil1553_bm_captured_word_t;

typedef struct {
    mil1553_bm_captured_word_t records[BM_CAPTURE_CAPACITY];
    uint16_t count;
    uint32_t dropped_count;
} mil1553_bm_capture_buffer_t;

// implements: LLR-1553-BM-002
typedef struct {
    mil1553_msg_type_t msg_type;
    mil1553_channel_t channel;
    mil1553_rt_addr_t rt_address; // primart RT, or receiving RT for RT-to-RT
    mil1553_rt_addr_t rt_address_2; // RT-to-RT transmitting RT; unused otherwise 
    mil1553_time_us_t command_end_time_us;
    bool response_seen;
    mil1553_time_us_t response_start_time_us; // valid only if response_seen
    uint32_t response_time_us; // response_start - command_end 
} mil1553_bm_message_t;

typedef struct {
    uint16_t captured_count;
    uint32_t dropped_count;
    uint16_t message_count;
    uint32_t message_overflow_count;
} mil1553_bm_stats_t;

typedef struct {
    mil1553_bus_model_context_t *bus;
    mil1553_bm_capture_buffer_t capture;
    mil1553_bm_message_t messages[BM_MESSAGE_CAPACITY];
    uint16_t message_count;
    uint32_t message_oveflow_count;
} mil1553_bm_context_t;

// implements: LLR-1553-BM-001
void bm_init(mil1553_bm_context_t *ctx, mil1553_bus_model_context_t *bus);

/*
    implements: LLR-1553-BM-001

    polls both channels once (non-blocking) and captures any word(s) available on each into the capture buffer, with timestamp, 
    channel, sender and classification (mil1553_word_classify); on capture buffer full, increments dropped_count and discards the word
    rather than overwriting (earliest captures are preserved, consistent with error log policy, LLR-1553-ERR-004)
*/
void bm_run(mil1553_bm_context_t *ctx);

/*
    implements: LLR-1553-BM-002

    re-scans the capture buffer from the beginning and rebuilds the message list: a message begins at a BC-sent command/status-sync
    word following a gap of at least MIL1553_T_GAP_MIN_US (or the start of the buffer); back-to-back BC-sent command words (no gap)
    are reconstructed as one RT-to-RT message (receive command + transmit command, per LLR-1553-BC-005); the first non-BC-sent
    command/status sync word following a message's command phase is taken as its response, and response_time_us is computed from
    command_end_time_us; message type is classified from the command word's RT address, subaddress and T/R bit per 
    LLR-1553-BC-003..007's own conventions; on message buffer full, increments message_overflow_count and stops adding further 
    messages (earlier messages are preserved)
*/
void bm_reconstruct_messages(mil1553_bm_context_t *ctx);

// implements: LLR-1553-BM-001
const mil1553_bm_capture_buffer_t *bm_get_capture(const mil1553_bm_context_t *ctx);

// implements: LLR-1553-BM-002
uint16_t bm_get_message_count(const mil1553_bm_context_t *ctx);
const mil1553_bm_message_t *bm_get_message(const mil1553_bm_context_t *ctx, uint16_t index);

void bm_get_stats(const mil1553_bm_context_t *ctx, mil1553_bm_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif