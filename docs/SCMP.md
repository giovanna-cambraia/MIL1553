# Software Configuration Management Plan (SCMP)
**Project:** MIL1553
**Document ID:** MIL1553-SCMP-001
**Revision:** A (Draft)
**Date:** 2026-09-25
**Related:** PSAC (`docs/PSAC.md`), SDP (`docs/SDP.md`)

---

## 1. Purpose

This SCMP defines configuration identification, baselines, change control,
problem reporting, and configuration item lists for the MIL1553 project,
per DO-178C §7 and Table A-8 objectives.

---

## 2. Configuration Identification

### 2.1 Configuration Item (CI) Categories

| CI Category | Examples | Location |
|---|---|---|
| Plans | PSAC, SDP, SVP, SCMP, SQAP | `docs/` |
| Requirements | HLR, LLR, requirements.csv | `requirements/` |
| Design Data | SDD, PlantUML/DOT sources | `design/` |
| Source Code — C | all `.h`/`.c` under certified scope | `include/c/`, `src/c/` (excluding `src/c/errinject/`) |
| Source Code — Ada | all `.ads`/`.adb` under certified scope | `include/ada/`, `src/ada/` (excluding `src/ada/errinject/`) |
| Test-Only Code | error injection framework, both languages | `src/c/errinject/`, `src/ada/errinject/` |
| Test Data | test cases, procedures, scenario configs | `tests/`, `config/error_scenarios.yaml` |
| Verification Results | coverage XML, proof summaries, campaign JSON | `tests/coverage/`, `viz/data/` |
| Traceability Data | CSVs, trace graph | `traceability/` |
| Build/Environment Config | CMakeLists.txt, .gpr files | project root |
| Problem Reports | per SQAP severity scheme | tracked per §5 |

### 2.2 Naming and Identification

Every CI is identified by its repository path plus revision control
history (commit hash). Requirements and design elements additionally carry
project-scheme IDs (`HLR-1553-<AREA>-<NNN>`, `LLR-1553-<AREA>-<NNN>`) that
are stable across file moves — traceability keys on the ID, not the path.

### 2.3 Test-Only Code Identification (Error Injection Framework)

The error injection framework is identified as a **distinct CI category**,
separately baselined from certified-scope source code, and is the CI
category subject to the build/link exclusion controls described in §2.4.
This separation is the mechanism, not merely a statement, by which "this
code is not present in the certified build" is demonstrated — an auditor
can diff the release-profile link inputs against the CI list and see the
errinject CIs are absent by construction.

### 2.4 Build Profile Separation

| Profile | C build | Ada build | Error injection linked? |
|---|---|---|---|
| Release (certified scope) | CMake release config, `MIL1553_ENABLE_ERROR_INJECTION` undefined | `mil1553_release.gpr` | No — object files not compiled, not present in link inputs |
| Test | CMake test config, `MIL1553_ENABLE_ERROR_INJECTION` defined | `mil1553_test.gpr` | Yes — additionally gated at runtime by `config/error_scenarios.sig` signature check |

---

## 3. Baselines

### 3.1 Baseline Points

A baseline is established at the close of each SDP increment (I1–I8) and
additionally at:

- Requirements baseline (post-review, pre-design)
- Design baseline (post-review, pre-code)
- Each SOI-equivalent internal milestone (PSAC §10)

### 3.2 Baseline Content

Each baseline captures: exact CI revisions (commit hash per file/set),
the traceability snapshot at that point, and verification results current
as of that baseline (coverage, test pass/fail, proof status where
applicable).

### 3.3 Baseline Labeling

Baselines are tagged in revision control as `MIL1553-BL-<increment>-<rev>`,
e.g. `MIL1553-BL-I3-A`. Re-baselining after a Problem Report fix increments
the revision letter.

---

## 4. Change Control

### 4.1 Change Process

1. Proposed change is recorded (as a Problem Report if it addresses a
   defect, or a Change Request if it's a requirements/design evolution).
2. Impact analysis: which CIs, which baseline(s), which increment(s) are
   affected; whether regression verification scope (SVP §9) is triggered.
3. Change is implemented against the affected CIs.
4. Independent review per SVP §4 independence requirements (for Level B
   items).
5. Regression verification executed per SVP §9.
6. Re-baseline.

### 4.2 Change Control Board (Project-Internal)

For this project, the "board" is the independent reviewer role defined in
SVP §4 — there is no multi-person CCB, but the independence requirement
still applies: the person approving a change to a Level B CI is not its
author.

---

## 5. Problem Reporting

### 5.1 Problem Report Template

See `config/problem_report_template.md`. Fields: ID, date, reporter,
affected CI(s), description, severity class, root cause (once
determined), corrective action, verification of fix, status.

### 5.2 Severity Classification

| Class | Definition | Effect on increment transition (SDP §2.2) |
|---|---|---|
| **A** | Incorrect behavior affecting a DAL B item's safety-relevant function, or a structural coverage/traceability gap | Blocks transition |
| **B** | Incorrect behavior in a non-safety-relevant path (e.g., BIT dashboard cosmetic issue, BM-only Level C item) | Does not block, tracked to closure before SAS |
| **C** | Documentation-only or process observation | Tracked, no code impact |

### 5.3 Problem Report Lifecycle

Open → Root Cause Determined → Fix Implemented → Fix Verified (independent)
→ Closed. A Class A PR cannot be closed without the independent
verification step per SVP §4.

---

## 6. Software Configuration Management Environment

| Function | Tool/Mechanism |
|---|---|
| Revision control | Git |
| Baseline tagging | Git tags, scheme per §3.3 |
| Build reproducibility | CMake (C) / GNAT project files (Ada) — pinned toolchain versions recorded in `docs/SCMP.md` §7 |
| Traceability validation | `tools/build_and_verify.sh` step 6 (automated orphan check) |
| Problem report tracking | `config/problem_report_template.md` instances, one file per PR, under a tracked directory, cross-referenced in `docs/SAS.md` at closure |

## 7. Toolchain Version Record

| Tool | Version (to be pinned at project start) |
|---|---|
| GCC | TBD — recorded here once fixed |
| GNAT | TBD |
| GNATprove | TBD |
| gcovr | TBD |
| GNATcoverage | TBD |
| Ceedling/Unity | TBD |
| AUnit | TBD |

This table is populated and frozen at the Requirements Baseline milestone
(PSAC §10 item 1) and any subsequent toolchain change requires a Change
Request per §4.1, since it can affect structural coverage and proof
results.

---

## 8. Archival and Retrieval

All baselined CIs are retrievable by baseline tag from revision control.
No CI is considered part of a baseline unless it is committed and tagged;
working-tree-only changes do not constitute verification evidence.
