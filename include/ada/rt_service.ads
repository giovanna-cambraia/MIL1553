--  project:    MIL1553
--  purpose:    remote terminal: command reception and validation, data transfer, status word construction, mode command dispatch, broadcast handling, response timing, and 
--              the RT state machine

--  realizes:   LLR-1553-RT-001 .. LLR-1553-RT-013
--  parent HLRs: HLR-1553-RT-001 .. HLR-1553-RT-007, HLR-1553-BIT-004

--  run is a single-step function, called repeatedly by the RT role's main loop, doing at most one state transition's worth of work per call (mirrors Bc_Scheduler.Run_Frame)

--  differences from the C interface (rt_service.h), and why
--    * no stored pointers; the C context holds pointers to the bus and to an optional CBIT context, SPARK does not allow that aliasing, so Run takes the bus and the CBIT 
--      context as parameters; "CBIT attached" is a flag in the context (Attach_Cbit / Detach_Cbit); when it is not set the Cbit parameter is left exactly as it was
--    * init returns Ok instead of a bool: it refuses an own address of 31 (broadcast) and a response delay outside 4 .. 12 us (LLR-1553-RT-005)
--    * the C returns NULL for an out-of-range error log index; here Error_At has a precondition instead

--  behaviour worth knowing (all LLR-driven, see the C history for why)
--    * received data is STAGED and committed to the subaddress buffer only when the response goes out, after the RT has checked that no extra word followed (LLR-1553-RT-003)
--    * ME and BCR are not cleared by the transmit-status-word and transmit-last-command mode commands (LLR-1553-RT-006), so a BC can learn of an error the RT could not answer
--    * a broadcast is never answered, not even to report an error or busy (LLR-1553-RT-009)
--    * FAIL_SAFE is latched: it answers with TF set and executes nothing, and only a reset (mode command 8 or Reset) leaves it (LLR-1553-RT-013)

with Mil1553_Types;   use Mil1553_Types;
with Bus_Model_Types;
with Bit_Cbit;

package Rt_Service
  with SPARK_Mode => On
