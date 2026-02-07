# user/Makefile — Build ELF programs for CupidOS
#
# Usage:
#   make -C user              # Build all example programs
#   make -C user clean        # Clean all build artifacts
#
# Programs are compiled as ELF32 i386 static executables.
# They can be copied to the FAT16 disk image and run with:
#   exec /home/<program>
#
# To add a new program:
#   1. Create user/examples/yourprog.c
#   2. Add 'yourprog' to the PROGRAMS list below
#   3. Include "cupid.h" and implement _start(cupid_syscall_table_t *sys)

CC = gcc
LD = ld
CFLAGS = -m32 -fno-pie -nostdlib -static -ffreestanding -O2 -Wall -Wextra \
         -Wno-unused-parameter -I.
LDFLAGS = -m elf_i386 -Ttext=0x00400000 --oformat=elf32-i386

# List of programs to build
PROGRAMS = hello ls cat

# Build directory
BUILD = build

all: $(BUILD) $(addprefix $(BUILD)/,$(PROGRAMS))

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%: $(BUILD)/%.o
	$(LD) $(LDFLAGS) -o $@ $<

$(BUILD)/%.o: examples/%.c cupid.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
