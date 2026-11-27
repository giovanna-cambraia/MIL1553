--  project:    MIL1553
--  purpose:    passive bus word capture and message reconstruction

--  realizes:   LLR-1553-BM-001, LLR-1553-BM-002, LLR-1553-BM-003
--  parent HLR: HLR-1553-BM-001

--  LLR-1553-BM-003 (no transmit path) is met at compile time; this package, and its body, with Bus_Model_Types and Bus_Model_Monitor_If and NEVER Bus_Model, so
--  Transmit, Set_Channel_Failed and Advance_Us cannot even be named here; the check script (tests/ada/check_bm_isolation.sh) confirms no "with Bus_Model;"
--  appears in bm_monitor.ads or bm_monitor.adb

--  difference from the C: the C monitor keeps a pointer to the bus; SPARK does not allow that aliasing, so here the monitor context holds only the
--  monitor's own buffers and Run takes the bus as a parameter; the Run contract says the monitor changes nothing on the bus except the BM's own read cursors

--  message reconstruction (a deliberate, documented simplification); a message begins at a VALID, BC-sent word with command/status sync - BC-sent data words
--  belong to the message in progress; this is sound because the BC never sends a new command until the previous message (and any retry) is over; a back-to-back
--  pair is one RT-to-RT message only when it is a plain receive command followed with no gap by a transmit command to a DIFFERENT RT; a broadcast or mode
--  command is never the first half of an RT-to-RT; reconstruction runs per channel, so a retry on the other bus is a separate message on that channel,
--  response time runs from the end of the last BC-sent word of the message to the start of the first RT-sent status word

with Mil1553_Types;   use Mil1553_Types;
with Bus_Model_Types;

package Bm_Monitor
  with SPARK_Mode => On
is

   use type Bus_Model_Types.Channel_States;
   use type Bus_Model_Types.Cursors;
   use type Bus_Model_Types.Counter;

   Capture_Capacity : constant := 256;
   Message_Capacity : constant := 128;

   type Count is mod 2 ** 32;

   --  captured words, LLR-1553-BM-001
   type Captured_Word is record
      Chan       : Channel     := A;
      Start_Time : Time_Us     := 0;
      Syms       : Symbols     := Zero_Symbols;
      Sender     : Terminal_Id := BC;
      Class      : Word_Class  := Valid;      --  Valid, Sync_Error, Manchester_Error, Parity_Error
   end record;

   subtype Capture_Index is Natural range 0 .. Capture_Capacity - 1;
   type Capture_Array is array (Capture_Index) of Captured_Word;

   type Capture_Buffer is record
      Records       : Capture_Array;
      Used          : Natural range 0 .. Capture_Capacity := 0;
      Dropped_Count : Count := 0;               --  words that did not fit
   end record;

   -- reconstructed messages, LLR-1553-BM-002
   type Message is record
      Kind                   : Message_Kind := BC_To_RT;
      Chan                   : Channel      := A;
      Address                : RT_Address   := 0;   --  the RT, or the receiving RT of an RT-to-RT
      Address_2              : RT_Address   := 0;   --  the transmitting RT of an RT-to-RT
      Command_End_Time       : Time_Us      := 0;   --  end of the last BC-sent word of the message
      Response_Seen          : Boolean      := False;
      Response_Start_Time    : Time_Us      := 0;   --  valid only if Response_Seen
      Response_Time_Us       : Time_Us      := 0;   --  Response_Start_Time - Command_End_Time
   end record;

   subtype Message_Index is Natural range 0 .. Message_Capacity - 1;
   type Message_Array is array (Message_Index) of Message;

   --  the monitor
   type Monitor_Context is record
      Capture                : Capture_Buffer;
      Messages               : Message_Array;
      Message_Used           : Natural range 0 .. Message_Capacity := 0;
      Message_Overflow_Count : Count := 0;
   end record;

   type Stats is record
      Captured_Count         : Natural range 0 .. Capture_Capacity := 0;
      Dropped_Count          : Count   := 0;
      Message_Count          : Natural range 0 .. Message_Capacity := 0;
      Message_Overflow_Count : Count   := 0;
   end record;

   --  implements: LLR-1553-BM-001
   procedure Init (M : out Monitor_Context)
   with Global  => null,
        Depends => (M => null),
        Pre     => True,
        Post    => M.Capture.Used = 0
                   and then M.Capture.Dropped_Count = 0
                   and then M.Message_Used = 0
                   and then M.Message_Overflow_Count = 0;

   --  implements: LLR-1553-BM-001, LLR-1553-BM-003
   --  reads whatever is waiting on both channels, as the BM listener, and appends each word to the capture buffer with its channel, start time, symbols,
   --  sender and classification (Mil1553_Word.Classify); when the buffer is full a word is dropped and counted: the EARLIEST captures are the ones kept, never
   --  overwritten; reading is passive: nothing on the bus changes except the BM's own cursors, so BC, RT1 and RT2 still see every word
   procedure Run (M : in out Monitor_Context; Bus : in out Bus_Model_Types.Context)
   with Global  => null,
        Depends => (M => (M, Bus), Bus => Bus),
        Pre     => True,
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
          and then M.Message_Used = M'Old.Message_Used;

   --  implements: LLR-1553-BM-002
   --  rebuilds the message list from the capture buffer, from scratch (so it is repeatable and picks up new words); the capture buffer is not changed; on a
   --  full message list the rest are counted in Message_Overflow_Count and dropped: the earlier messages are the ones kept
   procedure Reconstruct_Messages (M : in out Monitor_Context)
   with Global  => null,
        Depends => (M => M),
        Pre     => True,
        Post    => M.Capture = M'Old.Capture;

   --  read access (the C returns NULL for an out-of-range index; Ada states it as a precondition instead)
   function Captured_Count (M : Monitor_Context) return Natural
   is (M.Capture.Used)
   with Global => null;

   function Message_Count (M : Monitor_Context) return Natural
   is (M.Message_Used)
   with Global => null;

   function Capture_At (M : Monitor_Context; Index : Capture_Index) return Captured_Word
   is (M.Capture.Records (Index))
   with Global => null,
        Pre    => Index < M.Capture.Used;

   function Message_At (M : Monitor_Context; Index : Message_Index) return Message
   is (M.Messages (Index))
   with Global => null,
        Pre    => Index < M.Message_Used;

   function Get_Stats (M : Monitor_Context) return Stats
   is (Stats'(Captured_Count         => M.Capture.Used,
              Dropped_Count          => M.Capture.Dropped_Count,
              Message_Count          => M.Message_Used,
              Message_Overflow_Count => M.Message_Overflow_Count))
   with Global => null;

end Bm_Monitor;