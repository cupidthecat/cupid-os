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
X86_CONTRACT := $(BUILD_DIR)/x86-contract$(EXE)
CUPIDDIS_CONTRACT := $(BUILD_DIR)/cupiddis-contract$(EXE)
CUPIDDIS := $(BUILD_DIR)/cupiddis$(EXE)
CUPIDASM_CONTRACT := $(BUILD_DIR)/cupidasm-contract$(EXE)
CUPIDASM_DEMOS_CONTRACT := $(BUILD_DIR)/cupidasm-demos-contract$(EXE)
CUPIDASM_KERNEL_ELF_CONTRACT := $(BUILD_DIR)/cupidasm-kernel-elf-contract$(EXE)
CUPIDASM := $(BUILD_DIR)/cupidasm$(EXE)
CUPIDOBJ_CONTRACT := $(BUILD_DIR)/cupidobj-contract$(EXE)
CUPIDOBJ := $(BUILD_DIR)/cupidobj$(EXE)
CORE_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                $(BUILD_DIR)/core_contract.o
ELF32_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                 $(BUILD_DIR)/elf32.o $(BUILD_DIR)/elf32_contract.o
X86_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
               $(BUILD_DIR)/x86.o $(BUILD_DIR)/x86_contract.o
CUPIDDIS_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                            $(BUILD_DIR)/elf32.o $(BUILD_DIR)/x86.o \
                            $(BUILD_DIR)/cupiddis.o \
                            $(BUILD_DIR)/cupiddis_contract.o
CUPIDDIS_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                    $(BUILD_DIR)/elf32.o $(BUILD_DIR)/x86.o \
                    $(BUILD_DIR)/cupiddis.o $(BUILD_DIR)/cupiddis_main.o
CUPIDASM_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                            $(BUILD_DIR)/elf32.o $(BUILD_DIR)/x86.o \
                            $(BUILD_DIR)/cupidasm.o \
                            $(BUILD_DIR)/cupidasm_contract.o
CUPIDASM_DEMOS_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o \
                                  $(BUILD_DIR)/ctool_host.o \
                                  $(BUILD_DIR)/elf32.o $(BUILD_DIR)/x86.o \
                                  $(BUILD_DIR)/cupidasm.o \
                                  $(BUILD_DIR)/cupidasm_demos_contract.o
CUPIDASM_KERNEL_ELF_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o \
                                       $(BUILD_DIR)/ctool_host.o \
                                       $(BUILD_DIR)/elf32.o \
                                       $(BUILD_DIR)/as_elf.o \
                                       $(BUILD_DIR)/cupidasm_kernel_elf_contract.o
CUPIDASM_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                    $(BUILD_DIR)/elf32.o $(BUILD_DIR)/x86.o \
                    $(BUILD_DIR)/cupidasm.o $(BUILD_DIR)/cupidasm_main.o
CUPIDOBJ_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                            $(BUILD_DIR)/elf32.o $(BUILD_DIR)/cupidobj.o \
                            $(BUILD_DIR)/cupidobj_contract.o
CUPIDOBJ_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                    $(BUILD_DIR)/elf32.o $(BUILD_DIR)/cupidobj.o \
                    $(BUILD_DIR)/cupidobj_main.o
CFLAGS ?= -std=c11 -O2 -pedantic -Werror -Wall -Wextra -Wshadow \
          -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
          -Wmissing-prototypes -Wconversion -Wsign-conversion
CPPFLAGS ?= -I.

.DEFAULT_GOAL := all

all: $(CONTRACT) $(ELF32_CONTRACT) $(X86_CONTRACT) $(CUPIDDIS_CONTRACT) \
     $(CUPIDDIS) $(CUPIDASM_CONTRACT) $(CUPIDASM_DEMOS_CONTRACT) \
     $(CUPIDASM_KERNEL_ELF_CONTRACT) $(CUPIDASM) $(CUPIDOBJ_CONTRACT) \
     $(CUPIDOBJ)

$(BUILD_DIR):
	$(PYTHON) -c "from pathlib import Path; Path(r'$@').mkdir(parents=True, exist_ok=True)"

$(BUILD_DIR)/ctool.o: ctool.c ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ctool_host.o: ctool_host.c ctool_host.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/elf32.o: elf32.c elf32.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/x86.o: x86.c x86.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupiddis.o: cupiddis.c cupiddis.h elf32.h x86.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupiddis_main.o: cupiddis_main.c cupiddis.h ctool_host.h ctool.h elf32.h x86.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidasm.o: cupidasm.c cupidasm.h elf32.h x86.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidasm_main.o: cupidasm_main.c cupidasm.h ctool_host.h ctool.h elf32.h x86.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidobj.o: cupidobj.c cupidobj.h elf32.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidobj_main.o: cupidobj_main.c cupidobj.h ctool_host.h ctool.h elf32.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/as_elf.o: ../kernel/lang/as_elf.c ../kernel/lang/as_elf.h cupidasm.h ctool.h elf32.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/core_contract.o: tests/core_contract.c ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/elf32_contract.o: tests/elf32_contract.c elf32.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/x86_contract.o: tests/x86_contract.c tests/x86_active_cases.inc tests/x86_inline_cases.inc x86.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupiddis_contract.o: tests/cupiddis_contract.c cupiddis.h elf32.h x86.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidasm_contract.o: tests/cupidasm_contract.c cupidasm.h elf32.h x86.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidasm_demos_contract.o: tests/cupidasm_demos_contract.c cupidasm.h elf32.h x86.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidasm_kernel_elf_contract.o: tests/cupidasm_kernel_elf_contract.c ../kernel/lang/as_elf.h cupidasm.h elf32.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidobj_contract.o: tests/cupidobj_contract.c cupidobj.h elf32.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(CONTRACT): $(CORE_OBJECTS)
	$(CC) $(CFLAGS) $(CORE_OBJECTS) -o $@

