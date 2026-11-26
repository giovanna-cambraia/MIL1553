--  project:    MIL1553

--  realizes:   LLR-1553-BC-001 .. LLR-1553-BC-010, LLR-1553-REDUN-001 .. LLR-1553-REDUN-003

--  purpose:    subunit of Bc_Scheduler: the BC state machine, one step per call (port of bc_run_frame in bc_scheduler.c)
--  proof status: NOT compiled, NOT proved (no GNAT available to the author)

separate (Bc_Scheduler)
procedure Run_Frame
  (Ctx    : in out Bc_Context;
   Bus    : in out Bus_Model_Types.Context;
   Cbit   : in out Bit_Cbit.Cbit_Context;
   Active : out Boolean)
is

   function To_Kind (Class : Word_Class) return Error_Kind
   is (case Class is
         when Sync_Error       => Err_Sync,
         when Manchester_Error => Err_Manchester,
         when others           => Err_Parity);

   --  implements: LLR-1553-BC-010 (MAJOR_FRAME): a new major frame starts, nothing issued, nothing failed
   procedure Step_Major_Frame (Ctx : in out Bc_Context; Bus : Bus_Model_Types.Context) is
   begin
      Ctx.Entry_Issued         := (others => False);
      Ctx.Rt_Failed_This_Frame := (others => False);       --  LLR-1553-REDUN-003
      Ctx.Current_Minor_Frame  := 0;
      Ctx.Overrun_Logged       := False;
      Ctx.Minor_Frame_Start    := Bus_Model.Now_Us (Bus);
      Ctx.State                := BC_Minor_Frame;
   end Step_Major_Frame;

   --  implements: LLR-1553-BC-010 (DEGRADED): continue on the remaining capability; Degraded_Indication stays sticky
   procedure Step_Degraded (Ctx : in out Bc_Context) is
   begin
      Ctx.State := BC_Major_Frame;
   end Step_Degraded;

   --  implements: LLR-1553-BC-001, BC-002, BC-007, BC-008
   --  issues the first eligible entry of the current minor frame (at most one per call); when none is left and the period has elapsed, moves to the next
   --  minor frame (or the next major frame)
   procedure Step_Minor_Frame (Ctx : in out Bc_Context; Bus : in out Bus_Model_Types.Context) is
      Elapsed : constant Time_Us := Bus_Model.Now_Us (Bus) - Ctx.Minor_Frame_Start;
      Issued  : Boolean := False;
   begin
      for I in 0 .. Ctx.Sched.Entry_Count - 1 loop
         declare
            E       : constant Schedule_Entry := Ctx.Sched.Entries (I);
            Rt_Down : constant Boolean :=
              E.Address <= RT_Addr_Max
              and then Ctx.Rt_Failed_This_Frame (RT_Real_Address (E.Address));
         begin
            if E.Minor_Frame_Index = Ctx.Current_Minor_Frame
              and then not Ctx.Entry_Issued (I)
              and then not Rt_Down
              and then Elapsed >= Time_Us (E.Slot_Offset_Us)
            then
               Ctx.Entry_Issued (I)     := True;
               Ctx.Snapshot_Index       := I;
               Ctx.Current_Message_Bus  := Ctx.Primary_Bus;
               Ctx.Retry_Used           := False;

               declare
                  Chan : constant Channel := Ctx.Primary_Bus;
               begin
                  Wait_Gap (Ctx, Bus);                          --  LLR-1553-BC-008
                  Issue_Message (Ctx, Bus, E, Chan);
               end;

               Ctx.Message_Ever_Sent   := True;
               Ctx.Message_Start_Time  := Bus_Model.Now_Us (Bus);
               Ctx.Last_Activity_End   := Ctx.Message_Start_Time;

               if not Entry_Is_Broadcast (E) then
                  Begin_Response_Tracking (Ctx, E);
                  Ctx.State := BC_Wait_Response;
               end if;
               --  a broadcast expects no status (BC-007): the message is complete, stay in MINOR_FRAME to scan for the next entry

               Issued := True;
               exit;
            end if;
         end;
      end loop;

      if not Issued and then Elapsed >= Time_Us (Ctx.Sched.Period_Us) then
         --  every eligible entry has been issued and the period has elapsed; a frame that ran late is detected while the message is in flight (BC-002)
         Ctx.Overrun_Logged := False;
         if Ctx.Current_Minor_Frame < Max_Minor_Frames then
            Ctx.Current_Minor_Frame := Ctx.Current_Minor_Frame + 1;
         end if;
         if Ctx.Current_Minor_Frame >= Ctx.Sched.Minor_Frame_Count then
            Ctx.State := BC_Major_Frame;
         else
            Ctx.Minor_Frame_Start := Bus_Model.Now_Us (Bus);
         end if;
      end if;
      --  else: not yet time for the next entry nor the frame end; wait for the next call
   end Step_Minor_Frame;

   --  implements: LLR-1553-BC-003 .. BC-006, BC-009, ERR-001 .. ERR-003, BIT-005
   procedure Step_Wait_Response
     (Ctx : in out Bc_Context; Bus : in out Bus_Model_Types.Context; Cbit : in out Bit_Cbit.Cbit_Context)
   is
      Chan  : constant Channel := Ctx.Current_Message_Bus;
      Rec   : Bus_Model_Types.Bus_Record;
      Got   : Boolean;
   begin
      Bus_Model.Receive (Bus, Chan, BC, Rec, Got);

      declare
         Now   : constant Time_Us    := Bus_Model.Now_Us (Bus);
         Index : constant Message_Id := Message_Id (Ctx.Snapshot_Index);

         procedure Log (Kind : Error_Kind) is
         begin
            Log_Error (Ctx, Chan, Kind, Index, Now);
         end Log;
      begin
         if Got then
            declare
               Decoded  : constant Mil1553_Word.Decoded_Word := Mil1553_Word.Classify (Rec.Syms);
               Cls      : constant Word_Class := Decoded.Class;
               Word_End : constant Time_Us := Rec.Timestamp + Word_Time_Us;
            begin
               --  BC-009: the timeout window restarts from the end of each word actually received
               if Word_End > Ctx.Last_Activity_End then
                  Ctx.Last_Activity_End := Word_End;
               end if;

               if Ctx.Cbit_Attached then
                  Bit_Cbit.Record_Word (Cbit, Chan, Cls);                  --  BIT-005
                  if Cls = Valid and then Ctx.Response_Stage = 0
                    and then Rec.Timestamp >= Ctx.Message_Start_Time
                  then
                     Bit_Cbit.Record_Response_Time (Cbit, Rec.Timestamp - Ctx.Message_Start_Time);
                  end if;
               end if;

               if Cls /= Valid then
                  Log (To_Kind (Cls));
                  Ctx.State := BC_Timeout;

               elsif (Ctx.Response_Stage = 1 and then Decoded.Sync /= Data)
                 or else (Ctx.Response_Stage /= 1 and then Decoded.Sync /= Command_Status)
               then
                  --  valid word of the wrong kind for this stage (e.g. data where a status was expected)
                  Log (Err_Sync);
                  Ctx.State := BC_Timeout;

               elsif Ctx.Response_Stage = 0 then
                  --  a status word from the primary RT
                  declare
                     Status : constant Status_Fields := Mil1553_Word.Unpack_Status (Decoded.Field);
                  begin
                     if Status.Address /= Ctx.Response_Primary_RT then
                        Log (Err_Status_Address_Mismatch);
                        Ctx.State := BC_Timeout;
                     elsif Status.Message_Error then
                        Log (Err_Status_ME);
                        Ctx.State := BC_Timeout;
                     else
                        --  BUSY, SF and TF are reported but do not trigger a retry (only ME and a wrong address do)
                        if Status.Busy then
                           Log (Err_Status_Busy);
                        end if;
                        if Status.Subsystem_Flag then
                           Log (Err_Status_SF);
                        end if;
                        if Status.Terminal_Flag then
                           Log (Err_Status_TF);
                        end if;

                        if Ctx.Response_Words_Expected > 0 then
                           Ctx.Response_Stage := 1;
                        elsif Ctx.Response_Is_RT_To_RT then
                           Ctx.Response_Stage := 2;            --  zero-length transfer still owes the second status
                        else
                           Message_Succeeded (Ctx);
                        end if;
                     end if;
                  end;

               elsif Ctx.Response_Stage = 1 then
                  --  a data word
                  if Ctx.Response_Words_Received < Max_Data_Words then
                     Ctx.Response_Words_Received := Ctx.Response_Words_Received + 1;
                  end if;
                  if Ctx.Response_Words_Received >= Ctx.Response_Words_Expected then
                     if Ctx.Response_Is_RT_To_RT then
                        Ctx.Response_Stage := 2;
                     else
                        Message_Succeeded (Ctx);
                     end if;
                  end if;
                  --  else: remain in stage 1, awaiting more data words

               else
                  --  stage 2: RT-to-RT secondary status, from the receiving RT
                  declare
                     Status2 : constant Status_Fields := Mil1553_Word.Unpack_Status (Decoded.Field);
                  begin
                     if Status2.Address /= Ctx.Response_Secondary_RT then
                        Log (Err_Status_Address_Mismatch);
                        Ctx.State := BC_Timeout;
                     elsif Status2.Message_Error then
                        Log (Err_Status_ME);
                        Ctx.State := BC_Timeout;
                     else
                        Message_Succeeded (Ctx);
                     end if;
                  end;
               end if;
            end;

         elsif Now - Ctx.Last_Activity_End >= T_No_Response_Us then
            --  ERR-003: a stall in the middle of the data phase means fewer valid data words arrived than the command promised; a stall before any
            --  status is a plain no-response
            declare
               Short_Data : constant Boolean :=
                 Ctx.Response_Stage = 1 and then Ctx.Response_Words_Received < Ctx.Response_Words_Expected;
            begin
               Log (if Short_Data then Err_Word_Count_Mismatch else Err_No_Response);
               --  the gap (BC-008) runs from the timeout expiry
               Ctx.Last_Activity_End := Ctx.Last_Activity_End + T_No_Response_Us;
               Ctx.State := BC_Timeout;
            end;
         end if;
         --  else: still inside the response window, keep waiting
      end;
   end Step_Wait_Response;

   --  implements: LLR-1553-REDUN-001 .. REDUN-003
   procedure Step_Timeout (Ctx : in out Bc_Context; Bus : Bus_Model_Types.Context) is
      Chan : constant Channel := Ctx.Current_Message_Bus;
      E    : constant Schedule_Entry := Ctx.Sched.Entries (Ctx.Snapshot_Index);
   begin
      if Ctx.Consecutive_Fails (Chan) < 255 then
         Ctx.Consecutive_Fails (Chan) := Ctx.Consecutive_Fails (Chan) + 1;
      end if;

      if Ctx.Consecutive_Fails (Chan) >= Ctx.Fail_Threshold then
         --  REDUN-002: switch the primary bus
         Ctx.Primary_Bus          := Alternate (Ctx.Primary_Bus);
         Ctx.Consecutive_Fails    := (others => 0);
         Ctx.Degraded_Indication  := True;
         Ctx.Retry_Used           := False;
         Ctx.State                := BC_Degraded;

      elsif not Ctx.Retry_Used
        and then not Bus_Model.Is_Channel_Failed (Bus, Alternate (Chan))
      then
         Ctx.State := BC_Failover;

      else
         --  REDUN-003
         Mark_Rt_Failed (Ctx, E);
         Ctx.Retry_Used := False;
         Ctx.State      := BC_Minor_Frame;
      end if;
   end Step_Timeout;

   --  implements: LLR-1553-REDUN-001: the one retry, on the other channel
   procedure Step_Failover (Ctx : in out Bc_Context; Bus : in out Bus_Model_Types.Context) is
      E    : constant Schedule_Entry := Ctx.Sched.Entries (Ctx.Snapshot_Index);
      Chan : constant Channel := Alternate (Ctx.Current_Message_Bus);
   begin
      Ctx.Retry_Used          := True;
      Ctx.Current_Message_Bus := Chan;

      Wait_Gap (Ctx, Bus);                                  --  BC-008 also applies to the retry
      Issue_Message (Ctx, Bus, E, Chan);
      Ctx.Message_Start_Time := Bus_Model.Now_Us (Bus);
      Ctx.Last_Activity_End  := Ctx.Message_Start_Time;
      Begin_Response_Tracking (Ctx, E);

      Ctx.State := BC_Wait_Response;
   end Step_Failover;

