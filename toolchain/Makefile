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
CONTRACT := $(BUILD_DIR)/core-contract$(EXE)
CUPIDC_PP_CONTRACT := $(BUILD_DIR)/cupidc-pp-contract$(EXE)
CUPIDC_TYPE_CONTRACT := $(BUILD_DIR)/cupidc-type-contract$(EXE)
CUPIDC_FRONTEND_CONTRACT := $(BUILD_DIR)/cupidc-frontend-contract$(EXE)
CUPIDC_PP_ACTIVE_CASES := tests/cupidc_pp_active_cases.inc
CUPIDC_PP_GENERATED_SOURCES := ../kernel/cpu/ksyms_data.c \
                              ../kernel/util/bin_programs_gen.c \
                              ../kernel/util/demos_programs_gen.c \
                              ../kernel/util/docs_programs_gen.c
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
CUPIDLD_CONTRACT := $(BUILD_DIR)/cupidld-contract$(EXE)
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
BOOTSTRAP_ARTIFACTS := $(CONTRACT) $(CUPIDC_PP_CONTRACT) \
                       $(CUPIDC_TYPE_CONTRACT) $(CUPIDC_FRONTEND_CONTRACT) \
                       $(ELF32_CONTRACT) $(X86_CONTRACT) \
                       $(CUPIDDIS_CONTRACT) $(CUPIDDIS) \
                       $(CUPIDASM_CONTRACT) $(CUPIDASM_DEMOS_CONTRACT) \
                       $(CUPIDASM_KERNEL_ELF_CONTRACT) $(CUPIDASM) \
                       $(CUPIDOBJ_CONTRACT) $(CUPIDOBJ) \
                       $(CUPIDLD_CONTRACT) $(CUPIDLD)
CFLAGS ?= -std=c11 -O2 -pedantic -Werror -Wall -Wextra -Wshadow \
          -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
          -Wmissing-prototypes -Wconversion -Wsign-conversion
CPPFLAGS ?= -I.
LDFLAGS ?=

.DEFAULT_GOAL := all

all: $(BOOTSTRAP_ARTIFACTS)

$(BUILD_DIR):
	$(PYTHON) -c "from pathlib import Path; Path(r'$@').mkdir(parents=True, exist_ok=True)"

$(BUILD_DIR)/ctool.o: ctool.c ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ctool_host.o: ctool_host.c ctool_host.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidc_pp.o: cupidc_pp.c cupidc_pp.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidc_type.o: cupidc_type.c cupidc_type.h cupidc_pp.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidc_frontend.o: cupidc_frontend.c cupidc_frontend.h \
                                cupidc_type.h cupidc_pp.h ctool.h | $(BUILD_DIR)
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

$(BUILD_DIR)/cupidld.o: cupidld.c cupidld.h elf32.h ctool.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidld_main.o: cupidld_main.c cupidld.h ctool_host.h ctool.h elf32.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/as_elf.o: ../kernel/lang/as_elf.c ../kernel/lang/as_elf.h cupidasm.h ctool.h elf32.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/core_contract.o: tests/core_contract.c ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidc_pp_contract.o: tests/cupidc_pp_contract.c \
                                 tests/cupidc_pp_conditional_cases.inc \
                                 $(CUPIDC_PP_ACTIVE_CASES) \
                                 cupidc_pp.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidc_type_contract.o: tests/cupidc_type_contract.c \
                                   cupidc_type.h cupidc_pp.h \
                                   ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cupidc_frontend_contract.o: \
    tests/cupidc_frontend_contract.c $(CUPIDC_PP_ACTIVE_CASES) \
    cupidc_frontend.h cupidc_type.h cupidc_pp.h ctool.h ctool_host.h | $(BUILD_DIR)
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

$(BUILD_DIR)/cupidld_contract.o: tests/cupidld_contract.c cupidld.h elf32.h ctool.h ctool_host.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(CONTRACT): $(CORE_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CORE_OBJECTS) -o $@

$(CUPIDC_PP_CONTRACT): $(CUPIDC_PP_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDC_PP_CONTRACT_OBJECTS) -o $@

$(CUPIDC_TYPE_CONTRACT): $(CUPIDC_TYPE_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDC_TYPE_CONTRACT_OBJECTS) -o $@

$(CUPIDC_FRONTEND_CONTRACT): $(CUPIDC_FRONTEND_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDC_FRONTEND_CONTRACT_OBJECTS) -o $@

