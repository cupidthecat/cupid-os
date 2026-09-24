# user/Makefile - Build Cupid OS ELF programs
#
# Usage:
#   make -C user              # Build all example programs
#   make -C user clean        # Clean all build artifacts
#
# CupidC and CupidLD build static i386 ELF executables from the checked seed.
# Windows can also build native hosted drivers for an explicit equivalence test.
# Stage them at FAT root before a fresh image's first boot to import them into
# homefs, then run `exec /home/<program>`.  On an already-booted image, staged
# FAT files appear under `/disk` until copied into `/home` in the guest.
#
# To add a new program:
#   1. Create user/examples/yourprog.cc
#   2. Add 'yourprog' to the PROGRAMS list below
#   3. Include "../cupid.h" and implement _start(cupid_syscall_table_t *sys)

.SUFFIXES:
.DEFAULT_GOAL := all

ifeq ($(OS),Windows_NT)
PYTHON ?= python
PRODUCTION_SEED_MANIFEST ?= ../bootstrap/seeds/i386-windows/manifest.json
PRODUCTION_SEED_SUFFIX := exe
USER_SYSCALL_ABI_PLATFORM_ARGUMENTS := \
	--windows-manifest $(PRODUCTION_SEED_MANIFEST)
else
PYTHON ?= python3
PRODUCTION_SEED_MANIFEST ?= ../bootstrap/seeds/i386-linux/manifest.json
PRODUCTION_SEED_SUFFIX := elf
USER_SYSCALL_ABI_PLATFORM_ARGUMENTS :=
endif

PRODUCTION_SEED_DIRECTORY := $(dir $(PRODUCTION_SEED_MANIFEST))
CHECKED_SEED_INPUTS := $(PRODUCTION_SEED_MANIFEST) \
	$(PRODUCTION_SEED_DIRECTORY)cupidasm.$(PRODUCTION_SEED_SUFFIX) \
	$(PRODUCTION_SEED_DIRECTORY)cupidc.$(PRODUCTION_SEED_SUFFIX) \
	$(PRODUCTION_SEED_DIRECTORY)cupiddis.$(PRODUCTION_SEED_SUFFIX) \
	$(PRODUCTION_SEED_DIRECTORY)cupidld.$(PRODUCTION_SEED_SUFFIX) \
	$(PRODUCTION_SEED_DIRECTORY)cupidobj.$(PRODUCTION_SEED_SUFFIX) \
	$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX)
BOOTSTRAP_SEED_MANIFEST := ../bootstrap/seeds/i386-linux/manifest.json
BOOTSTRAP_SEED_INPUTS := $(BOOTSTRAP_SEED_MANIFEST) \
	../bootstrap/seeds/i386-linux/cupidasm.elf \
	../bootstrap/seeds/i386-linux/cupidc.elf \
	../bootstrap/seeds/i386-linux/cupiddis.elf \
	../bootstrap/seeds/i386-linux/cupidld.elf \
	../bootstrap/seeds/i386-linux/cupidobj.elf \
	../bootstrap/seeds/i386-linux/cupidbuild.elf
CUPIDC_PRODUCTION_COMPILE := $(PYTHON) ../tools/cupidc_production_compile.py \
	--root .. --cohort user --manifest $(PRODUCTION_SEED_MANIFEST)
CUPIDLD_USER_LINK := $(PYTHON) ../tools/cupidld_user_link.py --root .. \
	--manifest $(PRODUCTION_SEED_MANIFEST)
USER_SYSCALL_ABI := $(PYTHON) ../tools/cupidc_toolchain_contracts.py \
	user-abi --root .. \
	--manifest $(BOOTSTRAP_SEED_MANIFEST) \
	$(USER_SYSCALL_ABI_PLATFORM_ARGUMENTS) \
	--output ../toolchain/build/cupidc-contracts
