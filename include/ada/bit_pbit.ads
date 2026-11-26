--  project:    MIL1553
--  purpose:    power-up built-in test: RAM walking-bit test, ROM checksum, encoder/decoder loopback, timer sanity, bus idle check

--  realizes:   LLR-1553-BIT-001, LLR-1553-BIT-002, LLR-1553-BIT-003, LLR-1553-BIT-004
--  parent HLR: HLR-1553-BIT-001

--  host-based environment (PSAC 2.2): there is no physical RAM or ROM to fail, so both tests work through small models that let them be shown to detect a fault
--    * RAM: every test read and write goes through a stuck-bit model held in the context (Memory_Fault); when no fault is set the access is direct
--    * ROM: the checked region is a copy held in the context; the reference checksum is captured when the region is set (standing in for a value fixed at
--      build/flash time), so a test can corrupt the region afterwards with Poke_Rom_Byte

--  differences from the C interface (bit_pbit.h), and why
--    * the C uses function pointers (mil1553_bit_mem_ops_t) and a stored pointer to the ROM region; SPARK allows neither, so the memory ops become a data
--      model (Memory_Fault: one stuck bit at one offset) and the ROM region becomes a bounded copy (Rom_Capacity bytes)
--    * Set_Rom_Region COPIES the data; the C keeps the caller's pointer, so a caller there could corrupt it in place; here that is Poke_Rom_Byte
--    * Run takes the result, the bus and the listener as parameters, and returns Passed
--    * the codec round trip cannot see a pack failure: Mil1553_Word.Pack_* cannot fail (their field types are in range), so only the unpack side is exercised

--  PBIT may only run from Power_Up (LLR-1553-BIT-009); from any other state nothing runs, the call is counted as an ignored event and Passed is False

with Mil1553_Types; use Mil1553_Types;
with Bus_Model_Types;
with Bit_Types;

package Bit_Pbit
  with SPARK_Mode => On