$(ELF32_CONTRACT): $(ELF32_OBJECTS)
	$(CC) $(CFLAGS) $(ELF32_OBJECTS) -o $@

$(X86_CONTRACT): $(X86_OBJECTS)
	$(CC) $(CFLAGS) $(X86_OBJECTS) -o $@

$(CUPIDDIS_CONTRACT): $(CUPIDDIS_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(CUPIDDIS_CONTRACT_OBJECTS) -o $@

$(CUPIDDIS): $(CUPIDDIS_OBJECTS)
	$(CC) $(CFLAGS) $(CUPIDDIS_OBJECTS) -o $@

$(CUPIDASM_CONTRACT): $(CUPIDASM_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(CUPIDASM_CONTRACT_OBJECTS) -o $@

$(CUPIDASM_DEMOS_CONTRACT): $(CUPIDASM_DEMOS_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(CUPIDASM_DEMOS_CONTRACT_OBJECTS) -o $@

$(CUPIDASM_KERNEL_ELF_CONTRACT): $(CUPIDASM_KERNEL_ELF_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(CUPIDASM_KERNEL_ELF_CONTRACT_OBJECTS) -o $@

$(CUPIDASM): $(CUPIDASM_OBJECTS)
	$(CC) $(CFLAGS) $(CUPIDASM_OBJECTS) -o $@

$(CUPIDOBJ_CONTRACT): $(CUPIDOBJ_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(CUPIDOBJ_CONTRACT_OBJECTS) -o $@

$(CUPIDOBJ): $(CUPIDOBJ_OBJECTS)
	$(CC) $(CFLAGS) $(CUPIDOBJ_OBJECTS) -o $@

test: $(CONTRACT) $(ELF32_CONTRACT) $(X86_CONTRACT) $(CUPIDDIS_CONTRACT) \
      $(CUPIDDIS) $(CUPIDASM_CONTRACT) $(CUPIDASM_DEMOS_CONTRACT) \
      $(CUPIDASM_KERNEL_ELF_CONTRACT) $(CUPIDASM) $(CUPIDOBJ_CONTRACT) \
      $(CUPIDOBJ)
	$(CONTRACT) foundations
	$(CONTRACT) paths
	$(CONTRACT) diagnostics
	$(CONTRACT) limits
	$(ELF32_CONTRACT) writer-basic
	$(ELF32_CONTRACT) writer-model
	$(ELF32_CONTRACT) writer-errors
	$(ELF32_CONTRACT) reader-roundtrip
	$(ELF32_CONTRACT) reader-exec
	$(ELF32_CONTRACT) reader-exec-malformed
	$(ELF32_CONTRACT) reader-string-offsets
	$(ELF32_CONTRACT) reader-malformed
	$(X86_CONTRACT) inventory
	$(X86_CONTRACT) model
	$(X86_CONTRACT) integer
	$(X86_CONTRACT) addressing
	$(X86_CONTRACT) relocations
	$(X86_CONTRACT) system-simd
	$(X86_CONTRACT) active-surface
	$(X86_CONTRACT) errors
	$(CUPIDDIS_CONTRACT) raw
	$(CUPIDDIS_CONTRACT) object
	$(CUPIDDIS_CONTRACT) exec
	$(CUPIDDIS_CONTRACT) nm
	$(CUPIDDIS_CONTRACT) errors
	$(CUPIDASM_CONTRACT) raw-basic
	$(CUPIDASM_CONTRACT) raw-expressions
	$(CUPIDASM_CONTRACT) object-basic
	$(CUPIDASM_CONTRACT) object-symbolic-immediate
	$(CUPIDASM_CONTRACT) fixed-image
	$(CUPIDASM_CONTRACT) fixed-directives
	$(CUPIDASM_CONTRACT) include-resolution
	$(CUPIDASM_CONTRACT) errors
	$(CUPIDASM_CONTRACT) long-line
	$(CUPIDASM_DEMOS_CONTRACT) all
	$(CUPIDASM_KERNEL_ELF_CONTRACT) code-only
	$(CUPIDASM_KERNEL_ELF_CONTRACT) code-data-bss
	$(CUPIDASM_KERNEL_ELF_CONTRACT) errors
	$(CUPIDOBJ_CONTRACT) wrap-basic
	$(CUPIDOBJ_CONTRACT) wrap-model
	$(CUPIDOBJ_CONTRACT) extract-basic
	$(CUPIDOBJ_CONTRACT) extract-fallback
	$(CUPIDOBJ_CONTRACT) errors

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree(r'$(BUILD_DIR)', ignore_errors=True)"

.PHONY: all test clean
