--  project:  MIL1553
--  realizes: LLR-1553-WORD-006, LLR-1553-WORD-007

--  proof status: written to be proved with GNATprove, NOT yet run (the author's environment has no GNAT); the loop invariants restate the ghost layout of the
--  spec; expect to tune them on the first proof run

package body Manchester
  with SPARK_Mode => On
is

   --  packed-symbol access (file-local: an implementation detail of the layout)
   --  implements: LLR-1553-WORD-007 (helper)
   function Get_Symbol (S : Symbols; I : Symbol_Index) return Natural
   with Global => null,
        Post   => Get_Symbol'Result = Symbol_At (S, I)
   is
   begin
      return Natural ((S (I / 8) / (Byte (2) ** (7 - (I mod 8)))) mod 2);
   end Get_Symbol;

   --  implements: LLR-1553-WORD-006 (helper)
   procedure Set_Symbol (S : in out Symbols; I : Symbol_Index; Value : Natural)
   with Global => null,
        Pre    => Value <= 1,
        Post   => Symbol_At (S, I) = Value
                  and then (for all J in Symbol_Index =>
                              (if J /= I then Symbol_At (S, J) = Symbol_At (S'Old, J)))
   is
      Mask : constant Byte := Byte (2) ** (7 - (I mod 8));
   begin
      if Value = 1 then
         S (I / 8) := S (I / 8) or Mask;
      else
         S (I / 8) := S (I / 8) and (not Mask);
      end if;
   end Set_Symbol;

   --  implements: LLR-1553-WORD-006
   procedure Encode_Word
     (Sync    : Sync_Kind;
      Field   : Data_Word;
      Parity  : Crc_Parity.Parity_Bit;
      Result  : out Symbols;
      Success : out Boolean)
   is
   begin
      Result := Zero_Symbols;               --  always fully written

      if Sync = Invalid then
         Success := False;
         return;
      end if;

      Success := True;

      --  symbols 0 .. 5: the sync pattern
      for I in 0 .. Sync_Symbol_Count - 1 loop
         Set_Symbol
           (Result, I,
            (if Sync = Command_Status then (if I < 3 then 1 else 0)
             else                          (if I < 3 then 0 else 1)));
         pragma Loop_Invariant
           (for all J in Symbol_Index =>
              (if J <= I then Symbol_At (Result, J) = Expected_Symbol (Sync, Field, Parity, J)));
      end loop;

      --  symbols 6 .. 39: 17 bit cells, field MSB first then parity; a one is the pair 1 0, a zero is 0 1
      for N in Cell_Index loop
         declare
            Bit_Value : constant Natural :=
              (if N < 16 then Natural ((Field / (2 ** (15 - N))) mod 2) else Parity);
         begin
            Set_Symbol (Result, Sync_Symbol_Count + 2 * N,     Bit_Value);
            Set_Symbol (Result, Sync_Symbol_Count + 2 * N + 1, 1 - Bit_Value);
         end;
         pragma Loop_Invariant
           (for all J in Symbol_Index =>
              (if J <= Sync_Symbol_Count + 2 * N + 1 then
                 Symbol_At (Result, J) = Expected_Symbol (Sync, Field, Parity, J)));
      end loop;
   end Encode_Word;

   --  implements: LLR-1553-WORD-007
   procedure Decode_Word
     (In_Symbols : Symbols;
      Sync       : out Sync_Kind;
      Field      : out Data_Word;
      Parity     : out Crc_Parity.Parity_Bit;
      Violation  : out Boolean;
      Success    : out Boolean)
   is
      CS_Match   : Boolean := True;
      Data_Match : Boolean := True;
   begin
      --  sync: exact pattern match
      for I in 0 .. Sync_Symbol_Count - 1 loop
         if Get_Symbol (In_Symbols, I) /= (if I < 3 then 1 else 0) then
            CS_Match := False;
         end if;
         if Get_Symbol (In_Symbols, I) /= (if I < 3 then 0 else 1) then
            Data_Match := False;
         end if;
         pragma Loop_Invariant
           (CS_Match = (for all J in 0 .. I => Symbol_At (In_Symbols, J) = (if J < 3 then 1 else 0)));
         pragma Loop_Invariant
           (Data_Match = (for all J in 0 .. I => Symbol_At (In_Symbols, J) = (if J < 3 then 0 else 1)));
      end loop;

      if CS_Match then
         Sync := Command_Status;
      elsif Data_Match then
         Sync := Data;
      else
         Sync := Invalid;
      end if;

      --  the 17 bit cells. Written for every cell whether or not the sync was good, as the spec says: best effort, meaningful only on Success
      Field     := 0;
      Parity    := 0;
      Violation := False;

      for N in Cell_Index loop
         declare
            A   : constant Natural := Get_Symbol (In_Symbols, Sync_Symbol_Count + 2 * N);
            B   : constant Natural := Get_Symbol (In_Symbols, Sync_Symbol_Count + 2 * N + 1);
            Bit : constant Natural := (if A = 1 and then B = 0 then 1 else 0);
         begin
            if A = B then
               Violation := True;          --  0 0 or 1 1: no mid-bit transition
            end if;

            if N < 16 then
               if Bit = 1 then
                  Field := Field or (Data_Word (2) ** (15 - N));
               end if;
            else
               Parity := Bit;
            end if;
         end;

         pragma Loop_Invariant
           (Violation = (for some K in 0 .. N => Cell_Violation (In_Symbols, K)));
         pragma Loop_Invariant
           (for all K in 0 .. Natural'Min (N, 15) =>
              Crc_Parity.Bit_Of (Field, 15 - K) = Cell_Value (In_Symbols, K));
         pragma Loop_Invariant
           (for all K in Natural'Min (N, 15) + 1 .. 15 => Crc_Parity.Bit_Of (Field, 15 - K) = 0);
         pragma Loop_Invariant
           (if N >= 16 then Parity = Cell_Value (In_Symbols, 16) else Parity = 0);
      end loop;

      Success := Sync /= Invalid and then not Violation;
   end Decode_Word;

end Manchester;