# High-Level Requirements (HLR)
**Project:** MIL1553
**Document ID:** MIL1553-HLR-001
**Revision:** A (Draft)
**Date:** 2026-09-28
**Related:** PSAC (`docs/PSAC.md`), SDP (`docs/SDP.md`) 3.1, `docs/mil1553_notes.md`

---

## 1. Conventions

**ID scheme:** `HLR-1553-<AREA>-<NNN>`. Areas: `WORD`, `BC`, `RT`, `BM`,
`BUS`, `REDUN`, `BIT`, `ERR`. (`BUS` is added to the area list in SDP 3.1.)

**Fields per requirement:**

| Field | Meaning |
|---|---|
| Statement | The requirement, written with "shall" |
| Rationale | Why it exists |
| Source | MIL-STD-1553B Notice 2 paragraph, or the project document it derives from |
| Origin | `Standard` (from MIL-STD-1553B) or `Derived` (project decision; must be fed back to the system safety process per DO-178C 5.1.1) |
| Verification | Test / Analysis / Inspection / Demonstration |
| DAL | Level B (BC, RT, shared protocol/bus/BIT code), Level C (BM), or N/A (test tool) |
| Parent | System-level allocation from Section 2 |
| Downstream | LLR area(s) that will decompose it (IDs assigned in `LLR.md`) |

**Applicability:** every HLR applies to both the C and the Ada/SPARK
implementation unless stated otherwise. The two implementations must satisfy
identical statements.

**On paragraph references:** MIL-STD-1553B paragraph numbers below are given
at the granularity I am confident of (section / subsection). They must be
checked against the licensed copy of Notice 2 at the Requirements Baseline
review (PSAC 10, item 1) and corrected where needed.

## 2. System-Level Allocations (Parents)

Derived from PSAC 2. These are the parents used for A-2.6 / A-3.6 tracing.

| ID | System-level statement |
|---|---|
| SYS-1553-001 | The system shall provide a dual-redundant MIL-STD-1553B bus with one BC, two RTs and one BM. |
| SYS-1553-002 | The system shall implement the supported MIL-STD-1553B Notice 2 word and message formats. |
| SYS-1553-003 | The system shall meet the MIL-STD-1553B timing constraints (response time, inter-message gap, no-response timeout). |
| SYS-1553-004 | The system shall detect, report and recover from bus and terminal faults. |
| SYS-1553-005 | The system shall provide power-up, continuous and initiated built-in test. |
| SYS-1553-006 | Test-only fault injection shall not exist in, or be able to affect, the certified build. |
| SYS-1553-007 | Independent C and Ada/SPARK implementations shall behave identically at the bus level. |

## 3. Summary

| Area | Count | IDs |
|---|---|---|
| WORD | 5 | 001-005 |
| BC | 5 | 001-005 |
| RT | 7 | 001-007 |
| BM | 1 | 001 |
| BUS | 1 | 001 |
| REDUN | 1 | 001 |
| BIT | 4 | 001-004 |
| ERR | 6 | 001-006 |
| **Total** | **30** | |

---

## 4. Word Formats and Encoding (WORD)

### HLR-1553-WORD-001 — Command word encoding and decoding
- **Statement:** The software shall encode and decode the command word as a 3-bit-time command/status sync, a 16-bit information field (RT address 5 bits, T/R bit 1 bit, subaddress/mode 5 bits, word count/mode code 5 bits) and 1 parity bit. A word count field of zero shall represent 32 data words.
- **Rationale:** The command word is the basis of every message; wrong field packing breaks interoperability between the C and Ada implementations.
- **Source:** MIL-STD-1553B 4.3.3.5 (word formats), command word subclause
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002
- **Downstream:** LLR-1553-WORD

### HLR-1553-WORD-002 — Status word encoding and decoding
- **Statement:** The software shall encode and decode the status word as a command/status sync, RT address (5 bits), the message error, instrumentation, service request, broadcast command received, busy, subsystem flag, dynamic bus control acceptance and terminal flag bits, reserved bits transmitted as zero, and 1 parity bit.
- **Rationale:** Status bits are the RT's only channel for reporting message and terminal condition to the BC.
- **Source:** MIL-STD-1553B 4.3.3.5, status word subclause
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002
- **Downstream:** LLR-1553-WORD

### HLR-1553-WORD-003 — Data word encoding and decoding
- **Statement:** The software shall encode and decode the data word as a data sync (opposite polarity to the command/status sync), a 16-bit data field and 1 parity bit.
- **Rationale:** Sync polarity is how receivers distinguish data words from command/status words.
- **Source:** MIL-STD-1553B 4.3.3.5, data word subclause
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002
- **Downstream:** LLR-1553-WORD

