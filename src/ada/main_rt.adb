--  project:    MIL1553
--  purpose:    entry point of the remote terminal role: PBIT, enable, then the service loop with CBIT cycles and self-test requests on the virtual clock

--  realizes:   no LLR of its own; exercises LLR-1553-RT-001 .. RT-013, BIT-001 .. BIT-009
--  usage:      main_rt [1|2]    the terminal (RT1 = address 1, RT2 = address 2); default 1

--  the wiring decisions that Rt_Service deliberately leaves to the role's main loop (see rt_service.ads)
--    * Rt_Service does not depend on the BIT modules: this unit runs PBIT and reports it with Pbit_Complete, takes the self-test request (mode code 3) and
--      runs IBIT, runs a CBIT cycle once per Cbit_Period_Us and pushes the BIT state into the RT with Apply_Bit_State

--  limitation to know about: the wire transport between processes (LLR-1553-BUS-002) is not implemented, so this binary owns an in-process bus with no BC
--  on it. The RT passes PBIT, enables, and then waits for commands that never come; the summary shows an idle, healthy RT. The exchange with a BC needs the
--  roles on one bus (the Ada counterpart of hsi_smoke.c)

--  SPARK_Mode is Off: this unit does text I/O and is not part of the proved item
--  proof status: NOT compiled, NOT run (no GNAT available to the author)

pragma SPARK_Mode (Off);

with Ada.Command_Line;
with Ada.Text_IO;
with Bit_Cbit;
with Bit_Ibit;
with Bit_Pbit;
with Bit_Types;
with Bus_Model;
with Bus_Model_Types;
with Mil1553_Types; use Mil1553_Types;
with Rt_Service;

procedure Main_Rt is
   use Ada.Text_IO;

   Run_Until_Us  : constant Time_Us := 3_000;    --  the bound is on the virtual clock, not on step count
   Cbit_Period_Us : constant Time_Us := 1_000;   --  one CBIT cycle per period

   Terminal : Terminal_Id := RT1;
   Address  : RT_Address  := 1;

   Bus      : Bus_Model_Types.Context;
   Rt       : Rt_Service.Rt_Context;
   Cbit     : Bit_Cbit.Cbit_Context;
   Pbit     : Bit_Pbit.Pbit_Context;
   Bit      : Bit_Types.Result;
   Cfg      : Rt_Service.Subaddr_Configs;
   Ok       : Boolean;
   Passed   : Boolean;
   Active   : Boolean;
   Wanted   : Boolean;
   Next_Cbit : Time_Us := Cbit_Period_Us;

   function Img (T : Time_Us) return String is
      S : constant String := Time_Us'Image (T);
   begin
      return S (S'First + 1 .. S'Last);
   end Img;

   function Img (N : Natural) return String is
      S : constant String := Natural'Image (N);
   begin
      return S (S'First + 1 .. S'Last);
   end Img;
begin
   if Ada.Command_Line.Argument_Count >= 1 and then Ada.Command_Line.Argument (1) = "2" then
      Terminal := RT2;
      Address  := 2;
   end if;

   --  subaddresses 1 and 2 enabled both ways, 1 .. 32 words, as in hsi_smoke.c
   Cfg (1) := (Enabled => True, Allow_Receive => True, Allow_Transmit => True,
               Min_Word_Count => 1, Max_Word_Count => Max_Data_Words);
   Cfg (2) := Cfg (1);

   Bus_Model.Init (Bus);

   Rt_Service.Init (Rt, Own_Address => Address, Terminal => Terminal, Configs => Cfg,
                    Delay_Us => RT_Response_Delay_Default_Us, Ok => Ok);
   Put_Line ("rt init: " & Boolean'Image (Ok));
   if not Ok then
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
      return;
   end if;

   Bit_Types.Init (Bit);
   Bit_Pbit.Init (Pbit);
   Bit_Cbit.Init (Cbit, 0);

   --  PBIT blocks the terminal: it is enabled only on a pass (LLR-1553-BIT-004)
   Bit_Pbit.Run (Pbit, Bit, Bus, Terminal, Passed);
   Put_Line ("pbit: " & Boolean'Image (Passed));
   Rt_Service.Pbit_Complete (Rt, Passed);
   Rt_Service.Apply_Bit_State (Rt, Bit.State, Bit_Types.To_Word (Bit));

   if Passed then
      Rt_Service.Enable (Rt, Ok);
      Put_Line ("rt enable: " & Boolean'Image (Ok));
      Rt_Service.Attach_Cbit (Rt);
   else
      Put_Line ("PBIT failed: the RT stays in FAIL_SAFE and is not enabled");
   end if;

   while Bus_Model.Now_Us (Bus) < Run_Until_Us loop
      Rt_Service.Run (Rt, Bus, Cbit, Active);

      --  mode code 3 (initiate self-test): the RT only raises the request, IBIT runs here
      Rt_Service.Take_Self_Test_Request (Rt, Wanted);
      if Wanted then
         Bit_Ibit.Run (Bit, Bus, Terminal, Passed);
         Rt_Service.Apply_Bit_State (Rt, Bit.State, Bit_Types.To_Word (Bit));
         Put_Line ("ibit at t=" & Img (Bus_Model.Now_Us (Bus)) & ": " & Boolean'Image (Passed));
      end if;

      --  one CBIT cycle per period, only while the BIT state allows it
      if Active and then Bus_Model.Now_Us (Bus) >= Next_Cbit then
         Bit_Cbit.Update_Result (Cbit, Bit, Bus_Model.Now_Us (Bus));
         Rt_Service.Apply_Bit_State (Rt, Bit.State, Bit_Types.To_Word (Bit));
         Next_Cbit := Next_Cbit + Cbit_Period_Us;
      end if;

      Bus_Model.Advance_Us (Bus, 1);          --  forward progress even if nothing transmits
   end loop;

   declare
      H : constant Rt_Service.Health := Rt_Service.Get_Health (Rt);
   begin
      Put_Line ("t=" & Img (Bus_Model.Now_Us (Bus))
                & " rt" & Img (Natural (Address))
                & " state=" & RT_State'Image (H.State)
                & " busy=" & Boolean'Image (H.Busy)
                & " me=" & Boolean'Image (H.Message_Error)
                & " fail_safe=" & Boolean'Image (H.Fail_Safe)
                & " errlog=" & Img (Rt_Service.Error_Log_Count (Rt)));
   end;

   for I in 0 .. Rt_Service.Error_Log_Count (Rt) - 1 loop
      Put_Line ("  rt err" & Natural'Image (I) & ": kind="
                & Error_Kind'Image (Rt_Service.Error_At (Rt, I).Kind));
   end loop;

   Put_Line ("bit: state=" & BIT_State'Image (Bit.State)
             & " word=" & Data_Word'Image (Bit_Types.To_Word (Bit))
             & " ignored=" & Bit_Types.Event_Count'Image (Bit.Ignored_Event_Count));
   Put_Line ("rt sub1 word_count=" & Img (Rt.Buffers (1).Used)
             & ", sub2 word_count=" & Img (Rt.Buffers (2).Used));
end Main_Rt;