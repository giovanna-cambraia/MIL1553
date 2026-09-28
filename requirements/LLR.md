# Low-Level Requirements (LLR)
**Project:** MIL1553
**Document ID:** MIL1553-LLR-001
**Revision:** A (Draft)
**Date:** 2026-09-28
**Related:** HLR (`requirements/HLR.md`), SDP (`docs/SDP.md`) 3.1, `docs/mil1553_notes.md`

---

## 1. Conventions

**ID scheme:** `LLR-1553-<AREA>-<NNN>`, same areas as the HLR (`WORD`, `BC`,
`RT`, `BM`, `BUS`, `REDUN`, `BIT`, `ERR`).

**Fields per requirement:** Statement, Rationale, Parent (HLR), Allocation
(module in `include/` and `src/`, identical names in C and Ada), Verification
(Test / Analysis / Inspection / Demonstration), DAL.

**Source:** for every LLR the source is the decomposition of its parent HLR
(and, through it, the MIL-STD-1553B paragraph cited there) unless a line says
otherwise.

**Applicability:** every LLR applies to both the C and the Ada/SPARK
implementation. Code shall carry the LLR ID at the implementing function
(`/* Implements: LLR-1553-WORD-003 */` in C, `-- Implements: LLR-1553-WORD-003`
in Ada).

**Project constants** (single definition in the types module of each language,
values identical in both):

| Constant | Value |
|---|---|
| BIT_TIME_US | 1 |
| WORD_TIME_US | 20 |
| T_RESP_MIN_US / T_RESP_MAX_US | 4 / 12 |
| T_GAP_MIN_US | 4 |
| T_NORESP_US | 14 |
| MAX_DATA_WORDS | 32 |
| RT_ADDR_BROADCAST | 31 |

**Field layout of the 16-bit information field** (bit 15 = most significant):

```
Command : [15..11 RT ADDR][10 T/R][9..5 SUBADDR/MODE][4..0 WORD COUNT/MODE CODE]
Status  : [15..11 RT ADDR][10 ME][9 INSTR][8 SR][7..5 RESERVED][4 BCR][3 BUSY][2 SF][1 DBCA][0 TF]
Data    : [15..0 DATA]
```

---

## 2. Word Formats and Encoding (WORD)

### LLR-1553-WORD-001 — Command word pack and unpack
- **Statement:** `mil1553_word` shall pack the fields RT address (0-31), T/R bit, subaddress/mode (0-31) and word count/mode code (0-31) into the command word information field per the layout in Section 1, and shall unpack an information field into those fields. Pack shall reject any field outside its range without producing a word.
- **Rationale:** One bit layout, defined once, keeps C and Ada byte-identical on the bus.
- **Parent:** HLR-1553-WORD-001
- **Allocation:** mil1553_word
- **Verification:** Test
- **DAL:** B

### LLR-1553-WORD-002 — Word count encoding
- **Statement:** `mil1553_word` shall map a data word count of 1-32 to the word count field values 1-31 and 0 respectively (32 encodes as 0), shall map the field back on unpack, and shall reject an encode request for a count of 0 or greater than 32.
- **Rationale:** The 32-as-zero rule is the classic off-by-one defect in 1553 code.
- **Parent:** HLR-1553-WORD-001
- **Allocation:** mil1553_word
- **Verification:** Test (boundary values 0, 1, 31, 32, 33)
- **DAL:** B

### LLR-1553-WORD-003 — Status word pack and unpack
- **Statement:** `mil1553_word` shall pack RT address and the bits ME, INSTR, SR, BCR, BUSY, SF, DBCA and TF into the status information field per Section 1 with the reserved bits (7..5) set to zero, and shall unpack a status field into those items plus a flag indicating that a reserved bit was non-zero.
- **Rationale:** Reserved bits must be transmitted as zero; receiving a non-zero value is a detectable anomaly rather than something to silently mask away.
- **Parent:** HLR-1553-WORD-002
- **Allocation:** mil1553_word
- **Verification:** Test
- **DAL:** B

### LLR-1553-WORD-004 — Data word pack and unpack
- **Statement:** `mil1553_word` shall pack a 16-bit value into a data word with data sync type and shall unpack a data word to its 16-bit value, without altering the value.
- **Rationale:** Data words carry no structure at word level; the requirement is bit-exact transparency.
- **Parent:** HLR-1553-WORD-003
- **Allocation:** mil1553_word
- **Verification:** Test (0x0000, 0xFFFF, walking-one patterns)
- **DAL:** B

### LLR-1553-WORD-005 — Odd parity computation
- **Statement:** `crc_parity` shall compute the parity bit for a 16-bit information field such that the total number of ones in the field plus parity bit is odd, and shall provide a check function returning valid or invalid for a field and received parity bit.
- **Rationale:** Single source of parity for encoder, decoder, BIT and monitor.
- **Parent:** HLR-1553-WORD-005
- **Allocation:** crc_parity
- **Verification:** Test (exhaustive over all 65536 field values)
- **DAL:** B

