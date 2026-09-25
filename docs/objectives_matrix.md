# DO-178C Objectives Matrix
**Project:** MIL1553
**Document ID:** MIL1553-OBJ-001
**Revision:** A (Draft)
**Date:** 2026-09-25
**Related:** PSAC (`docs/PSAC.md`)

---

## Purpose

This matrix records, for each DO-178C Annex A objective, its applicability
to this project's Level B items (BC, RT1, RT2), the applicability for the
Level C item (BM) where it differs, the evidence type, the artifact
providing that evidence, and current status. Status values: **Open**,
**In Work**, **Satisfied**, **N/A**.

Objectives requiring independence at Level B are marked (◆) — see SVP §4.
Objects annotated (BM: no ◆) shows Level C applicability differs.

---

## Table A-1 — Software Planning Process

| # | Objective | Applicability | Evidence | Artifact | Status |
|---|---|---|---|---|---|
| A-1.1 | Software development and integral process activities are defined | Lvl B, Lvl C | Plans exist and are internally consistent | PSAC, SDP, SVP, SCMP, SQAP | Satisfied |
| A-1.2 | Transition criteria between processes are defined | Lvl B, Lvl C | Increment transition criteria documented | SDP §2.2, §7 | Satisfied |
| A-1.3 | Software life cycle environment is defined | Lvl B, Lvl C | Dev/verification environment documented | SDP §4, SVP §6 | Satisfied |
| A-1.4 | Additional considerations (deactivated code, parameter data, etc.) are addressed | Lvl B, Lvl C | Deactivated/dead-code handling for defensive checks addressed | SVP §5.2 item 3 | In Work |
| A-1.5 | Software plans comply with DO-178C | Lvl B, Lvl C | Plan review against Annex A | Conformity review (SQAP §3.2) | In Work |
| A-1.6 | Software plans are coordinated | Lvl B, Lvl C | Cross-plan references consistent (PSAC↔SDP↔SVP↔SCMP↔SQAP) | All `docs/*.md` | Satisfied |

---

## Table A-2 — Software Development Process

| # | Objective | Applicability | Evidence | Artifact | Status |
|---|---|---|---|---|---|
| A-2.1 | High-level requirements comply with system requirements | Lvl B, Lvl C | HLR review record, traceability to system-level assumptions | `requirements/HLR.md`, PSAC §2 | Open (pending HLR) |
| A-2.2 | High-level requirements are accurate and consistent | Lvl B, Lvl C | HLR review | `requirements/HLR.md` | Open |
| A-2.3 | High-level requirements are compatible with target computer | N/A (host-based, PSAC §11) | — | — | N/A |
| A-2.4 | High-level requirements are verifiable | Lvl B, Lvl C | Verification method assigned per HLR | `requirements/HLR.md`, `requirements.csv` | Open |
| A-2.5 | High-level requirements conform to standards | Lvl B, Lvl C | HLR format conformance to SDP §3.1 | Inspection (SQAP §4) | Open |
| A-2.6 | High-level requirements are traceable to system requirements | Lvl B, Lvl C | Trace to PSAC §2 system overview | `traceability/*` | Open |
| A-2.7 | Algorithms are accurate | Lvl B, Lvl C | Analysis of timing/protocol algorithms | `design/timing_budget.md`, SDD | Open |
| A-2.8–2.14 | Low-level requirements: compliance, accuracy, verifiability, standards conformance, traceability to HLR, algorithm accuracy (parallel set at design level) | Lvl B, Lvl C | LLR review record | `requirements/LLR.md` | Open |
| A-2.15 | Software architecture is compatible with high-level requirements | Lvl B, Lvl C | Architecture review vs. HLR | `design/SDD.md`, `design/architecture.puml` | Open |
| A-2.16 | Software architecture is consistent | Lvl B, Lvl C | Design review | `design/SDD.md` | Open |
| A-2.17 | Software architecture is compatible with target computer | N/A (host-based) | — | — | N/A |
| A-2.18 | Software architecture is verifiable | Lvl B, Lvl C | Design review | `design/SDD.md` | Open |
| A-2.19 | Software architecture conforms to standards | Lvl B, Lvl C | Design review vs. SDP §3.2 | `design/SDD.md` | Open |
| A-2.20 | Software partitioning integrity is confirmed | N/A (no spatial/temporal partitioning claimed — three separate executables, no shared address space between roles) | — | — | N/A |
| A-2.21 | Source code complies with low-level requirements | Lvl B, Lvl C | Code review vs. LLR | `src/c/`, `src/ada/`, code review record | Open |
| A-2.22 | Source code is accurate and consistent | Lvl B, Lvl C | Code review, static analysis | Code review, MISRA analyzer output | Open |
| A-2.23 | Source code is verifiable | Lvl B, Lvl C | Code review | Code review record | Open |
| A-2.24 | Source code conforms to standards | Lvl B, Lvl C | MISRA / Ada QSG conformance | Static analyzer, GNATprove | Open |
| A-2.25 | Source code is traceable to low-level requirements | Lvl B, Lvl C | LLR annotation in code (`/* Implements: LLR-... */`, Ada comment equivalent) | `traceability/llr_to_code.csv` | Open |
| A-2.26 | Software integration process is complete | Lvl B, Lvl C | HSI integration executed | `tests/hsi/` | Open |

