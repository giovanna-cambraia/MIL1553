/*
    project: MIL1553
    realizes: LLR-1553-ERR-006, LLR-1553-ERR-007, LLR-1553-ERR-008, LLR-1553-ERR-009

    TEST BUILD ONLY - outside the certification boundary (PSAC 8); see error_inject.h for the three guards and the harness-token caveat

    fault semantics (all act on words as they are transmitted, in bus_model_transmit's hook, for the terminal named by the scenario's "target" and the scenario's bus):

    PARITY_BIT_FLIP         swap the parity bit cell (valid Manchester, wrong parity) for target words word_index .. +count-1, counted from scenario start
    CORRUPT_SYNC            replace the sync with an invalid pattern, same word window
    SUPPRESS_RESPONSE       drop every word the target sends for duration_us of virtual time from scenario start
    DELAY_RESPONSE          advance the virtual clock by delay_us before the target's first `count` status words go out
    WRONG_WORD_COUNT        drop the data word with index word_index in the target's first `count` responses (short response)
    STATUS_BIT_INJECT       set BUSY / ME / SF / TF in the target's first `count` status words, parity recomputed
    ILLEGAL_COMMAND         rewrite the subaddress of the first `count` BC commands addressed to the target RT to ERRINJECT_ILLEGAL_SUBADDRESS, parity recomputed
    BUS_A_FAIL / BUS_B_FAIL mark the channel failed from scenario start
*/

#ifndef MIL1553_ENABLE_ERROR_INJECTION
#error "error_inject.c must not be built in a release configuration (LLR-1553-ERR-005)"
#endif

#include "error_inject.h"
#include "config_signature.h"
#include "mil1553_word.h"
#include <stdio.h>
#include <string.h>

#define YAML_MAX_BYTES (16384U)
#define LINE_MAX_CHARS (160U)

// module state (single instance; process start == inert)

static bool g_armed = false;
static bool g_loaded = false;

static errinject_scenario_t g_scen[ERRINJECT_MAX_SCENARIOS];
static uint32_t g_scen_count = 0U;

static errinject_result_t g_res[ERRINJECT_MAX_SCENARIOS];
static uint32_t g_res_count = 0U;

static mil1553_rt_addr_t g_rt_addr[BUS_MODEL_LISTENER_COUNT];

static bool g_active = false;
static uint32_t g_active_idx = 0U;
static mil1553_time_us_t g_active_start_us = 0U;
static mil1553_time_us_t g_first_inject_us = 0U;
static uint32_t g_words_seen = 0U;
static uint32_t g_applied = 0U;
static uint32_t g_resp_data_idx = 0U;

static uint32_t g_reject_count = 0U;
static errinject_reject_reason_t g_last_reject = ERRINJECT_REJECT_NONE;

static uint8_t g_yaml_buf[YAML_MAX_BYTES + 1U];

// test-harness support: back to the condition at process start
void errinject_reset(void)
{
    g_armed = false;
    g_loaded = false;
    g_active = false;
    g_scen_count = 0U;
    g_res_count = 0U;
    g_active_idx = 0U;
    g_active_start_us = 0U;
    g_first_inject_us = 0U;
    g_words_seen = 0U;
    g_applied = 0U;
    g_resp_data_idx = 0U;
    g_reject_count = 0U;
    g_last_reject = ERRINJECT_REJECT_NONE;
    (void)memset(g_rt_addr, 0, sizeof(g_rt_addr));
}

static void reject(errinject_reject_reason_t reason)
{
    g_reject_count++;
    g_last_reject = reason;
}

// name tables

typedef struct { const char *name; uint32_t value; } name_entry_t;

