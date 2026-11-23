--  project:  MIL1553

--  realizes: LLR-1553-RT-001 .. LLR-1553-RT-013

--  structure: this file is the parent body - lifecycle operations, the mode command table and its handlers, and the helper routines; the state machine,
--  Run, is the subunit src/ada/rt_service-run.adb; subunits see everything declared here before the stub, which is how Run uses the helpers

--  proof status: written to be proved with GNATprove, NOT yet run (no GNAT in the author's environment); the helpers mostly carry framing postconditions that
--  Run's contract is built from; expect to tune them on the first run

with Bus_Model;
with Mil1553_Word;

package body Rt_Service
  with SPARK_Mode => On
is

   --  mode command table, per design/mode_code_table.md (LLR-1553-RT-008)
   type Mode_Handler is
     (Mode_None,                 --  recognised but not supported
      Mode_Sync,                 --  code 1
      Mode_Tx_Status,            --  code 2   the status word is the response
      Mode_Self_Test,            --  code 3
      Mode_Tx_Shutdown,          --  code 4
      Mode_Override_Shutdown,    --  code 5
      Mode_Inhibit_Tf,           --  code 6
      Mode_Override_Inhibit_Tf,  --  code 7
      Mode_Reset,                --  code 8
      Mode_Tx_Vector,            --  code 16
      Mode_Sync_Data,            --  code 17
      Mode_Tx_Last_Command,      --  code 18
      Mode_Tx_Bit);              --  code 19

   type Mode_Entry is record
      Supported     : Boolean;
      Has_Data_Word : Boolean;
      Data_From_BC  : Boolean;     --  True: the BC supplies the word; False: the RT does
      Handler       : Mode_Handler;
   end record;

   Unsupported : constant Mode_Entry := (False, False, False, Mode_None);

   Mode_Table : constant array (Word_Count_Field) of Mode_Entry :=
     (1      => (True, False, False, Mode_Sync),
      2      => (True, False, False, Mode_Tx_Status),
      3      => (True, False, False, Mode_Self_Test),
      4      => (True, False, False, Mode_Tx_Shutdown),
      5      => (True, False, False, Mode_Override_Shutdown),
      6      => (True, False, False, Mode_Inhibit_Tf),
      7      => (True, False, False, Mode_Override_Inhibit_Tf),
      8      => (True, False, False, Mode_Reset),
      16     => (True, True,  False, Mode_Tx_Vector),
      17     => (True, True,  True,  Mode_Sync_Data),
      18     => (True, True,  False, Mode_Tx_Last_Command),
      19     => (True, True,  False, Mode_Tx_Bit),
      others => Unsupported);                 --  includes 0, Dynamic Bus Control

   --  lifecycle, LLR-1553-RT-013

   --  implements: LLR-1553-RT-001 .. RT-013
   procedure Init
     (Ctx         : out Rt_Context;
      Own_Address : RT_Address;
      Terminal    : Terminal_Id;
      Configs     : Subaddr_Configs;
      Delay_Us    : Natural;
      Ok          : out Boolean)
   is
   begin
      Ctx := (others => <>);       --  RT_Reset, nothing latched, empty log
      Ok  := Own_Address <= RT_Addr_Max and then Delay_Us in T_Resp_Min_Us .. T_Resp_Max_Us;

      if Ok then
         Ctx.Own_Address  := Own_Address;
         Ctx.Own_Terminal := Terminal;
         Ctx.Configs      := Configs;
         Ctx.Delay_Us     := Response_Delay (Delay_Us);
      end if;
   end Init;

   --  implements: LLR-1553-RT-013
   procedure Pbit_Complete (Ctx : in out Rt_Context; Passed : Boolean) is
   begin
      Ctx.Fail_Safe := not Passed;
      Ctx.State := (if Passed then RT_Idle else RT_Fail_Safe);
   end Pbit_Complete;

   --  implements: LLR-1553-RT-013
   procedure Enable (Ctx : in out Rt_Context; Ok : out Boolean) is
   begin
      Ok := Ctx.State = RT_Idle;
      if Ok then
         Ctx.State := RT_Wait_Command;
      end if;
   end Enable;

   --  implements: LLR-1553-RT-013
   procedure Reset (Ctx : in out Rt_Context) is
   begin
      Ctx.State                      := RT_Reset;
      Ctx.Message_Error_Latched      := False;
      Ctx.Broadcast_Received_Latched := False;
      Ctx.Terminal_Flag              := False;   --  PBIT re-evaluates it
      Ctx.Fail_Safe                  := False;
      Ctx.Reset_Pending              := False;
      Ctx.Tf_Inhibited               := False;
      Ctx.Tx_Shutdown                := (others => False);
      Ctx.Bit_Degraded               := False;
      Ctx.Bit_Word                   := 0;
      Ctx.Pending_Transmission_Err   := False;
      Ctx.Pending_Data_Expected      := 0;
      Ctx.Pending_Data_Received      := 0;
      Ctx.Pending_Data_To_Send       := 0;
      Ctx.Message_End_Time           := 0;
      Ctx.Commit_Pending             := False;
      Ctx.Replay_Valid               := False;
   end Reset;

   procedure Set_Busy (Ctx : in out Rt_Context; Busy : Boolean) is
   begin
      Ctx.Subsystem_Busy := Busy;
   end Set_Busy;

   procedure Set_Subsystem_Flag (Ctx : in out Rt_Context; Flag : Boolean) is
   begin
      Ctx.Subsystem_Flag := Flag;
   end Set_Subsystem_Flag;

   --  implements: LLR-1553-BIT-008
   procedure Report_Bit_Result
     (Ctx : in out Rt_Context; Fatal : Boolean; Set_Terminal : Boolean; Set_Subsystem : Boolean)
   is
   begin
      if Set_Terminal then
         Ctx.Terminal_Flag := True;
      end if;
      if Set_Subsystem then
         Ctx.Subsystem_Flag := True;
      end if;
      if Fatal then
         Ctx.Fail_Safe := True;
         Ctx.State := RT_Fail_Safe;           --  any state -> FAIL_SAFE, LLR-1553-RT-013
      end if;
   end Report_Bit_Result;

   --  implements: LLR-1553-BIT-008, LLR-1553-RT-006, LLR-1553-RT-008
   procedure Apply_Bit_State (Ctx : in out Rt_Context; State : BIT_State; Bit_Word : Data_Word) is
   begin
      Ctx.Bit_Word := Bit_Word;

      case State is
         when Fail =>
            Report_Bit_Result (Ctx, Fatal => True, Set_Terminal => True, Set_Subsystem => False);
         when Degraded =>
            Ctx.Bit_Degraded := True;
         when Operational =>
            Ctx.Bit_Degraded := False;
         when Power_Up | Pbit_Running | Cbit_Running | Ibit_Running =>
            null;      --  a test is in progress or has not started: no verdict yet
      end case;
   end Apply_Bit_State;

   --  implements: LLR-1553-RT-008
   procedure Take_Self_Test_Request (Ctx : in out Rt_Context; Requested : out Boolean) is
   begin
      Requested := Ctx.Self_Test_Requested;
      Ctx.Self_Test_Requested := False;
   end Take_Self_Test_Request;

   --  implements: LLR-1553-RT-008
   procedure Set_Vector_Word (Ctx : in out Rt_Context; Value : Data_Word) is
   begin
      Ctx.Vector_Word := Value;
   end Set_Vector_Word;

   procedure Attach_Cbit (Ctx : in out Rt_Context) is
   begin
      Ctx.Cbit_Attached := True;
   end Attach_Cbit;

   procedure Detach_Cbit (Ctx : in out Rt_Context) is
   begin
      Ctx.Cbit_Attached := False;
   end Detach_Cbit;

   --  helpers used by Run (the subunit)
   --  Implements: LLR-1553-ERR-004 (RT side); discards the NEWEST record when full
   procedure Log_Error
     (Ctx : in out Rt_Context; Chan : Channel; Kind : Error_Kind; Now : Time_Us)
   with Global => null,
        Post   =>
          Ctx.Log_Used >= Ctx'Old.Log_Used
          and then (for all I in 0 .. Ctx'Old.Log_Used - 1 => Ctx.Log (I) = Ctx'Old.Log (I))
          and then Ctx.State = Ctx'Old.State
   is
   begin
      if Ctx.Log_Used < Error_Log_Capacity then
         Ctx.Log (Ctx.Log_Used) :=
           (Timestamp => Now, Terminal => Ctx.Own_Terminal, Chan => Chan, Kind => Kind, Message => 0);
         Ctx.Log_Used := Ctx.Log_Used + 1;
      else
         Ctx.Log_Overflow := Ctx.Log_Overflow + 1;
      end if;
   end Log_Error;

   --  puts one word on this RT's current channel, unless this RT's transmitter on
   --  that channel has been shut down (mode codes 4/5), and lets the word time pass
   procedure Transmit_Word
     (Ctx : Rt_Context; Bus : in out Bus_Model_Types.Context; Syms : Symbols)
   with Global => null
   is
      Sent : Boolean;
   begin
      if Ctx.Tx_Shutdown (Ctx.Pending_Channel) then
         return;                                            --  LLR-1553-RT-008
      end if;
      Bus_Model.Transmit (Bus, Ctx.Pending_Channel, Syms, Ctx.Own_Terminal, Sent);
      Bus_Model.Advance_Us (Bus, Word_Time_Us);
      --  sent is not used: a failed channel still costs the RT the word time
      --  (as in the C), and the BC's timeout is what reports the silence
   end Transmit_Word;

   --  implements: LLR-1553-RT-008; the data word a transmit-type mode command returns
   function Mode_Response_Word (Ctx : Rt_Context) return Data_Word
   with Global => null
   is
   begin
      case Mode_Table (Ctx.Pending_Cmd.Word_Count_Raw).Handler is
         when Mode_Tx_Vector       => return Ctx.Vector_Word;
         when Mode_Tx_Last_Command => return Ctx.Last_Command;
         when Mode_Tx_Bit          => return Ctx.Bit_Word;     --  built from the BIT result structure
         when others               => return 0;
      end case;
   end Mode_Response_Word;

   --  implements: LLR-1553-RT-006
   --  builds this RT's status word from its latched conditions and sends it, followed (unless Suppress_Data) by Data_Count data
   --  words: the mode response word, or the subaddress buffer, or zero for any word the buffer does not hold
   procedure Build_And_Send_Status
     (Ctx           : Rt_Context;
      Bus           : in out Bus_Model_Types.Context;
      Suppress_Data : Boolean;
      Data_Count    : Data_Word_Count)
   with Global => null
   is
      Status : constant Status_Fields :=
        (Address            => Ctx.Own_Address,
         Message_Error      => Ctx.Message_Error_Latched,
         Instrumentation    => False,        --  not exercised (mil1553_notes.md 4)
         Service_Request    => False,
         Broadcast_Received => Ctx.Broadcast_Received_Latched,
         Busy               => Ctx.Subsystem_Busy,
         Subsystem_Flag     => Ctx.Subsystem_Flag or else Ctx.Bit_Degraded,   --  SF on a non-fatal BIT result
         DBC_Acceptance     => False,        --  dynamic Bus Control is not implemented
         --  TF: BIT-driven, hidden by inhibit-TF (mode 6), forced on in FAIL_SAFE (RT-013)
         Terminal_Flag      => (Ctx.Terminal_Flag and then not Ctx.Tf_Inhibited) or else Ctx.Fail_Safe,
         Reserved_Nonzero   => False);
      Syms : Symbols := Zero_Symbols;
   begin
      Mil1553_Word.Pack_Status (Status, Syms);
      Transmit_Word (Ctx, Bus, Syms);

      if not Suppress_Data then
         for I in 0 .. Data_Count - 1 loop
            declare
               Value : constant Data_Word :=
                 (if Ctx.Pending_Is_Mode then Mode_Response_Word (Ctx)
                  elsif I < Ctx.Buffers (Ctx.Pending_Cmd.Sub).Used
                  then Ctx.Buffers (Ctx.Pending_Cmd.Sub).Words (I)
                  else 0);                   --  the buffer holds fewer words than commanded
            begin
               Mil1553_Word.Pack_Data (Value, Syms);
               Transmit_Word (Ctx, Bus, Syms);
            end;
         end loop;
      end if;
   end Build_And_Send_Status;

   --  implements: LLR-1553-RT-003, RT-007, RT-010
   --  discards the whole message in progress: logs the error, latches ME, marks it a transmission error (so
   --  ERROR sends nothing) and enters RT_Error; a word-count error is also a CBIT word-count violation (LLR-1553-BIT-005)
   procedure Fail_Message
     (Ctx  : in out Rt_Context;
      Cbit : in out Bit_Cbit.Cbit_Context;
      Now  : Time_Us;
      Kind : Error_Kind)
   with Global => null
   is
   begin
      Log_Error (Ctx, Ctx.Pending_Channel, Kind, Now);
      if Kind = Err_Word_Count_Mismatch and then Ctx.Cbit_Attached then
         Bit_Cbit.Record_Word_Count_Violation (Cbit);
      end if;
      Ctx.Message_Error_Latched    := True;
      Ctx.Pending_Transmission_Err := True;
      Ctx.Commit_Pending           := False;
      Ctx.State                    := RT_Error;
   end Fail_Message;

   --  a word that belongs to the NEXT message is kept, not discarded
   procedure Replay_Word (Ctx : in out Rt_Context; Chan : Channel; Rec : Bus_Model_Types.Bus_Record)
   with Global => null
   is
   begin
      Ctx.Replay_Record  := Rec;
      Ctx.Replay_Channel := Chan;
      Ctx.Replay_Valid   := True;
   end Replay_Word;

   --  the next word on a channel for this RT: a replayed word first, else the bus
   procedure Next_Word
     (Ctx  : in out Rt_Context;
      Bus  : in out Bus_Model_Types.Context;
      Chan : Channel;
      Rec  : out Bus_Model_Types.Bus_Record;
      Got  : out Boolean)
   with Global => null
   is
   begin
      if Ctx.Replay_Valid and then Ctx.Replay_Channel = Chan then
         Rec := Ctx.Replay_Record;
         Ctx.Replay_Valid := False;
         Got := True;
      else
         Bus_Model.Receive (Bus, Chan, Ctx.Own_Terminal, Rec, Got);
      end if;
   end Next_Word;

   --  implements: LLR-1553-RT-008 (handler dispatch), LLR-1553-RT-013 (reset)
   procedure Execute_Mode (Ctx : in out Rt_Context; Data : Data_Word)
   with Global => null
   is
      --  transmitter shutdown commands act on the bus the command did NOT arrive on
      Other : constant Channel := (if Ctx.Pending_Channel = A then B else A);
   begin
      case Mode_Table (Ctx.Pending_Cmd.Word_Count_Raw).Handler is
         when Mode_Sync =>
            Ctx.Sync_Count := Ctx.Sync_Count + 1;
         when Mode_Sync_Data =>
            Ctx.Sync_Count := Ctx.Sync_Count + 1;
            Ctx.Last_Sync_Data := Data;
         when Mode_Self_Test =>
            Ctx.Self_Test_Requested := True;
         when Mode_Tx_Shutdown =>
            Ctx.Tx_Shutdown (Other) := True;
         when Mode_Override_Shutdown =>
            Ctx.Tx_Shutdown (Other) := False;
         when Mode_Inhibit_Tf =>
            Ctx.Tf_Inhibited := True;
         when Mode_Override_Inhibit_Tf =>
            Ctx.Tf_Inhibited := False;
         when Mode_Reset =>
            Ctx.Reset_Pending := True;        --  performed once the status word has been sent
         when Mode_None | Mode_Tx_Status | Mode_Tx_Vector | Mode_Tx_Last_Command | Mode_Tx_Bit =>
            null;                              --  transmit-type codes act through the response word
      end case;
   end Execute_Mode;

   --  implements: LLR-1553-RT-003. The whole block has arrived valid and no extra
   --  word followed, so it is committed now (or the data-carrying mode command
   --  executed)
   procedure Commit_Received (Ctx : in out Rt_Context)
   with Global => null
   is
   begin
      if Ctx.Pending_Is_Mode then
         Ctx.Mode_Data_Word := Ctx.Staging (0);
         Execute_Mode (Ctx, Ctx.Mode_Data_Word);
      else
         for K in 0 .. Ctx.Pending_Data_Received - 1 loop
            Ctx.Buffers (Ctx.Pending_Cmd.Sub).Words (K) := Ctx.Staging (K);
         end loop;
         Ctx.Buffers (Ctx.Pending_Cmd.Sub).Used := Ctx.Pending_Data_Received;
      end if;
      Ctx.Commit_Pending := False;
   end Commit_Received;

   --  implements: LLR-1553-RT-013. A message is over: honour a pending reset, else
   --  return to WAIT_COMMAND - or stay in FAIL_SAFE, which only a reset leaves
   procedure Finish_Message (Ctx : in out Rt_Context)
   with Global => null
   is
   begin
      Ctx.Message_End_Time := 0;
      if Ctx.Reset_Pending then
         Reset (Ctx);
      else
         Ctx.State := (if Ctx.Fail_Safe then RT_Fail_Safe else RT_Wait_Command);
      end if;
   end Finish_Message;

   --  implements: LLR-1553-RT-001 .. LLR-1553-RT-013
   --  the state machine: the subunit src/ada/rt_service-run.adb.
   procedure Run
     (Ctx    : in out Rt_Context;
      Bus    : in out Bus_Model_Types.Context;
      Cbit   : in out Bit_Cbit.Cbit_Context;
      Active : out Boolean) is separate;

end Rt_Service;