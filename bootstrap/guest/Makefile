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

ifneq ($(OS),Windows_NT)
LINUX_CONTRACT := $(BUILD)/engine-linux-contract.elf
LINUX_CONTRACT_OBJECTS := $(addprefix $(BUILD)/,start.o linux_runtime.o ctool_host.o \
	engine_linux_contract.o ctool.o elf32.o)
HOSTED_CFLAGS := --root "$(ROOT)" --gnu -I /toolchain --include-angle /toolchain/hosted/i386-linux/include

.PHONY: check-linux
check-linux: $(ENGINE) $(LINUX_CONTRACT)
	$(PYTHON) check_fixed_point.py "$(ROOT)" $(CORE) -- $(ENGINE_LINK)

$(LINUX_CONTRACT): $(LINUX_CONTRACT_OBJECTS) Makefile $(SEED_INPUTS)
	$(RUN) --tool cupidld -- -m elf_i386 --text-address 0x08048000 --entry _start \
		-o "$(abspath $@)" $(foreach obj,$(LINUX_CONTRACT_OBJECTS),"$(abspath $(obj))")

$(BUILD)/engine_linux_contract.o: engine_linux_contract.cc $(HEADERS) $(TOOLCHAIN)/ctool_host.h Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(HOSTED_CFLAGS) -c /bootstrap/guest/engine_linux_contract.cc -o $(PUBLISH)/engine_linux_contract.o

$(BUILD)/ctool_host.o: $(TOOLCHAIN)/ctool_host.cc $(TOOLCHAIN)/ctool_host.h $(TOOLCHAIN)/ctool.h $(HOST_HEADERS) Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(HOSTED_CFLAGS) -c /toolchain/ctool_host.cc -o $(PUBLISH)/ctool_host.o

$(BUILD)/linux_runtime.o: $(TOOLCHAIN)/hosted/i386-linux/runtime.cc $(TOOLCHAIN)/path_encoding.h $(HOST_HEADERS) Makefile $(SEED_INPUTS) | $(BUILD)
	$(RUN) --tool cupidc -- $(CFLAGS) -c /toolchain/hosted/i386-linux/runtime.cc -o $(PUBLISH)/linux_runtime.o

$(BUILD)/start.o: $(TOOLCHAIN)/hosted/i386-linux/start.asm ../../tools/bootstrap_toolchain.py Makefile $(SEED_INPUTS) | $(BUILD)
	$(PYTHON) ../../tools/bootstrap_toolchain.py run --manifest "$(abspath $(SEED_MANIFEST))" \
		--root "$(ROOT)" --tool cupidasm -- -f elf32 "$(abspath $<)" -o "$(abspath $@)"
endif
