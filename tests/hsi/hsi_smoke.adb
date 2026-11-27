--  project:    MIL1553
--  purpose:    HSI smoke test: BC, RT1, RT2 and BM on ONE in-process bus, driven by one loop on the virtual clock (the Ada counterpart of tests/hsi/hsi_smoke.c)
--  realizes:   no LLR of its own; the first end-to-end exercise of the Ada side: LLR-1553-BC-003 .. BC-005, RT-001 .. RT-007, BM-001, BM-002, BUS-001, BIT-005

--  the schedule is the one of hsi_smoke.c
--    entry 0: RT1 <- RT2, RT-to-RT, 4 words, slot offset 0     (Address = receiving RT = RT1, Address_2 = transmitting RT = RT2)
--    entry 1: BC -> RT1, 3 words, slot offset 300
--    entry 2: RT2 -> BC, 2 words, slot offset 600
--  one minor frame of 1000 us; RT1 and RT2 have subaddresses 1 and 2 enabled both ways, 1 .. 32 words, response delay 8 us

--  this prints a summary and does NOT assert exact values: compare it with the output of the C smoke test. The exit status is 1 only when an Init fails or
--  the RT-to-RT exchange left nothing in the receiving RT's buffer (the one thing that cannot be a timing detail)

--  SPARK_Mode is Off: text I/O, not part of the proved item
--  proof status: NOT compiled, NOT run (no GNAT available to the author)

pragma SPARK_Mode (Off);

with Ada.Command_Line;
with Ada.Text_IO;
with Bc_Scheduler;
with Bit_Cbit;
with Bm_Monitor;
with Bus_Model;
with Bus_Model_Types;
with Mil1553_Types; use Mil1553_Types;
with Rt_Service;

procedure Hsi_Smoke is
   use Ada.Text_IO;

   Run_Until_Us : constant Time_Us := 3_000;    --  at least two major frames; the bound is on the virtual clock, not on step count

   Bus      : Bus_Model_Types.Context;
   Bc       : Bc_Scheduler.Bc_Context;
   Rt1, Rt2 : Rt_Service.Rt_Context;
   Mon      : Bm_Monitor.Monitor_Context;

   Cbit_Bc  : Bit_Cbit.Cbit_Context;           --  attached to the BC, as in the C
   Cbit_R1  : Bit_Cbit.Cbit_Context;           --  Rt_Service.Run needs one; never attached, so it stays untouched
   Cbit_R2  : Bit_Cbit.Cbit_Context;

   Sched    : Bc_Scheduler.Schedule;
   Cfg      : Rt_Service.Subaddr_Configs;
   Ok       : Boolean;
   Active   : Boolean;
   Failed   : Boolean := False;

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

   --  four rounds of (BC, RT1, RT2, BM) per step so that each logical instant gives every party a chance to react, then a 1 us nudge so the loop makes
   --  forward progress even if nothing transmits (as in the C)
   procedure Step_All is
   begin
      for K in 1 .. 4 loop
         Bc_Scheduler.Run_Frame (Bc, Bus, Cbit_Bc, Active);
         Rt_Service.Run (Rt1, Bus, Cbit_R1, Active);
         Rt_Service.Run (Rt2, Bus, Cbit_R2, Active);
         Bm_Monitor.Run (Mon, Bus);
      end loop;
      Bus_Model.Advance_Us (Bus, 1);
   end Step_All;