NATIVE_WINDOWS_USER_SYSCALL_ABI_INPUTS := \
	../kernel/core/syscall.cc \
	../kernel/core/syscall.h \
	../kernel/core/types.h \
	../kernel/fs/vfs.h \
	../kernel/network/socket.h \
	../toolchain/ctool.cc \
	../toolchain/ctool.h \
	../toolchain/ctool_host.cc \
	../toolchain/ctool_host.h \
	../toolchain/hosted/i386-linux/include/cupid_host_abi.h \
	../toolchain/hosted/i386-linux/include/direct.h \
	../toolchain/hosted/i386-linux/include/errno.h \
	../toolchain/hosted/i386-linux/include/stddef.h \
	../toolchain/hosted/i386-linux/include/stdint.h \
	../toolchain/hosted/i386-linux/include/stdio.h \
	../toolchain/hosted/i386-linux/include/stdlib.h \
	../toolchain/hosted/i386-linux/include/string.h \
	../toolchain/hosted/i386-linux/include/unistd.h \
	../toolchain/hosted/i386-linux/include/windows.h \
	../toolchain/hosted/i386-linux/runtime.cc \
	../toolchain/hosted/i386-windows/runtime.cc \
	../toolchain/hosted/i386-windows/tool_start.asm \
	../toolchain/tests/user_syscall_abi_contract.cc \
	../tools/bootstrap_toolchain.py \
	../tools/cupidc_toolchain_contracts.py \
	../tools/user_syscall_abi.py \
	cupid.h
USER_SYSCALL_ABI_PUBLICATION_INPUTS := \
	../kernel/core/syscall.cc \
	../kernel/core/syscall.h \
	../kernel/core/types.h \
	../kernel/fs/vfs.h \
	../kernel/lang/as_elf.cc \
	../kernel/lang/as_elf.h \
	../kernel/network/socket.h \
	../toolchain/Makefile \
	../toolchain/ctool.h \
	../toolchain/ctool_host.h \
	../toolchain/cupidasm.h \
	../toolchain/cupidc_emit.h \
	../toolchain/cupidc_frontend.h \
	../toolchain/cupidc_ir.h \
	../toolchain/cupidc_pp.h \
	../toolchain/cupidc_type.h \
	../toolchain/cupiddis.h \
	../toolchain/cupidld.h \
	../toolchain/cupidobj.h \
	../toolchain/elf32.h \
	../toolchain/hosted/i386-linux/include/cupid_host_abi.h \
	../toolchain/hosted/i386-linux/include/direct.h \
	../toolchain/hosted/i386-linux/include/errno.h \
	../toolchain/hosted/i386-linux/include/stddef.h \
	../toolchain/hosted/i386-linux/include/stdint.h \
	../toolchain/hosted/i386-linux/include/stdio.h \
	../toolchain/hosted/i386-linux/include/stdlib.h \
	../toolchain/hosted/i386-linux/include/string.h \
	../toolchain/hosted/i386-linux/include/unistd.h \
	../toolchain/hosted/i386-linux/include/windows.h \
	../toolchain/hosted/i386-windows/publication_runtime.cc \
	../toolchain/hosted/i386-windows/publication_start.asm \
	../toolchain/hosted/i386-windows/runtime.cc \
	../toolchain/hosted/i386-windows/start.asm \
	../toolchain/hosted/i386-windows/tool_start.asm \
	../toolchain/tests/core_contract.cc \
	../toolchain/tests/cupidasm_contract.cc \
	../toolchain/tests/cupidasm_demos_contract.cc \
	../toolchain/tests/cupidasm_kernel_elf_contract.cc \
	../toolchain/tests/cupidc_frontend_contract.cc \
	../toolchain/tests/cupidc_ir_contract.cc \
	../toolchain/tests/cupidc_kernel_simd_fixture.h \
	../toolchain/tests/cupidc_object_contract.cc \
	../toolchain/tests/cupidc_pp_active_cases.inc \
	../toolchain/tests/cupidc_pp_conditional_cases.inc \
	../toolchain/tests/cupidc_pp_contract.cc \
	../toolchain/tests/cupidc_static_long_double_arithmetic_fixture.h \
	../toolchain/tests/cupidc_static_long_double_control_fixture.h \
	../toolchain/tests/cupidc_static_long_double_integer_fixture.h \
	../toolchain/tests/cupidc_type_contract.cc \
	../toolchain/tests/cupiddis_contract.cc \
	../toolchain/tests/cupidld_contract.cc \
	../toolchain/tests/cupidobj_contract.cc \
	../toolchain/tests/elf32_contract.cc \
	../toolchain/tests/hosted_i386_runtime_contract.cc \
	../toolchain/tests/hosted_i386_windows_contract.cc \
	../toolchain/tests/hosted_i386_windows_runtime_contract.cc \
	../toolchain/tests/user_syscall_abi_contract.cc \
	../toolchain/tests/x86_active_cases.inc \
	../toolchain/tests/x86_contract.cc \
	../toolchain/tests/x86_inline_cases.inc \
	../toolchain/x86.h \
	../tools/bootstrap_toolchain.py \
	../tools/cupidc_toolchain_contracts.py \
	../tools/user_syscall_abi.py \
	cupid.h
