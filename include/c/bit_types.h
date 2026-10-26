/*
    project: MIL1553
    purpose: shared BIT result structure, written by bit_pbit, bit_cbit and bit_ibit, read by rt_service(TF/SF), the transmit BIT word mode code handler, and the health
    monitor API; one structure, three read-only views, per SDD 4.9 / LLR-1553-BIT-008

    realizes: LLR-1553-BIT-008 (result structure), LLR-1553-BIT-009 (states)
    parent HLRs: HLR-1553-BIT-001, HLR-1553-BIT-004
*/

#ifndef MIL1553_BIT_TYPES_H
#define MIL1553_BIT_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// test identifiers within each level, LLR-1553-BIT-001/002/003/005/006/007/008
#define BIT_PBIT_TEST_RAM        (0U)
#define BIT_PBIT_TEST_ROM        (1U)
#define BIT_PBIT_TEST_CODEC      (2U)
#define BIT_PBIT_TEST_TIMER      (3U)
#define BIT_PBIT_TEST_BUS_IDLE   (4U)

#define BIT_CBIT_TEST_PARITY            (0U)
#define BIT_CBIT_TEST_PROTOCOL          (1U)
#define BIT_CBIT_TEST_CHANNEL_HEALTH    (2U)
#define BIT_CBIT_TEST_WATCHDOG          (3U)

#define BIT_IBIT_TEST_LOOPBACK_A    (0U)
#define BIT_IBIT_TEST_LOOPBACK_B    (1U)
#define BIT_IBIT_TEST_CROSS_CHANNEL (2U)

// implements: LLR-1553-BIT-008 (per-level latest outcome)
typedef struct {
    bool               ever_run;
    bool               last_passed;
    uint8_t            failing_test_id; // first failing test of the last run; valid only if !last_passed
    uint32_t           failing_item;    // what failed: RAM offset (PBIT RAM), channel 0/1 (PBIT idle, IBIT loopback)
    uint32_t           failed_mask;     // bit n set = test id n failed in the last run
    mil1553_time_us_t  timestamp_us;
} mil1553_bit_level_result_t;

// implements: LLR-1553-BIT-008, LLR-1553-BIT-009
typedef struct {
    mil1553_bit_level_result_t pbit;
    mil1553_bit_level_result_t cbit;
    mil1553_bit_level_result_t ibit;
    mil1553_bit_state_t         state;               // LLR-1553-BIT-009
    uint32_t                    ignored_event_count; // events not legal in the current state
} mil1553_bit_result_t;

// events that drive the BIT state machine, LLR-1553-BIT-009
typedef enum {
    BIT_EVENT_PBIT_START = 0,
    BIT_EVENT_PBIT_PASS,
    BIT_EVENT_PBIT_FAIL,
    BIT_EVENT_CBIT_BEGIN,
    BIT_EVENT_CBIT_PASS,
    BIT_EVENT_CBIT_NONFATAL,
    BIT_EVENT_IBIT_BEGIN,
    BIT_EVENT_IBIT_PASS,
    BIT_EVENT_IBIT_NONFATAL,
    BIT_EVENT_FATAL
} mil1553_bit_event_t;

// implements: LLR-1553-BIT-009 (initial state POWER_UP, nothing run yet)
void bit_result_init(mil1553_bit_result_t *result);

/*
    implements: LLR-1553-BIT-009

    applies one event; only the transitions of the LLR are legal:
      POWER_UP        --PBIT_START-->  PBIT
      PBIT            --PBIT_PASS-->   OPERATIONAL     --PBIT_FAIL--> FAIL
      OPERATIONAL     --CBIT_BEGIN-->  CBIT_RUNNING    --IBIT_BEGIN--> IBIT_RUNNING
      CBIT_RUNNING    --CBIT_PASS-->   OPERATIONAL     --CBIT_NONFATAL--> DEGRADED
      IBIT_RUNNING    --IBIT_PASS-->   OPERATIONAL     --IBIT_NONFATAL--> DEGRADED
      DEGRADED        --CBIT_PASS-->   OPERATIONAL     --CBIT_NONFATAL--> DEGRADED (stays)
      any except POWER_UP --FATAL-->   FAIL
    any other (state, event) pair changes nothing, is counted in ignored_event_count, and returns false;
    returns true if the event was legal
*/
bool bit_state_apply(mil1553_bit_result_t *result, mil1553_bit_event_t event);

// implements: LLR-1553-BIT-008 / BIT-009: a fatal condition found outside the BIT modules
bool bit_result_report_fatal(mil1553_bit_result_t *result);

/*
    implements: LLR-1553-BIT-008

    true if the current state reflects a fatal BIT condition (FAIL), the condition rt_service's rt_report_bit_result should treat as fatal when wiring BIT into the RT
    state machine (LLR-1553-RT-013)
*/
bool bit_result_is_fatal(const mil1553_bit_result_t *result);

// implements: LLR-1553-BIT-008, true if state is DEGRADED
bool bit_result_is_degraded(const mil1553_bit_result_t *result);

/*
    implements: LLR-1553-BIT-008

    the BIT word returned by the transmit-BIT-word mode command, built from the result structure:
      bit 0  state is FAIL            bit 1  state is DEGRADED
      bit 2  PBIT ran and passed      bit 3  PBIT failed
      bit 4  CBIT ran and passed      bit 5  CBIT failed
      bit 6  IBIT ran and passed      bit 7  IBIT failed
      bits 8..10  failing PBIT test id (0 if PBIT has not failed)
*/
uint16_t bit_result_to_word(const mil1553_bit_result_t *result);

#ifdef __cplusplus
}
#endif

#endif 