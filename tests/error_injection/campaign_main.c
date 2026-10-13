/*
    project: MIL1553
    purpose: error injection campaign runner (TEST BUILD ONLY)
            1. exercises the injector's runtime guards (LLR-1553-ERR-006 / ERR-007): bad token, arming before a signed load, tampered scenario file, missing signature
            2. loads the signed scenario file, arms, and runs every scenario on a fresh BC + RT1 + RT2 + BM system, comparing observed detection/recovery with the
            scenario's expectation (LLR-1553-ERR-009)
            3. writes the machine-readable result records (JSON)

    usage: campaign <scenario.yaml> <scenarios.sig> <result.json> 
    exit status: 0 only if every guard check and every scenario passed
*/

#ifndef MIL1553_ENABLE_ERROR_INJECTION
#error "campaign_main.c must not be built in a release configuration (LLR-1553-ERR-005)"
#endif

#include <stdio.h>
#include <string.h>
#include "bus_model.h"
#include "bc_scheduler.h"
#include "rt_service.h"
#include "bm_monitor.h"
#include "bit_cbit.h"
#include "error_inject.h"

#define RUN_US (3000U)

static mil1553_bus_model_context_t bus;
static mil1553_bc_context_t bc;
static mil1553_rt_context_t rt1;
static mil1553_rt_context_t rt2;
static mil1553_bm_context_t bm;
static mil1553_bit_cbit_context_t cbit_bc;
static mil1553_bc_schedule_t sched;

static void step_all(void)
{
    int k;

    for (k = 0; k < 4; k++)
    {
        (void)bc_run_frame(&bc);
        (void)rt_run(&rt1);
        (void)rt_run(&rt2);
        bm_run(&bm);
    }
    bus_model_advance_us(&bus, 1U);
}

static void build_system(void)
{
    mil1553_rt_subaddr_config_t cfg[RT_SUBADDRESS_COUNT];

    (void)memset(cfg, sizeof(cfg));
    cfg[1].enabled = true; cfg[1].allow_receive = true; cfg[1].allow_transmit = true;
    cfg[1].min_word_count = 1U; cfg[1].max_word_count = 32U;
    cfg[2] = cfg[1]; // subaddress 30 stays disabled: ILLEGAL_COMMAND target 

    (void)memset(&sched, 0, sizeof(sched));
    sched.minor_frame_count = 1U;
    sched.minor_frame_period_us = 1000U;
    sched.entry_count = 3U;

    sched.entries[0].msg_type = MIL1553_MSG_BC_TO_RT;
    sched.entries[0].rt_address = 1U; sched.entries[0].subaddress = 1U;
    sched.entries[0].data_word_count = 2U; sched.entries[0].slot_offset_us = 0U;

    sched.entries[1].msg_type = MIL1553_MSG_RT_TO_BC;
    sched.entries[1].rt_address = 2U; sched.entries[1].subaddress = 2U;
    sched.entries[1].data_word_count = 2U; sched.entries[1].slot_offset_us = 300U;

    sched.entries[2].msg_type = MIL1553_MSG_RT_TO_RT;
    sched.entries[2].rt_address = 1U; sched.entries[2].subaddress = 1U;
    sched.entries[2].rt_address_2 = 2U; sched.entries[2].subaddress_2 = 2U;
    sched.entries[2].data_word_count = 2U; sched.entries[2].slot_offset_us = 600U;

    bus_model_init(&bus);
    (void)bc_init(&bc, &sched, &bus, MIL1553_CHANNEL_A, 3U);
    (void)rt_init(&rt1, 1U, MIL1553_TERMINAL_RT1, cfg, 8U, &bus);
    (void)rt_init(&rt2, 2U, MIL1553_TERMINAL_RT2, cfg, 8U, &bus);
    bm_init(&bm, &bus);
    bit_cbit_init(&cbit_bc, 0U);
    bc_attach_cbit(&bc, &cbit_bc);
    rt_pbit_complete(&rt1, true); (void)rt_enable(&rt1);
    rt_pbit_complete(&rt2, true); (void)rt_enable(&rt2);
}

