--  project:    MIL1553
--  purpose:    The receive-only view of the bus, for Bm_Monitor and nobody else.

--  realizes:   LLR-1553-BM-003 ("no transmit function reachable from the bus monitor - no call, no reference"), LLR-1553-BM-001 (receive side)
--  parent HLR: HLR-1553-BM-001
--
--  how LLR-1553-BM-003 is met
--    Bm_Monitor withs Bus_Model_Types and THIS package, and never Bus_Model; the only bus operations visible to it are the two declared here, so Transmit,
--    Set_Channel_Failed and Advance_Us are not merely unused but unnameable; this spec has no with-clause for Bus_Model, only this package's BODY does, and a
--    with-clause in a body is invisible to its clients

--  differences from the C (bus_model_monitor_if.h)
--    * receive has no Listener parameter; the monitor is always the BM listener, so it cannot read through another terminal's cursor by mistake either
--    * nothing here moves the clock, so the monitor cannot influence timing

--  check (tests/ada/check_bm_isolation.sh): no "with Bus_Model;" appears in bm_monitor.ads or bm_monitor.adb

with Mil1553_Types;   use Mil1553_Types;
with Bus_Model_Types; use Bus_Model_Types;

package Bus_Model_Monitor_If
  with SPARK_Mode => On
is

   --  Implements: LLR-1553-BM-001, LLR-1553-BUS-002 (receive side only)
   --  non-blocking poll as the BM listener: the next word on Chan the monitor has not yet read; only the BM's own cursor on this channel moves, so reading
   --  never takes a word away from BC, RT1 or RT2, and never changes the channel; got is False when the channel is failed, or the monitor has caught up
   procedure Receive
     (Ctx  : in out Context;
      Chan : Channel;
      Rec  : out Bus_Record;
      Got  : out Boolean)
   with Global  => null,
        Depends => (Ctx => (Ctx, Chan),
                    Rec => (Ctx, Chan),
                    Got => (Ctx, Chan)),
        Pre     => True,
        Post    =>
          Ctx.Chan = Ctx'Old.Chan
          and then Ctx.Clock = Ctx'Old.Clock
          and then Ctx.Loopback_Count = Ctx'Old.Loopback_Count
          and then (for all C in Channel =>
                      (for all T in Terminal_Id =>
                         (if C /= Chan or else T /= BM then
                            Ctx.Read_Count (C, T) = Ctx'Old.Read_Count (C, T)
                            and then Ctx.Dropped_Count (C, T) = Ctx'Old.Dropped_Count (C, T))))
          and then (if Got then Rec.Sender /= BM)
          and then (if Ctx'Old.Chan (Chan).Failed then not Got and then Ctx = Ctx'Old
                    elsif not Got then Ctx.Read_Count (Chan, BM) = Ctx.Chan (Chan).Total_Written);

   --  implements: LLR-1553-BUS-003
   function Now_Us (Ctx : Context) return Time_Us
   is (Ctx.Clock)
   with Global => null;

end Bus_Model_Monitor_If;