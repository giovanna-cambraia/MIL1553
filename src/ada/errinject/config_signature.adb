--  project:    MIL1553

--  realizes:   LLR-1553-ERR-007
--  purpose:    body of Config_Signature: SHA-256 (FIPS 180-4) and the signature file check (port of config_signature.c, with the three defects of the
--              pasted C fixed: hex digit test, mismatch result, digest element type)
--  proof status: NOT compiled, NOT proved (no GNAT available to the author)

with Ada.Streams;           use Ada.Streams;
with Ada.Streams.Stream_IO;
with Interfaces;            use Interfaces;

package body Config_Signature
  with SPARK_Mode => Off
is

   type Round_Constants is array (0 .. 63) of Unsigned_32;

   K : constant Round_Constants :=
     (16#428a2f98#, 16#71374491#, 16#b5c0fbcf#, 16#e9b5dba5#, 16#3956c25b#, 16#59f111f1#, 16#923f82a4#, 16#ab1c5ed5#,
      16#d807aa98#, 16#12835b01#, 16#243185be#, 16#550c7dc3#, 16#72be5d74#, 16#80deb1fe#, 16#9bdc06a7#, 16#c19bf174#,
      16#e49b69c1#, 16#efbe4786#, 16#0fc19dc6#, 16#240ca1cc#, 16#2de92c6f#, 16#4a7484aa#, 16#5cb0a9dc#, 16#76f988da#,
      16#983e5152#, 16#a831c66d#, 16#b00327c8#, 16#bf597fc7#, 16#c6e00bf3#, 16#d5a79147#, 16#06ca6351#, 16#14292967#,
      16#27b70a85#, 16#2e1b2138#, 16#4d2c6dfc#, 16#53380d13#, 16#650a7354#, 16#766a0abb#, 16#81c2c92e#, 16#92722c85#,
      16#a2bfe8a1#, 16#a81a664b#, 16#c24b8b70#, 16#c76c51a3#, 16#d192e819#, 16#d6990624#, 16#f40e3585#, 16#106aa070#,
      16#19a4c116#, 16#1e376c08#, 16#2748774c#, 16#34b0bcb5#, 16#391c0cb3#, 16#4ed8aa4a#, 16#5b9cca4f#, 16#682e6ff3#,
      16#748f82ee#, 16#78a5636f#, 16#84c87814#, 16#8cc70208#, 16#90befffa#, 16#a4506ceb#, 16#bef9a3f7#, 16#c67178f2#);

   type Hash_State is array (0 .. 7) of Unsigned_32;
   type Block      is array (0 .. 63) of Byte;

   Initial_State : constant Hash_State :=
     (16#6a09e667#, 16#bb67ae85#, 16#3c6ef372#, 16#a54ff53a#,
      16#510e527f#, 16#9b05688c#, 16#1f83d9ab#, 16#5be0cd19#);

   --  one 64-byte block into the running state
   procedure Process (H : in out Hash_State; Blk : Block) is
      W : array (0 .. 63) of Unsigned_32;
      A, B, C, D, E, F, G, HH : Unsigned_32;
   begin
      for I in 0 .. 15 loop
         W (I) := Shift_Left (Unsigned_32 (Blk (4 * I)), 24)
                  or Shift_Left (Unsigned_32 (Blk (4 * I + 1)), 16)
                  or Shift_Left (Unsigned_32 (Blk (4 * I + 2)), 8)
                  or Unsigned_32 (Blk (4 * I + 3));
      end loop;

      for I in 16 .. 63 loop
         declare
            S0 : constant Unsigned_32 :=
              Rotate_Right (W (I - 15), 7) xor Rotate_Right (W (I - 15), 18) xor Shift_Right (W (I - 15), 3);
            S1 : constant Unsigned_32 :=
              Rotate_Right (W (I - 2), 17) xor Rotate_Right (W (I - 2), 19) xor Shift_Right (W (I - 2), 10);
         begin
            W (I) := W (I - 16) + S0 + W (I - 7) + S1;
         end;
      end loop;

      A := H (0); B := H (1); C := H (2); D := H (3);
      E := H (4); F := H (5); G := H (6); HH := H (7);

      for I in 0 .. 63 loop
         declare
            S1  : constant Unsigned_32 := Rotate_Right (E, 6) xor Rotate_Right (E, 11) xor Rotate_Right (E, 25);
            Ch  : constant Unsigned_32 := (E and F) xor ((not E) and G);
            T1  : constant Unsigned_32 := HH + S1 + Ch + K (I) + W (I);
            S0  : constant Unsigned_32 := Rotate_Right (A, 2) xor Rotate_Right (A, 13) xor Rotate_Right (A, 22);
            Maj : constant Unsigned_32 := (A and B) xor (A and C) xor (B and C);
            T2  : constant Unsigned_32 := S0 + Maj;
         begin
            HH := G; G := F; F := E; E := D + T1;
            D := C; C := B; B := A; A := T1 + T2;
         end;
      end loop;

      H (0) := H (0) + A; H (1) := H (1) + B; H (2) := H (2) + C; H (3) := H (3) + D;
      H (4) := H (4) + E; H (5) := H (5) + F; H (6) := H (6) + G; H (7) := H (7) + HH;
   end Process;

   --  implements: LLR-1553-ERR-007
   function Sha256 (Data : Byte_Array) return Digest is
      H          : Hash_State := Initial_State;
      Length     : constant Natural := Data'Length;
      Full       : constant Natural := Length / 64;
      Rem_Bytes  : constant Natural := Length mod 64;
      Bit_Len    : constant Unsigned_64 := Unsigned_64 (Length) * 8;
      Blk        : Block;
      Tail       : array (0 .. 127) of Byte := (others => 0);
      Tail_Len   : Natural;
      Result     : Digest;
   begin
      for I in 0 .. Full - 1 loop
         for J in 0 .. 63 loop
            Blk (J) := Data (Data'First + I * 64 + J);
         end loop;
         Process (H, Blk);
      end loop;

      for I in 0 .. Rem_Bytes - 1 loop
         Tail (I) := Data (Data'First + Full * 64 + I);
      end loop;
      Tail (Rem_Bytes) := 16#80#;
      Tail_Len := (if Rem_Bytes < 56 then 64 else 128);

      for I in 0 .. 7 loop
         Tail (Tail_Len - 1 - I) := Byte (Shift_Right (Bit_Len, 8 * I) and 16#FF#);
      end loop;

      for J in 0 .. 63 loop
         Blk (J) := Tail (J);
      end loop;
      Process (H, Blk);

      if Tail_Len = 128 then
         for J in 0 .. 63 loop
            Blk (J) := Tail (64 + J);
         end loop;
         Process (H, Blk);
      end if;

      for I in 0 .. 7 loop
         Result (4 * I)     := Byte (Shift_Right (H (I), 24) and 16#FF#);
         Result (4 * I + 1) := Byte (Shift_Right (H (I), 16) and 16#FF#);
         Result (4 * I + 2) := Byte (Shift_Right (H (I), 8) and 16#FF#);
         Result (4 * I + 3) := Byte (H (I) and 16#FF#);
      end loop;

      return Result;
   end Sha256;

   --  value of one hex digit character, -1 if it is not one
   function Hex_Value (C : Stream_Element) return Integer is
   begin
      case C is
         when 16#30# .. 16#39# => return Integer (C) - 16#30#;          --  0 .. 9
         when 16#61# .. 16#66# => return Integer (C) - 16#61# + 10;     --  a .. f
         when 16#41# .. 16#46# => return Integer (C) - 16#41# + 10;     --  A .. F
         when others           => return -1;
      end case;
   end Hex_Value;

   function Is_Blank (C : Stream_Element) return Boolean
   is (C = 16#20# or else C = 16#09# or else C = 16#0D# or else C = 16#0A#);

   --  implements: LLR-1553-ERR-007
   function Check_Signature (Data : Byte_Array; Sig_Path : String) return Sig_Result is
      use Ada.Streams.Stream_IO;

      File     : File_Type;
      Text     : Stream_Element_Array (1 .. 256);
      Last     : Stream_Element_Offset := 0;
      Expected : Digest;
      Pos      : Stream_Element_Offset;
   begin
      begin
         Open (File, In_File, Sig_Path);
      exception
         when others =>
            return Sig_Missing;                         --  absent or unreadable
      end;

      begin
         Read (File, Text, Last);
      exception
         when others =>
            Close (File);
            return Sig_Missing;
      end;
      Close (File);

      Pos := 1;
      while Pos <= Last and then Is_Blank (Text (Pos)) loop
         Pos := Pos + 1;
      end loop;

      if Last - Pos + 1 < 64 then
         return Sig_Mismatch;                           --  malformed: fewer than 64 hex digits
      end if;

      for I in Digest_Index loop
         declare
            Hi : constant Integer := Hex_Value (Text (Pos + Stream_Element_Offset (I * 2)));
            Lo : constant Integer := Hex_Value (Text (Pos + Stream_Element_Offset (I * 2 + 1)));
         begin
            if Hi < 0 or else Lo < 0 then
               return Sig_Mismatch;                     --  malformed: not a hex digit
            end if;
            Expected (I) := Byte (Hi * 16 + Lo);
         end;
      end loop;

      return (if Sha256 (Data) = Expected then Sig_Ok else Sig_Mismatch);
   end Check_Signature;

end Config_Signature;