static const name_entry_t FAULT_NAMES[] = {
    { "PARITY_BIT_FLIP",   (uint32_t)MIL1553_FAULT_PARITY_BIT_FLIP },
    { "CORRUPT_SYNC",      (uint32_t)MIL1553_FAULT_CORRUPT_SYNC },
    { "SUPPRESS_RESPONSE", (uint32_t)MIL1553_FAULT_SUPPRESS_RESPONSE },
    { "DELAY_RESPONSE",    (uint32_t)MIL1553_FAULT_DELAY_RESPONSE },
    { "WRONG_WORD_COUNT",  (uint32_t)MIL1553_FAULT_WRONG_WORD_COUNT },
    { "STATUS_BIT_INJECT", (uint32_t)MIL1553_FAULT_STATUS_BIT_INJECT },
    { "ILLEGAL_COMMAND",   (uint32_t)MIL1553_FAULT_ILLEGAL_COMMAND },
    { "BUS_A_FAIL",        (uint32_t)MIL1553_FAULT_BUS_A_FAIL },
    { "BUS_B_FAIL",        (uint32_t)MIL1553_FAULT_BUS_B_FAIL }
};
#define FAULT_NAME_COUNT  (sizeof(FAULT_NAMES) / sizeof(FAULT_NAMES[0]))

static const name_entry_t TARGET_NAMES[] = {
    { "BC", (uint32_t)MIL1553_TERMINAL_BC }, { "RT1", (uint32_t)MIL1553_TERMINAL_RT1 },
    { "RT2", (uint32_t)MIL1553_TERMINAL_RT2 }, { "BM", (uint32_t)MIL1553_TERMINAL_BM }
};
#define TARGET_NAME_COUNT (sizeof(TARGET_NAMES) / sizeof(TARGET_NAMES[0]))

static const name_entry_t BIT_NAMES[] = {
    { "BUSY", (uint32_t)ERRINJECT_BIT_BUSY }, { "ME", (uint32_t)ERRINJECT_BIT_ME },
    { "SF", (uint32_t)ERRINJECT_BIT_SF }, { "TF", (uint32_t)ERRINJECT_BIT_TF }
};
#define BIT_NAME_COUNT (sizeof(BIT_NAMES) / sizeof(BIT_NAMES[0]))

// detection tokens: values 0..11 are mil1553_error_type_t; the two below are special
#define DET_CBIT_RESPONSE_TIME (100U)
#define DET_NONE               (101U)

static const name_entry_t DETECTION_NAMES[] = {
    { "PARITY",                  (uint32_t)MIL1553_ERR_PARITY },
    { "SYNC",                    (uint32_t)MIL1553_ERR_SYNC },
    { "MANCHESTER",              (uint32_t)MIL1553_ERR_MANCHESTER },
    { "NO_RESPONSE",             (uint32_t)MIL1553_ERR_NO_RESPONSE },
    { "STATUS_ADDRESS_MISMATCH", (uint32_t)MIL1553_ERR_STATUS_ADDRESS_MISMATCH },
    { "STATUS_ME",               (uint32_t)MIL1553_ERR_STATUS_ME },
    { "STATUS_BUSY",             (uint32_t)MIL1553_ERR_STATUS_BUSY },
    { "STATUS_SF",               (uint32_t)MIL1553_ERR_STATUS_SF },
    { "STATUS_TF",               (uint32_t)MIL1553_ERR_STATUS_TF },
    { "WORD_COUNT_MISMATCH",     (uint32_t)MIL1553_ERR_WORD_COUNT_MISMATCH },
    { "MINOR_FRAME_OVERRUN",     (uint32_t)MIL1553_ERR_MINOR_FRAME_OVERRUN },
    { "TRANSPORT",               (uint32_t)MIL1553_ERR_TRANSPORT },
    { "CBIT_RESPONSE_TIME",      DET_CBIT_RESPONSE_TIME },
    { "NONE",                    DET_NONE }
};
#define DETECTION_NAME_COUNT (sizeof(DETECTION_NAMES) / sizeof(DETECTION_NAMES[0]))

