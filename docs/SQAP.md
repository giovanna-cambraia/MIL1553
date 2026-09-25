# Software Quality Assurance Plan (SQAP)
**Project:** MIL1553
**Document ID:** MIL1553-SQAP-001
**Revision:** A (Draft)
**Date:** 2026-09-25
**Related:** PSAC (`docs/PSAC.md`), SDP (`docs/SDP.md`), SCMP (`docs/SCMP.md`)

---

## 1. Purpose

This SQAP defines the quality assurance activities — audits, reviews, and
non-conformance handling — that provide objective evidence the MIL1553
software life cycle processes are being followed, per DO-178C §8 and
Table A-9/A-10 objectives.

---

## 2. QA Role and Independence

For this project, the QA function is performed by whichever engineer did
**not** author or independently verify the artifact under review (i.e.,
the same independence pool used in SVP §4, wearing a different hat). QA
review is distinct from the technical review already required by SVP §4 —
QA checks *process conformance* (was the right procedure followed, is the
artifact where it should be, does it have the required fields), not
technical correctness.

---

## 3. Audits

### 3.1 Process Audits

Conducted at each increment close (SDP §2.1) and at each baseline (SCMP
§3.1). Checks:

- All CIs for the increment exist at the expected paths with correct
  naming/ID conventions (SCMP §2.2).
- All required traceability links exist (cross-checked against the
  automated validator output — QA reviews the validator's report, not a
  substitute manual trace).
- Coding standard conformance evidence exists (MISRA deviation records
  where applicable, GNATprove proof summary with no unjustified failures).
- Test results are archived and correspond to the code baseline actually
  under review (not a stale run).

### 3.2 Conformity Review

At each internal milestone (PSAC §10), a conformity review confirms the
software product (code + life cycle data) matches what the plans (PSAC,
SDP, SVP, SCMP, this SQAP) say should exist at that point. Discrepancies
are raised as Problem Reports per SCMP §5, classified per severity.

### 3.3 Audit Records

Each audit produces a short record: date, scope, findings, and
disposition (closed / PR raised). Records are stored as dated entries
appended to a QA log (`docs/SQAP.md` §Appendix, populated as the project
proceeds — not fabricated in advance).

---

## 4. Reviews

QA participates in, and confirms the conduct of, the following reviews
(the reviews themselves are technical activities defined in SVP §4; QA's
role is confirming they happened with the required independence and were
recorded):

| Review | Trigger | Independence required (Level B items) |
|---|---|---|
| Requirements review (HLR) | Requirements baseline | Yes |
| Requirements review (LLR) | Design baseline | Yes |
| Design review (SDD, state machines, sequence diagrams) | Design baseline | Yes |
| Code review | Per change, before increment transition | Yes (author ≠ reviewer) |
| Test case/procedure review | Per LLR, before verification closure | Yes |
| Test results review | Per verification run counted toward closure | Yes |

BM (Level C) reviews follow the same process but independence is
best-effort per SVP §4, consistent with Table A-3–A-7 applicability at
Level C.

---

## 5. Non-Conformance Handling

### 5.1 Sources of Non-Conformance

- Audit findings (§3)
- Missed transition criteria (SDP §2.2, §7)
- Coverage or traceability gaps not resolved per SVP §5.2/§8
- MISRA deviations lacking a recorded justification
- Unjustified unproved GNATprove checks

### 5.2 Handling

Every non-conformance is logged as a Problem Report per SCMP §5 and
classified by the same severity scheme (A/B/C). QA does not have authority
to waive a Class A non-conformance — resolution requires the independent
fix-verification step per SVP §4, same as any other Class A PR.

### 5.3 Deviation Records (MISRA)

A MISRA deviation is not itself a non-conformance if properly recorded
(`<file>.deviation.md`, per SDP §3.3) with rationale and reviewed
independently. An *unrecorded* deviation found in audit is a Class A
finding.

---

## 6. Quality Records Retained

| Record | Location |
|---|---|
| Audit records | `docs/SQAP.md` Appendix (log) |
| Review records (who, when, independence confirmed) | Referenced per-CI in SCMP change log |
| Problem Reports | Per SCMP §5, one file per PR |
| MISRA deviation records | `<file>.deviation.md` alongside affected source |
| GNATprove justification comments | Inline in Ada source, reviewed at code review |

---

## 7. Relationship to Objectives Matrix

QA activities in this plan collectively provide the evidence trail
underlying the "Evidence" and "Status" columns of
`docs/objectives_matrix.md` for the process-assurance objectives (Table
A-8, A-9). QA does not independently re-derive technical verification
evidence (that's SVP's domain) — it confirms the evidence exists, is
current, and was produced with the required independence.

---

## Appendix: QA Audit Log

*(Populated as audits are actually conducted during the project. Empty at
plan baseline.)*

| Date | Scope | Findings | Disposition |
|---|---|---|---|
| — | — | — | — |