USER_SYSCALL_ABI_BOOTSTRAP_SOURCE_INPUTS := \
	../link.ld \
	../toolchain/ctool.cc \
	../toolchain/ctool.h \
	../toolchain/ctool_host.cc \
	../toolchain/ctool_host.h \
	../toolchain/cupidasm.cc \
	../toolchain/cupidasm.h \
	../toolchain/cupidasm_main.cc \
	../toolchain/cupidc_emit.cc \
	../toolchain/cupidc_emit.h \
	../toolchain/cupidc_frontend.cc \
	../toolchain/cupidc_frontend.h \
	../toolchain/cupidc_ir.cc \
	../toolchain/cupidc_ir.h \
	../toolchain/cupidc_main.cc \
	../toolchain/cupidc_pp.cc \
	../toolchain/cupidc_pp.h \
	../toolchain/cupidc_type.cc \
	../toolchain/cupidc_type.h \
	../toolchain/cupiddis.cc \
	../toolchain/cupiddis.h \
	../toolchain/cupiddis_main.cc \
	../toolchain/cupidld.cc \
	../toolchain/cupidld.h \
	../toolchain/cupidld_main.cc \
	../toolchain/cupidobj.cc \
	../toolchain/cupidobj.h \
	../toolchain/cupidobj_main.cc \
	../toolchain/elf32.cc \
	../toolchain/elf32.h \
	../toolchain/hosted/i386-linux/include/cupid_host_abi.h \
	../toolchain/hosted/i386-linux/include/direct.h \
	../toolchain/hosted/i386-linux/include/errno.h \
	../toolchain/hosted/i386-linux/include/stddef.h \
	../toolchain/hosted/i386-linux/include/stdint.h \
	../toolchain/hosted/i386-linux/include/stdio.h \
	../toolchain/hosted/i386-linux/include/stdlib.h \
	../toolchain/hosted/i386-linux/include/string.h \
	../toolchain/hosted/i386-linux/include/unistd.h \
	../toolchain/hosted/i386-linux/include/windows.h \
	../toolchain/hosted/i386-linux/runtime.cc \
	../toolchain/hosted/i386-linux/start.asm \
	../toolchain/hosted/i386-windows/publication_runtime.cc \
	../toolchain/hosted/i386-windows/publication_start.asm \
	../toolchain/hosted/i386-windows/runtime.cc \
	../toolchain/hosted/i386-windows/start.asm \
	../toolchain/hosted/i386-windows/tool_start.asm \
	../toolchain/tests/hosted_i386_windows_contract.cc \
	../toolchain/tests/hosted_i386_windows_runtime_contract.cc \
	../toolchain/x86.cc \
	../toolchain/x86.h
