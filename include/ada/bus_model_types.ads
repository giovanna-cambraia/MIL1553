--  project:    MIL1553
--  purpose:    the data structures of the software bus: the per-channel ring log, the per-listener read cursors and the virtual clock, Data only;
--              the operations are in Bus_Model (full interface) and Bus_Model_Monitor_If (receive-only interface)

--  realizes:   LLR-1553-BUS-001, LLR-1553-BUS-003 (data side)
--  parent HLR: HLR-1553-BUS-001

--  a 1553 channel is a broadcast medium: a transmitted word must be seen independently by every listener (BC, RT1, RT2, BM); each channel is therefore
--  an append-only ring log (Total_Written only ever grows; the ring overwrites its oldest entry once full) with ONE read cursor per listener; a listener
--  that falls more than a ring behind loses the oldest words and the loss is counted per listener, the same design as the C implementation
--  (include/c/bus_model_types.h)

--  why this is a separate package (LLR-1553-BM-003)
--    the bus monitor must not be able to transmit. If the monitor's package depended on Bus_Model, Transmit would be visible to it; so the types live here, Bus_Model has 
--    the full operations, and Bus_Model_Monitor_If has only the receive-side ones. Bm_Monitor withs this package and Bus_Model_Monitor_If and never names Bus_Model - 
--    the Ada compiler then makes "no transmit path" true by construction, not by convention

with Mil1553_Types; use Mil1553_Types;

package Bus_Model_Types
  with SPARK_Mode => On,
       Pure
is

   Queue_Capacity : constant := 64;   --  ring size, per channel

   --  uint32 in the C; modular so that wrap-around behaves identically. 2**32 is  a multiple of Queue_Capacity, so "Total_Written mod 64" stays consistent
   --  across a wrap
   type Counter is mod 2 ** 32;

   --  one word on the bus
   type Bus_Record is record
      Syms      : Symbols     := Zero_Symbols;
      Timestamp : Time_Us     := 0;           --  virtual clock at transmission
      Sender    : Terminal_Id := BC;
   end record;

   --  one channel: the ring log
   subtype Ring_Index is Natural range 0 .. Queue_Capacity - 1;

   type Ring is array (Ring_Index) of Bus_Record;

   type Channel_State is record
      Records       : Ring    := (others => (others => <>));
      Total_Written : Counter := 0;      --  words ever written on this channel
      Failed        : Boolean := False;  --  a failed channel delivers nothing
   end record;

   type Channel_States is array (Channel) of Channel_State;

   --  per-listener cursors, indexed by channel and terminal
   type Cursors is array (Channel, Terminal_Id) of Counter;

   --  the whole bus
   type Context is record
      Chan            : Channel_States := (others => (others => <>));

      --  normal receive position of each listener; a listener skips the words it sent itself, and loses (and counts) words it fell too far behind to read
      Read_Count      : Cursors := (others => (others => 0));
      Dropped_Count   : Cursors := (others => (others => 0));

      --  dedicated loopback cursor (LLR-1553-BIT-007), separate from Read_Count so that running IBIT never disturbs a terminal's normal receive position
      Loopback_Count  : Cursors := (others => (others => 0));

      Clock           : Time_Us := 0;    --  virtual clock, LLR-1553-BUS-003
      Transport_Errors : Counter := 0;   --  malformed transport input (peer-process mode)
   end record;

end Bus_Model_Types;