static bool lookup(const name_entry_t *table, size_t count, const char *name, uint32_t *out)
{
    size_t i;

    for (i = 0U; i < count; i++)
    {
        if (strcmp(table[i].name, name) == 0)
        {
            *out = table[i].value;
            return true;
        }
    }
    return false;
}

static const char *name_of(const name_entry_t *table, size_t count, uint32_t value)
{
    size_t i;

    for (i = 0U; i < count; i++)
    {
        if (table[i].value == value)
        {
            return table[i].name;
        }
    }
    return "UNKNOWN";
}

// arming, LLR-1553-ERR-006

// implements: LLR-1553-ERR-006
bool errinject_arm(uint32_t token)
{
    if (token != (uint32_t)ERRINJECT_HARNESS_TOKEN)
    {
        reject(ERRINJECT_REJECT_BAD_TOKEN);
        return false;
    }
    if (!g_loaded)
    {
        reject(ERRINJECT_REJECT_NOT_LOADED);
        return false;
    }

    g_armed = true;
    return true;
}

void errinject_disarm(void)
{
    g_armed = false;
    g_active = false;
}

bool errinject_is_armed(void)
{
    return g_armed;
}

uint32_t errinject_reject_count(void)
{
    return g_reject_count;
}

errinject_reject_reason_t errinject_last_reject(void)
{
    return g_last_reject;
}

// scenario file parsing

static char *trim(char *s)
{
    char *end;

    while ((*s == ' ') || (*s == '\t'))
    {
        s++;
    }
    end = s + strlen(s);
    while ((end > s) && ((end[-1] == ' ') || (end[-1] == '\t') || (end[-1] == '\r') || (end[-1] == '\n')))
    {
        end--;
    }
    *end = '\0';
    return s;
}

static bool parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0U;

    if (*s == '\0')
    {
        return false;
    }
    while (*s != '\0')
    {
        if ((*s < '0') || (*s > '9'))
        {
            return false;
        }
        v = (v * 10U) + (uint32_t)(*s - '0');
        s++;
    }
    *out = v;
    return true;
}

static bool copy_str(char *dst, size_t dst_size, const char *src)
{
    if (strlen(src) >= dst_size)
    {
        return false;
    }
    (void)strcpy(dst, src);
    return true;
}

#define HAVE_ID       (0x01U)
#define HAVE_TARGET   (0x02U)
#define HAVE_FAULT    (0x04U)
#define HAVE_EXPECTED (0x08U)
#define HAVE_ALL      (HAVE_ID | HAVE_TARGET | HAVE_FAULT | HAVE_EXPECTED)

