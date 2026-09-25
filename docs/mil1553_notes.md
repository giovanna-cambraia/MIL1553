# MIL-STD-1553B Protocol Interpretation Notes
**Project:** MIL1553**
**Document ID:** MIL1553-NOTES-001
**Revision:** A (Draft)
**Date:** 2026-09-25
**Purpose:** Working reference for protocol interpretation decisions made
during requirements and design authoring, so HLR/LLR/SDD content has a
single place recording *why* an interpretation was chosen where the
standard allows some latitude. This is not itself a life cycle data item
required by DO-178C — it exists to keep interpretation decisions
consistent and auditable across two independent implementations.

---

## 1. Scope of This Project's Protocol Subset

MIL-STD-1553B Notice 2 is a large specification. This project implements:

- Command word, status word, data word formats (16-bit, per §4.3.2–4.3.4
  of the standard's word structure definitions)
- BC→RT (receive), RT→BC (transmit), RT→RT (receive+transmit pair)
  message formats
- Mode codes, with and without an associated data word
- Broadcast (RT address 31) for BC→RT and mode code formats
- Manchester II bi-phase encoding at the bit level (modeled, not
  electrically simulated)
- Dual-redundant Bus A/B with failover

Not implemented (explicitly out of scope, not silently omitted): RT
subaddress 0 and 31 special mode code behaviors beyond basic mode code
handling, non-bus-controller RT-initiated communication (not part of
1553B proper), and superseding-standard extensions (e.g. STANAG 3838
variants).

## 2. Command Word Field Layout Used

Bit: 1-3 4-8 9 10-14 15-19 20
SYNC RT ADDR T/R BIT SUBADDRESS WORD CNT PARITY
(3) (5) (1) (5) (5) (1)


Note: bit numbering above follows the project's internal field-width
convention (used consistently in `mil1553_word.h`/`.ads` and the Word
Format Visualizer, viz E5); it is not a verbatim reproduction of the
standard's own figure numbering, since that's proprietary document
content — implementers should cross-check field order and width against
their own copy of MIL-STD-1553B Notice 2 §4.3.3.

## 3. Response Time Interpretation

Standard allows 4–12 µs from mid-bit-zero-crossing of the command word's
final sync bit to the start of the terminal's status word response. This
project treats "response time" as measured from end-of-command-word to
start-of-status-word at the bus model level (a modeled, not electrically
measured, instant) — see `design/timing_budget.md` for the exact modeled
event points. The 14 µs no-response timeout is treated as a hard bus
model deadline: if no status word begins within 14 µs, BC declares
no-response and the modeled bus transitions accordingly.

## 4. Status Word Bit Assignment (Project Convention)

Standard reserves specific bit positions for RT address, message error,
instrumentation, service request, reserved bits, broadcast command
received, busy, subsystem flag, dynamic bus control acceptance, and
terminal flag. This project implements: Message Error, Busy, Subsystem
Flag, Terminal Flag, and Broadcast Command Received as the actively
modeled bits (these are what the error injection framework's
STATUS_BIT_INJECT fault targets — BUSY / ME / SF / TF). Instrumentation
bit and Dynamic Bus Control Acceptance are represented in the word
structure but not functionally exercised — this project does not
implement Dynamic Bus Control (no BC-to-RT role handoff).

## 5. Mode Code Subset

See `design/mode_code_table.md` for the definitive list of which mode
codes are implemented with and without a data word. This section records
the *rationale*: mode codes were selected to exercise both
data-word-present and data-word-absent code paths, and to provide a
mode code that plausibly maps to a BIT trigger (Initiate Self-Test) so
the mode code handling path and the IBIT path have a natural
integration point in HSI testing (see `tests/hsi/`).

## 6. Broadcast Handling

RT address 31 is reserved for broadcast. Per the standard, RTs receiving
a broadcast do not respond with a status word (this would cause bus
contention with multiple RTs responding simultaneously). This project's
`rt_service` models this explicitly: broadcast reception suppresses the
status word response path entirely, distinct from the SUPPRESS_RESPONSE
error injection fault, which suppresses a response that *should* have
occurred. The two are deliberately kept as distinguishable code paths —
conflating "correctly no response" with "faulty no response" would be a
design and traceability error.

## 7. Manchester II Modeling Boundary

This project models Manchester bi-phase encoding at the logical bit
level (each bit maps to a two-phase symbol, and the decode path can
detect a "Manchester violation" as an injected fault) but does not model
actual analog waveform, voltage levels, or electrical noise. This is a
deliberate simplification recorded here so no HLR/LLR is mis-written to
imply electrical-level fidelity that isn't present — see PSAC §2.2 and
§11 for the broader host-based-environment limitation.

## 8. Dual-Redundant Bus A/B Failover Interpretation

The standard permits several failover policies (implementation-specific
above the base protocol). This project's policy, recorded here for
consistency across both language implementations: BC attempts a command
on the primary bus; on no-response or a detected fault, BC retries once
on the alternate bus before declaring the RT failed for that message
cycle. This is the "Error path: no response → retry on bus B →
failover" sequence in `design/message_sequence_*.puml` (viz A7). This
policy choice is a project convention, not a MIL-STD-1553B mandate — a
real program's failover policy is platform-specific and should be
confirmed against its own ICD.

---

*These notes are a living reference. Any interpretation decision made
during requirements or design authoring that isn't obvious from the
standard's text alone should be added here, not just implicitly encoded
in a comment in one language's source.*