CUPIDC_PRODUCTION_COMPILE_INPUTS := \
	../tools/cupidc_production_compile.py \
	../tools/cupidc_kernel_compile.py \
	../tools/native_user_toolchain.py \
	../tools/bootstrap_toolchain.py \
	$(CHECKED_SEED_INPUTS)
CUPIDLD_USER_LINK_INPUTS := ../tools/cupidld_user_link.py \
	../tools/cupidc_kernel_compile.py \
	../tools/native_user_toolchain.py \
	../tools/bootstrap_toolchain.py \
	$(CHECKED_SEED_INPUTS)
ifeq ($(OS),Windows_NT)
USER_SYSCALL_ABI_INPUTS := $(sort \
	$(NATIVE_WINDOWS_USER_SYSCALL_ABI_INPUTS) \
	$(CHECKED_SEED_INPUTS))
else
USER_SYSCALL_ABI_INPUTS := $(sort \
	$(USER_SYSCALL_ABI_PUBLICATION_INPUTS) \
	$(USER_SYSCALL_ABI_BOOTSTRAP_SOURCE_INPUTS) \
	$(BOOTSTRAP_SEED_INPUTS))
endif
USER_FRONTIER_INPUTS := ../tools/cupidc_production_frontier.py \
	$(CUPIDC_PRODUCTION_COMPILE_INPUTS) $(CUPIDLD_USER_LINK_INPUTS) \
	$(USER_SYSCALL_ABI_INPUTS)
USER_TEXT_ADDRESS ?= 0x01C00000

ifeq ($(OS),Windows_NT)
# Native hosted drivers remain an optional oracle. They are not part of the
# normal user build.
native-user-tools:
	$(MAKE) -C ../toolchain build/cupidc.exe build/cupidld.exe
endif

# List of programs to build
PROGRAMS = hello ls cat

# Build directory
BUILD ?= build
USER_SOURCES := $(addprefix examples/,$(addsuffix .cc,$(PROGRAMS)))
USER_OBJECTS := $(addprefix $(BUILD)/,$(addsuffix .o,$(PROGRAMS)))
BOOTSTRAP_ARTIFACTS := $(addprefix $(BUILD)/,$(PROGRAMS))

all: test-syscall-abi $(BOOTSTRAP_ARTIFACTS)

.PRECIOUS: $(USER_OBJECTS) $(BOOTSTRAP_ARTIFACTS)
.SECONDARY: $(USER_OBJECTS)

$(BUILD)/%: $(BUILD)/%.o $(CUPIDLD_USER_LINK_INPUTS) Makefile
	$(CUPIDLD_USER_LINK) --input user/$< --output user/$@ \
		--text-address $(USER_TEXT_ADDRESS) --entry _start

$(BUILD)/%.o: examples/%.cc cupid.h $(CUPIDC_PRODUCTION_COMPILE_INPUTS) Makefile | test-syscall-abi
	$(CUPIDC_PRODUCTION_COMPILE) --source user/$< --output user/$@

test-syscall-abi: $(USER_SYSCALL_ABI_INPUTS) Makefile
	$(USER_SYSCALL_ABI)

test-cupidc-frontier: all test-syscall-abi $(USER_SOURCES) cupid.h $(USER_FRONTIER_INPUTS) Makefile
	$(PYTHON) ../tools/cupidc_production_frontier.py \
		--root .. --cohort user

ifeq ($(OS),Windows_NT)
test-native-windows-equivalence: native-user-tools all
	$(PYTHON) ../tools/cupidc_production_frontier.py \
		--root .. --cohort user --compare-checked-seed
else
test-native-windows-equivalence:
	$(PYTHON) -c "raise SystemExit('The native Windows equivalence check requires Windows.')"
endif

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree(r'$(BUILD)', ignore_errors=True)"

print-bootstrap-artifacts:
	@$(PYTHON) -c "import json,sys; print(json.dumps(sys.argv[1:]))" $(BOOTSTRAP_ARTIFACTS)

.PHONY: all clean native-user-tools print-bootstrap-artifacts test-cupidc-frontier test-native-windows-equivalence test-syscall-abi
