/*
    project: MIL1553
    purpose: scenario-driven fault injection for the software bus (TEST BUILD ONLY)

    realizes: LLR-1553-ERR-006, LLR-1553-ERR-007, LLR-1553-ERR-008, LLR-1553-ERR-009
    parent HLRs: HLR-1553-ERR-005, HLR-1553-006

    CERTIFICATION BOUNDARY (PSAC 8): this framework is excluded from the certified build; three independent guards: 
    1. compile time: every file in src/c/errinject/ fails with #error unless MIL1553_ENABLE_ERROR_INJECTION is defined
    2. link-time: the release build lists none of these files, and its public bus_model_transmit (src/c/bus_model_tx.c) has no hook, so the release object cannot reach
    the injector (LLR-ERR-005)
    3. runtime (this module): inert until errinject_arm() is called with the harness token AND a scenario file whose SHA-256 matches its signature file has been loaded
    (LRR-ERR-006, LLR-ERR-007)

    the harness token (ERRINJECT_HARNESS_TOKEN, supplied with -D by the test build) guards against ACCIDENTAL arming, e.g. a role main calling arm by mistake; it is not
    a security boundary, link-tine exclusion is the real guarantee

    because stdio and string handling are used for scenario loading and result output, this module is not MISRA-claimed (it is outside the certified item); it is still
    written with fixed-size storage and no dynamic memory 

    scenario file schema (config/error_scenarios.yaml): a list of item,
    - id: ERR-001                  required, <= 15 chars
       target: RT1                 required: BC | RT1 | RT2 | BM
       bus: A                      optional: A | B | ANY (default ANY)
       fault: PARITY_BIT_FLIP      required, see below
       word_index: 0               optional (default 0)
       count: 1                    optional (default 1)
       duration_ms: 50             SUPPRESS_RESPONSE window
       delay_us: 20                DELAY_RESPONSE added delay
       bit: BUSY                   STATUS_BIT_INJECT: BUSY | ME | SF | TF
       expected_detection: PARITY  required: an error name, CBIT_RESPONSE_TIME or NONE
       expected_recovery: NONE     optional: NONE | RT_MARKED_FAILED | BUS_FAILOVER
       hlr: HLR-1553-ERR-001       optional traceability fields for the
       llr: LLR-1553-ERR-001         result record
       test_id: TC-ERR-001
*/

#ifndef MIL1553_ERROR_INJECT_H
#define MIL1553_ERROR_INJECT_H

#ifndef MIL1553_ENABLE_ERROR_INJECTION
#error "error_inject is part of the error injection framework and must not be build in a release configuration (LLR-1553-ERR-005)"
#endif

#ifndef ERRINJECT_HARNESS_TOKEN
#error "ERRINJECT_HARNESS_TOKEN must be supplied by the test build (-DERRINJECT_HARNESS_TOKEN=...)"
#endif

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"
#include "bus_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ERRINJECT_MAX_SCENAROS (32U)
#define ERRINJECT_ERR_TYPE_COUNT (12U) // number of mil1553_error_types_t values
#define ERRINJECT_ILLEGAL_SUBADDRESS (30U) // must be disabled in bus_config.yaml

#define ERRINJECT_BUS_A (0U)
#define ERRINJECT_BUS_B (1U)
#define ERRINJECT_BUS_ANY (2U)

typedef enum {
    ERRINJECT_BIT_BUSY = 0;
    ERRINJECT_BIT_ME = 1,
    ERRINJECT_BIT_SF = 2,
    ERRINJECT_BIT_TF = 3
} errinject_status_bit_t;

typedef enum {
    ERRINJECT_REJECT_NONE = 0,
    ERRINJECT_REJECT_BAD_TOKEN = 1,
    ERRINJECT_REJECT_NOT_LOADED = 2,
    ERRINJECT_REJECT_SIG_MISSING = 3,
    ERRINJECT_REJECT_SIG_MISMATCH = 4,
    ERRINJECT_REJECT_YAML_MISSING = 5,
    ERRINJECT_REJECT_YAML_TOO_LARGE = 6,
    ERRINJECT_REJECT_YAML_PARSE = 7,
    ERRINJECT_REJECT_NOT_ARMED = 8
} errinject_reject_reason_t;

