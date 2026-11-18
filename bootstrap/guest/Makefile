.SUFFIXES:
.DEFAULT_GOAL := all

ROOT := $(abspath ../..)
ifeq ($(OS),Windows_NT)
PYTHON ?= python
SEED_TARGET := i386-windows
SEED_SUFFIX := exe
else
PYTHON ?= python3
SEED_TARGET := i386-linux
SEED_SUFFIX := elf
endif
SEED_DIR := ../../bootstrap/seeds/$(SEED_TARGET)
SEED_MANIFEST := $(SEED_DIR)/manifest.json
SEED_RELEASE := ../../bootstrap/seeds/release.json
SEED_TOOLS := cupidasm cupidbuild cupidc cupiddis cupidld cupidobj
SEED_INPUTS := $(SEED_MANIFEST) $(SEED_RELEASE) $(addprefix $(SEED_DIR)/,$(addsuffix .$(SEED_SUFFIX),$(SEED_TOOLS)))
RUN := "$(abspath $(SEED_DIR)/cupidbuild.$(SEED_SUFFIX))" run --root "$(ROOT)" \
	--seed-manifest "$(abspath $(SEED_MANIFEST))" --seed-release "$(abspath $(SEED_RELEASE))" --timeout 600

TOOLCHAIN := ../../toolchain
BUILD := ../../build/guest-engine
ENGINE := $(BUILD)/cupidc-engine.elf
CORE := ctool elf32 x86 cupidc_pp cupidc_type cupidc_frontend cupidc_ir cupidc_emit
OBJECTS := $(addprefix $(BUILD)/,$(addsuffix .o,$(CORE) cupidc_engine))
HOST_HEADERS := $(wildcard $(TOOLCHAIN)/hosted/i386-linux/include/*.h)
HEADERS := $(addprefix $(TOOLCHAIN)/,$(addsuffix .h,$(CORE))) cupidc_engine.h $(HOST_HEADERS)
PUBLISH := /build/guest-engine
ENGINE_LINK := $(RUN) --tool cupidld -- -m elf_i386 --text-address 0x01100000 --entry ctool_c_engine_compile
CFLAGS := --root "$(ROOT)" --gnu --freestanding -I /toolchain --include-angle /toolchain/hosted/i386-linux/include

.PHONY: all
all: $(ENGINE)

$(BUILD):
	$(PYTHON) -c "from pathlib import Path; Path('$@').mkdir(parents=True, exist_ok=True)"

$(addprefix $(BUILD)/,$(addsuffix .o,$(CORE))): $(BUILD)/%.o: $(TOOLCHAIN)/%.cc $(HEADERS) Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(CFLAGS) -c /toolchain/$*.cc -o $(PUBLISH)/$*.o

$(BUILD)/cupidc_engine.o: cupidc_engine.cc $(HEADERS) Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(CFLAGS) -c /bootstrap/guest/cupidc_engine.cc -o $(PUBLISH)/cupidc_engine.o

$(ENGINE): $(OBJECTS) Makefile $(SEED_INPUTS)
	$(ENGINE_LINK) -o "$(abspath $@)" $(foreach obj,$(OBJECTS),"$(abspath $(obj))")

GUEST_COMPILER := $(BUILD)/cupidc-guest.elf
GUEST_ENTRY := cupidc_guest_run
GUEST_SUPPORT := $(BUILD)/guest_job.o
GUEST_LINKER := $(BUILD)/cupidld-guest.elf
GUEST_LINKER_OBJECTS := $(BUILD)/ctool.o $(BUILD)/elf32.o $(BUILD)/cupidld.o \
    $(GUEST_SUPPORT) $(BUILD)/cupidld_guest.o

.PHONY: guest-compiler guest-linker
guest-compiler: $(GUEST_COMPILER)
guest-linker: $(GUEST_LINKER)

$(BUILD)/cupidc_guest.o: cupidc_guest.cc guest_job.h ../../user/cupid.h $(HEADERS) Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(CFLAGS) -I /user -c /bootstrap/guest/cupidc_guest.cc -o $(PUBLISH)/cupidc_guest.o

$(GUEST_COMPILER): $(OBJECTS) $(GUEST_SUPPORT) $(BUILD)/cupidc_guest.o Makefile $(SEED_INPUTS)
	$(RUN) --tool cupidld -- -m elf_i386 --text-address 0x01e00000 --entry $(GUEST_ENTRY) \
		-o "$(abspath $@)" $(foreach obj,$(filter %.o,$^),"$(abspath $(obj))")

$(BUILD)/guest_job.o: guest_job.cc guest_job.h ../../user/cupid.h $(TOOLCHAIN)/ctool.h Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(CFLAGS) -I /user -c /bootstrap/guest/guest_job.cc -o $(PUBLISH)/guest_job.o

$(BUILD)/cupidld.o: $(TOOLCHAIN)/cupidld.cc $(TOOLCHAIN)/cupidld.h $(TOOLCHAIN)/elf32.h $(TOOLCHAIN)/ctool.h $(HOST_HEADERS) Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(CFLAGS) -c /toolchain/cupidld.cc -o $(PUBLISH)/cupidld.o

$(BUILD)/cupidld_guest.o: cupidld_guest.cc guest_job.h ../../user/cupid.h $(TOOLCHAIN)/cupidld.h $(TOOLCHAIN)/ctool.h Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(CFLAGS) -I /user -c /bootstrap/guest/cupidld_guest.cc -o $(PUBLISH)/cupidld_guest.o

$(GUEST_LINKER): $(GUEST_LINKER_OBJECTS) Makefile $(SEED_INPUTS)
	$(RUN) --tool cupidld -- -m elf_i386 --text-address 0x01e00000 --entry cupidld_guest_run \
		-o "$(abspath $@)" $(foreach obj,$(filter %.o,$^),"$(abspath $(obj))")

# Preserve repository-relative paths so the guest can use the same logical
# source and include names as the checked host compiler.
ISO := $(BUILD)/engsrc.iso
ISO_DIRS := bootstrap bootstrap/guest toolchain toolchain/hosted \
	toolchain/hosted/i386-linux toolchain/hosted/i386-linux/include user
ISO_SOURCES := $(addprefix toolchain/,$(addsuffix .cc,$(CORE))) \
	bootstrap/guest/cupidc_engine.cc bootstrap/guest/cupidc_engine.h \
	bootstrap/guest/cupidc_guest.cc bootstrap/guest/guest_job.cc bootstrap/guest/guest_job.h \
	bootstrap/guest/cupidld_guest.cc bootstrap/guest/TOOLS.CTXT \
	toolchain/cupidld.cc toolchain/cupidld.h user/cupid.h \
	$(patsubst ../../%,%,$(filter-out cupidc_engine.h,$(HEADERS)))
ISO_ENGINE := cupidc-engine.elf

ISO_LINUX_SEED_DIR := ../../bootstrap/seeds/i386-linux
ISO_WINDOWS_SEED_DIR := ../../bootstrap/seeds/i386-windows
ISO_SEEDS := $(addprefix $(ISO_LINUX_SEED_DIR)/,manifest.json $(addsuffix .elf,$(SEED_TOOLS))) \
	$(addprefix $(ISO_WINDOWS_SEED_DIR)/,manifest.json $(addsuffix .exe,$(SEED_TOOLS))) $(SEED_RELEASE)
ISO_PUBLISHER := $(abspath $(SEED_DIR)/cupidbuild.$(SEED_SUFFIX))
ISO_ARGUMENTS := "$(ISO_PUBLISHER)" "$(ROOT)" "$(abspath $(ENGINE))" "$(abspath $(ISO))" \
	$(sort $(ISO_DIRS) $(ISO_SOURCES) $(ISO_ENGINE))

.PHONY: source-iso check-source-iso
source-iso: $(ISO)

check-source-iso: $(ISO)
	$(PYTHON) check_source_iso.py $(ISO_ARGUMENTS)

$(ISO): $(ENGINE) $(addprefix ../../,$(ISO_SOURCES)) Makefile source_iso.py $(ISO_SEEDS)
	$(PYTHON) source_iso.py $(ISO_ARGUMENTS)

ifneq ($(OS),Windows_NT)
LINUX_CONTRACT := $(BUILD)/engine-linux-contract.elf
LINUX_CONTRACT_OBJECTS := $(addprefix $(BUILD)/,start.o linux_runtime.o ctool_host.o \
	engine_linux_contract.o engine_image.o ctool.o elf32.o)
HOSTED_CFLAGS := --root "$(ROOT)" --gnu -I /toolchain --include-angle /toolchain/hosted/i386-linux/include

GUEST_LINUX_CONTRACT := $(BUILD)/cupidc-guest-linux-contract.elf
GUEST_LINUX_OBJECTS := $(OBJECTS) $(GUEST_SUPPORT) $(addprefix $(BUILD)/,start.o linux_runtime.o ctool_host.o \
	cupidc_guest.o cupidc_guest_linux_contract.o)

.PHONY: check-guest-compiler
check-guest-compiler: $(GUEST_COMPILER) $(GUEST_LINUX_CONTRACT)
	$(PYTHON) check_guest_compiler.py "$(ROOT)"

$(GUEST_LINUX_CONTRACT): $(GUEST_LINUX_OBJECTS) Makefile $(SEED_INPUTS)
	$(RUN) --tool cupidld -- -m elf_i386 --text-address 0x08048000 --entry _start \
		-o "$(abspath $@)" $(foreach obj,$(filter %.o,$^),"$(abspath $(obj))")

$(BUILD)/cupidc_guest_linux_contract.o: cupidc_guest_linux_contract.cc guest_job.h ../../user/cupid.h $(TOOLCHAIN)/ctool_host.h $(TOOLCHAIN)/ctool.h $(HOST_HEADERS) Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(HOSTED_CFLAGS) -I /user -c /bootstrap/guest/cupidc_guest_linux_contract.cc -o $(PUBLISH)/cupidc_guest_linux_contract.o

GUEST_LINKER_CONTRACT := $(BUILD)/cupidld-guest-linux-contract.elf
GUEST_LINKER_CONTRACT_OBJECTS := $(GUEST_LINKER_OBJECTS) $(addprefix $(BUILD)/,start.o linux_runtime.o \
	ctool_host.o cupidld_guest_linux_contract.o)

.PHONY: check-guest-linker
check-guest-linker: $(GUEST_LINKER) $(GUEST_LINKER_CONTRACT)
	$(PYTHON) check_guest_linker.py "$(ROOT)"

$(GUEST_LINKER_CONTRACT): $(GUEST_LINKER_CONTRACT_OBJECTS) Makefile $(SEED_INPUTS)
	$(RUN) --tool cupidld -- -m elf_i386 --text-address 0x08048000 --entry _start \
		-o "$(abspath $@)" $(foreach obj,$(filter %.o,$^),"$(abspath $(obj))")

$(BUILD)/cupidld_guest_linux_contract.o: cupidc_guest_linux_contract.cc guest_job.h ../../user/cupid.h $(TOOLCHAIN)/ctool_host.h $(TOOLCHAIN)/ctool.h $(HOST_HEADERS) Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(HOSTED_CFLAGS) -I /user -D CUPID_TEST_GUEST_ENTRY=cupidld_guest_run \
		-c /bootstrap/guest/cupidc_guest_linux_contract.cc -o $(PUBLISH)/cupidld_guest_linux_contract.o

.PHONY: check-load check-linux
check-load: $(ENGINE) $(LINUX_CONTRACT)
	$(PYTHON) check_fixed_point.py "$(ROOT)" --load-only

check-linux: $(ENGINE) $(LINUX_CONTRACT)
	$(PYTHON) check_fixed_point.py "$(ROOT)" $(CORE) -- $(ENGINE_LINK)

$(LINUX_CONTRACT): $(LINUX_CONTRACT_OBJECTS) Makefile $(SEED_INPUTS)
	$(RUN) --tool cupidld -- -m elf_i386 --text-address 0x08048000 --entry _start \
		-o "$(abspath $@)" $(foreach obj,$(LINUX_CONTRACT_OBJECTS),"$(abspath $(obj))")

$(BUILD)/engine_linux_contract.o: engine_linux_contract.cc engine_image.h $(HEADERS) $(TOOLCHAIN)/ctool_host.h Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(HOSTED_CFLAGS) -c /bootstrap/guest/engine_linux_contract.cc -o $(PUBLISH)/engine_linux_contract.o

$(BUILD)/engine_image.o: engine_image.cc engine_image.h $(TOOLCHAIN)/elf32.h $(TOOLCHAIN)/ctool.h Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(CFLAGS) -c /bootstrap/guest/engine_image.cc -o $(PUBLISH)/engine_image.o

$(BUILD)/ctool_host.o: $(TOOLCHAIN)/ctool_host.cc $(TOOLCHAIN)/ctool_host.h $(TOOLCHAIN)/ctool.h $(HOST_HEADERS) Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(HOSTED_CFLAGS) -c /toolchain/ctool_host.cc -o $(PUBLISH)/ctool_host.o

$(BUILD)/linux_runtime.o: $(TOOLCHAIN)/hosted/i386-linux/runtime.cc $(TOOLCHAIN)/path_encoding.h $(HOST_HEADERS) Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(CFLAGS) -c /toolchain/hosted/i386-linux/runtime.cc -o $(PUBLISH)/linux_runtime.o

$(BUILD)/start.o: $(TOOLCHAIN)/hosted/i386-linux/start.asm ../../tools/bootstrap_toolchain.py Makefile $(SEED_INPUTS) | $(BUILD)
	$(PYTHON) ../../tools/bootstrap_toolchain.py run --manifest "$(abspath $(SEED_MANIFEST))" \
		--root "$(ROOT)" --tool cupidasm -- -f elf32 "$(abspath $<)" -o "$(abspath $@)"
endif
