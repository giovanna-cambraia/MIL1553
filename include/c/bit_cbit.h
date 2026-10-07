/*
    project: MIL1553
    purpose: continous built-in test: parity/protocol conformance observation, per channel health counters, watchdog

    realizes: LLR-1553-BIT-005, LLR-1553-BIT-006
    parent HLR: HLR-1553-BIT-002

    design note: CBIT does not poll the bus itself, LLR-1553-BIT-002 requires it to run "without disturing bus timing," and 
    bc_scheduler/rt_service already classify every word they receive (LLR-1553-WORD-008) as part of normal protocol processing;
    CBIT is therefore an event-driven observer: the caller (bc_scheduler/rt_service) reports each classificarion and response time
    measurement it already computed, via bit_cbit_record_word an bit_cbit_record_response_time, rather than CBIT re-deriving them from
    its own bus poll

    INTEGRATION STATUS: this module is complete and independently testable, but bc_scheduler.c and rt_service.c do not yet call
    bit_cbit_record_word / bit_cbit_record_response_time at their classification and response time points, that wiring is a flagged
    follow-up (a small, mechanical edit to each, not a design change) rather than included automaticaly in this step, since those two 
    files were alreay reviewed as complete modules in their own right
*/

#ifndef MIL1553_BIT_CBIT_H
#define MIL1553_BIT_CBIT_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"
#include "bus_model.h"
#include "bit_types.h"

#ifdef __cplusplus 
extern "C" {
#endif

#define BIT_CBIT_WINDOW_SIZE (32U) // rolling window of recent word outcomes, per channel
#define BIT_CBIT_ERROR_RATIO_THRESHOLD_PERCENT (20U) // configurable default
#define BIT_CBIT_WATCHDOG_PERIOD_US_DEFAULT (10000U) // 10ms default progress period

typedef struct {
    bool outcomes[BIT_CBIT_WINDOW_SIZE]; // true = good, false = error
    uint8_t next_index;
    uint8_t filled_count;
    uint32_t good_count_total;
    uint32_t error_count_total;
    bool unhealthy; // LLR-1553-BIT-006
} mil1553_bit_cbit_channel_t;

typedef struct {
    mil1553_bit_cbit_channel_t channel[BUS_MODEL_CHANNEL_COUNT];

    uint32_t response_time_violations; // LRR-1553-BIT-005
    uint32_t word_count_violations;

    mil1553_time_us_t watchdog_period_us;
    mil1553_time_us_t watchdog_last_progress_us;
    bool watchdog_tripped;
} mil1553_bit_cbit_context_t;

// implements: LLR-1553-BIT-002
void bit_cbit_init(mil1553_bit_cbit_context_t ctx, mil1553_time_us_t watchdog_period_us);

/*
    implements: LLR-1553-BIT-005

    reports the classification of one receive word on the given channel; MIL1553_WORD_VALID counts as good;
    any other classifications counts as an error (parity/sync/Manchester, per LLR-1553-WORD-008) and increments 
    channel's error counter 
*/
void bit_cbit_record_word(mil1553_bit_cbit_context_t *ctx, mil1553_channel_t channel, mil1553_word_class_t classification);

/*
    implements: LLR-1553-BIT-005

    reports a measured response time: increments response_time_violations if outside [MIL1553_T_RES_MIN_US, MIL_1553_T_RES_MAX_US]
*/
void bit_cbit_record_response_time(mil1553_bit_cbit_context_t *ctx, uint32_t measured_us);

// implements: LLR-1553-BIT-005
void bit_cbit_record_word_count_violation(mil1553_bit_cbit_context_t *ctx);

/*
    implements: LLR-1553-BIT-006

    called once per scheduler/service loop iteration (per the LLR's "once per BC frame loop or RT service loop iteration" cadence) with 
    the current time, to re-evaluate each channel's error ratio over its rolling window against BIT_CBIT_ERROR_RATIO_THRESHOLD_PERCENT, and to
    check the watchdog: if now_us, watchdog_last_progress_us exceeds watchdog_period_us, watchdog_tripped is set; the caller must call bit_cbit_watchdog_kick
    each time it makes real progress (per LLR-1553-BIT-006's "has not signaled progress" condition)
*/
void bit_cbit_tick(mil1553_bit_cbit_context_t *ctx, mil1553_time_us_t now_us);

/*
    implements: LLR-1553-BIT-005, LLR-1553-BIT-006, LLR-1553-BIT-008

    rolls the current CBIT observations into result->cbit and result->state: DEGRADED if any channel is unhealthy or the watchdog has trpped (non-fatal, per LLR-1553-BIT-009);
    OPERATIONAL otherwise (only from a non-FAIL state - CBIT never clears a PBIT/IBIT caused FAIL on its own)
*/
void bit_cbit_update_result(const mil1553_bit_cbit_context_t *ctx, mil1553_bit_result_t *result);

#ifdef __cplusplus
}
#endif
 
#endif