static bool set_field(errinject_scenario_t *sc, uint32_t *have, const char *key, const char *value)
{
    uint32_t v;

    if (strcmp(key, "id") == 0)
    {
        *have |= HAVE_ID;
        return copy_str(sc->id, sizeof(sc->id), value);
    }
    if (strcmp(key, "target") == 0)
    {
        *have |= HAVE_TARGET;
        if (!lookup(TARGET_NAMES, TARGET_NAME_COUNT, value, &v)) { return false; }
        sc->target = (mil1553_terminal_id_t)v;
        return true;
    }
    if (strcmp(key, "bus") == 0)
    {
        if (strcmp(value, "A") == 0) { sc->bus_sel = ERRINJECT_BUS_A; }
        else if (strcmp(value, "B") == 0) { sc->bus_sel = ERRINJECT_BUS_B; }
        else if (strcmp(value, "ANY") == 0) { sc->bus_sel = ERRINJECT_BUS_ANY; }
        else { return false; }
        return true;
    }
    if (strcmp(key, "fault") == 0)
    {
        *have |= HAVE_FAULT;
        if (!lookup(FAULT_NAMES, FAULT_NAME_COUNT, value, &v)) { return false; }
        sc->fault = (mil1553_fault_class_t)v;
        return true;
    }
    if (strcmp(key, "word_index") == 0) { return parse_u32(value, &sc->word_index); }
    if (strcmp(key, "count") == 0)      { return parse_u32(value, &sc->count); }
    if (strcmp(key, "delay_us") == 0)   { return parse_u32(value, &sc->delay_us); }
    if (strcmp(key, "duration_ms") == 0)
    {
        if (!parse_u32(value, &v)) { return false; }
        sc->duration_us = v * 1000U;
        return true;
    }
    if (strcmp(key, "bit") == 0)
    {
        if (!lookup(BIT_NAMES, BIT_NAME_COUNT, value, &v)) { return false; }
        sc->status_bit = (errinject_status_bit_t)v;
        return true;
    }
    if (strcmp(key, "expected_detection") == 0)
    {
        *have |= HAVE_EXPECTED;
        if (!lookup(DETECTION_NAMES, DETECTION_NAME_COUNT, value, &v)) { return false; }
        return copy_str(sc->expected_detection, sizeof(sc->expected_detection), value);
    }
    if (strcmp(key, "expected_recovery") == 0)
    {
        if ((strcmp(value, "NONE") != 0) && (strcmp(value, "RT_MARKED_FAILED") != 0) && (strcmp(value, "BUS_FAILOVER") != 0))
        {
            return false;
        }
        return copy_str(sc->expected_recovery, sizeof(sc->expected_recovery), value);
    }
    if (strcmp(key, "hlr") == 0)     { return copy_str(sc->hlr, sizeof(sc->hlr), value); }
    if (strcmp(key, "llr") == 0)     { return copy_str(sc->llr, sizeof(sc->llr), value); }
    if (strcmp(key, "test_id") == 0) { return copy_str(sc->test_id, sizeof(sc->test_id), value); }

    return false; // unknown key
}

static void init_scenario(errinject_scenario_t *sc)
{
    (void)memset(sc, 0, sizeof(*sc));
    sc->bus_sel = ERRINJECT_BUS_ANY;
    sc->count = 1U;
    (void)strcpy(sc->expected_recovery, "NONE");
}

static bool parse_scenarios(char *text)
{
    char *line = text;
    errinject_scenario_t *cur = (errinject_scenario_t *)0;
    uint32_t have = 0U;
    bool ok = true;

    g_scen_count = 0U;

    while ((line != (char *)0) && (*line != '\0') && ok)
    {
        char *next = strchr(line, '\n');
        char buf[LINE_MAX_CHARS];
        char *p;
        char *hash;
        size_t len;

        if (next != (char *)0) { len = (size_t)(next - line); next++; }
        else { len = strlen(line); }

        if (len >= sizeof(buf)) { ok = false; break; }
        (void)memcpy(buf, line, len);
        buf[len] = '\0';
        line = next;

        hash = strchr(buf, '#');
        if (hash != (char *)0) { *hash = '\0'; }
        p = trim(buf);
        if (*p == '\0') { continue; }
        if (strcmp(p, "scenarios:") == 0) { continue; }

        if ((p[0] == '-') && ((p[1] == ' ') || (p[1] == '\t')))
        {
            if ((cur != (errinject_scenario_t *)0) && (have != HAVE_ALL)) { ok = false; break; }
            if (g_scen_count >= ERRINJECT_MAX_SCENARIOS) { ok = false; break; }
            cur = &g_scen[g_scen_count];
            g_scen_count++;
            init_scenario(cur);
            have = 0U;
            p = trim(p + 1);
        }

        if (cur == (errinject_scenario_t *)0) { ok = false; break; }

        {
            char *colon = strchr(p, ':');
            char *key;
            char *value;

            if (colon == (char *)0) { ok = false; break; }
            *colon = '\0';
            key = trim(p);
            value = trim(colon + 1);
            if ((*key == '\0') || (*value == '\0')) { ok = false; break; }
            if (!set_field(cur, &have, key, value)) { ok = false; break; }
        }
    }

    if (ok && ((cur == (errinject_scenario_t *)0) || (have != HAVE_ALL)))
    {
        ok = false;
    }
    if (!ok)
    {
        g_scen_count = 0U;
    }
    return ok;
}

