# Plan for Software Aspects of Certification (PSAC)
**Project:** MIL1553 — MIL-STD-1553B Bus Controller / Remote Terminal / Bus Monitor Software
**Document ID:** MIL1553-PSAC-001
**Revision:** A (Draft)
**Date:** 2026-09-25
**Prepared under:** RTCA DO-178C / EUROCAE ED-12C

---

## 1. Purpose and Scope

This Plan for Software Aspects of Certification (PSAC) defines the means by
which the MIL1553 software will satisfy the objectives of RTCA DO-178C for
the software items listed in Section 3. It is submitted to the certifying
authority (or, for this project, the designated engineering representative
function) prior to the start of implementation, per DO-178C §4.

This document covers two independently implemented realizations of the same
functional and timing requirements:

- A **C** implementation, developed to MISRA C:2012.
- An **Ada/SPARK 2014** implementation, developed to the Ada Quality and
  Style Guide, with formal verification via GNATprove.

The two implementations are not a shared codebase split by language; each is
a complete, standalone software item validated independently and
cross-validated against the other over a defined bus transport contract
(see `design/bus_transport_contract.md`). This PSAC applies to both unless
explicitly noted.

Certification credit is sought for the core protocol, scheduling, RT
service, monitor, and BIT functions. The error injection framework
(`src/c/errinject/`, `src/ada/errinject/`) is explicitly **excluded from the
certification boundary** — see Section 8.

---

## 2. System Overview

### 2.1 Functional Description

The software under test implements a simplified dual-redundant MIL-STD-1553B
bus with three logical terminal roles:

| Role | Function | Analogous real LRU role |
|---|---|---|
| BC  | Bus Controller — schedules and issues all commands | Mission computer / central processor |
| RT1 | Remote Terminal — example: Air Data Computer | Sensor LRU |
| RT2 | Remote Terminal — example: Stores Management Unit | Effector LRU |
| BM  | Bus Monitor — passive listener, non-intrusive recorder | Maintenance/recording function |

Each role is built as a separate executable image (`main_bc`, `main_rt`,
`main_bm` in each language), reflecting how these functions are actually
partitioned onto separate LRUs in a fielded system rather than co-resident
in one binary.

### 2.2 Operating Environment

The software executes on a developer workstation (no target 1553 transceiver
hardware). Bus electrical behavior, Manchester II bi-phase encoding, and
timing constraints (response time, inter-message gap, no-response timeout)
are modeled in software with equivalent timing semantics, per
`design/timing_budget.md`. This is a **host-based verification environment**,
not a target LRU; Section 11 (Additional Considerations) records this as a
limitation for anyone seeking to lift this artifact set for actual
airborne credit.

### 2.3 System Boundary

Out of scope: physical layer transceiver hardware, avionics platform
integration, higher-level mission software consuming 1553 data. In scope:
protocol stack, message scheduling, RT command handling, bus monitoring,
Built-In Test, redundancy management, and their certification lifecycle
data.

---

## 3. Software Level (DAL) Assignment

| Software Item | DAL | Rationale |
|---|---|---|
| BC (C and Ada) | **Level B** | Loss/malfunction could contribute to a hazardous failure condition — BC scheduling failure can deny time-critical commands to flight-critical RTs. |
| RT1, RT2 (C and Ada) | **Level B** | RT malfunction (e.g., undetected corrupted command execution) can directly affect a hazardous-class function depending on what the RT fronts. |
| BM (C and Ada) | **Level C** | Passive, non-intrusive; loss of monitoring capability alone does not directly cause a hazardous condition, though it does reduce fault visibility. |

DAL assignment is a project convention for this exercise, not the output of
a System Safety Assessment. A real program must derive these from the
Functional Hazard Assessment (FHA) and System Safety Assessment (SSA) for
the host platform; see Section 11.

---

## 4. Certification Basis and Referenced Documents

### 4.1 Standards

- RTCA DO-178C, *Software Considerations in Airborne Systems and Equipment
  Certification*
- RTCA DO-330, *Software Tool Qualification Considerations*
- RTCA DO-331, *Model-Based Development and Verification Supplement* —
  referenced only where SPARK proof artifacts are treated as a form of
  model-based verification evidence (Section 7.4); this project does not
  claim full DO-331 credit.
- MIL-STD-1553B Notice 2, *Digital Time Division Command/Response Multiplex
  Data Bus*
- MISRA C:2012 (including Amendment 2)
- Ada Reference Manual (Ada 2012) and SPARK 2014 Reference Manual
- Ada Quality and Style Guide

### 4.2 Project Plans (this document set)

