
--  project:    MIL1553
--  purpose:    command, status and data word packing and unpacking, the word count encoding, and the single ordered classification of a received word used by every 
--  module that receives bus traffic

--  realizes:   LLR-1553-WORD-001 .. LLR-1553-WORD-004, LLR-1553-WORD-008
--  parent HLR: HLR-1553-WORD-001/002/003/005

--  information field layout (16 bits, bit 15 = most significant):
--    Command : [15..11 address][10 T/R][9..5 subaddress][4..0 word count / mode code]
--    Status  : [15..11 address][10 ME][9 INSTR][8 SR][7..5 reserved]
--              [4 BCR][3 BUSY][2 SF][1 DBCA][0 TF]
--    Data    : [15..0 value]

--  how this differs from the C interface (mil1553_word.h), and why
--    * the C pack functions reject out-of-range fields at run time (address 32, subaddress 32, ...), here Command_Fields / Status_Fields are built from  5-bit types, 
--      so an out-of-range field cannot be constructed: the check moves from run time to the type system, and Pack_Command / Pack_Status cannot fail;
--      what they promise instead is stated in their contracts
--    * likewise Encode_Word_Count takes a Valid_Data_Word_Count (1 .. 32); the C rejection of 0 and of counts over 32 becomes a precondition on the caller; Is_Valid_Count 
--      is provided for code that reads such a count from configuration and has to check it first (e.g. schedule validation)
--    * Classify (LLR-1553-WORD-008) stays the ONE place the precedence rule lives: sync, then Manchester, then parity

with Mil1553_Types; use Mil1553_Types;
with Crc_Parity;
with Manchester;

package Mil1553_Word
  with SPARK_Mode => On
