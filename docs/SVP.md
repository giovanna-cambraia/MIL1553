# Software Verification Plan (SVP)
**Project:** MIL1553
**Document ID:** MIL1553-SVP-001
**Revision:** A (Draft)
**Date:** 2026-09-25
**Related:** PSAC (`docs/PSAC.md`), SDP (`docs/SDP.md`)

---

## 1. Purpose

This SVP defines the verification methods, environments, independence
requirements, and tool considerations used to satisfy DO-178C verification
objectives (Tables A-3 through A-7) for both the C and Ada/SPARK
implementations of MIL1553.

---

## 2. Verification Methods

Per DO-178C, each requirement and design item is verified by one or more of:

| Method | Definition | Typical use here |
|---|---|---|
| **Test** | Execution of code against defined inputs, expected outputs checked | Default method for all LLRs with observable behavior |
| **Analysis** | Examination of the software, or its outputs, using rigorous, reasoned argument | Timing budget derivation, SPARK proof, worst-case scheduling analysis |
| **Inspection** | Visual/manual examination against a checklist | Coding standard conformance, comment/traceability annotation presence |
| **Demonstration** | Observation of functional behavior without full test rigor | BIT indicator visibility, dashboard/reporting correctness (non-safety-relevant) |

The verification method for each HLR/LLR is recorded in
`requirements/HLR.md`, `requirements/LLR.md`, and `requirements/requirements.csv`.
Test is the default and preferred method; Analysis, Inspection, and
Demonstration are used only where Test is impractical or insufficient
(e.g., proving absence of run-time errors is an Analysis activity even
though the same code is also under Test).

---

## 3. Verification Levels

### 3.1 Low-Level (Unit) Testing

- **C**: Unity test cases, run under Ceedling. One or more test cases per
  LLR at minimum; normal-range, boundary, and robustness (invalid input)
  cases required for every LLR marked DAL B.
- **Ada**: AUnit test cases, same coverage philosophy, run via the
  `mil1553_test.gpr` profile (error injection linked in but not invoked
  unless the specific test scenario calls for it).

### 3.2 Hardware/Software Integration (HSI) Testing

Since no target 1553 hardware exists, HSI here means integration of BC, RT1,
RT2, and BM roles over the software bus model (`bus_model`), which is the
closest equivalent to target integration available in this environment.
Two HSI configurations are run:

1. **Same-language**: C-BC + C-RT1 + C-RT2 + C-BM, and separately
   Ada-BC + Ada-RT1 + Ada-RT2 + Ada-BM.
2. **Cross-language**: e.g., C-BC driving Ada-RT1/RT2, and Ada-BC driving
   C-RT1/RT2, over the bus transport contract
   (`design/bus_transport_contract.md`). This is not a DO-178C-mandated
   activity — it is additional evidence this project uses to build
   confidence that the two independent implementations agree on protocol
   semantics, since they share no code.

### 3.3 System-Level Verification

- BIT verification: PBIT/CBIT/IBIT results checked against expected
  detection behavior using known-good and known-faulty conditions.
- Error injection campaign (`config/error_scenarios.yaml` driven): for
  each scenario, expected vs. observed detection/recovery behavior is
  asserted; results recorded per the structured record format in
  `docs/PSAC.md` §8 context and detailed in `tests/error_injection/`.
- Redundancy/failover verification: forced Bus A/B failure scenarios,
  confirming failover timing and continued operation.

---

## 4. Independence Requirements

Per DO-178C Table A-3–A-7 (Level B), independence is required for:

- Review of High-Level Requirements
- Review of Low-Level Requirements
- Review of Source Code (against LLR and standards)
- Test case/procedure development and review
- Verification of test results

For this project, independence is enforced procedurally: the author of a
given LLR's implementation does not author or approve the test cases
verifying that LLR, and does not perform the code review for their own
change. This is tracked per-objective in `docs/objectives_matrix.md`
(Independence column) and recorded per-PR in the change log described in
SCMP.

Level C items (BM) do not require independence per DO-178C Table A-3–A-7
applicability at Level C; BM verification independence is best-effort but
not a closure gate.

---

