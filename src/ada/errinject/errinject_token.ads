--  project:    MIL1553
--  purpose:    the harness token that arms the error injector (TEST BUILD ONLY)

--  realizes:   LLR-1553-ERR-006

--  the Ada form of the C's -DERRINJECT_HARNESS_TOKEN: a named number that the test harness passes to Error_Inject.Arm. It guards against ACCIDENTAL arming,
--  e.g. a role main calling Arm by mistake; it is not a security boundary, build-time exclusion (this file lives only in src/ada/errinject, which
--  mil1553_release.gpr does not list) is the real guarantee

--  a named number, not a typed constant, so it converts to whatever type Arm takes without this package depending on Error_Inject

package Errinject_Token
  with Pure
is

   Value : constant := 16#C0DE_1553#;

end Errinject_Token;