### LLR-1553-WORD-006 — Manchester II encoding of a word
- **Statement:** `manchester` shall encode a word (sync type, 16-bit field, parity bit) into 40 half-bit symbols: the command/status sync as 1,1,1,0,0,0, the data sync as 0,0,0,1,1,1, and each of the 17 field and parity bits as the pair 1,0 for a logic one and 0,1 for a logic zero, most significant field bit first.
- **Rationale:** Fixes the exact on-bus symbol sequence, which is what the error injector corrupts and the BM records.
- **Parent:** HLR-1553-WORD-004
- **Allocation:** manchester
- **Verification:** Test (known-answer vectors)
- **DAL:** B

### LLR-1553-WORD-007 — Manchester II decoding of a word
- **Statement:** `manchester` shall decode 40 half-bit symbols into sync type, 16-bit field and parity bit. It shall classify the sync as command/status, data or invalid by exact pattern match, and shall report a Manchester violation when any data or parity bit cell is 0,0 or 1,1.
- **Rationale:** Provides the detection point for HLR-1553-WORD-004 and ERR-001.
- **Parent:** HLR-1553-WORD-004
- **Allocation:** manchester
- **Verification:** Test (valid words, every single-symbol corruption of a valid word)
- **DAL:** B

### LLR-1553-WORD-008 — Word validity classification
- **Statement:** `mil1553_word` shall classify every received word as exactly one of VALID, SYNC_ERROR, MANCHESTER_ERROR or PARITY_ERROR, evaluated in that precedence order, and shall provide the decoded fields only when the result is VALID.
- **Rationale:** A single, ordered classification prevents different terminals reporting the same corruption differently; parity cannot be evaluated once a Manchester violation exists, which fixes the order.
- **Parent:** HLR-1553-WORD-005
- **Allocation:** mil1553_word
- **Verification:** Test (combined faults exercise the precedence)
- **DAL:** B

---

## 3. Bus Controller (BC)

### LLR-1553-BC-001 — Schedule table
- **Statement:** `bc_scheduler` shall hold the schedule in a fixed-capacity table of at most MAX_SCHEDULE_ENTRIES entries, each giving message type, RT address(es), subaddress, word count, mode code (if any), minor frame index, slot start offset (us) and preferred bus. At initialization the table shall be validated (ranges per WORD-001/002, offsets inside the minor frame, no overlapping slots) and an invalid table shall be rejected with the scheduler remaining in IDLE.
- **Rationale:** Static, validated schedule; no dynamic allocation.
- **Parent:** HLR-1553-BC-001
- **Allocation:** bc_scheduler
- **Verification:** Test
- **DAL:** B

### LLR-1553-BC-002 — Frame timing and overrun detection
- **Statement:** `bc_scheduler` shall start each message at its slot start offset within the current minor frame, advance to the next minor frame after the configured minor frame period, wrap to minor frame 0 after the last minor frame of the major frame, and raise a MINOR_FRAME_OVERRUN error when the last message of a minor frame has not completed by the end of the minor frame period.
- **Rationale:** Deterministic cycle plus explicit overrun detection.
- **Parent:** HLR-1553-BC-001
- **Allocation:** bc_scheduler
- **Verification:** Test, Analysis (schedule loading vs. `design/timing_budget.md`)
- **DAL:** B

### LLR-1553-BC-003 — BC-to-RT transfer sequence
- **Statement:** For a BC-to-RT message `bc_scheduler` shall transmit the receive command word, then N data words back to back (N = word count, 1-32), then wait for one status word from the addressed RT, and shall complete the message when a valid status word is received.
- **Rationale:** Defines the exact word order for the receive format.
- **Parent:** HLR-1553-BC-002
- **Allocation:** bc_scheduler
- **Verification:** Test
- **DAL:** B

### LLR-1553-BC-004 — RT-to-BC transfer sequence
- **Statement:** For an RT-to-BC message `bc_scheduler` shall transmit the transmit command word, then wait for a status word followed by N data words from the addressed RT, and shall complete the message only if the status word is valid and exactly N valid data words were received.
- **Rationale:** Word count check on the receive side is what catches a short or long response.
- **Parent:** HLR-1553-BC-002
- **Allocation:** bc_scheduler
- **Verification:** Test
- **DAL:** B

### LLR-1553-BC-005 — RT-to-RT transfer sequence
- **Statement:** For an RT-to-RT message `bc_scheduler` shall transmit the receive command word (receiving RT, T/R = 0) immediately followed by the transmit command word (transmitting RT, T/R = 1), then expect the transmitting RT's status word, N data words, and the receiving RT's status word. It shall reject a schedule entry whose two word counts differ and shall complete the message only if both status words are valid and both RT addresses match the commands.
- **Rationale:** Two commands, two statuses and one data block must all be accounted for.
- **Parent:** HLR-1553-BC-003
- **Allocation:** bc_scheduler
- **Verification:** Test
- **DAL:** B

