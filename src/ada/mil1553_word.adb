--  project:  MIL1553
--  realizes: LLR-1553-WORD-001 .. LLR-1553-WORD-004, LLR-1553-WORD-008

--  proof status: written to be proved with GNATprove, NOT yet run (the author's environment has no GNAT), the unpack postconditions are modular-arithmetic
--  facts about shifts and masks; they should be provable by the SMT provers but may want an intermediate assertion on the first run

package body Mil1553_Word
  with SPARK_Mode => On
is

   --  word count, LLR-1553-WORD-002
   --  implements: LLR-1553-WORD-002
   function Encode_Word_Count (Count : Valid_Data_Word_Count) return Word_Count_Field is
   begin
      if Count = Max_Data_Words then
         return 0;                              --  32 is carried as 0
      else
         return Word_Count_Field (Count);
      end if;
   end Encode_Word_Count;

   --  implements: LLR-1553-WORD-002
   function Decode_Word_Count (Field : Word_Count_Field) return Valid_Data_Word_Count is
   begin
      if Field = 0 then
         return Max_Data_Words;
      else
         return Natural (Field);
      end if;
   end Decode_Word_Count;

   --  command word, LLR-1553-WORD-001
   --  implements: LLR-1553-WORD-001
   procedure Pack_Command (Fields : Command_Fields; Result : out Symbols) is
      Field   : constant Data_Word := Command_Field_Value (Fields);
      Encoded : Boolean;
   begin
      Manchester.Encode_Word
        (Sync    => Command_Status,
         Field   => Field,
         Parity  => Crc_Parity.Compute_Parity (Field),
         Result  => Result,
         Success => Encoded);
      pragma Assert (Encoded);                  --  Command_Status is a transmittable sync
   end Pack_Command;

   --  implements: LLR-1553-WORD-001
   function Unpack_Command (Field : Data_Word) return Command_Fields is
   begin
      return (Address        => RT_Address (Field / 2 ** 11),
              Direction      => (if (Field / 2 ** 10) mod 2 = 1 then Transmit else Receive),
              Sub            => Subaddress ((Field / 2 ** 5) mod 32),
              Word_Count_Raw => Word_Count_Field (Field mod 32));
   end Unpack_Command;


   --  status word, LLR-1553-WORD-003
   --  implements: LLR-1553-WORD-003
   procedure Pack_Status (Fields : Status_Fields; Result : out Symbols) is
      Field   : constant Data_Word := Status_Field_Value (Fields);   --  reserved bits are zero
      Encoded : Boolean;
   begin
      Manchester.Encode_Word
        (Sync    => Command_Status,
         Field   => Field,
         Parity  => Crc_Parity.Compute_Parity (Field),
         Result  => Result,
         Success => Encoded);
      pragma Assert (Encoded);
   end Pack_Status;

   --  implements: LLR-1553-WORD-003
   function Unpack_Status (Field : Data_Word) return Status_Fields is
   begin
      return (Address            => RT_Address (Field / 2 ** 11),
              Message_Error      => (Field / 2 ** 10) mod 2 = 1,
              Instrumentation    => (Field / 2 ** 9)  mod 2 = 1,
              Service_Request    => (Field / 2 ** 8)  mod 2 = 1,
              Broadcast_Received => (Field / 2 ** 4)  mod 2 = 1,
              Busy               => (Field / 2 ** 3)  mod 2 = 1,
              Subsystem_Flag     => (Field / 2 ** 2)  mod 2 = 1,
              DBC_Acceptance     => (Field / 2 ** 1)  mod 2 = 1,
              Terminal_Flag      => Field mod 2 = 1,
              Reserved_Nonzero   => (Field and 16#00E0#) /= 0);
   end Unpack_Status;


   --  data word, LLR-1553-WORD-004
   --  implements: LLR-1553-WORD-004
   procedure Pack_Data (Value : Data_Word; Result : out Symbols) is
      Encoded : Boolean;
   begin
      Manchester.Encode_Word
        (Sync    => Data,
         Field   => Value,
         Parity  => Crc_Parity.Compute_Parity (Value),
         Result  => Result,
         Success => Encoded);
      pragma Assert (Encoded);                  --  data is a transmittable sync
   end Pack_Data;

   --  implements: LLR-1553-WORD-004
   function Unpack_Data (Field : Data_Word) return Data_Word is
   begin
      return Field;
   end Unpack_Data;


   --  classification, LLR-1553-WORD-008
   --  the single, ordered implementation of the precedence rule; do not re-implement this elsewhere: every caller (RT, BC, BM, CBIT) uses it, so the precedence cannot 
   --  drift between modules

   --  implements: LLR-1553-WORD-008
   function Classify (S : Symbols) return Decoded_Word is
      Sync      : Sync_Kind;
      Field     : Data_Word;
      Parity    : Crc_Parity.Parity_Bit;
      Violation : Boolean;
      Decoded   : Boolean;
      Result    : Decoded_Word;
   begin
      Manchester.Decode_Word (S, Sync, Field, Parity, Violation, Decoded);
      pragma Assert (Decoded = (Sync /= Invalid and then not Violation));

      Result.Sync   := Sync;
      Result.Field  := Field;
      Result.Parity := Parity;

      --  precedence: sync, then Manchester, then parity
      if Sync = Invalid then
         Result.Class := Sync_Error;
      elsif Violation then
         Result.Class := Manchester_Error;
      elsif not Crc_Parity.Check_Parity (Field, Parity) then
         Result.Class := Parity_Error;
      else
         Result.Class := Valid;
      end if;

      return Result;
   end Classify;

end Mil1553_Word;