### HLR-1553-WORD-004 — Manchester II bi-phase encoding model
- **Statement:** The software shall encode each word as 20 Manchester II bi-phase bit cells (3 sync, 16 data, 1 parity) at 1 Mbit/s, so that a word occupies 20 us of modeled bus time, and shall decode bit cells back to words. The decoder shall report a Manchester violation when a data or parity bit cell lacks its mid-bit transition.
- **Rationale:** Manchester violation is a required detectable fault; bit-level modeling gives the error injection framework a real place to corrupt a word.
- **Source:** MIL-STD-1553B 4.3.3 (signal encoding and transmission rate); modeling boundary per `mil1553_notes.md` 7
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002
- **Downstream:** LLR-1553-WORD

### HLR-1553-WORD-005 — Parity and sync validation
- **Statement:** The software shall generate odd parity over the 16 information bits of every transmitted word, and every receiving terminal shall verify parity and sync type on every received word. A word failing either check shall be flagged invalid.
- **Rationale:** Parity and sync are the basic word-level integrity checks; CBIT relies on them (HLR-1553-BIT-002).
- **Source:** MIL-STD-1553B 4.3.3.5 (parity, sync)
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002, SYS-1553-004
- **Downstream:** LLR-1553-WORD

---

## 5. Bus Controller (BC)

### HLR-1553-BC-001 — Major/minor frame scheduling
- **Statement:** The BC shall execute a cyclic schedule of messages organized as a major frame composed of a fixed number of minor frames. Each message shall be assigned to a minor frame slot, and the schedule (message list, slot assignment, frame periods) shall be loaded from configuration. The BC shall detect and report a minor frame overrun.
- **Rationale:** Deterministic cyclic scheduling is the standard 1553 operating model, and overrun detection prevents silent schedule slip.
- **Source:** MIL-STD-1553B BC operation; frame structure is a system convention (`config/bus_config.yaml`)
- **Origin:** Standard / Derived (frame structure)
- **Verification:** Test, Analysis (schedule fits frame budget)
- **DAL:** B
- **Parent:** SYS-1553-001
- **Downstream:** LLR-1553-BC

### HLR-1553-BC-002 — BC-to-RT and RT-to-BC message transfer
- **Statement:** The BC shall support BC-to-RT transfers (receive command, 1-32 data words, then RT status word) and RT-to-BC transfers (transmit command, RT status word, then 1-32 data words), and shall check that the data word count received matches the command word count.
- **Rationale:** These are the two fundamental message formats.
- **Source:** MIL-STD-1553B 4.3.3.6 (message formats: BC to RT, RT to BC)
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002
- **Downstream:** LLR-1553-BC

### HLR-1553-BC-003 — RT-to-RT message transfer
- **Statement:** The BC shall support RT-to-RT transfers by issuing a receive command to the receiving RT followed by a transmit command to the transmitting RT, and shall validate the status words of both RTs and the consistency of the two command word counts.
- **Rationale:** RT-to-RT is the most error-prone format (two commands, two statuses, one data block) and the one most often mishandled.
- **Source:** MIL-STD-1553B 4.3.3.6 (RT to RT)
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002
- **Downstream:** LLR-1553-BC

### HLR-1553-BC-004 — Mode code and broadcast command issuance
- **Statement:** The BC shall issue mode commands with and without a data word, using subaddress 00000 or 11111, and shall issue broadcast commands to RT address 31 for the supported message and mode command formats. For a broadcast, the BC shall not expect any RT status word.
- **Rationale:** A BC that waits for a status after a broadcast would declare a false no-response fault on every broadcast.
- **Source:** MIL-STD-1553B 4.3.3.6 (mode command and broadcast formats); supported subset in `design/mode_code_table.md`
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002
- **Downstream:** LLR-1553-BC

### HLR-1553-BC-005 — BC bus timing
- **Statement:** The BC shall leave an inter-message gap of at least 4 us between messages, and shall declare a no-response condition when no RT status word begins within 14 us of the end of the command word.
- **Rationale:** Gap and timeout define the BC side of the timing contract that RT response time (HLR-1553-RT-002) is checked against.
- **Source:** MIL-STD-1553B 4.3.3 (intermessage gap, minimum no-response timeout)
- **Origin:** Standard
- **Verification:** Test, Analysis (`design/timing_budget.md`)
- **DAL:** B
- **Parent:** SYS-1553-003
- **Downstream:** LLR-1553-BC

