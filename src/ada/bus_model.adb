
--  project:  MIL1553
--  realizes: LLR-1553-BUS-001, LLR-1553-BUS-003, and the in-process part of LLR-1553-BUS-002

--  proof status: written to be proved with GNATprove, NOT yet run (no GNAT in the  author's environment); the loop invariants in Receive and Receive_Loopback
--  carry the argument that a listener always ends up caught up within one ring's worth of iterations; expect to tune them on the first proof run

--  difference from the C (bus_model.c): "how far behind is this listener" is Total_Written - Read_Count, computed in modular arithmetic; that stays right
--  when the 32-bit counters wrap; the C's "read_pos >= total_written" comparison would not (after about four billion words, so theoretical, but free to avoid)

with Bus_Model.Raw;

package body Bus_Model
  with SPARK_Mode => On
is

   --  implements: LLR-1553-BUS-001, LLR-1553-BUS-003
   procedure Init (Ctx : out Context) is
   begin
      Ctx := (others => <>);       --  every component has a default: all zero / not failed
   end Init;

   --  implements: LLR-1553-BUS-001, LLR-1553-BUS-002
   --  the body is a subunit, selected by the build profile (see the spec)
   procedure Transmit
     (Ctx    : in out Context;
      Chan   : Channel;
      Syms   : Symbols;
      Sender : Terminal_Id;
      Sent   : out Boolean) is separate;


   --  implements: LLR-1553-BUS-001, LLR-1553-BUS-002
   --  advances Listener's cursor past its own transmissions and past any words it fell too far behind to read, and returns the next word it has not seen and
   --  did not send; at most Queue_Capacity iterations: after the "fell behind" correction a listener is at most one ring behind, and every iteration consumes one word
   procedure Receive
     (Ctx      : in out Context;
      Chan     : Channel;
      Listener : Terminal_Id;
      Rec      : out Bus_Record;
      Got      : out Boolean)
   is
   begin
      Rec := (others => <>);
      Got := False;

      if Ctx.Chan (Chan).Failed then
         return;
      end if;

      for Iteration in 1 .. Queue_Capacity loop
         declare
            Behind : constant Counter :=
              Ctx.Chan (Chan).Total_Written - Ctx.Read_Count (Chan, Listener);
         begin
            exit when Behind = 0;                            --  caught up

            if Behind > Queue_Capacity then
               --  fell behind the ring: the words beyond its capacity are gone
               Ctx.Dropped_Count (Chan, Listener) :=
                 Ctx.Dropped_Count (Chan, Listener) + (Behind - Queue_Capacity);
               Ctx.Read_Count (Chan, Listener) :=
                 Ctx.Chan (Chan).Total_Written - Queue_Capacity;
            end if;

            declare
               Slot : constant Ring_Index := Raw.Slot_Of (Ctx.Read_Count (Chan, Listener));
               Seen : constant Bus_Record  := Ctx.Chan (Chan).Records (Slot);
            begin
               Ctx.Read_Count (Chan, Listener) := Ctx.Read_Count (Chan, Listener) + 1;

               if Seen.Sender /= Listener then
                  Rec := Seen;
                  Got := True;
                  return;
               end if;
               --  else: this listener's own transmission - skip it
            end;
         end;

         pragma Loop_Invariant (not Got);
         pragma Loop_Invariant (Ctx.Chan = Ctx.Chan'Loop_Entry);
         pragma Loop_Invariant (Ctx.Clock = Ctx.Clock'Loop_Entry);
         pragma Loop_Invariant (Ctx.Loopback_Count = Ctx.Loopback_Count'Loop_Entry);
         pragma Loop_Invariant
           (for all C in Channel =>
              (for all T in Terminal_Id =>
                 (if C /= Chan or else T /= Listener then
                    Ctx.Read_Count (C, T) = Ctx.Read_Count'Loop_Entry (C, T)
                    and then Ctx.Dropped_Count (C, T) = Ctx.Dropped_Count'Loop_Entry (C, T))));
         pragma Loop_Invariant
           (Ctx.Chan (Chan).Total_Written - Ctx.Read_Count (Chan, Listener)
              <= Counter (Queue_Capacity - Iteration));
      end loop;
   end Receive;

   --  implements: LLR-1553-BUS-001, LLR-1553-BUS-002, LLR-1553-BIT-007
   --  the loopback counterpart: the next word sent by Terminal itself, on a  of its own. Words from other terminals are skipped (and consumed on this cursor only)
   procedure Receive_Loopback
     (Ctx      : in out Context;
      Chan     : Channel;
      Terminal : Terminal_Id;
      Rec      : out Bus_Record;
      Got      : out Boolean)
   is
   begin
      Rec := (others => <>);
      Got := False;

      if Ctx.Chan (Chan).Failed then
         return;
      end if;

      for Iteration in 1 .. Queue_Capacity loop
         declare
            Behind : constant Counter :=
              Ctx.Chan (Chan).Total_Written - Ctx.Loopback_Count (Chan, Terminal);
         begin
            exit when Behind = 0;

            if Behind > Queue_Capacity then
               --  a defensive bound: IBIT reads its own word straight after sending it, so this is not a normal occurrence (and, unlike the normal cursor, no drop count is 
               --  kept for it)
               Ctx.Loopback_Count (Chan, Terminal) :=
                 Ctx.Chan (Chan).Total_Written - Queue_Capacity;
            end if;

            declare
               Slot : constant Ring_Index := Raw.Slot_Of (Ctx.Loopback_Count (Chan, Terminal));
               Seen : constant Bus_Record  := Ctx.Chan (Chan).Records (Slot);
            begin
               Ctx.Loopback_Count (Chan, Terminal) := Ctx.Loopback_Count (Chan, Terminal) + 1;

               if Seen.Sender = Terminal then
                  Rec := Seen;
                  Got := True;
                  return;
               end if;
               --  else: someone else's word - skip it
            end;
         end;

         pragma Loop_Invariant (not Got);
         pragma Loop_Invariant (Ctx.Chan = Ctx.Chan'Loop_Entry);
         pragma Loop_Invariant (Ctx.Clock = Ctx.Clock'Loop_Entry);
         pragma Loop_Invariant (Ctx.Read_Count = Ctx.Read_Count'Loop_Entry);
         pragma Loop_Invariant (Ctx.Dropped_Count = Ctx.Dropped_Count'Loop_Entry);
      end loop;
   end Receive_Loopback;

   --  implements: LLR-1553-BUS-001
   procedure Set_Channel_Failed (Ctx : in out Context; Chan : Channel; Failed : Boolean) is
   begin
      Ctx.Chan (Chan).Failed := Failed;

      if Failed then
         --  a failed channel delivers nothing: no backlog is held for later and none is replayed when the channel is restored
         for T in Terminal_Id loop
            Ctx.Read_Count (Chan, T)     := Ctx.Chan (Chan).Total_Written;
            Ctx.Loopback_Count (Chan, T) := Ctx.Chan (Chan).Total_Written;
            pragma Loop_Invariant
              (for all U in Terminal_Id =>
                 (if U <= T then
                    Ctx.Read_Count (Chan, U)     = Ctx.Chan (Chan).Total_Written
                    and then Ctx.Loopback_Count (Chan, U) = Ctx.Chan (Chan).Total_Written));
            pragma Loop_Invariant (Ctx.Chan = Ctx.Chan'Loop_Entry);
         end loop;
      end if;
   end Set_Channel_Failed;

   --  implements: LLR-1553-BUS-003
   procedure Advance_Us (Ctx : in out Context; Delta_Us : Time_Us) is
   begin
      Ctx.Clock := Ctx.Clock + Delta_Us;      --  modular, like the C uint64_t
   end Advance_Us;

end Bus_Model;