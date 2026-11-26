--  project:    MIL1553

--  realizes:   LLR-1553-BIT-007 (and the IBIT side of LLR-1553-BIT-009)
--  purpose:    body of Bit_Ibit: loopback on both channels, cross-channel comparison, outcome mapping (port of bit_ibit.c)
--  proof status: NOT compiled, NOT proved (no GNAT available to the author)

with Bus_Model;
with Mil1553_Word;

package body Bit_Ibit
  with SPARK_Mode => On
is

   --  sends the pattern as a data word on Chan and reads it back through the loopback cursor; Ok is True only when the word was sent, came back, classified
   --  as valid and equals the pattern. Value is what came back (0 if nothing valid did)
   procedure Loopback_Channel
     (Bus      : in out Bus_Model_Types.Context;
      Chan     : Channel;
      Terminal : Terminal_Id;
      Value    : out Data_Word;
      Ok       : out Boolean)
   with Global => null,
        Post   => Bus.Read_Count = Bus'Old.Read_Count
   is
      Syms : Symbols := Zero_Symbols;
      Rec  : Bus_Model_Types.Bus_Record;
      Sent : Boolean;
      Got  : Boolean;
   begin
      Value := 0;
      Ok    := False;

      Mil1553_Word.Pack_Data (Pattern_Value, Syms);

      Bus_Model.Transmit (Bus, Chan, Syms, Terminal, Sent);
      if not Sent then
         return;                                   --  channel failed, cannot even attempt loopback
      end if;
      Bus_Model.Advance_Us (Bus, Word_Time_Us);

      Bus_Model.Receive_Loopback (Bus, Chan, Terminal, Rec, Got);
      if not Got then
         return;
      end if;

      declare
         Decoded : constant Mil1553_Word.Decoded_Word := Mil1553_Word.Classify (Rec.Syms);
      begin
         if Decoded.Class /= Valid then
            return;
         end if;
         Value := Mil1553_Word.Unpack_Data (Decoded.Field);
         Ok    := Value = Pattern_Value;
      end;
   end Loopback_Channel;

   --  implements: LLR-1553-BIT-007, LLR-1553-BIT-009
   procedure Run
     (R        : in out Bit_Types.Result;
      Bus      : in out Bus_Model_Types.Context;
      Terminal : Terminal_Id;
      Passed   : out Boolean)
   is
      Legal      : Boolean;
      Value_A    : Data_Word;
      Value_B    : Data_Word;
      Ok_A       : Boolean;
      Ok_B       : Boolean;
      Mask       : Bit_Types.Failure_Mask := 0;
      First_Id   : Bit_Types.Test_Id := 0;
      First_Item : Bit_Types.Item_Number := 0;
   begin
      --  IBIT only starts from Operational; from any other state the event is counted as ignored by Apply and nothing is transmitted
      Bit_Types.Apply (R, Bit_Types.Ibit_Begin, Legal);
      if not Legal then
         Passed := False;
         return;
      end if;

      Loopback_Channel (Bus, A, Terminal, Value_A, Ok_A);
      Loopback_Channel (Bus, B, Terminal, Value_B, Ok_B);

      --  all three tests run unconditionally; each failure contributes a bit of Mask, so Failed_Mask reports every test that failed, not just the first
      if not Ok_A then
         Mask := Mask or Bit_Types.Mask_Of (Bit_Types.Ibit_Test_Loopback_A);
      end if;
      if not Ok_B then
         Mask := Mask or Bit_Types.Mask_Of (Bit_Types.Ibit_Test_Loopback_B);
      end if;
      if not (Ok_A and then Ok_B and then Value_A = Value_B) then
         Mask := Mask or Bit_Types.Mask_Of (Bit_Types.Ibit_Test_Cross_Channel);
      end if;

      Passed := Mask = 0;

      if not Passed then
         --  the first (lowest-numbered) failed test; the failing item is the first failed channel (0 = A, 1 = B)
         for T in Bit_Types.Test_Id range 0 .. 2 loop
            pragma Loop_Invariant
              (for all U in Bit_Types.Test_Id => (if U < T then (Mask and Bit_Types.Mask_Of (U)) = 0));
            if (Mask and Bit_Types.Mask_Of (T)) /= 0 then
               First_Id := T;
               exit;
            end if;
         end loop;
         First_Item := (if not Ok_A then 0 else 1);
      end if;

      R.Ibit := (Ever_Run        => True,
                 Last_Passed     => Passed,
                 Failing_Test_Id => First_Id,
                 Failing_Item    => First_Item,
                 Failed_Mask     => Mask,
                 Timestamp       => Bus_Model.Now_Us (Bus));

      --  outcome: pass -> back to Operational; both channels lost -> Fail (the terminal can no longer test itself); exactly one channel failed, or the
      --  cross-channel check disagrees -> Degraded
      if Passed then
         Bit_Types.Apply (R, Bit_Types.Ibit_Pass, Legal);
      elsif not Ok_A and then not Ok_B then
         Bit_Types.Apply (R, Bit_Types.Fatal, Legal);
      else
         Bit_Types.Apply (R, Bit_Types.Ibit_Nonfatal, Legal);
      end if;
   end Run;

end Bit_Ibit;