-- project: MIL1553

-- purpose: shared types and constants for the Ada/SPARK implementation, no behaviour: data definitions only (SDD 4.1); the ada counterpart of include/c/mil1553_types.h
-- realizes: no LLR of its own; gives every WORD/BC/RT/BM/BUS/REDUN/BIT/ERR LLR a common type vocabulary 
-- standard: Ada 2012, SPARK 2014 (SPARK_Mode On, package is Pure);

-- design notes
   -- * the two implementations share no source (SDD 1); what they share is behaviour, and the bus-level numbers, wherever a value crosses the bus (sync kind, T/R bit, channel
   -- terminal id) the enumeration carries an explicit representation clause equal to the C constant, so the wire layout design/bus_transport_contract.md is the same in
   -- both
   -- * Time_Us is modular, like the C uint64_t: elapsed time is computed as "now - start", which relies on the same wrap-around arithmetic 
   -- * RT_Address, Subaddress and Word_Count_Field are distinct 5-bit types, so an address cannot be passed where a subaaddress is expected. A mode code travels in the word 
   --  count field (LLR-1553-WORD-001), hence Mode_Code is a subtype of Word_Count_Field

package Mil1553_Types 
   with  SPARK_Mode => On,
      Pure 
is 

   -- project-wide constants (design/timing_budget.md section 2, SDD 1)

   Bit_Time_Us        : constant := 1;
   Word_Time_Us       : constant := 20;
   T_Resp_Min_Us      : constant := 4;
   T_Resp_Max_Us      : constant := 12;
   T_Gap_Min_Us       : constant := 4;
   T_No_Response_Us   : constant := 14;
   Max_Data_Words     : constant := 32;
   Symbols_Per_Word   : constant := 40;     --  LLR-1553-WORD-006

   RT_Addr_Broadcast  : constant := 31;
   RT_Addr_Max        : constant := 30; -- 31 is broadcast, not a real RT

   Error_Log_Capacity : constant := 64; -- LLR-1553-ERR-004

   Redun_Fail_Threshold_Default :  constant := 3; -- LLR-1553-REDUN-002
   RT_Response_Delay_Default_Us :  constant := 8; -- LLR-1553-REDUN-005

   -- scalars 

   type Byte is mod 2 ** 8;

   -- virtual clock, microseconds (LLR-1553-BUS-003)
   type Time_Us is mod 2 ** 64;

   type RT_Address is range 0 .. 31; -- 31 = broadcast
   type Subaddress is range 0 .. 31; -- 0 and 31 = mode indicator 
   type Word_Count_Field is range 0 .. 31; -- wire value: 0 means 32 words 
   subtype Mode_Code is Word_Count_Field;

   --16-bit data word payload 
   type Data_Word is mod 2 ** 16;

   -- logical number of data words: 1 .. 32 for a real message, 0 where a message has no data block
   subtype Data_Word_Count is Natural range 0 .. Max_Data_Words;
   subtype Valid_Data_Word_Count is Natural range 1 .. Max_Data_Words;

   --  symbols (LLR-1553-WORD-006, bus_transport_contract.md 4)
   --  40 Manchester half-bit symbols packed 8 per byte, symbol 0 in the most
   subtype Symbol_Index is Natural range 0 .. Symbols_Per_Word - 1;
   type Symbols is array (Natural range 0 .. 4) of Byte;

   Zero_Symbols : constant Symbols := (others => 0);

   --  wire-significant enumerations (representation = the C constant)
   type Sync_Kind is (Command_Status, Data, Invalid);
   for Sync_Kind use (Command_Status => 0, Data => 1, Invalid => 2);

   type TR_Bit is (Receive, Transmit);
   for TR_Bit use (Receive => 0, Transmit => 1);

   type Channel is (A, B);
   for Channel use (A => 0, B => 1);

   type Terminal_Id is (BC, RT1, RT2, BM);
   for Terminal_Id use (BC => 0, RT1 => 1, RT2 => 2, BM => 3);

   --  word classification, LLR-1553-WORD-008
   type Word_Class is (Valid, Sync_Error, Manchester_Error, Parity_Error);
   for Word_Class use (Valid => 0, Sync_Error => 1, Manchester_Error => 2, Parity_Error => 3);

   --  command validation result, LLR-1553-RT-002
   type Command_Validation is
     (Command_Valid, Illegal_Subaddress, Illegal_Direction, Illegal_Word_Count);

   --  message type, LLR-1553-BM-002
   type Message_Kind is (BC_To_RT, RT_To_BC, RT_To_RT, Mode_Command, Broadcast);

   --  command word fields, LLR-1553-WORD-001
   type Command_Fields is record
      Address          : RT_Address       := 0;
      Direction        : TR_Bit           := Receive;
      Sub              : Subaddress       := 0;
      Word_Count_Raw   : Word_Count_Field := 0;   --  wire value, 0 = 32 words
   end record;

   --  status word fields, LLR-1553-WORD-003

   --  bits 7..5 of the word are reserved: always zero on encode; reserved_Nonzero is set by unpack if any of them was received set
   
   type Status_Fields is record
      Address            : RT_Address := 0;
      Message_Error      : Boolean    := False;   --  ME
      Instrumentation    : Boolean    := False;
      Service_Request    : Boolean    := False;
      Broadcast_Received : Boolean    := False;   --  BCR
      Busy               : Boolean    := False;
      Subsystem_Flag     : Boolean    := False;   --  SF
      DBC_Acceptance     : Boolean    := False;
      Terminal_Flag      : Boolean    := False;   --  TF
      Reserved_Nonzero   : Boolean    := False;
   end record;

   --  BIT levels and states, LLR-1553-BIT-008 / BIT-009
   type BIT_Level is (PBIT, CBIT, IBIT);

   type BIT_State is
     (Power_Up, Pbit_Running, Operational, Cbit_Running, Ibit_Running, Degraded, Fail);
   for BIT_State use
     (Power_Up => 0, Pbit_Running => 1, Operational => 2, Cbit_Running => 3,
      Ibit_Running => 4, Degraded => 5, Fail => 6);

   -- BC and RT states, LLR-1553-BC-010 / LLR-1553-RT-013
    type BC_State is
     (BC_Idle, BC_Major_Frame, BC_Minor_Frame, BC_Wait_Response,
      BC_Timeout, BC_Failover, BC_Degraded);

   type RT_State is
     (RT_Reset, RT_Idle, RT_Wait_Command, RT_Validate, RT_Execute,
      RT_Respond, RT_Error, RT_Busy, RT_Fail_Safe);

   --  error types and the error record, LLR-1553-ERR-001 .. ERR-004
   type Error_Kind is
     (Err_Parity,
      Err_Sync,
      Err_Manchester,
      Err_No_Response,
      Err_Status_Address_Mismatch,
      Err_Status_ME,
      Err_Status_Busy,
      Err_Status_SF,
      Err_Status_TF,
      Err_Word_Count_Mismatch,
      Err_Minor_Frame_Overrun,
      Err_Transport);

   type Message_Id is mod 2 ** 32;

   type Error_Record is record
      Timestamp : Time_Us     := 0;
      Terminal  : Terminal_Id := BC;
      Chan      : Channel     := A;
      Kind      : Error_Kind  := Err_Parity;
      Message   : Message_Id  := 0;
   end record;

   --  fault classes, LLR-1553-ERR-006 / ERR-008; the type is defined unconditionally: a type carries no behaviour, so it does not breach
   --  LLR-1553-ERR-005 (only the injector's implementation is release-excluded)
   type Fault_Class is
     (Parity_Bit_Flip,
      Corrupt_Sync,
      Suppress_Response,
      Delay_Response,
      Wrong_Word_Count,
      Status_Bit_Inject,
      Illegal_Command,
      Bus_A_Fail,
      Bus_B_Fail);

end Mil1553_Types;