// implements: LLR-1553-ERR-007
bool errinject_load_scenarios(const char *yaml_path, const char *sig_path)
{
    FILE *fp;
    size_t n;
    errinject_sig_result_t sig;

    g_loaded = false;
    g_armed = false;
    g_active = false;
    g_scen_count = 0U;
    g_res_count = 0U;

    fp = fopen(yaml_path, "rb");
    if (fp == (FILE *)0)
    {
        reject(ERRINJECT_REJECT_YAML_MISSING);
        return false;
    }
    n = fread(g_yaml_buf, 1U, YAML_MAX_BYTES + 1U, fp);
    (void)fclose(fp);

    if (n > YAML_MAX_BYTES)
    {
        reject(ERRINJECT_REJECT_YAML_TOO_LARGE);
        return false;
    }

    sig = errinject_check_signature(g_yaml_buf, (uint32_t)n, sig_path);
    if (sig == ERRINJECT_SIG_MISSING)
    {
        reject(ERRINJECT_REJECT_SIG_MISSING);
        return false;
    }
    if (sig != ERRINJECT_SIG_OK)
    {
        reject(ERRINJECT_REJECT_SIG_MISMATCH);
        return false;
    }

    g_yaml_buf[n] = 0U;
    if (!parse_scenarios((char *)g_yaml_buf))
    {
        reject(ERRINJECT_REJECT_YAML_PARSE);
        return false;
    }

    g_loaded = true;
    return true;
}

uint32_t errinject_scenario_count(void)
{
    return g_scen_count;
}

const errinject_scenario_t *errinject_get_scenario(uint32_t index)
{
    return (index < g_scen_count) ? &g_scen[index] : (const errinject_scenario_t *)0;
}

void errinject_register_rt_address(mil1553_terminal_id_t terminal, mil1553_rt_addr_t rt_address)
{
    if ((uint32_t)terminal < BUS_MODEL_LISTENER_COUNT)
    {
        g_rt_addr[terminal] = rt_address;
    }
}

// fault primitives, LLR-1553-ERR-008

static void sym_flip(mil1553_symbols_t s, uint8_t index)
{
    s[index / 8U] = (uint8_t)(s[index / 8U] ^ (uint8_t)(1U << (7U - (index % 8U))));
}

static void sym_set(mil1553_symbols_t s, uint8_t index, uint8_t value)
{
    uint8_t mask = (uint8_t)(1U << (7U - (index % 8U)));

    if (value != 0U) { s[index / 8U] = (uint8_t)(s[index / 8U] | mask); }
    else { s[index / 8U] = (uint8_t)(s[index / 8U] & (uint8_t)(~mask)); }
}

static void mark_applied(mil1553_bus_model_context_t *bus)
{
    if (g_applied == 0U)
    {
        g_first_inject_us = bus_model_now_us(bus);
    }
    g_applied++;
}

