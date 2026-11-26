--  project:    MIL1553
--  purpose:    TEST BUILD body of Bus_Model.Transmit: runs every word through the error injector's filter before it reaches the channel

--  realizes:   LLR-1553-ERR-008 (interposition point, SDD 4.5)

--  used ONLY by the test build. mil1553_test.gpr excludes the release subunit (src/ada/bus_model-transmit.adb) and names this file as the body of
--  Bus_Model.Transmit; exactly one of the two is in any build, chosen by the project file, never by a run-time branch (LLR-1553-ERR-005)

--  the "channel failed" test comes FIRST, before the filter: a failed channel returns Sent = False with the context untouched, which is what keeps
--  Bus_Model.Transmit's postcondition true (Sent = not Failed, and "if failed then Ctx = Ctx'Old"); the filter may advance the clock
--  (Delay_Response), so it must not run on a word that goes nowhere

--  SPARK_Mode is Off here: Error_Inject keeps global state and is outside the certified item. The postcondition of Bus_Model.Transmit is ASSUMED by the
--  provers for this build, not proved; it is carried by the checks above and by Bus_Model.Raw.Transmit_Raw's strong contract

with Bus_Model.Raw;
with Error_Inject;

separate (Bus_Model)
procedure Transmit
  (Ctx    : in out Context;
   Chan   : Channel;
   Syms   : Symbols;
   Sender : Terminal_Id;
   Sent   : out Boolean)
is
   pragma SPARK_Mode (Off);

   Working : Symbols := Syms;
   Drop    : Boolean;
begin
   if Ctx.Chan (Chan).Failed then
      Sent := False;                       --  nothing is stored, nothing changes, the filter is not consulted
      return;
   end if;

   Error_Inject.Filter_Transmit (Ctx, Chan, Sender, Working, Drop);

   if Drop then
      --  the sender believes the word went out; it never reached the channel (Suppress_Response / Wrong_Word_Count)
      Sent := True;
      return;
   end if;

   Raw.Transmit_Raw (Ctx, Chan, Working, Sender, Sent);
end Transmit;