is

   subtype Response_Delay is Natural range T_Resp_Min_Us .. T_Resp_Max_Us;

   type Count is mod 2 ** 32;

   --  subaddress table, LLR-1553-RT-002
   type Subaddr_Config is record
      Enabled        : Boolean := False;
      Allow_Receive  : Boolean := False;   --  BC-to-RT (T/R = receive) permitted
      Allow_Transmit : Boolean := False;   --  RT-to-BC (T/R = transmit) permitted
      Min_Word_Count : Valid_Data_Word_Count := 1;
      Max_Word_Count : Valid_Data_Word_Count := Max_Data_Words;
   end record;

   type Subaddr_Configs is array (Subaddress) of Subaddr_Config;

   --  data held per subaddress, LLR-1553-RT-003 / RT-004
   subtype Data_Index is Natural range 0 .. Max_Data_Words - 1;
   type Data_Words is array (Data_Index) of Data_Word;

   type Subaddr_Buffer is record
      Words : Data_Words := (others => 0);
      Used  : Data_Word_Count := 0;      --  words currently committed
   end record;

   type Subaddr_Buffers is array (Subaddress) of Subaddr_Buffer;

   --  error log, LLR-1553-ERR-004 (fixed capacity, earliest records kept)
   subtype Log_Index is Natural range 0 .. Error_Log_Capacity - 1;
   type Error_Log is array (Log_Index) of Error_Record;

   type Channel_Flags is array (Channel) of Boolean;


   --  the RT
   type Rt_Context is record
      Own_Address     : RT_Address   := 0;
      Own_Terminal    : Terminal_Id  := RT1;      --  RT1 or RT2
      Configs         : Subaddr_Configs;
      Buffers         : Subaddr_Buffers;
      Delay_Us        : Response_Delay := RT_Response_Delay_Default_Us;   --  LLR-1553-RT-005

      State           : RT_State := RT_Reset;     --  LLR-1553-RT-013

      --  latched status conditions, LLR-1553-RT-006
      Message_Error_Latched      : Boolean := False;
      Broadcast_Received_Latched : Boolean := False;
      Subsystem_Busy             : Boolean := False;
      Subsystem_Flag             : Boolean := False;
      Terminal_Flag              : Boolean := False;

      --  the message in progress
      Pending_Cmd               : Command_Fields;
      Pending_Channel           : Channel := A;
      Pending_Is_Broadcast      : Boolean := False;
      Pending_Is_Mode           : Boolean := False;
      Pending_Transmission_Err  : Boolean := False;    --  LLR-1553-RT-010 path
      Pending_Validation        : Command_Validation := Command_Valid;
      Pending_Data_Expected     : Data_Word_Count := 0;
      Pending_Data_Received     : Data_Word_Count := 0;
      Pending_Data_To_Send      : Data_Word_Count := 0;
      Message_End_Time          : Time_Us := 0;
      Pending_Last_Word_End     : Time_Us := 0;        --  bus time the response delay runs from

      --  RT-to-RT, receiving side, LLR-1553-RT-007
      Pending_Is_RT_To_RT       : Boolean := False;
      Pending_Status_Pending    : Boolean := False;    --  awaiting the transmitting RT's status
      Pending_Xmit_Address      : RT_Address := 0;
      Pending_Wait_Start        : Time_Us := 0;

      --  staged data and commit, LLR-1553-RT-003
      Staging         : Data_Words := (others => 0);
      Mode_Data_Word  : Data_Word := 0;
      Commit_Pending  : Boolean := False;

      --  a word that belongs to the NEXT message is replayed, not lost
      Replay_Valid    : Boolean := False;
      Replay_Channel  : Channel := A;
      Replay_Record   : Bus_Model_Types.Bus_Record;

      --  mode command state, LLR-1553-RT-008 / RT-013
      Fail_Safe            : Boolean := False;
      Reset_Pending        : Boolean := False;
      Tf_Inhibited         : Boolean := False;
      Tx_Shutdown          : Channel_Flags := (others => False);
      Self_Test_Requested  : Boolean := False;
      Sync_Count           : Count := 0;
      Last_Sync_Data       : Data_Word := 0;
      Vector_Word          : Data_Word := 0;
      Latest_Command       : Data_Word := 0;           --  last command word accepted
      Last_Command         : Data_Word := 0;           --  the one before it (mode 18)
      Bit_Degraded         : Boolean := False;
      Bit_Word             : Data_Word := 0;
      Cbit_Attached        : Boolean := False;         --  LLR-1553-BIT-005 / BIT-006

      Log             : Error_Log;
      Log_Used        : Natural range 0 .. Error_Log_Capacity := 0;
      Log_Overflow    : Count := 0;
   end record;

   type Health is record
      State         : RT_State := RT_Reset;
      Busy          : Boolean  := False;
      Message_Error : Boolean  := False;
      Fail_Safe     : Boolean  := False;
   end record;

   --  lifecycle, LLR-1553-RT-013

   --  implements: LLR-1553-RT-001 .. RT-013
   --  starts the RT in RT_Reset; Ok is False (and the context is left in its efault, RT_Reset state) when Own_Address is 31 (the broadcast address is
   --  not a terminal's own) or the response delay is outside 4 .. 12 us, the caller must then run PBIT and call Pbit_Complete
   procedure Init
     (Ctx          : out Rt_Context;
      Own_Address  : RT_Address;
      Terminal     : Terminal_Id;
      Configs      : Subaddr_Configs;
      Delay_Us     : Natural;
      Ok           : out Boolean)
   with Global  => null,
        Depends => (Ctx => (Own_Address, Terminal, Configs, Delay_Us),
                    Ok  => (Own_Address, Delay_Us)),
        Pre     => True,
        Post    =>
          Ok = (Own_Address <= RT_Addr_Max and then Delay_Us in T_Resp_Min_Us .. T_Resp_Max_Us)
          and then Ctx.State = RT_Reset
          and then Ctx.Log_Used = 0
          and then not Ctx.Fail_Safe
          and then not Ctx.Cbit_Attached
          and then (if Ok then Ctx.Own_Address = Own_Address
                               and then Ctx.Own_Terminal = Terminal
                               and then Ctx.Delay_Us = Delay_Us);

   --  implements: LLR-1553-RT-013 (RESET -> IDLE on a PBIT pass, -> FAIL_SAFE on a fail)
   procedure Pbit_Complete (Ctx : in out Rt_Context; Passed : Boolean)
   with Global  => null,
        Depends => (Ctx => (Ctx, Passed)),
        Pre     => True,
        Post    => Ctx.Fail_Safe = not Passed
                   and then Ctx.State = (if Passed then RT_Idle else RT_Fail_Safe);

   --  implements: LLR-1553-RT-013 (IDLE -> WAIT_COMMAND); Ok is False from any other state
   procedure Enable (Ctx : in out Rt_Context; Ok : out Boolean)
   with Global  => null,
        Depends => (Ctx => Ctx, Ok => Ctx),
        Pre     => True,
        Post    => Ok = (Ctx'Old.State = RT_Idle)
                   and then (if Ok then Ctx.State = RT_Wait_Command else Ctx.State = Ctx'Old.State);

   --  implements: LLR-1553-RT-013 (any state -> RESET): clears the latches, FAIL_SAFE, inhibit-TF, transmitter shutdown, the BIT indication and any message in progress
   procedure Reset (Ctx : in out Rt_Context)
   with Global  => null,
        Depends => (Ctx => Ctx),
        Pre     => True,
        Post    =>
          Ctx.State = RT_Reset
          and then not Ctx.Fail_Safe
          and then not Ctx.Message_Error_Latched
          and then not Ctx.Broadcast_Received_Latched
          and then not Ctx.Terminal_Flag
          and then not Ctx.Tf_Inhibited
          and then not Ctx.Bit_Degraded
          and then Ctx.Bit_Word = 0
          and then not Ctx.Commit_Pending
          and then not Ctx.Replay_Valid
          and then Ctx.Own_Address = Ctx'Old.Own_Address
          and then Ctx.Log_Used = Ctx'Old.Log_Used;

   --  subsystem and BIT interface
   procedure Set_Busy (Ctx : in out Rt_Context; Busy : Boolean)
   with Global => null, Depends => (Ctx => (Ctx, Busy)), Pre => True,
        Post   => Ctx.Subsystem_Busy = Busy;

   procedure Set_Subsystem_Flag (Ctx : in out Rt_Context; Flag : Boolean)
   with Global => null, Depends => (Ctx => (Ctx, Flag)), Pre => True,
        Post   => Ctx.Subsystem_Flag = Flag;

   --  implements: LLR-1553-BIT-008; A BIT result reported by the BIT modules: a fatal one forces FAIL_SAFE at once, from any state
   procedure Report_Bit_Result
     (Ctx : in out Rt_Context; Fatal : Boolean; Set_Terminal : Boolean; Set_Subsystem : Boolean)
   with Global  => null,
        Depends => (Ctx => (Ctx, Fatal, Set_Terminal, Set_Subsystem)),
        Pre     => True,
        Post    => (if Fatal then Ctx.State = RT_Fail_Safe and then Ctx.Fail_Safe)
                   and then (if Set_Terminal then Ctx.Terminal_Flag)
                   and then (if Set_Subsystem then Ctx.Subsystem_Flag);

   --  implements: LLR-1553-BIT-008, LLR-1553-RT-006, LLR-1553-RT-008
   --  applies the current BIT state: Fail is fatal (TF and FAIL_SAFE), Degraded shows as the subsystem flag, Operational clears that indication (a
   --  FAIL_SAFE is NOT cleared: only a reset leaves it), other states change no flag, Bit_Word is what the transmit-BIT-word mode command returns
   procedure Apply_Bit_State (Ctx : in out Rt_Context; State : BIT_State; Bit_Word : Data_Word)
   with Global  => null,
        Depends => (Ctx => (Ctx, State, Bit_Word)),
        Pre     => True,
        Post    =>
          Ctx.Bit_Word = Bit_Word
          and then (if State = Fail then Ctx.State = RT_Fail_Safe and then Ctx.Fail_Safe)
          and then (if State = Degraded then Ctx.Bit_Degraded)
          and then (if State = Operational then not Ctx.Bit_Degraded
                                                and then Ctx.Fail_Safe = Ctx'Old.Fail_Safe);

   --  implements: LLR-1553-RT-008; the initiate-self-test mode command only raises a request; the role's main loop takes it and runs Bit_Ibit, so the RT does
   --  not depend on the BIT modules
   procedure Take_Self_Test_Request (Ctx : in out Rt_Context; Requested : out Boolean)
   with Global  => null,
        Depends => (Ctx => Ctx, Requested => Ctx),
        Pre     => True,
        Post    => Requested = Ctx'Old.Self_Test_Requested
                   and then not Ctx.Self_Test_Requested;

   --  implements: LLR-1553-RT-008. The word returned by transmit-vector-word (code 16).
   procedure Set_Vector_Word (Ctx : in out Rt_Context; Value : Data_Word)
   with Global => null, Depends => (Ctx => (Ctx, Value)), Pre => True,
        Post   => Ctx.Vector_Word = Value;

   --  implements: LLR-1553-BIT-005 / BIT-006. Once attached, Run reports every received word to the Cbit context and runs its watchdog tick and kick
   procedure Attach_Cbit (Ctx : in out Rt_Context)
   with Global => null, Depends => (Ctx => Ctx), Pre => True,
        Post   => Ctx.Cbit_Attached;

   procedure Detach_Cbit (Ctx : in out Rt_Context)
   with Global => null, Depends => (Ctx => Ctx), Pre => True,
        Post   => not Ctx.Cbit_Attached;

   --  implements: LLR-1553-RT-001 .. LLR-1553-RT-013
   --  advances the RT by one step; active is False exactly when the RT is in RT_Reset or RT_Idle (caller error: Pbit_Complete / Enable first); nothing else happens then

   --  what a step may do to the shared bus: add this RT's own words and advance the clock, and move this RT's OWN read cursors, it never moves another
   --  terminal's cursor; the error log only grows (the earliest records are kept), when CBIT is not attached the Cbit parameter is unchanged
   procedure Run
     (Ctx    : in out Rt_Context;
      Bus    : in out Bus_Model_Types.Context;
      Cbit   : in out Bit_Cbit.Cbit_Context;
      Active : out Boolean)
   with Global  => null,
        Depends => (Ctx => (Ctx, Bus), Bus => (Ctx, Bus), Cbit => (Ctx, Bus, Cbit), Active => Ctx),
        Pre     => True,
        Post    =>
          Active = (Ctx'Old.State not in RT_Reset | RT_Idle)
          and then Ctx.Own_Address = Ctx'Old.Own_Address
          and then Ctx.Own_Terminal = Ctx'Old.Own_Terminal
          and then Bus.Clock >= Bus'Old.Clock
          and then (for all C in Channel =>
                      (for all T in Terminal_Id =>
                         (if T /= Ctx.Own_Terminal then
                            Bus.Read_Count (C, T) = Bus'Old.Read_Count (C, T)
                            and then Bus.Loopback_Count (C, T) = Bus'Old.Loopback_Count (C, T))))
          and then Ctx.Log_Used >= Ctx'Old.Log_Used
          and then (for all I in 0 .. Ctx'Old.Log_Used - 1 => Ctx.Log (I) = Ctx'Old.Log (I))
          and then (if not Ctx'Old.Cbit_Attached then Cbit = Cbit'Old)
          and then (if not Active then Ctx = Ctx'Old);

   --  read access
   function Get_State (Ctx : Rt_Context) return RT_State
   is (Ctx.State) with Global => null;

   function Get_Health (Ctx : Rt_Context) return Health
   is (Health'(State         => Ctx.State,
               Busy          => Ctx.Subsystem_Busy,
               Message_Error => Ctx.Message_Error_Latched,
               Fail_Safe     => Ctx.State = RT_Fail_Safe))
   with Global => null;

   --  implements: LLR-1553-ERR-004 (read-only error log view)
   function Error_Log_Count (Ctx : Rt_Context) return Natural
   is (Ctx.Log_Used) with Global => null;

   function Error_At (Ctx : Rt_Context; Index : Log_Index) return Error_Record
   is (Ctx.Log (Index))
   with Global => null,
        Pre    => Index < Ctx.Log_Used;

   function Error_Overflow_Count (Ctx : Rt_Context) return Count
   is (Ctx.Log_Overflow) with Global => null;

end Rt_Service;