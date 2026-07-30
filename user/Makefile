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

ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
endif

CUPIDC_PRODUCTION_COMPILE := $(PYTHON) ../tools/cupidc_production_compile.py \
	--root .. --cohort user
CUPIDLD_USER_LINK := $(PYTHON) ../tools/cupidld_user_link.py --root ..
USER_SYSCALL_ABI := $(PYTHON) ../tools/user_syscall_abi.py --root ..
CHECKED_SEED_INPUTS := ../bootstrap/seeds/i386-linux/manifest.json \
	../bootstrap/seeds/i386-linux/cupidasm.elf \
	../bootstrap/seeds/i386-linux/cupidc.elf \
	../bootstrap/seeds/i386-linux/cupiddis.elf \
	../bootstrap/seeds/i386-linux/cupidld.elf \
	../bootstrap/seeds/i386-linux/cupidobj.elf
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
USER_SYSCALL_ABI_INPUTS := ../tools/user_syscall_abi.py \
	../kernel/core/types.h \
	../kernel/core/syscall.h \
	../kernel/core/syscall.cc \
	../kernel/fs/vfs.h \
	../kernel/network/socket.h \
	cupid.h
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

all: test-syscall-abi $(BUILD) $(BOOTSTRAP_ARTIFACTS)

.PRECIOUS: $(USER_OBJECTS) $(BOOTSTRAP_ARTIFACTS)
.SECONDARY: $(USER_OBJECTS)

$(BUILD):
	$(PYTHON) -c "from pathlib import Path; Path(r'$@').mkdir(parents=True, exist_ok=True)"

$(BUILD)/%: $(BUILD)/%.o $(CUPIDLD_USER_LINK_INPUTS) Makefile
	$(CUPIDLD_USER_LINK) --input user/$< --output user/$@ \
		--text-address $(USER_TEXT_ADDRESS) --entry _start

$(BUILD)/%.o: examples/%.cc cupid.h $(CUPIDC_PRODUCTION_COMPILE_INPUTS) Makefile | $(BUILD) test-syscall-abi
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