// implements: LLR-1553-ERR-008
void errinject_filter_transmit(mil1553_bus_model_context_t *bus, mil1553_channel_t channel, mil1553_terminal_id_t sender, mil1553_symbols_t symbols, bool *drop)
{
    const errinject_scenario_t *sc;
    bool ch_ok;
    mil1553_decoded_word_t dec;
    mil1553_word_class_t cls;

    *drop = false;

    if (!(g_armed && g_loaded && g_active))
    {
        return; // inert, LLR-1553-ERR-006 / ERR-007
    }

    sc = &g_scen[g_active_idx];
    ch_ok = (sc->bus_sel == ERRINJECT_BUS_ANY) ||
            ((sc->bus_sel == ERRINJECT_BUS_A) && (channel == MIL1553_CHANNEL_A)) ||
            ((sc->bus_sel == ERRINJECT_BUS_B) && (channel == MIL1553_CHANNEL_B));
    cls = mil1553_word_classify(symbols, &dec);

    if (sc->fault == MIL1553_FAULT_ILLEGAL_COMMAND)
    {
        if ((sender == MIL1553_TERMINAL_BC) && ch_ok && (g_applied < sc->count) &&
            (cls == MIL1553_WORD_VALID) && (dec.sync == MIL1553_SYNC_COMMAND_STATUS))
        {
            mil1553_command_fields_t cmd;
            mil1553_word_unpack_command(dec.field, &cmd);

            if ((cmd.rt_address == g_rt_addr[sc->target]) && (cmd.subaddress != 0U) && (cmd.subaddress != 31U))
            {
                cmd.subaddress = (mil1553_subaddr_t)ERRINJECT_ILLEGAL_SUBADDRESS;
                (void)mil1553_word_pack_command(&cmd, symbols);
                mark_applied(bus);
            }
        }
        return;
    }

    if ((sc->fault == MIL1553_FAULT_BUS_A_FAIL) || (sc->fault == MIL1553_FAULT_BUS_B_FAIL))
    {
        return; // applied at scenario start
    }

    if ((sender != sc->target) || (!ch_ok))
    {
        return;
    }

    switch (sc->fault)
    {
        case MIL1553_FAULT_PARITY_BIT_FLIP:
            if ((g_words_seen >= sc->word_index) && (g_words_seen < (sc->word_index + sc->count)))
            {
                sym_flip(symbols, 38U); // parity bit cell = symbols 38,39
                sym_flip(symbols, 39U);
                mark_applied(bus);
            }
            break;

        case MIL1553_FAULT_CORRUPT_SYNC:
            if ((g_words_seen >= sc->word_index) && (g_words_seen < (sc->word_index + sc->count)))
            {
                uint8_t i;
                for (i = 0U; i < 6U; i++)
                {
                    sym_set(symbols, i, (uint8_t)((i % 2U) == 0U ? 1U : 0U)); // 1,0,1,0,1,0: neither valid sync
                }
                mark_applied(bus);
            }
            break;

        case MIL1553_FAULT_SUPPRESS_RESPONSE:
            if ((bus_model_now_us(bus) - g_active_start_us) < (mil1553_time_us_t)sc->duration_us)
            {
                *drop = true;
                mark_applied(bus);
            }
            break;

        case MIL1553_FAULT_DELAY_RESPONSE:
            if ((g_applied < sc->count) && (cls == MIL1553_WORD_VALID) && (dec.sync == MIL1553_SYNC_COMMAND_STATUS))
            {
                bus_model_advance_us(bus, (mil1553_time_us_t)sc->delay_us);
                mark_applied(bus);
            }
            break;

        case MIL1553_FAULT_WRONG_WORD_COUNT:
            if ((cls == MIL1553_WORD_VALID) && (dec.sync == MIL1553_SYNC_COMMAND_STATUS))
            {
                g_resp_data_idx = 0U; // a status word starts a new response
            }
            else if ((cls == MIL1553_WORD_VALID) && (dec.sync == MIL1553_SYNC_DATA))
            {
                if ((g_resp_data_idx == sc->word_index) && (g_applied < sc->count))
                {
                    *drop = true;
                    mark_applied(bus);
                }
                g_resp_data_idx++;
            }
            else
            {
                // not a well-formed status or data word: leave untouched
            }
            break;

        case MIL1553_FAULT_STATUS_BIT_INJECT:
            if ((g_applied < sc->count) && (cls == MIL1553_WORD_VALID) && (dec.sync == MIL1553_SYNC_COMMAND_STATUS))
            {
                mil1553_status_fields_t st;
                mil1553_word_unpack_status(dec.field, &st);

                switch (sc->status_bit)
                {
                    case ERRINJECT_BIT_BUSY: st.busy = true; break;
                    case ERRINJECT_BIT_ME:   st.message_error = true; break;
                    case ERRINJECT_BIT_SF:   st.subsystem_flag = true; break;
                    default:                 st.terminal_flag = true; break;
                }
                (void)mil1553_word_pack_status(&st, symbols);
                mark_applied(bus);
            }
            break;

        default:
            break;
    }

    g_words_seen++;
}

