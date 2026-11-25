--  project:    MIL1553

--  realizes:   LLR-1553-RT-001 .. LLR-1553-RT-013

--  purpose:    subunit of Rt_Service: the RT state machine, one step per call (port of rt_run in rt_service.c)
--  proof status: NOT compiled, NOT proved (no GNAT available to the author)

separate (Rt_Service)
procedure Run
  (Ctx    : in out Rt_Context;
   Bus    : in out Bus_Model_Types.Context;
   Cbit   : in out Bit_Cbit.Cbit_Context;
   Active : out Boolean)
is

   function To_Kind (Class : Word_Class) return Error_Kind
   is (case Class is
         when Sync_Error       => Err_Sync,
         when Manchester_Error => Err_Manchester,
         when others           => Err_Parity);

   --  WAIT_COMMAND / FAIL_SAFE: handle one word that has already been read
   --  implements: LLR-1553-RT-001, RT-006, RT-009, RT-010, RT-013
   procedure Handle_Command_Word
     (Ctx : in out Rt_Context;
      Bus : Bus_Model_Types.Context;
      Cbit : in out Bit_Cbit.Cbit_Context;
      Ch  : Channel;
      Rec : Bus_Model_Types.Bus_Record)
   is
      Decoded : constant Mil1553_Word.Decoded_Word := Mil1553_Word.Classify (Rec.Syms);
      Cls     : constant Word_Class := Decoded.Class;
      Cmd     : constant Command_Fields := Mil1553_Word.Unpack_Command (Decoded.Field);
      Was_Fail_Safe : constant Boolean := Ctx.State = RT_Fail_Safe;

      --  a VALID data-sync word is never a command candidate (IBIT patterns of
      --  other terminals); a corrupted sync still takes the error path
      Candidate : constant Boolean := Cls /= Valid or else Decoded.Sync = Command_Status;
      To_Me     : constant Boolean :=
        Candidate
        and then (Cmd.Address = Ctx.Own_Address or else Cmd.Address = RT_Addr_Broadcast);
   begin
      if Ctx.Cbit_Attached then
         Bit_Cbit.Record_Word (Cbit, Ch, Cls);
      end if;

      if not To_Me then
         return;                                  --  silently ignored, RT-001
      end if;

      Ctx.Pending_Channel      := Ch;
      Ctx.Pending_Cmd          := Cmd;
      Ctx.Pending_Last_Word_End := Rec.Timestamp + Word_Time_Us;

      if Cls = Valid then
         Ctx.Last_Command   := Ctx.Latest_Command;     --  mode 18 returns the one before this
         Ctx.Latest_Command := Decoded.Field;
      end if;

      Ctx.Pending_Is_Broadcast := Cmd.Address = RT_Addr_Broadcast;
      Ctx.Pending_Is_Mode      := Cmd.Sub = 0 or else Cmd.Sub = 31;
      Ctx.Pending_Is_RT_To_RT  := False;
      Ctx.Pending_Status_Pending := False;

      --  RT-006: ME/BCR clear on the next valid command, except for transmit-status
      --  and transmit-last-command, which must report the status as it stands
      declare
         Handler : constant Mode_Handler := Mode_Table (Cmd.Word_Count_Raw).Handler;
         Preserves : constant Boolean :=
           Ctx.Pending_Is_Mode and then Handler in Mode_Tx_Status | Mode_Tx_Last_Command;
      begin
         if Cls = Valid and then not Preserves then
            Ctx.Message_Error_Latched := False;
            if not Ctx.Pending_Is_Broadcast then
               Ctx.Broadcast_Received_Latched := False;
            end if;
         end if;
      end;

      if Cls /= Valid then
         --  RT-010
         Ctx.Pending_Transmission_Err := True;
         Log_Error (Ctx, Ch, To_Kind (Cls), Bus_Model.Now_Us (Bus));
         Ctx.State := (if Was_Fail_Safe then RT_Fail_Safe else RT_Error);
      else
         Ctx.Pending_Transmission_Err := False;
         if Was_Fail_Safe then
            --  RT-013: TF-only status, nothing executed except a reset
            Ctx.State                := RT_Respond;
            Ctx.Message_End_Time     := Ctx.Pending_Last_Word_End;
            Ctx.Pending_Data_To_Send := 0;
            if Ctx.Pending_Is_Mode
              and then Mode_Table (Cmd.Word_Count_Raw).Handler = Mode_Reset
            then
               Ctx.Reset_Pending := True;
            end if;
         else
            Ctx.State := RT_Validate;
         end if;
      end if;
   end Handle_Command_Word;

   procedure Step_Wait_Command
     (Ctx : in out Rt_Context; Bus : in out Bus_Model_Types.Context; Cbit : in out Bit_Cbit.Cbit_Context)
   is
      Rec : Bus_Model_Types.Bus_Record;
      Got : Boolean;
   begin
      --  both channels are polled, A first; the command's channel becomes the response channel
      for Ch in Channel loop
         Next_Word (Ctx, Bus, Ch, Rec, Got);
         if Got then
            Handle_Command_Word (Ctx, Bus, Cbit, Ch, Rec);
            exit;
         end if;
      end loop;
   end Step_Wait_Command;

   --  implements: LLR-1553-RT-002, RT-008, RT-009, RT-011, RT-012
   procedure Step_Validate (Ctx : in out Rt_Context) is
   begin
      if Ctx.Pending_Transmission_Err then
         Ctx.State := RT_Error;                  --  defensive, as in the C
      elsif Ctx.Subsystem_Busy then
         Ctx.State := RT_Busy;                   --  RT-012: busy takes precedence
      elsif Ctx.Pending_Is_Broadcast and then not Ctx.Pending_Is_Mode
        and then Ctx.Pending_Cmd.Direction = Transmit
      then
         Ctx.Pending_Validation := Illegal_Direction;     --  a broadcast cannot ask for data
         Ctx.State := RT_Error;
      elsif Ctx.Pending_Is_Mode then
         declare
            M : constant Mode_Entry := Mode_Table (Ctx.Pending_Cmd.Word_Count_Raw);
         begin
            if not M.Supported then
               Ctx.Pending_Validation := Illegal_Subaddress;
               Ctx.State := RT_Error;
            elsif M.Has_Data_Word and then M.Data_From_BC then
               Ctx.Pending_Data_Expected := 1;
               Ctx.Pending_Data_Received := 0;
               Ctx.State := RT_Execute;
            else
               Ctx.Pending_Data_Expected := 0;
               Ctx.Pending_Data_To_Send  := (if M.Has_Data_Word and then not M.Data_From_BC then 1 else 0);
               Execute_Mode (Ctx, 0);
               Ctx.State := RT_Execute;
            end if;
         end;
      else
         declare
            Cfg   : constant Subaddr_Config := Ctx.Configs (Ctx.Pending_Cmd.Sub);
            Count : constant Valid_Data_Word_Count :=
              Mil1553_Word.Decode_Word_Count (Ctx.Pending_Cmd.Word_Count_Raw);
            Direction_Ok : constant Boolean :=
              (if Ctx.Pending_Cmd.Direction = Receive then Cfg.Allow_Receive else Cfg.Allow_Transmit);
         begin
            if not Cfg.Enabled then
               Ctx.Pending_Validation := Illegal_Subaddress;
               Ctx.State := RT_Error;
            elsif not Direction_Ok then
               Ctx.Pending_Validation := Illegal_Direction;
               Ctx.State := RT_Error;
            elsif Count < Cfg.Min_Word_Count or else Count > Cfg.Max_Word_Count then
               Ctx.Pending_Validation := Illegal_Word_Count;
               Ctx.State := RT_Error;
            else
               Ctx.Pending_Validation := Command_Valid;
               if Ctx.Pending_Cmd.Direction = Receive then
                  Ctx.Pending_Data_Expected := Count;
                  Ctx.Pending_Data_Received := 0;
                  Ctx.Pending_Data_To_Send  := 0;
               else
                  Ctx.Pending_Data_Expected := 0;
                  Ctx.Pending_Data_To_Send  := Count;
               end if;
               Ctx.State := RT_Execute;
            end if;
         end;
      end if;
   end Step_Validate;

   --  implements: LLR-1553-RT-003, RT-007, RT-010
   procedure Step_Execute
     (Ctx : in out Rt_Context; Bus : in out Bus_Model_Types.Context; Cbit : in out Bit_Cbit.Cbit_Context)
   is
      Chan : constant Channel := Ctx.Pending_Channel;
      Rec  : Bus_Model_Types.Bus_Record;
      Got  : Boolean;
      Now  : Time_Us;
   begin
      if Ctx.Pending_Data_Expected <= Ctx.Pending_Data_Received then
         Ctx.Message_End_Time := Ctx.Pending_Last_Word_End;
         Ctx.State := RT_Respond;
         return;
      end if;

      Next_Word (Ctx, Bus, Chan, Rec, Got);
      Now := Bus_Model.Now_Us (Bus);

      if Got then
         declare
            Decoded : constant Mil1553_Word.Decoded_Word := Mil1553_Word.Classify (Rec.Syms);
            Cls     : constant Word_Class := Decoded.Class;
         begin
            if Ctx.Cbit_Attached then
               Bit_Cbit.Record_Word (Cbit, Chan, Cls);
            end if;

            Ctx.Pending_Last_Word_End := Rec.Timestamp + Word_Time_Us;

            if Cls /= Valid then
               Fail_Message (Ctx, Cbit, Now, To_Kind (Cls));      --  any word error discards the message

            elsif Ctx.Pending_Is_RT_To_RT and then Ctx.Pending_Status_Pending then
               --  RT-007: the next word must be the transmitting RT's status
               declare
                  Xmit : constant Status_Fields := Mil1553_Word.Unpack_Status (Decoded.Field);
               begin
                  if Decoded.Sync /= Command_Status then
                     Fail_Message (Ctx, Cbit, Now, Err_Word_Count_Mismatch);
                  elsif Xmit.Address /= Ctx.Pending_Xmit_Address then
                     Fail_Message (Ctx, Cbit, Now, Err_Status_Address_Mismatch);
                  elsif Xmit.Message_Error then
                     Fail_Message (Ctx, Cbit, Now, Err_Status_ME);
                  else
                     Ctx.Pending_Status_Pending := False;
                  end if;
               end;

            elsif Decoded.Sync = Command_Status then
               --  command-sync word where data was expected: legitimate only as the
               --  paired transmit command of an RT-to-RT message, else the message is short
               declare
                  Paired : constant Command_Fields := Mil1553_Word.Unpack_Command (Decoded.Field);
               begin
                  if Ctx.Pending_Data_Received = 0
                    and then not Ctx.Pending_Is_Mode
                    and then not Ctx.Pending_Is_Broadcast
                    and then not Ctx.Pending_Is_RT_To_RT
                    and then Paired.Direction = Transmit
                    and then Paired.Address <= RT_Addr_Max
                    and then Paired.Address /= Ctx.Own_Address
                  then
                     Ctx.Pending_Is_RT_To_RT    := True;
                     Ctx.Pending_Status_Pending := True;
                     Ctx.Pending_Xmit_Address   := Paired.Address;
                     Ctx.Pending_Wait_Start     := Now;
                  else
                     Fail_Message (Ctx, Cbit, Now, Err_Word_Count_Mismatch);
                     Replay_Word (Ctx, Chan, Rec);      --  the new command gets its own turn
                  end if;
               end;

            else
               --  data word: staged, committed only when the response goes out (RT-003)
               if Ctx.Pending_Data_Received < Max_Data_Words then
                  Ctx.Staging (Ctx.Pending_Data_Received) := Mil1553_Word.Unpack_Data (Decoded.Field);
                  Ctx.Pending_Data_Received := Ctx.Pending_Data_Received + 1;
               end if;

               if Ctx.Pending_Data_Received = Ctx.Pending_Data_Expected then
                  Ctx.Commit_Pending   := True;
                  Ctx.Message_End_Time := Ctx.Pending_Last_Word_End;
                  Ctx.State := RT_Respond;
               end if;
            end if;
         end;

      elsif Ctx.Pending_Is_RT_To_RT and then Ctx.Pending_Status_Pending
        and then Now - Ctx.Pending_Wait_Start >= T_No_Response_Us
      then
         Fail_Message (Ctx, Cbit, Now, Err_No_Response);       --  transmitting RT never answered

      elsif not Ctx.Pending_Status_Pending
        and then Now - Ctx.Pending_Last_Word_End >= T_Gap_Min_Us
      then
         Fail_Message (Ctx, Cbit, Now, Err_Word_Count_Mismatch);   --  gap with words still owed
      end if;
   end Step_Execute;

   --  implements: LLR-1553-RT-003, RT-005, RT-006, RT-009
   procedure Step_Respond
     (Ctx : in out Rt_Context; Bus : in out Bus_Model_Types.Context; Cbit : in out Bit_Cbit.Cbit_Context)
   is
      Chan : constant Channel := Ctx.Pending_Channel;
      Extra : Bus_Model_Types.Bus_Record;
      Got   : Boolean;
   begin
      --  an extra contiguous word is a count error; a later one belongs to the next message
      if Ctx.Commit_Pending and then not Ctx.Replay_Valid then
         Next_Word (Ctx, Bus, Chan, Extra, Got);
         if Got then
            if Extra.Timestamp < Ctx.Pending_Last_Word_End + T_Gap_Min_Us then
               Fail_Message (Ctx, Cbit, Bus_Model.Now_Us (Bus), Err_Word_Count_Mismatch);
               return;
            end if;
            Replay_Word (Ctx, Chan, Extra);
         end if;
      end if;

      if Bus_Model.Now_Us (Bus) - Ctx.Message_End_Time < Time_Us (Ctx.Delay_Us) then
         return;                                   --  still inside the response delay
      end if;

      if Ctx.Commit_Pending then
         Commit_Received (Ctx);
      end if;

      if Ctx.Pending_Is_Broadcast then
         Ctx.Broadcast_Received_Latched := True;   --  never answered, RT-009
      else
         Build_And_Send_Status (Ctx, Bus, False, Ctx.Pending_Data_To_Send);
      end if;
      Finish_Message (Ctx);
   end Step_Respond;

   --  implements: LLR-1553-RT-010 (transmission error), RT-011 (illegal command)
   procedure Step_Error (Ctx : in out Rt_Context; Bus : in out Bus_Model_Types.Context) is
   begin
      if Ctx.Pending_Transmission_Err then
         Ctx.Message_Error_Latched    := True;     --  transmit nothing
         Ctx.Pending_Transmission_Err := False;
         Finish_Message (Ctx);
      else
         if Ctx.Message_End_Time = 0 then
            Ctx.Message_End_Time := Ctx.Pending_Last_Word_End;
         end if;
         if Bus_Model.Now_Us (Bus) - Ctx.Message_End_Time >= Time_Us (Ctx.Delay_Us) then
            Ctx.Message_Error_Latched := True;
            if not Ctx.Pending_Is_Broadcast then
               Build_And_Send_Status (Ctx, Bus, True, 0);
            end if;
            Finish_Message (Ctx);
         end if;
      end if;
   end Step_Error;

   --  implements: LLR-1553-RT-012
   procedure Step_Busy (Ctx : in out Rt_Context; Bus : in out Bus_Model_Types.Context) is
   begin
      if Ctx.Message_End_Time = 0 then
         Ctx.Message_End_Time := Ctx.Pending_Last_Word_End;
      end if;
      if Bus_Model.Now_Us (Bus) - Ctx.Message_End_Time >= Time_Us (Ctx.Delay_Us) then
         if not Ctx.Pending_Is_Broadcast then
            Build_And_Send_Status (Ctx, Bus, True, 0);
         end if;
         Finish_Message (Ctx);
      end if;
   end Step_Busy;

begin
   Active := Ctx.State not in RT_Reset | RT_Idle;

   if not Active then
      return;
   end if;

   if Ctx.Cbit_Attached then
      Bit_Cbit.Tick (Cbit, Bus_Model.Now_Us (Bus));              --  tick first, then kick (BIT-006)
      Bit_Cbit.Watchdog_Kick (Cbit, Bus_Model.Now_Us (Bus));
   end if;

   case Ctx.State is
      when RT_Wait_Command | RT_Fail_Safe => Step_Wait_Command (Ctx, Bus, Cbit);
      when RT_Validate                    => Step_Validate (Ctx);
      when RT_Execute                     => Step_Execute (Ctx, Bus, Cbit);
      when RT_Respond                     => Step_Respond (Ctx, Bus, Cbit);
      when RT_Error                       => Step_Error (Ctx, Bus);
      when RT_Busy                        => Step_Busy (Ctx, Bus);
      when RT_Reset | RT_Idle             => null;
   end case;
end Run;