--  project:  MIL1553

--  realizes: LLR-1553-BM-001, LLR-1553-BM-002, LLR-1553-BM-003

--  proof status: written to be proved with GNATprove, NOT yet run (no GNAT in the author's environment), the while loops carry Loop_Variants for termination;
--  expect to tune the invariants (framing of the capture buffer) on the first run

--  LLR-1553-BM-003: this body withs Bus_Model_Monitor_If and Mil1553_Word, never Bus_Model, nothing here can transmit

with Bus_Model_Monitor_If;
with Mil1553_Word;

package body Bm_Monitor
  with SPARK_Mode => On
is

   --  implements: LLR-1553-BM-001
   procedure Init (M : out Monitor_Context) is
   begin
      M := (others => <>);
   end Init;

   --  implements: LLR-1553-BM-001
   --  drains what is waiting on one channel (at most one bus ring's worth per call, a bound that keeps the loop finite) into the capture buffer
   procedure Capture_Channel
     (M    : in out Monitor_Context;
      Bus  : in out Bus_Model_Types.Context;
      Chan : Channel)
   with Global  => null,
        Depends => (M => (M, Bus, Chan), Bus => (Bus, Chan)),
        Post    =>
          Bus.Chan = Bus'Old.Chan
          and then Bus.Clock = Bus'Old.Clock
          and then Bus.Loopback_Count = Bus'Old.Loopback_Count
          and then (for all C in Channel =>
                      (for all T in Terminal_Id =>
                         (if T /= BM then
                            Bus.Read_Count (C, T) = Bus'Old.Read_Count (C, T)
                            and then Bus.Dropped_Count (C, T) = Bus'Old.Dropped_Count (C, T))))
          and then M.Capture.Used >= M'Old.Capture.Used
          and then (for all I in 0 .. M'Old.Capture.Used - 1 =>
                      M.Capture.Records (I) = M'Old.Capture.Records (I))
          and then M.Messages = M'Old.Messages
          and then M.Message_Used = M'Old.Message_Used
   is
      Rec : Bus_Model_Types.Bus_Record;
      Got : Boolean;
   begin
      for Drained in 1 .. Bus_Model_Types.Queue_Capacity loop
         Bus_Model_Monitor_If.Receive (Bus, Chan, Rec, Got);
         exit when not Got;

         if M.Capture.Used < Capture_Capacity then
            declare
               Decoded : constant Mil1553_Word.Decoded_Word := Mil1553_Word.Classify (Rec.Syms);
            begin
               M.Capture.Records (M.Capture.Used) :=
                 (Chan       => Chan,
                  Start_Time => Rec.Timestamp,
                  Syms       => Rec.Syms,
                  Sender     => Rec.Sender,
                  Class      => Decoded.Class);
               M.Capture.Used := M.Capture.Used + 1;
            end;
         else
            M.Capture.Dropped_Count := M.Capture.Dropped_Count + 1;   --  the earliest words are kept
         end if;

         pragma Loop_Invariant (M.Capture.Used >= M.Capture.Used'Loop_Entry);
         pragma Loop_Invariant
           (for all I in 0 .. M.Capture.Used'Loop_Entry - 1 =>
              M.Capture.Records (I) = M.Capture.Records'Loop_Entry (I));
         pragma Loop_Invariant (Bus.Chan = Bus.Chan'Loop_Entry);
         pragma Loop_Invariant (Bus.Clock = Bus.Clock'Loop_Entry);
         pragma Loop_Invariant (Bus.Loopback_Count = Bus.Loopback_Count'Loop_Entry);
         pragma Loop_Invariant
           (for all C in Channel =>
              (for all T in Terminal_Id =>
                 (if T /= BM then
                    Bus.Read_Count (C, T) = Bus.Read_Count'Loop_Entry (C, T)
                    and then Bus.Dropped_Count (C, T) = Bus.Dropped_Count'Loop_Entry (C, T))));
         pragma Loop_Invariant (M.Messages = M.Messages'Loop_Entry);
         pragma Loop_Invariant (M.Message_Used = M.Message_Used'Loop_Entry);
      end loop;
   end Capture_Channel;

   --  implements: LLR-1553-BM-001
   procedure Run (M : in out Monitor_Context; Bus : in out Bus_Model_Types.Context) is
   begin
      Capture_Channel (M, Bus, A);
      Capture_Channel (M, Bus, B);
   end Run;

   --  message reconstruction, LLR-1553-BM-002
   --  implements: LLR-1553-BM-002 (message type from the command word)
   function Kind_Of (Cmd : Command_Fields) return Message_Kind
   is (if Cmd.Address = RT_Address (RT_Addr_Broadcast) then Broadcast
       elsif Cmd.Sub = 0 or else Cmd.Sub = 31 then Mode_Command
       elsif Cmd.Direction = Receive then BC_To_RT
       else RT_To_BC)
   with Global => null;

   --  implements: LLR-1553-BM-002
   procedure Add_Message (M : in out Monitor_Context; Msg : Message)
   with Global  => null,
        Depends => (M => (M, Msg)),
        Post    => M.Capture = M'Old.Capture
   is
   begin
      if M.Message_Used < Message_Capacity then
         M.Messages (M.Message_Used) := Msg;
         M.Message_Used := M.Message_Used + 1;
      else
         M.Message_Overflow_Count := M.Message_Overflow_Count + 1;   --  earlier messages are kept
      end if;
   end Add_Message;

   --  implements: LLR-1553-BM-002
   --  builds the one message that starts at capture index Start (a VALID BC-sent command/status-sync word): its kind, the RT-to-RT pairing, the end of its
   --  last BC-sent word, and the response; Last_Index is the last capture index that belongs to the command phase (Start, or the second command of an
   --  RT-to-RT pair); reconstruction resumes after it
   procedure Build_Message
     (M          : Monitor_Context;
      Chan       : Channel;
      Start      : Capture_Index;
      Cmd1       : Command_Fields;
      Msg        : out Message;
      Last_Index : out Natural)
   with Global  => null,
        Depends => (Msg => (M, Chan, Start, Cmd1), Last_Index => (M, Chan, Start, Cmd1)),
        Pre     => Start < M.Capture.Used,
        Post    => Last_Index >= Start and then Last_Index < M.Capture.Used
   is
      W        : constant Captured_Word := M.Capture.Records (Start);
      Last_BC_End : Time_Us;
      J, K     : Natural;
      Done     : Boolean;
   begin
      Msg := (Kind             => Kind_Of (Cmd1),
              Chan             => Chan,
              Address          => Cmd1.Address,
              Address_2        => 0,
              Command_End_Time => W.Start_Time + Word_Time_Us,
              Response_Seen    => False,
              Response_Start_Time => 0,
              Response_Time_Us => 0);
      Last_Index := Start;

      --  look at the next captured word on this channel: an RT-to-RT message is a plain receive command followed with NO gap by a transmit command to a
      --  different RT; a broadcast or mode command is never the first half
      J := Start + 1;
      while J < M.Capture.Used and then M.Capture.Records (J).Chan /= Chan loop
         pragma Loop_Invariant (J > Start and then J <= M.Capture.Used);
         pragma Loop_Variant (Increases => J);
         J := J + 1;
      end loop;

      if J < M.Capture.Used
        and then M.Capture.Records (J).Sender = BC
        and then M.Capture.Records (J).Class = Valid
      then
         declare
            D2   : constant Mil1553_Word.Decoded_Word := Mil1553_Word.Classify (M.Capture.Records (J).Syms);
            Cmd2 : constant Command_Fields             := Mil1553_Word.Unpack_Command (D2.Field);
         begin
            if D2.Sync = Command_Status
              and then Cmd2.Direction = Transmit
              and then Cmd1.Direction = Receive
              and then Msg.Kind /= Broadcast
              and then Msg.Kind /= Mode_Command
              and then Cmd2.Address /= Cmd1.Address
              and then M.Capture.Records (J).Start_Time < W.Start_Time + Word_Time_Us + T_Gap_Min_Us
            then
               Msg.Kind             := RT_To_RT;
               Msg.Address_2        := Cmd2.Address;
               Msg.Command_End_Time := M.Capture.Records (J).Start_Time + Word_Time_Us;
               Last_Index           := J;
            end if;
         end;
      end if;

      --  scan forward for the response: the first RT-sent, valid, command/status sync word before the next BC command; BC data words extend the BC-sent
      --  part of the message; RT data words are not the status
      Last_BC_End := Msg.Command_End_Time;
      K := Last_Index + 1;
      Done := False;
      while K < M.Capture.Used and then not Done loop
         pragma Loop_Invariant (K > Last_Index and then K <= M.Capture.Used);
         pragma Loop_Variant (Increases => K);

         if M.Capture.Records (K).Chan = Chan and then M.Capture.Records (K).Class = Valid then
            declare
               D3 : constant Mil1553_Word.Decoded_Word := Mil1553_Word.Classify (M.Capture.Records (K).Syms);
            begin
               if M.Capture.Records (K).Sender = BC then
                  if D3.Sync = Command_Status then
                     Done := True;                       --  the next message has begun
                  else
                     Last_BC_End := M.Capture.Records (K).Start_Time + Word_Time_Us;
                  end if;
               elsif D3.Sync = Command_Status then
                  Msg.Response_Seen       := True;
                  Msg.Response_Start_Time := M.Capture.Records (K).Start_Time;
                  Msg.Response_Time_Us    := M.Capture.Records (K).Start_Time - Last_BC_End;
                  Done := True;
               end if;
            end;
         end if;

         K := K + 1;
      end loop;

      Msg.Command_End_Time := Last_BC_End;
   end Build_Message;

   --  implements: LLR-1553-BM-002
   procedure Reconstruct_Channel (M : in out Monitor_Context; Chan : Channel)
   with Global  => null,
        Depends => (M => (M, Chan)),
        Post    => M.Capture = M'Old.Capture
   is
      I : Natural := 0;
   begin
      while I < M.Capture.Used loop
         pragma Loop_Invariant (M.Capture = M.Capture'Loop_Entry);
         pragma Loop_Variant (Increases => I);

         declare
            W : constant Captured_Word := M.Capture.Records (I);
         begin
            if W.Chan = Chan and then W.Sender = BC and then W.Class = Valid then
               declare
                  D : constant Mil1553_Word.Decoded_Word := Mil1553_Word.Classify (W.Syms);
               begin
                  --  a BC-sent word starts a message only if it is a command (command/status sync); BC data words belong to the message
                  --  already in progress
                  if D.Sync = Command_Status then
                     declare
                        Msg  : Message;
                        Last : Natural;
                     begin
                        Build_Message (M, Chan, I, Mil1553_Word.Unpack_Command (D.Field), Msg, Last);
                        Add_Message (M, Msg);
                        I := Last;       --  skip the second command of an RT-to-RT pair
                     end;
                  end if;
               end;
            end if;
         end;

         I := I + 1;
      end loop;
   end Reconstruct_Channel;

   --  implements: LLR-1553-BM-002
   procedure Reconstruct_Messages (M : in out Monitor_Context) is
   begin
      M.Message_Used := 0;
      M.Message_Overflow_Count := 0;

      Reconstruct_Channel (M, A);
      Reconstruct_Channel (M, B);
   end Reconstruct_Messages;

end Bm_Monitor;