// scenario execution and result records, LLR-1553-ERR-009

// implements: LLR-1553-ERR-008, LLR-1553-ERR-009
bool errinject_begin_scenario(uint32_t index, mil1553_bus_model_context_t *bus)
{
    const errinject_scenario_t *sc;

    if ((!g_armed) || (!g_loaded))
    {
        reject(ERRINJECT_REJECT_NOT_ARMED);
        return false;
    }
    if ((g_active) || (index >= g_scen_count))
    {
        return false;
    }

    sc = &g_scen[index];
    g_active = true;
    g_active_idx = index;
    g_active_start_us = bus_model_now_us(bus);
    g_first_inject_us = g_active_start_us;
    g_words_seen = 0U;
    g_applied = 0U;
    g_resp_data_idx = 0U;

    if (sc->fault == MIL1553_FAULT_BUS_A_FAIL)
    {
        bus_model_set_channel_failed(bus, MIL1553_CHANNEL_A, true);
        mark_applied(bus);
    }
    else if (sc->fault == MIL1553_FAULT_BUS_B_FAIL)
    {
        bus_model_set_channel_failed(bus, MIL1553_CHANNEL_B, true);
        mark_applied(bus);
    }
    else
    {
        // word-level faults act in errinject_filter_transmit
    }

    return true;
}

void errinject_observation_clear(errinject_observation_t *obs)
{
    (void)memset(obs, 0, sizeof(*obs));
}

void errinject_observation_add_log(errinject_observation_t *obs, const mil1553_error_record_t *records, uint32_t record_count)
{
    uint32_t i;

    for (i = 0U; i < record_count; i++)
    {
        uint32_t t = (uint32_t)records[i].error_type;
        if (t < ERRINJECT_ERR_TYPE_COUNT)
        {
            obs->error_type_count[t]++;
        }
    }
}

static bool detection_present(const errinject_observation_t *obs, uint32_t token)
{
    if (token == DET_CBIT_RESPONSE_TIME) { return obs->cbit_response_time_violations > 0U; }
    if (token < ERRINJECT_ERR_TYPE_COUNT) { return obs->error_type_count[token] > 0U; }
    return false;
}

static void append(char *dst, size_t size, const char *fmt, const char *arg)
{
    size_t used = strlen(dst);

    if (used < (size - 1U))
    {
        (void)snprintf(&dst[used], size - used, fmt, arg);
    }
}

