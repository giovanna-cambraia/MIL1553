/*
    project: MIL1553
    purpose: continuous built-in test: word and protocol conformance observation, per-channel health counters, scheduler watchdog

    realizes: LLR-1553-BIT-005, LLR-1553-BIT-006 (and the BIT-009 CBIT transitions through bit_cbit_update_result)
    parent HLR: HLR-1553-BIT-002

    design note: CBIT does not poll the bus; LLR-1553-BIT-002 requires it to run "without disturbing bus timing", and bc_scheduler / rt_service already
    classify every word they handle (LLR-1553-WORD-008). They report each classification and response time through bc_attach_cbit / rt_attach_cbit;
    the owner calls bit_cbit_update_result once per CBIT cycle to turn what was observed into a BIT state

    cycle semantics: a cycle "fails" (non-fatal -> DEGRADED) if, since the previous update, any channel is unhealthy, the watchdog tripped, or a new
    response-time or word-count violation was recorded. The next cycle with none of those passes and clears DEGRADED (LLR-1553-BIT-009)

    INTEGRATION STATUS: this module is complete and independently testable, but bc_scheduler.c and rt_service.c do not yet call
    bit_cbit_record_word / bit_cbit_record_response_time at their classification and response-time points; that wiring is a flagged follow-up
    (a small, mechanical edit to each, not a design change) rather than included automatically in this step, since those two files were already
    reviewed as complete modules in their own right
*/

#ifndef MIL1553_BIT_CBIT_H
#define MIL1553_BIT_CBIT_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"
#include "bit_types.h"
#include "bus_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BIT_CBIT_WINDOW_SIZE                    (32U)  // rolling window of recent word outcomes, per channel
#define BIT_CBIT_MIN_SAMPLES                    (8U)   // a channel is judged only once this many words are in the window
#define BIT_CBIT_ERROR_RATIO_THRESHOLD_PERCENT  (20U)  // unhealthy when errors in the window exceed this share
#define BIT_CBIT_WATCHDOG_PERIOD_US_DEFAULT     (10000U) // 10ms default progress period
#define BIT_CBIT_WORD_CLASS_COUNT               (4U)   // mil1553_word_class_t values

typedef struct {
    bool     outcomes[BIT_CBIT_WINDOW_SIZE]; // true = good, false = error
    uint8_t  next_index;
    uint8_t  filled_count;
    uint32_t good_count_total;
    uint32_t error_count_total;
    uint32_t class_count[BIT_CBIT_WORD_CLASS_COUNT]; // per classification: VALID, SYNC, MANCHESTER, PARITY
    bool     unhealthy; // LLR-1553-BIT-006
} mil1553_bit_cbit_channel_t;

typedef struct {
    mil1553_bit_cbit_channel_t channel[BUS_MODEL_CHANNEL_COUNT];

    uint32_t response_time_violations;   // cumulative, LLR-1553-BIT-005
    uint32_t word_count_violations;      // cumulative
    uint32_t response_time_reported;     // value at the previous update
    uint32_t word_count_reported;

    mil1553_time_us_t watchdog_period_us;
    mil1553_time_us_t watchdog_last_progress_us;
    bool                watchdog_armed;       // false until the first kick
    bool                watchdog_stalled;     // currently past its period
    bool                watchdog_tripped;     // latched until bit_cbit_update_result reports it
    uint32_t            watchdog_trip_count;
} mil1553_bit_cbit_context_t;

// implements: LLR-1553-BIT-002; watchdog_period_us of 0 selects the default
void bit_cbit_init(mil1553_bit_cbit_context_t *ctx, mil1553_time_us_t watchdog_period_us);

/*
    implements: LLR-1553-BIT-005

    reports the classification of one received word on the given channel; MIL1553_WORD_VALID counts as good; any other classification counts as an error
    (parity/sync/Manchester, per LLR-1553-WORD-008) and increments the channel's error counter; counted per channel and per classification
*/
void bit_cbit_record_word(mil1553_bit_cbit_context_t *ctx, mil1553_channel_t channel, mil1553_word_class_t classification);

// implements: LLR-1553-BIT-005; outside [4, 12] us is a violation
void bit_cbit_record_response_time(mil1553_bit_cbit_context_t *ctx, uint32_t measured_us);

// implements: LLR-1553-BIT-005
void bit_cbit_record_word_count_violation(mil1553_bit_cbit_context_t *ctx);

// implements: LLR-1553-BIT-006; progress mark, the first call arms the watchdog; does not clear a trip
void bit_cbit_watchdog_kick(mil1553_bit_cbit_context_t *ctx, mil1553_time_us_t now_us);

/*
    implements: LLR-1553-BIT-006

    called once per scheduler / service loop iteration; re-evaluates each channel's error ratio over its window, and trips (latches) the watchdog
    if the loop has not kicked it within its period; does nothing about the watchdog until it has been armed by a kick
*/
void bit_cbit_tick(mil1553_bit_cbit_context_t *ctx, mil1553_time_us_t now_us);

/*
    implements: LLR-1553-BIT-005, LLR-1553-BIT-006, LLR-1553-BIT-008, LLR-1553-BIT-009

    one CBIT cycle: decides pass or non-fatal fail from what was observed since the previous call, drives the BIT state machine
    (OPERATIONAL -> CBIT_RUNNING -> OPERATIONAL / DEGRADED; DEGRADED -> OPERATIONAL on a pass), records the outcome in result->cbit, and
    consumes the watchdog latch and the violation counts it has now reported; in any state where a CBIT cycle is not legal (POWER_UP, PBIT,
    IBIT_RUNNING, FAIL) the cycle is ignored and counted
*/
void bit_cbit_update_result(mil1553_bit_cbit_context_t *ctx, mil1553_bit_result_t *result, mil1553_time_us_t now_us);

#ifdef __cplusplus
}
#endif

#endif 