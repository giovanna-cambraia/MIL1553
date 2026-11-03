# tests/c/Makefile

# Builds and runs the C unit tests (Unity), the error-injection suite and the
# error-injection campaign, with plain make + gcc (no Ceedling, no Ruby).

#   make              inspections, every unit suite, test_errinject, the campaign
#   make test_word    one release-configuration suite
#   make test_errinject   the error-injection suite (test configuration)
#   make campaign     the error-injection campaign (test configuration)
#   make inspect      only the inspection scripts
#   make clean

# Layout assumed (relative to this file):
#   ../../include/c   ../../src/c   ../../src/c/errinject   ../../config   ./unity
#   ../error_injection/campaign_main.c
# Override tools if needed:  make CC=gcc PYTHON=python
# On MinGW use the make that ships with it (mingw32-make) if `make` is absent.

# Two configurations are built, never mixed:
#   release : every src/c/*.c except main_*.c; NO src/c/errinject; bus_model_tx.c
#   test    : the same minus bus_model_tx.c, plus src/c/errinject/*.c, built with
#             -DMIL1553_ENABLE_ERROR_INJECTION (bus_model_tx_hooked.c replaces
#             bus_model_tx.c)

CC      ?= gcc
PYTHON  ?= python3
ROOT    := ../..

ifeq ($(OS),Windows_NT)
EXE := .exe
else
EXE :=
endif

CFLAGS  := -std=c11 -Wall -Wextra -Wshadow -g \
           -I$(ROOT)/include/c -I$(ROOT)/src/c -Iunity -I.

TEST_DEFS := -DMIL1553_ENABLE_ERROR_INJECTION -DERRINJECT_HARNESS_TOKEN=0xA5A5F00DU
TCFLAGS   := $(CFLAGS) $(TEST_DEFS) -I$(ROOT)/src/c/errinject

# ---- release configuration
LIB_SRC := $(filter-out $(ROOT)/src/c/main_%.c,$(wildcard $(ROOT)/src/c/*.c))
LIB_OBJ := $(patsubst $(ROOT)/src/c/%.c,build/obj/%.o,$(LIB_SRC))

# ---- test configuration
TLIB_SRC := $(filter-out $(ROOT)/src/c/bus_model_tx.c,$(LIB_SRC)) $(wildcard $(ROOT)/src/c/errinject/*.c)
TLIB_OBJ := $(patsubst $(ROOT)/src/c/%.c,build/tobj/%.o,$(TLIB_SRC))

TESTS    := $(filter-out test_errinject,$(basename $(notdir $(wildcard test_*.c))))
RESULTS  := $(addprefix build/,$(addsuffix .result,$(TESTS)))

CAMPAIGN_SRC := ../error_injection/campaign_main.c

.PHONY: all clean inspect campaign test_errinject $(TESTS)

all: inspect $(RESULTS) build/test_errinject.result build/campaign.result
	@echo
	@failed=0; for t in $(TESTS) test_errinject; do \
	    printf '%-28s ' $$t; grep -E 'Tests [0-9]+ Failures' build/$$t.log; \
	    grep -q "^FAIL" build/$$t.log && failed=1; done; \
	printf '%-28s ' campaign; grep -E 'CAMPAIGN (PASSED|FAILED)' build/campaign.log; \
	grep -q "CAMPAIGN FAILED" build/campaign.log && failed=1; \
	if [ $$failed -ne 0 ]; then echo "TESTS FAILED"; exit 1; fi; echo "ALL TESTS PASSED"

$(TESTS): %: build/%.result
test_errinject: build/test_errinject.result
campaign: build/campaign.result

# Inspections that cannot be expressed as unit tests (LLR-1553-BM-003, LLR-1553-ERR-005).
inspect:
	@sh check_bm_isolation.sh $(ROOT)/include/c $(ROOT)/src/c $(CC); r1=$$?; \
	 sh check_errinject_exclusion.sh $(ROOT)/include/c $(ROOT)/src/c $(CC); r2=$$?; \
	 exit $$((r1 + r2))

# ---- objects
build/obj/%.o: $(ROOT)/src/c/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/tobj/%.o: $(ROOT)/src/c/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TCFLAGS) -c $< -o $@

build/unity.o: unity/unity.c
	@mkdir -p build
	$(CC) -std=c11 -Iunity -c $< -o $@

# ---- release-configuration suites
build/%_runner.c: %.c gen_runner.py
	@mkdir -p build
	$(PYTHON) gen_runner.py $< $@

build/%$(EXE): %.c build/%_runner.c build/unity.o $(LIB_OBJ) test_support.h
	$(CC) $(CFLAGS) $< build/$*_runner.c build/unity.o $(LIB_OBJ) -o $@

# Run a suite; keep its log; never abort the whole make on one failing suite.
build/%.result: build/%$(EXE)
	@./build/$*$(EXE) > build/$*.log; rc=$$?; grep -v ":PASS" build/$*.log; \
	echo $$rc > $@; true

# ---- error-injection suite (test configuration; runs in build/ because it writes temp files)
build/test_errinject$(EXE): test_errinject.c build/test_errinject_runner.c build/unity.o $(TLIB_OBJ) test_support.h
	$(CC) $(TCFLAGS) test_errinject.c build/test_errinject_runner.c build/unity.o $(TLIB_OBJ) -o $@

build/test_errinject.result: build/test_errinject$(EXE)
	@cd build && ./test_errinject$(EXE) > test_errinject.log; rc=$$?; grep -v ":PASS" test_errinject.log; \
	echo $$rc > test_errinject.result; true

# ---- error-injection campaign (test configuration, signed scenario file from config/)
build/campaign$(EXE): $(CAMPAIGN_SRC) $(TLIB_OBJ)
	$(CC) $(TCFLAGS) $(CAMPAIGN_SRC) $(TLIB_OBJ) -o $@

build/campaign.result: build/campaign$(EXE) $(ROOT)/config/error_scenarios.yaml $(ROOT)/config/error_scenarios.sig
	@cd build && ./campaign$(EXE) ../$(ROOT)/config/error_scenarios.yaml ../$(ROOT)/config/error_scenarios.sig error_campaign.json > campaign.log; rc=$$?; \
	grep -E "FAIL|CAMPAIGN" campaign.log; echo $$rc > campaign.result; true

clean:
	rm -rf build