### LLR-1553-BC-006 — Mode command issuance
- **Statement:** `bc_scheduler` shall issue a mode command with the mode indicator subaddress (00000 or 11111 as configured) and a mode code contained in `design/mode_code_table.md`. For a mode code defined with a data word it shall transmit (transmit-type codes: receive from RT; receive-type codes: send) exactly one data word in the position defined by the table. It shall reject a schedule entry with an unlisted mode code.
- **Rationale:** Ties BC behavior to the same table the RT uses.
- **Parent:** HLR-1553-BC-004
- **Allocation:** bc_scheduler
- **Verification:** Test
- **DAL:** B

### LLR-1553-BC-007 — Broadcast issuance
- **Statement:** For a broadcast message (RT address 31) `bc_scheduler` shall transmit the command word and any data words, shall not wait for a status word, and shall treat the message as complete after the last word plus the inter-message gap.
- **Rationale:** Waiting for a status after a broadcast would produce a false no-response fault on every broadcast.
- **Parent:** HLR-1553-BC-004
- **Allocation:** bc_scheduler
- **Verification:** Test
- **DAL:** B

### LLR-1553-BC-008 — Inter-message gap
- **Statement:** `bc_scheduler` shall start the first command word of a message no earlier than T_GAP_MIN_US after the end of the last word of the previous message, or after the no-response timeout expiry if the previous message ended in a timeout, whichever is later.
- **Rationale:** Prevents the BC from violating its own timing rule after a timeout.
- **Parent:** HLR-1553-BC-005
- **Allocation:** bc_scheduler
- **Verification:** Test (measured on the bus model timestamps)
- **DAL:** B

### LLR-1553-BC-009 — No-response timeout
- **Statement:** `bc_scheduler` shall start a timer at the end of the last command word of a message that expects a status word, and shall declare NO_RESPONSE when the start of a status word has not been observed within T_NORESP_US, and shall not declare it earlier.
- **Rationale:** Both directions matter: late declaration wastes bandwidth, early declaration falsely fails RTs answering at 12 us.
- **Parent:** HLR-1553-BC-005
- **Allocation:** bc_scheduler
- **Verification:** Test (status at 4, 12, 13, 14, 15 us)
- **DAL:** B

### LLR-1553-BC-010 — BC state machine
- **Statement:** `bc_scheduler` shall implement the states IDLE, MAJOR_FRAME, MINOR_FRAME, WAIT_RESPONSE, TIMEOUT, FAILOVER and DEGRADED with only the following transitions: IDLE to MAJOR_FRAME on start with a valid table; MAJOR_FRAME to MINOR_FRAME at each minor frame start; MINOR_FRAME to WAIT_RESPONSE after a command expecting a response; MINOR_FRAME to MAJOR_FRAME after the last minor frame; WAIT_RESPONSE to MINOR_FRAME on message completion; WAIT_RESPONSE to TIMEOUT on NO_RESPONSE or invalid status; TIMEOUT to FAILOVER when the alternate bus is available and no retry has yet been made for the message; FAILOVER to WAIT_RESPONSE on retry; TIMEOUT or WAIT_RESPONSE (after a failed retry) to MINOR_FRAME with the RT marked failed; any state to DEGRADED on primary bus switch; DEGRADED to MAJOR_FRAME to continue on the remaining capability; any state to IDLE on stop. Any other event in a state shall be ignored and counted.
- **Rationale:** Explicit transition list is what the state machine viewer overlays test coverage on.
- **Parent:** HLR-1553-BC-001, HLR-1553-REDUN-001
- **Allocation:** bc_scheduler
- **Verification:** Test (every transition), Analysis
- **DAL:** B

---

## 4. Remote Terminal (RT)

### LLR-1553-RT-001 — Address filtering
- **Statement:** `rt_service` shall process a received command word only if its RT address equals the terminal's configured address (0-30) or equals RT_ADDR_BROADCAST, and shall silently ignore all other command words.
- **Rationale:** An RT that answers another RT's command creates bus contention.
- **Parent:** HLR-1553-RT-001
- **Allocation:** rt_service
- **Verification:** Test (all 32 addresses)
- **DAL:** B

### LLR-1553-RT-002 — Command validation
- **Statement:** `rt_service` shall validate a command word against the configured subaddress table (direction allowed for the T/R bit, word count within the configured minimum and maximum, subaddress or mode enabled) before any execution, and shall report the result as VALID, ILLEGAL_SUBADDRESS, ILLEGAL_DIRECTION or ILLEGAL_WORD_COUNT.
- **Rationale:** Separates the three illegal-command causes so each is testable.
- **Parent:** HLR-1553-RT-001
- **Allocation:** rt_service
- **Verification:** Test (normal, boundary and robustness cases)
- **DAL:** B

