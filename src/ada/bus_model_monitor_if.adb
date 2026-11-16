--  project:  MIL1553
--  realizes: LLR-1553-BM-001, LLR-1553-BM-003

--  the with-clause for Bus_Model is here in the BODY, where clients of this package (Bm_Monitor) cannot see it; the visible interface stays receive-only

with Bus_Model;

package body Bus_Model_Monitor_If
  with SPARK_Mode => On
is

   --  implements: LLR-1553-BM-001, LLR-1553-BUS-002 (receive side only)
   procedure Receive
     (Ctx  : in out Context;
      Chan : Channel;
      Rec  : out Bus_Record;
      Got  : out Boolean)
   is
   begin
      --  always the BM's own cursor: the monitor cannot read through another terminal's position; the postcondition of this procedure is the
      --  Bus_Model.Receive postcondition with Listener = BM
      Bus_Model.Receive (Ctx, Chan, BM, Rec, Got);
   end Receive;

end Bus_Model_Monitor_If;