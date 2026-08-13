.SUFFIXES:

ifeq ($(OS),Windows_NT)
PYTHON ?= python
ifeq ($(origin CC),default)
CC := clang
endif
EXE := .exe
# LLD's default PE/COFF timestamp changes on every link. Keep /Brepro
# link-only: Clang rejects an unused -Wl option on compile steps under -Werror.
HOST_REPRO_LDFLAGS ?= -Wl,/Brepro
else
PYTHON ?= python3
ifeq ($(origin CC),default)
CC := gcc
endif
EXE :=
HOST_REPRO_LDFLAGS ?=
endif

BUILD_DIR ?= build
CONTRACT_DIR := $(BUILD_DIR)/cupidc-contracts
CONTRACT_MANIFEST := $(CONTRACT_DIR)/manifest.json
CONTRACT_RUN := $(PYTHON) ../tools/cupidc_toolchain_contracts.py run \
	--root .. --executable
CONTRACT_ARTIFACT := $(CONTRACT_DIR)/core-contract.elf
CUPIDC_PP_CONTRACT_ARTIFACT := $(CONTRACT_DIR)/cupidc-pp-contract.elf
CUPIDC_TYPE_CONTRACT_ARTIFACT := $(CONTRACT_DIR)/cupidc-type-contract.elf
CUPIDC_FRONTEND_CONTRACT_ARTIFACT := \
	$(CONTRACT_DIR)/cupidc-frontend-contract.elf
CUPIDC_IR_CONTRACT_ARTIFACT := $(CONTRACT_DIR)/cupidc-ir-contract.elf
CUPIDC_OBJECT_CONTRACT_ARTIFACT := \
	$(CONTRACT_DIR)/cupidc-object-contract.elf
ELF32_CONTRACT_ARTIFACT := $(CONTRACT_DIR)/elf32-contract.elf
X86_CONTRACT_ARTIFACT := $(CONTRACT_DIR)/x86-contract.elf
CUPIDDIS_CONTRACT_ARTIFACT := $(CONTRACT_DIR)/cupiddis-contract.elf
CUPIDASM_CONTRACT_ARTIFACT := $(CONTRACT_DIR)/cupidasm-contract.elf
CUPIDASM_DEMOS_CONTRACT_ARTIFACT := \
	$(CONTRACT_DIR)/cupidasm-demos-contract.elf
CUPIDASM_KERNEL_ELF_CONTRACT_ARTIFACT := \
	$(CONTRACT_DIR)/cupidasm-kernel-elf-contract.elf
CUPIDOBJ_CONTRACT_ARTIFACT := $(CONTRACT_DIR)/cupidobj-contract.elf
CUPIDLD_CONTRACT_ARTIFACT := $(CONTRACT_DIR)/cupidld-contract.elf
USER_SYSCALL_ABI_CONTRACT_ARTIFACT := \
	$(CONTRACT_DIR)/user-syscall-abi-contract.elf
CONTRACT := $(CONTRACT_RUN) $(CONTRACT_ARTIFACT) --
CUPIDC_PP_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDC_PP_CONTRACT_ARTIFACT) --
CUPIDC_TYPE_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDC_TYPE_CONTRACT_ARTIFACT) --
CUPIDC_FRONTEND_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDC_FRONTEND_CONTRACT_ARTIFACT) --
CUPIDC_IR_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDC_IR_CONTRACT_ARTIFACT) --
CUPIDC_OBJECT_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDC_OBJECT_CONTRACT_ARTIFACT) --
CUPIDC_OBJECT_CONTRACT_SLOW := \
	$(CONTRACT_RUN) $(CUPIDC_OBJECT_CONTRACT_ARTIFACT) --timeout 1800 --
ELF32_CONTRACT := $(CONTRACT_RUN) $(ELF32_CONTRACT_ARTIFACT) --
X86_CONTRACT := $(CONTRACT_RUN) $(X86_CONTRACT_ARTIFACT) --
CUPIDDIS_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDDIS_CONTRACT_ARTIFACT) --
CUPIDASM_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDASM_CONTRACT_ARTIFACT) --
CUPIDASM_DEMOS_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDASM_DEMOS_CONTRACT_ARTIFACT) --
CUPIDASM_KERNEL_ELF_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDASM_KERNEL_ELF_CONTRACT_ARTIFACT) --
CUPIDOBJ_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDOBJ_CONTRACT_ARTIFACT) --
CUPIDLD_CONTRACT := \
	$(CONTRACT_RUN) $(CUPIDLD_CONTRACT_ARTIFACT) --
USER_SYSCALL_ABI_CONTRACT := \
	$(CONTRACT_RUN) $(USER_SYSCALL_ABI_CONTRACT_ARTIFACT) --
NATIVE_CONTRACT := $(BUILD_DIR)/core-contract$(EXE)
NATIVE_CUPIDC_PP_CONTRACT := $(BUILD_DIR)/cupidc-pp-contract$(EXE)
NATIVE_CUPIDC_TYPE_CONTRACT := \
	$(BUILD_DIR)/cupidc-type-contract$(EXE)
NATIVE_CUPIDC_FRONTEND_CONTRACT := \
	$(BUILD_DIR)/cupidc-frontend-contract$(EXE)
NATIVE_CUPIDC_IR_CONTRACT := $(BUILD_DIR)/cupidc-ir-contract$(EXE)
NATIVE_CUPIDC_OBJECT_CONTRACT := \
	$(BUILD_DIR)/cupidc-object-contract$(EXE)
NATIVE_ELF32_CONTRACT := $(BUILD_DIR)/elf32-contract$(EXE)
NATIVE_X86_CONTRACT := $(BUILD_DIR)/x86-contract$(EXE)
NATIVE_CUPIDDIS_CONTRACT := $(BUILD_DIR)/cupiddis-contract$(EXE)
NATIVE_CUPIDASM_CONTRACT := $(BUILD_DIR)/cupidasm-contract$(EXE)
NATIVE_CUPIDASM_DEMOS_CONTRACT := \
	$(BUILD_DIR)/cupidasm-demos-contract$(EXE)