### LLR-1553-RT-003 — Receive data handling
- **Statement:** For a valid receive command `rt_service` shall accept exactly N data words into the subaddress buffer, shall commit them to the buffer only if all N words are valid, and shall treat fewer or more than N words before the next command word as a word count error.
- **Rationale:** Prevents a partially received block from reaching the subsystem.
- **Parent:** HLR-1553-RT-001
- **Allocation:** rt_service
- **Verification:** Test
- **DAL:** B

### LLR-1553-RT-004 — Transmit data handling
- **Statement:** For a valid transmit command `rt_service` shall transmit its status word followed by exactly N data words taken from the subaddress buffer, with no gap between words.
- **Rationale:** Word order and count for the transmit format.
- **Parent:** HLR-1553-RT-001
- **Allocation:** rt_service
- **Verification:** Test
- **DAL:** B

### LLR-1553-RT-005 — Response timing state machine
- **Statement:** `rt_service` shall start its status word at a delay from the end of the received command word (or, for a receive-type message, from the end of the last data word) equal to the configured response delay, using the states WAIT_END_OF_WORD, DELAY_RUNNING and TRANSMIT_STATUS. The configured delay shall default to 8 us, and a configuration value outside 4 us to 12 us inclusive shall be rejected at initialization.
- **Rationale:** Guarantees the 4-12 us window by construction and by configuration check.
- **Parent:** HLR-1553-RT-002
- **Allocation:** rt_service
- **Verification:** Test (delay 3, 4, 8, 12, 13 us), Analysis
- **DAL:** B

### LLR-1553-RT-006 — Status word construction and latching
- **Statement:** `rt_service` shall build every transmitted status word with its own RT address; shall set ME when a message is rejected and clear it on the next valid command word; shall set BUSY only while the subsystem reports busy; shall set SF and TF from the subsystem indication and the BIT result respectively; and shall set BCR after a valid broadcast and clear it on the next non-broadcast valid command word.
- **Rationale:** Each bit gets an explicit set and clear condition so that HLR-1553-RT-003 is testable bit by bit.
- **Parent:** HLR-1553-RT-003
- **Allocation:** rt_service
- **Verification:** Test
- **DAL:** B

### LLR-1553-RT-007 — RT-to-RT roles
- **Statement:** As transmitting RT `rt_service` shall answer the transmit command that follows a receive command addressed to another RT with its status word and N data words. As receiving RT it shall, after the receive command, wait for the transmitting RT's status word and N data words, and then send its own status word only if all N data words are valid and the word count matches; otherwise it shall set ME and send no status word.
- **Rationale:** Each RT sees only part of the message.
- **Parent:** HLR-1553-RT-004
- **Allocation:** rt_service
- **Verification:** Test
- **DAL:** B

### LLR-1553-RT-008 — Mode code dispatch
- **Statement:** `rt_service` shall dispatch a mode command through a constant table containing each supported mode code, its data word presence and direction, and its handler. It shall include at least the codes for transmit status word, initiate self-test, transmit BIT word, reset remote terminal and synchronize (with and without data word), and shall treat any other mode code as ILLEGAL.
- **Rationale:** Table-driven dispatch keeps the supported subset and the BC table in one place.
- **Parent:** HLR-1553-RT-005
- **Allocation:** rt_service
- **Verification:** Test
- **DAL:** B

### LLR-1553-RT-009 — Broadcast handling
- **Statement:** For a valid broadcast command `rt_service` shall perform the same validation and execution as for an individually addressed command, shall not transmit any word, shall set BCR per RT-006, and shall use a code path separate from the fault-driven response suppression.
- **Rationale:** "Correctly silent" and "faulty silent" must be distinguishable in code and in tests.
- **Parent:** HLR-1553-RT-006
- **Allocation:** rt_service
- **Verification:** Test, Inspection (separate paths)
- **DAL:** B

### LLR-1553-RT-010 — Transmission error handling
- **Statement:** When any word of a message is classified other than VALID, or a word count error occurs, `rt_service` shall discard the message, set ME, transmit nothing, and record the error per ERR-004.
- **Rationale:** A message with a transmission error must not be executed nor answered.
- **Parent:** HLR-1553-RT-007
- **Allocation:** rt_service
- **Verification:** Test
- **DAL:** B

### LLR-1553-RT-011 — Illegal command handling
- **Statement:** When a command word is VALID at word level but fails validation per RT-002 or names an unsupported mode code, `rt_service` shall not execute it, shall set ME, and shall transmit a status word (no data words) after the configured response delay.
- **Rationale:** The BC needs an answer to distinguish an illegal command from a bus fault.
- **Parent:** HLR-1553-RT-007
- **Allocation:** rt_service
- **Verification:** Test
- **DAL:** B

### LLR-1553-RT-012 — Busy handling
- **Statement:** When the subsystem reports busy, `rt_service` shall not execute the command, shall transmit a status word with BUSY set and no data words, and for a transmit command shall not transmit any data word.
- **Rationale:** Busy means "cannot serve" and must not be answered with stale data.
- **Parent:** HLR-1553-RT-007
- **Allocation:** rt_service
- **Verification:** Test
- **DAL:** B