is

   --  field values (pure functions, visible to the provers)
   function To_Bit (Flag : Boolean) return Data_Word is
     (if Flag then 1 else 0);

   --  implements: LLR-1553-WORD-001; the 16-bit information field of a command word
   function Command_Field_Value (Fields : Command_Fields) return Data_Word is
     (Data_Word (Fields.Address)                 * 2 ** 11
      + Data_Word (TR_Bit'Pos (Fields.Direction)) * 2 ** 10
      + Data_Word (Fields.Sub)                   * 2 ** 5
      + Data_Word (Fields.Word_Count_Raw))
   with Global => null;

   --  implements: LLR-1553-WORD-003; the 16-bit information field of a status word; the reserved bits (7..5) are always zero
   function Status_Field_Value (Fields : Status_Fields) return Data_Word is
     (Data_Word (Fields.Address)           * 2 ** 11
      + To_Bit (Fields.Message_Error)      * 2 ** 10
      + To_Bit (Fields.Instrumentation)    * 2 ** 9
      + To_Bit (Fields.Service_Request)    * 2 ** 8
      + To_Bit (Fields.Broadcast_Received) * 2 ** 4
      + To_Bit (Fields.Busy)               * 2 ** 3
      + To_Bit (Fields.Subsystem_Flag)     * 2 ** 2
      + To_Bit (Fields.DBC_Acceptance)     * 2 ** 1
      + To_Bit (Fields.Terminal_Flag))
   with Global => null;

   --  word count encoding, LLR-1553-WORD-002
   function Is_Valid_Count (Count : Natural) return Boolean is
     (Count in Valid_Data_Word_Count)
   with Global => null;

   --  implements: LLR-1553-WORD-002. 1 .. 31 encode as themselves, 32 as 0
   function Encode_Word_Count (Count : Valid_Data_Word_Count) return Word_Count_Field
   with Global  => null,
        Depends => (Encode_Word_Count'Result => Count),
        Pre     => True,
        Post    => Encode_Word_Count'Result = (if Count = Max_Data_Words then 0
                                               else Word_Count_Field (Count));

   --  implements: LLR-1553-WORD-002. Total: every field value 0 .. 31 decodes (0 means 32 words)
   function Decode_Word_Count (Field : Word_Count_Field) return Valid_Data_Word_Count
   with Global  => null,
        Depends => (Decode_Word_Count'Result => Field),
        Pre     => True,
        Post    => Decode_Word_Count'Result = (if Field = 0 then Max_Data_Words else Natural (Field));

   --  ghost: "these symbols are the encoding of this field with this sync and the correct parity"
   function Encodes (S : Symbols; Sync : Sync_Kind; Field : Data_Word) return Boolean is
     (for all I in Symbol_Index =>
        Manchester.Symbol_At (S, I)
          = Manchester.Expected_Symbol (Sync, Field, Crc_Parity.Compute_Parity (Field), I))
   with Ghost, Global => null;

   --  command word, LLR-1553-WORD-001
   --  packs Fields into a command word: command/status sync, the information field, and its odd parity bit; cannot fail (the field types are in range)
   procedure Pack_Command (Fields : Command_Fields; Result : out Symbols)
   with Global  => null,
        Depends => (Result => Fields),
        Pre     => True,
        Post    => Encodes (Result, Command_Status, Command_Field_Value (Fields));

   --  Every 16-bit pattern is some command word: it always unpacks, and packing
   --  what came out gives the same field back.
   function Unpack_Command (Field : Data_Word) return Command_Fields
   with Global  => null,
        Depends => (Unpack_Command'Result => Field),
        Pre     => True,
        Post    => Command_Field_Value (Unpack_Command'Result) = Field;

   ---------------------------------------------------------------------------
   --  Status word, LLR-1553-WORD-003
   ---------------------------------------------------------------------------

   --  Packs Fields into a status word with the reserved bits zero.
   procedure Pack_Status (Fields : Status_Fields; Result : out Symbols)
   with Global  => null,
        Depends => (Result => Fields),
        Pre     => True,
        Post    => Encodes (Result, Command_Status, Status_Field_Value (Fields));

   --  Unpacks a status field. Reserved_Nonzero reports whether any reserved bit
   --  (7..5) was set; the other fields are decoded regardless.
   function Unpack_Status (Field : Data_Word) return Status_Fields
   with Global  => null,
        Depends => (Unpack_Status'Result => Field),
        Pre     => True,
        Post    => Status_Field_Value (Unpack_Status'Result) = (Field and 16#FF1F#)
                   and then Unpack_Status'Result.Reserved_Nonzero = ((Field and 16#00E0#) /= 0);

   --  data word, LLR-1553-WORD-004
   --  packs a 16-bit value with data sync and parity; cannot fail
   procedure Pack_Data (Value : Data_Word; Result : out Symbols)
   with Global  => null,
        Depends => (Result => Value),
        Pre     => True,
        Post    => Encodes (Result, Data, Value);

   --  bit-exact transparency
   function Unpack_Data (Field : Data_Word) return Data_Word
   with Global  => null,
        Depends => (Unpack_Data'Result => Field),
        Pre     => True,
        Post    => Unpack_Data'Result = Field;

   --  classification, LLR-1553-WORD-008
   type Decoded_Word is record
      Class  : Word_Class            := Valid;
      Sync   : Sync_Kind             := Invalid;
      Field  : Data_Word             := 0;
      Parity : Crc_Parity.Parity_Bit := 0;
   end record;

   --  classifies a received word as exactly one of Valid, Sync_Error, Manchester_Error, Parity_Error, tested in that order:
   --    1. the sync must be Command_Status or Data, else Sync_Error;
   --    2. no bit cell may lack its mid-bit transition, else Manchester_Error;
   --    3. the parity must check, else Parity_Error.
   --  Sync, Field and Parity are filled in for every class (best effort, as Manchester.Decode_Word) but only mean something when Class = Valid
   function Classify (S : Symbols) return Decoded_Word
   with Global  => null,
        Depends => (Classify'Result => S),
        Pre     => True,
        Post    =>
          Classify'Result.Sync = (if Manchester.Sync_Matches (S, Command_Status) then Command_Status
                                  elsif Manchester.Sync_Matches (S, Data)         then Data
                                  else Invalid)
          and then Classify'Result.Parity = Manchester.Cell_Value (S, 16)
          and then (for all N in 0 .. 15 =>
                      Crc_Parity.Bit_Of (Classify'Result.Field, 15 - N) = Manchester.Cell_Value (S, N))
          and then Classify'Result.Class =
                     (if Classify'Result.Sync = Invalid then Sync_Error
                      elsif (for some N in Manchester.Cell_Index => Manchester.Cell_Violation (S, N))
                        then Manchester_Error
                      elsif not Crc_Parity.Check_Parity (Classify'Result.Field, Classify'Result.Parity)
                        then Parity_Error
                      else Valid);

end Mil1553_Word;