NATIVE_CUPIDASM_KERNEL_ELF_CONTRACT := \
	$(BUILD_DIR)/cupidasm-kernel-elf-contract$(EXE)
NATIVE_CUPIDOBJ_CONTRACT := $(BUILD_DIR)/cupidobj-contract$(EXE)
NATIVE_CUPIDLD_CONTRACT := $(BUILD_DIR)/cupidld-contract$(EXE)
CUPIDC := $(BUILD_DIR)/cupidc$(EXE)
CUPIDC_PP_ACTIVE_CASES := tests/cupidc_pp_active_cases.inc
CUPIDC_PP_GENERATED_SOURCES := ../kernel/cpu/ksyms_data.cc \
                              ../kernel/util/bin_programs_gen.cc \
                              ../kernel/util/demos_programs_gen.cc \
                              ../kernel/util/docs_programs_gen.cc
CUPIDC_HOSTED_I386_HEADERS := hosted/i386-linux/include/cupid_host_abi.h \
                              hosted/i386-linux/include/direct.h \
                              hosted/i386-linux/include/errno.h \
                              hosted/i386-linux/include/stdint.h \
                              hosted/i386-linux/include/stdio.h \
                              hosted/i386-linux/include/stdlib.h \
                              hosted/i386-linux/include/string.h \
                              hosted/i386-linux/include/unistd.h
CUPIDC_HOSTED_I386_RUNTIME := hosted/i386-linux/runtime.cc \
                              hosted/i386-linux/start.asm \
                              tests/hosted_i386_runtime_contract.cc
CUPIDC_HOSTED_I386_WINDOWS_RUNTIME := hosted/i386-windows/runtime.cc \
                                      hosted/i386-windows/start.asm \
                                      hosted/i386-windows/tool_start.asm \
                                      tests/hosted_i386_windows_contract.cc \
                                      tests/hosted_i386_windows_runtime_contract.cc
CUPIDC_HOSTED_I386_CLOSURE := ctool.cc ctool.h ctool_host.cc ctool_host.h \
                              elf32.cc elf32.h x86.cc x86.h \
                              cupidc_pp.cc cupidc_pp.h \
                              cupidc_type.cc cupidc_type.h \
                              cupidc_frontend.cc cupidc_frontend.h \
                              cupidc_ir.cc cupidc_ir.h \
                              cupidc_emit.cc cupidc_emit.h cupidc_main.cc \
                              cupidasm.cc cupidasm.h cupidasm_main.cc \
                              cupiddis.cc cupiddis.h cupiddis_main.cc \
                              cupidld.cc cupidld.h cupidld_main.cc \
                              cupidobj.cc cupidobj.h cupidobj_main.cc \
                              $(CUPIDC_HOSTED_I386_HEADERS) \
                              $(CUPIDC_HOSTED_I386_RUNTIME)
CUPIDC_CONTRACT_SOURCES := tests/core_contract.cc \
	tests/cupidc_pp_contract.cc tests/cupidc_type_contract.cc \
	tests/cupidc_frontend_contract.cc tests/cupidc_ir_contract.cc \
	tests/cupidc_object_contract.cc tests/elf32_contract.cc \
	tests/x86_contract.cc tests/cupiddis_contract.cc \
	tests/cupidasm_contract.cc tests/cupidasm_demos_contract.cc \
	tests/cupidasm_kernel_elf_contract.cc tests/cupidobj_contract.cc \
	tests/cupidld_contract.cc tests/user_syscall_abi_contract.cc
USER_SYSCALL_ABI_INPUTS := ../tools/user_syscall_abi.py \
	../kernel/core/types.h ../kernel/core/syscall.h \
	../kernel/core/syscall.cc ../kernel/fs/vfs.h \
	../kernel/network/socket.h ../user/cupid.h