---

## Table A-3 — Verification of Outputs of Software Requirements Process

| # | Objective | Applicability | Evidence | Artifact | Status |
|---|---|---|---|---|---|
| A-3.1 | HLRs comply with system requirements ◆ | Lvl B (◆) / Lvl C (no ◆) | Independent HLR review | Review record, SVP §4 | Open |
| A-3.2 | HLRs are accurate and consistent ◆ | Lvl B (◆) | Independent HLR review | Review record | Open |
| A-3.3 | HLRs are compatible with target computer | N/A | — | — | N/A |
| A-3.4 | HLRs are verifiable ◆ | Lvl B (◆) | Independent review of verification method assignment | `requirements/requirements.csv` | Open |
| A-3.5 | HLRs conform to standards ◆ | Lvl B (◆) | Independent review vs. SDP §3.1 | Review record | Open |
| A-3.6 | HLRs are traceable to system requirements ◆ | Lvl B (◆) | Independent trace review | `traceability/*` | Open |
| A-3.7 | Algorithms in HLRs are accurate ◆ | Lvl B (◆) | Independent analysis | Review record | Open |

---

## Table A-4 — Verification of Outputs of Software Design Process

| # | Objective | Applicability | Evidence | Artifact | Status |
|---|---|---|---|---|---|
| A-4.1–4.7 | LLRs comply with, are traceable to, and consistent with HLRs; verifiable; conform to standards; algorithms accurate ◆ | Lvl B (◆) | Independent LLR review | `requirements/LLR.md`, review record | Open |
| A-4.8–4.13 | Software architecture compatible with HLRs, consistent, verifiable, conforms to standards, partitioning integrity confirmed ◆ | Lvl B (◆) (partitioning: N/A, see A-2.20) | Independent design review | `design/SDD.md`, review record | Open |

---

## Table A-5 — Verification of Outputs of Software Coding & Integration Process

| # | Objective | Applicability | Evidence | Artifact | Status |
|---|---|---|---|---|---|
| A-5.1 | Source code complies with low-level requirements ◆ | Lvl B (◆) | Independent code review | Code review record | Open |
| A-5.2 | Source code complies with software architecture ◆ | Lvl B (◆) | Independent code review | Code review record | Open |
| A-5.3 | Source code is verifiable ◆ | Lvl B (◆) | Independent code review | Code review record | Open |
| A-5.4 | Source code conforms to standards ◆ | Lvl B (◆) | MISRA analyzer / GNATprove + independent review of deviations | Static analysis output, deviation records | Open |
| A-5.5 | Source code is traceable to low-level requirements ◆ | Lvl B (◆) | Independent trace review | `traceability/llr_to_code.csv` | Open |
| A-5.6 | Source code is accurate and consistent ◆ | Lvl B (◆) | Independent code review | Code review record | Open |
| A-5.7 | Output of software integration process is complete and correct ◆ | Lvl B (◆) | HSI test results review | `tests/hsi/`, review record | Open |

---

## Table A-6 — Testing of Outputs of Integration Process

| # | Objective | Applicability | Evidence | Artifact | Status |
|---|---|---|---|---|---|
| A-6.1 | Executable object code complies with high-level requirements ◆ | Lvl B (◆) | Requirements-based test, HLR-level | `tests/hsi/`, `tests/error_injection/` | Open |
| A-6.2 | Executable object code is robust with high-level requirements ◆ | Lvl B (◆) | Robustness test cases (invalid inputs, error injection scenarios) | `tests/error_injection/`, error campaign report | Open |
| A-6.3 | Executable object code complies with low-level requirements ◆ | Lvl B (◆) | Requirements-based unit test, LLR-level | `tests/c/`, `tests/ada/` | Open |
| A-6.4 | Executable object code is robust with low-level requirements ◆ | Lvl B (◆) | Boundary/robustness unit test cases | `tests/c/`, `tests/ada/` | Open |
| A-6.5 | Test coverage of software structure (MC/DC) is achieved ◆ | Lvl B (◆) (BM: statement+decision only, no ◆) | Structural coverage analysis | `tests/coverage/*.xml`, gcovr/GNATcoverage output | Open |
| A-6.6 | Test coverage of software structure (decision coverage) is achieved ◆ | Lvl B (◆) / Lvl C | Structural coverage analysis | `tests/coverage/*.xml` | Open |
| A-6.7 | Test coverage of software structure (statement coverage) is achieved ◆ | Lvl B (◆) / Lvl C | Structural coverage analysis | `tests/coverage/*.xml` | Open |
| A-6.8 | Test coverage of data and control coupling is achieved ◆ | Lvl B (◆) | Coupling analysis between `bus_model` and role modules (BC/RT/BM) | Coverage tool coupling report, SDD cross-reference | Open |

