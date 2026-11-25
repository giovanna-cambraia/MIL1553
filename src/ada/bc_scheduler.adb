--  project:    MIL1553

--  realizes:   LLR-1553-BC-001 .. LLR-1553-BC-010, LLR-1553-REDUN-001 .. LLR-1553-REDUN-003

--  structure:  this file is the parent body: the BC's own mode code table, message duration, schedule validation, lifecycle, and the helpers; the state
--              machine, Run_Frame, is the subunit src/ada/bc_scheduler-run_frame.adb, which sees everything declared here before the stub
--  design simplifications carried over from the C (flagged for review at design baseline, SDP 2.2)
--    * BC-supplied data words (BC-to-RT, broadcast, mode 17) are a placeholder pattern (word index value; 0 for the mode data word): there is no mission
--      computer in scope (PSAC 2.3); the word-level format is fully real
--    * the first attempt of a message always uses Primary_Bus (the live REDUN-002 designation), not the entry's Preferred_Bus
--    * a Mode_Command entry addressed to 31 is a broadcast mode command: no status phase (BC-007)
--  proof status: NOT compiled, NOT proved (no GNAT in the author's environment)

with Bus_Model;
with Mil1553_Word;

package body Bc_Scheduler
  with SPARK_Mode => On
is

   --  mode codes this BC may schedule, per design/mode_code_table.md (LLR-1553-BC-006); the BC's own copy, deliberately not shared with Rt_Service (SDD 1)
   type Bc_Mode_Entry is record
      Supported     : Boolean;
      Has_Data_Word : Boolean;
      Data_From_BC  : Boolean;
   end record;

   Not_Listed : constant Bc_Mode_Entry := (False, False, False);

   Mode_Table : constant array (Mode_Code) of Bc_Mode_Entry :=
     (1 .. 8 => (True, False, False),
      16     => (True, True, False),       --  transmit vector word
      17     => (True, True, True),        --  synchronize with data
      18     => (True, True, False),       --  transmit last command
      19     => (True, True, False),       --  transmit BIT word
      others => Not_Listed);               --  includes 0 (dynamic bus control)

   function Alternate (Chan : Channel) return Channel
   is (if Chan = A then B else A);

   --  implements: LLR-1553-BC-007; True if no status/response phase follows this entry
   function Entry_Is_Broadcast (E : Schedule_Entry) return Boolean
   is (E.Kind = Broadcast
       or else (E.Kind = Mode_Command and then E.Address = RT_Addr_Broadcast));

   --  data subaddresses are 1 .. 30; 0 and 31 are mode indicators (LLR-1553-BC-006)
   function Sub_Is_Data (Sub : Subaddress) return Boolean
   is (Sub in 1 .. 30);

   --  the word count field of an entry (0 when the entry has no data count)
   function Wc_Field (E : Schedule_Entry) return Word_Count_Field
   is (if E.Word_Count >= 1 then Mil1553_Word.Encode_Word_Count (E.Word_Count) else 0);

   --  implements: LLR-1553-BC-001 (duration formulas per timing_budget.md 4)
   function Message_Duration_Us (E : Schedule_Entry) return Natural is
      N : constant Natural := E.Word_Count;
   begin
      case E.Kind is
         when BC_To_RT =>
            --  cmd + data(N) + resp_delay(max) + status + gap
            return Word_Time_Us + N * Word_Time_Us + T_Resp_Max_Us + Word_Time_Us + T_Gap_Min_Us;
         when RT_To_BC =>
            --  cmd + resp_delay(max) + status + data(N) + gap
            return Word_Time_Us + T_Resp_Max_Us + Word_Time_Us + N * Word_Time_Us + T_Gap_Min_Us;
         when RT_To_RT =>
            --  recv_cmd + xmit_cmd + resp_delay + status_xmit + data(N) + resp_delay + status_recv + gap
            return 2 * Word_Time_Us + T_Resp_Max_Us + Word_Time_Us + N * Word_Time_Us
                   + T_Resp_Max_Us + Word_Time_Us + T_Gap_Min_Us;
         when Mode_Command =>
            if Entry_Is_Broadcast (E) then
               --  cmd (+ data) + gap, no status phase
               return Word_Time_Us + (if E.Has_Data_Word then Word_Time_Us else 0) + T_Gap_Min_Us;
            elsif E.Has_Data_Word then
               --  mode_cmd + data(1) + resp_delay + status + gap (worst case of either direction)
               return 2 * Word_Time_Us + T_Resp_Max_Us + Word_Time_Us + T_Gap_Min_Us;
            else
               --  mode_cmd + resp_delay + status + gap
               return Word_Time_Us + T_Resp_Max_Us + Word_Time_Us + T_Gap_Min_Us;
            end if;
         when Broadcast =>
            --  cmd + data(N) + gap, no status phase
            return Word_Time_Us + N * Word_Time_Us + T_Gap_Min_Us;
      end case;
   end Message_Duration_Us;

   --  implements: LLR-1553-BC-001, LLR-1553-BC-006
   function Is_Valid (S : Schedule) return Boolean is
   begin
      --  (entry count <= 64 and minor frame count <= 32 hold by type)
      if S.Minor_Frame_Count = 0 or else S.Period_Us = 0 then
         return False;
      end if;

      for I in 0 .. S.Entry_Count - 1 loop
         declare
            E : Schedule_Entry renames S.Entries (I);
            Duration : constant Natural := Message_Duration_Us (E);
         begin
            if E.Minor_Frame_Index >= S.Minor_Frame_Count
              or else E.Slot_Offset_Us > S.Period_Us
            then
               return False;
            end if;

            --  RT address: a real RT (0 .. 30) for every addressed format; BROADCAST ignores it; a Mode_Command may also target 31
            if E.Kind /= Mode_Command and then E.Kind /= Broadcast
              and then E.Address > RT_Addr_Max
            then
               return False;
            end if;

            if E.Kind = RT_To_RT
              and then (E.Address_2 > RT_Addr_Max
                        or else E.Address_2 = E.Address
                        or else not Sub_Is_Data (E.Sub_2))
            then
               return False;
            end if;

            if E.Kind = Mode_Command then
               --  LLR-1553-BC-006: reject an unlisted mode code, or a data word presence/direction that disagrees with the table
               declare
                  M : constant Bc_Mode_Entry := Mode_Table (E.Mode);
               begin
                  if not M.Supported
                    or else E.Has_Data_Word /= M.Has_Data_Word
                    or else (M.Has_Data_Word and then E.Mode_Data_From_BC /= M.Data_From_BC)
                  then
                     return False;
                  end if;
                  --  a broadcast mode command cannot ask an RT to return a data word
                  if E.Address = RT_Addr_Broadcast and then M.Has_Data_Word and then not M.Data_From_BC then
                     return False;
                  end if;
               end;
            else
               --  every non-mode format carries a real subaddress and 1 .. 32 data words
               if not Sub_Is_Data (E.Sub) or else E.Word_Count = 0 then
                  return False;
               end if;
            end if;

            if Long_Long_Integer (E.Slot_Offset_Us) + Long_Long_Integer (Duration)
               > Long_Long_Integer (S.Period_Us)
            then
               return False;
            end if;

            --  overlap against every other entry of the same minor frame
            for J in 0 .. S.Entry_Count - 1 loop
               if J /= I and then S.Entries (J).Minor_Frame_Index = E.Minor_Frame_Index then
                  declare
                     Other_Duration : constant Natural := Message_Duration_Us (S.Entries (J));
                     E_After_Other  : constant Boolean :=
                       Long_Long_Integer (E.Slot_Offset_Us)
                       >= Long_Long_Integer (S.Entries (J).Slot_Offset_Us) + Long_Long_Integer (Other_Duration);
                     Other_After_E  : constant Boolean :=
                       Long_Long_Integer (S.Entries (J).Slot_Offset_Us)
                       >= Long_Long_Integer (E.Slot_Offset_Us) + Long_Long_Integer (Duration);
                  begin
                     if not E_After_Other and then not Other_After_E then
                        return False;                     --  overlap
                     end if;
                  end;
               end if;
            end loop;
         end;
      end loop;

      return True;
   end Is_Valid;

   --  lifecycle

   Empty_Sched : constant Schedule :=
     (Entries => (others => <>), Entry_Count => 0, Minor_Frame_Count => 0, Period_Us => 0);

   --  implements: LLR-1553-BC-001, LLR-1553-REDUN-001, LLR-1553-REDUN-002
   procedure Init
     (Ctx             : out Bc_Context;
      Sched           : Schedule;
      Initial_Primary : Channel;
      Fail_Threshold  : Byte;
      Ok              : out Boolean)
   is
   begin
      Ctx := (Sched => Empty_Sched, Log => (others => <>), others => <>);   --  BC_Idle, nothing latched, empty log
      Ok  := Is_Valid (Sched);

      if Ok then
         Ctx.Sched               := Sched;
         Ctx.Primary_Bus         := Initial_Primary;
         Ctx.Current_Message_Bus := Initial_Primary;
         Ctx.Fail_Threshold      :=
           (if Fail_Threshold = 0 then Redun_Fail_Threshold_Default else Positive (Fail_Threshold));
      end if;
   end Init;

   --  implements: LLR-1553-BIT-005, LLR-1553-BIT-006
   procedure Attach_Cbit (Ctx : in out Bc_Context) is
   begin
      Ctx.Cbit_Attached := True;
   end Attach_Cbit;

   procedure Detach_Cbit (Ctx : in out Bc_Context) is
   begin
      Ctx.Cbit_Attached := False;
   end Detach_Cbit;

   --  implements: LLR-1553-BC-010
   procedure Start (Ctx : in out Bc_Context; Ok : out Boolean) is
   begin
      Ok := Ctx.State = BC_Idle;
      if Ok then
         Ctx.State := BC_Major_Frame;
      else
         Ctx.Ignored_Event_Count := Ctx.Ignored_Event_Count + 1;   --  start outside IDLE: ignored and counted
      end if;
   end Start;

   --  implements: LLR-1553-BC-010
   procedure Stop (Ctx : in out Bc_Context) is
   begin
      Ctx.State := BC_Idle;
   end Stop;

   --  helpers used by Run_Frame (the subunit)

   --  implements: LLR-1553-ERR-004 (BC side); discards the NEWEST record when full
   procedure Log_Error
     (Ctx   : in out Bc_Context;
      Chan  : Channel;
      Kind  : Error_Kind;
      Index : Message_Id;
      Now   : Time_Us)
   with Global => null,
        Post   =>
          Ctx.Log_Used >= Ctx'Old.Log_Used
          and then (for all I in 0 .. Ctx'Old.Log_Used - 1 => Ctx.Log (I) = Ctx'Old.Log (I))
          and then Ctx.State = Ctx'Old.State
          and then Ctx.Sched = Ctx'Old.Sched
          and then Ctx.Cbit_Attached = Ctx'Old.Cbit_Attached
   is
   begin
      if Ctx.Log_Used < Error_Log_Capacity then
         Ctx.Log (Ctx.Log_Used) :=
           (Timestamp => Now, Terminal => BC, Chan => Chan, Kind => Kind, Message => Index);
         Ctx.Log_Used := Ctx.Log_Used + 1;
      else
         Ctx.Log_Overflow := Ctx.Log_Overflow + 1;
      end if;
   end Log_Error;

   --  puts one word on a channel and lets the word time pass
   procedure Transmit_Word
     (Bus : in out Bus_Model_Types.Context; Chan : Channel; Syms : Symbols)
   with Global => null
   is
      Sent : Boolean;
   begin
      Bus_Model.Transmit (Bus, Chan, Syms, BC, Sent);
      Bus_Model.Advance_Us (Bus, Word_Time_Us);
      --  Sent is not used: a failed channel still costs the BC the word time (as in the C); the no-response timeout is what reports the silence
   end Transmit_Word;

   --  implements: LLR-1553-BC-008
   --  no command word earlier than T_Gap_Min_Us after the end of the previous message (or after its no-response timeout expiry, which is what
   --  Last_Activity_End holds after a timeout); the BC idles the gap out
   procedure Wait_Gap (Ctx : Bc_Context; Bus : in out Bus_Model_Types.Context)
   with Global => null
   is
   begin
      if Ctx.Message_Ever_Sent then
         declare
            Earliest : constant Time_Us := Ctx.Last_Activity_End + T_Gap_Min_Us;
            Now      : constant Time_Us := Bus_Model.Now_Us (Bus);
         begin
            if Now < Earliest then
               Bus_Model.Advance_Us (Bus, Earliest - Now);
            end if;
         end;
      end if;
   end Wait_Gap;

   --  implements: LLR-1553-BC-009, LLR-1553-REDUN-001
   --  words still unread on either channel when a new command is about to go out belong to an earlier, finished or aborted transaction; they must not be mistaken
   --  for the response to the new command, so they are discarded first (bounded by the ring size); only the BC's own cursors move
   procedure Discard_Stale_Words (Ctx : in out Bc_Context; Bus : in out Bus_Model_Types.Context)
   with Global => null,
        Post   => Ctx.Sched = Ctx'Old.Sched
                  and then Ctx.State = Ctx'Old.State
                  and then Ctx.Log_Used = Ctx'Old.Log_Used
                  and then (for all C in Channel =>
                              (for all T in Terminal_Id =>
                                 (if T /= BC then
                                    Bus.Read_Count (C, T) = Bus'Old.Read_Count (C, T)
                                    and then Bus.Loopback_Count (C, T) = Bus'Old.Loopback_Count (C, T))))
   is
      Scratch : Bus_Model_Types.Bus_Record;
      Got     : Boolean;
   begin
      for Ch in Channel loop
         for N in 1 .. Bus_Model_Types.Queue_Capacity loop
            Bus_Model.Receive (Bus, Ch, BC, Scratch, Got);
            exit when not Got;
            Ctx.Ignored_Event_Count := Ctx.Ignored_Event_Count + 1;   --  unsolicited word ignored and counted, BC-010
         end loop;
      end loop;
   end Discard_Stale_Words;

   --  message issuance, LLR-1553-BC-003 .. LLR-1553-BC-007

   --  implements: LLR-1553-BC-003
   procedure Issue_BC_To_RT
     (Bus : in out Bus_Model_Types.Context; E : Schedule_Entry; Chan : Channel)
   with Global => null
   is
      Syms : Symbols := Zero_Symbols;
   begin
      Mil1553_Word.Pack_Command
        ((Address => E.Address, Direction => Receive, Sub => E.Sub, Word_Count_Raw => Wc_Field (E)), Syms);
      Transmit_Word (Bus, Chan, Syms);

      for I in 0 .. E.Word_Count - 1 loop                  --  placeholder data, see file header
         Mil1553_Word.Pack_Data (Data_Word (I), Syms);
         Transmit_Word (Bus, Chan, Syms);
      end loop;
   end Issue_BC_To_RT;

   --  implements: LLR-1553-BC-004
   procedure Issue_RT_To_BC
     (Bus : in out Bus_Model_Types.Context; E : Schedule_Entry; Chan : Channel)
   with Global => null
   is
      Syms : Symbols := Zero_Symbols;
   begin
      Mil1553_Word.Pack_Command
        ((Address => E.Address, Direction => Transmit, Sub => E.Sub, Word_Count_Raw => Wc_Field (E)), Syms);
      Transmit_Word (Bus, Chan, Syms);
   end Issue_RT_To_BC;

   --  implements: LLR-1553-BC-005; the receive and transmit commands go out back to back
   procedure Issue_RT_To_RT
     (Bus : in out Bus_Model_Types.Context; E : Schedule_Entry; Chan : Channel)
   with Global => null
   is
      Syms : Symbols := Zero_Symbols;
   begin
      Mil1553_Word.Pack_Command
        ((Address => E.Address, Direction => Receive, Sub => E.Sub, Word_Count_Raw => Wc_Field (E)), Syms);
      Transmit_Word (Bus, Chan, Syms);

      Mil1553_Word.Pack_Command
        ((Address => E.Address_2, Direction => Transmit, Sub => E.Sub_2, Word_Count_Raw => Wc_Field (E)), Syms);
      Transmit_Word (Bus, Chan, Syms);
   end Issue_RT_To_RT;

   --  implements: LLR-1553-BC-006
   procedure Issue_Mode_Command
     (Bus : in out Bus_Model_Types.Context; E : Schedule_Entry; Chan : Channel)
   with Global => null
   is
      Sends_Data : constant Boolean := E.Has_Data_Word and then E.Mode_Data_From_BC;
      Syms       : Symbols := Zero_Symbols;
   begin
      Mil1553_Word.Pack_Command
        ((Address        => E.Address,
          Direction      => (if Sends_Data then Receive else Transmit),
          Sub            => 0,                              --  mode indicator, 00000 convention (mil1553_notes.md 5)
          Word_Count_Raw => E.Mode),
         Syms);
      Transmit_Word (Bus, Chan, Syms);

      if Sends_Data then
         Mil1553_Word.Pack_Data (0, Syms);                  --  placeholder, see file header
         Transmit_Word (Bus, Chan, Syms);
      end if;
   end Issue_Mode_Command;

   --  implements: LLR-1553-BC-007
   procedure Issue_Broadcast
     (Bus : in out Bus_Model_Types.Context; E : Schedule_Entry; Chan : Channel)
   with Global => null
   is
      Syms : Symbols := Zero_Symbols;
   begin
      Mil1553_Word.Pack_Command
        ((Address => RT_Addr_Broadcast, Direction => Receive, Sub => E.Sub, Word_Count_Raw => Wc_Field (E)), Syms);
      Transmit_Word (Bus, Chan, Syms);

      for I in 0 .. E.Word_Count - 1 loop
         Mil1553_Word.Pack_Data (Data_Word (I), Syms);
         Transmit_Word (Bus, Chan, Syms);
      end loop;
   end Issue_Broadcast;

   procedure Issue_Message
     (Ctx  : in out Bc_Context;
      Bus  : in out Bus_Model_Types.Context;
      E    : Schedule_Entry;
      Chan : Channel)
   with Global => null,
        Post   => Ctx.Sched = Ctx'Old.Sched
                  and then Ctx.State = Ctx'Old.State
                  and then Ctx.Log_Used = Ctx'Old.Log_Used
   is
   begin
      Discard_Stale_Words (Ctx, Bus);

      case E.Kind is
         when BC_To_RT      => Issue_BC_To_RT (Bus, E, Chan);
         when RT_To_BC      => Issue_RT_To_BC (Bus, E, Chan);
         when RT_To_RT      => Issue_RT_To_RT (Bus, E, Chan);
         when Mode_Command  => Issue_Mode_Command (Bus, E, Chan);
         when Broadcast     => Issue_Broadcast (Bus, E, Chan);
      end case;
   end Issue_Message;

   --  in-flight tracking, LLR-1553-BC-003 .. BC-006
   procedure Begin_Response_Tracking (Ctx : in out Bc_Context; E : Schedule_Entry)
   with Global => null,
        Post   => Ctx.Sched = Ctx'Old.Sched
                  and then Ctx.State = Ctx'Old.State
                  and then Ctx.Log_Used = Ctx'Old.Log_Used
   is
   begin
      Ctx.Response_Stage          := 0;
      Ctx.Response_Words_Received := 0;
      Ctx.Response_Primary_RT     := E.Address;
      Ctx.Response_Is_RT_To_RT    := False;
      Ctx.Response_Words_Expected := 0;

      case E.Kind is
         when RT_To_BC =>
            Ctx.Response_Words_Expected := E.Word_Count;
         when RT_To_RT =>
            --  the transmitting RT's status arrives first (BC-005)
            Ctx.Response_Primary_RT     := E.Address_2;
            Ctx.Response_Secondary_RT   := E.Address;
            Ctx.Response_Is_RT_To_RT    := True;
            Ctx.Response_Words_Expected := E.Word_Count;
         when Mode_Command =>
            Ctx.Response_Words_Expected :=
              (if E.Has_Data_Word and then not E.Mode_Data_From_BC then 1 else 0);
         when BC_To_RT | Broadcast =>
            null;
      end case;
   end Begin_Response_Tracking;

   --  implements: LLR-1553-REDUN-003
   --  for RT-to-RT the stage at which the response stalled identifies the culprit: stage 2 means the transmitting RT answered (status + data) and only the
   --  receiving RT's closing status is missing; stage 0 or 1 means the transmitting RT stopped; blaming both would needlessly drop a healthy RT
   procedure Mark_Rt_Failed (Ctx : in out Bc_Context; E : Schedule_Entry)
   with Global => null,
        Post   => Ctx.Sched = Ctx'Old.Sched
                  and then Ctx.State = Ctx'Old.State
                  and then Ctx.Log_Used = Ctx'Old.Log_Used
   is
      Culprit : constant RT_Address :=
        (if E.Kind = RT_To_RT and then Ctx.Response_Stage /= 2 then E.Address_2 else E.Address);
   begin
      if Culprit <= RT_Addr_Max then
         Ctx.Rt_Failed_This_Frame (RT_Real_Address (Culprit)) := True;
      end if;
   end Mark_Rt_Failed;

   --  a message completed successfully: clear the consecutive failure count of the bus it ran on
   procedure Message_Succeeded (Ctx : in out Bc_Context)
   with Global => null,
        Post   => Ctx.Sched = Ctx'Old.Sched
                  and then Ctx.State = BC_Minor_Frame
                  and then Ctx.Log_Used = Ctx'Old.Log_Used
   is
   begin
      Ctx.Consecutive_Fails (Ctx.Current_Message_Bus) := 0;
      Ctx.State := BC_Minor_Frame;
   end Message_Succeeded;

   --  implements: LLR-1553-BC-001 .. LLR-1553-BC-010, LLR-1553-REDUN-001 .. LLR-1553-REDUN-003
   --  the state machine: the subunit src/ada/bc_scheduler-run_frame.adb
   procedure Run_Frame
     (Ctx    : in out Bc_Context;
      Bus    : in out Bus_Model_Types.Context;
      Cbit   : in out Bit_Cbit.Cbit_Context;
      Active : out Boolean) is separate;

end Bc_Scheduler;