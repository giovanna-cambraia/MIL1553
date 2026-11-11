--  project:    MIL1553
--  purpose:    manchester II bi-phase encoding and decoding of a whole word (sync + 16-bit field + parity bit) to and from 40 packed half-bit symbols

--  realizes:   LLR-1553-WORD-006, LLR-1553-WORD-007
--  parent HLR: HLR-1553-WORD-004

--  symbol layout (LLR-1553-WORD-006)
--    * command/status sync:  1 1 1 0 0 0
--    * data sync:            0 0 0 1 1 1
--    * each of the 17 field+parity bits, field MSB first and parity last, is a pair of symbols: a one is 1 0, a zero is 0 1
--    * 6 + 17 * 2 = 40 symbols, packed 8 per byte with symbol 0 in the most significant bit of byte 0 (design/bus_transport_contract.md 4) - the same packing as the
--    C implementation

--  the contracts below state exactly that layout, through GHOST functions that exist only for proof

with Mil1553_Types; use Mil1553_Types;
with Crc_Parity;

package Manchester
  with SPARK_Mode => On
is

   Sync_Symbol_Count : constant := 6;
   Bit_Cell_Count    : constant := 17;     --  16 field bits + 1 parity bit

   --  cell 0 is the field's most significant bit, cell 15 its least significant, cell 16 the parity bit
   subtype Cell_Index is Natural range 0 .. Bit_Cell_Count - 1;

   --  ghost specification of the symbol layout
   --  symbol number I of S, as 0 or 1
   function Symbol_At (S : Symbols; I : Symbol_Index) return Natural is
     (Natural ((S (I / 8) / (2 ** (7 - (I mod 8)))) mod 2))
   with Ghost,
        Global => null,
        Post   => Symbol_At'Result <= 1;

   --  the logical value of bit cell N of a word: a field bit, or the parity bit
   function Cell_Bit (Field : Data_Word; Parity : Crc_Parity.Parity_Bit; N : Cell_Index) return Natural is
     (if N < 16 then Crc_Parity.Bit_Of (Field, 15 - N) else Parity)
   with Ghost,
        Global => null,
        Post   => Cell_Bit'Result <= 1;

   --  the symbol the encoder must produce at position I
   function Expected_Symbol
     (Sync   : Sync_Kind;
      Field  : Data_Word;
      Parity : Crc_Parity.Parity_Bit;
      I      : Symbol_Index) return Natural
   is
     (if I < Sync_Symbol_Count then
        (if Sync = Command_Status then (if I < 3 then 1 else 0)
         else                          (if I < 3 then 0 else 1))
      elsif (I - Sync_Symbol_Count) mod 2 = 0 then
        Cell_Bit (Field, Parity, (I - Sync_Symbol_Count) / 2)
      else
        1 - Cell_Bit (Field, Parity, (I - Sync_Symbol_Count) / 2))
   with Ghost,
        Global => null,
        Post   => Expected_Symbol'Result <= 1;

   --  the two symbols of bit cell N, as received
   function Cell_Value (S : Symbols; N : Cell_Index) return Natural is
     (if Symbol_At (S, Sync_Symbol_Count + 2 * N) = 1
         and then Symbol_At (S, Sync_Symbol_Count + 2 * N + 1) = 0
      then 1 else 0)
   with Ghost, Global => null;

   --  a cell with no mid-bit transition: 0 0 or 1 1
   function Cell_Violation (S : Symbols; N : Cell_Index) return Boolean is
     (Symbol_At (S, Sync_Symbol_Count + 2 * N) = Symbol_At (S, Sync_Symbol_Count + 2 * N + 1))
   with Ghost, Global => null;

   --  True if the first six symbols of S are exactly the sync pattern of Kind
   function Sync_Matches (S : Symbols; Kind : Sync_Kind) return Boolean is
     (case Kind is
        when Command_Status =>
          (for all I in 0 .. Sync_Symbol_Count - 1 => Symbol_At (S, I) = (if I < 3 then 1 else 0)),
        when Data =>
          (for all I in 0 .. Sync_Symbol_Count - 1 => Symbol_At (S, I) = (if I < 3 then 0 else 1)),
        when Invalid => False)
   with Ghost, Global => null;

   --  implements: LLR-1553-WORD-006
   --  encodes a word into 40 symbols; result is always fully written: if Sync is not a transmittable sync (Invalid), Result is all zero and Success is
   --  False, so a caller that ignores Success still holds a deterministic buffer, never uninitialised memory
   procedure Encode_Word
     (Sync    : Sync_Kind;
      Field   : Data_Word;
      Parity  : Crc_Parity.Parity_Bit;
      Result  : out Symbols;
      Success : out Boolean)
   with Global  => null,
        Depends => (Result  => (Sync, Field, Parity),
                    Success => Sync),
        Pre     => True,
        Post    =>
          (if Sync = Invalid then
             not Success and then Result = Zero_Symbols
           else
             Success
             and then (for all I in Symbol_Index =>
                         Symbol_At (Result, I) = Expected_Symbol (Sync, Field, Parity, I)));


   --  implements: LLR-1553-WORD-007
   --  decodes 40 symbols;
   --    sync                 classified by EXACT pattern match: Command_Status, Data, or Invalid for anything else
   --    violation            True exactly when some bit cell (any of the 17) is 0 0 or 1 1
   --    Field and Parity     are always written, best effort: a cell that reads 1 0 gives 1; every other cell, including a violating one, gives 0
   --    Success              True when the sync was recognised and there is no violation
   --  a caller that needs the REASON for a failure reads Sync and Violation (this is what Mil1553_Word.Classify does, LLR-1553-WORD-008)
   procedure Decode_Word
     (In_Symbols : Symbols;
      Sync       : out Sync_Kind;
      Field      : out Data_Word;
      Parity     : out Crc_Parity.Parity_Bit;
      Violation  : out Boolean;
      Success    : out Boolean)
   with Global  => null,
        Depends => (Sync      => In_Symbols,
                    Field     => In_Symbols,
                    Parity    => In_Symbols,
                    Violation => In_Symbols,
                    Success   => In_Symbols),
        Pre     => True,
        Post    =>
          Sync = (if Sync_Matches (In_Symbols, Command_Status) then Command_Status
                  elsif Sync_Matches (In_Symbols, Data)         then Data
                  else Invalid)
          and then Violation = (for some N in Cell_Index => Cell_Violation (In_Symbols, N))
          and then Parity = Cell_Value (In_Symbols, 16)
          and then (for all N in 0 .. 15 => Crc_Parity.Bit_Of (Field, 15 - N) = Cell_Value (In_Symbols, N))
          and then Success = (Sync /= Invalid and then not Violation);

end Manchester;