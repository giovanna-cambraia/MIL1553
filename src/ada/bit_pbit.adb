--  project:    MIL1553

--  realizes:   LLR-1553-BIT-001, LLR-1553-BIT-002, LLR-1553-BIT-003, LLR-1553-BIT-004
--  purpose:    body of Bit_Pbit: the five power-up tests and their combination (port of bit_pbit.c)
--  proof status: NOT compiled, NOT proved (no GNAT available to the author)

with Bus_Model;
with Mil1553_Word;

package body Bit_Pbit
  with SPARK_Mode => On
is

   --  default code/constant region (stands in for e.g. the mode code table); same 32 bytes as the C
   type Default_Rom_Bytes is array (0 .. 31) of Byte;

   Default_Rom : constant Default_Rom_Bytes :=
     (16#1A#, 16#2B#, 16#3C#, 16#4D#, 16#5E#, 16#6F#, 16#70#, 16#81#,
      16#92#, 16#A3#, 16#B4#, 16#C5#, 16#D6#, 16#E7#, 16#F8#, 16#09#,
      16#11#, 16#22#, 16#33#, 16#44#, 16#55#, 16#66#, 16#77#, 16#88#,
      16#99#, 16#AA#, 16#BB#, 16#CC#, 16#DD#, 16#EE#, 16#FF#, 16#00#);

   --  implements: LLR-1553-BIT-002
   function Rom_Checksum (Data : Rom_Bytes; Length : Natural) return Word32 is
      Sum : Word32 := 0;
   begin
      for I in 0 .. Length - 1 loop
         Sum := Sum + Word32 (Data (I));
         Sum := (Sum * 2) or (Sum / 2 ** 31);          --  rotate left by one, cheap mixing
      end loop;
      return Sum;
   end Rom_Checksum;

   --  implements: LLR-1553-BIT-001, LLR-1553-BIT-002
   procedure Init (Ctx : out Pbit_Context) is
   begin
      Ctx := (others => <>);
      for I in Default_Rom'Range loop
         Ctx.Rom (I) := Default_Rom (I);
      end loop;
      Ctx.Rom_Length    := Default_Rom'Length;
      Ctx.Rom_Reference := Rom_Checksum (Ctx.Rom, Ctx.Rom_Length);
      Ctx.Rom_Captured  := True;
   end Init;

   --  implements: LLR-1553-BIT-001
   procedure Set_Memory_Fault (Ctx : in out Pbit_Context; Fault : Memory_Fault) is
   begin
      Ctx.Fault := Fault;
   end Set_Memory_Fault;

   procedure Clear_Memory_Fault (Ctx : in out Pbit_Context) is
   begin
      Ctx.Fault.Active := False;
   end Clear_Memory_Fault;

   --  implements: LLR-1553-BIT-002
   procedure Set_Rom_Region
     (Ctx : in out Pbit_Context; Data : Rom_Bytes; Length : Natural; Ok : out Boolean)
   is
   begin
      Ok := Length in 1 .. Rom_Capacity;
      if Ok then
         Ctx.Rom           := Data;
         Ctx.Rom_Length    := Length;
         Ctx.Rom_Reference := Rom_Checksum (Data, Length);
         Ctx.Rom_Captured  := True;
      end if;
   end Set_Rom_Region;

   procedure Poke_Rom_Byte (Ctx : in out Pbit_Context; Index : Rom_Index; Value : Byte) is
   begin
      Ctx.Rom (Index) := Value;
   end Poke_Rom_Byte;

   --  RAM test accesses: every read and write of the walking-bit test goes through these; with no fault set the access is direct, with a fault set the
   --  one stuck bit reads back stuck whatever was written (the test is falsifiable on a host)
   function Mem_Read (Ctx : Pbit_Context; Offset : Ram_Index) return Byte
   with Global => null
   is
      Value : constant Byte := Ctx.Ram_Region (Offset);
      Mask  : constant Byte := Byte (2) ** Ctx.Fault.Bit_Number;
   begin
      if Ctx.Fault.Active and then Ctx.Fault.Offset = Offset then
         return (if Ctx.Fault.Stuck_High then Value or Mask else Value and not Mask);
      else
         return Value;
      end if;
   end Mem_Read;

   procedure Mem_Write (Ctx : in out Pbit_Context; Offset : Ram_Index; Value : Byte)
   with Global => null,
        Post   => Ctx.Ram_Region (Offset) = Value
                  and then Ctx.Fault = Ctx'Old.Fault
                  and then Ctx.Rom = Ctx'Old.Rom
                  and then Ctx.Rom_Reference = Ctx'Old.Rom_Reference
                  and then Ctx.Rom_Length = Ctx'Old.Rom_Length
                  and then Ctx.Rom_Captured = Ctx'Old.Rom_Captured
   is
   begin
      Ctx.Ram_Region (Offset) := Value;
   end Mem_Write;

   --  implements: LLR-1553-BIT-001
   --  walking ones then walking zeros through every bit of every byte; the original content of each byte is restored whether or not it passed; reports the
   --  offset of the first mismatch
   procedure Check_Ram (Ctx : in out Pbit_Context; Offset : out Ram_Index; Passed : out Boolean)
   with Global => null,
        Post   => Ctx.Fault = Ctx'Old.Fault
                  and then Ctx.Rom = Ctx'Old.Rom
                  and then Ctx.Rom_Reference = Ctx'Old.Rom_Reference
                  and then Ctx.Rom_Length = Ctx'Old.Rom_Length
                  and then Ctx.Rom_Captured = Ctx'Old.Rom_Captured
   is
   begin
      Offset := 0;
      Passed := True;

      for I in Ram_Index loop
         declare
            Saved   : constant Byte := Mem_Read (Ctx, I);
            Byte_Ok : Boolean := True;
         begin
            for Bit in 0 .. 7 loop
               declare
                  Ones  : constant Byte := Byte (2) ** Bit;
                  Zeros : constant Byte := not Ones;
               begin
                  Mem_Write (Ctx, I, Ones);
                  if Mem_Read (Ctx, I) /= Ones then
                     Byte_Ok := False;
                  end if;
                  Mem_Write (Ctx, I, Zeros);
                  if Mem_Read (Ctx, I) /= Zeros then
                     Byte_Ok := False;
                  end if;
               end;
            end loop;

            Mem_Write (Ctx, I, Saved);
            if not Byte_Ok then
               Offset := I;
               Passed := False;
               return;
            end if;
         end;
      end loop;
   end Check_Ram;

   --  implements: LLR-1553-BIT-002
   function Check_Rom (Ctx : Pbit_Context) return Boolean
   is (Ctx.Rom_Captured
       and then Ctx.Rom_Length > 0
       and then Rom_Checksum (Ctx.Rom, Ctx.Rom_Length) = Ctx.Rom_Reference)
   with Global => null;

   --  implements: LLR-1553-BIT-003
   --  command, status and data words round trip, with edge values (pack, classify as valid, unpack, compare)
   function Check_Codec return Boolean
   with Global => null
   is
      type Rt_Cases   is array (0 .. 2) of RT_Address;
      type Data_Cases is array (0 .. 3) of Data_Word;

      Rts   : constant Rt_Cases   := (0, 7, 31);
      Datas : constant Data_Cases := (16#0000#, 16#FFFF#, 16#A5A5#, 16#5A5A#);

      Syms : Symbols := Zero_Symbols;
      D    : Mil1553_Word.Decoded_Word;
   begin
      --  command word: rt address 0, 7, 31; T/R both values; subaddress and word count edges
      for I in 0 .. 2 loop
         declare
            In_F : constant Command_Fields :=
              (Address        => Rts (I),
               Direction      => (if I = 1 then Transmit else Receive),
               Sub            => Subaddress (I * 15),
               Word_Count_Raw => Word_Count_Field (31 - I * 15));
         begin
            Mil1553_Word.Pack_Command (In_F, Syms);
            D := Mil1553_Word.Classify (Syms);
            if D.Class /= Valid then
               return False;
            end if;
            declare
               Out_F : constant Command_Fields := Mil1553_Word.Unpack_Command (D.Field);
            begin
               if Out_F.Address /= In_F.Address
                 or else Out_F.Direction /= In_F.Direction
                 or else Out_F.Sub /= In_F.Sub
                 or else Out_F.Word_Count_Raw /= In_F.Word_Count_Raw
               then
                  return False;
               end if;
            end;
         end;
      end loop;

      --  status word: three cases exercising the latched flags
      for I in 0 .. 2 loop
         declare
            In_F : constant Status_Fields :=
              (Address            => Rts (I),
               Message_Error      => I = 0,
               Instrumentation    => False,
               Service_Request    => False,
               Broadcast_Received => I = 1,
               Busy               => I = 2,
               Subsystem_Flag     => I /= 1,
               DBC_Acceptance     => False,
               Terminal_Flag      => I = 2,
               Reserved_Nonzero   => False);
         begin
            Mil1553_Word.Pack_Status (In_F, Syms);
            D := Mil1553_Word.Classify (Syms);
            if D.Class /= Valid then
               return False;
            end if;
            declare
               Out_F : constant Status_Fields := Mil1553_Word.Unpack_Status (D.Field);
            begin
               if Out_F.Address /= In_F.Address
                 or else Out_F.Message_Error /= In_F.Message_Error
                 or else Out_F.Broadcast_Received /= In_F.Broadcast_Received
                 or else Out_F.Busy /= In_F.Busy
                 or else Out_F.Subsystem_Flag /= In_F.Subsystem_Flag
                 or else Out_F.Terminal_Flag /= In_F.Terminal_Flag
               then
                  return False;
               end if;
            end;
         end;
      end loop;

      --  data word: zeros, ones, and the two alternating patterns
      for I in 0 .. 3 loop
         Mil1553_Word.Pack_Data (Datas (I), Syms);
         D := Mil1553_Word.Classify (Syms);
         if D.Class /= Valid or else Mil1553_Word.Unpack_Data (D.Field) /= Datas (I) then
            return False;
         end if;
      end loop;

      return True;
   end Check_Codec;

   --  implements: LLR-1553-BIT-003
   --  the clock must advance by exactly what it was told, within 1 us
   procedure Check_Timer (Bus : in out Bus_Model_Types.Context; Passed : out Boolean)
   with Global => null,
        Post   => Bus.Chan = Bus'Old.Chan
                  and then Bus.Read_Count = Bus'Old.Read_Count
                  and then Bus.Loopback_Count = Bus'Old.Loopback_Count
   is
      Before : constant Time_Us := Bus_Model.Now_Us (Bus);
   begin
      Bus_Model.Advance_Us (Bus, Timer_Check_Us);
      declare
         After : constant Time_Us := Bus_Model.Now_Us (Bus);
      begin
         if After < Before then
            Passed := False;
         else
            declare
               Elapsed : constant Time_Us := After - Before;
               Diff    : constant Time_Us :=
                 (if Elapsed >= Timer_Check_Us then Elapsed - Timer_Check_Us else Timer_Check_Us - Elapsed);
            begin
               Passed := Diff <= 1;                    --  1 us tolerance, LLR-1553-BIT-001
            end;
         end if;
      end;
   end Check_Timer;

   --  implements: LLR-1553-BIT-004
   --  listens on both channels for the whole period; Busy_Channel is the first channel a word was heard on (0 = A, 1 = B), 0 if none
   procedure Check_Bus_Idle
     (Bus          : in out Bus_Model_Types.Context;
      Listener     : Terminal_Id;
      Busy_Channel : out Natural;
      Idle         : out Boolean)
   with Global => null,
        Post   => Busy_Channel <= 1
                  and then Bus.Chan = Bus'Old.Chan
                  and then (for all C in Channel =>
                              (for all T in Terminal_Id =>
                                 (if T /= Listener then
                                    Bus.Read_Count (C, T) = Bus'Old.Read_Count (C, T)
                                    and then Bus.Loopback_Count (C, T) = Bus'Old.Loopback_Count (C, T))))
   is
      Rec : Bus_Model_Types.Bus_Record;
      Got : Boolean;
   begin
      Busy_Channel := 0;
      Idle         := True;

      for Step in 1 .. Idle_Listen_Steps loop
         pragma Loop_Invariant (Busy_Channel <= 1);
         pragma Loop_Invariant (Bus.Chan = Bus'Loop_Entry.Chan);
         pragma Loop_Invariant
           (for all C in Channel =>
              (for all T in Terminal_Id =>
                 (if T /= Listener then
                    Bus.Read_Count (C, T) = Bus'Loop_Entry.Read_Count (C, T)
                    and then Bus.Loopback_Count (C, T) = Bus'Loop_Entry.Loopback_Count (C, T))));

         Bus_Model.Receive (Bus, A, Listener, Rec, Got);
         if Got then
            if Idle then
               Busy_Channel := 0;
            end if;
            Idle := False;
         end if;

         Bus_Model.Receive (Bus, B, Listener, Rec, Got);
         if Got then
            if Idle then
               Busy_Channel := 1;
            end if;
            Idle := False;
         end if;

         Bus_Model.Advance_Us (Bus, Idle_Listen_Step_Us);
      end loop;
   end Check_Bus_Idle;

   --  implements: LLR-1553-BIT-001, LLR-1553-BIT-004
   procedure Run
     (Ctx      : in out Pbit_Context;
      R        : in out Bit_Types.Result;
      Bus      : in out Bus_Model_Types.Context;
      Listener : Terminal_Id;
      Passed   : out Boolean)
   is
      Legal        : Boolean;
      Mask         : Bit_Types.Failure_Mask := 0;
      Ram_Offset   : Ram_Index;
      Ram_Ok       : Boolean;
      Timer_Ok     : Boolean;
      Idle_Channel : Natural;
      Idle_Ok      : Boolean;
      First_Id     : Bit_Types.Test_Id := 0;
      First_Item   : Bit_Types.Item_Number := 0;
   begin
      Bit_Types.Apply (R, Bit_Types.Pbit_Start, Legal);
      if not Legal then
         Passed := False;                          --  PBIT only runs from Power_Up; the event is counted by Apply
         return;
      end if;

      --  all five tests run unconditionally; each failure sets a bit of Mask, so Failed_Mask reports every test that failed, not just the first
      Check_Ram (Ctx, Ram_Offset, Ram_Ok);
      if not Ram_Ok then
         Mask := Mask or Bit_Types.Mask_Of (Bit_Types.Pbit_Test_Ram);
      end if;

      if not Check_Rom (Ctx) then
         Mask := Mask or Bit_Types.Mask_Of (Bit_Types.Pbit_Test_Rom);
      end if;

      if not Check_Codec then
         Mask := Mask or Bit_Types.Mask_Of (Bit_Types.Pbit_Test_Codec);
      end if;

      Check_Timer (Bus, Timer_Ok);
      if not Timer_Ok then
         Mask := Mask or Bit_Types.Mask_Of (Bit_Types.Pbit_Test_Timer);
      end if;

      Check_Bus_Idle (Bus, Listener, Idle_Channel, Idle_Ok);
      if not Idle_Ok then
         Mask := Mask or Bit_Types.Mask_Of (Bit_Types.Pbit_Test_Bus_Idle);
      end if;

      Passed := Mask = 0;

      if not Passed then
         --  the first (lowest-numbered) failed test
         for T in Bit_Types.Test_Id range 0 .. 4 loop
            pragma Loop_Invariant
              (for all U in Bit_Types.Test_Id => (if U < T then (Mask and Bit_Types.Mask_Of (U)) = 0));
            if (Mask and Bit_Types.Mask_Of (T)) /= 0 then
               First_Id := T;
               exit;
            end if;
         end loop;

         if First_Id = Bit_Types.Pbit_Test_Ram then
            First_Item := Bit_Types.Item_Number (Ram_Offset);
         elsif First_Id = Bit_Types.Pbit_Test_Bus_Idle then
            First_Item := Bit_Types.Item_Number (Idle_Channel);
         end if;
      end if;

      R.Pbit := (Ever_Run        => True,
                 Last_Passed     => Passed,
                 Failing_Test_Id => First_Id,
                 Failing_Item    => First_Item,
                 Failed_Mask     => Mask,
                 Timestamp       => Bus_Model.Now_Us (Bus));

      Bit_Types.Apply (R, (if Passed then Bit_Types.Pbit_Pass else Bit_Types.Pbit_Fail), Legal);
   end Run;

end Bit_Pbit;