- SDP — `docs/SDP.md`
- SVP — `docs/SVP.md`
- SCMP — `docs/SCMP.md`
- SQAP — `docs/SQAP.md`
- SAS (outline, populated at project close) — `docs/SAS.md`

---

## 5. Software Life Cycle

A V-model life cycle is used, per the SDP. Two parallel implementation
branches (C, Ada/SPARK) share the same requirements and verification
baseline (HLR/LLR) but independent design, code, and low-level test
artifacts. Integration occurs at the Hardware/Software Integration (HSI)
level, where both language implementations are run against the same
software bus model and, in select scenarios, against each other
(C-BC ↔ Ada-RT and vice versa) to provide independent cross-implementation
evidence beyond what same-language test alone provides.

---

## 6. Software Life Cycle Data

Per DO-178C Table A-11, the following life cycle data items are produced.
Each maps to a file in this repository.

| Life Cycle Data | Artifact |
|---|---|
| PSAC | `docs/PSAC.md` (this document) |
| SDP | `docs/SDP.md` |
| SVP | `docs/SVP.md` |
| SCMP | `docs/SCMP.md` |
| SQAP | `docs/SQAP.md` |
| Software Requirements Data (HLR) | `requirements/HLR.md`, `requirements/requirements.csv` |
| Software Design Data (LLR + SDD) | `requirements/LLR.md`, `design/SDD.md` |
| Source Code | `src/c/`, `src/ada/` |
| Executable Object Code | build output (CMake / GNAT project artifacts) |
| Software Verification Cases and Procedures | `tests/c/`, `tests/ada/`, `tests/hsi/`, `tests/error_injection/` |
| Software Verification Results | `tests/coverage/`, `viz/data/` (generated) |
| Software Life Cycle Environment Config Index | `docs/SCMP.md` §Configuration Items |
| Software Configuration Index | `docs/SCMP.md` §Baselines |
| Problem Reports | `config/problem_report_template.md` + tracked instances |
| Software Accomplishment Summary | `docs/SAS.md` |
| Trace Data | `traceability/*.csv`, `traceability/trace_graph.dot` |

---

## 7. Software Verification Approach Summary

Full detail in the SVP (`docs/SVP.md`); summarized here per PSAC convention.

### 7.1 Requirements-Based Testing

Every LLR is covered by at least one requirements-based test case in the
Unity (C) or AUnit (Ada) suites, plus applicable HSI-level tests.

### 7.2 Structural Coverage

- **C**: statement, decision, and Modified Condition/Decision Coverage
  (MC/DC), collected via `gcovr`, targeting 100% per Level B objectives
  (Table A-7).
- **Ada**: equivalent coverage via GNATcoverage.

Structural coverage is analyzed against requirements-based tests only,
per DO-178C §6.4.4.2 — it is not permitted to justify coverage gaps with
error-injection-framework-driven test executions, since that framework is
outside the certification boundary (Section 8).

### 7.3 Independence

Per Level B objectives requiring independence (DO-178C Table A-3 through
A-7, independence column), review and verification activities for BC and
RT items are performed by personnel other than the item's author. This is
tracked per-objective in `docs/objectives_matrix.md`.

### 7.4 Formal Methods (Ada/SPARK)

GNATprove is used to discharge Absence of Run-Time Errors (AoRTE) proof
obligations for the Ada/SPARK implementation, and functional contract
proof where feasible (Section design intent in `docs/SDD.md`). Formal
proof results are treated as verification evidence supplementing, not
replacing, requirements-based testing — consistent with DO-333 formal
methods supplement practice, referenced informally here since this
project does not claim DO-333 credit formally.

---

## 8. Error Injection Framework — Certification Boundary Statement

The error injection framework is a **test-only capability**, structurally
isolated as a separate library in both languages (`src/c/errinject/`,
`src/ada/errinject/`), linked only into test-profile builds
(`mil1553_test.gpr` for Ada; the `MIL1553_ENABLE_ERROR_INJECTION` CMake
option for C).

It is explicitly and permanently excluded from:
- The certified/release build (`mil1553_release.gpr`, release CMake
  configuration) — the object code for the injector is not present in
  release binaries, not merely disabled by a flag.
- Structural coverage credit (Section 7.2).
- The DO-178C objectives satisfaction claims in
  `docs/objectives_matrix.md`.

Because it executes as software during verification activities and its
incorrect behavior could mask a real defect (e.g., failing to actually
inject a fault, giving false confidence in fault detection logic), it is
treated as a **verification tool** subject to DO-330 tool qualification
analysis. See `tools/do330_tool_qual.md` for TQL determination and
rationale — this is called out here because a reviewer's