// implements: LLR-1553-ERR-009
bool errinject_end_scenario(mil1553_bus_model_context_t *bus, const errinject_observation_t *obs)
{
    const errinject_scenario_t *sc;
    errinject_result_t *r;
    uint32_t expected_token = DET_NONE;
    bool any_detection = false;
    bool detection_ok;
    bool recovery_ok;
    bool applied_ok;
    size_t i;

    if ((!g_active) || (g_res_count >= ERRINJECT_MAX_SCENARIOS))
    {
        return false;
    }

    sc = &g_scen[g_active_idx];
    r = &g_res[g_res_count];
    (void)memset(r, 0, sizeof(*r));

    if (sc->fault == MIL1553_FAULT_BUS_A_FAIL) { bus_model_set_channel_failed(bus, MIL1553_CHANNEL_A, false); }
    if (sc->fault == MIL1553_FAULT_BUS_B_FAIL) { bus_model_set_channel_failed(bus, MIL1553_CHANNEL_B, false); }

    (void)lookup(DETECTION_NAMES, DETECTION_NAME_COUNT, sc->expected_detection, &expected_token);

    (void)strcpy(r->scenario_id, sc->id);
    r->timestamp_us = (g_applied > 0U) ? g_first_inject_us : g_active_start_us;
    r->terminal = sc->target;
    (void)strcpy(r->bus, (sc->bus_sel == ERRINJECT_BUS_A) ? "A" : (sc->bus_sel == ERRINJECT_BUS_B) ? "B" : "ANY");
    (void)strcpy(r->fault, name_of(FAULT_NAMES, FAULT_NAME_COUNT, (uint32_t)sc->fault));
    (void)snprintf(r->expected, sizeof(r->expected), "detect=%s;recover=%s", sc->expected_detection, sc->expected_recovery);
    (void)strcpy(r->hlr, sc->hlr);
    (void)strcpy(r->llr, sc->llr);
    (void)strcpy(r->test_id, sc->test_id);

    // observed detections
    for (i = 0U; i < DETECTION_NAME_COUNT; i++)
    {
        if ((DETECTION_NAMES[i].value != DET_NONE) && detection_present(obs, DETECTION_NAMES[i].value))
        {
            any_detection = true;
            if (r->observed[0] != '\0') { append(r->observed, sizeof(r->observed), "%s", ","); }
            append(r->observed, sizeof(r->observed), "%s", DETECTION_NAMES[i].name);
        }
    }
    if (!any_detection) { (void)strcpy(r->observed, "NONE"); }
    append(r->observed, sizeof(r->observed), ";recover=%s",
           (obs->failover_occurred && obs->rt_marked_failed) ? "BUS_FAILOVER,RT_MARKED_FAILED" :
           obs->failover_occurred ? "BUS_FAILOVER" :
           obs->rt_marked_failed ? "RT_MARKED_FAILED" : "NONE");

    applied_ok = (g_applied > 0U);
    if (!applied_ok)
    {
        append(r->observed, sizeof(r->observed), ";%s", "FAULT_NOT_APPLIED");
    }

    // a detection that is expected but not observed is a FAILURE, never a pass
    detection_ok = (expected_token == DET_NONE) ? (!any_detection) : detection_present(obs, expected_token);

    if (strcmp(sc->expected_recovery, "RT_MARKED_FAILED") == 0) { recovery_ok = obs->rt_marked_failed; }
    else if (strcmp(sc->expected_recovery, "BUS_FAILOVER") == 0) { recovery_ok = obs->failover_occurred; }
    else { recovery_ok = true; }

    r->pass = applied_ok && detection_ok && recovery_ok;

    g_res_count++;
    g_active = false;
    return r->pass;
}

uint32_t errinject_result_count(void)
{
    return g_res_count;
}

const errinject_result_t *errinject_get_result(uint32_t index)
{
    return (index < g_res_count) ? &g_res[index] : (const errinject_result_t *)0;
}

// implements: LLR-1553-ERR-009 (machine-readable output)
bool errinject_write_results_json(const char *path)
{
    FILE *fp = fopen(path, "wb");
    uint32_t i;

    if (fp == (FILE *)0)
    {
        return false;
    }

    (void)fprintf(fp, "[\n");
    for (i = 0U; i < g_res_count; i++)
    {
        const errinject_result_t *r = &g_res[i];
        (void)fprintf(fp,
            "  {\"scenario_id\": \"%s\", \"timestamp_us\": %llu, \"terminal\": \"%s\", \"bus\": \"%s\", "
            "\"fault\": \"%s\", \"expected\": \"%s\", \"observed\": \"%s\", \"pass_fail\": \"%s\", "
            "\"HLR\": \"%s\", \"LLR\": \"%s\", \"test_id\": \"%s\"}%s\n",
            r->scenario_id, (unsigned long long)r->timestamp_us,
            name_of(TARGET_NAMES, TARGET_NAME_COUNT, (uint32_t)r->terminal), r->bus,
            r->fault, r->expected, r->observed, r->pass ? "PASS" : "FAIL",
            r->hlr, r->llr, r->test_id, ((i + 1U) < g_res_count) ? "," : "");
    }
    (void)fprintf(fp, "]\n");
    (void)fclose(fp);
    return true;
}