CUPIDC_CONTRACT_SUPPORT := $(wildcard tests/*.inc) \
	$(wildcard tests/*.h) ../kernel/lang/as_elf.cc \
	../kernel/lang/as_elf.h
CUPIDC_CONTRACT_SEED := ../bootstrap/seeds/i386-linux/manifest.json \
	../bootstrap/seeds/i386-linux/cupidasm.elf \
	../bootstrap/seeds/i386-linux/cupidc.elf \
	../bootstrap/seeds/i386-linux/cupiddis.elf \
	../bootstrap/seeds/i386-linux/cupidld.elf \
	../bootstrap/seeds/i386-linux/cupidobj.elf
CUPIDC_HOSTED_I386_ARTIFACTS := $(CONTRACT_DIR)/cupidc-cupidasm.elf \
	$(CONTRACT_DIR)/cupidc-cupiddis.elf \
	$(CONTRACT_DIR)/cupidc-cupidld.elf \
	$(CONTRACT_DIR)/cupidc-cupidobj.elf \
	$(CONTRACT_DIR)/cupidc-cupidc.elf \
	$(CONTRACT_DIR)/cupidc-runtime-contract.elf
CUPIDC_CONTRACT_ARTIFACTS := $(CONTRACT_ARTIFACT) \
	$(CUPIDC_PP_CONTRACT_ARTIFACT) $(CUPIDC_TYPE_CONTRACT_ARTIFACT) \
	$(CUPIDC_FRONTEND_CONTRACT_ARTIFACT) $(CUPIDC_IR_CONTRACT_ARTIFACT) \
	$(CUPIDC_OBJECT_CONTRACT_ARTIFACT) $(ELF32_CONTRACT_ARTIFACT) \
	$(X86_CONTRACT_ARTIFACT) $(CUPIDDIS_CONTRACT_ARTIFACT) \
	$(CUPIDASM_CONTRACT_ARTIFACT) $(CUPIDASM_DEMOS_CONTRACT_ARTIFACT) \
	$(CUPIDASM_KERNEL_ELF_CONTRACT_ARTIFACT) \
	$(CUPIDOBJ_CONTRACT_ARTIFACT) $(CUPIDLD_CONTRACT_ARTIFACT) \
	$(USER_SYSCALL_ABI_CONTRACT_ARTIFACT)
CUPIDDIS := $(BUILD_DIR)/cupiddis$(EXE)
CUPIDASM := $(BUILD_DIR)/cupidasm$(EXE)
CUPIDOBJ := $(BUILD_DIR)/cupidobj$(EXE)
CUPIDLD := $(BUILD_DIR)/cupidld$(EXE)
CORE_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                $(BUILD_DIR)/core_contract.o
CUPIDC_PP_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o \
                              $(BUILD_DIR)/ctool_host.o \
                              $(BUILD_DIR)/cupidc_pp.o \
                              $(BUILD_DIR)/cupidc_pp_contract.o
CUPIDC_TYPE_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o \
                                $(BUILD_DIR)/ctool_host.o \
                                $(BUILD_DIR)/cupidc_type.o \
                                $(BUILD_DIR)/cupidc_type_contract.o
CUPIDC_FRONTEND_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o \
                                    $(BUILD_DIR)/ctool_host.o \
                                    $(BUILD_DIR)/cupidc_pp.o \
                                    $(BUILD_DIR)/cupidc_type.o \
                                    $(BUILD_DIR)/cupidc_frontend.o \
                                    $(BUILD_DIR)/cupidc_frontend_contract.o
CUPIDC_IR_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o \
                              $(BUILD_DIR)/ctool_host.o \
                              $(BUILD_DIR)/cupidc_pp.o \
                              $(BUILD_DIR)/cupidc_type.o \
                              $(BUILD_DIR)/cupidc_frontend.o \
                              $(BUILD_DIR)/cupidc_ir.o \
                              $(BUILD_DIR)/cupidc_ir_contract.o
CUPIDC_OBJECT_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o \
                                  $(BUILD_DIR)/ctool_host.o \
                                  $(BUILD_DIR)/cupidasm.o \
                                  $(BUILD_DIR)/cupidc_pp.o \
                                  $(BUILD_DIR)/cupidc_type.o \
                                  $(BUILD_DIR)/cupidc_frontend.o \
                                  $(BUILD_DIR)/cupidc_ir.o \
                                  $(BUILD_DIR)/elf32.o \
                                  $(BUILD_DIR)/x86.o \
                                  $(BUILD_DIR)/cupidld.o \
                                  $(BUILD_DIR)/cupidc_emit.o \
                                  $(BUILD_DIR)/cupidc_object_contract.o
CUPIDC_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                  $(BUILD_DIR)/cupidc_pp.o $(BUILD_DIR)/cupidc_type.o \
                  $(BUILD_DIR)/cupidc_frontend.o $(BUILD_DIR)/cupidc_ir.o \
                  $(BUILD_DIR)/cupidc_emit.o $(BUILD_DIR)/elf32.o \
                  $(BUILD_DIR)/x86.o $(BUILD_DIR)/cupidc_main.o
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
CUPIDLD_CONTRACT_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                           $(BUILD_DIR)/elf32.o $(BUILD_DIR)/cupidld.o \
                           $(BUILD_DIR)/cupidld_contract.o
CUPIDLD_OBJECTS := $(BUILD_DIR)/ctool.o $(BUILD_DIR)/ctool_host.o \
                   $(BUILD_DIR)/elf32.o $(BUILD_DIR)/cupidld.o \
                   $(BUILD_DIR)/cupidld_main.o
NATIVE_ORACLE_ARTIFACTS := $(NATIVE_CONTRACT) \
	$(NATIVE_CUPIDC_PP_CONTRACT) $(NATIVE_CUPIDC_TYPE_CONTRACT) \
	$(NATIVE_CUPIDC_FRONTEND_CONTRACT) $(NATIVE_CUPIDC_IR_CONTRACT) \
	$(NATIVE_CUPIDC_OBJECT_CONTRACT) $(CUPIDC) \
	$(NATIVE_ELF32_CONTRACT) $(NATIVE_X86_CONTRACT) \
	$(NATIVE_CUPIDDIS_CONTRACT) $(CUPIDDIS) \
	$(NATIVE_CUPIDASM_CONTRACT) $(NATIVE_CUPIDASM_DEMOS_CONTRACT) \
	$(NATIVE_CUPIDASM_KERNEL_ELF_CONTRACT) $(CUPIDASM) \
	$(NATIVE_CUPIDOBJ_CONTRACT) $(CUPIDOBJ) \
	$(NATIVE_CUPIDLD_CONTRACT) $(CUPIDLD)
BOOTSTRAP_ARTIFACTS := $(CUPIDC_CONTRACT_ARTIFACTS) \
	$(CUPIDC_HOSTED_I386_ARTIFACTS)
CFLAGS ?= -std=c11 -O2 -pedantic -Werror -Wall -Wextra -Wshadow \
          -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
          -Wmissing-prototypes -Wconversion -Wsign-conversion
CPPFLAGS ?= -I.
LDFLAGS ?=

.DEFAULT_GOAL := all

all: $(CONTRACT_MANIFEST)
	$(PYTHON) ../tools/cupidc_toolchain_contracts.py verify \
		--root .. --output $(CONTRACT_DIR)

$(BUILD_DIR):
	$(PYTHON) -c "from pathlib import Path; Path(r'$@').mkdir(parents=True, exist_ok=True)"

$(BUILD_DIR)/ctool.o: ctool.cc ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/ctool_host.o: ctool_host.cc ctool_host.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_pp.o: cupidc_pp.cc cupidc_pp.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_type.o: cupidc_type.cc cupidc_type.h cupidc_pp.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_frontend.o: cupidc_frontend.cc cupidc_frontend.h \
                                cupidc_type.h cupidc_pp.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_ir.o: cupidc_ir.cc cupidc_ir.h cupidc_frontend.h \
                          cupidc_type.h cupidc_pp.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_emit.o: cupidc_emit.cc cupidc_emit.h cupidc_frontend.h \
                            cupidc_ir.h cupidc_type.h cupidc_pp.h elf32.h \
                            x86.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_main.o: cupidc_main.cc cupidc_emit.h cupidc_ir.h \
                            cupidc_frontend.h cupidc_type.h cupidc_pp.h \
                            ctool_host.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/elf32.o: elf32.cc elf32.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/x86.o: x86.cc x86.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupiddis.o: cupiddis.cc cupiddis.h elf32.h x86.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupiddis_main.o: cupiddis_main.cc cupiddis.h ctool_host.h ctool.h elf32.h x86.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidasm.o: cupidasm.cc cupidasm.h elf32.h x86.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidasm_main.o: cupidasm_main.cc cupidasm.h ctool_host.h ctool.h elf32.h x86.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidobj.o: cupidobj.cc cupidobj.h elf32.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidobj_main.o: cupidobj_main.cc cupidobj.h ctool_host.h ctool.h elf32.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidld.o: cupidld.cc cupidld.h elf32.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidld_main.o: cupidld_main.cc cupidld.h ctool_host.h ctool.h elf32.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/as_elf.o: ../kernel/lang/as_elf.cc ../kernel/lang/as_elf.h cupidasm.h ctool.h elf32.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/core_contract.o: tests/core_contract.cc ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_pp_contract.o: tests/cupidc_pp_contract.cc \
                                 tests/cupidc_pp_conditional_cases.inc \
                                 $(CUPIDC_PP_ACTIVE_CASES) \
                                 cupidc_pp.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_type_contract.o: tests/cupidc_type_contract.cc \
                                   cupidc_type.h cupidc_pp.h \
                                   ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_frontend_contract.o: \
    tests/cupidc_frontend_contract.cc $(CUPIDC_PP_ACTIVE_CASES) \
    tests/cupidc_static_long_double_arithmetic_fixture.h \
    tests/cupidc_static_long_double_control_fixture.h \
    tests/cupidc_static_long_double_integer_fixture.h \
    cupidc_frontend.h cupidc_type.h cupidc_pp.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_ir_contract.o: tests/cupidc_ir_contract.cc \
    tests/cupidc_static_long_double_arithmetic_fixture.h \
    tests/cupidc_static_long_double_control_fixture.h \
    tests/cupidc_static_long_double_integer_fixture.h \
    cupidc_ir.h cupidc_frontend.h cupidc_type.h cupidc_pp.h \
    ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidc_object_contract.o: tests/cupidc_object_contract.cc \
    tests/cupidc_kernel_simd_fixture.h \
    tests/cupidc_static_long_double_arithmetic_fixture.h \
    tests/cupidc_static_long_double_control_fixture.h \
    tests/cupidc_static_long_double_integer_fixture.h \
    cupidc_emit.h cupidc_ir.h cupidc_frontend.h cupidc_type.h cupidc_pp.h \
    cupidasm.h cupidld.h elf32.h x86.h ctool.h ctool_host.h \
    $(CUPIDC_HOSTED_I386_HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/elf32_contract.o: tests/elf32_contract.cc elf32.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/x86_contract.o: tests/x86_contract.cc tests/x86_active_cases.inc tests/x86_inline_cases.inc x86.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupiddis_contract.o: tests/cupiddis_contract.cc cupiddis.h elf32.h x86.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidasm_contract.o: tests/cupidasm_contract.cc cupidasm.h elf32.h x86.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidasm_demos_contract.o: tests/cupidasm_demos_contract.cc cupidasm.h elf32.h x86.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidasm_kernel_elf_contract.o: tests/cupidasm_kernel_elf_contract.cc ../kernel/lang/as_elf.h cupidasm.h elf32.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidobj_contract.o: tests/cupidobj_contract.cc cupidobj.h elf32.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/cupidld_contract.o: tests/cupidld_contract.cc cupidld.h elf32.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@

$(NATIVE_CONTRACT): $(CORE_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CORE_OBJECTS) -o $@

$(NATIVE_CUPIDC_PP_CONTRACT): $(CUPIDC_PP_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDC_PP_CONTRACT_OBJECTS) -o $@

$(NATIVE_CUPIDC_TYPE_CONTRACT): $(CUPIDC_TYPE_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDC_TYPE_CONTRACT_OBJECTS) -o $@

$(NATIVE_CUPIDC_FRONTEND_CONTRACT): $(CUPIDC_FRONTEND_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDC_FRONTEND_CONTRACT_OBJECTS) -o $@

$(NATIVE_CUPIDC_IR_CONTRACT): $(CUPIDC_IR_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDC_IR_CONTRACT_OBJECTS) -o $@

$(NATIVE_CUPIDC_OBJECT_CONTRACT): $(CUPIDC_OBJECT_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDC_OBJECT_CONTRACT_OBJECTS) -o $@

$(CUPIDC): $(CUPIDC_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDC_OBJECTS) -o $@

$(CONTRACT_MANIFEST): ../tools/cupidc_toolchain_contracts.py \
	../tools/bootstrap_toolchain.py $(CUPIDC_CONTRACT_SEED) \
	../link.ld \
	$(CUPIDC_HOSTED_I386_CLOSURE) $(CUPIDC_CONTRACT_SOURCES) \
	$(CUPIDC_CONTRACT_SUPPORT) $(CUPIDC_HOSTED_I386_WINDOWS_RUNTIME) \
	$(USER_SYSCALL_ABI_INPUTS) Makefile
	$(PYTHON) ../tools/cupidc_toolchain_contracts.py build \
		--root .. --manifest ../bootstrap/seeds/i386-linux/manifest.json \
		--output $(CONTRACT_DIR)

$(CUPIDC_CONTRACT_ARTIFACTS) $(CUPIDC_HOSTED_I386_ARTIFACTS): \
	$(CONTRACT_MANIFEST)

$(NATIVE_ELF32_CONTRACT): $(ELF32_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(ELF32_OBJECTS) -o $@

$(NATIVE_X86_CONTRACT): $(X86_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(X86_OBJECTS) -o $@

$(NATIVE_CUPIDDIS_CONTRACT): $(CUPIDDIS_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDDIS_CONTRACT_OBJECTS) -o $@

$(CUPIDDIS): $(CUPIDDIS_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDDIS_OBJECTS) -o $@

$(NATIVE_CUPIDASM_CONTRACT): $(CUPIDASM_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDASM_CONTRACT_OBJECTS) -o $@

$(NATIVE_CUPIDASM_DEMOS_CONTRACT): $(CUPIDASM_DEMOS_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDASM_DEMOS_CONTRACT_OBJECTS) -o $@

$(NATIVE_CUPIDASM_KERNEL_ELF_CONTRACT): $(CUPIDASM_KERNEL_ELF_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDASM_KERNEL_ELF_CONTRACT_OBJECTS) -o $@

$(CUPIDASM): $(CUPIDASM_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDASM_OBJECTS) -o $@

$(NATIVE_CUPIDOBJ_CONTRACT): $(CUPIDOBJ_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDOBJ_CONTRACT_OBJECTS) -o $@

$(CUPIDOBJ): $(CUPIDOBJ_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDOBJ_OBJECTS) -o $@

$(NATIVE_CUPIDLD_CONTRACT): $(CUPIDLD_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDLD_CONTRACT_OBJECTS) -o $@

$(CUPIDLD): $(CUPIDLD_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDLD_OBJECTS) -o $@

test: $(CONTRACT_MANIFEST) \
      tests/x86_active_cases.inc tests/x86_inline_cases.inc \
      $(CUPIDC_PP_ACTIVE_CASES) $(CUPIDC_HOSTED_I386_RUNTIME)
	$(PYTHON) ../tools/cupidc_toolchain_contracts.py verify \
		--root .. --output $(CONTRACT_DIR)
	$(CONTRACT) foundations
	$(CONTRACT) paths
	$(CONTRACT) diagnostics
	$(CONTRACT) limits
	$(USER_SYSCALL_ABI_CONTRACT) check ..
	$(CUPIDC_PP_CONTRACT) phases
	$(CUPIDC_PP_CONTRACT) tokens
	$(CUPIDC_PP_CONTRACT) errors
	$(CUPIDC_PP_CONTRACT) unsupported
	$(CUPIDC_PP_CONTRACT) line-directives
	$(CUPIDC_PP_CONTRACT) line-files
	$(CUPIDC_PP_CONTRACT) line-errors
	$(CUPIDC_PP_CONTRACT) line-scale
	$(CUPIDC_PP_CONTRACT) conditional-expressions
	$(CUPIDC_PP_CONTRACT) predefined
	$(CUPIDC_PP_CONTRACT) predefined-files
	$(CUPIDC_PP_CONTRACT) predefined-errors
	$(CUPIDC_PP_CONTRACT) pragmas
	$(CUPIDC_PP_CONTRACT) pragma-files
	$(CUPIDC_PP_CONTRACT) pragma-errors
	$(CUPIDC_PP_CONTRACT) pragma-scale
	$(CUPIDC_PP_CONTRACT) cupid-exe
	$(CUPIDC_PP_CONTRACT) cupid-exe-active ..
	$(CUPIDC_PP_CONTRACT) cupid-exe-files
	$(CUPIDC_PP_CONTRACT) cupid-exe-errors
	$(CUPIDC_PP_CONTRACT) cupid-exe-scale
	$(CUPIDC_PP_CONTRACT) conditional-errors
	$(CUPIDC_PP_CONTRACT) conditional-active
	$(CUPIDC_PP_CONTRACT) conditional-scale
	$(CUPIDC_PP_CONTRACT) function-macros
	$(CUPIDC_PP_CONTRACT) function-scale
	$(CUPIDC_PP_CONTRACT) macro-operators
	$(CUPIDC_PP_CONTRACT) macro-gnu
	$(CUPIDC_PP_CONTRACT) macro-operator-scale
	$(CUPIDC_PP_CONTRACT) macro-active-cases .
	$(CUPIDC_PP_CONTRACT) macro-operator-errors
	$(CUPIDC_PP_CONTRACT) function-errors
	$(CUPIDC_PP_CONTRACT) object-includes
	$(CUPIDC_PP_CONTRACT) include-macros
	$(CUPIDC_PP_CONTRACT) include-macro-errors
	$(CUPIDC_PP_CONTRACT) include-macro-scale
	$(CUPIDC_PP_CONTRACT) active-corpus ..
	$(CUPIDC_PP_CONTRACT) directive-errors
	$(CUPIDC_PP_CONTRACT) limits
	$(CUPIDC_TYPE_CONTRACT) scalars
	$(CUPIDC_TYPE_CONTRACT) fat16
	$(CUPIDC_TYPE_CONTRACT) records
	$(CUPIDC_TYPE_CONTRACT) errors
	$(CUPIDC_TYPE_CONTRACT) scale
	$(CUPIDC_TYPE_CONTRACT) adversarial
	$(CUPIDC_FRONTEND_CONTRACT) fat16 ..
	$(CUPIDC_FRONTEND_CONTRACT) redeclarations ..
	$(CUPIDC_FRONTEND_CONTRACT) attributes ..
	$(CUPIDC_FRONTEND_CONTRACT) section-attributes ..
	$(CUPIDC_FRONTEND_CONTRACT) unused-attributes ..
	$(CUPIDC_FRONTEND_CONTRACT) used-attributes ..
	$(CUPIDC_FRONTEND_CONTRACT) static-asserts ..
	$(CUPIDC_FRONTEND_CONTRACT) function-bodies ..
	$(CUPIDC_FRONTEND_CONTRACT) old-style-empty-functions ..
	$(CUPIDC_FRONTEND_CONTRACT) variadic-callees ..
	$(CUPIDC_FRONTEND_CONTRACT) wide-variadics ..
	$(CUPIDC_FRONTEND_CONTRACT) floating-transport ..
	$(CUPIDC_FRONTEND_CONTRACT) floating-arithmetic ..
	$(CUPIDC_FRONTEND_CONTRACT) floating-comparisons ..
	$(CUPIDC_FRONTEND_CONTRACT) floating-conversions ..
	$(CUPIDC_FRONTEND_CONTRACT) floating-truth ..
	$(CUPIDC_FRONTEND_CONTRACT) floating-scalars ..
	$(CUPIDC_FRONTEND_CONTRACT) static-long-double-arithmetic ..
	$(CUPIDC_FRONTEND_CONTRACT) inline-assembly ..
	$(CUPIDC_FRONTEND_CONTRACT) port-io-assembly ..
	$(CUPIDC_FRONTEND_CONTRACT) privileged-register-assembly ..
	$(CUPIDC_FRONTEND_CONTRACT) fxsave-assembly ..
	$(CUPIDC_FRONTEND_CONTRACT) legacy-port-assembly ..
	$(CUPIDC_FRONTEND_CONTRACT) state-memory-assembly ..
	$(CUPIDC_FRONTEND_CONTRACT) atomic-builtins ..
	$(CUPIDC_FRONTEND_CONTRACT) pointer-output-assembly ..
	$(CUPIDC_FRONTEND_CONTRACT) call-next-assembly ..
	$(CUPIDC_FRONTEND_CONTRACT) operand-free-assembly ..
	$(CUPIDC_FRONTEND_CONTRACT) kernel-start-assembly ..
	$(CUPIDC_FRONTEND_CONTRACT) block-bindings ..
	$(CUPIDC_FRONTEND_CONTRACT) block-typedefs ..
	$(CUPIDC_FRONTEND_CONTRACT) block-externs ..
	$(CUPIDC_FRONTEND_CONTRACT) block-functions ..
	$(CUPIDC_FRONTEND_CONTRACT) block-enums ..
	$(CUPIDC_FRONTEND_CONTRACT) scalar-initializers ..
	$(CUPIDC_FRONTEND_CONTRACT) static-initializers ..
	$(CUPIDC_FRONTEND_CONTRACT) aggregate-initializers ..
	$(CUPIDC_FRONTEND_CONTRACT) automatic-aggregate-initializers ..
	$(CUPIDC_FRONTEND_CONTRACT) designated-initializers ..
	$(CUPIDC_FRONTEND_CONTRACT) file-scope-initializers ..
	$(CUPIDC_FRONTEND_CONTRACT) scalar-returns ..
	$(CUPIDC_FRONTEND_CONTRACT) conditional-expressions ..
	$(CUPIDC_FRONTEND_CONTRACT) aggregate-values ..
	$(CUPIDC_FRONTEND_CONTRACT) compound-literals ..
	$(CUPIDC_FRONTEND_CONTRACT) for-statements ..
	$(CUPIDC_FRONTEND_CONTRACT) if-statements ..
	$(CUPIDC_FRONTEND_CONTRACT) while-statements ..
	$(CUPIDC_FRONTEND_CONTRACT) do-statements ..
	$(CUPIDC_FRONTEND_CONTRACT) switch-statements ..
	$(CUPIDC_FRONTEND_CONTRACT) labels-and-goto ..
	$(CUPIDC_FRONTEND_CONTRACT) pointer-expressions ..
	$(CUPIDC_FRONTEND_CONTRACT) function-pointer-casts ..
	$(CUPIDC_FRONTEND_CONTRACT) pointer-arithmetic ..
	$(CUPIDC_FRONTEND_CONTRACT) pointer-comparisons ..
	$(CUPIDC_FRONTEND_CONTRACT) scalar-updates ..
	$(CUPIDC_FRONTEND_CONTRACT) function-specifiers ..
	$(CUPIDC_FRONTEND_CONTRACT) errors ..
	$(CUPIDC_FRONTEND_CONTRACT) scale ..
	$(CUPIDC_FRONTEND_CONTRACT) semantics ..
	$(CUPIDC_FRONTEND_CONTRACT) constants ..
	$(CUPIDC_FRONTEND_CONTRACT) boundaries ..
	$(CUPIDC_FRONTEND_CONTRACT) depth-declarator ..
	$(CUPIDC_FRONTEND_CONTRACT) depth-constant ..
	$(CUPIDC_FRONTEND_CONTRACT) depth-record ..
	$(CUPIDC_IR_CONTRACT) active-leaf ..
	$(CUPIDC_IR_CONTRACT) section-attributes ..
	$(CUPIDC_IR_CONTRACT) unused-attributes ..
	$(CUPIDC_IR_CONTRACT) used-attributes ..
	$(CUPIDC_IR_CONTRACT) integer-updates ..
	$(CUPIDC_IR_CONTRACT) integer-compounds ..
	$(CUPIDC_IR_CONTRACT) integer-compound-conversions ..
	$(CUPIDC_IR_CONTRACT) integer-update-conversions ..
	$(CUPIDC_IR_CONTRACT) narrow-mutations ..
	$(CUPIDC_IR_CONTRACT) integer-mutation-rejections ..
	$(CUPIDC_IR_CONTRACT) pointer-member-loads ..
	$(CUPIDC_IR_CONTRACT) pointer-values ..
	$(CUPIDC_IR_CONTRACT) pointer-comparisons ..
	$(CUPIDC_IR_CONTRACT) pointer-conditions ..
	$(CUPIDC_IR_CONTRACT) pointer-arithmetic ..
	$(CUPIDC_IR_CONTRACT) function-pointers ..
	$(CUPIDC_IR_CONTRACT) function-pointer-casts ..
	$(CUPIDC_IR_CONTRACT) automatic-objects ..
	$(CUPIDC_IR_CONTRACT) block-externs ..
	$(CUPIDC_IR_CONTRACT) block-functions ..
	$(CUPIDC_IR_CONTRACT) block-enums ..
	$(CUPIDC_IR_CONTRACT) bit-field-stores ..
	$(CUPIDC_IR_CONTRACT) bit-field-promotions ..
	$(CUPIDC_IR_CONTRACT) bit-field-mutations ..
	$(CUPIDC_IR_CONTRACT) block-typedefs ..
	$(CUPIDC_IR_CONTRACT) aggregate-initializers ..
	$(CUPIDC_IR_CONTRACT) structure-values ..
	$(CUPIDC_IR_CONTRACT) compound-literals ..
	$(CUPIDC_IR_CONTRACT) old-style-empty-functions ..
	$(CUPIDC_IR_CONTRACT) variadic-callees ..
	$(CUPIDC_IR_CONTRACT) wide-variadics ..
	$(CUPIDC_IR_CONTRACT) floating-transport ..
	$(CUPIDC_IR_CONTRACT) floating-arithmetic ..
	$(CUPIDC_IR_CONTRACT) floating-comparisons ..
	$(CUPIDC_IR_CONTRACT) floating-conversions ..
	$(CUPIDC_IR_CONTRACT) floating-truth ..
	$(CUPIDC_IR_CONTRACT) floating-updates ..
	$(CUPIDC_IR_CONTRACT) floating-scalars ..
	$(CUPIDC_IR_CONTRACT) static-long-double-arithmetic ..
	$(CUPIDC_IR_CONTRACT) inline-assembly ..
	$(CUPIDC_IR_CONTRACT) port-io-assembly ..
	$(CUPIDC_IR_CONTRACT) privileged-register-assembly ..
	$(CUPIDC_IR_CONTRACT) fxsave-assembly ..
	$(CUPIDC_IR_CONTRACT) legacy-port-assembly ..
	$(CUPIDC_IR_CONTRACT) state-memory-assembly ..
	$(CUPIDC_IR_CONTRACT) atomic-builtins ..
	$(CUPIDC_IR_CONTRACT) pointer-output-assembly ..
	$(CUPIDC_IR_CONTRACT) call-next-assembly ..
	$(CUPIDC_IR_CONTRACT) operand-free-assembly ..
	$(CUPIDC_IR_CONTRACT) kernel-start-assembly ..
	$(CUPIDC_IR_CONTRACT) narrow-values ..
	$(CUPIDC_IR_CONTRACT) void-casts ..
	$(CUPIDC_IR_CONTRACT) wide-returns ..
	$(CUPIDC_IR_CONTRACT) wide-conditions ..
	$(CUPIDC_IR_CONTRACT) wide-objects ..
	$(CUPIDC_IR_CONTRACT) wide-mutations ..
	$(CUPIDC_IR_CONTRACT) self-host-frontier ..
	$(CUPIDC_OBJECT_CONTRACT) static-data ..
	$(CUPIDC_OBJECT_CONTRACT) section-attributes ..
	$(CUPIDC_OBJECT_CONTRACT) unused-attributes ..
	$(CUPIDC_OBJECT_CONTRACT) used-attributes ..
	$(CUPIDC_OBJECT_CONTRACT) integer-mutation ..
	$(CUPIDC_OBJECT_CONTRACT) pointer-values ..
	$(CUPIDC_OBJECT_CONTRACT) pointer-comparisons ..
	$(CUPIDC_OBJECT_CONTRACT) pointer-conditions ..
	$(CUPIDC_OBJECT_CONTRACT) pointer-arithmetic ..
	$(CUPIDC_OBJECT_CONTRACT) function-pointers ..
	$(CUPIDC_OBJECT_CONTRACT) function-pointer-casts ..
	$(CUPIDC_OBJECT_CONTRACT) automatic-objects ..
	$(CUPIDC_OBJECT_CONTRACT) block-externs ..
	$(CUPIDC_OBJECT_CONTRACT) block-functions ..
	$(CUPIDC_OBJECT_CONTRACT) block-enums ..
	$(CUPIDC_OBJECT_CONTRACT) bit-field-stores ..
	$(CUPIDC_OBJECT_CONTRACT) bit-field-promotions ..
	$(CUPIDC_OBJECT_CONTRACT) bit-field-mutations ..
	$(CUPIDC_OBJECT_CONTRACT) block-typedefs ..
	$(CUPIDC_OBJECT_CONTRACT) aggregate-initializers ..
	$(CUPIDC_OBJECT_CONTRACT) structure-values ..
	$(CUPIDC_OBJECT_CONTRACT) call-alignment ..
	$(CUPIDC_OBJECT_CONTRACT) block-statics ..
	$(CUPIDC_OBJECT_CONTRACT) compound-literals ..
	$(CUPIDC_OBJECT_CONTRACT) old-style-empty-functions ..
	$(CUPIDC_OBJECT_CONTRACT) variadic-callees ..
	$(CUPIDC_OBJECT_CONTRACT) wide-variadics ..
	$(CUPIDC_OBJECT_CONTRACT) floating-transport ..
	$(CUPIDC_OBJECT_CONTRACT) floating-arithmetic ..
	$(CUPIDC_OBJECT_CONTRACT) floating-comparisons ..
	$(CUPIDC_OBJECT_CONTRACT) floating-conversions ..
	$(CUPIDC_OBJECT_CONTRACT) floating-truth ..
	$(CUPIDC_OBJECT_CONTRACT) floating-updates ..
	$(CUPIDC_OBJECT_CONTRACT) floating-scalars ..
	$(CUPIDC_OBJECT_CONTRACT) static-long-double-arithmetic ..
	$(CUPIDC_OBJECT_CONTRACT) inline-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) port-io-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) privileged-register-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) fxsave-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) legacy-port-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) state-memory-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) atomic-builtins ..
	$(CUPIDC_OBJECT_CONTRACT) pointer-output-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) call-next-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) operand-free-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) kernel-start-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) dglibc-jump-assembly ..
	$(CUPIDC_OBJECT_CONTRACT) returns-twice-calls ..
	$(CUPIDC_OBJECT_CONTRACT) narrow-mutations ..
	$(CUPIDC_OBJECT_CONTRACT) narrow-values ..
	$(CUPIDC_OBJECT_CONTRACT) void-casts ..
	$(CUPIDC_OBJECT_CONTRACT) wide-returns ..
	$(CUPIDC_OBJECT_CONTRACT) wide-conditions ..
	$(CUPIDC_OBJECT_CONTRACT) wide-objects ..
	$(CUPIDC_OBJECT_CONTRACT) wide-mutations ..
	$(CUPIDC_OBJECT_CONTRACT_SLOW) self-host-frontier ..
	$(CUPIDC_OBJECT_CONTRACT) self-host-hosted-adapters ..
	$(CUPIDC_OBJECT_CONTRACT) self-host-hosted-profile-errors ..
	$(CUPIDC_OBJECT_CONTRACT) self-host-link-ctool-host .. \
		$(BUILD_DIR)/cupidc-ctool-host-smoke.elf
	$(CUPIDC_OBJECT_CONTRACT_SLOW) self-host-link-tools .. \
		$(BUILD_DIR)/cupidc-cupidasm.elf \
		$(BUILD_DIR)/cupidc-cupiddis.elf \
		$(BUILD_DIR)/cupidc-cupidld.elf \
		$(BUILD_DIR)/cupidc-cupidobj.elf \
		$(BUILD_DIR)/cupidc-cupidc.elf \
		$(BUILD_DIR)/cupidc-runtime-contract.elf
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
	$(X86_CONTRACT) conditional-moves
	$(X86_CONTRACT) parity-setcc
	$(X86_CONTRACT) immediate-imul
	$(X86_CONTRACT) padding-nops
	$(X86_CONTRACT) clang-padding-nops
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
	$(CUPIDASM_CONTRACT) alignment
	$(CUPIDASM_CONTRACT) include-resolution
	$(CUPIDASM_CONTRACT) errors
	$(CUPIDASM_CONTRACT) long-line
	$(CUPIDASM_DEMOS_CONTRACT) all
	$(CUPIDASM_KERNEL_ELF_CONTRACT) code-only
	$(CUPIDASM_KERNEL_ELF_CONTRACT) code-data-bss
	$(CUPIDASM_KERNEL_ELF_CONTRACT) errors
	$(CUPIDOBJ_CONTRACT) wrap-basic
	$(CUPIDOBJ_CONTRACT) wrap-model
	$(CUPIDOBJ_CONTRACT) wrap-text
	$(CUPIDOBJ_CONTRACT) wrap-jpeg
	$(CUPIDOBJ_CONTRACT) extract-basic
	$(CUPIDOBJ_CONTRACT) extract-fallback
	$(CUPIDOBJ_CONTRACT) install-source
	$(CUPIDOBJ_CONTRACT) ksyms-source
	$(CUPIDOBJ_CONTRACT) disk-template
	$(CUPIDOBJ_CONTRACT) iso-fixture
	$(CUPIDOBJ_CONTRACT) profile-manifest
	$(CUPIDOBJ_CONTRACT) errors
	$(CUPIDLD_CONTRACT) fixed-basic
	$(CUPIDLD_CONTRACT) fixed-layout
	$(CUPIDLD_CONTRACT) pe32-fixed
	$(CUPIDLD_CONTRACT) pe32-imports
	$(CUPIDLD_CONTRACT) merge-symbols
	$(CUPIDLD_CONTRACT) merge-addends
	$(CUPIDLD_CONTRACT) script-layout
	$(CUPIDLD_CONTRACT) errors

# Generated C sources remain explicit materialization boundaries.  The hosted
# contract prerequisite completes first; one recipe then serializes the root
# build and execution even under parallel Make.  A missing file is not a skip.
test-cupidc-pp-active-generated: $(CONTRACT_MANIFEST) \
                                 $(CUPIDC_PP_ACTIVE_CASES)
	$(PYTHON) ../tools/cupidc_toolchain_contracts.py verify \
		--root .. --output $(CONTRACT_DIR)
	$(MAKE) -C .. $(patsubst ../%,%,$(CUPIDC_PP_GENERATED_SOURCES))
	$(CUPIDC_PP_CONTRACT) active-corpus-generated ..

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree(r'$(BUILD_DIR)', ignore_errors=True)"

native-oracles: $(NATIVE_ORACLE_ARTIFACTS)

print-bootstrap-artifacts:
	@$(PYTHON) -c "import json,sys; print(json.dumps(sys.argv[1:]))" $(BOOTSTRAP_ARTIFACTS)

.PHONY: all test test-cupidc-pp-active-generated native-oracles clean \
        print-bootstrap-artifacts
