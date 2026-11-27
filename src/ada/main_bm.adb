--  project:    MIL1553
--  purpose:    entry point of the bus monitor role: passive capture on both channels, message reconstruction and a summary
--  realizes:   no LLR of its own; exercises LLR-1553-BM-001, BM-002, BM-003

--  LLR-1553-BM-003 (no transmit path) is a property of Bm_Monitor, which never names Bus_Model. THIS unit is the role's main and owns the in-process bus, so
--  it does name Bus_Model, to create the bus and to move the virtual clock; it never transmits. The isolation check script looks at bm_monitor.ads and
--  bm_monitor.adb only, not at this file

--  limitation to know about: the wire transport between processes (LLR-1553-BUS-002) is not implemented, so this binary owns an in-process bus with no
--  other terminal on it. The monitor sees no traffic, and the summary shows an empty capture and no messages. The monitoring of a real exchange needs the
--  roles on one bus (the Ada counterpart of hsi_smoke.c)

--  SPARK_Mode is Off: this unit does text I/O and is not part of the proved item
--  proof status: NOT compiled, NOT run (no GNAT available to the author)

pragma SPARK_Mode (Off);

with Ada.Command_Line;
with Ada.Text_IO;
with Bm_Monitor;
with Bus_Model;
with Bus_Model_Types;
with Mil1553_Types; use Mil1553_Types;

procedure Main_Bm is
   use Ada.Text_IO;

   Run_Until_Us : constant Time_Us := 3_000;    --  the bound is on the virtual clock, not on step count

   Bus : Bus_Model_Types.Context;
   Mon : Bm_Monitor.Monitor_Context;

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
   Bus_Model.Init (Bus);
   Bm_Monitor.Init (Mon);

   while Bus_Model.Now_Us (Bus) < Run_Until_Us loop
      Bm_Monitor.Run (Mon, Bus);              --  passive: only the BM's own read cursors move
      Bus_Model.Advance_Us (Bus, 1);          --  forward progress even if nothing transmits
   end loop;

   Bm_Monitor.Reconstruct_Messages (Mon);

   declare
      S : constant Bm_Monitor.Stats := Bm_Monitor.Get_Stats (Mon);
   begin
      Put_Line ("t=" & Img (Bus_Model.Now_Us (Bus))
                & " captured=" & Img (S.Captured_Count)
                & " dropped=" & Bm_Monitor.Count'Image (S.Dropped_Count)
                & " messages=" & Img (S.Message_Count)
                & " msg_overflow=" & Bm_Monitor.Count'Image (S.Message_Overflow_Count));
   end;

   for I in 0 .. Bm_Monitor.Message_Count (Mon) - 1 loop
      declare
         Msg : constant Bm_Monitor.Message := Bm_Monitor.Message_At (Mon, I);
      begin
         Put_Line ("  bm msg" & Natural'Image (I)
                   & ": kind=" & Message_Kind'Image (Msg.Kind)
                   & " chan=" & Channel'Image (Msg.Chan)
                   & " rt=" & Img (Natural (Msg.Address))
                   & " rt2=" & Img (Natural (Msg.Address_2))
                   & " resp_seen=" & Boolean'Image (Msg.Response_Seen)
                   & " resp_us=" & Img (Msg.Response_Time_Us));
      end;
   end loop;
end Main_Bm;