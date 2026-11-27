--  project:    MIL1553
--  realizes:   LLR-1553-BIT-008 (result structure, BIT word), LLR-1553-BIT-009 (state machine)

--  purpose:    body of Bit_Types: initial result, the event-driven state machine, the fatal report and the BIT word
--  proof status: NOT compiled, NOT proved (no GNAT available to the author)

package body Bit_Types
  with SPARK_Mode => On
is

   --  implements: LLR-1553-BIT-009 (initial state Power_Up, nothing run yet)
   procedure Init (R : out Result) is
   begin
      R := (others => <>);
   end Init;

   --  implements: LLR-1553-BIT-009
   --  a legal event moves the state by the table (Is_Legal / Next_State); anything else changes nothing except the ignored-event count
   procedure Apply (R : in out Result; Ev : Event; Legal : out Boolean) is
   begin
      Legal := Is_Legal (R.State, Ev);

      if Legal then
         R.State := Next_State (R.State, Ev);
      else
         R.Ignored_Event_Count := R.Ignored_Event_Count + 1;
      end if;
   end Apply;

   --  implements: LLR-1553-BIT-008 / BIT-009
   procedure Report_Fatal (R : in out Result; Applied : out Boolean) is
   begin
      Applied := R.State /= Power_Up;

      if Applied then
         R.State := Fail;
      end if;
   end Report_Fatal;

   --  implements: LLR-1553-BIT-008
   --    bit 0 Fail, bit 1 Degraded, bits 2/3 PBIT passed/failed, 4/5 CBIT, 6/7 IBIT, bits 8 .. 10 failing PBIT test id, bits 11 .. 15 zero
   function To_Word (R : Result) return Data_Word is
      W : Data_Word := 0;
   begin
      if R.State = Fail then
         W := W + 1;
      end if;
      if R.State = Degraded then
         W := W + 2;
      end if;

      if R.Pbit.Ever_Run then
         W := W + (if R.Pbit.Last_Passed then 4 else 8);
      end if;
      if R.Cbit.Ever_Run then
         W := W + (if R.Cbit.Last_Passed then 16 else 32);
      end if;
      if R.Ibit.Ever_Run then
         W := W + (if R.Ibit.Last_Passed then 64 else 128);
      end if;

      if R.Pbit.Ever_Run and then not R.Pbit.Last_Passed then
         W := W + Data_Word (R.Pbit.Failing_Test_Id) * 256;
      end if;

      return W;
   end To_Word;

end Bit_Types;