begin
   Active := Ctx.State /= BC_Idle;

   if not Active then
      return;
   end if;

   if Ctx.Cbit_Attached then
      Bit_Cbit.Tick (Cbit, Bus_Model.Now_Us (Bus));              --  tick first (detects a stall), then kick (this call is progress), BIT-006
      Bit_Cbit.Watchdog_Kick (Cbit, Bus_Model.Now_Us (Bus));
   end if;

   --  LLR-1553-BC-002: a message still in flight when its minor frame's period has elapsed means the frame overran; raised once per minor frame
   if Ctx.State in BC_Wait_Response | BC_Timeout | BC_Failover
     and then not Ctx.Overrun_Logged
     and then Bus_Model.Now_Us (Bus) - Ctx.Minor_Frame_Start >= Time_Us (Ctx.Sched.Period_Us)
   then
      Log_Error (Ctx, Ctx.Primary_Bus, Err_Minor_Frame_Overrun,
                 Message_Id (Ctx.Snapshot_Index), Bus_Model.Now_Us (Bus));
      Ctx.Overrun_Logged := True;
   end if;

   case Ctx.State is
      when BC_Major_Frame  => Step_Major_Frame (Ctx, Bus);
      when BC_Degraded     => Step_Degraded (Ctx);
      when BC_Minor_Frame  => Step_Minor_Frame (Ctx, Bus);
      when BC_Wait_Response => Step_Wait_Response (Ctx, Bus, Cbit);
      when BC_Timeout      => Step_Timeout (Ctx, Bus);
      when BC_Failover     => Step_Failover (Ctx, Bus);
      when BC_Idle         => null;
   end case;
end Run_Frame;