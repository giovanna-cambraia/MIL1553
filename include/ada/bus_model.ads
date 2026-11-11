--  project:    MIL1553
--  purpose:    the software dual-redundant bus: two independent channels, a virtual clock, and multicast delivery of every word to every listener 
--             (BC, RT1, RT2, BM). Full interface, used by BC, RT and the BIT modules. (The bus monitor uses Bus_Model_Monitor_If.)

--  realizes:   LLR-1553-BUS-001, LLR-1553-BUS-003, and the in-process part of LLR-1553-BUS-002
--  parent HLR: HLR-1553-BUS-001
--
--  the data structures are in Bus_Model_Types (see the design note there)
--
--  design notes
--    * transmit is the interposition point for the error injector's word- corrupting and response-suppression faults (SDD 4.5); its BODY is a subunit (separate), chosen by 
--       the build profile: the release subunit forwards to Bus_Model.Raw.Transmit_Raw, the test subunit (under src/ada/errinject, not in the release project) applies the 
--       injector's filter first, nothing in this spec or in the release build can reach the injector (LLR-1553-ERR-005)
--    * Bus_Model.Raw is a PRIVATE child package: only the parent's own body and its other children can name it; that is the Ada equivalent of the C's
--      bus_model_internal.h, enforced by the compiler instead of by a comment
--    * because the test subunit may drop a word the sender believes it sent (SUPPRESS_RESPONSE, WRONG_WORD_COUNT), Transmit's postcondition does not promise that a 
--      successful transmit stored a word. The exact storage behaviour is the contract of Bus_Model.Raw.Transmit_Raw

with Mil1553_Types;   use Mil1553_Types;
with Bus_Model_Types; use Bus_Model_Types;

package Bus_Model
  with SPARK_Mode => On
