--  project:    MIL1553
--  purpose:    minimal test runner for the Ada build: a few known-answer and behaviour checks across the lower layers, so mil1553_test.gpr builds and runs

--  realizes:   checks LLR-1553-WORD-001, WORD-008, BUS-001, BIT-009, ERR-007

--  test names follow test_llr_1553_<area>_<nnn>_<case> so the traceability CSVs can be generated from them; the numbers are the LLRs each check exercises
--  this is NOT the AUnit port of the C suites (310 tests): that is the next stage after v1. It is a runner that proves the toolchain, the project file and
--  a few of the lowest layers work end to end

--  the check for the signature code (SHA-256 of "abc") lives here because Config_Signature is in the test build only
--  exit status is 0 when every check passed, 1 otherwise

--  SPARK_Mode is Off: text I/O, not part of the proved item
--  proof status: NOT compiled, NOT run (no GNAT available to the author)

pragma SPARK_Mode (Off);

with Ada.Command_Line;
with Ada.Text_IO;
with Bit_Types;
with Bus_Model;
with Bus_Model_Types;
with Config_Signature;
with Mil1553_Types; use Mil1553_Types;
with Mil1553_Word;

procedure Test_Runner is
   use Ada.Text_IO;
   use type Bit_Types.Event_Count;

   Total    : Natural := 0;
   Failures : Natural := 0;

   procedure Check (Name : String; Ok : Boolean) is
   begin
      Total := Total + 1;
      if Ok then
         Put_Line ("PASS  " & Name);
      else
         Failures := Failures + 1;
         Put_Line ("FAIL  " & Name);
      end if;
   end Check;

   function To_Hex (D : Config_Signature.Digest) return String is
      Hex    : constant String := "0123456789abcdef";
      Result : String (1 .. 64);
   begin
      for I in Config_Signature.Digest_Index loop
         Result (2 * I + 1) := Hex (Natural (D (I)) / 16 + 1);
         Result (2 * I + 2) := Hex (Natural (D (I)) mod 16 + 1);
      end loop;
      return Result;
   end To_Hex;

   --  LLR-1553-ERR-007: SHA-256 known answer (FIPS 180-4)
   procedure Test_Sha256 is
   begin
      Check ("test_llr_1553_err_007_sha256_abc",
             To_Hex (Config_Signature.Sha256 ((97, 98, 99)))
             = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
   end Test_Sha256;

   --  LLR-1553-WORD-001 / WORD-008: a command word round trips, and a flipped parity cell is a parity error (valid Manchester, wrong parity)
   procedure Test_Word is
      Fields : constant Command_Fields :=
        (Address => 7, Direction => Transmit, Sub => 5, Word_Count_Raw => 0);
      Syms   : Symbols := Zero_Symbols;
      D      : Mil1553_Word.Decoded_Word;
   begin
      Mil1553_Word.Pack_Command (Fields, Syms);
      D := Mil1553_Word.Classify (Syms);
      Check ("test_llr_1553_word_001_command_roundtrip",
             D.Class = Valid
             and then D.Sync = Command_Status
             and then Mil1553_Word.Unpack_Command (D.Field) = Fields);

      Syms (4) := Syms (4) xor 3;                  --  symbols 38 and 39: the parity bit cell
      D := Mil1553_Word.Classify (Syms);
      Check ("test_llr_1553_word_008_parity_cell_flip", D.Class = Parity_Error);
   end Test_Word;

   --  LLR-1553-BIT-009: the transition table, an illegal event, and Fail as a dead end
   procedure Test_Bit_States is
      R     : Bit_Types.Result;
      Legal : Boolean;
   begin
      Bit_Types.Init (R);
      Check ("test_llr_1553_bit_009_initial_power_up", R.State = Power_Up);

      Bit_Types.Apply (R, Bit_Types.Pbit_Start, Legal);
      Check ("test_llr_1553_bit_009_pbit_start", Legal and then R.State = Pbit_Running);

      Bit_Types.Apply (R, Bit_Types.Pbit_Pass, Legal);
      Check ("test_llr_1553_bit_009_pbit_pass", Legal and then R.State = Operational);

      Bit_Types.Apply (R, Bit_Types.Pbit_Start, Legal);
      Check ("test_llr_1553_bit_009_illegal_event_ignored_and_counted",
             not Legal and then R.State = Operational and then R.Ignored_Event_Count = 1);

      Bit_Types.Apply (R, Bit_Types.Cbit_Begin, Legal);
      Bit_Types.Apply (R, Bit_Types.Cbit_Nonfatal, Legal);
      Check ("test_llr_1553_bit_009_cbit_nonfatal_degraded", Legal and then R.State = Degraded);

      Bit_Types.Apply (R, Bit_Types.Cbit_Begin, Legal);
      Bit_Types.Apply (R, Bit_Types.Cbit_Pass, Legal);
      Check ("test_llr_1553_bit_009_degraded_cbit_pass_operational", Legal and then R.State = Operational);

      Bit_Types.Apply (R, Bit_Types.Fatal, Legal);
      Check ("test_llr_1553_bit_009_fatal_to_fail", Legal and then R.State = Fail);

      Bit_Types.Apply (R, Bit_Types.Cbit_Pass, Legal);
      Check ("test_llr_1553_bit_009_fail_is_dead_end", not Legal and then R.State = Fail);

      Bit_Types.Init (R);
      Bit_Types.Apply (R, Bit_Types.Fatal, Legal);
      Check ("test_llr_1553_bit_009_fatal_ignored_in_power_up", not Legal and then R.State = Power_Up);
   end Test_Bit_States;

   --  LLR-1553-BUS-001: a word reaches every other listener on its own cursor, a terminal does not read its own word, the loopback cursor does, a failed
   --  channel delivers nothing
   procedure Test_Bus is
      Bus  : Bus_Model_Types.Context;
      Syms : Symbols := Zero_Symbols;
      Rec  : Bus_Model_Types.Bus_Record;
      Sent : Boolean;
      Got  : Boolean;
   begin
      Bus_Model.Init (Bus);
      Mil1553_Word.Pack_Data (16#5AA5#, Syms);

      Bus_Model.Transmit (Bus, A, Syms, BC, Sent);
      Check ("test_llr_1553_bus_001_transmit_sent", Sent);

      Bus_Model.Receive (Bus, A, RT1, Rec, Got);
      Check ("test_llr_1553_bus_001_listener_receives_word",
             Got and then Rec.Sender = BC and then Rec.Syms = Syms);

      Bus_Model.Receive (Bus, A, BC, Rec, Got);
      Check ("test_llr_1553_bus_001_sender_skips_own_word", not Got);

      Bus_Model.Receive_Loopback (Bus, A, BC, Rec, Got);
      Check ("test_llr_1553_bus_001_loopback_reads_own_word", Got and then Rec.Sender = BC);

      Bus_Model.Receive (Bus, A, RT1, Rec, Got);
      Check ("test_llr_1553_bus_001_listener_caught_up", not Got);

      Bus_Model.Receive (Bus, B, RT1, Rec, Got);
      Check ("test_llr_1553_bus_001_channels_independent", not Got);

      Bus_Model.Set_Channel_Failed (Bus, A, True);
      Bus_Model.Transmit (Bus, A, Syms, BC, Sent);
      Check ("test_llr_1553_bus_001_failed_channel_not_sent", not Sent);

      Bus_Model.Advance_Us (Bus, 20);
      Check ("test_llr_1553_bus_003_clock_advances", Bus_Model.Now_Us (Bus) = 20);
   end Test_Bus;
begin
   Test_Sha256;
   Test_Word;
   Test_Bit_States;
   Test_Bus;

   New_Line;
   Put_Line (Natural'Image (Total - Failures) & " passed," & Natural'Image (Failures) & " failed");

   if Failures /= 0 then
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
   end if;
end Test_Runner;