is
   use type Bit_Types.Failure_Mask;
   use type Bit_Types.Test_Id;
   use type Bit_Types.Event_Count;
   use type Bit_Types.Item_Number;
   use type Bit_Types.Level_Result;
   use type Bus_Model_Types.Counter;
   use type Bus_Model_Types.Channel_States;
   use type Bus_Model_Types.Context;

   Ram_Test_Region_Size : constant := 64;
   Rom_Capacity         : constant := 256;
   Idle_Listen_Steps    : constant := 5;     --  polls of both channels ...
   Idle_Listen_Step_Us  : constant := 10;    --  ... this far apart: a 50 us listen period
   Timer_Check_Us       : constant := 100;

   type Word32 is mod 2 ** 32;

   subtype Ram_Index is Natural range 0 .. Ram_Test_Region_Size - 1;
   type Ram_Bytes is array (Ram_Index) of Byte;

   subtype Rom_Index is Natural range 0 .. Rom_Capacity - 1;
   type Rom_Bytes is array (Rom_Index) of Byte;

   --  implements: LLR-1553-BIT-001
   --  the stand-in for the C memory ops: while Active, bit Bit_Number of the byte at Offset reads back stuck at 1 (Stuck_High) or 0, whatever is written
   type Memory_Fault is record
      Active     : Boolean   := False;
      Offset     : Ram_Index := 0;
      Bit_Number : Natural range 0 .. 7 := 0;
      Stuck_High : Boolean   := False;
   end record;

   type Pbit_Context is record
      Ram_Region    : Ram_Bytes := (others => 0);
      Fault         : Memory_Fault;
      Rom           : Rom_Bytes := (others => 0);
      Rom_Length    : Natural range 0 .. Rom_Capacity := 0;
      Rom_Reference : Word32    := 0;          --  captured by Init / Set_Rom_Region
      Rom_Captured  : Boolean   := False;
   end record;

   --  implements: LLR-1553-BIT-002
   --  order-dependent (a swapped pair of bytes changes it): add the byte, then rotate left by one; the first Length bytes of Data
   function Rom_Checksum (Data : Rom_Bytes; Length : Natural) return Word32
   with Global => null,
        Pre    => Length <= Rom_Capacity;

   --  implements: LLR-1553-BIT-001, LLR-1553-BIT-002
   --  clears the RAM region, no memory fault, and selects the default ROM region (32 bytes) with its reference captured
   procedure Init (Ctx : out Pbit_Context)
   with Global  => null,
        Depends => (Ctx => null),
        Pre     => True,
        Post    =>
          (for all I in Ram_Index => Ctx.Ram_Region (I) = 0)
          and then not Ctx.Fault.Active
          and then Ctx.Rom_Captured
          and then Ctx.Rom_Length = 32
          and then Ctx.Rom_Reference = Rom_Checksum (Ctx.Rom, Ctx.Rom_Length);

   --  implements: LLR-1553-BIT-001
   --  sets the stuck-bit model; Clear_Memory_Fault restores direct access
   procedure Set_Memory_Fault (Ctx : in out Pbit_Context; Fault : Memory_Fault)
   with Global  => null,
        Depends => (Ctx => (Ctx, Fault)),
        Pre     => True,
        Post    => Ctx.Fault = Fault
                   and then Ctx.Ram_Region = Ctx'Old.Ram_Region
                   and then Ctx.Rom = Ctx'Old.Rom
                   and then Ctx.Rom_Reference = Ctx'Old.Rom_Reference;

   procedure Clear_Memory_Fault (Ctx : in out Pbit_Context)
   with Global  => null,
        Depends => (Ctx => Ctx),
        Pre     => True,
        Post    => not Ctx.Fault.Active
                   and then Ctx.Ram_Region = Ctx'Old.Ram_Region
                   and then Ctx.Rom = Ctx'Old.Rom
                   and then Ctx.Rom_Reference = Ctx'Old.Rom_Reference;

   --  implements: LLR-1553-BIT-002
   --  copies the first Length bytes of Data as the region to checksum and captures its reference checksum now; Ok is False (and nothing changes) for a
   --  Length of 0 or over Rom_Capacity
   procedure Set_Rom_Region
     (Ctx : in out Pbit_Context; Data : Rom_Bytes; Length : Natural; Ok : out Boolean)
   with Global  => null,
        Depends => (Ctx => (Ctx, Data, Length), Ok => Length),
        Pre     => True,
        Post    =>
          Ok = (Length in 1 .. Rom_Capacity)
          and then (if Ok then Ctx.Rom_Length = Length
                               and then Ctx.Rom_Captured
                               and then Ctx.Rom_Reference = Rom_Checksum (Data, Length)
                               and then (for all I in 0 .. Length - 1 => Ctx.Rom (I) = Data (I))
                    else Ctx = Ctx'Old);

   --  test hook, the Ada form of corrupting the ROM region after its reference was captured: changes one byte and leaves the reference alone
   procedure Poke_Rom_Byte (Ctx : in out Pbit_Context; Index : Rom_Index; Value : Byte)
   with Global  => null,
        Depends => (Ctx => (Ctx, Index, Value)),
        Pre     => True,
        Post    => Ctx.Rom (Index) = Value
                   and then Ctx.Rom_Reference = Ctx'Old.Rom_Reference
                   and then Ctx.Rom_Length = Ctx'Old.Rom_Length
                   and then Ctx.Rom_Captured = Ctx'Old.Rom_Captured;

   --  implements: LLR-1553-BIT-001, LLR-1553-BIT-004
   --  runs all five PBIT tests, in order RAM, ROM, codec loopback, timer, bus idle, and combines the results: R.Pbit.Failed_Mask has a bit per failed test,
   --  Failing_Test_Id is the first (lowest) failed test, Failing_Item is the RAM offset of the first mismatch (RAM) or the channel (bus idle), 0 otherwise;
   --  on a pass Power_Up -> Pbit_Running -> Operational, on any failure -> Fail (the blocking of LLR-1553-BIT-004: the owner must not enable the
   --  terminal unless Passed)

   --  the bus idle check listens on both channels for 50 us of virtual time with the Listener's cursor: this advances the shared virtual clock, and any word
   --  heard is consumed. Only the Listener's own cursors move, the channel contents are never touched
   procedure Run
     (Ctx      : in out Pbit_Context;
      R        : in out Bit_Types.Result;
      Bus      : in out Bus_Model_Types.Context;
      Listener : Terminal_Id;
      Passed   : out Boolean)
   with Global  => null,
        Depends => (Ctx => (Ctx, R, Bus), R => (Ctx, R, Bus), Bus => (R, Bus, Listener), Passed => (Ctx, R, Bus)),
        Pre     => True,
        Post    =>
          Bus.Chan = Bus'Old.Chan
          and then (if R'Old.State /= Power_Up then
                      not Passed
                      and then R.State = R'Old.State
                      and then R.Ignored_Event_Count = R'Old.Ignored_Event_Count + 1
                      and then R.Pbit = R'Old.Pbit
                      and then Ctx = Ctx'Old
                      and then Bus = Bus'Old
                    else
                      R.Pbit.Ever_Run
                      and then R.Pbit.Last_Passed = Passed
                      and then Passed = (R.Pbit.Failed_Mask = 0)
                      and then R.Pbit.Timestamp = Bus.Clock
                      and then R.State = (if Passed then Operational else Fail)
                      and then R.Cbit = R'Old.Cbit
                      and then R.Ibit = R'Old.Ibit
                      and then (if Passed then R.Pbit.Failing_Test_Id = 0 and then R.Pbit.Failing_Item = 0
                                else (R.Pbit.Failed_Mask and Bit_Types.Mask_Of (R.Pbit.Failing_Test_Id)) /= 0
                                     and then (for all T in Bit_Types.Test_Id =>
                                                 (if T < R.Pbit.Failing_Test_Id
                                                  then (R.Pbit.Failed_Mask and Bit_Types.Mask_Of (T)) = 0))
                                     and then (if R.Pbit.Failing_Test_Id /= Bit_Types.Pbit_Test_Ram
                                                  and then R.Pbit.Failing_Test_Id /= Bit_Types.Pbit_Test_Bus_Idle
                                               then R.Pbit.Failing_Item = 0))
                      and then (for all C in Channel =>
                                  (for all T in Terminal_Id =>
                                     (if T /= Listener then
                                        Bus.Read_Count (C, T) = Bus'Old.Read_Count (C, T)
                                        and then Bus.Loopback_Count (C, T) = Bus'Old.Loopback_Count (C, T)))));

end Bit_Pbit;