### LLR-1553-RT-013 — RT state machine
- **Statement:** `rt_service` shall implement the states RESET, IDLE, WAIT_COMMAND, VALIDATE, EXECUTE, RESPOND, ERROR, BUSY and FAIL_SAFE with only the following transitions: RESET to IDLE on PBIT pass; RESET to FAIL_SAFE on PBIT fail; IDLE to WAIT_COMMAND on enable; WAIT_COMMAND to VALIDATE on a command word addressed per RT-001; VALIDATE to EXECUTE on a valid command; VALIDATE to ERROR on transmission or illegal command; VALIDATE to BUSY when the subsystem is busy; EXECUTE to RESPOND; RESPOND to WAIT_COMMAND after the last word; ERROR to WAIT_COMMAND after any status word is sent or immediately if none; BUSY to RESPOND; any state to FAIL_SAFE on a fatal BIT result; and any state to RESET on a reset mode command. In FAIL_SAFE the RT shall execute no command and shall answer a command addressed to it with a status word with TF set and no data words.
- **Rationale:** Explicit transitions for coverage overlay; FAIL_SAFE keeps the RT visible to the BC rather than silent.
- **Parent:** HLR-1553-RT-001, HLR-1553-RT-007, HLR-1553-BIT-004
- **Allocation:** rt_service
- **Verification:** Test (every transition), Analysis
- **DAL:** B

---

## 5. Bus Monitor (BM)

### LLR-1553-BM-001 — Word capture
- **Statement:** `bm_monitor` shall capture every word delivered by the bus model on channels A and B into a fixed-capacity record buffer with fields bus, start time (us), 40 half-bit symbols and the classification from WORD-008, and shall count records dropped when the buffer is full.
- **Rationale:** The record buffer is the source for the timeline and response-time views.
- **Parent:** HLR-1553-BM-001
- **Allocation:** bm_monitor
- **Verification:** Test
- **DAL:** C

### LLR-1553-BM-002 — Message reconstruction
- **Statement:** `bm_monitor` shall group captured words into messages by command word and inter-message gap, classify each message as BC-to-RT, RT-to-BC, RT-to-RT, mode command or broadcast, and compute the response time from the end of the command word to the start of the first status word.
- **Rationale:** Response time and message type are required outputs for the visualizations.
- **Parent:** HLR-1553-BM-001
- **Allocation:** bm_monitor
- **Verification:** Test
- **DAL:** C

### LLR-1553-BM-003 — No transmit path
- **Statement:** `bm_monitor` shall access the bus model only through the receive interface; no transmit function of the bus model shall be reachable from `bm_monitor` (no call, no reference).
- **Rationale:** Proves by construction that the monitor cannot influence traffic.
- **Parent:** HLR-1553-BM-001
- **Allocation:** bm_monitor, bus_model (interface)
- **Verification:** Inspection (interface and dependency check), Test (bus content identical with and without BM)
- **DAL:** C

---

## 6. Software Bus (BUS)

### LLR-1553-BUS-001 — Channel model
- **Statement:** `bus_model` shall provide two independent channels A and B; a word transmitted on a channel shall be delivered to every other connected terminal and monitor on that channel with its start time and 40 symbols unchanged, and shall not appear on the other channel. A channel marked failed shall deliver nothing.
- **Rationale:** Channel independence is what makes A/B failover testable.
- **Parent:** HLR-1553-BUS-001
- **Allocation:** bus_model
- **Verification:** Test
- **DAL:** B

### LLR-1553-BUS-002 — Transport contract
- **Statement:** The bus model shall exchange words with terminals as messages defined in `design/bus_transport_contract.md` (message type, channel, start time in us, 40 symbols, terminal identity), with a fixed byte layout and a protocol version field, so that a terminal implemented in either language can attach to a bus model implemented in either language, and shall reject a message with an unknown version or a malformed layout.
- **Rationale:** The contract, not shared code, is what ties the two implementations together.
- **Parent:** HLR-1553-BUS-001
- **Allocation:** bus_model
- **Verification:** Test (cross-language HSI), Inspection (contract document vs. code)
- **DAL:** B

### LLR-1553-BUS-003 — Virtual time base
- **Statement:** The bus model shall keep a deterministic virtual clock in microseconds, advanced only by transmitted word durations (WORD_TIME_US) and explicit waits, shall provide all timestamps used by BC, RT, BM and the error log from that clock, and shall not read the host clock for any protocol timing.
- **Rationale:** Timing results must be reproducible on any workstation.
- **Parent:** HLR-1553-BUS-001
- **Allocation:** bus_model
- **Verification:** Test (repeat runs give identical timestamps), Inspection
- **DAL:** B

---

## 7. Redundancy (REDUN)

