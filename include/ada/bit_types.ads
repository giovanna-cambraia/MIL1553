--  project:    MIL1553
--  purpose:    the shared BIT result structure and the BIT state machine; written by Bit_Pbit, Bit_Cbit and Bit_Ibit; read by Rt_Service (TF/SF and
--              the BIT word) and the health monitor; one structure, one state machine, three read-only views (SDD 4.9)

--  realizes:   LLR-1553-BIT-008 (result structure), LLR-1553-BIT-009 (states)
--  parent HLRs: HLR-1553-BIT-001, HLR-1553-BIT-004
--
--  the transition table of LLR-1553-BIT-009 is written ONCE, as two expression functions (Is_Legal, Next_State). The contract of Apply is stated in terms of
--  them, so the table is both the implementation and the specification, and a reviewer reads it in one place

--  the legal transitions
--    Power_Up      --Pbit_Start-->    Pbit_Running
--    Pbit_Running  --Pbit_Pass-->     Operational      --Pbit_Fail--> Fail
--    Operational   --Cbit_Begin-->    Cbit_Running     --Ibit_Begin--> Ibit_Running
--    Cbit_Running  --Cbit_Pass-->     Operational      --Cbit_Nonfatal--> Degraded
--    Ibit_Running  --Ibit_Pass-->     Operational      --Ibit_Nonfatal--> Degraded
--    Degraded      --Cbit_Pass-->     Operational      --Cbit_Nonfatal--> Degraded
--    any except Power_Up --Fatal-->   Fail

--  anything else is ignored: the state does not change and the event is counted; fail is a dead end - nothing leaves it

with Mil1553_Types; use Mil1553_Types;

package Bit_Types
  with SPARK_Mode => On
