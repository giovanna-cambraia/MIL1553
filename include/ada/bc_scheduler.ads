--  project:    MIL1553
--  purpose:    bus controller: schedule table and its validation, message issuance for all formats, inter-message gap, no-response timeout, A/B retry and
--              failover, and the BC state machine
--  realizes:   LLR-1553-BC-001 .. LLR-1553-BC-010, LLR-1553-REDUN-001 .. LLR-1553-REDUN-003
--  parent HLRs: HLR-1553-BC-001 .. HLR-1553-BC-005, HLR-1553-REDUN-001

--  Run_Frame is a single-step procedure, called repeatedly by the BC role's main loop; at most one state transition's worth of work per call (mirrors Rt_Service.Run)

--  differences from the C interface (bc_scheduler.h), and why
--    * no stored pointers: Run_Frame takes the bus and the CBIT context as parameters; "CBIT attached" is a flag in the context (Attach_Cbit / Detach_Cbit);
--      when it is not set the Cbit parameter is left exactly as it was
--    * Init returns Ok and has no bus parameter; the initial primary bus cannot be out of range (type), so that C check disappears
--    * Run_Frame returns Active instead of a bool: False exactly when the BC is in BC_Idle (nothing else happens then)
--    * Validate / Message_Duration are functions here; their bodies are in bc_scheduler.adb
--    * message type is Mil1553_Types.Message_Kind; the BC has its OWN mode code table (in the body), deliberately not shared with Rt_Service (SDD 1)

with Mil1553_Types;   use Mil1553_Types;
with Bus_Model_Types;
with Bit_Cbit;

package Bc_Scheduler
  with SPARK_Mode => On