---

## 6. Remote Terminal (RT)

### HLR-1553-RT-001 — Command reception and validation
- **Statement:** An RT shall act only on commands addressed to its own RT address or to broadcast address 31. Before executing a command it shall validate parity and sync of every received word, the T/R bit, the subaddress or mode code, and the word count against its configured subaddress table.
- **Rationale:** Executing an unvalidated command is the main hazard an RT presents to its subsystem.
- **Source:** MIL-STD-1553B RT operation; subaddress table in `config/bus_config.yaml`
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002, SYS-1553-004
- **Downstream:** LLR-1553-RT

### HLR-1553-RT-002 — RT response time
- **Statement:** After receiving a valid command that requires a response, the RT shall begin its status word between 4 us and 12 us after the end of the command word.
- **Rationale:** Response outside the window is a protocol violation and either invites a false BC timeout (too late) or collides with the gap (too early).
- **Source:** MIL-STD-1553B 4.3.3 (response time)
- **Origin:** Standard
- **Verification:** Test, Analysis (`design/timing_budget.md`)
- **DAL:** B
- **Parent:** SYS-1553-003
- **Downstream:** LLR-1553-RT

### HLR-1553-RT-003 — RT status word bit behavior
- **Statement:** The RT shall set and clear the status word bits as follows: message error when a message is rejected; busy when the subsystem cannot accept or supply data; subsystem flag and terminal flag when the corresponding condition is reported by the subsystem or by BIT; broadcast command received after a broadcast; and RT address always equal to its own address.
- **Rationale:** The BC makes recovery decisions from these bits, so each must have a defined set and clear condition.
- **Source:** MIL-STD-1553B 4.3.3.5 (status word), RT operation
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002, SYS-1553-004
- **Downstream:** LLR-1553-RT

### HLR-1553-RT-004 — RT-to-RT message handling
- **Statement:** As receiving terminal the RT shall accept the data words following the transmitting RT's status word and respond with its own status word after the last data word. As transmitting terminal it shall respond to the transmit command with its status word followed by the requested data words.
- **Rationale:** Each RT sees only part of an RT-to-RT message, so each role needs explicit handling.
- **Source:** MIL-STD-1553B 4.3.3.6 (RT to RT)
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002
- **Downstream:** LLR-1553-RT

### HLR-1553-RT-005 — Mode code handling
- **Statement:** The RT shall handle the supported mode codes listed in `design/mode_code_table.md`, both with and without a data word, including the mode codes that return a status word, transmit a BIT word and initiate self-test.
- **Rationale:** Mode codes are how the BC manages an RT and how IBIT is triggered externally.
- **Source:** MIL-STD-1553B 4.3.3.5 (mode codes); subset in `design/mode_code_table.md`
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002, SYS-1553-005
- **Downstream:** LLR-1553-RT

### HLR-1553-RT-006 — Broadcast handling
- **Statement:** The RT shall accept and execute valid broadcast commands (address 31) without transmitting a status word, and shall set the broadcast command received bit in its next status word. This behavior shall be implemented separately from any fault-driven response suppression.
- **Rationale:** Simultaneous RT responses would cause bus contention. "Correctly silent" must be distinguishable from "faulty silent" (`mil1553_notes.md` 6).
- **Source:** MIL-STD-1553B 4.3.3.6 (broadcast formats)
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-002
- **Downstream:** LLR-1553-RT

### HLR-1553-RT-007 — Invalid, illegal and busy command handling
- **Statement:** On a message with a transmission error (parity, sync, Manchester, word count) the RT shall discard the message, set message error and not respond. On a valid transmission carrying an illegal command (unsupported subaddress, word count or mode code) the RT shall not execute it and shall respond with a status word with message error set. When busy, the RT shall respond with a status word with busy set and no data words.
- **Rationale:** Separating transmission errors from illegal commands lets the BC tell a bus problem from a command problem. This is a project convention within the latitude the standard allows.
- **Source:** MIL-STD-1553B RT operation; convention in `mil1553_notes.md` (to be added)
- **Origin:** Standard / Derived (convention)
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-004
- **Downstream:** LLR-1553-RT

---

## 7. Bus Monitor (BM)

