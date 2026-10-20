# tests/c/Makefile

# Builds and runs the C unit tests (Unity) without Ceedling or Ruby.
#
#   make            build and run every tests/c/test_*.c
#   make test_word  build and run one suite
#   make clean
#
# Layout assumed (relative to this file):  ../../include/c  ../../src/c  ./unity
# Override tools if needed:  make CC=gcc PYTHON=python

# On MinGW use the make that ships with it (mingw32-make) if `make` is absent

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

# Release-configuration library: every src/c/*.c except the role entry points. src/c/errinject/ is deliberately NOT listed: unit tests exercise the release
# build (bus_model_tx.c, no injector). Error-injection tests have their own test-configuration target
LIB_SRC := $(filter-out $(ROOT)/src/c/main_%.c,$(wildcard $(ROOT)/src/c/*.c))
LIB_OBJ := $(patsubst $(ROOT)/src/c/%.c,build/obj/%.o,$(LIB_SRC))

TESTS   := $(basename $(notdir $(wildcard test_*.c)))
RESULTS := $(addprefix build/,$(addsuffix .result,$(TESTS)))

.PHONY: all clean $(TESTS)
all: $(RESULTS)
	@echo
	@failed=0; for t in $(TESTS); do \
	    printf '%-28s ' $$t; grep -E 'Tests [0-9]+ Failures' build/$$t.log; \
	    grep -q "^FAIL" build/$$t.log && failed=1; done; \
	if [ $$failed -ne 0 ]; then echo "UNIT TESTS FAILED"; exit 1; fi; echo "ALL UNIT TESTS PASSED"

$(TESTS): %: build/%.result

build/obj/%.o: $(ROOT)/src/c/%.c | build/obj
	$(CC) $(CFLAGS) -c $< -o $@

build/unity.o: unity/unity.c | build
	$(CC) -std=c11 -Iunity -c $< -o $@

build/%_runner.c: %.c gen_runner.py | build
	$(PYTHON) gen_runner.py $< $@

build/%$(EXE): %.c build/%_runner.c build/unity.o $(LIB_OBJ) test_support.h
	$(CC) $(CFLAGS) $< build/$*_runner.c build/unity.o $(LIB_OBJ) -o $@

# Run the suite; keep its log; never abort the whole make on one failing suite
build/%.result: build/%$(EXE)
	@./build/$*$(EXE) > build/$*.log; rc=$$?; grep -v ":PASS" build/$*.log; \
	echo $$rc > $@; true

build build/obj:
	mkdir -p $@

clean:
	rm -rf build