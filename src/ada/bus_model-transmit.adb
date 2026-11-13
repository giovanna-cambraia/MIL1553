
--  project:  MIL1553
--  purpose:  RELEASE body of Bus_Model.Transmit: forwards directly to the raw transmit; it contains no fault-injection hook of any kind

--  realizes: LLR-1553-BUS-001, LLR-1553-BUS-002, LLR-1553-ERR-005

--  used by every build EXCEPT the test build. mil1553_test.gpr excludes this file and names src/ada/errinject/bus_model_transmit_hooked.adb as the body of
--  Bus_Model.Transmit instead; exactly one of the two is in any given build, and the choice is made by the project file, never by a run-time branch

--  SPARK_Mode is inherited from the enclosing Bus_Model body (On); the postcondition of Bus_Model.Transmit follows from the stronger one on Bus_Model.Raw.Transmit_Raw

with Bus_Model.Raw;

separate (Bus_Model)
procedure Transmit
  (Ctx    : in out Context;
   Chan   : Channel;
   Syms   : Symbols;
   Sender : Terminal_Id;
   Sent   : out Boolean)
is
begin
   Raw.Transmit_Raw (Ctx, Chan, Syms, Sender, Sent);
end Transmit;