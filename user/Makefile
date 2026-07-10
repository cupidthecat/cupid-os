# user/Makefile - Build ELF programs for CupidOS
#
# Usage:
#   make -C user              # Build all example programs
#   make -C user clean        # Clean all build artifacts
#
# Programs are compiled as ELF32 i386 static executables.
# Stage them at FAT root before a fresh image's first boot to import them into
# homefs, then run `exec /home/<program>`.  On an already-booted image, staged
# FAT files appear under `/disk` until copied into `/home` in the guest.
#
# To add a new program:
#   1. Create user/examples/yourprog.c
#   2. Add 'yourprog' to the PROGRAMS list below
#   3. Include "cupid.h" and implement _start(cupid_syscall_table_t *sys)

.SUFFIXES:

ifeq ($(OS),Windows_NT)
PYTHON ?= python
ifeq ($(origin CC),default)
CC := clang
endif
CC_TARGET ?= --target=i386-unknown-elf
HOST_EXE := .exe
else
PYTHON ?= python3
ifeq ($(origin CC),default)
CC := gcc
endif
CC_TARGET ?=
HOST_EXE :=
endif

CUPIDLD_BUILD := ../toolchain/build/cupidld$(HOST_EXE)
CUPIDLD ?= $(CUPIDLD_BUILD)
CUPIDLD_SOURCES := ../toolchain/ctool.c ../toolchain/ctool.h \
                   ../toolchain/ctool_host.c ../toolchain/ctool_host.h \
                   ../toolchain/elf32.c ../toolchain/elf32.h \
                   ../toolchain/cupidld.c ../toolchain/cupidld.h \
                   ../toolchain/cupidld_main.c ../toolchain/Makefile
USER_TEXT_ADDRESS ?= 0x00D00000
# Cupid user programs have no CFI unwinder, so their objects must not retain
# host-generated .eh_frame metadata either.
FREESTANDING_CODEGEN_CFLAGS := -fno-asynchronous-unwind-tables -fno-unwind-tables
CFLAGS = $(CC_TARGET) -m32 -fno-pie -fno-stack-protector -nostdlib -static -ffreestanding $(FREESTANDING_CODEGEN_CFLAGS) -O2 -Wall -Wextra \
         -Wno-unused-parameter -I.

# List of programs to build
PROGRAMS = hello ls cat

# Build directory
BUILD ?= build
BOOTSTRAP_ARTIFACTS := $(addprefix $(BUILD)/,$(PROGRAMS))

all: $(BUILD) $(BOOTSTRAP_ARTIFACTS)

$(BUILD):
	$(PYTHON) -c "from pathlib import Path; Path(r'$@').mkdir(parents=True, exist_ok=True)"

$(CUPIDLD_BUILD): $(CUPIDLD_SOURCES)
	$(MAKE) -C ../toolchain $(patsubst ../toolchain/%,%,$@)

$(BUILD)/%: $(BUILD)/%.o $(CUPIDLD)
	$(CUPIDLD) -m elf_i386 --text-address $(USER_TEXT_ADDRESS) --entry _start -o $@ $<

$(BUILD)/%.o: examples/%.c cupid.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree(r'$(BUILD)', ignore_errors=True)"

print-bootstrap-artifacts:
	@$(PYTHON) -c "import json,sys; print(json.dumps(sys.argv[1:]))" $(BOOTSTRAP_ARTIFACTS)

.PHONY: all clean print-bootstrap-artifacts
