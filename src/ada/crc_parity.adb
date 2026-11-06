-- project: MIL1553
-- realizes: LLR-1553-WORD-005

-- design note
   -- the number of one bits of a 16-bit value is odd exactly when the XOR of all sixteen bits is 1, the fold felow brings that XOR down into bit 0 in four steps 
   -- (16 -> 8 -> 4 -> 2 -> 1 live bits); odd parity then needs the parity bit to be 1 when the field's count is even (XOR = 0) and 0 when it is odd (XOR = 1); no loop, so 
   -- there is no loop invariant to maintain and the postcondition is a finite 16-bit problem for the provers 

   package body Crc_Parity
      with SPARK_Mode => On
   is

   -- implements: LLR-1553-WORD-005
   function Compute_Parity (Field : Data_Word) return Parity_Bit is
      F : Data_Word := Field;
   begin 
      F := F xor (F / 2 ** 8);
      F := F xor (F / 2 ** 4);
      F := F xor (F / 2 ** 2);
      F := F xor (F / 2 ** 1);
      -- bit 0 of F is now the XOR of all 16 bits of Field
      if (F and 1) = 1 then
         return 0;
      else
         return 1;
      end if;
   end Compute_Parity;

   -- implements: LLR-1553-WORD-005
   function Check_Parity (Field : Data_Word; Received : Parity_Bit) return Boolean is
   begin 
      return Received = Compute_Parity (Field);
   end Check_Parity;

end Crc_Parity;