### LLR-1553-REDUN-001 — Retry on alternate bus
- **Statement:** When a message on the current bus ends in NO_RESPONSE or a word-level error, `bc_scheduler` shall retransmit the same message exactly once on the alternate bus, after the inter-message gap, and shall use the result of the retry as the result of the message.
- **Rationale:** One deterministic retry, per `mil1553_notes.md` 8.
- **Parent:** HLR-1553-REDUN-001
- **Allocation:** bc_scheduler
- **Verification:** Test
- **DAL:** B

### LLR-1553-REDUN-002 — Primary bus switch
- **Statement:** `bc_scheduler` shall count consecutive failed messages per bus and, when the count on the primary bus reaches the configured threshold (default 3), shall designate the other bus as primary, reset the counters, record the failover event, and enter DEGRADED.
- **Rationale:** Distinguishes an RT problem from a bus problem.
- **Parent:** HLR-1553-REDUN-001
- **Allocation:** bc_scheduler
- **Verification:** Test (threshold - 1, threshold, threshold + 1)
- **DAL:** B

### LLR-1553-REDUN-003 — RT failed marking and degraded reporting
- **Statement:** When a retry also fails, `bc_scheduler` shall mark the RT as failed for the current major frame, skip its remaining messages in that major frame, clear the mark at the next major frame start, and expose failed-RT flags and the DEGRADED indication through the health monitor API.
- **Rationale:** Avoids wasting the schedule on a dead RT while still retrying it later.
- **Parent:** HLR-1553-REDUN-001
- **Allocation:** bc_scheduler
- **Verification:** Test
- **DAL:** B

---

## 8. Built-In Test (BIT)

### LLR-1553-BIT-001 — PBIT RAM test
- **Statement:** `bit_pbit` shall run a walking-ones and walking-zeros pattern test over each RAM region under test, restoring the contents afterwards, and shall report the address of the first mismatch.
- **Rationale:** Detects stuck bits in the buffers used for data words.
- **Parent:** HLR-1553-BIT-001
- **Allocation:** bit_pbit
- **Verification:** Test (with a simulated stuck bit, test build only)
- **DAL:** B

### LLR-1553-BIT-002 — PBIT ROM checksum
- **Statement:** `bit_pbit` shall compute a checksum over the configured code/constant region and compare it with the stored reference value, reporting failure on mismatch.
- **Rationale:** Detects corrupted constants such as the mode code table.
- **Parent:** HLR-1553-BIT-001
- **Allocation:** bit_pbit
- **Verification:** Test
- **DAL:** B

### LLR-1553-BIT-003 — PBIT codec loopback and timer sanity
- **Statement:** `bit_pbit` shall encode a set of known words, decode the symbols and compare, for command, status and data words, and shall verify that the timer advances by the expected amount over a known interval within a tolerance of 1 us.
- **Rationale:** Exercises the encode and decode paths and the time source the protocol timing depends on.
- **Parent:** HLR-1553-BIT-001
- **Allocation:** bit_pbit
- **Verification:** Test
- **DAL:** B

### LLR-1553-BIT-004 — PBIT bus idle check and gating
- **Statement:** `bit_pbit` shall verify that no word activity is present on channels A and B for a defined listen period, shall combine all PBIT results, and shall prevent entry into OPERATIONAL when any PBIT test has failed.
- **Rationale:** Implements the blocking behavior in HLR-1553-BIT-001.
- **Parent:** HLR-1553-BIT-001
- **Allocation:** bit_pbit
- **Verification:** Test
- **DAL:** B

### LLR-1553-BIT-005 — CBIT word and protocol checks
- **Statement:** `bit_cbit` shall, for each received word and message, evaluate the classification from WORD-008, the word count consistency and the observed response time against the 4-12 us window, and shall increment the matching counter for each violation.
- **Rationale:** Continuous version of the same checks, using the same functions as operation.
- **Parent:** HLR-1553-BIT-002
- **Allocation:** bit_cbit
- **Verification:** Test
- **DAL:** B

### LLR-1553-BIT-006 — CBIT health counters and watchdog
- **Statement:** `bit_cbit` shall keep separate good-message and error counters for channel A and channel B, shall declare a channel unhealthy when its error ratio over a configured window exceeds a configured threshold, and shall run a watchdog that reports a fault if the BC frame loop or RT service loop has not signaled progress within its configured period.
- **Rationale:** Provides the bus health data for redundancy decisions and the scheduler hang detection.
- **Parent:** HLR-1553-BIT-002
- **Allocation:** bit_cbit
- **Verification:** Test
- **DAL:** B

### LLR-1553-BIT-007 — IBIT sequence
- **Statement:** `bit_ibit` shall, on request, transmit a known pattern of words on each channel and verify the echo received on that channel, compare the two channels' results, report any difference, and restore the terminal to its previous state, with no effect on messages addressed to other terminals.
- **Rationale:** The deeper test required by HLR-1553-BIT-003.
- **Parent:** HLR-1553-BIT-003
- **Allocation:** bit_ibit
- **Verification:** Test
- **DAL:** B