---

## Table A-7 — Verification of Verification Process Results

| # | Objective | Applicability | Evidence | Artifact | Status |
|---|---|---|---|---|---|
| A-7.1 | Test procedures are correct ◆ | Lvl B (◆) | Independent review of test cases vs. requirements | Test case review record | Open |
| A-7.2 | Test results are correct and discrepancies explained ◆ | Lvl B (◆) | Independent review of test results, PR raised for any failure | `tests/coverage/`, Problem Reports | Open |
| A-7.3 | Test coverage of high-level requirements is achieved | Lvl B, Lvl C | Requirements-based test coverage vs. HLR set | `traceability/test_to_hlr.csv` | Open |
| A-7.4 | Test coverage of low-level requirements is achieved | Lvl B, Lvl C | Requirements-based test coverage vs. LLR set | `traceability/llr_to_code.csv`, `code_to_test.csv` | Open |
| A-7.5 | Test coverage of software structure (MC/DC) is achieved | Lvl B (BM: N/A) | Same as A-6.5, independently reviewed | `tests/coverage/*.xml` | Open |
| A-7.6 | Test coverage of software structure (decision coverage) is achieved | Lvl B, Lvl C | Same as A-6.6, independently reviewed | `tests/coverage/*.xml` | Open |
| A-7.7 | Test coverage of software structure (statement coverage) is achieved | Lvl B, Lvl C | Same as A-6.7, independently reviewed | `tests/coverage/*.xml` | Open |
| A-7.8 | Test coverage of data and control coupling is achieved | Lvl B, Lvl C | Same as A-6.8, independently reviewed | Coverage tool coupling report | Open |

---

## Table A-8 — Software Configuration Management Process

| # | Objective | Applicability | Evidence | Artifact | Status |
|---|---|---|---|---|---|
| A-8.1 | Configuration items are identified | Lvl B, Lvl C | CI list, naming scheme | SCMP §2 | Satisfied |
| A-8.2 | Baselines and traceability are established | Lvl B, Lvl C | Baseline tags, content definition | SCMP §3 | In Work |
| A-8.3 | Problem reporting, change control, change review, configuration status accounting exist | Lvl B, Lvl C | PR template, change process, severity scheme | SCMP §4–§5 | Satisfied |
| A-8.4 | Archive, retrieval, release records exist | Lvl B, Lvl C | Archival process defined | SCMP §8 | Satisfied |
| A-8.5 | Software load is controlled | N/A (host-based, no target load process, PSAC §11) | — | — | N/A |
| A-8.6 | Software life cycle environment control is established | Lvl B, Lvl C | Toolchain version record | SCMP §7 | Open (versions TBD) |

---

## Table A-9 — Software Quality Assurance Process

| # | Objective | Applicability | Evidence | Artifact | Status |
|---|---|---|---|---|---|
| A-9.1 | Software conformance to standards is assured | Lvl B, Lvl C | Audit findings | SQAP §3, Audit Log | In Work |
| A-9.2 | Life cycle process conformance is assured | Lvl B, Lvl C | Conformity reviews | SQAP §3.2 | In Work |
| A-9.3 | Transition criteria compliance is assured | Lvl B, Lvl C | Increment audit vs. SDP §2.2 | SQAP §3.1 | In Work |
| A-9.4 | SQA records are maintained | Lvl B, Lvl C | Audit log, review records | SQAP §6, Appendix | In Work |
| A-9.5 | SQA has independence and objective authority to enter Problem Reports | Lvl B, Lvl C | QA role definition | SQAP §2 | Satisfied |

---

## Table A-10 — Certification Liaison Process

| # | Objective | Applicability | Evidence | Artifact | Status |
|---|---|---|---|---|---|
| A-10.1 | Communication and understanding with certification authority is established | N/A — no engaged certifying authority (PSAC §11) | — | — | N/A |
| A-10.2 | Compliance substantiation is available | Lvl B, Lvl C (self-assessed, no external authority) | This matrix, SAS at closure | `docs/objectives_matrix.md`, `docs/SAS.md` | In Work |

---

## Notes on Applicability Deltas (BM, Level C)

Per DO-178C Annex A applicability tables, Level C removes MC/DC coverage
(A-6.5/A-7.5) and reduces independence (◆) requirements relative to Level
B across Tables A-3 through A-7. BM-specific rows are called out inline
above; where not called out, BM applicability equals the Level B row minus
the ◆ independence mark, per standard Annex A Level C reduction.

## Status Legend

- **Open** — not yet started
- **In Work** — artifact exists in draft or partial form
- **Satisfied** — objective evidence complete and reviewed
- **N/A** — objective does not apply, rationale given inline

This matrix is a living document, updated at each increment close (SDP
§2.1) and audited at each milestone (SQAP §3.1).

