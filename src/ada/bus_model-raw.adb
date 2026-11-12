--  project:  MIL1553
--  realizes: LLR-1553-BUS-001, LLR-1553-BUS-002

--  proof status: written to be proved with GNATprove, NOT yet run (no GNAT in the
--  author's environment); the postcondition is a framing argument (what changed
--  and what did not), expect it to prove directly

package body Bus_Model.Raw
  with SPARK_Mode => On
is

   --  implements: LLR-1553-BUS-001, LLR-1553-BUS-002
   procedure Transmit_Raw
     (Ctx    : in out Context;
      Chan   : Channel;
      Syms   : Symbols;
      Sender : Terminal_Id;
      Sent   : out Boolean)
   is
   begin
      if Ctx.Chan (Chan).Failed then
         Sent := False;                  --  a failed channel accepts nothing
         return;
      end if;

      declare
         Slot : constant Ring_Index := Slot_Of (Ctx.Chan (Chan).Total_Written);
      begin
         Ctx.Chan (Chan).Records (Slot) :=
           (Syms => Syms, Timestamp => Ctx.Clock, Sender => Sender);
         --  total_Written is modular: it wraps at 2**32, a multiple of the ring
         --  size, so Slot_Of stays consistent across the wrap
         Ctx.Chan (Chan).Total_Written := Ctx.Chan (Chan).Total_Written + 1;
      end;

      Sent := True;
   end Transmit_Raw;

end Bus_Model.Raw;