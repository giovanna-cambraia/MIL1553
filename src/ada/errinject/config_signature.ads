--  project:    MIL1553
--  purpose:    SHA-256 and the scenario file signature check for the error injection framework (TEST BUILD ONLY)

--  realizes:   LLR-1553-ERR-007
--  parent HLR: HLR-1553-ERR-005

--  CERTIFICATION BOUNDARY (PSAC 8): lives in src/ada/errinject, which mil1553_release.gpr does not list; see Error_Inject for the three guards

--  SPARK_Mode is Off: the signature check reads a file. The SHA-256 core itself is plain modular-integer code (FIPS 180-4), written so it could be moved
--  into SPARK later, but nothing here is claimed proved

--  differences from the C interface (config_signature.h), and why
--    * data travels as an unconstrained Byte_Array, not a pointer and a length
--    * the digest is a fixed 32-byte array returned by a function

--  behaviour to keep, as in the C header
--    * the signature file holds the 64 hex digits of the digest at its start (leading white space skipped); any text after them, such as the file name
--      written by sha256sum, is ignored; upper and lower case hex both accepted
--    * the comparison is done on the bytes already in memory, so the bytes hashed are exactly the bytes the caller goes on to parse (no hash-then-reread window)
--    * Sig_Missing: the signature file is absent or unreadable. Sig_Mismatch: the digest differs, OR the file is malformed (fewer than 64 hex digits, or a
--      non-hex character among them), as the C header says. (The pasted C returned Missing for a malformed file; the header and the Ada follow the header)

with Mil1553_Types; use Mil1553_Types;

package Config_Signature
  with SPARK_Mode => Off
is

   type Byte_Array is array (Natural range <>) of Byte;

   subtype Digest_Index is Natural range 0 .. 31;
   type Digest is array (Digest_Index) of Byte;

   type Sig_Result is (Sig_Ok, Sig_Missing, Sig_Mismatch);

   --  implements: LLR-1553-ERR-007
   --  standard SHA-256 (FIPS 180-4) over Data
   function Sha256 (Data : Byte_Array) return Digest;

   --  implements: LLR-1553-ERR-007
   --  computes SHA-256 over Data and compares it with the 64 hex digits at the start of the file Sig_Path
   function Check_Signature (Data : Byte_Array; Sig_Path : String) return Sig_Result;

end Config_Signature;