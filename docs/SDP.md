# Software Development Plan (SDP)
**Project:** MIL1553
**Document ID:** MIL1553-SDP-001
**Revision:** A (Draft)
**Date:** 2026-09-25
**Related:** PSAC (`docs/PSAC.md`)

---

## 1. Purpose

This SDP defines the software development standards, methods, and life
cycle to be followed for both the C and Ada/SPARK implementations of the
MIL1553 software items, per DO-178C §5.1 and Table A-2 objectives.

---

## 2. Software Life Cycle Model

A **V-model** is used, with two parallel implementation branches sharing a
common requirements baseline:

```
Requirements (HLR)
      |
      v
Design (LLR / SDD)  ------------------------+
      |                                     |
      v                                     v
  C Implementation                Ada/SPARK Implementation
      |                                     |
      v                                     v
  C Unit Test (Unity)              Ada Unit Test (AUnit)
      |                                     |
      +------------------+------------------+
                          v
         HSI Integration (software bus model,
         same-language AND cross-language runs)
                          |
                          v
         System-level verification (error injection
         campaign, BIT verification, coverage closure)
                          |
                          v
              Traceability closure, SAS
```

The branches diverge at design (each language gets its own SDD content
under separate headings) and reconverge only at HSI integration, where both
implementations are tested against the same bus transport contract
(`design/bus_transport_contract.md`).

### 2.1 Increments

Development proceeds in the following increments, each with defined entry
and transition criteria (§2.2):

| Increment | Content |
|---|---|
| I1 | Common word/protocol layer: word encode/decode, Manchester model, parity/CRC -- both languages |
| I2 | BC scheduler -- both languages |
| I3 | RT service -- both languages |
| I4 | BM monitor -- both languages |
| I5 | BIT (PBIT/CBIT/IBIT) -- both languages |
| I6 | Error injection framework (test-profile only) -- both languages |
| I7 | HSI integration, cross-language validation |
| I8 | Coverage closure, traceability closure, visualization, SAS |

### 2.2 Transition Criteria

An increment is considered complete and the next may begin when:

1. All LLRs allocated to that increment are implemented and code-reviewed.
2. Unit tests for those LLRs pass in both languages independently.
3. No open Problem Reports of Class A (see SQAP severity scheme) exist
   against the increment's code.
4. Traceability links (HLR to LLR to Code to Test) exist for every item in the
   increment -- validated by `tools/build_and_verify.sh` step 6.

---

## 3. Standards

### 3.1 Requirements Standard

HLRs and LLRs follow the format defined in `requirements/HLR.md` /
`requirements/LLR.md`: ID, description, rationale, source paragraph
reference (MIL-STD-1553B or project-derived), verification method, DAL,
and traceability. IDs follow the scheme `HLR-1553-<AREA>-<NNN>` /
`LLR-1553-<AREA>-<NNN>`, where AREA is one of {BC, RT, BM, BIT, ERR, WORD, REDUN}.

### 3.2 Design Standard

Design data (`design/SDD.md` and associated PlantUML/DOT sources) documents
architecture, state machines, sequence diagrams, and timing budget. Every
design element cites the LLR(s) it realizes.

### 3.3 Coding Standards

**C**: MISRA C:2012 (including Amendment 2). No dynamic memory allocation,
no recursion, no unbounded loops, fixed-width types (`stdint.h`), explicit
initialization of all variables, defensive checks on all external inputs.
Every function is annotated with the LLR(s) it implements via a
`/* Implements: LLR-1553-xxx-NNN */` comment immediately preceding the
function definition. Deviations from MISRA rules require a Deviation
Record per `docs/SQAP.md` section on the Deviation Process, stored alongside the
offending file as `<file>.deviation.md`.

**Ada/SPARK**: Ada 2012 per the Ada Quality and Style Guide; SPARK 2014
subset for all packages except the error injection child packages, where
`SPARK_Mode => Off` is applied narrowly around the specific unsafe
operation and restored immediately after. Every subprogram carries `Pre`,
`Post`, `Global`, and `Depends` aspects at minimum, plus
`Exceptional_Cases` where the subprogram can legitimately propagate an
exception.

### 3.4 Test Standard

C: Unity framework, run via Ceedling. Ada: AUnit. Both suites organize test
cases 1:1 against LLR IDs where practical (`test_<llr-id-slug>` naming),
with additional boundary and robustness cases beyond the 1:1 minimum. See
SVP for full method.

---

## 4. Development Environment

| Item | C | Ada/SPARK |
|---|---|---|
| Build system | CMake (`CMakeLists.txt`) | GNAT project files (`mil1553_release.gpr`, `mil1553_test.gpr`) |
| Compiler | GCC (strict flags, see 4.1) | GNAT |
| Static analysis | MISRA-checking static analyzer (tool TBD per project, see `tools/do330_tool_qual.md`) | GNATprove (SPARK proof) |
| Unit test runner | Ceedling / Unity | AUnit / gprbuild test harness |
| Structural coverage | gcovr | GNATcoverage |
| Host platform | Developer workstation (MinGW64 / Linux CI-equivalent) | same |

### 4.1 C Compiler Flags (baseline)

```
-std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion
-Wcast-qual -Wshadow -fno-common -fstack-protector-strong
```

No `-O0` in verification builds used for coverage; optimization level and
its effect on structural coverage traceability is documented in SVP under
Tool Considerations.

---

## 5. Software Architecture Summary

See `design/SDD.md` for full detail. Top-level components, per language:

- `mil1553_word` -- command/status/data word encode-decode
- `manchester` -- bi-phase encode/decode model
- `crc_parity` -- parity computation and checking
- `bc_scheduler` -- BC major/minor frame scheduling and command issue
- `rt_service` -- RT command validation, execution, response
- `bm_monitor` -- passive bus listener/recorder
- `bus_model` -- software dual-redundant bus (A/B), used by all three roles
- `bit_pbit` / `bit_cbit` / `bit_ibit` -- the three BIT levels
- `errinject/error_inject` -- fault injection (test-profile only, isolated library)

Each role (`main_bc`, `main_rt`, `main_bm`) is a separate executable per
language, communicating over the bus transport contract -- not shared
in-process state -- matching how these functions are actually partitioned
across separate LRUs.

---

## 6. Configuration Management Interface

Full detail in SCMP. Development-relevant points:

- Source is baselined per increment (2.1); each increment close is a
  configuration baseline.
- Problem Reports block increment transition per 2.2 item 3.
- The error injection framework's isolation (separate directory, separate
  build profile) is itself a configuration management control, not just an
  implementation convenience -- it is how "this code is not in the certified
  build" is demonstrated to an auditor, not merely asserted.

---

## 7. Transition Criteria to Verification

A software item is ready to enter formal verification (structural coverage
collection counted toward Level B closure) when:

1. Code review complete, coding standard conformance checked (MISRA
   static analysis clean or all deviations recorded; GNATprove run with
   no unjustified unproved checks).
2. Unit tests for all allocated LLRs pass.
3. Traceability from LLR to code to test exists and validates with no
   orphans (`tools/build_and_verify.sh` step 6).