## 5. Structural Coverage Analysis

### 5.1 Coverage Types and Targets (Level B)

| Coverage type | C tool | Ada tool | Target |
|---|---|---|---|
| Statement | gcovr | GNATcoverage | 100% |
| Decision | gcovr | GNATcoverage | 100% |
| Modified Condition/Decision Coverage (MC/DC) | gcovr (with `--decisions --mcdc`) | GNATcoverage (`--level=stmt+mcdc`) | 100% |

BM (Level C) requires Statement and Decision coverage only; MC/DC is not
required per Table A-7 applicability at Level C, though it is collected
for BM anyway for consistency and not excluded from reporting.

### 5.2 Coverage Analysis Process

1. Run requirements-based test suites (unit + HSI) with coverage
   instrumentation enabled.
2. Generate coverage XML (`tests/coverage/c_coverage.xml`,
   `tests/coverage/ada_coverage.xml`).
3. Identify any structural coverage gap not explained by a requirements-based
   test. Per DO-178C §6.4.4.3, such gaps require either an additional
   test case or a documented analysis of why the code exists (e.g.,
   defensive code that cannot be exercised by valid or invalid inputs —
   itself subject to a deactivated-code / dead-code argument).
4. Error-injection-framework-driven executions are explicitly **not**
   counted toward structural coverage closure (see PSAC §8) — only
   requirements-based tests count.

### 5.3 Coverage of Error Injection Framework Itself

The error injection framework is verified for its own correctness (it
must reliably inject the fault it claims to inject) but its coverage is
tracked and reported **separately** from the certified-item coverage
numbers, and is not part of Level B closure evidence.

---

## 6. Verification Environment

| Item | Description |
|---|---|
| Host | Developer workstation, no target hardware |
| Bus model | Software dual-redundant bus model (`bus_model`), timing-faithful per `design/timing_budget.md` |
| Test frameworks | Unity/Ceedling (C), AUnit (Ada) |
| Coverage tools | gcovr (C), GNATcoverage (Ada) |
| Formal analysis | GNATprove (Ada/SPARK) |
| Error injection driver | `tools/run_error_campaign.sh`, scenario-driven from `config/error_scenarios.yaml` |
| Traceability validator | `tools/viz/traceability_graph.py` and related scripts, invoked by `tools/build_and_verify.sh` |

---

## 7. Tool Considerations for Verification

- **gcovr / GNATcoverage**: used to produce verification results
  (coverage percentages) that are reviewed but not independently
  re-derived; per DO-330 these are TQL5 tools (output is manually
  reviewable/spot-checkable, an error in the tool would be caught by
  a missing/implausible coverage result rather than silently accepted).
- **GNATprove**: proof results are reviewed at increment close; any
  unproved check must be either resolved or justified with a written
  rationale (e.g., a `pragma Annotate (GNATprove, False_Positive, ...)`
  requires a paired justification comment, reviewed independently of the
  author).
- **Compiler optimization level**: coverage is collected at the same
  optimization level used for the verification build, per DO-178C
  structural coverage guidance — see SDP §4.1. Object-code-level
  verification (vs. source-level) is not claimed; source-level
  structural coverage is the basis for this project's evidence.

---

## 8. Verification of Requirements Traceability

Handled procedurally by `tools/build_and_verify.sh` step 6: traceability
CSVs (`traceability/*.csv`) are validated to confirm no HLR is without a
downstream LLR, no LLR is without downstream code, no code function tagged
with an LLR ID is without a test, and no test lacks a traceable HLR. Any
orphan fails the build. This is Inspection-method verification of the
traceability data itself, distinct from Test-method verification of the
requirements' functional content.

---

## 9. Regression Verification

On any change to code, design, or requirements baselined in a prior
increment (SDP §2.1), the following regression scope applies:

- Full unit suite (affected language) re-run.
- HSI suite (same-language and cross-language) re-run if the change
  touches `bus_model`, any role's service logic, or the transport
  contract.
- Coverage re-collected; any coverage regression (previously covered code
  now uncovered) is a Problem Report per SQAP.
- Error injection campaign re-run if the change touches fault detection
  or BIT logic.

