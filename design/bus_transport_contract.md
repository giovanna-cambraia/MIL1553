# Bus Transport Contract
**Project:** MIL1553
**Related:** HLR-1553-BUS-001, LLR-1553-BUS-001, LLR-1553-BUS-002,
LLR-1553-BUS-003, SDD §2.3, §3

---

## 1. Purpose

This document is the single source of truth for the wire-level protocol
that BC, RT1, RT2 and BM use to exchange words through `bus_model`,
regardless of whether the language implementation is C or Ada, and
regardless of whether `bus_model` is linked in-process (same-language HSI)
or run as a peer process over a local transport (cross-language HSI, per
SDD §2.3). Because the two language implementations share no source code
(SDP §2), this contract — not a shared header — is what guarantees they
agree on wire format.

## 2. Topology Modes

| Mode | When used | Transport |
|---|---|---|
| **In-process (library)** | Same-language HSI (SDP §2, increment I7 same-language runs); default for unit/HSI test speed | Function calls into a linked `bus_model` instance; the 24-byte record layout in §3 is still the internal representation, just not serialized over a socket |
| **Peer process (IPC)** | Cross-language HSI (BC in one language, RT/BM in the other) | Local Unix domain socket / named pipe (platform-appropriate), one connection per terminal, records serialized exactly per §3 |

A terminal's code does not know which mode is active — both are reached
through the same `bus_model` public interface (SDD §4.5); the mode is a
build/config-time selection.

## 3. Wire Record Format

Every unit of communication is a fixed 24-byte record, network byte order
(big-endian) for all multi-byte integer fields:

```
Offset  Size  Field              Notes
0       1     protocol_version   Constant value 1 for this project
1       1     msg_type           0=WORD, 1=CHANNEL_STATUS, 2=CONTROL
2       1     channel            0=A, 1=B (ignored for CONTROL)
3       5     reserved           Must be all zero; non-zero is a
                                  transport error (see 5.2)
8       8     timestamp_us       uint64, from the virtual clock
                                  (LLR-1553-BUS-003) of the sending side
16      5     symbols            40 half-bit Manchester symbols,
                                  packed 8 symbols per byte, symbol 0
                                  in the most significant bit of byte 0
21      1     terminal_id        0=BC, 1=RT1, 2=RT2, 3=BM
22      2     reserved           Must be zero
----------------------------------------------------------------------
24 bytes total
```

### 3.1 msg_type = WORD

The `symbols` field carries one Manchester-encoded word (command, status
or data), per the symbol layout of LLR-1553-WORD-006. This is the only
message type that BC/RT/BM protocol logic (`bc_scheduler`, `rt_service`,
`bm_monitor`) consumes directly.

### 3.2 msg_type = CHANNEL_STATUS

Carries a channel up/down transition. `symbols` bytes 0 only is used:
`0x00` = channel up, `0x01` = channel down; remaining symbol bytes are
zero. Used by `error_inject`'s BUS_A_FAIL/BUS_B_FAIL primitives
(LLR-1553-ERR-008) to flip a channel's state through the transport layer
in the peer-process topology; in the in-process topology the equivalent
effect is a direct call to `bus_set_channel_failed` (SDD §4.5) and no
CHANNEL_STATUS record is actually serialized.

### 3.3 msg_type = CONTROL

Carries errinject arm/disarm requests (LLR-1553-ERR-006) when the test
harness and the executable under test are separate processes in the
peer-process topology. `symbols` byte 0 encodes the request (`0x01`=arm,
`0x02`=disarm), bytes 1-8 carry the harness token (truncated/padded to
fit; full token comparison happens above the transport layer). In the
in-process/library topology, arm/disarm are ordinary function calls and
CONTROL records are unused.

### 3.4 Validation and Rejection

A receiving end shall:
1. Reject (and count as a transport error, not a 1553 protocol error) any
   record with `protocol_version != 1`.
2. Reject any record with a non-zero `reserved` field.
3. Reject any record with `msg_type` outside {0, 1, 2}.
4. Reject any record with `channel` outside {0, 1} when `msg_type = WORD`
   or `CHANNEL_STATUS`.

Transport-level rejection happens strictly before any 1553 protocol
validation (LLR-1553-WORD-008 classification) — a transport error is a
different failure class from a parity/sync/Manchester error, and is
counted separately (LLR-1553-BUS-002).

## 4. Symbol Packing Detail

The 40 half-bit symbols of one word (LLR-1553-WORD-006) are packed into 5
bytes, most significant symbol first:

```
byte 0: symbols[0..7]   (symbol 0 = MSB of byte 0)
byte 1: symbols[8..15]
byte 2: symbols[16..23]
byte 3: symbols[24..31]
byte 4: symbols[32..39]
```

Each symbol is one bit (0 or 1); this is a direct bit-packing, not a
Manchester re-encoding of the symbols themselves — the transport layer
moves already-Manchester-encoded data, it does not interpret it (that's
`manchester.decode_word`'s job, downstream of the transport, per SDD §7
dataflow).

## 5. Virtual Time Discipline

Per LLR-1553-BUS-003, `timestamp_us` in every record is read from the
sending terminal's virtual clock, which in turn is driven only by word
durations and explicit waits — never the host wall clock. In the
peer-process topology, this means the receiving side must **not**
timestamp records on arrival; it must trust and use the sender's
`timestamp_us` field, so that a run's timing results are reproducible
independent of IPC scheduling jitter on the host OS. This is the one
place the peer-process topology could silently reintroduce
non-determinism if implemented carelessly, so it is called out explicitly
here rather than left to be discovered during cross-language HSI
debugging.

## 6. Versioning

`protocol_version` exists so that a future change to this contract (e.g.,
widening the symbol field, or adding a message type) can be introduced
without silently breaking a mismatched pair of executables — a version
mismatch is a hard, immediate, loggable rejection (§3.4 item 1), not a
misinterpreted record. There is no negotiation; both sides of any HSI run
must be built against the same contract version, recorded in the SCMP
toolchain/version record (`docs/SCMP.md` §7) alongside compiler versions.

## 7. Relationship to bus_model's Public Interface

This contract describes what crosses the boundary; it does not describe
`bus_model`'s internal queueing, the virtual clock's internal
representation, or the channel failed-flag storage — those are
implementation details of `bus_model` (SDD §4.5) and may differ between
the C and Ada implementations as long as both correctly produce and
consume records per this document.

---
