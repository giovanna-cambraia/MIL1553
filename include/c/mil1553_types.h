/*
    project: MIL1553
    purpose: shared enumerations and fixed width type definitions used across all C modules; no behavior, data definitions only
    see design/SDD.md 4.1.

    standard: MISRA C:2012 (+Amendment 2)
    realizes: no llr of its own (SDD 4.1) - supports every WORD/BC/RT/BM/BUS/REDUN/BIT/ERR LLR by giving them a common type vocabulary

    MISRA notes:
        - fixed-width types only (stdint.h), per SDP 3.3
        - all enums explicit backing rationale in comments; C does not let me fix underlying type of plain enum portably under
        MISRA without violating Rule 8.5/Dir 4.6 concerns, so status/result codes that cross the bus transport boundary are
        defined as uint8_t constants instead of enum where wire-exactness matters; enums are used only for in-memory-only
        values never serialized directly
*/

#ifndef MIL1553_H
#define MIL1553_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // project-wide constant (design/timing_budget.md 2, SDD 1)

#define MIL1553_BIT_TIME_US (1U)
#define MIL1553_WORD_TIME_US (20U)
#define MIL1553_T_RESP_MIN_US (4U)
#define MIL1553_T_RESP_MAX_US (12U)
#define MIL1553_T_GAP_MIN_US (4U)
#define MIL1553_NORESP_US (14U)
#define MIL1553_MAX_DATA_WORDS (32U)
#define MIL1553_RT_ADDR_BROADCAST (31U)
#define MIL1553_RT_ADDR_MIN (0U)
#define MIL1553_RT_ADDR_MAX (30U)      // 31 is broadcast, not a real RT
#define MIL1553_SYMBOLS_PER_WORD (40U) // LLR-1553-WORD-006

// error log capacity, LLR-1553-ERR-004
#define MIL1553_ERROR_LOG_CAPACITY (64U)

// default BC redundacy retry threshold, LLR-1553-REDUN-002
#define MIL1553_REDUN_FAIL_THRESHOLD_DEFAULT (3U)

// default RT response delay, LLR-1553-RT-005
#define MIL1553_RT_RESPONSE_DELAY_DEFAULT_US (8U)

    // basic scalar aliases
    typedef uint8_t mil1553_rt_addr_t;    // 0-31, 31 = brodcast
    typedef uint8_t mil1553_subaddr_t;    // 0-31 (00000/11111 = mode)
    typedef uint8_t mil1553_word_count_t; // 0-31 on the wire; 1-32 logical
    typedef uint8_t mil1553_mode_code_t;  // 0-31
    typedef uint16_t mil1553_data_t;      // 16-bit data word payload
    typedef uint64_t mil1553_time_us_t;   // virtual clock, LLR-1553-BUS-003

    // 40 packed Manchester half-bit symbols, LLR-1553-WORD-006/BUS-002
    typedef uint8_t mil1553_symbols_t[5]; // 40 bits packed 8/byte

    // sync type - wire significant, kept as explicit uint8_t constants rather than an enum
    typedef uint8_t mil1553_sync_t;

#define MIL1553_SYNC_COMMAND_STATUS ((mil1553_sync_t)0U)
#define MIL1553_SYNC_DATA ((mil1553_sync_t)1U)
#define MIL1553_SYNC_INVALID ((mil1553_sync_t)2U)

    // T/R bit
    typedef uint8_t mil1553_tr_t;

#define MIL1553_TR_RECEIVE ((mil1553_tr_t)0U)
#define MIL1553_TR_TRANSMIT ((mil1553_tr_t)1U)

    // word classification, LLR-1553-WORD-008
    typedef enum
    {
        MIL1553_WORD_VALID = 0,
        MIL1553_WORD_SYNC_ERROR = 1,
        MIL1553_WORD_MANCHESTER_ERROR = 2,
        MIL1553_WORD_PARITY_ERROR = 3
    } mil1553_word_class_t;

    // command validation result, LLR-1553-RT-002
    typedef enum
    {
        MIL1553_CMD_VALID = 0,
        MIL1553_CMD_ILLEGAL_SUBADDRESS = 1,
        MIL1553_CMD_ILLEGAL_DIRECTION = 2,
        MIL1553_CMD_ILLEGAL_WORD_COUNT = 3
    } mil1553_cmd_validation_t;

    // message type, used by BM reconstruction (LLR-1553-BM-002) and the bus transport contract (design/bus_transport_contract.md 3.1)
    typedef enum
    {
        MIL1553_MSG_BC_TO_RT = 0,
        MIL1553_MSG_RT_TO_BC = 1,
        MIL1553_MSG_RT_TO_RT = 2,
        MIL1553_MSG_MODE_CODE = 3,
        MIL1553_MSG_BROADCOAST = 4
    } mil1553_msg_type_t;

    // bus channel identifier
    typedef uint8_t mil1553_channel_t;

#define MIL1553_CHANNEL_A ((mil1553_channel_t)0U)
#define MIL1553_CHANNEL_B ((mil1553_channel_t)1U)

    // terminal identity, per bus_transport_contract.md 3
    typedef uint8_t mil1553_terminal_id_t;

