--  project:    MIL1553
--  purpose:    initiated built-in test: full loopback on both channels, cross-channel comparison, known-pattern injection with echo verification
--  realizes:   LLR-1553-BIT-007 (and the IBIT side of LLR-1553-BIT-009)
--  parent HLR: HLR-1553-BIT-003

--  isolation from other terminals (LLR-1553-BIT-007): IBIT reads its own pattern back through the dedicated loopback cursor (Bus_Model.Receive_Loopback), which
--  never disturbs a terminal's normal receive position; the pattern is sent as a DATA-sync word, and Rt_Service ignores a valid word whose sync is not
--  Command_Status when looking for commands, so other RTs do not read it as one

--  differences from the C interface (bit_ibit.h), and why
--    * Run takes the result, the bus and the terminal as parameters, and returns Passed (the C returns the bool and takes pointers)
--    * the pattern is Data_Word, the C mil1553_data_t

--  IBIT may only start from Operational (Operational -> Ibit_Running); from any other state nothing is sent, the request is counted as an ignored event and
--  Passed is False; in particular IBIT can never clear a Fail

with Mil1553_Types; use Mil1553_Types;
with Bus_Model_Types;
with Bit_Types;

package Bit_Ibit
  with SPARK_Mode => On
is
   use type Bit_Types.Failure_Mask;
   use type Bit_Types.Test_Id;
   use type Bit_Types.Event_Count;
   use type Bit_Types.Item_Number;
   use type Bit_Types.Level_Result;
   use type Bus_Model_Types.Counter;
   use type Bus_Model_Types.Context;

   Pattern_Value : constant Data_Word := 16#5AA5#;     --  known pattern, LLR-1553-BIT-007

   --  implements: LLR-1553-BIT-007, LLR-1553-BIT-009
   --  runs the IBIT sequence: sends the pattern as a data word on channel A and reads it back (Ibit_Test_Loopback_A), the same on channel B
   --  (Ibit_Test_Loopback_B), then checks the two channels agree (Ibit_Test_Cross_Channel); all three are run and combined in R.Ibit.Failed_Mask;
   --  Failing_Item is the first failed channel (0 = A, 1 = B), Failing_Test_Id the first (lowest) failed test, both 0 on a pass

   --  outcome: both loopbacks fail -> Fail (the terminal can no longer test itself); exactly one fails, or the channels disagree -> Degraded; everything
   --  passes -> back to Operational. The cross-channel test also counts as failed whenever either loopback failed (as in the C)

   --  only the bus ring log and this terminal's loopback cursor are touched: no read cursor, subaddress buffer, schedule or other terminal state
   procedure Run
     (R        : in out Bit_Types.Result;
      Bus      : in out Bus_Model_Types.Context;
      Terminal : Terminal_Id;
      Passed   : out Boolean)
   with Global  => null,
        Depends => (R => (R, Bus), Bus => (R, Bus, Terminal), Passed => (R, Bus)),
        Pre     => True,
        Post    =>
          (if R'Old.State /= Operational then
             not Passed
             and then R.State = R'Old.State
             and then R.Ignored_Event_Count = R'Old.Ignored_Event_Count + 1
             and then R.Ibit = R'Old.Ibit
             and then Bus = Bus'Old
           else
             R.Ibit.Ever_Run
             and then R.Ibit.Last_Passed = Passed
             and then Passed = (R.Ibit.Failed_Mask = 0)
             and then R.Ibit.Failed_Mask < 8                          --  only the three IBIT tests
             and then R.Ibit.Timestamp = Bus.Clock
             and then R.Pbit = R'Old.Pbit
             and then R.Cbit = R'Old.Cbit
             and then R.State =
                        (if Passed then Operational
                         elsif (R.Ibit.Failed_Mask and Bit_Types.Mask_Of (Bit_Types.Ibit_Test_Loopback_A)) /= 0
                           and then (R.Ibit.Failed_Mask and Bit_Types.Mask_Of (Bit_Types.Ibit_Test_Loopback_B)) /= 0
                         then Fail
                         else Degraded)
             and then (if Passed then R.Ibit.Failing_Test_Id = 0 and then R.Ibit.Failing_Item = 0
                       else (R.Ibit.Failed_Mask and Bit_Types.Mask_Of (R.Ibit.Failing_Test_Id)) /= 0
                            and then (for all T in Bit_Types.Test_Id =>
                                        (if T < R.Ibit.Failing_Test_Id
                                         then (R.Ibit.Failed_Mask and Bit_Types.Mask_Of (T)) = 0))
                            and then R.Ibit.Failing_Item =
                                       (if (R.Ibit.Failed_Mask
                                            and Bit_Types.Mask_Of (Bit_Types.Ibit_Test_Loopback_A)) /= 0
                                        then 0 else 1)))
          and then (for all C in Channel =>
                      (for all T in Terminal_Id =>
                         Bus.Read_Count (C, T) = Bus'Old.Read_Count (C, T)));

end Bit_Ibit;