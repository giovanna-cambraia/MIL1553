--  project:    MIL1553
--  purpose:    entry point of the bus controller role: builds the schedule, initialises the BC and its CBIT, runs it on the virtual clock and prints a summary
--  realizes:   no LLR of its own; exercises LLR-1553-BC-001 .. BC-010, REDUN-001 .. REDUN-003, BIT-005, BIT-006

--  limitation to know about: the wire transport between processes (LLR-1553-BUS-002, design/bus_transport_contract.md) is not implemented, so this
--  binary owns an in-process bus with no other terminal on it. No RT answers, so every message ends in a no-response timeout, the BC fails over,
--  and the summary shows exactly that. It is a role entry point and a smoke test of the BC alone; the full RT-to-RT exchange needs the roles on one bus
--  (the Ada counterpart of hsi_smoke.c)

--  the schedule is the one of hsi_smoke.c, hard-coded: config/bus_config.yaml needs a parser that is not written yet
--    entry 0: RT1 <- RT2, RT-to-RT, 4 words, slot offset 0     (receiving RT = Address, transmitting RT = Address_2)
--    entry 1: BC -> RT1, 3 words, slot offset 300
--    entry 2: RT2 -> BC, 2 words, slot offset 600
--  one minor frame of 1000 us

--  SPARK_Mode is Off: this unit does text I/O and is not part of the proved item
--  proof status: NOT compiled, NOT run (no GNAT available to the author)

pragma SPARK_Mode (Off);

with Ada.Command_Line;
with Ada.Text_IO;
with Bc_Scheduler;
with Bit_Cbit;
with Bus_Model;
with Bus_Model_Types;
with Mil1553_Types; use Mil1553_Types;

procedure Main_Bc is
   use Ada.Text_IO;

   Run_Until_Us : constant Time_Us := 3_000;    --  at least two major frames; the clock is virtual, the bound is on it, not on step count

   Bus    : Bus_Model_Types.Context;
   Ctx    : Bc_Scheduler.Bc_Context;
   Cbit   : Bit_Cbit.Cbit_Context;
   Sched  : Bc_Scheduler.Schedule;
   Ok     : Boolean;
   Active : Boolean;

   function Img (N : Natural) return String is
      S : constant String := Natural'Image (N);
   begin
      return S (S'First + 1 .. S'Last);
   end Img;

   function Img (T : Time_Us) return String is
      S : constant String := Time_Us'Image (T);
   begin
      return S (S'First + 1 .. S'Last);
   end Img;
begin
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

   Bc_Scheduler.Init (Ctx, Sched, Initial_Primary => A, Fail_Threshold => 3, Ok => Ok);
   Put_Line ("bc_init: " & Boolean'Image (Ok));
   if not Ok then
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
      return;
   end if;

   Bit_Cbit.Init (Cbit, 0);
   Bc_Scheduler.Attach_Cbit (Ctx);

   Bc_Scheduler.Start (Ctx, Ok);
   Put_Line ("bc_start: " & Boolean'Image (Ok));

   while Bus_Model.Now_Us (Bus) < Run_Until_Us loop
      Bc_Scheduler.Run_Frame (Ctx, Bus, Cbit, Active);
      Bus_Model.Advance_Us (Bus, 1);          --  forward progress even if nothing transmits
   end loop;

   Put_Line ("t=" & Img (Bus_Model.Now_Us (Bus))
             & " bc_state=" & BC_State'Image (Bc_Scheduler.Get_State (Ctx))
             & " bc_errlog=" & Img (Bc_Scheduler.Error_Log_Count (Ctx)));

   for I in 0 .. Bc_Scheduler.Error_Log_Count (Ctx) - 1 loop
      declare
         Rec : constant Error_Record := Bc_Scheduler.Error_At (Ctx, I);
      begin
         Put_Line ("  bc err" & Natural'Image (I) & ": kind=" & Error_Kind'Image (Rec.Kind)
                   & " t=" & Img (Rec.Timestamp));
      end;
   end loop;

   declare
      H : constant Bc_Scheduler.Health := Bc_Scheduler.Get_Health (Ctx);
   begin
      Put_Line ("health: degraded=" & Boolean'Image (H.Degraded)
                & " primary=" & Channel'Image (H.Primary_Bus));
   end;

   Put_Line ("cbit: A good=" & Bit_Cbit.Count'Image (Cbit.Chan (A).Good_Count_Total)
             & " err=" & Bit_Cbit.Count'Image (Cbit.Chan (A).Error_Count_Total)
             & " rtviol=" & Bit_Cbit.Count'Image (Cbit.Response_Time_Violations)
             & " wd=" & Boolean'Image (Cbit.Watchdog_Tripped));
end Main_Bc;