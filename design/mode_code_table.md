# Mode Code Table
**Project:** MIL1553
**Related:** HLR-1553-BC-004, HLR-1553-RT-005, LLR-1553-BC-006, LLR-1553-RT-008

---

## 1. Purpose

Defines the mode code subset implemented by this project, referenced
identically by the BC (`bc_scheduler`, LLR-1553-BC-006) and every RT
(`rt_service`'s mode dispatch table, LLR-1553-RT-008), so both sides of
the bus agree on which codes exist, whether they carry a data word, and
their direction.

Mode codes are carried in the command word's word-count/mode-code field
(bits 4..0) when the subaddress/mode field is 00000 or 11111 (mode
indicator), per `mil1553_notes.md` §5. Direction "receive" means the RT
receives a data word from the BC; "transmit" means the RT transmits a
data word to the BC; "none" means no data word.

## 2. Supported Mode Codes

| Code (dec) | Name | Data word | Direction | Rationale for inclusion |
|---|---|---|---|---|
| 0 | Dynamic Bus Control (announce only) | None | — | Included in the table for completeness/rejection path only — see §3, not functionally implemented (SDD 4.6 note; no BC handoff). |
| 1 | Synchronize (no data word) | None | — | Baseline no-data mode code, exercises the "no data word" path. |
| 2 | Transmit Status Word | None | transmit (implicit — the status word itself is the response) | Lets the BC re-request the last status word without a full message. |
| 3 | Initiate Self-Test | None | — | Triggers IBIT (LLR-1553-BIT-007); direct integration point between mode code handling and the BIT subsystem. |
| 4 | Transmitter Shutdown | None | — | Redundancy-management mode code; exercised alongside REDUN scenarios. |
| 5 | Override Transmitter Shutdown | None | — | Paired with code 4. |
| 6 | Inhibit Terminal Flag | None | — | Exercises TF suppression distinct from TF's normal BIT-driven set/clear (RT-006). |
| 7 | Override Inhibit Terminal Flag | None | — | Paired with code 6. |
| 8 | Reset Remote Terminal | None | — | Drives the RT state machine's RESET transition (LLR-1553-RT-013) from a mode command rather than only at startup. |
| 16 | Transmit Vector Word | Transmit | Provides a data word carrying RT-specific status detail beyond the status word's fixed bits. |
| 17 | Synchronize (with data word) | Receive | Data-word-bearing counterpart to code 1; the value carried is a synchronization reference (project convention — content is opaque to the protocol layer). |
| 18 | Transmit Last Command | Transmit | Returns the last valid command word received, for BC-side diagnosis of an illegal-command dispute. |
| 19 | Transmit BIT Word | Transmit | Direct integration point with the BIT result structure (LLR-1553-BIT-008). |

Codes not listed above (9-15, 20-31) are unsupported: an RT receiving one
sets ME and responds per LLR-1553-RT-011 (illegal command handling); a BC
schedule entry naming one is rejected at initialization per
LLR-1553-BC-006.

## 3. Code 0 Special Case

Mode code 0 (Dynamic Bus Control) is listed only so that the RT's
dispatch table (LLR-1553-RT-008) has an explicit, tested "recognized but
unsupported" entry — it is answered as an illegal command (ME set), not
silently merged with the unlisted-code range 9-15/20-31. This keeps a
clear rationale trail: 0 is a real MIL-STD-1553B mode code deliberately
out of scope (no Dynamic Bus Control handoff in this project, per SDD
§4.6), while 9-15/20-31 are simply not part of the standard's defined set
at all in this project's interpretation.

## 4. BC and RT Table Consistency

`bc_scheduler` (LLR-1553-BC-006) and `rt_service` (LLR-1553-RT-008) each
hold their own copy of this table (no shared code, per SDD §1), but both
copies are generated or hand-verified against this document at design
baseline, and any future change to this file requires updating both
language's tables in the same increment (SDP §2.2 transition criterion 4
— traceability would otherwise show a mismatch as an orphaned or
conflicting mode code entry).

## 5. Visualization

This table is the direct source for viz **A8** (Mode code table visual) —
rendered as a simple two-column-direction chart (data word: yes/no) with
each code's supporting/rejecting module annotated.

---