### HLR-1553-BM-001 — Passive bus monitoring
- **Statement:** The BM shall record every word on both buses A and B with a timestamp in microseconds, bus identifier and validity flag (parity, sync, Manchester), and shall never transmit or otherwise influence bus traffic.
- **Rationale:** The BM is the independent evidence source for the timeline and response-time visualizations; any influence on the bus would invalidate it.
- **Source:** MIL-STD-1553B bus monitor operation
- **Origin:** Standard
- **Verification:** Test, Inspection (no transmit path in BM code)
- **DAL:** C
- **Parent:** SYS-1553-001
- **Downstream:** LLR-1553-BM

---

## 8. Software Bus and Redundancy (BUS, REDUN)

### HLR-1553-BUS-001 — Dual-redundant software bus and transport contract
- **Statement:** The software shall provide a bus model with two independent channels, A and B, carrying words with modeled timing. BC, RT and BM shall communicate only through this bus according to `design/bus_transport_contract.md`, with no shared in-process state, so that any C role can operate against any Ada role.
- **Rationale:** Enables the cross-language HSI evidence and mirrors separate-LRU partitioning.
- **Source:** PSAC 2.1, 5; `design/bus_transport_contract.md`
- **Origin:** Derived
- **Verification:** Test, Inspection
- **DAL:** B
- **Parent:** SYS-1553-001, SYS-1553-007
- **Downstream:** LLR-1553-BUS

### HLR-1553-REDUN-001 — Bus A/B retry and failover
- **Statement:** On a no-response or a detected bus fault, the BC shall retry the message once on the alternate bus before declaring the RT failed for that message cycle. When the primary bus fails a configured number of consecutive messages, the BC shall switch the primary bus designation, log the failover and report degraded status.
- **Rationale:** Redundancy is only useful if failover is deterministic and visible.
- **Source:** MIL-STD-1553B redundant bus provisions; policy per `mil1553_notes.md` 8
- **Origin:** Standard / Derived (policy)
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-004
- **Downstream:** LLR-1553-REDUN

---

## 9. Built-In Test (BIT)

### HLR-1553-BIT-001 — Power-up BIT
- **Statement:** At power-up, before entering operation, each terminal shall run PBIT consisting of a RAM walking-ones/zeros test, ROM checksum, encoder/decoder loopback, timer sanity check and idle check of both buses. A PBIT failure shall prevent entry to OPERATIONAL.
- **Rationale:** An RT that cannot trust its own memory or codec must not answer commands.
- **Source:** Project-derived from SYS-1553-005
- **Origin:** Derived
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-005
- **Downstream:** LLR-1553-BIT

### HLR-1553-BIT-002 — Continuous BIT
- **Statement:** During operation each terminal shall run CBIT without disturbing bus timing: parity check on every received word, protocol conformance checks (sync, word count, response window), per-bus health counters for A and B, and a watchdog on the scheduler (BC) or service loop (RT).
- **Rationale:** CBIT is how faults developing in service are found.
- **Source:** Project-derived from SYS-1553-005
- **Origin:** Derived
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-005, SYS-1553-004
- **Downstream:** LLR-1553-BIT

### HLR-1553-BIT-003 — Initiated BIT
- **Statement:** On request (initiate self-test mode code or local test command) a terminal shall run IBIT consisting of a full loopback on both channels, a cross-channel comparison and a known-pattern injection with echo verification, and shall return to its prior state afterwards.
- **Rationale:** IBIT gives a deeper on-ground or on-demand check than CBIT.
- **Source:** Project-derived; trigger via MIL-STD-1553B initiate self-test mode command
- **Origin:** Derived
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-005
- **Downstream:** LLR-1553-BIT

### HLR-1553-BIT-004 — BIT result reporting
- **Statement:** BIT results shall be reported through (a) the terminal flag and subsystem flag bits in the RT status word, (b) the BIT word returned by the transmit BIT word mode command, (c) an internal BIT result structure identifying the failed test and the level (PBIT/CBIT/IBIT), and (d) an API for the health monitor.
- **Rationale:** The same result must be visible to the BC over the bus and to the local health monitor.
- **Source:** MIL-STD-1553B 4.3.3.5 (status word bits, mode codes); result structure project-derived
- **Origin:** Standard / Derived
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-005
- **Downstream:** LLR-1553-BIT

---

## 10. Error Handling and Error Injection (ERR)

### HLR-1553-ERR-001 — Word-level error detection
- **Statement:** Every terminal shall detect on each received word a parity error, an invalid sync and a Manchester violation, shall reject the enclosing message, and shall update its error counters.
- **Rationale:** Word-level detection is the first line of defense against corrupted traffic.
- **Source:** MIL-STD-1553B 4.3.3.5, 4.3.3 (encoding)
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-004
- **Downstream:** LLR-1553-ERR

