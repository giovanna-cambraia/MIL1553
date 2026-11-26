--  project:    MIL1553
--  realizes:   LLR-1553-ERR-006, LLR-1553-ERR-007, LLR-1553-ERR-008, LLR-1553-ERR-009
--  purpose:    body of Error_Inject: scenario file parser, fault filter, scenario execution, result records and JSON (port of error_inject.c)
--  TEST BUILD ONLY - outside the certification boundary (PSAC 8); see error_inject.ads for the three guards

--  fault semantics (all act on words as they are transmitted, in the hooked Bus_Model.Transmit, for the terminal named by the scenario's target and its bus)
--    Parity_Bit_Flip      swap the parity bit cell (valid Manchester, wrong parity) for target words Word_Index .. +Fault_Count-1, counted from scenario start
--    Corrupt_Sync         replace the sync with an invalid pattern, same word window
--    Suppress_Response    drop every word the target sends for Duration_Us of virtual time from scenario start
--    Delay_Response       advance the virtual clock by Delay_Us before the target's first Fault_Count status words go out
--    Wrong_Word_Count     drop the data word with index Word_Index in the target's first Fault_Count responses (short response)
--    Status_Bit_Inject    set BUSY / ME / SF / TF in the target's first Fault_Count status words, parity recomputed
--    Illegal_Command      rewrite the subaddress of the first Fault_Count BC commands addressed to the target RT to Illegal_Command_Subaddress
--    Bus_A_Fail / Bus_B_Fail   mark the channel failed from scenario start

--  proof status: NOT compiled, NOT proved (no GNAT available to the author)

with Ada.Streams;            use Ada.Streams;
with Ada.Streams.Stream_IO;
with Ada.Strings;
with Ada.Strings.Fixed;
with Ada.Strings.Maps;
with Ada.Text_IO;
with Bus_Model;
with Config_Signature;
with Errinject_Token;
with Mil1553_Word;

package body Error_Inject
  with SPARK_Mode => Off
is

   Yaml_Max_Bytes : constant := 16384;
   Line_Max_Chars : constant := 160;

   --  module state (single instance; process start == inert)

   type Scenario_Array is array (0 .. Max_Scenarios - 1) of Scenario;
   type Result_Array   is array (0 .. Max_Scenarios - 1) of Result;
   type Terminal_Addrs is array (Terminal_Id) of RT_Address;

   Armed          : Boolean := False;
   Loaded         : Boolean := False;

   Scen           : Scenario_Array;
   Scen_Count     : Natural := 0;

   Res            : Result_Array;
   Res_Count      : Natural := 0;

   Rt_Addr        : Terminal_Addrs := (others => 0);

   Active         : Boolean := False;
   Active_Idx     : Natural := 0;
   Active_Start   : Time_Us := 0;
   First_Inject   : Time_Us := 0;
   Words_Seen     : Count   := 0;
   Applied        : Count   := 0;
   Resp_Data_Idx  : Count   := 0;

   Reject_Cnt     : Count := 0;
   Last_Rej       : Reject_Reason := Reject_None;

   Yaml_Buf       : Stream_Element_Array (1 .. Yaml_Max_Bytes + 1);

   procedure Reject (Reason : Reject_Reason) is
   begin
      Reject_Cnt := Reject_Cnt + 1;
      Last_Rej   := Reason;
   end Reject;

   --  small text helpers

   WS : constant Ada.Strings.Maps.Character_Set :=
     Ada.Strings.Maps.To_Set (" " & ASCII.HT & ASCII.CR & ASCII.LF);

   function Trim_Ws (S : String) return String
   is (Ada.Strings.Fixed.Trim (S, WS, WS));

   procedure Append (T : in out Text; S : String) is
   begin
      Texts.Append (T, S, Ada.Strings.Right);          --  truncate, as the C snprintf does
   end Append;

   procedure Copy_Text (Dst : in out Text; Src : String; Max_Chars : Natural; Ok : out Boolean) is
   begin
      Ok := Src'Length <= Max_Chars;
      if Ok then
         Dst := Texts.To_Bounded_String (Src);
      end if;
   end Copy_Text;

   procedure Parse_U32 (S : String; V : out Count; Ok : out Boolean) is
   begin
      V  := 0;
      Ok := S'Length > 0;
      for C of S loop
         if C not in '0' .. '9' then
            Ok := False;
            return;
         end if;
         V := V * 10 + Count (Character'Pos (C) - Character'Pos ('0'));   --  wraps like the C uint32
      end loop;
   end Parse_U32;

   --  detection names: the error names without the "Err_" prefix, plus two special tokens

   type Det_Kind is (Det_Error, Det_Cbit_Response_Time, Det_None);

   type Detection is record
      Kind : Det_Kind   := Det_None;
      Err  : Error_Kind := Err_Parity;
   end record;

   function Err_Name (K : Error_Kind) return String is
      Img : constant String := Error_Kind'Image (K);
   begin
      return Img (Img'First + 4 .. Img'Last);          --  strip "ERR_"
   end Err_Name;

   procedure Find_Detection (Name : String; D : out Detection; Ok : out Boolean) is
   begin
      D  := (Kind => Det_None, Err => Err_Parity);
      Ok := True;
      if Name = "NONE" then
         D.Kind := Det_None;
      elsif Name = "CBIT_RESPONSE_TIME" then
         D.Kind := Det_Cbit_Response_Time;
      else
         Ok := False;
         for K in Error_Kind loop
            if Err_Name (K) = Name then
               D  := (Kind => Det_Error, Err => K);
               Ok := True;
            end if;
         end loop;
      end if;
   end Find_Detection;

   function Detection_Present (Obs : Observation; D : Detection) return Boolean
   is (case D.Kind is
         when Det_Cbit_Response_Time => Obs.Cbit_Response_Time_Violations > 0,
         when Det_Error              => Obs.Error_Type_Count (D.Err) > 0,
         when Det_None               => False);

   function Bus_Name (B : Bus_Select) return String
   is (case B is when Bus_A => "A", when Bus_B => "B", when Bus_Any => "ANY");

   --  arming, LLR-1553-ERR-006

   procedure Reset is
   begin
      Armed         := False;
      Loaded        := False;
      Active        := False;
      Scen_Count    := 0;
      Res_Count     := 0;
      Active_Idx    := 0;
      Active_Start  := 0;
      First_Inject  := 0;
      Words_Seen    := 0;
      Applied       := 0;
      Resp_Data_Idx := 0;
      Reject_Cnt    := 0;
      Last_Rej      := Reject_None;
      Rt_Addr       := (others => 0);
   end Reset;

   --  implements: LLR-1553-ERR-006
   procedure Arm (Token : Count; Ok : out Boolean) is
   begin
      if Token /= Count (Errinject_Token.Value) then
         Reject (Reject_Bad_Token);
         Ok := False;
      elsif not Loaded then
         Reject (Reject_Not_Loaded);
         Ok := False;
      else
         Armed := True;
         Ok    := True;
      end if;
   end Arm;

   procedure Disarm is
   begin
      Armed  := False;
      Active := False;
   end Disarm;

   function Is_Armed return Boolean is (Armed);

   function Reject_Count return Count is (Reject_Cnt);

   function Last_Reject return Reject_Reason is (Last_Rej);

   --  scenario file parsing

   type Have_Flags is mod 16;
   Have_Id       : constant Have_Flags := 1;
   Have_Target   : constant Have_Flags := 2;
   Have_Fault    : constant Have_Flags := 4;
   Have_Expected : constant Have_Flags := 8;
   Have_All      : constant Have_Flags := 15;

   procedure Set_Field
     (Sc : in out Scenario; Have : in out Have_Flags; Key, Value : String; Ok : out Boolean)
   is
      V : Count;
      D : Detection;
   begin
      Ok := True;

      if Key = "id" then
         Have := Have or Have_Id;
         Copy_Text (Sc.Id, Value, Id_Max_Chars, Ok);

      elsif Key = "target" then
         Have := Have or Have_Target;
         Ok   := False;
         for T in Terminal_Id loop
            if Terminal_Id'Image (T) = Value then
               Sc.Target := T;
               Ok := True;
            end if;
         end loop;

      elsif Key = "bus" then
         if Value = "A" then
            Sc.Bus_Sel := Bus_A;
         elsif Value = "B" then
            Sc.Bus_Sel := Bus_B;
         elsif Value = "ANY" then
            Sc.Bus_Sel := Bus_Any;
         else
            Ok := False;
         end if;

      elsif Key = "fault" then
         Have := Have or Have_Fault;
         Ok   := False;
         for F in Fault_Class loop
            if Fault_Class'Image (F) = Value then
               Sc.Fault := F;
               Ok := True;
            end if;
         end loop;

      elsif Key = "word_index" then
         Parse_U32 (Value, Sc.Word_Index, Ok);
      elsif Key = "count" then
         Parse_U32 (Value, Sc.Fault_Count, Ok);
      elsif Key = "delay_us" then
         Parse_U32 (Value, Sc.Delay_Us, Ok);

      elsif Key = "duration_ms" then
         Parse_U32 (Value, V, Ok);
         if Ok then
            Sc.Duration_Us := V * 1000;
         end if;

      elsif Key = "bit" then
         if Value = "BUSY" then
            Sc.Bit := Bit_Busy;
         elsif Value = "ME" then
            Sc.Bit := Bit_Me;
         elsif Value = "SF" then
            Sc.Bit := Bit_Sf;
         elsif Value = "TF" then
            Sc.Bit := Bit_Tf;
         else
            Ok := False;
         end if;

      elsif Key = "expected_detection" then
         Have := Have or Have_Expected;
         Find_Detection (Value, D, Ok);
         if Ok then
            Copy_Text (Sc.Expected_Detection, Value, Detection_Max_Chars, Ok);
         end if;

      elsif Key = "expected_recovery" then
         if Value = "NONE" or else Value = "RT_MARKED_FAILED" or else Value = "BUS_FAILOVER" then
            Copy_Text (Sc.Expected_Recovery, Value, Recovery_Max_Chars, Ok);
         else
            Ok := False;
         end if;

      elsif Key = "hlr" then
         Copy_Text (Sc.Hlr, Value, Ref_Max_Chars, Ok);
      elsif Key = "llr" then
         Copy_Text (Sc.Llr, Value, Ref_Max_Chars, Ok);
      elsif Key = "test_id" then
         Copy_Text (Sc.Test_Id, Value, Ref_Max_Chars, Ok);

      else
         Ok := False;                                   --  unknown key
      end if;
   end Set_Field;

   --  parses the whole file text into Scen / Scen_Count; on any failure Scen_Count is 0
   function Parse_Scenarios (Text : String) return Boolean is
      Pos       : Natural := Text'First;
      Cur       : Natural := 0;
      Cur_Valid : Boolean := False;
      Have      : Have_Flags := 0;
      Ok        : Boolean;

      function Fail return Boolean is
      begin
         Scen_Count := 0;
         return False;
      end Fail;
   begin
      Scen_Count := 0;

      while Pos <= Text'Last loop
         declare
            Line_First : constant Natural := Pos;
            Line_End   : Natural := Pos;
         begin
            while Line_End <= Text'Last and then Text (Line_End) /= ASCII.LF loop
               Line_End := Line_End + 1;
            end loop;
            Pos := Line_End + 1;

            if Line_End - Line_First >= Line_Max_Chars then
               return Fail;                             --  line too long
            end if;

            declare
               Raw  : constant String := Text (Line_First .. Line_End - 1);
               Hash : constant Natural := Ada.Strings.Fixed.Index (Raw, "#");
               Cut  : constant String := (if Hash = 0 then Raw else Raw (Raw'First .. Hash - 1));
               P    : constant String := Trim_Ws (Cut);
            begin
               if P'Length = 0 or else P = "scenarios:" then
                  null;
               else
                  declare
                     Dash    : constant Boolean :=
                       P'Length >= 2 and then P (P'First) = '-'
                       and then (P (P'First + 1) = ' ' or else P (P'First + 1) = ASCII.HT);
                     Content : constant String :=
                       (if Dash then Trim_Ws (P (P'First + 1 .. P'Last)) else P);
                  begin
                     if Dash then
                        if Cur_Valid and then Have /= Have_All then
                           return Fail;
                        end if;
                        if Scen_Count >= Max_Scenarios then
                           return Fail;
                        end if;
                        Cur        := Scen_Count;
                        Scen_Count := Scen_Count + 1;
                        Scen (Cur) := (others => <>);
                        Have       := 0;
                        Cur_Valid  := True;
                     end if;

                     if not Cur_Valid then
                        return Fail;
                     end if;

                     declare
                        Colon : constant Natural := Ada.Strings.Fixed.Index (Content, ":");
                     begin
                        if Colon = 0 then
                           return Fail;
                        end if;
                        declare
                           Key   : constant String := Trim_Ws (Content (Content'First .. Colon - 1));
                           Value : constant String := Trim_Ws (Content (Colon + 1 .. Content'Last));
                        begin
                           if Key'Length = 0 or else Value'Length = 0 then
                              return Fail;
                           end if;
                           Set_Field (Scen (Cur), Have, Key, Value, Ok);
                           if not Ok then
                              return Fail;
                           end if;
                        end;
                     end;
                  end;
               end if;
            end;
         end;
      end loop;

      if not Cur_Valid or else Have /= Have_All then
         return Fail;
      end if;
      return True;
   end Parse_Scenarios;

   --  implements: LLR-1553-ERR-007
   procedure Load_Scenarios (Yaml_Path : String; Sig_Path : String; Ok : out Boolean) is
      use Ada.Streams.Stream_IO;
      File : File_Type;
      Last : Stream_Element_Offset := 0;
   begin
      Loaded     := False;
      Armed      := False;
      Active     := False;
      Scen_Count := 0;
      Res_Count  := 0;
      Ok         := False;

      begin
         Open (File, In_File, Yaml_Path);
      exception
         when others =>
            Reject (Reject_Yaml_Missing);
            return;
      end;

      begin
         Read (File, Yaml_Buf, Last);                   --  at most Yaml_Max_Bytes + 1 bytes, once
      exception
         when others =>
            Close (File);
            Reject (Reject_Yaml_Missing);
            return;
      end;
      Close (File);

      if Last > Yaml_Max_Bytes then
         Reject (Reject_Yaml_Too_Large);
         return;
      end if;

      declare
         N    : constant Natural := Natural (Last);
         Data : Config_Signature.Byte_Array (1 .. N);
         Text : String (1 .. N);
         Sig  : Config_Signature.Sig_Result;
      begin
         for I in 1 .. N loop
            Data (I) := Byte (Yaml_Buf (Stream_Element_Offset (I)));
            Text (I) := Character'Val (Integer (Data (I)));
         end loop;

         --  the digest is checked on these very bytes, and the same bytes are parsed: no hash-then-reread window
         Sig := Config_Signature.Check_Signature (Data, Sig_Path);
         if Sig = Config_Signature.Sig_Missing then
            Reject (Reject_Sig_Missing);
            return;
         elsif Sig /= Config_Signature.Sig_Ok then
            Reject (Reject_Sig_Mismatch);
            return;
         end if;

         if not Parse_Scenarios (Text) then
            Reject (Reject_Yaml_Parse);
            return;
         end if;
      end;

      Loaded := True;
      Ok     := True;
   end Load_Scenarios;

   function Scenario_Count return Natural is (Scen_Count);

   function Get_Scenario (Index : Natural) return Scenario is (Scen (Index));

   procedure Register_Rt_Address (Terminal : Terminal_Id; Address : RT_Address) is
   begin
      Rt_Addr (Terminal) := Address;
   end Register_Rt_Address;

   --  fault primitives, LLR-1553-ERR-008

   procedure Sym_Flip (S : in out Symbols; Index : Natural) is
   begin
      S (Index / 8) := S (Index / 8) xor Byte (2 ** (7 - Index mod 8));
   end Sym_Flip;

   procedure Sym_Set (S : in out Symbols; Index : Natural; Value : Boolean) is
      Mask : constant Byte := Byte (2 ** (7 - Index mod 8));
   begin
      if Value then
         S (Index / 8) := S (Index / 8) or Mask;
      else
         S (Index / 8) := S (Index / 8) and not Mask;
      end if;
   end Sym_Set;

   procedure Mark_Applied (Bus : Bus_Model_Types.Context) is
   begin
      if Applied = 0 then
         First_Inject := Bus_Model.Now_Us (Bus);
      end if;
      Applied := Applied + 1;
   end Mark_Applied;

   --  implements: LLR-1553-ERR-008
   procedure Filter_Transmit
     (Bus    : in out Bus_Model_Types.Context;
      Chan   : Channel;
      Sender : Terminal_Id;
      Syms   : in out Symbols;
      Drop   : out Boolean)
   is
   begin
      Drop := False;

      if not (Armed and then Loaded and then Active) then
         return;                                        --  inert, LLR-1553-ERR-006 / ERR-007
      end if;

      declare
         Sc      : Scenario renames Scen (Active_Idx);
         Ch_Ok   : constant Boolean :=
           Sc.Bus_Sel = Bus_Any
           or else (Sc.Bus_Sel = Bus_A and then Chan = A)
           or else (Sc.Bus_Sel = Bus_B and then Chan = B);
         Decoded : constant Mil1553_Word.Decoded_Word := Mil1553_Word.Classify (Syms);
         Is_Status : constant Boolean := Decoded.Class = Valid and then Decoded.Sync = Command_Status;
         Is_Data   : constant Boolean := Decoded.Class = Valid and then Decoded.Sync = Data;
      begin
         if Sc.Fault = Illegal_Command then
            if Sender = BC and then Ch_Ok and then Applied < Sc.Fault_Count and then Is_Status then
               declare
                  Cmd : Command_Fields := Mil1553_Word.Unpack_Command (Decoded.Field);
               begin
                  if Cmd.Address = Rt_Addr (Sc.Target) and then Cmd.Sub /= 0 and then Cmd.Sub /= 31 then
                     Cmd.Sub := Illegal_Command_Subaddress;
                     Mil1553_Word.Pack_Command (Cmd, Syms);
                     Mark_Applied (Bus);
                  end if;
               end;
            end if;
            return;
         end if;

         if Sc.Fault = Bus_A_Fail or else Sc.Fault = Bus_B_Fail then
            return;                                     --  applied at scenario start
         end if;

         if Sender /= Sc.Target or else not Ch_Ok then
            return;
         end if;

         case Sc.Fault is
            when Parity_Bit_Flip =>
               if Words_Seen >= Sc.Word_Index and then Words_Seen < Sc.Word_Index + Sc.Fault_Count then
                  Sym_Flip (Syms, 38);                  --  parity bit cell = symbols 38, 39
                  Sym_Flip (Syms, 39);
                  Mark_Applied (Bus);
               end if;

            when Corrupt_Sync =>
               if Words_Seen >= Sc.Word_Index and then Words_Seen < Sc.Word_Index + Sc.Fault_Count then
                  for I in 0 .. 5 loop
                     Sym_Set (Syms, I, I mod 2 = 0);    --  1,0,1,0,1,0: neither valid sync
                  end loop;
                  Mark_Applied (Bus);
               end if;

            when Suppress_Response =>
               if Bus_Model.Now_Us (Bus) - Active_Start < Time_Us (Sc.Duration_Us) then
                  Drop := True;
                  Mark_Applied (Bus);
               end if;

            when Delay_Response =>
               if Applied < Sc.Fault_Count and then Is_Status then
                  Bus_Model.Advance_Us (Bus, Time_Us (Sc.Delay_Us));
                  Mark_Applied (Bus);
               end if;

            when Wrong_Word_Count =>
               if Is_Status then
                  Resp_Data_Idx := 0;                   --  a status word starts a new response
               elsif Is_Data then
                  if Resp_Data_Idx = Sc.Word_Index and then Applied < Sc.Fault_Count then
                     Drop := True;
                     Mark_Applied (Bus);
                  end if;
                  Resp_Data_Idx := Resp_Data_Idx + 1;
               end if;                                  --  not a well-formed status or data word: untouched

            when Status_Bit_Inject =>
               if Applied < Sc.Fault_Count and then Is_Status then
                  declare
                     St : Status_Fields := Mil1553_Word.Unpack_Status (Decoded.Field);
                  begin
                     case Sc.Bit is
                        when Bit_Busy => St.Busy := True;
                        when Bit_Me   => St.Message_Error := True;
                        when Bit_Sf   => St.Subsystem_Flag := True;
                        when Bit_Tf   => St.Terminal_Flag := True;
                     end case;
                     Mil1553_Word.Pack_Status (St, Syms);
                     Mark_Applied (Bus);
                  end;
               end if;

            when others =>
               null;
         end case;

         Words_Seen := Words_Seen + 1;
      end;
   end Filter_Transmit;

   --  scenario execution and result records, LLR-1553-ERR-009

   --  implements: LLR-1553-ERR-008, LLR-1553-ERR-009
   procedure Begin_Scenario
     (Index : Natural; Bus : in out Bus_Model_Types.Context; Ok : out Boolean)
   is
   begin
      if not Armed or else not Loaded then
         Reject (Reject_Not_Armed);
         Ok := False;
         return;
      end if;
      if Active or else Index >= Scen_Count then
         Ok := False;
         return;
      end if;

      Active        := True;
      Active_Idx    := Index;
      Active_Start  := Bus_Model.Now_Us (Bus);
      First_Inject  := Active_Start;
      Words_Seen    := 0;
      Applied       := 0;
      Resp_Data_Idx := 0;

      case Scen (Index).Fault is
         when Bus_A_Fail =>
            Bus_Model.Set_Channel_Failed (Bus, A, True);
            Mark_Applied (Bus);
         when Bus_B_Fail =>
            Bus_Model.Set_Channel_Failed (Bus, B, True);
            Mark_Applied (Bus);
         when others =>
            null;                                       --  word-level faults act in Filter_Transmit
      end case;

      Ok := True;
   end Begin_Scenario;

   procedure Observation_Clear (Obs : out Observation) is
   begin
      Obs := (others => <>);
   end Observation_Clear;

   procedure Observation_Add_Log (Obs : in out Observation; Records : Error_Records) is
   begin
      for R of Records loop
         Obs.Error_Type_Count (R.Kind) := Obs.Error_Type_Count (R.Kind) + 1;
      end loop;
   end Observation_Add_Log;

   --  implements: LLR-1553-ERR-009
   procedure End_Scenario
     (Bus : in out Bus_Model_Types.Context; Obs : Observation; Passed : out Boolean)
   is
   begin
      if not Active or else Res_Count >= Max_Scenarios then
         Passed := False;
         return;
      end if;

      declare
         Sc        : Scenario renames Scen (Active_Idx);
         R         : Result := (others => <>);
         Expected  : Detection;
         Found     : Boolean;
         Any       : Boolean := False;
         Applied_Ok, Detection_Ok, Recovery_Ok : Boolean;
         Recovery  : constant String := Texts.To_String (Sc.Expected_Recovery);

         procedure Note (Name : String) is
         begin
            Any := True;
            if Texts.Length (R.Observed) > 0 then
               Append (R.Observed, ",");
            end if;
            Append (R.Observed, Name);
         end Note;
      begin
         if Sc.Fault = Bus_A_Fail then
            Bus_Model.Set_Channel_Failed (Bus, A, False);
         elsif Sc.Fault = Bus_B_Fail then
            Bus_Model.Set_Channel_Failed (Bus, B, False);
         end if;

         Find_Detection (Texts.To_String (Sc.Expected_Detection), Expected, Found);   --  not found: treated as NONE, as in the C

         R.Scenario_Id := Sc.Id;
         R.Timestamp   := (if Applied > 0 then First_Inject else Active_Start);
         R.Terminal    := Sc.Target;
         R.Bus         := Sc.Bus_Sel;
         R.Fault       := Sc.Fault;
         R.Hlr         := Sc.Hlr;
         R.Llr         := Sc.Llr;
         R.Test_Id     := Sc.Test_Id;
         Append (R.Expected,
                 "detect=" & Texts.To_String (Sc.Expected_Detection) & ";recover=" & Recovery);

         --  observed detections, in the C's table order, then the recovery
         for K in Error_Kind loop
            if Obs.Error_Type_Count (K) > 0 then
               Note (Err_Name (K));
            end if;
         end loop;
         if Obs.Cbit_Response_Time_Violations > 0 then
            Note ("CBIT_RESPONSE_TIME");
         end if;
         if not Any then
            Append (R.Observed, "NONE");
         end if;
         Append (R.Observed,
                 ";recover="
                 & (if Obs.Failover_Occurred and then Obs.Rt_Marked_Failed then "BUS_FAILOVER,RT_MARKED_FAILED"
                    elsif Obs.Failover_Occurred then "BUS_FAILOVER"
                    elsif Obs.Rt_Marked_Failed then "RT_MARKED_FAILED"
                    else "NONE"));

         Applied_Ok := Applied > 0;
         if not Applied_Ok then
            Append (R.Observed, ";FAULT_NOT_APPLIED");
         end if;

         --  a detection that is expected but not observed is a FAILURE, never a pass
         Detection_Ok :=
           (if Expected.Kind = Det_None then not Any else Detection_Present (Obs, Expected));

         Recovery_Ok :=
           (if Recovery = "RT_MARKED_FAILED" then Obs.Rt_Marked_Failed
            elsif Recovery = "BUS_FAILOVER" then Obs.Failover_Occurred
            else True);

         R.Pass := Applied_Ok and then Detection_Ok and then Recovery_Ok;

         Res (Res_Count) := R;
         Res_Count       := Res_Count + 1;
         Active          := False;
         Passed          := R.Pass;
      end;
   end End_Scenario;

   function Result_Count return Natural is (Res_Count);

   function Get_Result (Index : Natural) return Result is (Res (Index));

   --  implements: LLR-1553-ERR-009 (machine-readable output)
   procedure Write_Results_Json (Path : String; Ok : out Boolean) is
      use Ada.Text_IO;

      F : File_Type;

      function Q (S : String) return String is ("""" & S & """");
   begin
      Ok := False;

      begin
         Create (F, Out_File, Path);
      exception
         when others =>
            return;
      end;

      Put_Line (F, "[");
      for I in 0 .. Res_Count - 1 loop
         declare
            R : Result renames Res (I);
         begin
            Put_Line
              (F,
               "  {""scenario_id"": " & Q (Texts.To_String (R.Scenario_Id))
               & ", ""timestamp_us"": " & Ada.Strings.Fixed.Trim (Time_Us'Image (R.Timestamp), Ada.Strings.Left)
               & ", ""terminal"": " & Q (Terminal_Id'Image (R.Terminal))
               & ", ""bus"": " & Q (Bus_Name (R.Bus))
               & ", ""fault"": " & Q (Fault_Class'Image (R.Fault))
               & ", ""expected"": " & Q (Texts.To_String (R.Expected))
               & ", ""observed"": " & Q (Texts.To_String (R.Observed))
               & ", ""pass_fail"": " & Q (if R.Pass then "PASS" else "FAIL")
               & ", ""HLR"": " & Q (Texts.To_String (R.Hlr))
               & ", ""LLR"": " & Q (Texts.To_String (R.Llr))
               & ", ""test_id"": " & Q (Texts.To_String (R.Test_Id))
               & "}" & (if I + 1 < Res_Count then "," else ""));
         end;
      end loop;
      Put_Line (F, "]");
      Close (F);
      Ok := True;
   exception
      when others =>
         if Is_Open (F) then
            Close (F);
         end if;
         Ok := False;
   end Write_Results_Json;

end Error_Inject;