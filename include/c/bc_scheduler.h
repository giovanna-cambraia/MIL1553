/*
    project: MIL1553
    purpose: BC major/minor frame scheduling, message issuance for all supported message formats, inter-message gap and
    no-response timeout enforcement, bus A/B retry and failover, and the BC state machine

    realizes: LLR-1553-BC-001 .. LLR-1553-BC-010, LLR-1553-REDUN-001 .. LLR-1553-REDUN-003
    parent HLRs: HLR-1553-BC-001 .. HLR-1553-BC-005, HLR-1553-REDUN-001

    design note (SDD 4.6): the BC state machine (LLR-1553-BC-010) is implemented as an explicit enumerated state (mil1553_bc_state_t,
    mil1553_types.h) plus an explicit transition table/switch in bc_scheduler.c, not implicit control flow - this is what lets the
    state-machine viewer's coverage overlay map directly to code

    MISRA C:2012 notes:
        - fixed capacity schedule table, no dynamic memory
        - bc_run_frame is a single step function, called repeatedly by main_bc's loop; it performs at most one state transition's
        worth of work per call so call frequency (not internal looping) bounds execution time per call
*/

#ifndef MIL1553_BC_SCHEDULER_H
#define MIL1553_BC_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"
#include "bus_model.h"
#include "bit_cbit.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC_MAX_SCHEDULE_ENTRIES (64U)
#define BC_MAX_MINOR_FRAMES (32U)

/*
    implements: LLR-1553-BC-001

    one schedule table entry; for MIL1553_MSG_RT_TO_RT, rt_address is the receiving RT and rt_address_2 is the transmitting RT
    (per LLR-1553-BC-005); rt_address_2/subaddress_2 are unused for every other msg_type; for MIL1553_MSG_MODE_CODE, mode_code
    and has_data_word are used and subaddress/subaddress_2/data_word_count are not applicable (mode indicator subaddress
    00000/11111 is implied by msg_type, per LLR-1553-BC-006)
*/
typedef struct {
    mil1553_msg_type_t msg_type;
    mil1553_rt_addr_t rt_address;
    mil1553_rt_addr_t rt_address_2; // RT_TO_RT transmitting RT only
    mil1553_subaddr_t subaddress;
    mil1553_subaddr_t subaddress_2; // RT_TO_RT transmitting RT only
    uint8_t data_word_count; // logical 1-32; 0 if N/A
    mil1553_mode_code_t mode_code; // valid only if msg_type == MODE_CODE
    bool has_data_word; // mode code data word presence
    bool mode_data_from_bc; // true: BC sends the data word (receive-dir mode code, e.g. Synchronize-with-data);
                            // false: RT returns it in its response (transmit-dir mode code, e.g. Transmit BIT Word); unused if !has_data_word

    uint8_t minor_frame_index;
    uint32_t slot_offset_us; // offset from minor frame start
    mil1553_channel_t preferred_bus;
} mil1553_bc_schedule_entry_t;

typedef struct {
    mil1553_bc_schedule_entry_t entries[BC_MAX_SCHEDULE_ENTRIES];
    uint8_t entry_count;
    uint8_t minor_frame_count;
    uint32_t minor_frame_period_us;
} mil1553_bc_schedule_t;

typedef struct {
    bool degraded;
    mil1553_channel_t primary_bus;
    bool rt_failed[MIL1553_RT_ADDR_MAX + 1U];
} mil1553_bc_health_t;