static void run_scenario(uint32_t index, errinject_observation_t *obs)
{
    bool rt_failed_seen = false;

    build_system();
    errinject_observation_clear(obs);
    (void)errinject_begin_scenario(index, &bus);
    (void)bc_start(&bc);

    while (bus_model_now_us(&bus) < (mil1553_time_us_t)RUN_US)
    {
        mil1553_bc_health_t health;
        uint8_t i;

        step_all();
        bc_get_health(&bc, &health);
        for (i = 0U; i <= MIL1553_RT_ADDR_MAX; i++)
        {
            if (health.rt_failed[i]) { rt_failed_seen = true; }
        }
    }

    errinject_observation_add_log(obs, bc.error_log, bc.error_log_count);
    errinject_observation_add_log(obs, rt1.error_log, rt1.error_log_count);
    errinject_observation_add_log(obs, rt2.error_log, rt2.error_log_count);
    obs->cbit_response_time_violations = cbit_bc.response_time_violations;
    obs->failover_occurred = bc.degraded_indication;
    obs->rt_marked_failed = rt_failed_seen;
}

static int check(const char *what, bool condition)
{
    (void)printf(" [%s] %s\n", condition ? "PASS" : "FAIL", what);
    return condition ? 0 : 1;
}

static void write_tampered_copy(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out = fopen(dst, "wb");
    int c;
    bool done = false;

    if ((in == (FILE *)0) || (out == (FILE *)0)) { return; }
    while (( c = fgetc (in)) != EOF)
    {
        if ((!done) && (c == 'R') ) { c = 'X'; done = true; } // flip one character 
        (void)fputc(c, out);
    }
    (void)fclose(in);
    (void)fclose(out);
}

int main(int argc, char **argv)
{
    const char *yaml = (argc > 1) ? argv[1] : "error_scenarios.yaml";
    const char *sig = (argc > 2) ? argv[2] : "error_scenarios.sig";
    const char *json = (argc > 3) ? argv[3] : "error_campaign.json";
    int failures = 0;
    uint32_t i;

    (void)printf("== Guard checks (LLR-1553-ERR-006 / ERR-007) ==\n");

    failures += check("inert at process start", !errinject_is_armed());
    failures += check("arming before any signed load is refused",
                      !errinject_arm((uint32_t)ERRINJECT_HARNESS_TOKEN));

    write_tampered_copy(yaml, "errinject_tampered.yaml");
    failures += check("tampered scenario file is rejected (signature mismatch)",
                      !errinject_load_scenarios("errinject_tampered.yaml", sig) &&
                      (errinject_last_reject() == ERRINJECT_REJECT_SIG_MISMATCH) &&
                      (errinject_scenario_count() == 0U));
    (void)remove("errinject_tampered.yaml");

    failures += check("missing signature file is rejected",
                      !errinject_load_scenarios(yaml, "no_such_file.sig") &&
                      (errinject_last_reject() == ERRINJECT_REJECT_SIG_MISSING));

    failures += check("signed scenario file loads",
                      errinject_load_scenarios(yaml, sig) && (errinject_scenario_count() > 0U));
    failures += check("wrong harness token is refused",
                      !errinject_arm((uint32_t)ERRINJECT_HARNESS_TOKEN + 1U) &&
                      (errinject_last_reject() == ERRINJECT_REJECT_BAD_TOKEN));
    failures += check("scenario cannot start while disarmed",
                      !errinject_begin_scenario(0U, &bus));
    failures += check("arming with the harness token succeeds",
                      errinject_arm((uint32_t)ERRINJECT_HARNESS_TOKEN));

    errinject_register_rt_address(MIL1553_TERMINAL_RT1, 1U);
    errinject_register_rt_address(MIL1553_TERMINAL_RT2, 2U);

    (void)printf("\n== Campaign: %u scenarios ==\n", errinject_scenario_count());
    for (i = 0U; i < errinject_scenario_count(); i++)
    {
        errinject_observation_t obs;
        run_scenario(i, &obs);
        (void)errinject_end_scenario(&bus, &obs);
    }

    for (i = 0U; i < errinject_result_count(); i++)
    {
        const errinject_result_t *r = errinject_get_result(i);
        (void)printf("  [%s] %-8s %-18s expected {%s}\n           observed {%s}\n",
                     r->pass ? "PASS" : "FAIL", r->scenario_id, r->fault, r->expected, r->observed);
        if (!r->pass) { failures++; }
    }

    errinject_disarm();
    failures += check("disarm returns the injector to inert", !errinject_is_armed());
    failures += check("results written", errinject_write_results_json(json));

    (void)printf("\n%s (%d failure(s))\n", (failures == 0) ? "CAMPAIGN PASSED" : "CAMPAIGN FAILED", failures);
    return (failures == 0) ? 0 : 1;
}