is
   use type Bus_Model_Types.Counter;
   use type Bit_Cbit.Cbit_Context;

   Max_Schedule_Entries : constant := 64;
   Max_Minor_Frames     : constant := 32;

   subtype RT_Real_Address is RT_Address range 0 .. RT_Addr_Max;

   type Count is mod 2 ** 32;

   --  implements: LLR-1553-BC-001
   --  one schedule table entry. RT_To_RT: Address is the RECEIVING RT, Address_2 the TRANSMITTING one (LLR-1553-BC-005); Address_2 / Sub_2 are unused for every
   --  other kind. Mode_Command: Mode, Has_Data_Word and Mode_Data_From_BC are used, Sub / Sub_2 / Word_Count are not (the mode indicator is implied, BC-006)
   type Schedule_Entry is record
      Kind              : Message_Kind    := BC_To_RT;
      Address           : RT_Address      := 0;
      Address_2         : RT_Address      := 0;
      Sub               : Subaddress      := 1;
      Sub_2             : Subaddress      := 1;
      Word_Count        : Data_Word_Count := 1;           --  logical 1 .. 32; 0 if not applicable
      Mode              : Mode_Code       := 0;
      Has_Data_Word     : Boolean         := False;
      Mode_Data_From_BC : Boolean         := False;       --  True: BC sends the word (mode 17); False: RT returns it (16, 18, 19)
      Minor_Frame_Index : Natural range 0 .. Max_Minor_Frames - 1 := 0;
      Slot_Offset_Us    : Natural         := 0;           --  from the minor frame start
      Preferred_Bus     : Channel         := A;           --  reserved for validation grouping, as in the C
   end record;

   subtype Entry_Index is Natural range 0 .. Max_Schedule_Entries - 1;
   type Schedule_Entries is array (Entry_Index) of Schedule_Entry;

   type Schedule is record
      Entries          : Schedule_Entries;
      Entry_Count      : Natural range 0 .. Max_Schedule_Entries := 0;
      Minor_Frame_Count : Natural range 0 .. Max_Minor_Frames    := 0;   --  valid schedules have 1 .. 32
      Period_Us        : Natural := 0;                                   --  minor frame period; valid schedules have > 0
   end record;

   type Entry_Flags is array (Entry_Index) of Boolean;
   type Rt_Flags    is array (RT_Real_Address) of Boolean;
   type Fail_Counts is array (Channel) of Natural range 0 .. 255;

   --  error log, LLR-1553-ERR-004 (fixed capacity, earliest records kept)
   subtype Log_Index is Natural range 0 .. Error_Log_Capacity - 1;
   type Error_Log is array (Log_Index) of Error_Record;

   type Health is record
      Degraded       : Boolean := False;
      Primary_Bus    : Channel := A;
      Rt_Failed      : Rt_Flags := (others => False);
      Ignored_Events : Count := 0;                  --  LLR-1553-BC-010
   end record;

   --  the BC
   type Bc_Context is record
      Sched                   : Schedule;
      State                   : BC_State := BC_Idle;
      Current_Minor_Frame     : Natural range 0 .. Max_Minor_Frames := 0;
      Entry_Issued            : Entry_Flags := (others => False);     --  cleared each major frame

      Primary_Bus             : Channel := A;
      Consecutive_Fails       : Fail_Counts := (others => 0);
      Fail_Threshold          : Positive range 1 .. 255 := Redun_Fail_Threshold_Default;   --  LLR-1553-REDUN-002

      Retry_Used              : Boolean := False;                     --  LLR-1553-REDUN-001
      Current_Message_Bus     : Channel := A;
      Message_Start_Time      : Time_Us := 0;
      Last_Activity_End       : Time_Us := 0;    --  end of the last word of the in-flight message (or the BC's own last word); the timeout runs from here (BC-009)
      Minor_Frame_Start       : Time_Us := 0;

      --  in-flight message tracking (BC-003 .. BC-006); Response_Stage: 0 = awaiting primary status, 1 = awaiting data, 2 = awaiting secondary status (RT-to-RT)
      Snapshot_Index          : Entry_Index := 0;
      Response_Primary_RT     : RT_Address := 0;
      Response_Secondary_RT   : RT_Address := 0;
      Response_Is_RT_To_RT    : Boolean := False;
      Response_Stage          : Natural range 0 .. 2 := 0;
      Response_Words_Expected : Data_Word_Count := 0;
      Response_Words_Received : Data_Word_Count := 0;

      Degraded_Indication     : Boolean := False;     --  sticky until Init, set on a bus switch
      Overrun_Logged          : Boolean := False;     --  MINOR_FRAME_OVERRUN already raised for this minor frame
      Message_Ever_Sent       : Boolean := False;     --  False until the first command goes out (gap rule)
      Ignored_Event_Count     : Count := 0;           --  LLR-1553-BC-010

      Rt_Failed_This_Frame    : Rt_Flags := (others => False);   --  LLR-1553-REDUN-003
      Cbit_Attached           : Boolean := False;     --  LLR-1553-BIT-005 / BIT-006

      Log                     : Error_Log;
      Log_Used                : Natural range 0 .. Error_Log_Capacity := 0;
      Log_Overflow            : Count := 0;
   end record;

   --  schedule validation and message duration (bodies in bc_scheduler.adb)

   --  implements: LLR-1553-BC-001
   --  worst-case duration in microseconds of one entry, per design/timing_budget.md 4; uses the entry's own word count, not an assumed maximum
   function Message_Duration_Us (E : Schedule_Entry) return Natural
   with Global => null;

   --  implements: LLR-1553-BC-001, LLR-1553-BC-006
   --  every field range check, mode code table agreement, no two entries of one minor frame overlapping (worst-case durations), and every entry's
   --  slot offset + duration inside the minor frame period
   function Is_Valid (S : Schedule) return Boolean
   with Global => null;

   --  lifecycle

   --  implements: LLR-1553-BC-001, LLR-1553-REDUN-001, LLR-1553-REDUN-002
   --  validates the schedule; on failure Ok is False and the context is left in BC_Idle with default contents. Fail_Threshold of 0 selects the default (3)
   procedure Init
     (Ctx               : out Bc_Context;
      Sched             : Schedule;
      Initial_Primary   : Channel;
      Fail_Threshold    : Byte;
      Ok                : out Boolean)
   with Global  => null,
        Depends => (Ctx => (Sched, Initial_Primary, Fail_Threshold),
                    Ok  => Sched),
        Pre     => True,
        Post    =>
          Ok = Is_Valid (Sched)
          and then Ctx.State = BC_Idle
          and then Ctx.Log_Used = 0
          and then (if Ok then Ctx.Sched = Sched
                               and then Ctx.Primary_Bus = Initial_Primary
                               and then not Ctx.Degraded_Indication
                               and then not Ctx.Cbit_Attached
                               and then Ctx.Fail_Threshold =
                                          (if Fail_Threshold = 0 then Redun_Fail_Threshold_Default
                                           else Positive (Fail_Threshold)));

   --  implements: LLR-1553-BIT-005, LLR-1553-BIT-006
   --  once attached, Run_Frame reports every received word classification and response time to the Cbit context and runs its watchdog tick and kick
   procedure Attach_Cbit (Ctx : in out Bc_Context)
   with Global => null, Depends => (Ctx => Ctx), Pre => True,
        Post   => Ctx.Cbit_Attached;

   procedure Detach_Cbit (Ctx : in out Bc_Context)
   with Global => null, Depends => (Ctx => Ctx), Pre => True,
        Post   => not Ctx.Cbit_Attached;

   --  implements: LLR-1553-BC-010 (start: BC_Idle -> BC_Major_Frame); Ok is False from any other state, and the request is then ignored and counted
   procedure Start (Ctx : in out Bc_Context; Ok : out Boolean)
   with Global  => null,
        Depends => (Ctx => Ctx, Ok => Ctx),
        Pre     => True,
        Post    => Ok = (Ctx'Old.State = BC_Idle)
                   and then (if Ok then Ctx.State = BC_Major_Frame
                             else Ctx.State = Ctx'Old.State
                                  and then Ctx.Ignored_Event_Count = Ctx'Old.Ignored_Event_Count + 1);

   --  implements: LLR-1553-BC-010 (stop: any state -> BC_Idle)
   procedure Stop (Ctx : in out Bc_Context)
   with Global => null, Depends => (Ctx => Ctx), Pre => True,
        Post   => Ctx.State = BC_Idle;

   --  the state machine

   --  implements: LLR-1553-BC-001 .. LLR-1553-BC-010, LLR-1553-REDUN-001 .. LLR-1553-REDUN-003
   --  advances the BC by one step; Active is False exactly when the BC is in BC_Idle (nothing else happens then). Normal protocol conditions (timeouts,
   --  errors) are handled inside and still give Active = True

   --  what a step may do to the shared bus: add the BC's own words and advance the clock, and move the BC's OWN read cursors (discarding stale words);
   --  it never moves another terminal's cursor. The error log only grows (earliest records kept). When CBIT is not attached Cbit is unchanged
   procedure Run_Frame
     (Ctx    : in out Bc_Context;
      Bus    : in out Bus_Model_Types.Context;
      Cbit   : in out Bit_Cbit.Cbit_Context;
      Active : out Boolean)
   with Global  => null,
        Depends => (Ctx => (Ctx, Bus), Bus => (Ctx, Bus), Cbit => (Ctx, Bus, Cbit), Active => Ctx),
        Pre     => True,
        Post    =>
          Active = (Ctx'Old.State /= BC_Idle)
          and then Ctx.Sched = Ctx'Old.Sched
          and then (for all C in Channel =>
                      (for all T in Terminal_Id =>
                         (if T /= BC then
                            Bus.Read_Count (C, T) = Bus'Old.Read_Count (C, T)
                            and then Bus.Loopback_Count (C, T) = Bus'Old.Loopback_Count (C, T))))
          and then Ctx.Log_Used >= Ctx'Old.Log_Used
          and then (for all I in 0 .. Ctx'Old.Log_Used - 1 => Ctx.Log (I) = Ctx'Old.Log (I))
          and then (if not Ctx'Old.Cbit_Attached then Cbit = Cbit'Old)
          and then (if not Active then Ctx = Ctx'Old);

   --  read access
   function Get_State (Ctx : Bc_Context) return BC_State
   is (Ctx.State) with Global => null;

   --  implements: LLR-1553-REDUN-002, LLR-1553-REDUN-003
   function Get_Health (Ctx : Bc_Context) return Health
   is (Health'(Degraded       => Ctx.Degraded_Indication,
               Primary_Bus    => Ctx.Primary_Bus,
               Rt_Failed      => Ctx.Rt_Failed_This_Frame,
               Ignored_Events => Ctx.Ignored_Event_Count))
   with Global => null;

   --  implements: LLR-1553-ERR-004 (read-only error log view)
   function Error_Log_Count (Ctx : Bc_Context) return Natural
   is (Ctx.Log_Used) with Global => null;

   function Error_At (Ctx : Bc_Context; Index : Log_Index) return Error_Record
   is (Ctx.Log (Index))
   with Global => null,
        Pre    => Index < Ctx.Log_Used;

   function Error_Overflow_Count (Ctx : Bc_Context) return Count
   is (Ctx.Log_Overflow) with Global => null;

end Bc_Scheduler;