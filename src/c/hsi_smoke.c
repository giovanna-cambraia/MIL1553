#include <stdio.h>
#include <string.h>
#include "bus_model.h"
#include "bc_scheduler.h"
#include "rt_service.h"
#include "bm_monitor.h"
#include "bit_cbit.h"

static mil1553_bus_model_context_t bus;
static mil1553_bc_context_t bc;
static mil1553_rt_context_t rt1, rt2;
static mil1553_bm_context_t bm;
static mil1553_bit_cbit_context_t cbit_bc;
static mil1553_bc_schedule_t sched;

static void step_all(void)
{
    int k;
    // four rounds of (BC, RT1, RT2, BM) per step so that each logical "instant" gives every party a chance to react, then a 1 us nudge
    // so the loop makes forward progress even if nothing transmits
    for (k = 0; k < 4; k++)
    {
        (void)bc_run_frame(&bc);
        (void)rt_run(&rt1);
        (void)rt_run(&rt2);
        bm_run(&bm);
    }
    bus_model_advance_us(&bus, 1U);
}

int main(void)
{
    mil1553_rt_subaddr_config_t cfg[RT_SUBADDRESS_COUNT];
    unsigned i;
    memset(cfg, 0, sizeof cfg);
    cfg[1].enabled = true;
    cfg[1].allow_receive = true;
    cfg[1].allow_transmit = true;
    cfg[1].min_word_count = 1U;
    cfg[1].max_word_count = 32U;
    cfg[2] = cfg[1];

    memset(&sched, 0, sizeof sched);
    sched.minor_frame_count = 1U;
    sched.minor_frame_period_us = 1000U;
    sched.entry_count = 3U;
    // RT1 <- RT2, RT-to-RT, 4 words. bc_scheduler.h: rt_address = receiving RT (= RT1), rt_address_2 = transmitting RT (= RT2); the BC sends
    // recv_cmd (to RT1) and xmit_cmd (to RT2) back to back; rt_service.c's receiving-RT RT-to-RT detection relies on seeing the paired transmit command as the very first word
    // of the data phase, i.e. no intervening word between the two commands
    sched.entries[0].msg_type = MIL1553_MSG_RT_TO_RT;
    sched.entries[0].rt_address = 1U;
    sched.entries[0].subaddress = 1U;
    sched.entries[0].rt_address_2 = 2U;
    sched.entries[0].subaddress_2 = 2U;
    sched.entries[0].data_word_count = 4U;
    sched.entries[0].slot_offset_us = 0U;
    // BC -> RT1, 3 words 
    sched.entries[1].msg_type = MIL1553_MSG_BC_TO_RT;
    sched.entries[1].rt_address = 1U;
    sched.entries[1].subaddress = 1U;
    sched.entries[1].data_word_count = 3U;
    sched.entries[1].slot_offset_us = 300U;
    // RT2 -> BC, 2 words 
    sched.entries[2].msg_type = MIL1553_MSG_RT_TO_BC;
    sched.entries[2].rt_address = 2U;
    sched.entries[2].subaddress = 2U;
    sched.entries[2].data_word_count = 2U;
    sched.entries[2].slot_offset_us = 600U;

    bus_model_init(&bus);
    printf("bc_init: %d\n", bc_init(&bc, &sched, &bus, MIL1553_CHANNEL_A, 3U));
    printf("rt1 init: %d\n", rt_init(&rt1, 1U, MIL1553_TERMINAL_RT1, cfg, 8U, &bus));
    printf("rt2 init: %d\n", rt_init(&rt2, 2U, MIL1553_TERMINAL_RT2, cfg, 8U, &bus));
    bm_init(&bm, &bus);
    bit_cbit_init(&cbit_bc, 0U);
    bc_attach_cbit(&bc, &cbit_bc);
    rt_pbit_complete(&rt1, true);
    rt_enable(&rt1);
    rt_pbit_complete(&rt2, true);
    rt_enable(&rt2);
    bc_start(&bc);

    // Run for enough sim time to cover at least two full major frames; one minor frame's worst case is roughly (RT-to-RT 748 us) + (BC-to-RT 96 us) + (RT-to-BC 96 us) +
    // per-message gaps, i.e. ~1 ms; 3000 us gives at least two full frames and exercises the schedule's slot-offset behavior; the  is on the virtual clock, not on step count: 
    // every word the BC or an RT transmits also advances the clock
    while (bus_model_now_us(&bus) < 3000U)
    {
        step_all();
    }

    bm_reconstruct_messages(&bm);
    printf("t=%llu bc_state=%d bc_errlog=%u rt1_errlog=%u rt2_errlog=%u\n",
           (unsigned long long)bus_model_now_us(&bus), (int)bc_get_state(&bc),
           (unsigned)bc.error_log_count, (unsigned)rt1.error_log_count, (unsigned)rt2.error_log_count);
    for (i = 0; i < bc.error_log_count; i++)
        printf("  bc err %u: type=%d t=%llu\n", i, (int)bc.error_log[i].error_type, (unsigned long long)bc.error_log[i].timestamp_us);
    for (i = 0; i < rt1.error_log_count; i++)
        printf("  rt1 err %u: type=%d\n", i, (int)rt1.error_log[i].error_type);
    for (i = 0; i < rt2.error_log_count; i++)
        printf("  rt2 err %u: type=%d\n", i, (int)rt2.error_log[i].error_type);
    printf("rt1 sub1 word_count=%u, rt2 sub2 word_count=%u\n",
           (unsigned)rt1.subaddr_buffer[1].word_count,
           (unsigned)rt2.subaddr_buffer[2].word_count);
    for (i = 0; i < bm_get_message_count(&bm); i++)
    {
        const mil1553_bm_message_t *m = bm_get_message(&bm, (uint16_t)i);
        printf("  bm msg %u: type=%d rt=%u rt2=%u resp_seen=%d resp_us=%u\n",
               i, (int)m->msg_type, (unsigned)m->rt_address, (unsigned)m->rt_address_2,
               (int)m->response_seen, (unsigned)m->response_time_us);
    }
    printf("cbit: A good=%u err=%u rtviol=%u wd=%d\n",
           (unsigned)cbit_bc.channel[0].good_count_total,
           (unsigned)cbit_bc.channel[0].error_count_total,
           (unsigned)cbit_bc.response_time_violations,
           (int)cbit_bc.watchdog_tripped);
    return 0;
}