typedef struct {
    char id[16];
    mil1553_terminal_id_t target;
    uint8_t bus_sel; // ERRINJECT_BUS_*
    mil1553_fault_class_t fault;
    uint32_t word_index;
    uint32_t count;
    uint32_t duration_us;
    uint32_t delay_us;
    errinject_status_bit_t status_bit;
    char expected_detection[28];
    char expected_recovery[24];
    char hlr[24];
    char llr[24];
    char test_id[24];
} errinject_scenario_t;

// what the compaigns harness observed while a scenario ran
typedef struct {
    uint32_t error_type_count[ERRINJECT_ERR_TYPE_COUNT];
    uint32_t cbit_response_time_violations;
    bool failover_occurred; // BC switched its primary bus
    bool rt_marked_failed; // BC marked an RT failed
} errinject_observation_t;

// implements: LLR-1553-ERR-009 (result record)
typedef struct {
    char scenario_id[16];
    mil1553_time_us_t timestamp_us; // first injection (or scenario start if none)
    mil1553_terminal_id_t terminal;
    char bus[4];
    char fault[24];
    char expected[80];
    char observed[128];
    bool pass;
    char hlr[24];
    char llr[24];
    char test_id[24];
} errinject_result_t;

// arming, LLR-1553-ERR-006

// arms the injector if token equals ERRINJECT_HARNESS_TOKEN, and a signed scenario file has been loaded; otherwise stays inert, records the rejection 
// (errinject_reject_count / errinject_last_reject) and returns false
bool errinject_arm(uint32_t token);
void errinject_disarm(void);
bool errinject_is_armed(void);

// scenario loading, LLR-1553-ERR-007

// reads yaml_path once into a fixed buffer, checks its SHA-256 against sig_path, and only then parses the bytes; on any failure (missing file, missing/mismatched
// signature, parse error) no scenario is loaded, the injector stays inert, and the reason is recorded
bool errinject_load_scenarios(const char *yaml_path, const char *sig_path);
uint32_t errinject_scenario_count(void);
const errinject_scenario_t *errinject_get_scenario(uint32_t index);

// register the configured RT address of an RT terminal (needed by ILLEGAL_COMMAND)
void errinject_register_rt_address(mil1553_terminal_id_t, mil1553_rt_addr_t rt_address);

// scenario execution, LLR-1553-ERR-008 / ERR-009
// starts scenario `index` (one at a time); BUS_A_FAIL/BUS_B_FAIL act immediately 
bool errinject_begin_scenario(uint32_t index, mil1553_bus_model_context_t *bus);

// ends the active scenario, restores any failed channel, compares the observation with the scenario's expectation and appends a result record; a scenario whose fault
// was never applied, or that expected a detection and saw none, is recorded as a FAILURE (never a pass by default); returns the pass/fail of this scenario
bool errinject_end_scenario(mil1553_bus_model_context_t *bus, const errinject_observation_t *obs);

void errinject_observation_clear(errinject_observation_t *obs);
void errinject_observation_add_log(errinject_observation_t *obs, const mil1553_error_record_t *records, uint32_t record_count);

// the interposition point used by bus_model_tx_hooded.c; may modify symbols in place or ask for word to be dropped
void errinject_filter_transmit(mil1553_bus_model_context_t *bus, mil1553_channel_t channel, mil1533_terminal_id_t sender, mil1553_symbols_t symbols, bool *drop);

// results and diagnostics
uint32_t errinject_result_count(void);
const errinject_result_t *errinject_get_result(uint32_t index);
bool errinject_write_results_json(const char *path);

uint32_t errinject_reject_count(void);
errinject_reject_reason_t errinject_last_reject(void);

#ifdef __cplusplus
}
#endif

#endif