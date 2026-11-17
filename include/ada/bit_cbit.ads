--  project:    MIL1553
--  purpose:    Continuous Built-In Test: word and protocol conformance observation, per-channel health counters, scheduler watchdog

--  realizes:   LLR-1553-BIT-005, LLR-1553-BIT-006 (and the CBIT side of LLR-1553-BIT-009 through Update_Result)
--  parent HLR: HLR-1553-BIT-002

--  CBIT does not poll the bus: LLR-1553-BIT-002 requires it to run "without disturbing bus timing", and Bc_Scheduler / Rt_Service already classify every
--  word they handle; they REPORT each classification and response time here; the owner calls Update_Result once per CBIT cycle to turn what was observed into a
--  BIT state

--  cycle semantics; a cycle fails (non-fatal, so DEGRADED) when, since the previous Update_Result, any channel is unhealthy, the watchdog tripped, or a
--  new response-time or word-count violation was recorded; the next cycle with none of those passes and clears DEGRADED (LLR-1553-BIT-009)

--  channel health; a channel is judged only once its window holds at least Min_Samples words, and is unhealthy when the share of errors in the window is
--  GREATER than 20 %. The C computes (errors * 100) / words > 20 with integer division; that is exactly errors * 100 >= 21 * words, which is what is used
--  here, so no division (and no rounding question) is involved

--  watchdog; it does nothing until the loop has kicked it once (so a terminal that starts late is not tripped on its first call); a trip is LATCHED: a later kick
--  does not clear it, only Update_Result reports and consumes it

with Mil1553_Types; use Mil1553_Types;
with Bit_Types;

package Bit_Cbit
  with SPARK_Mode => On
