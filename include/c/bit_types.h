/*
    project: MIL1553
    purpose: shared BIT result structure, written by bit_pbit, bit_cbit and bit_ibit, read by rt_service(TF/SF), the transmit BIT word mode code handler, and the health
    monitor API; one structure, three read-only views, per SDD 4.9 / LLR-1553-BIT-008

    realizes: (types only, behavior realized by bit_pbit/cbit/ibit)
*/

#ifndef MIL1553_BIT_TYPES_H
#define MIL1553_BIT_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// test identifiers within each level, LLR-1553-BIT-001/002/003/008
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
    bool ever_run;
    bool last_passed;
    uint8_t failing_test_id; // valid only if !last_passed
    mil1553_time_us_t timestamp_us;
} mil1553_bit_level_result_t;

// implements: LLR-1553-BIT-008, LLR-1553-BIT-009
typedef struct {
 mil1553_bit_level_result_t pbit;
 mil1553_bit_level_result_t cbit;
 mil1553_bit_level_result_t ibit;
 mil1553_bit_state_t state; // LLR-1553-BIT-009
} mil1553_bit_result_t;

// implements: LLR-1553-BIT-009 (initial state)
void bit_result_init(mil1553_bit_result_t *result);

/*
    implements: LLR-1553-BIT-008

    true if the current state reflects a fatal BIT condition (FAIL), the condition rt_service's rt_report_bit_result should treat as fatal when wiring BIT into the RT
    state machine (LLR-1553-RT-013)
*/
bool bit_result_is_fatal(const mil1553_bit_result_t *result);

// implements: LLR-1553-BIT-008, true if state is DEGRADED
bool bit_result_is_degraded(const mil1553_bit_result_t *result);

#ifdef __cplusplus
}
#endif

#endif