typedef struct {
    mil1553_bc_schedule_t schedule;
    mil1553_bus_model_context_t *bus;
    mil1553_bit_cbit_context_t  *cbit; // optional (NULL = CBIT not attached), LLR-1553-BIT-005/006

    mil1553_bc_state_t state;
    uint8_t current_minor_frame;
    uint8_t current_entry_index; // index into entries for this minor frame scan
    bool entry_issued[BC_MAX_SCHEDULE_ENTRIES]; // cleared each major frame

    mil1553_channel_t primary_bus;
    uint8_t consecutive_fail_count[BUS_MODEL_CHANNEL_COUNT];
    uint8_t fail_threshold; // LLR-1553-REDUN-002, default 3

    bool retry_used_for_current_message; // LLR-1553-REDUN-001
    mil1553_channel_t current_message_bus;
    mil1553_time_us_t message_start_time_us;
    mil1553_time_us_t last_activity_end_us; // end of the last word seen for the in-flight message (or of the BC's own
                                             // last word); the no-response timeout is measured from here (LLR-1553-BC-009)
    mil1553_time_us_t minor_frame_start_time_us;

    /*
        in-flight message response tracking (WAIT_RESPONSE state); generalizes over BC-to-RT (1 status), RT-to-BC (1 status + N data),
        RT-to-RT (xmit-RT status + N data + recv-RT status), mode code (1 status +/- 1 data), per LLR-1553-BC-003/004/005/006;
        response_stage: 0 = awaiting primary status; 1 = awaiting data word(s); 2 = awaiting secondary status (RT-to-RT only)
    */
    uint8_t current_entry_snapshot_index; // which entries[] this message is
    mil1553_rt_addr_t response_primary_rt; // first status expected from
    mil1553_rt_addr_t response_secondary_rt; // RT-RT only: second status from
    bool response_is_rt_to_rt;
    uint8_t response_stage;
    uint8_t response_data_words_expected;
    uint8_t response_data_words_received;

    bool degraded_indication; // sticky until bc_init; set on bus switch
    bool rt_failed_this_major_frame[MIL1553_RT_ADDR_MAX + 1U]; // LLR-1553-REDUN-003

    mil1553_error_record_t error_log[MIL1553_ERROR_LOG_CAPACITY]; // LLR-1553-ERR-004
    uint8_t error_log_count;
    uint32_t error_log_overflow_count;
} mil1553_bc_context_t;

/*
    implements: LLR-1553-BC-001

    validates a schedule table: every field range check (LLR-1553-WORD-001/002 ranges), no two entries in the same minor frame
    overlap in time (using the worst-case duration per design/timing_budget.md 4), and every entry's slot_offset_us + worst-case
    duration fits within minor_frame_period_us; returns true if the table is valid
*/
bool bc_validate_schedule(const mil1553_bc_schedule_t *schedule);

/*
    implements: LLR-1553-BC-001

    computes the worst-case duration (microseconds) of one schedule entry, per design/timing_budget.md 4 formulas (uses
    entry->data_word_count / has_data_word, not an assumed maximum, so validation is tight to the actual configured message)
*/
uint32_t bc_message_duration_us(const mil1553_bc_schedule_entry_t *entry);

/*
    implements: LLR-1553-BC-001, LLR-1553-REDUN-001, LLR-1553-REDUN-002

    initializes the BC context, validates the schedule (bc_validate_schedule); on failure, ctx->state remains MIL1553_BC_STATE_IDLE
    and this function returns false; fail_threshold of 0 is replaced with MIL1553_REDUN_FAIL_THRESHOLD_DEFAULT
*/
bool bc_init(mil1553_bc_context_t *ctx, const mil1553_bc_schedule_t *schedule, mil1553_bus_model_context_t *bus,
             mil1553_channel_t initial_primary_bus, uint8_t fail_threshold);

/*
    implements: LLR-1553-BIT-005, LLR-1553-BIT-006

    attaches a CBIT context; when attached, bc_run_frame reports every received word classification and response time to it,
    and runs the watchdog tick/kick once per call; pass NULL to detach
*/
void bc_attach_cbit(mil1553_bc_context_t *ctx, mil1553_bit_cbit_context_t *cbit);

// implements: LLR-1553-BC-010 ("start" transition, IDLE -> MAJOR_FRAME)
bool bc_start(mil1553_bc_context_t *ctx);

// implements: LLR-1553-BC-010 ("stop" transition, any state -> IDLE)
void bc_stop(mil1553_bc_context_t *ctx);

/*
    implements: LLR-1553-BC-001 .. LLR-1553-BC-010, LLR-1553-REDUN-001..003

    advances the BC by one step, driven by bus_model_now_us(ctx->bus); performs at most one state transition's worth of work
    (one message issued, one response checked, one timeout declared, etc.) per call; the caller (main_bc's loop) calls this
    repeatedly; returns false only on an unrecoverable internal error (e.g. called while IDLE); normal protocol conditions
    (timeouts, errors) are handled internally and still return true
*/
bool bc_run_frame(mil1553_bc_context_t *ctx);

// implements: LLR-1553-BC-010
mil1553_bc_state_t bc_get_state(const mil1553_bc_context_t *ctx);

// implements: LLR-1553-REDUN-002, LLR-1553-REDUN-003
void bc_get_health(const mil1553_bc_context_t *ctx, mil1553_bc_health_t *out_health);

#ifdef __cplusplus
}
#endif

#endif