is

   Window_Size      : constant := 32;   --  recent word outcomes kept, per channel
   Min_Samples      : constant := 8;    --  words needed before a channel is judged
   Error_Percent_Threshold : constant := 20;
   Watchdog_Period_Default_Us : constant := 10_000;

   type Count is mod 2 ** 32;

   subtype Window_Index is Natural range 0 .. Window_Size - 1;
   type Outcome_Window is array (Window_Index) of Boolean;     --  True = good word
   type Class_Counts is array (Word_Class) of Count;

   type Channel_Health is record
      Outcomes          : Outcome_Window := (others => True);
      Next_Index        : Window_Index   := 0;
      Filled_Count      : Natural range 0 .. Window_Size := 0;
      Good_Count_Total  : Count := 0;
      Error_Count_Total : Count := 0;
      Class_Count       : Class_Counts := (others => 0);   --  Valid, Sync_Error, Manchester_Error, Parity_Error
      Unhealthy         : Boolean := False;
   end record;

   type Channel_Healths is array (Channel) of Channel_Health;

   type Cbit_Context is record
      Chan                     : Channel_Healths;
      Response_Time_Violations : Count := 0;       --  cumulative
      Word_Count_Violations    : Count := 0;       --  cumulative
      Response_Time_Reported   : Count := 0;       --  value at the previous Update_Result
      Word_Count_Reported      : Count := 0;

      Watchdog_Period_Us       : Time_Us := Watchdog_Period_Default_Us;
      Watchdog_Last_Progress   : Time_Us := 0;
      Watchdog_Armed           : Boolean := False;   --  False until the first kick
      Watchdog_Stalled         : Boolean := False;   --  currently past its period
      Watchdog_Tripped         : Boolean := False;   --  latched until reported
      Watchdog_Trip_Count      : Count   := 0;
   end record;

   --  the health rule and the cycle rule, as functions the contracts can use
   --  Number of error words among the Filled_Count words held in the window
   function Window_Errors (Ch : Channel_Health) return Natural
   with Global => null,
        Post   => Window_Errors'Result <= Ch.Filled_Count;

   --  implements: LLR-1553-BIT-006
   function Is_Unhealthy (Ch : Channel_Health) return Boolean
   is (Ch.Filled_Count >= Min_Samples
       and then Window_Errors (Ch) * 100 >= (Error_Percent_Threshold + 1) * Ch.Filled_Count)
   with Global => null;

   --  true if a CBIT cycle run now would fail (LLR-1553-BIT-005 / BIT-006)
   function Cycle_Failed (Ctx : Cbit_Context) return Boolean
   is (Ctx.Chan (A).Unhealthy
       or else Ctx.Chan (B).Unhealthy
       or else Ctx.Watchdog_Tripped
       or else Ctx.Response_Time_Violations /= Ctx.Response_Time_Reported
       or else Ctx.Word_Count_Violations    /= Ctx.Word_Count_Reported)
   with Global => null;

   --  the tests that failed in a cycle run now, one bit each
   function Cause_Mask (Ctx : Cbit_Context) return Bit_Types.Failure_Mask
   is ((if Ctx.Chan (A).Unhealthy or else Ctx.Chan (B).Unhealthy
        then Bit_Types.Mask_Of (Bit_Types.Cbit_Test_Channel_Health) else 0)
       + (if Ctx.Watchdog_Tripped
          then Bit_Types.Mask_Of (Bit_Types.Cbit_Test_Watchdog) else 0)
       + (if Ctx.Response_Time_Violations /= Ctx.Response_Time_Reported
             or else Ctx.Word_Count_Violations /= Ctx.Word_Count_Reported
          then Bit_Types.Mask_Of (Bit_Types.Cbit_Test_Protocol) else 0))
   with Global => null;

   --  the first (lowest-numbered) failed test, or Cbit_Test_Parity (0) if none
   function First_Cause (Ctx : Cbit_Context) return Bit_Types.Test_Id
   is (if Ctx.Response_Time_Violations /= Ctx.Response_Time_Reported
          or else Ctx.Word_Count_Violations /= Ctx.Word_Count_Reported
       then Bit_Types.Cbit_Test_Protocol
       elsif Ctx.Chan (A).Unhealthy or else Ctx.Chan (B).Unhealthy
       then Bit_Types.Cbit_Test_Channel_Health
       elsif Ctx.Watchdog_Tripped
       then Bit_Types.Cbit_Test_Watchdog
       else Bit_Types.Cbit_Test_Parity)
   with Global => null;


   --  implements: LLR-1553-BIT-002
   --  starts a fresh context; a Watchdog_Period_Us of 0 selects the default
   procedure Init (Ctx : out Cbit_Context; Watchdog_Period_Us : Time_Us)
   with Global  => null,
        Depends => (Ctx => Watchdog_Period_Us),
        Pre     => True,
        Post    =>
          Ctx.Watchdog_Period_Us =
            (if Watchdog_Period_Us = 0 then Watchdog_Period_Default_Us else Watchdog_Period_Us)
          and then not Ctx.Watchdog_Armed
          and then not Ctx.Watchdog_Tripped
          and then Ctx.Watchdog_Trip_Count = 0
          and then Ctx.Response_Time_Violations = 0
          and then Ctx.Word_Count_Violations = 0
          and then not Cycle_Failed (Ctx)
          and then (for all C in Channel =>
                      Ctx.Chan (C).Filled_Count = 0
                      and then Ctx.Chan (C).Good_Count_Total = 0
                      and then Ctx.Chan (C).Error_Count_Total = 0);

   --  implements: LLR-1553-BIT-005
   --  reports the classification of one received word on a channel; valid counts as good, anything else as an error; counted per channel and per
   --  classification (parity, sync, Manchester), and entered in the channel's window of recent words
   procedure Record_Word (Ctx : in out Cbit_Context; Chan : Channel; Class : Word_Class)
   with Global  => null,
        Depends => (Ctx => (Ctx, Chan, Class)),
        Pre     => True,
        Post    =>
          Ctx.Chan (Chan).Class_Count (Class) = Ctx'Old.Chan (Chan).Class_Count (Class) + 1
          and then Ctx.Chan (Chan).Good_Count_Total =
                     Ctx'Old.Chan (Chan).Good_Count_Total + (if Class = Valid then 1 else 0)
          and then Ctx.Chan (Chan).Error_Count_Total =
                     Ctx'Old.Chan (Chan).Error_Count_Total + (if Class = Valid then 0 else 1)
          and then Ctx.Chan (Chan).Outcomes (Ctx'Old.Chan (Chan).Next_Index) = (Class = Valid)
          and then Ctx.Chan (Chan).Next_Index = (Ctx'Old.Chan (Chan).Next_Index + 1) mod Window_Size
          and then Ctx.Chan (Chan).Filled_Count =
                     Natural'Min (Ctx'Old.Chan (Chan).Filled_Count + 1, Window_Size)
          and then (for all C in Channel => (if C /= Chan then Ctx.Chan (C) = Ctx'Old.Chan (C)))
          and then Ctx.Response_Time_Violations = Ctx'Old.Response_Time_Violations
          and then Ctx.Watchdog_Tripped = Ctx'Old.Watchdog_Tripped;


   --  implements: LLR-1553-BIT-005
   --  reports a measured response time; outside [T_Resp_Min_Us, T_Resp_Max_Us] (4 .. 12 us, inclusive) is a violation
   procedure Record_Response_Time (Ctx : in out Cbit_Context; Measured_Us : Time_Us)
   with Global  => null,
        Depends => (Ctx => (Ctx, Measured_Us)),
        Pre     => True,
        Post    =>
          Ctx.Response_Time_Violations =
            Ctx'Old.Response_Time_Violations
            + (if Measured_Us < T_Resp_Min_Us or else Measured_Us > T_Resp_Max_Us then 1 else 0)
          and then Ctx.Word_Count_Violations = Ctx'Old.Word_Count_Violations
          and then Ctx.Chan = Ctx'Old.Chan;

   --  implements: LLR-1553-BIT-005
   procedure Record_Word_Count_Violation (Ctx : in out Cbit_Context)
   with Global  => null,
        Depends => (Ctx => Ctx),
        Pre     => True,
        Post    => Ctx.Word_Count_Violations = Ctx'Old.Word_Count_Violations + 1
                   and then Ctx.Response_Time_Violations = Ctx'Old.Response_Time_Violations
                   and then Ctx.Chan = Ctx'Old.Chan;


   --  implements: LLR-1553-BIT-006
   --  progress mark from the loop being watched; the first call arms the watchdog; it never clears a trip that is already latched
   procedure Watchdog_Kick (Ctx : in out Cbit_Context; Now : Time_Us)
   with Global  => null,
        Depends => (Ctx => (Ctx, Now)),
        Pre     => True,
        Post    =>
          Ctx.Watchdog_Last_Progress = Now
          and then Ctx.Watchdog_Armed
          and then not Ctx.Watchdog_Stalled
          and then Ctx.Watchdog_Tripped = Ctx'Old.Watchdog_Tripped
          and then Ctx.Watchdog_Trip_Count = Ctx'Old.Watchdog_Trip_Count;

   --  implements: LLR-1553-BIT-006
   --  called once per scheduler / service loop iteration; trips (latches) the watchdog if it is armed and the loop has not kicked it for MORE than its
   --  period, a trip is counted once per stall; re-judges both channels' health from their windows
   procedure Tick (Ctx : in out Cbit_Context; Now : Time_Us)
   with Global  => null,
        Depends => (Ctx => (Ctx, Now)),
        Pre     => True,
        Post    =>
          (for all C in Channel => Ctx.Chan (C).Unhealthy = Is_Unhealthy (Ctx.Chan (C)))
          and then Ctx.Watchdog_Tripped =
            (Ctx'Old.Watchdog_Tripped
             or else (Ctx'Old.Watchdog_Armed
                      and then Now >= Ctx'Old.Watchdog_Last_Progress
                      and then Now - Ctx'Old.Watchdog_Last_Progress > Ctx'Old.Watchdog_Period_Us))
          and then Ctx.Response_Time_Violations = Ctx'Old.Response_Time_Violations
          and then Ctx.Word_Count_Violations = Ctx'Old.Word_Count_Violations;

   --  implements: LLR-1553-BIT-005, BIT-006, BIT-008, BIT-009

   --  one CBIT cycle; from Operational or Degraded: decides pass or non-fatal fail from what was observed since the previous call (Cycle_Failed), moves the BIT
   --  state (Operational -> Cbit_Running -> Operational / Degraded, or Degraded -> Operational / Degraded), records the outcome in R.Cbit, and consumes what it
   --  reported: the watchdog latch and the violation counts

   --  in any other state (Power_Up, Pbit_Running, Ibit_Running, Fail) the cycle is not legal: it is ignored and counted in R.Ignored_Event_Count, R.Cbit is
   --  untouched, and NOTHING is consumed - a pending watchdog trip waits for the next legal cycle instead of being lost
   procedure Update_Result (Ctx : in out Cbit_Context; R : in out Bit_Types.Result; Now : Time_Us)
   with Global  => null,
        Depends => (Ctx => (Ctx, R), R => (Ctx, R, Now)),
        Pre     => True,
        Post    =>
          (if R'Old.State in Operational | Degraded then
             R.State = (if Cycle_Failed (Ctx'Old) then Degraded else Operational)
             and then R.Cbit.Ever_Run
             and then R.Cbit.Last_Passed = not Cycle_Failed (Ctx'Old)
             and then R.Cbit.Failed_Mask = Cause_Mask (Ctx'Old)
             and then R.Cbit.Failing_Test_Id = First_Cause (Ctx'Old)
             and then R.Cbit.Timestamp = Now
             and then R.Ignored_Event_Count = R'Old.Ignored_Event_Count
             and then not Ctx.Watchdog_Tripped
             and then Ctx.Response_Time_Reported = Ctx'Old.Response_Time_Violations
             and then Ctx.Word_Count_Reported = Ctx'Old.Word_Count_Violations
           else
             R.State = R'Old.State
             and then R.Cbit = R'Old.Cbit
             and then R.Ignored_Event_Count = R'Old.Ignored_Event_Count + 1
             and then Ctx = Ctx'Old);

end Bit_Cbit;