$(ELF32_CONTRACT): $(ELF32_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(ELF32_OBJECTS) -o $@

$(X86_CONTRACT): $(X86_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(X86_OBJECTS) -o $@

$(CUPIDDIS_CONTRACT): $(CUPIDDIS_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDDIS_CONTRACT_OBJECTS) -o $@

$(CUPIDDIS): $(CUPIDDIS_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDDIS_OBJECTS) -o $@

$(CUPIDASM_CONTRACT): $(CUPIDASM_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDASM_CONTRACT_OBJECTS) -o $@

$(CUPIDASM_DEMOS_CONTRACT): $(CUPIDASM_DEMOS_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDASM_DEMOS_CONTRACT_OBJECTS) -o $@

$(CUPIDASM_KERNEL_ELF_CONTRACT): $(CUPIDASM_KERNEL_ELF_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDASM_KERNEL_ELF_CONTRACT_OBJECTS) -o $@

$(CUPIDASM): $(CUPIDASM_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDASM_OBJECTS) -o $@

$(CUPIDOBJ_CONTRACT): $(CUPIDOBJ_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDOBJ_CONTRACT_OBJECTS) -o $@

$(CUPIDOBJ): $(CUPIDOBJ_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDOBJ_OBJECTS) -o $@

$(CUPIDLD_CONTRACT): $(CUPIDLD_CONTRACT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDLD_CONTRACT_OBJECTS) -o $@

$(CUPIDLD): $(CUPIDLD_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(HOST_REPRO_LDFLAGS) $(CUPIDLD_OBJECTS) -o $@

test: $(CONTRACT) $(CUPIDC_PP_CONTRACT) $(CUPIDC_TYPE_CONTRACT) \
      $(CUPIDC_FRONTEND_CONTRACT) \
      $(ELF32_CONTRACT) $(X86_CONTRACT) \
      $(CUPIDDIS_CONTRACT) \
      $(CUPIDDIS) $(CUPIDASM_CONTRACT) $(CUPIDASM_DEMOS_CONTRACT) \
      $(CUPIDASM_KERNEL_ELF_CONTRACT) $(CUPIDASM) $(CUPIDOBJ_CONTRACT) \
      $(CUPIDOBJ) $(CUPIDLD_CONTRACT) $(CUPIDLD) \
      tests/x86_active_cases.inc tests/x86_inline_cases.inc \
      $(CUPIDC_PP_ACTIVE_CASES)
	$(CONTRACT) foundations
	$(CONTRACT) paths
	$(CONTRACT) diagnostics
	$(CONTRACT) limits
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
	$(CUPIDC_FRONTEND_CONTRACT) static-asserts ..
	$(CUPIDC_FRONTEND_CONTRACT) function-bodies ..
	$(CUPIDC_FRONTEND_CONTRACT) block-bindings ..
	$(CUPIDC_FRONTEND_CONTRACT) scalar-initializers ..
	$(CUPIDC_FRONTEND_CONTRACT) static-initializers ..
	$(CUPIDC_FRONTEND_CONTRACT) aggregate-initializers ..
	$(CUPIDC_FRONTEND_CONTRACT) scalar-returns ..
	$(CUPIDC_FRONTEND_CONTRACT) conditional-expressions ..
	$(CUPIDC_FRONTEND_CONTRACT) aggregate-values ..
	$(CUPIDC_FRONTEND_CONTRACT) for-statements ..
	$(CUPIDC_FRONTEND_CONTRACT) if-statements ..
	$(CUPIDC_FRONTEND_CONTRACT) while-statements ..
	$(CUPIDC_FRONTEND_CONTRACT) do-statements ..
	$(CUPIDC_FRONTEND_CONTRACT) switch-statements ..
	$(CUPIDC_FRONTEND_CONTRACT) pointer-expressions ..
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
	$(CUPIDLD_CONTRACT) fixed-basic
	$(CUPIDLD_CONTRACT) fixed-layout
	$(CUPIDLD_CONTRACT) merge-symbols
	$(CUPIDLD_CONTRACT) merge-addends
	$(CUPIDLD_CONTRACT) script-layout
	$(CUPIDLD_CONTRACT) errors

# Generated C sources remain explicit materialization boundaries.  The hosted
# contract prerequisite completes first; one recipe then serializes the root
# build and execution even under parallel Make.  A missing file is not a skip.
test-cupidc-pp-active-generated: $(CUPIDC_PP_CONTRACT) \
                                 $(CUPIDC_PP_ACTIVE_CASES)
	$(MAKE) -C .. $(patsubst ../%,%,$(CUPIDC_PP_GENERATED_SOURCES))
	$(CUPIDC_PP_CONTRACT) active-corpus-generated ..

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree(r'$(BUILD_DIR)', ignore_errors=True)"

print-bootstrap-artifacts:
	@$(PYTHON) -c "import json,sys; print(json.dumps(sys.argv[1:]))" $(BOOTSTRAP_ARTIFACTS)

.PHONY: all test test-cupidc-pp-active-generated clean \
        print-bootstrap-artifacts
