--  project:  MIL1553
--  realizes: LLR-1553-BIT-005, LLR-1553-BIT-006

--  proof status: written to be proved with GNATprove, NOT yet run (no GNAT in theauthor's environment); Record_Word and Update_Result are field-by-field
--  framing arguments; Tick has a loop whose invariant says earlier channels are judged and nothing relevant to Is_Unhealthy has changed

package body Bit_Cbit
  with SPARK_Mode => On
is

   --  implements: LLR-1553-BIT-006
   function Window_Errors (Ch : Channel_Health) return Natural is
      Errors : Natural := 0;
   begin
      for I in 0 .. Ch.Filled_Count - 1 loop
         if not Ch.Outcomes (I) then
            Errors := Errors + 1;
         end if;
         pragma Loop_Invariant (Errors <= I + 1);
      end loop;
      return Errors;
   end Window_Errors;

   --  implements: LLR-1553-BIT-002
   procedure Init (Ctx : out Cbit_Context; Watchdog_Period_Us : Time_Us) is
   begin
      Ctx := (others => <>);
      Ctx.Watchdog_Period_Us :=
        (if Watchdog_Period_Us = 0 then Watchdog_Period_Default_Us else Watchdog_Period_Us);
   end Init;

   --  implements: LLR-1553-BIT-005
   procedure Record_Word (Ctx : in out Cbit_Context; Chan : Channel; Class : Word_Class) is
      Good : constant Boolean := Class = Valid;
   begin
      Ctx.Chan (Chan).Outcomes (Ctx.Chan (Chan).Next_Index) := Good;
      Ctx.Chan (Chan).Next_Index := (Ctx.Chan (Chan).Next_Index + 1) mod Window_Size;

      if Ctx.Chan (Chan).Filled_Count < Window_Size then
         Ctx.Chan (Chan).Filled_Count := Ctx.Chan (Chan).Filled_Count + 1;
      end if;

      Ctx.Chan (Chan).Class_Count (Class) := Ctx.Chan (Chan).Class_Count (Class) + 1;

      if Good then
         Ctx.Chan (Chan).Good_Count_Total := Ctx.Chan (Chan).Good_Count_Total + 1;
      else
         Ctx.Chan (Chan).Error_Count_Total := Ctx.Chan (Chan).Error_Count_Total + 1;
      end if;
   end Record_Word;

   --  implements: LLR-1553-BIT-005
   procedure Record_Response_Time (Ctx : in out Cbit_Context; Measured_Us : Time_Us) is
   begin
      if Measured_Us < T_Resp_Min_Us or else Measured_Us > T_Resp_Max_Us then
         Ctx.Response_Time_Violations := Ctx.Response_Time_Violations + 1;
      end if;
   end Record_Response_Time;

   --  implements: LLR-1553-BIT-005
   procedure Record_Word_Count_Violation (Ctx : in out Cbit_Context) is
   begin
      Ctx.Word_Count_Violations := Ctx.Word_Count_Violations + 1;
   end Record_Word_Count_Violation;

   --  implements: LLR-1553-BIT-006
   procedure Watchdog_Kick (Ctx : in out Cbit_Context; Now : Time_Us) is
   begin
      Ctx.Watchdog_Last_Progress := Now;
      Ctx.Watchdog_Armed := True;
      Ctx.Watchdog_Stalled := False;     --  the loop is moving; a trip already latched stays latched
   end Watchdog_Kick;

   --  implements: LLR-1553-BIT-006
   procedure Tick (Ctx : in out Cbit_Context; Now : Time_Us) is
   begin
      if Ctx.Watchdog_Armed
        and then Now >= Ctx.Watchdog_Last_Progress
        and then Now - Ctx.Watchdog_Last_Progress > Ctx.Watchdog_Period_Us
      then
         if not Ctx.Watchdog_Stalled then
            Ctx.Watchdog_Stalled := True;
            Ctx.Watchdog_Trip_Count := Ctx.Watchdog_Trip_Count + 1;   --  once per stall
         end if;
         Ctx.Watchdog_Tripped := True;
      end if;

      for C in Channel loop
         Ctx.Chan (C).Unhealthy := Is_Unhealthy (Ctx.Chan (C));

         pragma Loop_Invariant
           (for all D in Channel =>
              (if D <= C then Ctx.Chan (D).Unhealthy = Is_Unhealthy (Ctx.Chan (D))));
         pragma Loop_Invariant
           (for all D in Channel =>
              Ctx.Chan (D).Filled_Count = Ctx.Chan (D).Filled_Count'Loop_Entry
              and then Ctx.Chan (D).Outcomes = Ctx.Chan (D).Outcomes'Loop_Entry);
         pragma Loop_Invariant (Ctx.Watchdog_Tripped = Ctx.Watchdog_Tripped'Loop_Entry);
      end loop;
   end Tick;

   --  implements: LLR-1553-BIT-005, LLR-1553-BIT-006, LLR-1553-BIT-008, LLR-1553-BIT-009
   procedure Update_Result (Ctx : in out Cbit_Context; R : in out Bit_Types.Result; Now : Time_Us) is
      Failed     : constant Boolean               := Cycle_Failed (Ctx);
      Mask       : constant Bit_Types.Failure_Mask := Cause_Mask (Ctx);
      Cause      : constant Bit_Types.Test_Id      := First_Cause (Ctx);
      Resp_Seen  : constant Count                  := Ctx.Response_Time_Violations;
      Count_Seen : constant Count                  := Ctx.Word_Count_Violations;
      Legal      : Boolean;
   begin
      if R.State = Operational then
         Bit_Types.Apply (R, Bit_Types.Cbit_Begin, Legal);
         pragma Assert (Legal);
         Bit_Types.Apply (R, (if Failed then Bit_Types.Cbit_Nonfatal else Bit_Types.Cbit_Pass), Legal);
         pragma Assert (Legal);

      elsif R.State = Degraded then
         --  DEGRADED takes the outcome directly: a passing cycle clears it
         Bit_Types.Apply (R, (if Failed then Bit_Types.Cbit_Nonfatal else Bit_Types.Cbit_Pass), Legal);
         pragma Assert (Legal);

      else
         --  a CBIT cycle is not legal here (Power_Up, Pbit_Running, Cbit_Running, Ibit_Running, Fail); apply counts it as an ignored event and changes
         --  nothing else; nothing is consumed, so a pending watchdog trip or new violation waits for the next legal cycle
         Bit_Types.Apply (R, Bit_Types.Cbit_Begin, Legal);
         pragma Assert (not Legal);
         return;
      end if;

      R.Cbit := (Ever_Run        => True,
                 Last_Passed     => not Failed,
                 Failing_Test_Id => Cause,
                 Failing_Item    => 0,
                 Failed_Mask     => Mask,
                 Timestamp       => Now);

      --  what has now been reported is consumed
      Ctx.Watchdog_Tripped        := False;
      Ctx.Response_Time_Reported  := Resp_Seen;
      Ctx.Word_Count_Reported     := Count_Seen;
   end Update_Result;

end Bit_Cbit;