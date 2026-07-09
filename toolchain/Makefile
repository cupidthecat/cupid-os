.SUFFIXES:

ifeq ($(OS),Windows_NT)
PYTHON ?= python
ifeq ($(origin CC),default)
CC := clang
endif
EXE := .exe
else
PYTHON ?= python3
ifeq ($(origin CC),default)
CC := gcc
endif
EXE :=
endif

BUILD_DIR := build
CONTRACT := $(BUILD_DIR)/core-contract$(EXE)
OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
           $(BUILD_DIR)/core_contract.o
CFLAGS ?= -std=c11 -O2 -pedantic -Werror -Wall -Wextra -Wshadow \
          -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
          -Wmissing-prototypes -Wconversion -Wsign-conversion
CPPFLAGS ?= -I.

.DEFAULT_GOAL := all

all: $(CONTRACT)

$(BUILD_DIR):
	$(PYTHON) -c "from pathlib import Path; Path(r'$@').mkdir(parents=True, exist_ok=True)"

$(BUILD_DIR)/ctool.o: ctool.c ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ctool_host.o: ctool_host.c ctool_host.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/core_contract.o: tests/core_contract.c ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(CONTRACT): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

test: $(CONTRACT)
	$(CONTRACT) foundations
	$(CONTRACT) paths
	$(CONTRACT) diagnostics
	$(CONTRACT) limits

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree(r'$(BUILD_DIR)', ignore_errors=True)"

.PHONY: all test clean
