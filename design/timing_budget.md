# Timing Budget
**Project:** MIL1553
**Related:** HLR-1553-BC-001, HLR-1553-BC-005, HLR-1553-RT-002,
LLR-1553-BC-001, LLR-1553-BC-002, LLR-1553-BC-008, LLR-1553-BC-009,
LLR-1553-RT-005, LLR-1553-BUS-003

---

## 1. Purpose

Gives the microsecond budget per protocol phase, derives the worst-case
message and minor-frame durations used by `bc_scheduler`'s schedule
validation (LLR-1553-BC-001), and records the constants that both
language implementations must share bit-for-bit (SDD §1 constants table).

## 2. Base Constants

| Constant | Value | Note |
|---|---|---|
| BIT_TIME_US | 1 us | one Manchester half-bit cell |
| WORD_TIME_US | 20 us | 40 half-bit symbols x 0.5 us |
| T_RESP_MIN_US | 4 us | earliest legal response start |
| T_RESP_MAX_US | 12 us | latest legal response start |
| T_GAP_MIN_US | 4 us | minimum inter-message gap |
| T_NORESP_US | 14 us | no-response declaration threshold |
| MAX_DATA_WORDS | 32 | per message |

These values are fixed by MIL-STD-1553B for T_RESP_MIN_US, T_RESP_MAX_US
and T_GAP_MIN_US; WORD_TIME_US follows from the standard's 1 Mbit/s rate
and 20-bit-time word length; T_NORESP_US and MAX_DATA_WORDS are standard
values used directly (no project-specific adjustment).

## 3. Per-Phase Budget

| Phase | Formula | Typical | Worst case |
|---|---|---|---|
| Command word | 1 x WORD_TIME_US | 20 us | 20 us |
| Data words (N words) | N x WORD_TIME_US | N x 20 us | 32 x 20 = 640 us |
| RT response delay | configured (default 8 us; range 4-12 us) | 8 us | 12 us |
| Status word | 1 x WORD_TIME_US | 20 us | 20 us |
| Inter-message gap | >= T_GAP_MIN_US | 4 us | 4 us (minimum honored; scheduler may allot more) |

## 4. Worst-Case Message Durations

### 4.1 BC-to-RT (receive), N=32 data words

```
command(20) + data(32*20=640) + response_delay(12) + status(20) + gap(4)
= 20 + 640 + 12 + 20 + 4 = 696 us
```

### 4.2 RT-to-BC (transmit), N=32 data words

```
command(20) + response_delay(12) + status(20) + data(640) + gap(4)
= 20 + 12 + 20 + 640 + 4 = 696 us
```

(Same total: the response delay and status word occur before the data
block instead of after, but the phase durations are identical.)

### 4.3 RT-to-RT, N=32 data words

```
recv_cmd(20) + xmit_cmd(20) [issued back-to-back, LLR-BC-005]
+ response_delay(12) + status_xmit(20) + data(640)
+ response_delay(12) + status_recv(20) + gap(4)
= 20 + 20 + 12 + 20 + 640 + 12 + 20 + 4 = 748 us
```

RT-to-RT is the longest message type at maximum word count, since it has
two response delays and two status words on the critical path.

### 4.4 Mode code, no data word

```
mode_cmd(20) + response_delay(12) + status(20) + gap(4) = 56 us
```

### 4.5 Mode code, with data word

```
mode_cmd(20) + data(20) + response_delay(12) + status(20) + gap(4) = 76 us
```

### 4.6 Broadcast (no status expected, LLR-BC-007)

```
cmd(20) + data(N*20, 0 if none) + gap(4)
```
No response delay or status phase — the BC declares the message complete
after the last word plus the gap.

## 5. Minor Frame Sizing Implication

LLR-1553-BC-001 requires the schedule validator to reject overlapping
slots. Given the worst-case durations in §4, a minor frame containing
messages must allot at least the sum of each scheduled message's
worst-case duration (using its own configured word count, not
necessarily 32) plus gaps between them. The validator computes this sum
at `bc_init` time and rejects the table if it exceeds the configured
minor frame period (LLR-1553-BC-002).

## 6. No-Response Timeout Relationship to Response Window

T_NORESP_US (14 us) is intentionally 2 us beyond T_RESP_MAX_US (12 us),
giving margin so that a response starting at exactly 12 us is never
mistaken for a timeout by a BC whose own timer has any small jitter. Per
LLR-1553-BC-009, this project's virtual clock (LLR-1553-BUS-003) makes
this margin exact rather than approximate — test cases at 11, 12, 13 and
14 us (LLR-1553-BC-009 verification note) are expected to be
deterministic, not flaky, specifically because of this virtual-clock
design choice.

## 7. Relationship to Manchester/Word Timing (LLR-1553-WORD-006)

WORD_TIME_US (20 us) derives directly from 40 half-bit symbols x
BIT_TIME_US (1 us per half-bit cell, i.e. 0.5 us per full bit at 1 Mbit/s
doubled for the two-symbol Manchester encoding of each bit). Any change
to the symbol count in LLR-1553-WORD-006 must be reflected here and in
the base constants table (§2), since every downstream schedule and
timeout calculation depends on it.

---
