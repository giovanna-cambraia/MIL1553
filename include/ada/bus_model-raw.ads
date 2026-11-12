--  project:    MIL1553
--  purpose:    the raw, un-interposed transmit: puts a word in the channel's ring log and nothing else. Called by Bus_Model's Transmit subunit (the release one directly, 
--              the test one after the error injector's filter)

--  realizes:   LLR-1553-BUS-001, LLR-1553-BUS-002, LLR-1553-ERR-005

--  this is a PRIVATE child package, the Ada rules let only the parent's body and other children of Bus_Model name it, so BC, RT, BM and the BIT modules cannot
--  call it and bypass Bus_Model.Transmit. It is the compiler-enforced version of the C's bus_model_internal.h ("only bus_model.c and the two bus_model_tx*.c
--  wrappers may include this header")

--  its postcondition is the STRONG one: exactly one ring slot is written, the channel's write count goes up by one, and nothing else changes; the public Bus_Model.Transmit 
--  promises less, because the test build's fault hook may drop a word the sender believes it sent

with Mil1553_Types;   use Mil1553_Types;
with Bus_Model_Types; use Bus_Model_Types;

private package Bus_Model.Raw
  with SPARK_Mode => On
is

   --  the ring slot the next word on a channel goes into, given how many words were written before it
   function Slot_Of (Total_Written : Counter) return Ring_Index
   is (Ring_Index (Total_Written mod Queue_Capacity))
   with Global => null;

   --  implements: LLR-1553-BUS-001, LLR-1553-BUS-002
   --  if the channel is failed: Sent is False and nothing changes; otherwise the word (symbols, the current virtual clock, the sender) is stored in the next ring slot, 
   --  overwriting the oldest word once the ring is full, and the channel's Total_Written goes up by one; the clock, every cursor, the other channel and the failed flags are
   --  untouched
   procedure Transmit_Raw
     (Ctx    : in out Context;
      Chan   : Channel;
      Syms   : Symbols;
      Sender : Terminal_Id;
      Sent   : out Boolean)
   with Global  => null,
        Depends => (Ctx  => (Ctx, Chan, Syms, Sender),
                    Sent => (Ctx, Chan)),
        Pre     => True,
        Post    =>
          Sent = not Ctx'Old.Chan (Chan).Failed
          and then Ctx.Clock = Ctx'Old.Clock
          and then Ctx.Read_Count     = Ctx'Old.Read_Count
          and then Ctx.Dropped_Count  = Ctx'Old.Dropped_Count
          and then Ctx.Loopback_Count = Ctx'Old.Loopback_Count
          and then Ctx.Chan (Chan).Failed = Ctx'Old.Chan (Chan).Failed
          and then (for all C in Channel => (if C /= Chan then Ctx.Chan (C) = Ctx'Old.Chan (C)))
          and then
            (if Ctx'Old.Chan (Chan).Failed then
               Ctx = Ctx'Old
             else
               Ctx.Chan (Chan).Total_Written = Ctx'Old.Chan (Chan).Total_Written + 1
               and then Ctx.Chan (Chan).Records (Slot_Of (Ctx'Old.Chan (Chan).Total_Written)) =
                          (Syms => Syms, Timestamp => Ctx.Clock, Sender => Sender)
               and then (for all I in Ring_Index =>
                           (if I /= Slot_Of (Ctx'Old.Chan (Chan).Total_Written) then
                              Ctx.Chan (Chan).Records (I) = Ctx'Old.Chan (Chan).Records (I))));

end Bus_Model.Raw;