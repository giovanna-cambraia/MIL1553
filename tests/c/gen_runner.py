#!/usr/bin/env python3
"""
gen_runner.py  (tests/c/gen_runner.py)

generates a Unity test runner (main) for one test file, so the unit tests
build with plain make + gcc and need no ruby; test files stay compatible
with Ceedling (test_*.c, setUp/tearDown, void test_xxx(void))

Usage: gen_runner.py <test_file.c> <runner_out.c>
"""
import re
import sys

src, out = sys.argv[1], sys.argv[2]
text = open(src, encoding="utf-8").read()
tests = re.findall(r"^void\s+(test_\w+)\s*\(\s*void\s*\)\s*\{?", text, re.M)
if not tests:
    sys.exit(f"gen_runner: no test_* functions found in {src}")

lines = ['#include "unity.h"', "void setUp(void);", "void tearDown(void);"]
lines += [f"void {t}(void);" for t in tests]
lines += ["", "int main(void)", "{", "    UNITY_BEGIN();"]
lines += [f"    RUN_TEST({t});" for t in tests]
lines += ["    return UNITY_END();", "}", ""]
open(out, "w", encoding="utf-8").write("\n".join(lines))