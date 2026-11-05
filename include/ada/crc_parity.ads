--  project:    MIL1553
--  purpose:    odd parity computation and checking over a 16-bit information field; pure functions, no state

--  realizes:   LLR-1553-WORD-005
--  parent HLR: HLR-1553-WORD-005

--  standard:   Ada 2012, SPARK 2014.

--  design notes
--    * the contracts state parity in terms of a GHOST function, Ones_Count, which exists only for proof: the number of one-bits in the field; it is
--      written out as an expression function (16 terms, no loop, no recursion) so the provers see its definition directly and the proof of
--      the body is a bit-vector problem, not an induction
--    * parity_Bit is a 0 .. 1 subtype. The C version takes a uint8_t and has to mask it down to bit 0 defensively; here the type already guarantees
--      it, so there is no such robustness case to test (the compiler/prover rejects a value of 2 or more)
--    * every subprogram states Global (none: pure), Depends and Pre, as the SDP 3.3 coding standard requires; exceptional_Cases is not stated: these
--      are functions and cannot propagate an exception

with Mil1553_Types; use Mil1553_Types;

package Crc_Parity
  with SPARK_Mode => On
is

   subtype Parity_Bit is Natural range 0 .. 1;

   --  ghost specification

   --  bit number Position (0 = least significant) of Field, as 0 or 1
   function Bit_Of (Field : Data_Word; Position : Natural) return Natural is
     (Natural ((Field / (2 ** Position)) mod 2))
   with Ghost,
        Global => null,
        Pre    => Position <= 15,
        Post   => Bit_Of'Result <= 1;

   --  number of one-bits in Field
   function Ones_Count (Field : Data_Word) return Natural is
     (Bit_Of (Field, 0)  + Bit_Of (Field, 1)  + Bit_Of (Field, 2)  + Bit_Of (Field, 3)  +
      Bit_Of (Field, 4)  + Bit_Of (Field, 5)  + Bit_Of (Field, 6)  + Bit_Of (Field, 7)  +
      Bit_Of (Field, 8)  + Bit_Of (Field, 9)  + Bit_Of (Field, 10) + Bit_Of (Field, 11) +
      Bit_Of (Field, 12) + Bit_Of (Field, 13) + Bit_Of (Field, 14) + Bit_Of (Field, 15))
   with Ghost,
        Global => null,
        Post   => Ones_Count'Result <= 16;

   --  Implements: LLR-1553-WORD-005
   --  the parity bit that makes the total number of one-bits in (Field, parity bit) odd
   function Compute_Parity (Field : Data_Word) return Parity_Bit
   with Global  => null,
        Depends => (Compute_Parity'Result => Field),
        Pre     => True,
        Post    => (Ones_Count (Field) + Compute_Parity'Result) mod 2 = 1;

   --  Implements: LLR-1553-WORD-005
   --  true exactly when the total number of one-bits in (Field, Received) is odd, i.e. when Received is the parity bit Compute_Parity would give
   ---------------------------------------------------------------------------
   function Check_Parity (Field : Data_Word; Received : Parity_Bit) return Boolean
   with Global  => null,
        Depends => (Check_Parity'Result => (Field, Received)),
        Pre     => True,
        Post    => Check_Parity'Result = ((Ones_Count (Field) + Received) mod 2 = 1)
                   and then Check_Parity'Result = (Received = Compute_Parity (Field));

end Crc_Parity;