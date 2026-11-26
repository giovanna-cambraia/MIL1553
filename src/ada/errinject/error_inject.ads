--  project:    MIL1553
--  purpose:    scenario-driven fault injection for the software bus (TEST BUILD ONLY)

--  realizes:   LLR-1553-ERR-006, LLR-1553-ERR-007, LLR-1553-ERR-008, LLR-1553-ERR-009
--  parent HLRs: HLR-1553-ERR-005, HLR-1553-ERR-006

--  CERTIFICATION BOUNDARY (PSAC 8): this package is excluded from the certified build
--    1. build time: it lives only in src/ada/errinject, which mil1553_release.gpr does not list (see "deviation" below)
--    2. link time: the release Bus_Model.Transmit subunit (bus_model-transmit.adb) has no hook, so no release unit can reach the injector (LLR-1553-ERR-005)
--    3. run time: inert until Arm is called with the harness token AND a scenario file whose SHA-256 matches its signature file has been loaded
--       (LLR-1553-ERR-006, LLR-1553-ERR-007)
--  the harness token (Errinject_Token.Value, supplied by the test build) guards against ACCIDENTAL arming, e.g. a role main calling Arm by mistake; it is not
--  a security boundary, build-time exclusion is the real guarantee

--  deviation from the C (flag for LLR-1553-ERR-005 review): the C has a compile-time guard (#error unless MIL1553_ENABLE_ERROR_INJECTION). Ada has no
--  #error; the equivalent here is that the file is only reachable from mil1553_test.gpr, which the exclusion check script must verify

--  SPARK_Mode is Off: the module is outside the certified item, loads files and handles text, and keeps single-instance package state, as the C does;
--  Bus_Model.Transmit (Global => null) is therefore implemented by a hooked subunit that is also SPARK_Mode Off, its contract assumed, not proved

--  differences from the C interface (error_inject.h), and why
--    * Err_Type_Count is derived from Error_Kind, not hard-coded to 12 (closes that open item); observation counters are indexed by Error_Kind
--    * names, buses and faults are enumerations in the scenario and in the result record; the C kept strings and converts only when writing the JSON
--    * pointer results become preconditions (Get_Scenario, Get_Result) and Ok / Passed outputs
--    * text fields are bounded strings; the C char-array capacities stay as the limits the loader enforces (Id_Max_Chars ...)
--    * Filter_Transmit takes the bus (it may advance the clock for DELAY_RESPONSE, as in the C), the symbols in out, and Drop out

--  scenario file schema (config/error_scenarios.yaml): unchanged from the C, a list of items
--    - id, target, fault, expected_detection are required; bus (A | B | ANY), word_index, count, duration_ms, delay_us, bit (BUSY | ME | SF | TF),
--      expected_recovery (NONE | RT_MARKED_FAILED | BUS_FAILOVER), hlr, llr, test_id are optional

with Ada.Strings.Bounded;
with Mil1553_Types;   use Mil1553_Types;
with Bus_Model_Types;

package Error_Inject
  with SPARK_Mode => Off
is

   Max_Scenarios : constant := 32;

   Err_Type_Count : constant := Error_Kind'Pos (Error_Kind'Last) + 1;   --  12 today

   --  limits the loader enforces on scenario text (as the C char array sizes minus the terminator)
   Id_Max_Chars        : constant := 15;
   Detection_Max_Chars : constant := 27;
   Recovery_Max_Chars  : constant := 23;
   Ref_Max_Chars       : constant := 23;     --  hlr, llr, test_id

   --  the injector's own scenario for ILLEGAL_COMMAND: this subaddress must be disabled in config/bus_config.yaml
   Illegal_Command_Subaddress : constant Subaddress := 30;

   type Count is mod 2 ** 32;

   package Texts is new Ada.Strings.Bounded.Generic_Bounded_Length (Max => 128);
   subtype Text is Texts.Bounded_String;

   type Bus_Select is (Bus_A, Bus_B, Bus_Any);

   type Status_Bit is (Bit_Busy, Bit_Me, Bit_Sf, Bit_Tf);

   type Reject_Reason is
     (Reject_None,
      Reject_Bad_Token,
      Reject_Not_Loaded,
      Reject_Sig_Missing,
      Reject_Sig_Mismatch,
      Reject_Yaml_Missing,
      Reject_Yaml_Too_Large,
      Reject_Yaml_Parse,
      Reject_Not_Armed);

   --  implements: LLR-1553-ERR-007
   type Scenario is record
      Id                 : Text         := Texts.Null_Bounded_String;
      Target             : Terminal_Id  := BC;
      Bus_Sel            : Bus_Select   := Bus_Any;
      Fault              : Fault_Class  := Parity_Bit_Flip;
      Word_Index         : Count        := 0;
      Fault_Count        : Count        := 1;
      Duration_Us        : Count        := 0;     --  SUPPRESS_RESPONSE window
      Delay_Us           : Count        := 0;     --  DELAY_RESPONSE added delay
      Bit                : Status_Bit   := Bit_Busy;
      Expected_Detection : Text         := Texts.Null_Bounded_String;
      Expected_Recovery  : Text         := Texts.To_Bounded_String ("NONE");
      Hlr                : Text         := Texts.Null_Bounded_String;
      Llr                : Text         := Texts.Null_Bounded_String;
      Test_Id            : Text         := Texts.Null_Bounded_String;
   end record;

   type Error_Counts is array (Error_Kind) of Count;

   --  what the campaign harness observed while a scenario ran
   type Observation is record
      Error_Type_Count              : Error_Counts := (others => 0);
      Cbit_Response_Time_Violations : Count        := 0;
      Failover_Occurred             : Boolean      := False;   --  the BC switched its primary bus
      Rt_Marked_Failed              : Boolean      := False;   --  the BC marked an RT failed
   end record;

   type Error_Records is array (Natural range <>) of Error_Record;

   --  implements: LLR-1553-ERR-009 (result record)
   type Result is record
      Scenario_Id : Text         := Texts.Null_Bounded_String;
      Timestamp   : Time_Us      := 0;      --  first injection (or scenario start if none)
      Terminal    : Terminal_Id  := BC;
      Bus         : Bus_Select   := Bus_Any;
      Fault       : Fault_Class  := Parity_Bit_Flip;
      Expected    : Text         := Texts.Null_Bounded_String;
      Observed    : Text         := Texts.Null_Bounded_String;
      Pass        : Boolean      := False;
      Hlr         : Text         := Texts.Null_Bounded_String;
      Llr         : Text         := Texts.Null_Bounded_String;
      Test_Id     : Text         := Texts.Null_Bounded_String;
   end record;

   --  test harness support: returns the module to its power-up condition (inert, nothing loaded, no active scenario, no results, reject log cleared);
   --  the module is a single instance, so unit tests call this between cases; not part of any campaign
   procedure Reset
   with Post => not Is_Armed and then Scenario_Count = 0 and then Result_Count = 0 and then Reject_Count = 0;

   --  arming, LLR-1553-ERR-006

   --  arms the injector if Token equals the harness token and a signed scenario file has been loaded; otherwise stays inert, records the rejection
   --  (Reject_Count / Last_Reject) and Ok is False
   procedure Arm (Token : Count; Ok : out Boolean)
   with Post => Ok = Is_Armed;

   procedure Disarm
   with Post => not Is_Armed;

   function Is_Armed return Boolean;

   --  scenario loading, LLR-1553-ERR-007

   --  reads Yaml_Path once into a fixed buffer, checks its SHA-256 against Sig_Path, and only then parses the same bytes; on any failure (missing file,
   --  missing or mismatched signature, file over 16384 bytes, parse error) no scenario is loaded, the injector stays inert, and the reason is recorded
   procedure Load_Scenarios (Yaml_Path : String; Sig_Path : String; Ok : out Boolean)
   with Post => (if not Ok then Scenario_Count = 0 and then not Is_Armed);

   function Scenario_Count return Natural;

   function Get_Scenario (Index : Natural) return Scenario
   with Pre => Index < Scenario_Count;

   --  registers the configured RT address of an RT terminal (needed by ILLEGAL_COMMAND)
   procedure Register_Rt_Address (Terminal : Terminal_Id; Address : RT_Address);

   --  scenario execution, LLR-1553-ERR-008 / ERR-009

   --  starts scenario Index (one at a time); BUS_A_FAIL / BUS_B_FAIL act immediately. Ok is False (and Reject_Not_Armed is recorded when the cause is
   --  that) if the injector is not armed and loaded, a scenario is already active, or Index is out of range
   procedure Begin_Scenario
     (Index : Natural; Bus : in out Bus_Model_Types.Context; Ok : out Boolean);

   --  ends the active scenario, restores any failed channel, compares the observation with the scenario's expectation and appends a result record; a scenario
   --  whose fault was never applied, or that expected a detection and saw none, is recorded as a FAILURE (never a pass by default); Passed is the
   --  pass/fail of this scenario, False when no scenario was active
   procedure End_Scenario
     (Bus : in out Bus_Model_Types.Context; Obs : Observation; Passed : out Boolean);

   procedure Observation_Clear (Obs : out Observation)
   with Post => Obs = Observation'(others => <>);

   procedure Observation_Add_Log (Obs : in out Observation; Records : Error_Records);

   --  the interposition point used by the hooked Bus_Model.Transmit subunit: may modify Syms in place, ask for the word to be dropped, or advance the
   --  clock (DELAY_RESPONSE); inert (Drop False, nothing changed) unless armed, loaded and a scenario is active
   procedure Filter_Transmit
     (Bus    : in out Bus_Model_Types.Context;
      Chan   : Channel;
      Sender : Terminal_Id;
      Syms   : in out Symbols;
      Drop   : out Boolean);

   --  results and diagnostics

   function Result_Count return Natural;

   function Get_Result (Index : Natural) return Result
   with Pre => Index < Result_Count;

   --  JSON, 11 keys per record, the same as the C
   procedure Write_Results_Json (Path : String; Ok : out Boolean);

   function Reject_Count return Count;

   function Last_Reject return Reject_Reason;

end Error_Inject;