/*
    project: MIL1553
    purpose: RT command reception/validation/execution, status word construction, mode code dispatch,
    broadcast handling, response timing, and the RT state machine

    realizes: LLR-1553-RT-001 .. LLR-1553-RT-013
    parent HLRs: HLR-1553-RT-001 .. HLR-1553-RT-007, HLR-1553-BIT-004

    design note (SDD 4.7): same explicit transition table approach as bc_scheduler, for the same coverage
    overlay reason (LLR-1553-RT-013)

    MISRA C:2012 notes:
        - fixed capacity subaddress table and data buffers, no dynamic memory
        - rt_run is a single step function, called repeatedly by main_rt's loop, mirroring bc_run_frame's design (bc_scheduler.h)
*/

#ifndef MIL1553_RT_SERVICE_H
#define MIL1553_RT_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"
#include "bus_model.h"
#include "bit_cbit.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT_SUBADDRESS_COUNT (32U)

// implements: LLR-1553-RT-002 (subaddress table entry)
typedef struct {
    bool    enabled;
    bool    allow_receive;   // BC-to-RT (T/R=receive) permitted on this subaddr
    bool    allow_transmit;  // RT-to-BC (T/R=transmit) permitted on this subaddr
    uint8_t min_word_count;  // logical 1-32
    uint8_t max_word_count;  // logical 1-32
} mil1553_rt_subaddr_config_t;

// data held per subaddress, LLR-1553-RT-003/RT-004
typedef struct {
    mil1553_data_t words[MIL1553_MAX_DATA_WORDS];
    uint8_t         word_count; // words currently committed/valid
} mil1553_rt_subaddr_buffer_t;

typedef struct {
    mil1553_rt_state_t state;
    bool    busy;
    bool    message_error;
    bool    fail_safe;
} mil1553_rt_health_t;

typedef struct {
    mil1553_rt_addr_t own_address;
    mil1553_terminal_id_t own_terminal_id; // MIL1553_TERMINAL_RT1 or RT2
    mil1553_bus_model_context_t *bus;
    mil1553_bit_cbit_context_t  *cbit; // optional (NULL = CBIT not attached), LLR-1553-BIT-005/006

    mil1553_rt_subaddr_config_t  subaddr_config[RT_SUBADDRESS_COUNT];
    mil1553_rt_subaddr_buffer_t  subaddr_buffer[RT_SUBADDRESS_COUNT];

    uint8_t response_delay_us; // LLR-1553-RT-005: default 8, range 4-12

    mil1553_rt_state_t state;  // LLR-1553-RT-013

    // latched status conditions, LLR-1553-RT-006
    bool message_error_latched;
    bool broadcast_received_latched;
    bool subsystem_busy;        // set via rt_set_busy
    bool subsystem_flag;        // set via rt_set_subsystem_flag
    bool terminal_flag;         // set via BIT result, rt_report_bit_result

    // in-progress command/message state
    mil1553_command_fields_t pending_cmd;
    mil1553_channel_t         pending_channel;
    bool                       pending_is_broadcast;
    bool                       pending_is_mode_code;
    bool                       pending_transmission_error; // LLR-1553-RT-010 path
    mil1553_cmd_validation_t   pending_validation;          // LLR-1553-RT-002 result
    uint8_t                    pending_data_expected;        // words still to receive
    uint8_t                    pending_data_received;
    uint8_t                    pending_data_to_send;         // words to transmit in RESPOND

    // RT-to-RT receiving-RT tracking, LLR-1553-RT-007. After a receive command, a COMMAND_STATUS-sync word (instead of a DATA-sync word)
    // as the very first following word is the BC's paired transmit command to another RT; the RT then waits for that RT's status
    // word before accepting the data words
    bool                       pending_is_rt_to_rt;
    bool                       pending_rt_to_rt_status_pending; // awaiting xmit RT status
    mil1553_rt_addr_t          pending_rt_to_rt_xmit_addr;
    mil1553_time_us_t          pending_rt_to_rt_wait_start_us;
    mil1553_time_us_t          message_end_time_us;           // end of last received word

    mil1553_error_record_t error_log[MIL1553_ERROR_LOG_CAPACITY]; // LLR-1553-ERR-004
    uint8_t  error_log_count;
    uint32_t error_log_overflow_count;
} mil1553_rt_context_t;

/*
    implements: LLR-1553-RT-001 .. LLR-1553-RT-013

    initializes the RT context; validates response_delay_us is in [MIL1553_T_RESP_MIN_US, MIL1553_T_RESP_MAX_US];
    on failure returns false and does not change ctx->state, own_address must be <= MIL1553_RT_ADDR_MAX; state is set to
    MIL1553_RT_STATE_RESET; the caller must call rt_pbit_complete to advance out of RESET, per LLR-1553-RT-013
    (RESET -> IDLE on PBIT pass, -> FAIL_SAFE on fail)
*/
bool rt_init(mil1553_rt_context_t *ctx, mil1553_rt_addr_t own_address, mil1553_terminal_id_t own_terminal_id,
             const mil1553_rt_subaddr_config_t subaddr_config[RT_SUBADDRESS_COUNT],
             uint8_t response_delay_us,
             mil1553_bus_model_context_t *bus);

/*
    implements: LLR-1553-BIT-005, LLR-1553-BIT-006

    attaches a CBIT context; when attached, rt_run reports every received word classification to it and runs
    the watchdog tick/kick once per call; pass NULL to detach
*/
void rt_attach_cbit(mil1553_rt_context_t *ctx, mil1553_bit_cbit_context_t *cbit);

// implements: LLR-1553-RT-013 (RESET -> IDLE or FAIL_SAFE)
void rt_pbit_complete(mil1553_rt_context_t *ctx, bool passed);

// implements: LLR-1553-RT-013 (IDLE -> WAIT_COMMAND)
bool rt_enable(mil1553_rt_context_t *ctx);

// implements: LLR-1553-RT-013 (any state -> RESET, e.g. reset mode command)
void rt_reset(mil1553_rt_context_t *ctx);

// subsystem interface, LLR-1553-RT-003/004/006/012
void rt_set_busy(mil1553_rt_context_t *ctx, bool busy);
void rt_set_subsystem_flag(mil1553_rt_context_t *ctx, bool flag);

/*
    implements: LLR-1553-RT-013 (any state -> FAIL_SAFE on fatal BIT result), LLR-1553-BIT-004 (TF/SF reporting)

    called by the BIT modules (bit_pbit/bit_cbit/bit_ibit) to report a result; a fatal result forces FAIL_SAFE immediately, overriding
    whatever rt_run was doing; a non-fatal result sets the subsystem flag equivalent (terminal_flag/subsystem_flag per caller's choice via the
    two booleans) without changing state
*/
void rt_report_bit_result(mil1553_rt_context_t *ctx, bool fatal, bool set_terminal_flag, bool set_subsystem_flag);

/*
    implements: LLR-1553-RT-001 .. LLR-1553-RT-013

    advances the RT by one step, performing at most one state transition's worth of work per call (mirrors bc_run_frame); the caller
    (main_rt's loop) calls this repeatedly; returns false only when called while in RESET or IDLE (caller error: must call rt_pbit_complete / rt_enable first)
*/
bool rt_run(mil1553_rt_context_t *ctx);

// implements: LLR-1553-RT-013
mil1553_rt_state_t rt_get_state(const mil1553_rt_context_t *ctx);

// implements: LLR-1553-RT-003/004/012/013
void rt_get_health(const mil1553_rt_context_t *ctx, mil1553_rt_health_t *out_health);

#ifdef __cplusplus
}
#endif

#endif 