is

   --  implements: LLR-1553-BUS-001, LLR-1553-BUS-003
   --  returns the bus to its start condition: nothing written, no channel failed, every cursor and counter zero, clock at zero
   procedure Init (Ctx : out Context)
   with Global  => null,
        Depends => (Ctx => null),
        Pre     => True,
        Post    =>
          Ctx.Clock = 0
          and then Ctx.Transport_Errors = 0
          and then (for all C in Channel =>
                      Ctx.Chan (C).Total_Written = 0
                      and then not Ctx.Chan (C).Failed
                      and then (for all T in Terminal_Id =>
                                  Ctx.Read_Count (C, T) = 0
                                  and then Ctx.Dropped_Count (C, T) = 0
                                  and then Ctx.Loopback_Count (C, T) = 0));

   --  implements: LLR-1553-BUS-001, LLR-1553-BUS-002
   --  puts one Manchester-encoded word on a channel, visible independently to
   --  every listener's own read cursor; sent is False exactly when the channel is failed (and then nothing changes); a full ring never makes it fail - the ring overwrites 
   --  its oldest entry and a slow listener loses history, counted per listener, which is not a transmit-side error

   --  only this channel's state can change (the test build's fault hook may delay or drop the word, or alter its symbols, but never touches another channel or a failed flag)
   procedure Transmit
     (Ctx    : in out Context;
      Chan   : Channel;
      Syms   : Symbols;
      Sender : Terminal_Id;
      Sent   : out Boolean)
   with Global  => null,
        Depends => (Ctx  => (Ctx, Chan, Syms, Sender),
                    Sent => (Ctx, Chan)),
        Pre     => True,
        Post    =>
          Sent = not Ctx'Old.Chan (Chan).Failed
          and then Ctx.Chan (Chan).Failed = Ctx'Old.Chan (Chan).Failed
          and then (for all C in Channel => (if C /= Chan then Ctx.Chan (C) = Ctx'Old.Chan (C)))
          and then Ctx.Read_Count     = Ctx'Old.Read_Count
          and then Ctx.Loopback_Count = Ctx'Old.Loopback_Count
          and then (if Ctx'Old.Chan (Chan).Failed then Ctx = Ctx'Old);

   --  implements: LLR-1553-BUS-001, LLR-1553-BUS-002
   --  non-blocking poll for one listener: the next word on Chan that this listener has not read and did not send itself (a terminal's own transmissions are skipped on its 
   --  own cursor); a listener that has fallen more than a ring behind is moved forward and the words it lost are added to its Dropped_Count; got is False when the channel is 
   --  failed, or the listener has caught up (its cursor then equals Total_Written)

   --  only the calling listener's own cursors on this channel move: BC, RT1, RT2 and BM can all poll the same channel without disturbing one another
   procedure Receive
     (Ctx      : in out Context;
      Chan     : Channel;
      Listener : Terminal_Id;
      Rec      : out Bus_Record;
      Got      : out Boolean)
   with Global  => null,
        Depends => (Ctx => (Ctx, Chan, Listener),
                    Rec => (Ctx, Chan, Listener),
                    Got => (Ctx, Chan, Listener)),
        Pre     => True,
        Post    =>
          Ctx.Chan = Ctx'Old.Chan
          and then Ctx.Clock = Ctx'Old.Clock
          and then Ctx.Loopback_Count = Ctx'Old.Loopback_Count
          and then (for all C in Channel =>
                      (for all T in Terminal_Id =>
                         (if C /= Chan or else T /= Listener then
                            Ctx.Read_Count (C, T) = Ctx'Old.Read_Count (C, T)
                            and then Ctx.Dropped_Count (C, T) = Ctx'Old.Dropped_Count (C, T))))
          and then (if Got then Rec.Sender /= Listener)
          and then (if Ctx'Old.Chan (Chan).Failed then not Got and then Ctx = Ctx'Old
                    elsif not Got then
                      Ctx.Read_Count (Chan, Listener) = Ctx.Chan (Chan).Total_Written);

   --  implements: LLR-1553-BUS-001, LLR-1553-BUS-002 (loopback variant for LLR-1553-BIT-007)
   --  the loopback counterpart of Receive: the next word sent by Terminal ITSELF (the opposite filter), skipping words from other terminals, on a cursor
   --  dedicated to loopback so the terminal's normal receive position is untouched; IBIT uses it to read back its own pattern through the real channel even when other traffic 
   --  is already queued
   procedure Receive_Loopback
     (Ctx      : in out Context;
      Chan     : Channel;
      Terminal : Terminal_Id;
      Rec      : out Bus_Record;
      Got      : out Boolean)
   with Global  => null,
        Depends => (Ctx => (Ctx, Chan, Terminal),
                    Rec => (Ctx, Chan, Terminal),
                    Got => (Ctx, Chan, Terminal)),
        Pre     => True,
        Post    =>
          Ctx.Chan = Ctx'Old.Chan
          and then Ctx.Clock = Ctx'Old.Clock
          and then Ctx.Read_Count = Ctx'Old.Read_Count
          and then Ctx.Dropped_Count = Ctx'Old.Dropped_Count
          and then (if Got then Rec.Sender = Terminal)
          and then (if Ctx'Old.Chan (Chan).Failed then not Got and then Ctx = Ctx'Old);

   --  implements: LLR-1553-BUS-001
   --  marks a channel failed or restored; on the transition to failed, every listener's cursor (normal and loopback) on that channel is moved to the channel's current 
   --  write position: "a failed channel delivers nothing", no backlog is held for later, and none is replayed when the channel is restored
   procedure Set_Channel_Failed (Ctx : in out Context; Chan : Channel; Failed : Boolean)
   with Global  => null,
        Depends => (Ctx => (Ctx, Chan, Failed)),
        Pre     => True,
        Post    =>
          Ctx.Chan (Chan).Failed = Failed
          and then Ctx.Chan (Chan).Records       = Ctx'Old.Chan (Chan).Records
          and then Ctx.Chan (Chan).Total_Written = Ctx'Old.Chan (Chan).Total_Written
          and then (for all C in Channel => (if C /= Chan then Ctx.Chan (C) = Ctx'Old.Chan (C)))
          and then Ctx.Clock = Ctx'Old.Clock
          and then (if Failed then
                      (for all T in Terminal_Id =>
                         Ctx.Read_Count (Chan, T)     = Ctx.Chan (Chan).Total_Written
                         and then Ctx.Loopback_Count (Chan, T) = Ctx.Chan (Chan).Total_Written));

   --  implements: LLR-1553-BUS-001
   function Is_Channel_Failed (Ctx : Context; Chan : Channel) return Boolean
   is (Ctx.Chan (Chan).Failed)
   with Global => null;

   --  implements: LLR-1553-BUS-001
   --  how many words the listener lost on this channel by falling more than a ring behind (zero in normal operation: every listener polls every step)
   function Listener_Dropped_Count
     (Ctx : Context; Chan : Channel; Listener : Terminal_Id) return Counter
   is (Ctx.Dropped_Count (Chan, Listener))
   with Global => null;

   --  implements: LLR-1553-BUS-003
   function Now_Us (Ctx : Context) return Time_Us
   is (Ctx.Clock)
   with Global => null;

   --  implements: LLR-1553-BUS-003
   --  the only sanctioned way the virtual clock moves: by transmitted word durations and explicit waits (LLR-1553-BUS-003); callers advance it after each word or wait; 
   --  nothing reads the host clock
   procedure Advance_Us (Ctx : in out Context; Delta_Us : Time_Us)
   with Global  => null,
        Depends => (Ctx => (Ctx, Delta_Us)),
        Pre     => True,
        Post    => Ctx.Clock = Ctx'Old.Clock + Delta_Us
                   and then Ctx.Chan = Ctx'Old.Chan
                   and then Ctx.Read_Count = Ctx'Old.Read_Count;

end Bus_Model;