is

   --  Test identifiers within each level (LLR-1553-BIT-001/002/003/005/006/007/008)

   type Test_Id is range 0 .. 7;

   Pbit_Test_Ram       : constant Test_Id := 0;
   Pbit_Test_Rom       : constant Test_Id := 1;
   Pbit_Test_Codec     : constant Test_Id := 2;
   Pbit_Test_Timer     : constant Test_Id := 3;
   Pbit_Test_Bus_Idle  : constant Test_Id := 4;

   Cbit_Test_Parity         : constant Test_Id := 0;
   Cbit_Test_Protocol       : constant Test_Id := 1;
   Cbit_Test_Channel_Health : constant Test_Id := 2;
   Cbit_Test_Watchdog       : constant Test_Id := 3;

   Ibit_Test_Loopback_A     : constant Test_Id := 0;
   Ibit_Test_Loopback_B     : constant Test_Id := 1;
   Ibit_Test_Cross_Channel  : constant Test_Id := 2;

   --  bit n set = test id n failed in the last run of a level
   type Failure_Mask is mod 2 ** 32;

   function Mask_Of (Id : Test_Id) return Failure_Mask
   is (Failure_Mask (2) ** Natural (Id))
   with Global => null;

   type Item_Number is mod 2 ** 32;
   type Event_Count is mod 2 ** 32;

   --  the result structure, LLR-1553-BIT-008

   --  the latest outcome of one level
   type Level_Result is record
      Ever_Run        : Boolean      := False;
      Last_Passed     : Boolean      := False;
      Failing_Test_Id : Test_Id      := 0;       --  first failing test of the last run; only if not Last_Passed
      Failing_Item    : Item_Number  := 0;       --  what failed: RAM offset (PBIT RAM), channel 0/1 (bus idle, IBIT loopback)
      Failed_Mask     : Failure_Mask := 0;
      Timestamp       : Time_Us      := 0;
   end record;

   type Result is record
      Pbit                : Level_Result;
      Cbit                : Level_Result;
      Ibit                : Level_Result;
      State               : BIT_State   := Power_Up;     --  LLR-1553-BIT-009
      Ignored_Event_Count : Event_Count := 0;            --  events not legal in the current state
   end record;

   --  events that drive the state machine

   type Event is
     (Pbit_Start, Pbit_Pass, Pbit_Fail,
      Cbit_Begin, Cbit_Pass, Cbit_Nonfatal,
      Ibit_Begin, Ibit_Pass, Ibit_Nonfatal,
      Fatal);

   --  the transition table (LLR-1553-BIT-009)

   function Is_Legal (From : BIT_State; Ev : Event) return Boolean
   is (if Ev = Fatal then From /= Power_Up
       else
         (case From is
            when Power_Up     => Ev = Pbit_Start,
            when Pbit_Running => Ev in Pbit_Pass | Pbit_Fail,
            when Operational  => Ev in Cbit_Begin | Ibit_Begin,
            when Cbit_Running => Ev in Cbit_Pass | Cbit_Nonfatal,
            when Ibit_Running => Ev in Ibit_Pass | Ibit_Nonfatal,
            when Degraded     => Ev in Cbit_Pass | Cbit_Nonfatal,
            when Fail         => False))
   with Global => null;

   function Next_State (From : BIT_State; Ev : Event) return BIT_State
   is (if Ev = Fatal then Fail
       else
         (case From is
            when Power_Up     => Pbit_Running,
            when Pbit_Running => (if Ev = Pbit_Pass     then Operational else Fail),
            when Operational  => (if Ev = Cbit_Begin    then Cbit_Running else Ibit_Running),
            when Cbit_Running => (if Ev = Cbit_Pass     then Operational else Degraded),
            when Ibit_Running => (if Ev = Ibit_Pass     then Operational else Degraded),
            when Degraded     => (if Ev = Cbit_Pass     then Operational else Degraded),
            when Fail         => Fail))
   with Global => null,
        Pre    => Is_Legal (From, Ev);

   --  operations

   --  implements: LLR-1553-BIT-009 (initial state Power_Up, nothing run yet)
   procedure Init (R : out Result)
   with Global  => null,
        Depends => (R => null),
        Pre     => True,
        Post    => R.State = Power_Up
                   and then R.Ignored_Event_Count = 0
                   and then not R.Pbit.Ever_Run
                   and then not R.Cbit.Ever_Run
                   and then not R.Ibit.Ever_Run;

   --  implements: LLR-1553-BIT-009
   --  applies one event; legal is True exactly when the transition is in the table; then the state becomes Next_State, otherwise nothing changes except
   --  that the event is counted in Ignored_Event_Count
   procedure Apply (R : in out Result; Ev : Event; Legal : out Boolean)
   with Global  => null,
        Depends => (R => (R, Ev), Legal => (R, Ev)),
        Pre     => True,
        Post    =>
          Legal = Is_Legal (R'Old.State, Ev)
          and then R.Pbit = R'Old.Pbit
          and then R.Cbit = R'Old.Cbit
          and then R.Ibit = R'Old.Ibit
          and then (if Legal then
                      R.State = Next_State (R'Old.State, Ev)
                      and then R.Ignored_Event_Count = R'Old.Ignored_Event_Count
                    else
                      R.State = R'Old.State
                      and then R.Ignored_Event_Count = R'Old.Ignored_Event_Count + 1);

   --  implements: LLR-1553-BIT-008 / BIT-009; a fatal condition found outside the BIT modules (e.g. by the health monitor); applied is False in Power_Up
   procedure Report_Fatal (R : in out Result; Applied : out Boolean)
   with Global  => null,
        Depends => (R => R, Applied => R),
        Pre     => True,
        Post    => Applied = (R'Old.State /= Power_Up)
                   and then (if Applied then R.State = Fail);

   --  implements: LLR-1553-BIT-008 (read-only views)
   function Is_Fatal (R : Result) return Boolean is (R.State = Fail)
   with Global => null;

   function Is_Degraded (R : Result) return Boolean is (R.State = Degraded)
   with Global => null;

   ---------------------------------------------------------------------------
   --  Implements: LLR-1553-BIT-008
   --
   --  The BIT word returned by the transmit-BIT-word mode command, built from the
   --  result structure:
   --    bit 0  state is Fail          bit 1  state is Degraded
   --    bit 2  PBIT ran and passed    bit 3  PBIT failed
   --    bit 4  CBIT ran and passed    bit 5  CBIT failed
   --    bit 6  IBIT ran and passed    bit 7  IBIT failed
   --    bits 8..10  failing PBIT test id (0 unless PBIT has failed)
   --    bits 11..15 zero
   ---------------------------------------------------------------------------
   function To_Word (R : Result) return Data_Word
   with Global  => null,
        Depends => (To_Word'Result => R),
        Pre     => True,
        Post    =>
          (To_Word'Result mod 2 = 1)               = (R.State = Fail)
          and then ((To_Word'Result / 2) mod 2 = 1) = (R.State = Degraded)
          and then ((To_Word'Result / 4) mod 2 = 1)  = (R.Pbit.Ever_Run and then R.Pbit.Last_Passed)
          and then ((To_Word'Result / 8) mod 2 = 1)  = (R.Pbit.Ever_Run and then not R.Pbit.Last_Passed)
          and then ((To_Word'Result / 16) mod 2 = 1) = (R.Cbit.Ever_Run and then R.Cbit.Last_Passed)
          and then ((To_Word'Result / 32) mod 2 = 1) = (R.Cbit.Ever_Run and then not R.Cbit.Last_Passed)
          and then ((To_Word'Result / 64) mod 2 = 1) = (R.Ibit.Ever_Run and then R.Ibit.Last_Passed)
          and then ((To_Word'Result / 128) mod 2 = 1) = (R.Ibit.Ever_Run and then not R.Ibit.Last_Passed)
          and then (To_Word'Result / 256) mod 8 =
                     (if R.Pbit.Ever_Run and then not R.Pbit.Last_Passed
                      then Data_Word (R.Pbit.Failing_Test_Id) else 0)
          and then To_Word'Result / 2048 = 0;

end Bit_Types;