### HLR-1553-ERR-002 — Message-level error detection
- **Statement:** The BC shall detect no-response, a status word with a wrong RT address, unexpected message error, busy, subsystem flag or terminal flag bits, and a data word count that does not match the command word. The BC shall react per HLR-1553-REDUN-001 and HLR-1553-BC-005.
- **Rationale:** The BC is the point where message-level faults must be turned into a recovery action.
- **Source:** MIL-STD-1553B BC operation, status word
- **Origin:** Standard
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-004
- **Downstream:** LLR-1553-ERR

### HLR-1553-ERR-003 — Error record and reporting
- **Statement:** Every detected error shall be recorded in a fixed-size, non-overwriting-on-read log with timestamp (us), terminal, bus, error type and message identifier, with an overflow counter, and shall be readable through the health monitor API.
- **Rationale:** Error records are the observed side of every error injection comparison and of BIT coverage evidence.
- **Source:** Project-derived
- **Origin:** Derived
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-004
- **Downstream:** LLR-1553-ERR

### HLR-1553-ERR-004 — Compile-time exclusion of error injection
- **Statement:** The release build of both the C and Ada implementations shall contain no error injection code or data. Release build inputs shall not include the `errinject` sources, and the release link map shall contain no `errinject` symbols.
- **Rationale:** The certification argument is that the injector does not exist in the certified object code, not that it is disabled (PSAC 8).
- **Source:** PSAC 8; SCMP 2.4
- **Origin:** Derived
- **Verification:** Inspection (build inputs, link map), Test (release build fails if errinject is referenced)
- **DAL:** B
- **Parent:** SYS-1553-006
- **Downstream:** LLR-1553-ERR

### HLR-1553-ERR-005 — Runtime gating of error injection
- **Statement:** In a test build, error injection shall remain inert until (a) the test harness arms it through the dedicated enable interface and (b) the scenario file's SHA-256 matches the signature in `config/error_scenarios.sig`. Any failure of (a) or (b) shall leave the injector inert and shall be logged.
- **Rationale:** Prevents accidental or unreviewed scenarios from perturbing a test run, and gives a checkable control on scenario content.
- **Source:** PSAC 8; SCMP 2.4
- **Origin:** Derived
- **Verification:** Test
- **DAL:** B
- **Parent:** SYS-1553-006
- **Downstream:** LLR-1553-ERR

### HLR-1553-ERR-006 — Injection fault classes and result record (tool operational requirement)
- **Statement:** The test build error injection framework shall support the fault classes PARITY_BIT_FLIP, CORRUPT_SYNC, SUPPRESS_RESPONSE, DELAY_RESPONSE, WRONG_WORD_COUNT, STATUS_BIT_INJECT (BUSY, ME, SF, TF), ILLEGAL_COMMAND, BUS_A_FAIL and BUS_B_FAIL, driven by scenarios in `config/error_scenarios.yaml`. For each scenario it shall produce a record containing scenario_id, timestamp_us, terminal, bus, fault, expected, observed, pass_fail, HLR, LLR and test_id.
- **Rationale:** Operational requirements of the injector as a verification tool (DO-330); its results are used as robustness evidence (Table A-6, objectives 2 and 4).
- **Source:** PSAC 8, 9; `tools/do330_tool_qual.md`
- **Origin:** Derived
- **Verification:** Test
- **DAL:** N/A (verification tool; qualification level per `tools/do330_tool_qual.md`)
- **Parent:** SYS-1553-006
- **Downstream:** LLR-1553-ERR

---

## 11. Coverage Check

Each PSAC/task-mandated topic and where it is covered:

| Required topic | HLR |
|---|---|
| BC major/minor frame scheduling | BC-001 |
| Command word formats and encoding | WORD-001, BC-002 |
| RT response time window (4-12 us) | RT-002 |
| RT status word bit definitions | WORD-002, RT-003 |
| RT-to-RT command pair | BC-003, RT-004 |
| Mode codes (with/without data) | BC-004, RT-005 |
| Broadcast (address 31) | BC-004, RT-006 |
| Bus A/B redundancy and failover | BUS-001, REDUN-001 |
| Error detection and reporting | ERR-001, ERR-002, ERR-003, RT-007 |
| PBIT / CBIT / IBIT | BIT-001, BIT-002, BIT-003, BIT-004 |
| Error injection gating | ERR-004, ERR-005, ERR-006 |

---