### LLR-1553-BIT-008 — BIT result structure and reporting
- **Statement:** The BIT modules shall maintain a result structure recording, per level (PBIT, CBIT, IBIT), each test identifier, pass/fail, the failing item and a timestamp; shall set TF in the RT status word on a fatal result and SF on a non-fatal one; shall return a BIT word built from the structure for the transmit BIT word mode command; and shall provide read-only access through the health monitor API.
- **Rationale:** One result, three views (bus, BIT word, local API).
- **Parent:** HLR-1553-BIT-004
- **Allocation:** bit_pbit, bit_cbit, bit_ibit (shared result structure in the BIT interface)
- **Verification:** Test
- **DAL:** B

### LLR-1553-BIT-009 — BIT state machine
- **Statement:** The BIT manager shall implement the states POWER_UP, PBIT, OPERATIONAL, CBIT_RUNNING, IBIT_RUNNING, DEGRADED and FAIL with only the following transitions: POWER_UP to PBIT; PBIT to OPERATIONAL on pass; PBIT to FAIL on fail; OPERATIONAL to CBIT_RUNNING and back on each CBIT cycle; OPERATIONAL to IBIT_RUNNING on an IBIT request and back on completion; CBIT_RUNNING or IBIT_RUNNING to DEGRADED on a non-fatal result; any state except POWER_UP to FAIL on a fatal result; and DEGRADED to OPERATIONAL when a subsequent complete CBIT cycle passes.
- **Rationale:** Explicit transitions for the state machine viewer.
- **Parent:** HLR-1553-BIT-001, HLR-1553-BIT-004
- **Allocation:** bit_pbit, bit_cbit, bit_ibit
- **Verification:** Test (every transition), Analysis
- **DAL:** B

---

## 9. Error Handling and Error Injection (ERR)

### LLR-1553-ERR-001 — Word-level error reaction
- **Statement:** On any word classified other than VALID, a terminal shall reject the enclosing message, increment the counter for the error type (parity, sync, Manchester) and channel, and create an error record per ERR-004.
- **Rationale:** Common reaction, shared by BC, RT and BM.
- **Parent:** HLR-1553-ERR-001
- **Allocation:** mil1553_word, rt_service, bc_scheduler, bm_monitor
- **Verification:** Test
- **DAL:** B

### LLR-1553-ERR-002 — Status word validation by the BC
- **Statement:** `bc_scheduler` shall check every received status word for RT address equal to the commanded address, and for the bits ME, BUSY, SF and TF, and shall report STATUS_ADDRESS_MISMATCH, STATUS_ME, STATUS_BUSY, STATUS_SF or STATUS_TF as applicable, each as an error record and each with its scheduled reaction (retry per REDUN-001 for ME and address mismatch; report only for BUSY, SF, TF).
- **Rationale:** BC-side decision table for status bits.
- **Parent:** HLR-1553-ERR-002
- **Allocation:** bc_scheduler
- **Verification:** Test
- **DAL:** B

### LLR-1553-ERR-003 — Word count mismatch
- **Statement:** `bc_scheduler` shall detect a mismatch between the word count in the command word and the number of valid data words received (fewer or more), shall report WORD_COUNT_MISMATCH, and shall treat the message as failed.
- **Rationale:** Covers the wrong-word-count fault class end to end.
- **Parent:** HLR-1553-ERR-002
- **Allocation:** bc_scheduler
- **Verification:** Test
- **DAL:** B

### LLR-1553-ERR-004 — Error log
- **Statement:** Each terminal shall keep a fixed-capacity error log (default 64 records) of timestamp (us, from BUS-003), terminal identity, channel, error type and message identifier. When the log is full new records shall be discarded and an overflow counter incremented, so that the earliest errors are preserved. The log shall be readable, not modifiable, through the health monitor API.
- **Rationale:** The first fault is usually the one that matters; discard-newest keeps it.
- **Parent:** HLR-1553-ERR-003
- **Allocation:** common error log interface, used by bc_scheduler, rt_service, bm_monitor, bit_cbit
- **Verification:** Test (fill to capacity and one beyond)
- **DAL:** B

### LLR-1553-ERR-005 — Release build exclusion
- **Statement:** The release build definitions (`CMakeLists.txt` release configuration and `mil1553_release.gpr`) shall not list any file under `src/c/errinject/` or `src/ada/errinject/`, and no file in the release source set shall reference an `errinject` symbol. A build step shall fail if the release link map contains an `errinject` symbol.
- **Rationale:** The certification argument is that the injector is absent from the certified object code.
- **Parent:** HLR-1553-ERR-004
- **Allocation:** CMakeLists.txt, mil1553_release.gpr, `tools/build_and_verify.sh`
- **Verification:** Inspection (build files), Test (release build with an added errinject reference must fail)
- **DAL:** B

