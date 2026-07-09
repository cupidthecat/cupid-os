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

BUILD_DIR ?= build
CONTRACT := $(BUILD_DIR)/core-contract$(EXE)
ELF32_CONTRACT := $(BUILD_DIR)/elf32-contract$(EXE)
CORE_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                $(BUILD_DIR)/core_contract.o
ELF32_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                 $(BUILD_DIR)/elf32.o $(BUILD_DIR)/elf32_contract.o
CFLAGS ?= -std=c11 -O2 -pedantic -Werror -Wall -Wextra -Wshadow \
          -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
          -Wmissing-prototypes -Wconversion -Wsign-conversion
CPPFLAGS ?= -I.

.DEFAULT_GOAL := all

all: $(CONTRACT) $(ELF32_CONTRACT)

$(BUILD_DIR):
	$(PYTHON) -c "from pathlib import Path; Path(r'$@').mkdir(parents=True, exist_ok=True)"

$(BUILD_DIR)/ctool.o: ctool.c ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ctool_host.o: ctool_host.c ctool_host.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/elf32.o: elf32.c elf32.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/core_contract.o: tests/core_contract.c ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/elf32_contract.o: tests/elf32_contract.c elf32.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(CONTRACT): $(CORE_OBJECTS)
	$(CC) $(CFLAGS) $(CORE_OBJECTS) -o $@

$(ELF32_CONTRACT): $(ELF32_OBJECTS)
	$(CC) $(CFLAGS) $(ELF32_OBJECTS) -o $@

test: $(CONTRACT) $(ELF32_CONTRACT)
	$(CONTRACT) foundations
	$(CONTRACT) paths
	$(CONTRACT) diagnostics
	$(CONTRACT) limits
	$(ELF32_CONTRACT) writer-basic
	$(ELF32_CONTRACT) writer-model
	$(ELF32_CONTRACT) writer-errors
	$(ELF32_CONTRACT) reader-roundtrip
	$(ELF32_CONTRACT) reader-string-offsets
	$(ELF32_CONTRACT) reader-malformed

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree(r'$(BUILD_DIR)', ignore_errors=True)"

.PHONY: all test clean