#define MIL1553_TERMINAL_BC ((mil1553_terminal_id_t)0U)
#define MIL1553_TERMINAL_RT1 ((mil1553_terminal_id_t)1U)
#define MIL1553_TERMINAL_RT2 ((mil1553_terminal_id_t)2U)
#define MIL1553_TERMINAL_BM ((mil1553_terminal_id_t)3U)

    // status word bit positions (LLR-1553-WORD-003 layout)
    typedef struct
    {
        mil1553_rt_addr_t rt_address;
        bool message_error;   // ME  - bit 10 
        bool instrumentation; // INSTR - bit 9
        bool service_request; // SR  - bit 8
        // bits 7..5 reserved, always 0 on encode
        bool broadcast_received; // BCR - bit 4
        bool busy;               // BUSY - bit 3
        bool subsystem_flag;     //SF  - bit 2 
        bool dbc_acceptance;     // DBCA - bit 1
        bool terminal_flag;      // TF  - bit 0
        bool reserved_nonzero;   // set by unpack if bits 7..5 were nonzero
    } mil1553_status_fields_t;

    // command word fields (LLR-1553-WORD-001 layout)
    typedef struct {
        mil1553_rt_addr_t rt_address;
        mil1553_tr_t tr_bit;
        mil1553_subaddr_t subaddress; // 0-31; 0 or 31 = mode indicator 
        mil1553_word_count_t word_count_field; // raw wire value, 0-31
    } mil1553_command_fields_t;

    // BIT levels and result, LLR-1553-BIT-008/009
    typedef enum {
        MIL1553_BIT_LEVEL_PBIT = 0,
        MIL1553_BIT_LEVEL_CBIT = 1,
        MIL1553_BIT_LEVEL_IBIT = 2
    } mil1553_bit_level_t;

    typedef enum {
        MIL1553_BIT_STATE_POWER_UP = 0,
        MIL1553_BIT_STATE_PBIT = 1,
        MIL1553_BIT_STATE_OPERATIONAL = 2,
        MIL1553_BIT_STATE_CBIT_RUNNING = 3,
        MIL1553_BIT_STATE_IBIT_RUNNING = 4,
        MIL1553_BIT_STATE_DEGRADED = 5,
        MIL1553_BIT_STATE_FAIL = 6
    } mil1553_bit_state_t;

    // BC state, LLR-1553-BC-010
    typedef enum {
        MIL1553_BC_STATE_IDLE = 0,
        MIL1553_BC_STATE_MAJOR_FRAME = 1,
        MIL1553_BC_STATE_MINOR_FRAME = 2,
        MIL1553_BC_STATE_WAIT_RESPONSE = 3,
        MIL1553_BC_STATE_TIMEOUT = 4,
        MIL1553_BC_STATE_FAILOVER = 5,
        MIL1553_BC_STATE_DEGRADED = 6
    } mil1553_bc_state_t;

    // RT state, LLR-1553-RT-013
    typedef enum {
        MIL1553_RT_STATE_RESET = 0,
        MIL1553_RT_STATE_IDLE = 1,
        MIL1553_RT_STATE_WAIT_COMMAND = 2,
        MIL1553_RT_STATE_VALIDATE = 3,
        MIL1553_RT_STATE_EXECUTE = 4,
        MIL1553_RT_STATE_RESPOND = 5,
        MIL1553_RT_STATE_ERROR = 6,
        MIL1553_RT_STATE_BUSY = 7,
        MIL1553_RT_STATE_FAIL_SAFE = 8
    } mil1553_rt_state_t;

    // error type, LLR-1553-ERR-001..004
    typedef enum {
        MIL1553_ERR_PARITY = 0,
        MIL1553_ERR_SYNC = 1,
        MIL1553_ERR_MANCHESTER = 2,
        MIL1553_ERR_NO_RESPONSE = 3,
        MIL1553_ERR_STATUS_ADDRESS_MISMATCH = 4,
        MIL1553_ERR_STATUS_ME = 5,
        MIL1553_ERR_STATUS_BUSY = 6,
        MIL1553_ERR_STATUS_SF = 7,
        MIL1553_ERR_STATUS_TF = 8,
        MIL1553_ERR_WORD_COUNT_MISMATCH = 9,
        MIL1553_ERR_MINOR_FRAME_OVERRUN = 10,
        MIL1553_ERR_TRANSPORT = 11,
    } mil1553_error_type_t;

    // error log record, LLR-1553-ERR-004
    typedef struct {
        mil1553_time_us_t timestamp_us;
        mil1553_terminal_id_t terminal;
        mil1553_channel_t channel;
        mil1553_error_type_t error_type;
        uint32_t message_id;
    } mil1553_error_record_t;

    // fault classes, LLR-1553-ERR-006/008 (test build only; types are defined unconditionaly here since a type definition alone carries no executable behavior and
    // does not violate LLR-1553-ERR-005; only error_inject's *implementation* is release-excluded, per SDD 4.10)

    typedef enum {
        MIL1553_FAULT_PARITY_BIT_FLIP = 0,
    MIL1553_FAULT_CORRUPT_SYNC = 1,
    MIL1553_FAULT_SUPPRESS_RESPONSE = 2,
    MIL1553_FAULT_DELAY_RESPONSE = 3,
    MIL1553_FAULT_WRONG_WORD_COUNT = 4,
    MIL1553_FAULT_STATUS_BIT_INJECT = 5,
    MIL1553_FAULT_ILLEGAL_COMMAND = 6,
    MIL1553_FAULT_BUS_A_FAIL = 7,
    MIL1553_FAULT_BUS_B_FAIL = 8
} mil1553_fault_class_t;

#ifdef __cplusplus
}
#endif

#endif 