### LLR-1553-ERR-006 — Arming interface
- **Statement:** In the test build the error injector shall remain inert until the test harness calls the arming interface with the harness token, shall return to inert on a disarm call or process start, and shall reject an arm request without the token, logging the rejection.
- **Rationale:** Runtime gate (a) of HLR-1553-ERR-005.
- **Parent:** HLR-1553-ERR-005
- **Allocation:** errinject (error_inject)
- **Verification:** Test
- **DAL:** N/A (test-build tool; see finding F2 on the parent HLR)

### LLR-1553-ERR-007 — Scenario signature check
- **Statement:** Before arming, the injector shall compute the SHA-256 digest of `config/error_scenarios.yaml`, compare it with the digest in `config/error_scenarios.sig`, and stay inert and log the rejection if they differ or the signature file is absent.
- **Rationale:** Runtime gate (b) of HLR-1553-ERR-005; every executed scenario is one that was reviewed and signed.
- **Parent:** HLR-1553-ERR-005
- **Allocation:** errinject (config_signature)
- **Verification:** Test (unchanged, one byte changed, missing signature)
- **DAL:** N/A (test-build tool)

### LLR-1553-ERR-008 — Fault primitives
- **Statement:** The injector shall implement each fault class of HLR-1553-ERR-006 at the interposition point defined in the SDD: PARITY_BIT_FLIP (invert the parity symbol pair of the selected word), CORRUPT_SYNC (replace the sync symbols), SUPPRESS_RESPONSE (drop the response for a given duration), DELAY_RESPONSE (add a given delay), WRONG_WORD_COUNT (change the count of words in the response), STATUS_BIT_INJECT (set BUSY, ME, SF or TF in a transmitted status word, recomputing parity), ILLEGAL_COMMAND (substitute an unsupported command), BUS_A_FAIL and BUS_B_FAIL (mark the channel failed), with target terminal, channel, word index and count taken from the scenario.
- **Rationale:** Each primitive changes only what its name says, so an observed detection can be attributed to it.
- **Parent:** HLR-1553-ERR-006
- **Allocation:** errinject (error_inject)
- **Verification:** Test
- **DAL:** N/A (test-build tool)

### LLR-1553-ERR-009 — Scenario execution and result record
- **Statement:** The injector shall execute each scenario of the signed file in order, compare the observed detection and recovery (from the error log, BIT results and bus record) with the scenario's expected result, and emit one record per scenario with scenario_id, timestamp_us, terminal, bus, fault, expected, observed, pass_fail, HLR, LLR and test_id in machine-readable form. A scenario with no observed detection shall be recorded as a failure.
- **Rationale:** A missed detection is the most important result the campaign can produce, so it must never default to pass.
- **Parent:** HLR-1553-ERR-006
- **Allocation:** errinject (error_inject), `tools/run_error_campaign.sh`
- **Verification:** Test (including a deliberately undetectable fault expecting a failure record)
- **DAL:** N/A (test-build tool)

---

## 10. HLR to LLR Decomposition

This table seeds `traceability/hlr_to_llr.csv`.

| HLR | LLRs |
|---|---|
| WORD-001 | WORD-001, WORD-002 |
| WORD-002 | WORD-003 |
| WORD-003 | WORD-004 |
| WORD-004 | WORD-006, WORD-007 |
| WORD-005 | WORD-005, WORD-008 |
| BC-001 | BC-001, BC-002, BC-010 |
| BC-002 | BC-003, BC-004 |
| BC-003 | BC-005 |
| BC-004 | BC-006, BC-007 |
| BC-005 | BC-008, BC-009 |
| RT-001 | RT-001, RT-002, RT-003, RT-004, RT-013 |
| RT-002 | RT-005 |
| RT-003 | RT-006 |
| RT-004 | RT-007 |
| RT-005 | RT-008 |
| RT-006 | RT-009 |
| RT-007 | RT-010, RT-011, RT-012, RT-013 |
| BM-001 | BM-001, BM-002, BM-003 |
| BUS-001 | BUS-001, BUS-002, BUS-003 |
| REDUN-001 | REDUN-001, REDUN-002, REDUN-003, BC-010 |
| BIT-001 | BIT-001, BIT-002, BIT-003, BIT-004, BIT-009 |
| BIT-002 | BIT-005, BIT-006 |
| BIT-003 | BIT-007 |
| BIT-004 | BIT-008, BIT-009, RT-013 |
| ERR-001 | ERR-001 |
| ERR-002 | ERR-002, ERR-003 |
| ERR-003 | ERR-004 |
| ERR-004 | ERR-005 |
| ERR-005 | ERR-006, ERR-007 |
| ERR-006 | ERR-008, ERR-009 |

Totals: 57 LLRs (WORD 8, BC 10, RT 13, BM 3, BUS 3, REDUN 3, BIT 9, ERR 9),
covering all 30 HLRs.

---