begin
   --  RT subaddresses 1 and 2: enabled both ways, 1 .. 32 words
   Cfg (1) := (Enabled => True, Allow_Receive => True, Allow_Transmit => True,
               Min_Word_Count => 1, Max_Word_Count => Max_Data_Words);
   Cfg (2) := Cfg (1);

   Sched.Minor_Frame_Count := 1;
   Sched.Period_Us         := 1_000;
   Sched.Entry_Count       := 3;
   Sched.Entries (0) := (Kind => RT_To_RT, Address => 1, Address_2 => 2, Sub => 1, Sub_2 => 2,
                         Word_Count => 4, Slot_Offset_Us => 0, others => <>);
   Sched.Entries (1) := (Kind => BC_To_RT, Address => 1, Sub => 1,
                         Word_Count => 3, Slot_Offset_Us => 300, others => <>);
   Sched.Entries (2) := (Kind => RT_To_BC, Address => 2, Sub => 2,
                         Word_Count => 2, Slot_Offset_Us => 600, others => <>);

   Bus_Model.Init (Bus);

   Bc_Scheduler.Init (Bc, Sched, Initial_Primary => A, Fail_Threshold => 3, Ok => Ok);
   Put_Line ("bc_init: " & Boolean'Image (Ok));
   Failed := Failed or not Ok;

   Rt_Service.Init (Rt1, 1, RT1, Cfg, RT_Response_Delay_Default_Us, Ok);
   Put_Line ("rt1 init: " & Boolean'Image (Ok));
   Failed := Failed or not Ok;

   Rt_Service.Init (Rt2, 2, RT2, Cfg, RT_Response_Delay_Default_Us, Ok);
   Put_Line ("rt2 init: " & Boolean'Image (Ok));
   Failed := Failed or not Ok;

   if Failed then
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
      return;
   end if;

   Bm_Monitor.Init (Mon);
   Bit_Cbit.Init (Cbit_Bc, 0);
   Bit_Cbit.Init (Cbit_R1, 0);
   Bit_Cbit.Init (Cbit_R2, 0);
   Bc_Scheduler.Attach_Cbit (Bc);

   Rt_Service.Pbit_Complete (Rt1, True);
   Rt_Service.Enable (Rt1, Ok);
   Rt_Service.Pbit_Complete (Rt2, True);
   Rt_Service.Enable (Rt2, Ok);
   Bc_Scheduler.Start (Bc, Ok);

   while Bus_Model.Now_Us (Bus) < Run_Until_Us loop
      Step_All;
   end loop;

   Bm_Monitor.Reconstruct_Messages (Mon);

   Put_Line ("t=" & Img (Bus_Model.Now_Us (Bus))
             & " bc_state=" & BC_State'Image (Bc_Scheduler.Get_State (Bc))
             & " bc_errlog=" & Img (Bc_Scheduler.Error_Log_Count (Bc))
             & " rt1_errlog=" & Img (Rt_Service.Error_Log_Count (Rt1))
             & " rt2_errlog=" & Img (Rt_Service.Error_Log_Count (Rt2)));

   for I in 0 .. Bc_Scheduler.Error_Log_Count (Bc) - 1 loop
      declare
         Rec : constant Error_Record := Bc_Scheduler.Error_At (Bc, I);
      begin
         Put_Line ("  bc err" & Natural'Image (I) & ": kind=" & Error_Kind'Image (Rec.Kind)
                   & " t=" & Img (Rec.Timestamp));
      end;
   end loop;
   for I in 0 .. Rt_Service.Error_Log_Count (Rt1) - 1 loop
      Put_Line ("  rt1 err" & Natural'Image (I) & ": kind="
                & Error_Kind'Image (Rt_Service.Error_At (Rt1, I).Kind));
   end loop;
   for I in 0 .. Rt_Service.Error_Log_Count (Rt2) - 1 loop
      Put_Line ("  rt2 err" & Natural'Image (I) & ": kind="
                & Error_Kind'Image (Rt_Service.Error_At (Rt2, I).Kind));
   end loop;

   Put_Line ("rt1 sub1 word_count=" & Img (Rt1.Buffers (1).Used)
             & ", rt2 sub2 word_count=" & Img (Rt2.Buffers (2).Used));

   for I in 0 .. Bm_Monitor.Message_Count (Mon) - 1 loop
      declare
         Msg : constant Bm_Monitor.Message := Bm_Monitor.Message_At (Mon, I);
      begin
         Put_Line ("  bm msg" & Natural'Image (I)
                   & ": kind=" & Message_Kind'Image (Msg.Kind)
                   & " rt=" & Img (Natural (Msg.Address))
                   & " rt2=" & Img (Natural (Msg.Address_2))
                   & " resp_seen=" & Boolean'Image (Msg.Response_Seen)
                   & " resp_us=" & Img (Msg.Response_Time_Us));
      end;
   end loop;

   Put_Line ("cbit: A good=" & Bit_Cbit.Count'Image (Cbit_Bc.Chan (A).Good_Count_Total)
             & " err=" & Bit_Cbit.Count'Image (Cbit_Bc.Chan (A).Error_Count_Total)
             & " rtviol=" & Bit_Cbit.Count'Image (Cbit_Bc.Response_Time_Violations)
             & " wd=" & Boolean'Image (Cbit_Bc.Watchdog_Tripped));

   --  the one hard check: the RT-to-RT exchange (entry 0) or the BC-to-RT write (entry 1) must have left words in RT1's subaddress 1 buffer
   if Rt1.Buffers (1).Used = 0 then
      Put_Line ("smoke: FAIL (RT1 subaddress 1 buffer is empty)");
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
   else
      Put_Line ("smoke: ran to completion, compare with the C output");
   end if;
end Hsi_Smoke;