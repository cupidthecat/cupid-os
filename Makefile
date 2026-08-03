# Compiler settings
.SUFFIXES:
QEMU ?= qemu-system-i386
ifeq ($(OS),Windows_NT)
PYTHON ?= python
ifeq ($(origin CC),default)
CC := clang
endif
HOST_EXE := .exe
CC_TARGET ?= --target=i386-unknown-elf
QEMU_AUDIODEV ?= none,id=speaker
CLANG_COMPAT_CFLAGS ?= -Wno-gnu-zero-variadic-macro-arguments -Wno-strict-prototypes -Wno-implicit-int-conversion -Wno-sign-conversion
else
PYTHON ?= python3
ifeq ($(origin CC),default)
CC := gcc
endif
HOST_EXE :=
CC_TARGET ?=
QEMU_AUDIODEV ?= alsa,id=speaker
CLANG_COMPAT_CFLAGS ?=
endif
CUPIDC_KERNEL_COMPILE := $(PYTHON) tools/cupidc_kernel_compile.py --root .
CUPIDC_KERNEL_COMPILE_INPUTS := Makefile tools/cupidc_kernel_compile.py \
	tools/kernel_cupidc_frontier.py tools/bootstrap_toolchain.py \
	bootstrap/seeds/i386-linux/manifest.json \
	bootstrap/seeds/i386-linux/cupidasm.elf \
	bootstrap/seeds/i386-linux/cupidc.elf \
	bootstrap/seeds/i386-linux/cupiddis.elf \
	bootstrap/seeds/i386-linux/cupidld.elf \
	bootstrap/seeds/i386-linux/cupidobj.elf
CUPIDC_PRODUCTION_COMPILE := $(PYTHON) \
	tools/cupidc_production_compile.py --root . --cohort generated-install
CUPIDC_PRODUCTION_COMPILE_INPUTS := Makefile \
	tools/cupidc_production_compile.py \
	tools/cupidc_kernel_compile.py \
	tools/native_user_toolchain.py \
	tools/bootstrap_toolchain.py \
	bootstrap/seeds/i386-linux/manifest.json \
	bootstrap/seeds/i386-linux/cupidasm.elf \
	bootstrap/seeds/i386-linux/cupidc.elf \
	bootstrap/seeds/i386-linux/cupiddis.elf \
	bootstrap/seeds/i386-linux/cupidld.elf \
	bootstrap/seeds/i386-linux/cupidobj.elf
CUPIDC_PRODUCTION_FRONTIER_INPUTS := \
	tools/cupidc_production_frontier.py tools/hostbuild.py \
	$(CUPIDC_PRODUCTION_COMPILE_INPUTS)
BOOTSTRAP_SEED_MANIFEST ?= bootstrap/seeds/i386-linux/manifest.json
BOOTSTRAP_SEED_DIRECTORY := $(dir $(BOOTSTRAP_SEED_MANIFEST))
CHECKED_SEED_INPUTS := Makefile tools/bootstrap_toolchain.py \
	$(BOOTSTRAP_SEED_MANIFEST) \
	$(BOOTSTRAP_SEED_DIRECTORY)cupidasm.elf \
	$(BOOTSTRAP_SEED_DIRECTORY)cupidc.elf \
	$(BOOTSTRAP_SEED_DIRECTORY)cupiddis.elf \
	$(BOOTSTRAP_SEED_DIRECTORY)cupidld.elf \
	$(BOOTSTRAP_SEED_DIRECTORY)cupidobj.elf
CHECKED_SEED_RUN := $(PYTHON) tools/bootstrap_toolchain.py run \
	--manifest $(BOOTSTRAP_SEED_MANIFEST) --root .
CUPIDDIS_BUILD := toolchain/build/cupiddis$(HOST_EXE)
CUPIDDIS ?= $(CHECKED_SEED_RUN) --tool cupiddis --
CUPIDDIS_INPUTS ?= $(CHECKED_SEED_INPUTS)
CUPIDASM_BUILD := toolchain/build/cupidasm$(HOST_EXE)
CUPIDASM ?= $(CHECKED_SEED_RUN) --tool cupidasm --
CUPIDASM_INPUTS ?= $(CHECKED_SEED_INPUTS)
CUPIDOBJ_BUILD := toolchain/build/cupidobj$(HOST_EXE)
CUPIDOBJ ?= $(CHECKED_SEED_RUN) --tool cupidobj --
CUPIDOBJ_INPUTS ?= $(CHECKED_SEED_INPUTS)
CUPIDLD_BUILD := toolchain/build/cupidld$(HOST_EXE)
CUPIDLD ?= $(CHECKED_SEED_RUN) --tool cupidld --
CUPIDLD_INPUTS ?= $(CHECKED_SEED_INPUTS)
HOSTED_TOOL_CORE_SOURCES := toolchain/ctool.cc toolchain/ctool.h \
	toolchain/ctool_host.cc toolchain/ctool_host.h \
	toolchain/elf32.cc toolchain/elf32.h
CUPIDDIS_SOURCES := $(HOSTED_TOOL_CORE_SOURCES) \
	toolchain/x86.cc toolchain/x86.h \
	toolchain/cupiddis.cc toolchain/cupiddis.h toolchain/cupiddis_main.cc \
	toolchain/Makefile
CUPIDASM_SOURCES := $(HOSTED_TOOL_CORE_SOURCES) \
	toolchain/x86.cc toolchain/x86.h \
	toolchain/cupidasm.cc toolchain/cupidasm.h toolchain/cupidasm_main.cc \
	toolchain/Makefile
CUPIDOBJ_SOURCES := $(HOSTED_TOOL_CORE_SOURCES) \
	toolchain/cupidobj.cc toolchain/cupidobj.h toolchain/cupidobj_main.cc \
	toolchain/Makefile
CUPIDLD_SOURCES := $(HOSTED_TOOL_CORE_SOURCES) \
	toolchain/cupidld.cc toolchain/cupidld.h toolchain/cupidld_main.cc \
	toolchain/Makefile
.DEFAULT_GOAL := all
# NASA Power of 10 compliant flags: pedantic, warnings as errors, strict checks
EXTRA_CFLAGS ?=
# The kernel has no CFI unwinder.  Keep host compilers from emitting allocated
# .eh_frame metadata that cannot be consumed at runtime.
FREESTANDING_CODEGEN_CFLAGS := -fno-asynchronous-unwind-tables -fno-unwind-tables
KERNEL_INCLUDES=-I./kernel -I./kernel/audio -I./kernel/core -I./kernel/cpu \
                -I./kernel/crypto -I./kernel/doom -I./kernel/fs -I./kernel/gfx \
                -I./kernel/gui -I./kernel/lang -I./kernel/mm -I./kernel/network \
                -I./kernel/smp -I./kernel/tls -I./kernel/usb -I./kernel/util \
                -I./drivers -I./toolchain
CFLAGS=$(CC_TARGET) -m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc -ffreestanding $(FREESTANDING_CODEGEN_CFLAGS) -c $(KERNEL_INCLUDES) \
	-mfpmath=sse -msse -msse2 -mstackrealign -fno-omit-frame-pointer \
       -DDEBUG -pedantic -Werror -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
       -Wmissing-prototypes -Wconversion -Wsign-conversion -Wwrite-strings $(EXTRA_CFLAGS) $(CLANG_COMPAT_CFLAGS)
# These legacy flag records define the audited Doom preprocessing profiles.
# Production Doom recipes below use the equivalent fixed CupidC profiles.
CFLAGS_DOOM := $(CC_TARGET) -m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc \
               -ffreestanding $(FREESTANDING_CODEGEN_CFLAGS) -c $(KERNEL_INCLUDES) \
               -I./kernel/doom/src \
               -I./kernel/doom/src/include_stubs \
               -mfpmath=sse -msse -msse2 -mstackrealign -fno-omit-frame-pointer \
               -O2 -Wno-unused -Wno-unused-result \
               -Wno-implicit-function-declaration \
               -Wno-sign-compare -Wno-strict-prototypes \
               -Wno-unused-parameter $(CLANG_COMPAT_CFLAGS)
# The source-tree record adds the dglibc compatibility header and upstream
# Doom definitions to the shared profile.
CFLAGS_DOOM_TREE := $(CFLAGS_DOOM) \
               -include kernel/doom/dglibc_compat.h \
               -Wno-unused-variable -Wno-type-limits \
               -Wno-missing-field-initializers \
               -DDEFAULT_SAVEGAMEDIR=\"/home/doom/\" \
               -DDOOM_PORT_CUPIDOS=1
# Optimisation flags for rendering/computation-only files (no hw I/O or IRQs)
OPT=-O2
# Auto-discover all CupidC programs in bin/.
# Exclude legacy cc2-bootstrap fixtures (old_cc2*) — they're superseded
# by the production CupidC compiler and embed ~265 KB of fixture text
# into the kernel binary, eating the bootloader's reserved kernel-area.
BIN_CC_SRCS := $(sort $(filter-out bin/old_cc2.cc bin/old_cc2_single.cc, $(wildcard bin/*.cc)))
$(info BIN_CC_SRCS=$(BIN_CC_SRCS))
BIN_CC_OBJS := $(BIN_CC_SRCS:.cc=.o)
BIN_CC_NAMES := $(notdir $(basename $(BIN_CC_SRCS)))

# Auto-discover embeddable headers used by CupidC demos/programs
BIN_HDR_SRCS := $(sort $(wildcard bin/*.h))
$(info BIN_HDR_SRCS=$(BIN_HDR_SRCS))
BIN_HDR_OBJS := $(BIN_HDR_SRCS:.h=.h.o)
BIN_HDR_NAMES := $(notdir $(basename $(BIN_HDR_SRCS)))

# Auto-discover browser library sub-files (bin/browser/*.cc).
# These are #include'd by bin/browser.cc and embedded in ramfs at
# /bin/browser/<n>.cc so the CupidC preprocessor can resolve them
# at JIT time. They are NOT runnable programs (not added to BIN_CC_NAMES).
BROWSER_SUB_SRCS := $(sort $(wildcard bin/browser/*.cc))
$(info BROWSER_SUB_SRCS=$(BROWSER_SUB_SRCS))
BROWSER_SUB_OBJS := $(BROWSER_SUB_SRCS:.cc=.o)
BROWSER_SUB_NAMES := $(notdir $(basename $(BROWSER_SUB_SRCS)))

# Auto-discover CupidDoc files to embed at boot (/docs/*.ctxt in ramfs)
DOC_CTXT_SRCS := $(sort $(wildcard cupidos-txt/*.CTXT))
$(info DOC_CTXT_SRCS=$(DOC_CTXT_SRCS))
DOC_CTXT_OBJS := $(DOC_CTXT_SRCS:.CTXT=.o)
DOC_CTXT_NAMES := $(notdir $(basename $(DOC_CTXT_SRCS)))

# Explicit doc assets embedded alongside the manuals
DOC_ASSET_SRCS := image.bmp
$(info DOC_ASSET_SRCS=$(DOC_ASSET_SRCS))
DOC_ASSET_OBJS := $(DOC_ASSET_SRCS:.bmp=.bmp.o)
DOC_ASSET_NAMES := $(notdir $(basename $(DOC_ASSET_SRCS)))

# Auto-discover top-level image assets to seed /home at boot.
HOME_BMP_SRCS := $(sort $(wildcard *.bmp))
HOME_PNG_SRCS := $(sort $(wildcard *.png))
HOME_JPG_SRCS := $(sort $(wildcard *.jpg))
HOME_JPEG_SRCS := $(sort $(wildcard *.jpeg))
HOME_ASSET_SRCS := $(HOME_BMP_SRCS) $(HOME_PNG_SRCS) $(HOME_JPG_SRCS) $(HOME_JPEG_SRCS)
$(info HOME_ASSET_SRCS=$(HOME_ASSET_SRCS))
HOME_ASSET_OBJS := $(addsuffix .o,$(HOME_ASSET_SRCS))
HOME_BMP_NAMES := $(notdir $(basename $(HOME_BMP_SRCS)))
HOME_PNG_NAMES := $(notdir $(basename $(HOME_PNG_SRCS)))
HOME_JPG_NAMES := $(notdir $(basename $(HOME_JPG_SRCS)))
HOME_JPEG_NAMES := $(notdir $(basename $(HOME_JPEG_SRCS)))
EMBED_ASSET_OBJS := $(sort $(DOC_ASSET_OBJS) $(HOME_ASSET_OBJS))

# Auto-discover CupidASM demos to embed at boot (/demos/*.asm in ramfs)
DEMO_ASM_SRCS := $(sort $(wildcard demos/*.asm))
$(info DEMO_ASM_SRCS=$(DEMO_ASM_SRCS))
DEMO_ASM_OBJS := $(DEMO_ASM_SRCS:.asm=.o)
DEMO_ASM_NAMES := $(notdir $(basename $(DEMO_ASM_SRCS)))

# TempleOS God vocabulary data (embedded at boot)
GOD_DD_SRCS := $(sort $(wildcard god/*.DD))
$(info GOD_DD_SRCS=$(GOD_DD_SRCS))
GOD_DD_OBJS := $(GOD_DD_SRCS:.DD=.o)

# System fonts. Bundled TTFs are embedded directly into the kernel
# binary so fontsys can register them at boot without depending on the
# filesystem coming up first.
FONT_TTF_SRCS := $(sort $(wildcard system/fonts/*.ttf))
$(info FONT_TTF_SRCS=$(FONT_TTF_SRCS))
FONT_TTF_OBJS := $(FONT_TTF_SRCS:.ttf=.ttf.o)

# Files
BOOTLOADER=boot/boot.bin
KERNEL=kernel/kernel.bin
OS_IMAGE=cupidos.img
HDD_MB ?= 200
FAT_START_LBA ?= 20480
FAT_OFFSET_BYTES := $(shell $(PYTHON) -c "print($(FAT_START_LBA) * 512)")
WAD_SRCS := $(sort $(wildcard /usr/share/games/doom/freedoom*.wad))
KERNEL_OBJS=kernel/core/kernel.o kernel/cpu/idt.o kernel/cpu/isr.o kernel/cpu/irq.o kernel/cpu/pic.o \
            kernel/fs/fs.o drivers/keyboard.o drivers/timer.o kernel/cpu/math.o drivers/pit.o \
            drivers/speaker.o kernel/lang/shell.o kernel/core/string.o kernel/mm/memory.o drivers/pci.o kernel/usb/usb.o kernel/usb/uhci.o kernel/usb/ehci.o kernel/usb/usb_hid.o kernel/usb/usb_hub.o kernel/usb/usb_msc.o \
            kernel/mm/paging.o drivers/ata.o kernel/fs/blockdev.o kernel/fs/blockcache.o kernel/fs/fat16.o \
            drivers/serial.o kernel/core/panic.o kernel/gui/ed.o \
            drivers/vga.o drivers/mouse.o kernel/gfx/font_8x8.o kernel/gfx/graphics.o \
			kernel/gui/gui.o kernel/gui/desktop.o kernel/core/app_launch.o kernel/core/process.o kernel/core/context_switch.o \
			kernel/gui/clipboard.o kernel/gui/ui.o \
			kernel/lang/godspeak.o \
			kernel/cpu/fpu.o kernel/cpu/libm.o \
            kernel/lang/cupidscript_lex.o kernel/lang/cupidscript_parse.o \
            kernel/lang/cupidscript_exec.o kernel/lang/cupidscript_runtime.o \
            kernel/lang/cupidscript_streams.o kernel/lang/cupidscript_strings.o \
            kernel/lang/cupidscript_arrays.o kernel/lang/cupidscript_jobs.o \
			kernel/gui/ansi.o \
			kernel/gui/terminal_app.o \
			kernel/gui/ctxt_image_worker.o \
            kernel/fs/vfs.o kernel/fs/ramfs.o kernel/fs/devfs.o kernel/fs/fat16_vfs.o kernel/lang/exec.o \
            kernel/fs/homefs.o kernel/fs/loopdev.o kernel/fs/iso9660.o kernel/fs/iso9660_vfs.o \
            kernel/mm/swap_disk.o kernel/mm/swap.o \
            kernel/smp/percpu.o \
            kernel/smp/smp_trampoline.o \
            kernel/smp/lapic.o \
            kernel/smp/ioapic.o \
            kernel/smp/bkl.o \
            kernel/smp/mp_tables.o \
            kernel/smp/acpi.o \
            kernel/smp/smp.o \
            kernel/network/net_if.o \
            kernel/network/arp.o \
            kernel/network/ip.o \
            kernel/network/icmp.o \
            kernel/network/udp.o \
            kernel/network/socket.o \
            kernel/network/tcp.o \
            kernel/network/dhcp.o \
            kernel/network/dns.o \
            kernel/network/sshd.o \
            drivers/rtl8139.o \
            drivers/e1000.o \
            kernel/core/syscall.o \
            kernel/crypto/chacha20.o kernel/crypto/csprng.o \
            kernel/crypto/sha256.o kernel/crypto/sha512.o kernel/crypto/hmac.o kernel/crypto/hkdf.o \
            kernel/crypto/ct.o kernel/crypto/poly1305.o \
            kernel/crypto/chacha20poly1305.o \
            kernel/crypto/aes.o kernel/crypto/aes_gcm.o \
            kernel/crypto/bigint.o kernel/crypto/rsa.o \
            kernel/crypto/x25519.o kernel/crypto/p256.o kernel/crypto/ecdsa.o \
            kernel/crypto/ed25519.o \
            kernel/crypto/asn1.o kernel/crypto/x509.o \
            kernel/crypto/x509_chain.o kernel/tls/tls_ca_bundle.o \
            kernel/tls/tls_record.o kernel/tls/tls_kdf.o \
            kernel/tls/tls_ctx.o kernel/tls/tls_handshake.o \
            kernel/tls/tls12_handshake.o \
            kernel/tls/tls_selftest.o \
			toolchain/ctool.o toolchain/elf32.o toolchain/x86.o toolchain/cupiddis.o toolchain/cupidasm.o kernel/lang/ctool_kernel.o \
			kernel/lang/cupidc.o kernel/lang/cupidc_lex.o kernel/lang/cupidc_parse.o \
			kernel/lang/cupidc_string.o \
            kernel/lang/cupidc_elf.o kernel/lang/ssh_io.o \
			kernel/lang/as.o kernel/lang/as_elf.o \
			kernel/lang/dis.o \
            kernel/gfx/gfx2d.o \
            kernel/gfx/bmp.o \
            kernel/gfx/png.o \
            kernel/gfx/deflate.o \
            kernel/gfx/jpeg.o \
            kernel/gfx/ttf.o \
            kernel/gfx/glyph_raster.o \
            kernel/gfx/fontsys.o \
            kernel/fs/vfs_helpers.o \
            drivers/rtc.o kernel/util/calendar.o \
            kernel/gfx/gfx2d_assets.o kernel/gfx/gfx2d_transform.o kernel/gfx/gfx2d_effects.o \
			kernel/cpu/simd.o \
            kernel/gfx/gfx2d_icons.o \
            kernel/gui/gui_widgets.o kernel/gui/gui_containers.o kernel/gui/gui_menus.o \
            kernel/gui/gui_events.o kernel/gui/gui_themes.o \
            kernel/util/bin_programs_gen.o \
			kernel/util/docs_programs_gen.o \
			kernel/util/demos_programs_gen.o \
			kernel/cpu/ksyms.o \
			kernel/audio/ac97.o \
			kernel/audio/mixer.o \
			kernel/audio/nuked_opl3.o \
			kernel/audio/opl_smoke.o \
			kernel/audio/memio.o \
			kernel/audio/mus2midi.o \
			kernel/audio/midiopl.o \
			$(BIN_CC_OBJS) $(BIN_HDR_OBJS) $(BROWSER_SUB_OBJS) $(DOC_CTXT_OBJS) $(EMBED_ASSET_OBJS) $(DEMO_ASM_OBJS) $(GOD_DD_OBJS) $(FONT_TTF_OBJS)

.PHONY: FORCE
FORCE:

# Keep tracked binary artifacts intact if a later build step fails.
.PRECIOUS: $(BOOTLOADER) $(KERNEL)

all: $(OS_IMAGE)

# Compile bootloader
$(BOOTLOADER): boot/boot.asm $(CUPIDASM_INPUTS)
	$(CUPIDASM) -f bin boot/boot.asm -o $(BOOTLOADER)

# Compile C source files
kernel/core/kernel.o: kernel/core/kernel.cc drivers/ata.h drivers/keyboard.h \
	drivers/mouse.h drivers/pci.h drivers/pit.h drivers/rtc.h \
	drivers/serial.h drivers/speaker.h drivers/timer.h drivers/vga.h \
	kernel/core/debug.h kernel/core/kernel.h kernel/core/panic.h \
	kernel/core/ports.h kernel/core/process.h kernel/core/string.h \
	kernel/core/syscall.h kernel/core/types.h kernel/cpu/fpu.h \
	kernel/cpu/idt.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/cpu/pic.h \
	kernel/cpu/simd.h kernel/crypto/csprng.h kernel/fs/blockcache.h \
	kernel/fs/blockdev.h kernel/fs/devfs.h kernel/fs/fat16.h \
	kernel/fs/fat16_vfs.h kernel/fs/fs.h kernel/fs/homefs.h \
	kernel/fs/iso9660_vfs.h kernel/fs/ramfs.h kernel/fs/vfs.h \
	kernel/gfx/fontsys.h kernel/gfx/gfx2d.h kernel/gfx/graphics.h \
	kernel/gui/clipboard.h kernel/gui/desktop.h kernel/gui/gui.h \
	kernel/gui/gui_containers.h kernel/gui/gui_events.h \
	kernel/gui/gui_menus.h kernel/gui/gui_themes.h kernel/gui/gui_widgets.h \
	kernel/gui/ui.h kernel/lang/as.h kernel/lang/ctool_kernel.h \
	kernel/lang/exec.h kernel/lang/shell.h kernel/mm/memory.h \
	kernel/network/net_if.h kernel/smp/bkl.h kernel/smp/ioapic.h \
	kernel/smp/lapic.h kernel/smp/percpu.h kernel/smp/smp.h \
	kernel/tls/tls_selftest.h kernel/usb/usb.h kernel/usb/usb_hc.h \
	kernel/util/calendar.h toolchain/ctool.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/core/kernel.cc --output kernel/core/kernel.o

kernel/cpu/simd.o: kernel/cpu/simd.cc drivers/serial.h drivers/timer.h \
	kernel/core/kernel.h kernel/core/string.h kernel/core/types.h \
	kernel/cpu/isr.h kernel/cpu/simd.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/cpu/simd.cc --output kernel/cpu/simd.o

kernel/cpu/idt.o: kernel/cpu/idt.cc drivers/serial.h kernel/core/kernel.h \
	kernel/core/panic.h kernel/core/types.h kernel/cpu/idt.h kernel/cpu/isr.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/cpu/idt.cc --output kernel/cpu/idt.o

# Compile assembly files
kernel/cpu/isr.o: kernel/cpu/isr.asm $(CUPIDASM_INPUTS)
	$(CUPIDASM) -f elf32 kernel/cpu/isr.asm -o kernel/cpu/isr.o

kernel/cpu/pic.o: kernel/cpu/pic.cc kernel/core/kernel.h kernel/core/types.h \
	kernel/cpu/isr.h kernel/cpu/pic.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/cpu/pic.cc --output kernel/cpu/pic.o

kernel/cpu/irq.o: kernel/cpu/irq.cc kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/cpu/math.h kernel/cpu/pic.h kernel/smp/bkl.h kernel/smp/ioapic.h kernel/smp/lapic.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/cpu/irq.cc --output kernel/cpu/irq.o

# Symbol-table runtime + (weak) blob fallback. The strong blob lives in
# the auto-generated kernel/cpu/ksyms_data.o; see the kernel link rule.
kernel/cpu/ksyms.o: kernel/cpu/ksyms.cc kernel/core/types.h kernel/cpu/ksyms.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/cpu/ksyms.cc --output kernel/cpu/ksyms.o

# PS/2 keyboard driver
drivers/keyboard.o: drivers/keyboard.cc drivers/keyboard.h drivers/rtc.h \
	drivers/serial.h drivers/vga.h kernel/core/kernel.h kernel/core/ports.h \
	kernel/core/process.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h \
	kernel/gui/desktop.h kernel/gui/gui.h kernel/lang/shell.h \
	kernel/util/calendar.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/keyboard.cc --output drivers/keyboard.o

# Add new rule for timer.o
drivers/timer.o: drivers/timer.cc drivers/keyboard.h drivers/timer.h kernel/core/kernel.h kernel/core/ports.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/cpu/math.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/timer.cc --output drivers/timer.o

# Compile the shared integer and graphics math helpers with CupidC.
kernel/cpu/math.o: kernel/cpu/math.cc kernel/core/kernel.h kernel/core/types.h kernel/cpu/isr.h kernel/cpu/math.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/cpu/math.cc --output kernel/cpu/math.o

# Programmable interval timer driver
drivers/pit.o: drivers/pit.cc drivers/pit.h kernel/core/ports.h \
	kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/pit.cc --output drivers/pit.o

# PC speaker driver
drivers/speaker.o: drivers/speaker.cc drivers/pit.h drivers/speaker.h \
	drivers/timer.h kernel/core/kernel.h kernel/core/ports.h \
	kernel/core/types.h kernel/cpu/isr.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/speaker.cc --output drivers/speaker.o

# ATA block-device driver
drivers/ata.o: drivers/ata.cc drivers/ata.h kernel/core/debug.h \
	kernel/core/kernel.h kernel/core/ports.h kernel/core/types.h \
	kernel/cpu/isr.h kernel/fs/blockdev.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/ata.cc --output drivers/ata.o

# Kernel command shell
kernel/lang/shell.o: kernel/lang/shell.cc drivers/keyboard.h drivers/pci.h \
	drivers/rtc.h drivers/serial.h drivers/timer.h drivers/vga.h \
	kernel/core/app_launch.h kernel/core/assert.h kernel/core/kernel.h \
	kernel/core/panic.h kernel/core/ports.h kernel/core/process.h \
	kernel/core/string.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h \
	kernel/cpu/math.h kernel/fs/blockcache.h kernel/fs/blockdev.h \
	kernel/fs/fat16.h kernel/fs/fs.h kernel/fs/vfs.h kernel/gfx/gfx2d.h \
	kernel/gui/ansi.h kernel/gui/desktop.h kernel/gui/gui.h \
	kernel/gui/gui_themes.h kernel/gui/terminal_app.h kernel/lang/as.h \
	kernel/lang/cupidc.h kernel/lang/cupidscript.h \
	kernel/lang/cupidscript_arrays.h kernel/lang/cupidscript_jobs.h \
	kernel/lang/cupidscript_streams.h kernel/lang/dis.h kernel/lang/exec.h \
	kernel/lang/shell.h kernel/mm/memory.h kernel/mm/swap.h \
	kernel/network/arp.h kernel/network/dns.h kernel/network/icmp.h \
	kernel/network/ip.h kernel/network/net_if.h kernel/network/socket.h \
	kernel/network/sshd.h kernel/smp/bkl.h kernel/smp/percpu.h \
	kernel/smp/smp.h kernel/usb/usb.h kernel/usb/usb_hc.h \
	kernel/util/calendar.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/shell.cc --output kernel/lang/shell.o

# Add new rule for string.o
kernel/core/string.o: kernel/core/string.cc kernel/core/string.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/core/string.cc --output kernel/core/string.o

# Add new rule for fs.o
kernel/fs/fs.o: kernel/fs/fs.cc kernel/core/string.h kernel/core/types.h kernel/fs/fs.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/fs.cc --output kernel/fs/fs.o

# Add new rule for memory.o
kernel/mm/memory.o: kernel/mm/memory.cc drivers/serial.h drivers/timer.h kernel/core/kernel.h kernel/core/panic.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/mm/memory.h kernel/smp/bkl.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/mm/memory.cc --output kernel/mm/memory.o

# PCI configuration space layer
drivers/pci.o: drivers/pci.cc drivers/pci.h drivers/serial.h \
	kernel/core/ports.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/pci.cc --output drivers/pci.o

# AP trampoline raw binary blob (P5 SMP T8)
kernel/smp_trampoline.bin: kernel/smp/smp_trampoline.S \
	$(CUPIDASM_INPUTS)
	$(CUPIDASM) -f bin -o $@ $<

kernel/smp/smp_trampoline.o: kernel/smp_trampoline.bin \
	$(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) wrap $< --stem smp_trampoline --section .rodata --readonly -o $@

# Per-CPU data infrastructure (P5 SMP)
kernel/smp/percpu.o: kernel/smp/percpu.cc drivers/serial.h kernel/core/process.h \
	kernel/core/types.h kernel/smp/percpu.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/smp/percpu.cc --output kernel/smp/percpu.o

# Local APIC BSP init + timer calibration (P5 SMP)
kernel/smp/lapic.o: kernel/smp/lapic.cc drivers/serial.h kernel/core/ports.h \
	kernel/core/types.h kernel/mm/memory.h kernel/smp/lapic.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/smp/lapic.cc --output kernel/smp/lapic.o

# IOAPIC redirection table + 8259 mask (P5 SMP)
kernel/smp/ioapic.o: kernel/smp/ioapic.cc drivers/serial.h kernel/core/types.h kernel/mm/memory.h kernel/smp/ioapic.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/smp/ioapic.cc --output kernel/smp/ioapic.o

# Big Kernel Lock: recursive ticket spinlock, IRQ-save (P5 T7)
kernel/smp/bkl.o: kernel/smp/bkl.cc kernel/core/process.h kernel/core/types.h kernel/smp/bkl.h kernel/smp/percpu.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/smp/bkl.cc --output kernel/smp/bkl.o

# MP tables discovery (P5 SMP)
kernel/smp/mp_tables.o: kernel/smp/mp_tables.cc kernel/smp/mp_tables.h kernel/smp/ioapic.h kernel/smp/percpu.h kernel/core/process.h kernel/core/types.h drivers/serial.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/smp/mp_tables.cc --output kernel/smp/mp_tables.o

# ACPI MADT fallback discovery (P5 SMP)
kernel/smp/acpi.o: kernel/smp/acpi.cc kernel/smp/acpi.h kernel/smp/mp_tables.h kernel/smp/ioapic.h kernel/smp/percpu.h kernel/core/process.h kernel/core/types.h drivers/serial.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/smp/acpi.cc --output kernel/smp/acpi.o

# SMP discovery orchestration + AP bringup (P5 T9)
kernel/smp/smp.o: kernel/smp/smp.cc drivers/serial.h kernel/core/process.h \
	kernel/core/types.h kernel/cpu/fpu.h kernel/cpu/idt.h kernel/cpu/isr.h \
	kernel/mm/memory.h kernel/smp/acpi.h kernel/smp/bkl.h kernel/smp/ioapic.h \
	kernel/smp/lapic.h kernel/smp/mp_tables.h kernel/smp/percpu.h kernel/smp/smp.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/smp/smp.cc --output kernel/smp/smp.o

# NIC interface scaffold + 64-slot lockless RX ring (P6 T1)
kernel/network/net_if.o: kernel/network/net_if.cc drivers/serial.h kernel/core/types.h kernel/network/arp.h kernel/network/dhcp.h kernel/network/ip.h kernel/network/net_if.h kernel/network/tcp.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/network/net_if.cc --output kernel/network/net_if.o

# ARP: 16-entry cache + blocking resolve + Ethernet dispatch (P6 T6)
kernel/network/arp.o: kernel/network/arp.cc drivers/serial.h drivers/timer.h kernel/core/kernel.h kernel/core/types.h kernel/cpu/isr.h kernel/network/arp.h kernel/network/net_if.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/network/arp.cc --output kernel/network/arp.o

# IPv4: parse + build + dispatch to ICMP/UDP/TCP (P6 T7)
kernel/network/ip.o: kernel/network/ip.cc drivers/serial.h drivers/timer.h kernel/core/kernel.h kernel/core/types.h kernel/cpu/isr.h kernel/network/arp.h kernel/network/icmp.h kernel/network/ip.h kernel/network/net_if.h kernel/network/tcp.h kernel/network/udp.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/network/ip.cc --output kernel/network/ip.o

# ICMP: echo reply (P6 T8)
kernel/network/icmp.o: kernel/network/icmp.cc drivers/timer.h kernel/core/kernel.h kernel/core/types.h kernel/cpu/isr.h kernel/network/icmp.h kernel/network/ip.h kernel/network/net_if.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/network/icmp.cc --output kernel/network/icmp.o

# UDP: send + recv + pseudo-header checksum (P6 T9)
kernel/network/udp.o: kernel/network/udp.cc drivers/serial.h kernel/core/types.h kernel/network/dhcp.h kernel/network/ip.h kernel/network/net_if.h kernel/network/udp.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/network/udp.cc --output kernel/network/udp.o

# Socket table + BSD UDP API (P6 T10)
kernel/network/socket.o: kernel/network/socket.cc drivers/rtc.h drivers/serial.h drivers/timer.h kernel/core/kernel.h kernel/core/process.h kernel/core/types.h kernel/cpu/isr.h kernel/crypto/sha256.h kernel/crypto/x509.h kernel/crypto/x509_chain.h kernel/mm/memory.h kernel/network/socket.h kernel/network/tcp.h kernel/network/udp.h kernel/smp/bkl.h kernel/tls/tls_ctx.h kernel/tls/tls_record.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/network/socket.cc --output kernel/network/socket.o

# TCP client state machine (P6 T13)
kernel/network/tcp.o: kernel/network/tcp.cc drivers/timer.h kernel/core/kernel.h kernel/core/process.h kernel/core/types.h kernel/cpu/cpu.h kernel/cpu/isr.h kernel/network/ip.h kernel/network/net_if.h kernel/network/socket.h kernel/network/tcp.h kernel/smp/bkl.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/network/tcp.cc --output kernel/network/tcp.o

# DHCP client with static fallback (P6 T11)
kernel/network/dhcp.o: kernel/network/dhcp.cc drivers/serial.h drivers/timer.h kernel/core/kernel.h kernel/core/types.h kernel/cpu/isr.h kernel/network/dhcp.h kernel/network/ip.h kernel/network/net_if.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/network/dhcp.cc --output kernel/network/dhcp.o

# DNS A-record resolver + 16-entry cache (P6 T12)
kernel/network/dns.o: kernel/network/dns.cc drivers/serial.h drivers/timer.h kernel/core/kernel.h kernel/core/types.h kernel/cpu/isr.h kernel/network/dns.h kernel/network/net_if.h kernel/network/socket.h kernel/smp/bkl.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/network/dns.cc --output kernel/network/dns.o

kernel/network/sshd.o: kernel/network/sshd.cc drivers/serial.h drivers/timer.h kernel/core/kernel.h kernel/core/process.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/crypto/chacha20.h kernel/crypto/csprng.h kernel/crypto/ecdsa.h kernel/crypto/hkdf.h kernel/crypto/hmac.h kernel/crypto/p256.h kernel/crypto/poly1305.h kernel/crypto/sha256.h kernel/crypto/x25519.h kernel/fs/vfs.h kernel/lang/shell.h kernel/mm/memory.h kernel/network/socket.h kernel/network/sshd.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/network/sshd.cc --output kernel/network/sshd.o

# RTL8139 NIC driver: PCI probe, reset, RX/TX buffers, MAC read (P6 T3)
drivers/rtl8139.o: drivers/rtl8139.cc drivers/pci.h drivers/serial.h \
	kernel/core/ports.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h \
	kernel/mm/memory.h kernel/network/net_if.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/rtl8139.cc --output drivers/rtl8139.o

# E1000 (Intel 82540EM) NIC driver: MMIO probe, RX/TX rings, MAC read (P6 T15)
drivers/e1000.o: drivers/e1000.cc drivers/pci.h drivers/serial.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/mm/memory.h kernel/network/net_if.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/e1000.cc --output drivers/e1000.o

# TLS subsystem: crypto primitives, X.509, handshake state machine.
# Built phase by phase under kernel/tls/. See plan in
# /home/frank/.claude/plans/implementy-tls-into-the-breezy-biscuit.md.
kernel/crypto/chacha20.o: kernel/crypto/chacha20.cc kernel/crypto/chacha20.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/chacha20.cc --output kernel/crypto/chacha20.o

kernel/crypto/csprng.o: kernel/crypto/csprng.cc kernel/crypto/csprng.h kernel/crypto/chacha20.h kernel/core/types.h drivers/serial.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/csprng.cc --output kernel/crypto/csprng.o

kernel/crypto/sha256.o: kernel/crypto/sha256.cc kernel/crypto/sha256.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/sha256.cc --output kernel/crypto/sha256.o

kernel/crypto/sha512.o: kernel/crypto/sha512.cc kernel/crypto/sha512.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/sha512.cc --output kernel/crypto/sha512.o

kernel/crypto/hmac.o: kernel/crypto/hmac.cc kernel/crypto/hmac.h kernel/crypto/sha256.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/hmac.cc --output kernel/crypto/hmac.o

kernel/crypto/hkdf.o: kernel/crypto/hkdf.cc kernel/crypto/hkdf.h kernel/crypto/hmac.h kernel/crypto/sha256.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/hkdf.cc --output kernel/crypto/hkdf.o

kernel/crypto/ct.o: kernel/crypto/ct.cc kernel/crypto/ct.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/ct.cc --output kernel/crypto/ct.o

kernel/crypto/poly1305.o: kernel/crypto/poly1305.cc kernel/crypto/poly1305.h kernel/crypto/ct.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/poly1305.cc --output kernel/crypto/poly1305.o

kernel/crypto/chacha20poly1305.o: kernel/crypto/chacha20poly1305.cc kernel/crypto/chacha20poly1305.h kernel/crypto/chacha20.h kernel/crypto/poly1305.h kernel/crypto/ct.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/chacha20poly1305.cc --output kernel/crypto/chacha20poly1305.o

kernel/crypto/aes.o: kernel/crypto/aes.cc kernel/crypto/aes.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/aes.cc --output kernel/crypto/aes.o

kernel/crypto/aes_gcm.o: kernel/crypto/aes_gcm.cc kernel/crypto/aes_gcm.h kernel/crypto/aes.h kernel/crypto/ct.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/aes_gcm.cc --output kernel/crypto/aes_gcm.o

kernel/crypto/bigint.o: kernel/crypto/bigint.cc kernel/crypto/bigint.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/bigint.cc --output kernel/crypto/bigint.o

kernel/crypto/rsa.o: kernel/crypto/rsa.cc kernel/crypto/rsa.h kernel/crypto/bigint.h kernel/crypto/sha256.h kernel/crypto/ct.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/rsa.cc --output kernel/crypto/rsa.o

kernel/crypto/x25519.o: kernel/crypto/x25519.cc kernel/crypto/x25519.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/x25519.cc --output kernel/crypto/x25519.o

kernel/crypto/p256.o: kernel/crypto/p256.cc kernel/crypto/p256.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/p256.cc --output kernel/crypto/p256.o

kernel/crypto/ecdsa.o: kernel/crypto/ecdsa.cc kernel/crypto/ecdsa.h kernel/crypto/p256.h kernel/crypto/hmac.h kernel/crypto/sha256.h kernel/core/string.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/ecdsa.cc --output kernel/crypto/ecdsa.o

kernel/crypto/ed25519.o: kernel/crypto/ed25519.cc kernel/crypto/ed25519.h kernel/crypto/sha512.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/ed25519.cc --output kernel/crypto/ed25519.o

kernel/crypto/asn1.o: kernel/crypto/asn1.cc kernel/crypto/asn1.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/asn1.cc --output kernel/crypto/asn1.o

kernel/crypto/x509.o: kernel/crypto/x509.cc kernel/crypto/x509.h kernel/crypto/asn1.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/x509.cc --output kernel/crypto/x509.o

kernel/crypto/x509_chain.o: kernel/crypto/x509_chain.cc kernel/crypto/x509_chain.h kernel/crypto/x509.h kernel/crypto/sha256.h kernel/crypto/sha512.h kernel/crypto/rsa.h kernel/crypto/p256.h kernel/crypto/ecdsa.h kernel/crypto/asn1.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/crypto/x509_chain.cc --output kernel/crypto/x509_chain.o

kernel/tls/tls_ca_bundle.o: kernel/tls/tls_ca_bundle.cc kernel/core/types.h kernel/crypto/x509.h kernel/crypto/x509_chain.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/tls/tls_ca_bundle.cc --output kernel/tls/tls_ca_bundle.o

kernel/tls/tls_record.o: kernel/tls/tls_record.cc kernel/core/types.h kernel/crypto/aes.h kernel/crypto/aes_gcm.h kernel/crypto/chacha20poly1305.h kernel/crypto/ct.h kernel/tls/tls_record.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/tls/tls_record.cc --output kernel/tls/tls_record.o

kernel/tls/tls_kdf.o: kernel/tls/tls_kdf.cc kernel/core/types.h kernel/crypto/hkdf.h kernel/crypto/hmac.h kernel/crypto/sha256.h kernel/tls/tls_kdf.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/tls/tls_kdf.cc --output kernel/tls/tls_kdf.o

kernel/tls/tls_ctx.o: kernel/tls/tls_ctx.cc kernel/core/types.h kernel/crypto/csprng.h kernel/crypto/ct.h kernel/crypto/p256.h kernel/crypto/sha256.h kernel/crypto/x25519.h kernel/crypto/x509.h kernel/crypto/x509_chain.h kernel/tls/tls_ctx.h kernel/tls/tls_record.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/tls/tls_ctx.cc --output kernel/tls/tls_ctx.o

kernel/tls/tls_handshake.o: kernel/tls/tls_handshake.cc drivers/serial.h kernel/core/types.h kernel/crypto/asn1.h kernel/crypto/csprng.h kernel/crypto/ct.h kernel/crypto/ecdsa.h kernel/crypto/hkdf.h kernel/crypto/hmac.h kernel/crypto/p256.h kernel/crypto/rsa.h kernel/crypto/sha256.h kernel/crypto/x25519.h kernel/crypto/x509.h kernel/crypto/x509_chain.h kernel/tls/tls12_handshake.h kernel/tls/tls_ctx.h kernel/tls/tls_kdf.h kernel/tls/tls_record.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/tls/tls_handshake.cc --output kernel/tls/tls_handshake.o

kernel/tls/tls12_handshake.o: kernel/tls/tls12_handshake.cc drivers/serial.h kernel/core/types.h kernel/crypto/asn1.h kernel/crypto/ct.h kernel/crypto/ecdsa.h kernel/crypto/p256.h kernel/crypto/rsa.h kernel/crypto/sha256.h kernel/crypto/x25519.h kernel/crypto/x509.h kernel/crypto/x509_chain.h kernel/tls/tls12_handshake.h kernel/tls/tls_ctx.h kernel/tls/tls_kdf.h kernel/tls/tls_record.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/tls/tls12_handshake.cc --output kernel/tls/tls12_handshake.o

# Optional auto-generated bundle blob; only built if the file exists
# (run tools/fetch_ca_bundle.sh to populate it).
ifneq (,$(wildcard kernel/tls/tls_ca_bundle_data.cc))
KERNEL_OBJS += kernel/tls/tls_ca_bundle_data.o
kernel/tls/tls_ca_bundle_data.o: kernel/tls/tls_ca_bundle_data.cc kernel/core/types.h kernel/crypto/x509.h kernel/crypto/x509_chain.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/tls/tls_ca_bundle_data.cc --output kernel/tls/tls_ca_bundle_data.o
endif

kernel/tls/tls_selftest.o: kernel/tls/tls_selftest.cc drivers/serial.h kernel/core/panic.h kernel/core/types.h kernel/cpu/isr.h kernel/crypto/aes.h kernel/crypto/aes_gcm.h kernel/crypto/asn1.h kernel/crypto/bigint.h kernel/crypto/chacha20poly1305.h kernel/crypto/ecdsa.h kernel/crypto/ed25519.h kernel/crypto/hkdf.h kernel/crypto/hmac.h kernel/crypto/p256.h kernel/crypto/rsa.h kernel/crypto/sha256.h kernel/crypto/sha512.h kernel/crypto/x25519.h kernel/crypto/x509.h kernel/crypto/x509_chain.h kernel/tls/tls_selftest.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/tls/tls_selftest.cc --output kernel/tls/tls_selftest.o

# USB core scaffold
kernel/usb/usb.o: kernel/usb/usb.cc drivers/serial.h drivers/timer.h kernel/core/kernel.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/mm/memory.h kernel/usb/usb.h kernel/usb/usb_hc.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/usb/usb.cc --output kernel/usb/usb.o

# UHCI host controller init + port ops
kernel/usb/uhci.o: kernel/usb/uhci.cc drivers/pci.h drivers/serial.h \
	drivers/timer.h kernel/core/kernel.h kernel/core/panic.h \
	kernel/core/ports.h kernel/core/types.h kernel/cpu/irq.h \
	kernel/cpu/isr.h kernel/mm/memory.h kernel/usb/usb.h kernel/usb/usb_hc.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/usb/uhci.cc --output kernel/usb/uhci.o

# EHCI host controller init + BIOS handoff + port ops
kernel/usb/ehci.o: kernel/usb/ehci.cc drivers/pci.h drivers/serial.h \
	drivers/timer.h kernel/core/kernel.h kernel/core/panic.h \
	kernel/core/ports.h kernel/core/types.h kernel/cpu/irq.h \
	kernel/cpu/isr.h kernel/mm/memory.h kernel/usb/usb.h kernel/usb/usb_hc.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/usb/ehci.cc --output kernel/usb/ehci.o

# USB HID boot-protocol keyboard driver
kernel/usb/usb_hid.o: kernel/usb/usb_hid.cc drivers/keyboard.h drivers/mouse.h drivers/serial.h drivers/timer.h kernel/core/kernel.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/mm/memory.h kernel/usb/usb.h kernel/usb/usb_hc.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/usb/usb_hid.cc --output kernel/usb/usb_hid.o

# USB hub class driver (recursive enumeration + TT routing)
kernel/usb/usb_hub.o: kernel/usb/usb_hub.cc drivers/serial.h kernel/core/types.h kernel/mm/memory.h kernel/usb/usb.h kernel/usb/usb_hc.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/usb/usb_hub.cc --output kernel/usb/usb_hub.o

# USB mass storage class driver (BBB + SCSI)
kernel/usb/usb_msc.o: kernel/usb/usb_msc.cc drivers/serial.h kernel/core/types.h kernel/fs/blockdev.h kernel/mm/memory.h kernel/usb/usb.h kernel/usb/usb_hc.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/usb/usb_msc.cc --output kernel/usb/usb_msc.o

# AC97 audio: BDL DMA, IRQ, and smoke helper
kernel/audio/ac97.o: kernel/audio/ac97.cc drivers/pci.h drivers/serial.h \
	kernel/audio/ac97.h kernel/core/kernel.h kernel/core/ports.h \
	kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/mm/memory.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/audio/ac97.cc --output kernel/audio/ac97.o

# Mixer — 16-slot s16 stereo software mixer
kernel/audio/mixer.o: kernel/audio/mixer.cc kernel/audio/mixer.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/audio/mixer.cc --output kernel/audio/mixer.o

# Nuked OPL3 emulator, vendored LGPL-2.1 and compiled by checked CupidC
kernel/audio/nuked_opl3.o: kernel/audio/nuked_opl3.cc \
	kernel/audio/nuked_opl3.h kernel/core/string.h kernel/core/types.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/audio/nuked_opl3.cc --output kernel/audio/nuked_opl3.o

# Vendored GPL-2 mus2midi and memio sources compiled by checked CupidC
kernel/audio/memio.o: kernel/audio/memio.cc kernel/audio/memio.h kernel/core/string.h kernel/core/types.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/audio/memio.cc --output kernel/audio/memio.o

kernel/audio/mus2midi.o: kernel/audio/mus2midi.cc kernel/audio/memio.h kernel/audio/mus2midi.h kernel/core/string.h kernel/core/types.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/audio/mus2midi.cc --output kernel/audio/mus2midi.o

# midiopl — MIDI → OPL3 synth (our code; built with strict CFLAGS)
kernel/audio/midiopl.o: kernel/audio/midiopl.cc drivers/serial.h kernel/audio/midiopl.h kernel/audio/nuked_opl3.h kernel/core/string.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/audio/midiopl.cc --output kernel/audio/midiopl.o

# OPL smoke test — Nuked-OPL3 → mixer → AC97 path verification
kernel/audio/opl_smoke.o: kernel/audio/opl_smoke.cc drivers/serial.h kernel/audio/ac97.h kernel/audio/mixer.h kernel/audio/nuked_opl3.h kernel/audio/opl_smoke.h kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/audio/opl_smoke.cc --output kernel/audio/opl_smoke.o

# The wrapper freezes every configured include root. Make tracks that same
# complete header search space, including headers not selected by this source.
DOOM_CUPIDC_HEADERS := $(sort $(wildcard drivers/*.h kernel/*.h kernel/*/*.h \
	                         kernel/doom/src/*.h \
	                         kernel/doom/src/include_stubs/*.h \
	                         kernel/doom/src/include_stubs/*/*.h \
	                         toolchain/*.h \
	                         toolchain/hosted/i386-linux/include/*.h \
	                         toolchain/tests/*.h toolchain/tests/*.inc))
DOOM_CUPIDC_INPUT_MANIFEST := build/bootstrap/doom-cupidc-inputs.json

# FORCE makes the profile scan run on every build. The writer retains the
# manifest timestamp unless header membership or content has changed.
$(DOOM_CUPIDC_INPUT_MANIFEST): FORCE $(DOOM_CUPIDC_HEADERS) Makefile \
	tools/cupidc_kernel_compile.py
	$(PYTHON) tools/cupidc_kernel_compile.py --root . \
		--write-profile-input-manifest $@

# dglibc: DOOM libc shim (heap/string/stdio/fmt/setjmp)
kernel/doom/dglibc.o: kernel/doom/dglibc.cc kernel/doom/dglibc.h kernel/core/types.h \
                      kernel/mm/memory.h kernel/fs/vfs.h kernel/core/string.h \
                      drivers/serial.h drivers/timer.h $(DOOM_CUPIDC_HEADERS) \
                      $(DOOM_CUPIDC_INPUT_MANIFEST) \
                      $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --profile doom-compat --source kernel/doom/dglibc.cc --output kernel/doom/dglibc.o

KERNEL_OBJS += kernel/doom/dglibc.o

# doomgeneric cupidos platform shim
kernel/doom/doomgeneric_cupidos.o: kernel/doom/doomgeneric_cupidos.cc \
                                    kernel/doom/doomgeneric_cupidos.h \
                                    kernel/doom/dglibc.h \
                                    kernel/core/types.h \
                                    drivers/vga.h \
                                    drivers/keyboard.h \
                                    drivers/serial.h \
                                    drivers/timer.h \
                                    kernel/fs/vfs.h \
                                    kernel/usb/usb.h \
                                    $(DOOM_CUPIDC_HEADERS) \
                                    $(DOOM_CUPIDC_INPUT_MANIFEST) \
                                    $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --profile doom-compat --source kernel/doom/doomgeneric_cupidos.cc --output kernel/doom/doomgeneric_cupidos.o

KERNEL_OBJS += kernel/doom/doomgeneric_cupidos.o

# doom_libc_stubs: atoi/sscanf/puts/etc. and i_music stubs
kernel/doom/doom_libc_stubs.o: kernel/doom/doom_libc_stubs.cc \
                                kernel/core/types.h kernel/core/string.h kernel/doom/dglibc.h \
                                drivers/serial.h $(DOOM_CUPIDC_HEADERS) \
                                $(DOOM_CUPIDC_INPUT_MANIFEST) \
                                $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --profile doom-compat --source kernel/doom/doom_libc_stubs.cc --output kernel/doom/doom_libc_stubs.o

KERNEL_OBJS += kernel/doom/doom_libc_stubs.o

# i_sound_cupidos: SFX path, lump cache, and mixer bridge
kernel/doom/i_sound_cupidos.o: kernel/doom/i_sound_cupidos.cc \
                                kernel/core/types.h kernel/mm/memory.h \
                                drivers/serial.h \
                                kernel/audio/mixer.h \
                                kernel/doom/src/i_sound.h \
                                kernel/doom/src/w_wad.h \
                                $(DOOM_CUPIDC_HEADERS) \
                                $(DOOM_CUPIDC_INPUT_MANIFEST) \
                                $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --profile doom-tree --source kernel/doom/i_sound_cupidos.cc --output kernel/doom/i_sound_cupidos.o

KERNEL_OBJS += kernel/doom/i_sound_cupidos.o

# Doom source tree
DOOM_SRC := $(sort $(wildcard kernel/doom/src/*.cc))
DOOM_SRC_OBJS := $(DOOM_SRC:.cc=.o)

kernel/doom/src/%.o: kernel/doom/src/%.cc $(DOOM_CUPIDC_HEADERS) \
	$(DOOM_CUPIDC_INPUT_MANIFEST) $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --profile doom-tree --source $< --output $@

KERNEL_OBJS += $(DOOM_SRC_OBJS)

# Define whole-link contracts only after every conditional and discovered
# KERNEL_OBJS cohort has been appended. GNU Make expands target/prerequisite
# lists and simply-expanded variables at their definition site.
#
# Exact output cohort hashed by the reproducible host-toolchain baseline.
# KERNEL_OBJS remains in link order; the recorder also includes every format
# boundary that is not itself a final link input.
BOOTSTRAP_ARTIFACTS := $(KERNEL_OBJS) \
	$(BOOTLOADER) kernel/smp_trampoline.bin \
	kernel/kernel.elf.pass1 kernel/cpu/ksyms_data.cc kernel/cpu/ksyms_data.o \
	kernel/kernel.elf $(KERNEL) $(OS_IMAGE)

# A build tree may contain artifacts whose mtimes match source checkout mtimes
# closely enough that make incorrectly reuses stale objects from an older
# source revision, producing bad kernels or link failures.
# Force every final kernel object to rebuild from source on each invocation.
$(KERNEL_OBJS) $(BOOTLOADER) $(KERNEL): FORCE

# Add new rule for paging.o
kernel/mm/paging.o: kernel/mm/paging.cc kernel/core/types.h kernel/mm/memory.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/mm/paging.cc --output kernel/mm/paging.o

# Add new rule for blockdev.o
kernel/fs/blockdev.o: kernel/fs/blockdev.cc kernel/core/kernel.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/blockdev.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/blockdev.cc --output kernel/fs/blockdev.o

# Add new rule for blockcache.o
kernel/fs/blockcache.o: kernel/fs/blockcache.cc kernel/core/debug.h kernel/core/kernel.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/blockcache.h kernel/fs/blockdev.h kernel/fs/homefs.h kernel/fs/vfs.h kernel/mm/memory.h kernel/smp/bkl.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/blockcache.cc --output kernel/fs/blockcache.o

# Add new rule for fat16.o
kernel/fs/fat16.o: kernel/fs/fat16.cc drivers/serial.h kernel/core/debug.h kernel/core/kernel.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/blockcache.h kernel/fs/blockdev.h kernel/fs/fat16.h kernel/fs/fat16_control.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/fat16.cc --output kernel/fs/fat16.o

# RTC (Real-Time Clock) driver
drivers/rtc.o: drivers/rtc.cc drivers/rtc.h drivers/serial.h \
	kernel/core/kernel.h kernel/core/ports.h kernel/core/types.h \
	kernel/cpu/isr.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/rtc.cc --output drivers/rtc.o

# Serial port driver
drivers/serial.o: drivers/serial.cc drivers/serial.h drivers/timer.h kernel/core/kernel.h kernel/core/ports.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/smp/bkl.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/serial.cc --output drivers/serial.o

# Panic handler
kernel/core/panic.o: kernel/core/panic.cc drivers/serial.h drivers/timer.h \
	kernel/core/kernel.h kernel/core/panic.h kernel/core/string.h \
	kernel/core/types.h kernel/cpu/fpu.h kernel/cpu/isr.h kernel/cpu/ksyms.h \
	kernel/cpu/math.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/core/panic.cc --output kernel/core/panic.o

# Ed line editor
kernel/gui/ed.o: kernel/gui/ed.cc drivers/keyboard.h kernel/core/kernel.h kernel/core/string.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/cpu/math.h kernel/fs/blockdev.h kernel/fs/fat16.h kernel/fs/fs.h kernel/gui/ed.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/ed.cc --output kernel/gui/ed.o

# VGA graphics mode driver (no -O2: physical address reads trigger array-bounds)
drivers/vga.o: drivers/vga.cc drivers/timer.h drivers/vga.h \
	kernel/core/kernel.h kernel/core/ports.h kernel/core/string.h \
	kernel/core/types.h kernel/cpu/isr.h kernel/cpu/simd.h \
	kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/vga.cc --output drivers/vga.o

# PS/2 mouse driver
drivers/mouse.o: drivers/mouse.cc drivers/mouse.h drivers/serial.h \
	drivers/vga.h kernel/core/ports.h kernel/core/string.h \
	kernel/core/types.h kernel/cpu/isr.h kernel/cpu/pic.h \
	kernel/gfx/graphics.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source drivers/mouse.cc --output drivers/mouse.o

# 8x8 bitmap font
kernel/gfx/font_8x8.o: kernel/gfx/font_8x8.cc kernel/core/types.h kernel/gfx/font_8x8.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/font_8x8.cc --output kernel/gfx/font_8x8.o

# Graphics primitives
kernel/gfx/graphics.o: kernel/gfx/graphics.cc drivers/vga.h kernel/core/string.h kernel/core/types.h kernel/cpu/simd.h kernel/gfx/font_8x8.h kernel/gfx/fontsys.h kernel/gfx/gfx2d.h kernel/gfx/graphics.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/graphics.cc --output kernel/gfx/graphics.o

# GUI / window manager
kernel/gui/gui.o: kernel/gui/gui.cc drivers/mouse.h drivers/rtc.h drivers/serial.h drivers/vga.h kernel/core/process.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/cpu/simd.h kernel/gfx/gfx2d.h kernel/gfx/graphics.h kernel/gui/desktop.h kernel/gui/gui.h kernel/gui/gui_themes.h kernel/mm/memory.h kernel/smp/bkl.h kernel/util/calendar.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/gui.cc --output kernel/gui/gui.o

# Calendar math and formatting
kernel/util/calendar.o: kernel/util/calendar.cc drivers/rtc.h kernel/core/string.h kernel/core/types.h kernel/fs/vfs.h kernel/util/calendar.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/util/calendar.cc --output kernel/util/calendar.o

# Desktop shell
kernel/gui/desktop.o: kernel/gui/desktop.cc drivers/keyboard.h drivers/mouse.h drivers/rtc.h drivers/serial.h drivers/timer.h drivers/vga.h kernel/core/app_launch.h kernel/core/kernel.h kernel/core/process.h kernel/core/string.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/cpu/simd.h kernel/fs/vfs.h kernel/gfx/bmp.h kernel/gfx/gfx2d.h kernel/gfx/gfx2d_icons.h kernel/gfx/graphics.h kernel/gui/desktop.h kernel/gui/gui.h kernel/gui/gui_themes.h kernel/gui/gui_widgets.h kernel/gui/terminal_app.h kernel/gui/ui.h kernel/lang/cupidc.h kernel/lang/dis.h kernel/lang/shell.h kernel/mm/memory.h kernel/util/calendar.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/desktop.cc --output kernel/gui/desktop.o

kernel/core/app_launch.o: kernel/core/app_launch.cc kernel/core/app_launch.h kernel/core/process.h kernel/core/string.h kernel/core/types.h kernel/gui/ctxt_image_worker.h kernel/gui/gui.h kernel/gui/terminal_app.h kernel/lang/cupidc.h kernel/lang/dis.h kernel/lang/shell.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/core/app_launch.cc --output kernel/core/app_launch.o

# Terminal application
kernel/gui/ansi.o: kernel/gui/ansi.cc kernel/core/string.h kernel/core/types.h kernel/gui/ansi.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/ansi.cc --output kernel/gui/ansi.o

kernel/gui/terminal_ansi.o: kernel/gui/terminal_ansi.c kernel/gui/terminal_ansi.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/terminal_ansi.c -o kernel/gui/terminal_ansi.o

kernel/gui/terminal_app.o: kernel/gui/terminal_app.cc drivers/keyboard.h drivers/serial.h drivers/timer.h drivers/vga.h kernel/core/kernel.h kernel/core/process.h kernel/core/string.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/gfx/font_8x8.h kernel/gfx/graphics.h kernel/gui/gui.h kernel/gui/terminal_ansi.h kernel/gui/terminal_app.h kernel/lang/shell.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/terminal_app.cc --output kernel/gui/terminal_app.o

kernel/gui/ctxt_image_worker.o: kernel/gui/ctxt_image_worker.cc drivers/timer.h kernel/core/kernel.h kernel/core/process.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/vfs.h kernel/fs/vfs_helpers.h kernel/gui/ctxt_image_worker.h kernel/mm/memory.h kernel/network/dns.h kernel/network/socket.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/ctxt_image_worker.cc --output kernel/gui/ctxt_image_worker.o

# Process management and round-robin scheduler.
kernel/core/process.o: kernel/core/process.cc drivers/serial.h \
	kernel/core/debug.h kernel/core/kernel.h kernel/core/process.h \
	kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h \
	kernel/cpu/simd.h kernel/gui/gui.h kernel/lang/shell.h \
	kernel/mm/memory.h kernel/smp/bkl.h kernel/smp/percpu.h kernel/smp/smp.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/core/process.cc --output kernel/core/process.o

# Context switch (assembly)
kernel/core/context_switch.o: kernel/core/context_switch.asm \
	$(CUPIDASM_INPUTS)
	$(CUPIDASM) -f elf32 kernel/core/context_switch.asm -o kernel/core/context_switch.o

# Clipboard
kernel/gui/clipboard.o: kernel/gui/clipboard.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/gui/clipboard.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/clipboard.cc --output kernel/gui/clipboard.o

# UI widget toolkit
kernel/gui/ui.o: kernel/gui/ui.cc drivers/vga.h kernel/core/string.h kernel/core/types.h kernel/gfx/font_8x8.h kernel/gfx/gfx2d.h kernel/gfx/graphics.h kernel/gui/gui_themes.h kernel/gui/ui.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/ui.cc --output kernel/gui/ui.o

# GodSpeak helper
kernel/lang/godspeak.o: kernel/lang/godspeak.cc drivers/timer.h kernel/core/kernel.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/lang/godspeak.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/godspeak.cc --output kernel/lang/godspeak.o

kernel/cpu/fpu.o: kernel/cpu/fpu.cc drivers/serial.h kernel/core/panic.h \
	kernel/core/process.h kernel/core/types.h kernel/cpu/fpu.h kernel/cpu/isr.h \
	kernel/cpu/libm.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/cpu/fpu.cc --output kernel/cpu/fpu.o

kernel/cpu/libm.o: kernel/cpu/libm.cc kernel/core/types.h kernel/cpu/libm.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/cpu/libm.cc --output kernel/cpu/libm.o

kernel/lang/cupidscript_lex.o: kernel/lang/cupidscript_lex.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/gui/ansi.h kernel/lang/cupidscript.h kernel/lang/cupidscript_arrays.h kernel/lang/cupidscript_jobs.h kernel/lang/cupidscript_streams.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidscript_lex.cc --output kernel/lang/cupidscript_lex.o

kernel/lang/cupidscript_parse.o: kernel/lang/cupidscript_parse.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/gui/ansi.h kernel/lang/cupidscript.h kernel/lang/cupidscript_arrays.h kernel/lang/cupidscript_jobs.h kernel/lang/cupidscript_streams.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidscript_parse.cc --output kernel/lang/cupidscript_parse.o

kernel/lang/cupidscript_exec.o: kernel/lang/cupidscript_exec.cc drivers/rtc.h drivers/serial.h kernel/core/kernel.h kernel/core/process.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/blockdev.h kernel/fs/fat16.h kernel/fs/fs.h kernel/fs/vfs.h kernel/gui/ansi.h kernel/lang/cupidc.h kernel/lang/cupidscript.h kernel/lang/cupidscript_arrays.h kernel/lang/cupidscript_jobs.h kernel/lang/cupidscript_streams.h kernel/lang/dis.h kernel/lang/exec.h kernel/lang/shell.h kernel/mm/memory.h kernel/util/calendar.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidscript_exec.cc --output kernel/lang/cupidscript_exec.o

kernel/lang/cupidscript_runtime.o: kernel/lang/cupidscript_runtime.cc drivers/rtc.h drivers/serial.h kernel/core/kernel.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/gui/ansi.h kernel/lang/cupidscript.h kernel/lang/cupidscript_arrays.h kernel/lang/cupidscript_jobs.h kernel/lang/cupidscript_streams.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidscript_runtime.cc --output kernel/lang/cupidscript_runtime.o

kernel/lang/cupidscript_streams.o: kernel/lang/cupidscript_streams.cc drivers/keyboard.h kernel/core/string.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/fs/vfs.h kernel/gui/ansi.h kernel/lang/cupidscript.h kernel/lang/cupidscript_arrays.h kernel/lang/cupidscript_jobs.h kernel/lang/cupidscript_streams.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidscript_streams.cc --output kernel/lang/cupidscript_streams.o

kernel/lang/cupidscript_strings.o: kernel/lang/cupidscript_strings.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/gui/ansi.h kernel/lang/cupidscript.h kernel/lang/cupidscript_arrays.h kernel/lang/cupidscript_jobs.h kernel/lang/cupidscript_streams.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidscript_strings.cc --output kernel/lang/cupidscript_strings.o

kernel/lang/cupidscript_arrays.o: kernel/lang/cupidscript_arrays.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/lang/cupidscript_arrays.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidscript_arrays.cc --output kernel/lang/cupidscript_arrays.o

kernel/lang/cupidscript_jobs.o: kernel/lang/cupidscript_jobs.cc drivers/serial.h kernel/core/process.h kernel/core/string.h kernel/core/types.h kernel/cpu/math.h kernel/lang/cupidscript_jobs.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidscript_jobs.cc --output kernel/lang/cupidscript_jobs.o

# VFS core
kernel/fs/vfs.o: kernel/fs/vfs.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/fs/vfs.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/vfs.cc --output kernel/fs/vfs.o

# RamFS
kernel/fs/ramfs.o: kernel/fs/ramfs.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/fs/ramfs.h kernel/fs/vfs.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/ramfs.cc --output kernel/fs/ramfs.o

# DevFS
kernel/fs/devfs.o: kernel/fs/devfs.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/crypto/csprng.h kernel/fs/devfs.h kernel/fs/vfs.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/devfs.cc --output kernel/fs/devfs.o

# FAT16 VFS wrapper
kernel/fs/fat16_vfs.o: kernel/fs/fat16_vfs.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/fs/blockdev.h kernel/fs/fat16.h kernel/fs/fat16_control.h kernel/fs/fat16_vfs.h kernel/fs/vfs.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/fat16_vfs.cc --output kernel/fs/fat16_vfs.o

kernel/fs/homefs.o: kernel/fs/homefs.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/blockcache.h kernel/fs/blockdev.h kernel/fs/fat16.h kernel/fs/fat16_control.h kernel/fs/homefs.h kernel/fs/vfs.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/homefs.cc --output kernel/fs/homefs.o

# File-backed loop block device (for ISO9660 mounting)
kernel/fs/loopdev.o: kernel/fs/loopdev.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/fs/blockdev.h kernel/fs/loopdev.h kernel/fs/vfs.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/loopdev.cc --output kernel/fs/loopdev.o

# ISO9660 / ECMA-119 + Rock Ridge parser
kernel/fs/iso9660.o: kernel/fs/iso9660.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/fs/blockdev.h kernel/fs/iso9660.h kernel/fs/vfs.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/iso9660.cc --output kernel/fs/iso9660.o

kernel/fs/iso9660_vfs.o: kernel/fs/iso9660_vfs.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/fs/blockdev.h kernel/fs/iso9660.h kernel/fs/iso9660_vfs.h kernel/fs/loopdev.h kernel/fs/vfs.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/iso9660_vfs.cc --output kernel/fs/iso9660_vfs.o

kernel/mm/swap_disk.o: kernel/mm/swap_disk.cc drivers/serial.h kernel/core/types.h kernel/fs/vfs.h kernel/mm/swap_disk.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/mm/swap_disk.cc --output kernel/mm/swap_disk.o

kernel/mm/swap.o: kernel/mm/swap.cc drivers/serial.h kernel/core/panic.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/vfs.h kernel/mm/memory.h kernel/mm/swap.h kernel/mm/swap_disk.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/mm/swap.cc --output kernel/mm/swap.o

# Program loader (ELF + CUPD)
kernel/lang/exec.o: kernel/lang/exec.cc drivers/serial.h kernel/core/kernel.h kernel/core/process.h kernel/core/string.h kernel/core/syscall.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/vfs.h kernel/lang/exec.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/exec.cc --output kernel/lang/exec.o

# Syscall table for ELF programs
kernel/core/syscall.o: kernel/core/syscall.cc drivers/ata.h drivers/pci.h \
	drivers/pit.h drivers/serial.h drivers/speaker.h drivers/timer.h \
	kernel/core/kernel.h kernel/core/ports.h kernel/core/process.h \
	kernel/core/string.h kernel/core/syscall.h kernel/core/types.h \
	kernel/cpu/isr.h kernel/fs/blockdev.h kernel/fs/vfs.h \
	kernel/fs/vfs_helpers.h kernel/lang/exec.h kernel/lang/shell.h \
	kernel/mm/memory.h kernel/network/arp.h kernel/network/dns.h \
	kernel/network/icmp.h kernel/network/ip.h kernel/network/net_if.h \
	kernel/network/socket.h kernel/network/udp.h kernel/smp/bkl.h \
	kernel/smp/lapic.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/core/syscall.cc --output kernel/core/syscall.o

# BMP image encoding/decoding
kernel/gfx/bmp.o: kernel/gfx/bmp.cc drivers/serial.h drivers/vga.h kernel/core/string.h kernel/core/types.h kernel/fs/vfs.h kernel/gfx/bmp.h kernel/gfx/gfx2d.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/bmp.cc --output kernel/gfx/bmp.o

kernel/gfx/png.o: kernel/gfx/png.cc kernel/core/types.h kernel/gfx/png.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/png.cc --output kernel/gfx/png.o

kernel/gfx/deflate.o: kernel/gfx/deflate.cc kernel/core/types.h kernel/gfx/deflate.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/deflate.cc --output kernel/gfx/deflate.o

kernel/gfx/jpeg.o: kernel/gfx/jpeg.cc kernel/core/types.h \
	kernel/cpu/libm.h kernel/gfx/jpeg.h kernel/mm/memory.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/jpeg.cc --output kernel/gfx/jpeg.o

# TrueType font system: parser, rasterizer, registry/cache.
kernel/gfx/ttf.o: kernel/gfx/ttf.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/gfx/ttf.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/ttf.cc --output kernel/gfx/ttf.o

kernel/gfx/glyph_raster.o: kernel/gfx/glyph_raster.cc \
	kernel/core/string.h kernel/core/types.h \
	kernel/gfx/glyph_raster.h kernel/mm/memory.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/glyph_raster.cc --output kernel/gfx/glyph_raster.o

kernel/gfx/fontsys.o: kernel/gfx/fontsys.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/fs/vfs.h kernel/fs/vfs_helpers.h kernel/gfx/fontsys.h kernel/gfx/gfx2d.h kernel/gfx/glyph_raster.h kernel/gfx/ttf.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/fontsys.cc --output kernel/gfx/fontsys.o

# VFS helpers (read_all, write_all, read_text, write_text)
kernel/fs/vfs_helpers.o: kernel/fs/vfs_helpers.cc kernel/core/string.h kernel/core/types.h kernel/fs/vfs.h kernel/fs/vfs_helpers.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/fs/vfs_helpers.cc --output kernel/fs/vfs_helpers.o

# 2D graphics library (includes file dialog)
kernel/gfx/gfx2d.o: kernel/gfx/gfx2d.cc drivers/keyboard.h drivers/mouse.h drivers/rtc.h drivers/serial.h drivers/timer.h drivers/vga.h kernel/core/kernel.h kernel/core/process.h kernel/core/string.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/cpu/simd.h kernel/fs/vfs.h kernel/gfx/font_8x8.h kernel/gfx/fontsys.h kernel/gfx/gfx2d.h kernel/gfx/graphics.h kernel/gui/desktop.h kernel/gui/gui.h kernel/gui/ui.h kernel/lang/shell.h kernel/mm/memory.h kernel/util/calendar.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/gfx2d.cc --output kernel/gfx/gfx2d.o

# gfx2d subsystems
kernel/gfx/gfx2d_assets.o: kernel/gfx/gfx2d_assets.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/fs/vfs.h kernel/fs/vfs_helpers.h kernel/gfx/bmp.h kernel/gfx/font_8x8.h kernel/gfx/gfx2d.h kernel/gfx/gfx2d_assets.h kernel/gfx/jpeg.h kernel/gfx/png.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/gfx2d_assets.cc --output kernel/gfx/gfx2d_assets.o

kernel/gfx/gfx2d_transform.o: kernel/gfx/gfx2d_transform.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/cpu/math.h kernel/gfx/gfx2d.h kernel/gfx/gfx2d_assets.h kernel/gfx/gfx2d_transform.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/gfx2d_transform.cc --output kernel/gfx/gfx2d_transform.o

kernel/gfx/gfx2d_effects.o: kernel/gfx/gfx2d_effects.cc drivers/serial.h kernel/core/string.h kernel/core/types.h kernel/cpu/simd.h kernel/gfx/gfx2d.h kernel/gfx/gfx2d_effects.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/gfx2d_effects.cc --output kernel/gfx/gfx2d_effects.o

# Desktop icon system
kernel/gfx/gfx2d_icons.o: kernel/gfx/gfx2d_icons.cc drivers/serial.h kernel/core/app_launch.h kernel/core/process.h kernel/core/string.h kernel/core/types.h kernel/fs/vfs.h kernel/gfx/gfx2d.h kernel/gfx/gfx2d_icons.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gfx/gfx2d_icons.cc --output kernel/gfx/gfx2d_icons.o

# GUI subsystems
kernel/gui/gui_widgets.o: kernel/gui/gui_widgets.cc drivers/vga.h kernel/core/string.h kernel/core/types.h kernel/cpu/math.h kernel/gfx/font_8x8.h kernel/gfx/gfx2d.h kernel/gfx/graphics.h kernel/gui/gui_themes.h kernel/gui/gui_widgets.h kernel/gui/ui.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/gui_widgets.cc --output kernel/gui/gui_widgets.o

kernel/gui/gui_containers.o: kernel/gui/gui_containers.cc drivers/vga.h kernel/core/string.h kernel/core/types.h kernel/gfx/font_8x8.h kernel/gfx/gfx2d.h kernel/gfx/graphics.h kernel/gui/gui_containers.h kernel/gui/gui_themes.h kernel/gui/ui.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/gui_containers.cc --output kernel/gui/gui_containers.o

kernel/gui/gui_menus.o: kernel/gui/gui_menus.cc drivers/vga.h kernel/core/string.h kernel/core/types.h kernel/gfx/font_8x8.h kernel/gfx/gfx2d.h kernel/gfx/graphics.h kernel/gui/gui_menus.h kernel/gui/gui_themes.h kernel/gui/ui.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/gui_menus.cc --output kernel/gui/gui_menus.o

kernel/gui/gui_events.o: kernel/gui/gui_events.cc drivers/vga.h kernel/core/string.h kernel/core/types.h kernel/gfx/font_8x8.h kernel/gfx/gfx2d.h kernel/gfx/graphics.h kernel/gui/gui.h kernel/gui/gui_events.h kernel/gui/ui.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/gui_events.cc --output kernel/gui/gui_events.o

kernel/gui/gui_themes.o: kernel/gui/gui_themes.cc kernel/core/string.h kernel/core/types.h kernel/fs/vfs.h kernel/gui/gui_themes.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/gui/gui_themes.cc --output kernel/gui/gui_themes.o

# CupidC compiler
toolchain/ctool.o: toolchain/ctool.cc toolchain/ctool.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source toolchain/ctool.cc --output toolchain/ctool.o

toolchain/elf32.o: toolchain/elf32.cc toolchain/ctool.h toolchain/elf32.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source toolchain/elf32.cc --output toolchain/elf32.o

toolchain/x86.o: toolchain/x86.cc toolchain/ctool.h toolchain/x86.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source toolchain/x86.cc --output toolchain/x86.o

toolchain/cupiddis.o: toolchain/cupiddis.cc toolchain/ctool.h toolchain/cupiddis.h toolchain/elf32.h toolchain/x86.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source toolchain/cupiddis.cc --output toolchain/cupiddis.o

toolchain/cupidasm.o: toolchain/cupidasm.cc toolchain/ctool.h toolchain/cupidasm.h toolchain/elf32.h toolchain/x86.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source toolchain/cupidasm.cc --output toolchain/cupidasm.o

kernel/lang/ctool_kernel.o: kernel/lang/ctool_kernel.cc drivers/serial.h kernel/core/kernel.h kernel/core/panic.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/vfs.h kernel/fs/vfs_helpers.h kernel/lang/ctool_kernel.h kernel/lang/dis.h kernel/mm/memory.h toolchain/ctool.h toolchain/elf32.h toolchain/x86.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/ctool_kernel.cc --output kernel/lang/ctool_kernel.o

kernel/lang/cupidc.o: kernel/lang/cupidc.cc drivers/ata.h drivers/keyboard.h \
	drivers/mouse.h drivers/pci.h drivers/pit.h drivers/rtc.h \
	drivers/serial.h drivers/speaker.h drivers/timer.h drivers/vga.h \
	kernel/audio/ac97.h kernel/audio/midiopl.h kernel/audio/mixer.h \
	kernel/audio/opl_smoke.h kernel/core/kernel.h kernel/core/panic.h \
	kernel/core/ports.h kernel/core/process.h kernel/core/string.h \
	kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/cpu/libm.h \
	kernel/cpu/math.h kernel/crypto/chacha20.h kernel/crypto/csprng.h \
	kernel/crypto/ed25519.h kernel/crypto/hmac.h kernel/crypto/poly1305.h \
	kernel/crypto/rsa.h kernel/crypto/sha256.h kernel/crypto/sha512.h \
	kernel/crypto/x25519.h kernel/doom/dglibc.h kernel/fs/blockcache.h \
	kernel/fs/blockdev.h kernel/fs/fat16.h kernel/fs/vfs.h \
	kernel/fs/vfs_helpers.h kernel/gfx/bmp.h kernel/gfx/deflate.h \
	kernel/gfx/fontsys.h kernel/gfx/gfx2d.h kernel/gfx/gfx2d_assets.h \
	kernel/gfx/gfx2d_icons.h kernel/gfx/gfx2d_transform.h \
	kernel/gfx/graphics.h kernel/gfx/jpeg.h kernel/gfx/png.h \
	kernel/gui/ansi.h kernel/gui/clipboard.h kernel/gui/ctxt_image_worker.h \
	kernel/gui/desktop.h kernel/gui/ed.h kernel/gui/gui.h \
	kernel/lang/cupidc.h kernel/lang/dis.h kernel/lang/exec.h \
	kernel/lang/shell.h kernel/lang/ssh_io.h kernel/mm/memory.h \
	kernel/mm/swap.h kernel/network/arp.h kernel/network/dhcp.h \
	kernel/network/dns.h kernel/network/icmp.h kernel/network/ip.h \
	kernel/network/net_if.h kernel/network/socket.h kernel/network/udp.h \
	kernel/smp/bkl.h kernel/smp/lapic.h kernel/smp/percpu.h kernel/smp/smp.h \
	kernel/usb/usb.h kernel/usb/usb_hc.h kernel/util/calendar.h \
	$(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidc.cc --output kernel/lang/cupidc.o

kernel/lang/cupidc_string.o: kernel/lang/cupidc_string.cc kernel/core/string.h kernel/core/types.h kernel/lang/cupidc_string.h kernel/mm/memory.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidc_string.cc --output kernel/lang/cupidc_string.o

kernel/lang/cupidc_lex.o: kernel/lang/cupidc_lex.cc kernel/core/string.h kernel/core/types.h kernel/lang/cupidc.h kernel/lang/dis.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidc_lex.cc --output kernel/lang/cupidc_lex.o

kernel/lang/cupidc_parse.o: kernel/lang/cupidc_parse.cc drivers/serial.h kernel/core/kernel.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/lang/cupidc.h kernel/lang/dis.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidc_parse.cc --output kernel/lang/cupidc_parse.o

kernel/lang/cupidc_elf.o: kernel/lang/cupidc_elf.cc drivers/serial.h kernel/core/kernel.h kernel/core/string.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/vfs.h kernel/lang/cupidc.h kernel/lang/dis.h kernel/lang/exec.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/cupidc_elf.cc --output kernel/lang/cupidc_elf.o

kernel/lang/ssh_io.o: kernel/lang/ssh_io.cc drivers/keyboard.h drivers/serial.h kernel/core/kernel.h kernel/core/process.h kernel/core/types.h kernel/cpu/irq.h kernel/cpu/isr.h kernel/crypto/ecdsa.h kernel/crypto/p256.h kernel/gui/gui.h kernel/gui/terminal_app.h kernel/lang/shell.h kernel/lang/ssh_io.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/ssh_io.cc --output kernel/lang/ssh_io.o

# CupidASM assembler
kernel/lang/as.o: kernel/lang/as.cc drivers/ata.h drivers/keyboard.h \
	drivers/mouse.h drivers/pci.h drivers/pit.h drivers/rtc.h \
	drivers/serial.h drivers/speaker.h drivers/timer.h drivers/vga.h \
	kernel/audio/ac97.h kernel/audio/midiopl.h kernel/audio/mixer.h \
	kernel/audio/opl_smoke.h kernel/core/kernel.h kernel/core/panic.h \
	kernel/core/ports.h kernel/core/process.h kernel/core/string.h \
	kernel/core/syscall.h kernel/core/types.h kernel/cpu/irq.h \
	kernel/cpu/isr.h kernel/cpu/libm.h kernel/cpu/math.h \
	kernel/doom/dglibc.h kernel/fs/blockcache.h kernel/fs/blockdev.h \
	kernel/fs/fat16.h kernel/fs/vfs.h kernel/fs/vfs_helpers.h \
	kernel/gfx/bmp.h kernel/gfx/deflate.h kernel/gfx/fontsys.h \
	kernel/gfx/gfx2d.h kernel/gfx/gfx2d_assets.h kernel/gfx/gfx2d_icons.h \
	kernel/gfx/gfx2d_transform.h kernel/gfx/graphics.h kernel/gfx/jpeg.h \
	kernel/gfx/png.h kernel/gui/ansi.h kernel/gui/clipboard.h \
	kernel/gui/desktop.h kernel/gui/ed.h kernel/gui/gui.h \
	kernel/gui/notepad.h kernel/lang/as.h kernel/lang/as_elf.h \
	kernel/lang/ctool_kernel.h kernel/lang/cupidc.h kernel/lang/dis.h \
	kernel/lang/exec.h kernel/lang/shell.h kernel/mm/memory.h kernel/mm/swap.h \
	kernel/network/arp.h kernel/network/dhcp.h kernel/network/dns.h \
	kernel/network/icmp.h kernel/network/ip.h kernel/network/net_if.h \
	kernel/network/socket.h kernel/network/udp.h kernel/smp/bkl.h \
	kernel/smp/lapic.h kernel/smp/percpu.h kernel/smp/smp.h \
	kernel/usb/usb.h kernel/usb/usb_hc.h kernel/util/calendar.h \
	toolchain/ctool.h toolchain/cupidasm.h toolchain/elf32.h \
	toolchain/x86.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/as.cc --output kernel/lang/as.o

kernel/lang/as_elf.o: kernel/lang/as_elf.cc kernel/lang/as_elf.h toolchain/ctool.h toolchain/cupidasm.h toolchain/elf32.h toolchain/x86.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/as_elf.cc --output kernel/lang/as_elf.o

kernel/lang/dis.o: kernel/lang/dis.cc kernel/core/kernel.h kernel/core/types.h kernel/cpu/isr.h kernel/fs/vfs.h kernel/lang/ctool_kernel.h kernel/lang/dis.h toolchain/ctool.h toolchain/cupiddis.h toolchain/elf32.h toolchain/x86.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/lang/dis.cc --output kernel/lang/dis.o

# Auto-generate browser CSS data tables from Blink .in files.
# Produces gen_css_properties.h, gen_css_keywords.h, gen_media_features.h
# consumed by bin/browser parser + style code.
BROWSER_CSS_GEN := bin/browser/gen_css_properties.h \
                   bin/browser/gen_css_keywords.h \
                   bin/browser/gen_media_features.h
BLINK_CSS_INS := blink/Source/core/css/CSSProperties.in \
                 blink/Source/core/css/CSSValueKeywords.in \
                 blink/Source/core/css/MediaFeatureNames.in

$(BROWSER_CSS_GEN): $(BLINK_CSS_INS) tools/gen_css_props.py
	$(PYTHON) tools/gen_css_props.py blink bin/browser

browser_css_gen: $(BROWSER_CSS_GEN)

# Stable host-side verification entry points. The full bootstrap baseline
# builds the committed revision twice in isolated worktrees and records JSON
# evidence under build/bootstrap/ by default.
BOOTSTRAP_AUDIT_BUILDS := --supplemental-build user:all \
	--supplemental-build toolchain:all
BOOTSTRAP_CUPIDC_ACTIVE_CASES := toolchain/tests/cupidc_pp_active_cases.inc
BOOTSTRAP_SEED_OUTPUT ?= build/bootstrap/checked-seed
BOOTSTRAP_WINDOWS_BASELINE ?= docs/bootstrap/baselines/windows-amd64.json
BOOTSTRAP_LINUX_BASELINE ?= docs/bootstrap/baselines/linux-x86_64.json
BOOTSTRAP_HOST_COMPARISON ?= docs/bootstrap/baselines/windows-linux.json

test: test-generated-cupidc-frontier test-user-cupidc-frontier
	$(PYTHON) -m unittest discover -s tests -p "test_*.py"
	$(PYTHON) tools/build_graph_audit.py --root . $(BOOTSTRAP_AUDIT_BUILDS) \
	  --output docs/bootstrap/audits/active-build.json \
	  --summary docs/bootstrap/ACTIVE-SOURCE-AUDIT.md \
	  --c-preprocessor-active-cases $(BOOTSTRAP_CUPIDC_ACTIVE_CASES) --check

test-cupidc-fixed-point: test-toolchain-fixed-point

test-toolchain-fixed-point:
	$(PYTHON) -m unittest -v \
	  tests.test_toolchain_cupidc_object.ToolchainCupidCObjectContractTests.test_cupid_built_toolchain_reaches_a_full_static_fixed_point

test-kernel-cupidc-frontier:
	$(PYTHON) -m unittest -v \
	  tests.test_kernel_cupidc_frontier.RealKernelCupidCFrontierTests

test-kernel-crypto-frontier: test-kernel-cupidc-frontier

verify-bootstrap-seed:
	$(PYTHON) tools/bootstrap_toolchain.py verify \
	  --manifest $(BOOTSTRAP_SEED_MANIFEST)

bootstrap-from-seed: verify-bootstrap-seed
	$(PYTHON) tools/bootstrap_toolchain.py bootstrap \
	  --root . --manifest $(BOOTSTRAP_SEED_MANIFEST) \
	  --output $(BOOTSTRAP_SEED_OUTPUT)

# NASM is not part of the normal build.  When it is installed, this optional
# source-parity suite assembles all four active inputs with both assemblers.
nasm-assembly-oracle:
	$(PYTHON) -m unittest -v tests.test_toolchain_cupidasm_sources

bootstrap-audit:
	$(PYTHON) tools/build_graph_audit.py --root . $(BOOTSTRAP_AUDIT_BUILDS) \
	  --output docs/bootstrap/audits/active-build.json \
	  --summary docs/bootstrap/ACTIVE-SOURCE-AUDIT.md \
	  --c-preprocessor-active-cases $(BOOTSTRAP_CUPIDC_ACTIVE_CASES)

check-bootstrap-audit:
	$(PYTHON) tools/build_graph_audit.py --root . $(BOOTSTRAP_AUDIT_BUILDS) \
	  --output docs/bootstrap/audits/active-build.json \
	  --summary docs/bootstrap/ACTIVE-SOURCE-AUDIT.md \
	  --c-preprocessor-active-cases $(BOOTSTRAP_CUPIDC_ACTIVE_CASES) --check

print-bootstrap-artifacts:
	@$(PYTHON) -c "import json,sys; print(json.dumps(sys.argv[1:]))" $(BOOTSTRAP_ARTIFACTS)

bootstrap-baseline:
	$(PYTHON) tools/bootstrap_baseline.py

bootstrap-host-comparison:
	$(PYTHON) tools/bootstrap_baseline.py \
	  --compare-hosts $(BOOTSTRAP_WINDOWS_BASELINE) $(BOOTSTRAP_LINUX_BASELINE) \
	  --output $(BOOTSTRAP_HOST_COMPARISON)

check-bootstrap-host-comparison:
	$(PYTHON) tools/bootstrap_baseline.py \
	  --compare-hosts $(BOOTSTRAP_WINDOWS_BASELINE) $(BOOTSTRAP_LINUX_BASELINE) \
	  --output $(BOOTSTRAP_HOST_COMPARISON) --check

$(CUPIDASM_BUILD): $(CUPIDASM_SOURCES)
	$(MAKE) -C toolchain $(patsubst toolchain/%,%,$@)

# The hosted Cupid tools share objects in toolchain/build.  Keep independent
# recursive builds ordered under -j while allowing CupidASM assembly jobs to
# proceed once its complete executable is available.
$(CUPIDOBJ_BUILD): $(CUPIDOBJ_SOURCES) | $(CUPIDASM_BUILD)
	$(MAKE) -C toolchain $(patsubst toolchain/%,%,$@)

# Do not make CupidObj's private sources timestamp dependencies of CupidLD.
$(CUPIDLD_BUILD): $(CUPIDLD_SOURCES) | $(CUPIDOBJ_BUILD)
	$(MAKE) -C toolchain $(patsubst toolchain/%,%,$@)

# CupidDis shares hosted objects with the other bootstrap tools. Build it
# after their ordered chain so parallel root builds cannot race those objects.
$(CUPIDDIS_BUILD): $(CUPIDDIS_SOURCES) | $(CUPIDLD_BUILD)
	$(MAKE) -C toolchain $(patsubst toolchain/%,%,$@)

# Generate bin_programs_gen.cc from every active bin/*.cc file.
# This generates extern declarations + install function automatically.
# To add a new CupidC program: just create bin/<name>.cc - that's it!
kernel/util/bin_programs_gen.cc: $(BIN_CC_SRCS) $(BIN_HDR_SRCS) \
	$(BROWSER_SUB_SRCS) $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) install-source bin --bin $(BIN_CC_SRCS) --headers $(BIN_HDR_SRCS) --browser $(BROWSER_SUB_SRCS) -o $@

kernel/util/bin_programs_gen.o: kernel/util/bin_programs_gen.cc \
	drivers/serial.h kernel/core/types.h kernel/fs/ramfs.h kernel/fs/vfs.h \
	$(CUPIDC_PRODUCTION_COMPILE_INPUTS)
	$(CUPIDC_PRODUCTION_COMPILE) --source $< --output $@

# Generate docs_programs_gen.cc from the manuals and seeded home assets.
kernel/util/docs_programs_gen.cc: $(DOC_CTXT_SRCS) $(DOC_ASSET_SRCS) \
	$(HOME_ASSET_SRCS) $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) install-source docs --ctxt $(DOC_CTXT_SRCS) --doc-assets $(DOC_ASSET_SRCS) --home-assets $(HOME_ASSET_SRCS) -o $@

kernel/util/docs_programs_gen.o: kernel/util/docs_programs_gen.cc \
	drivers/serial.h kernel/core/types.h kernel/fs/homefs.h \
	kernel/fs/ramfs.h kernel/fs/vfs.h $(CUPIDC_PRODUCTION_COMPILE_INPUTS)
	$(CUPIDC_PRODUCTION_COMPILE) --source $< --output $@

# Generate demos_programs_gen.cc from the active CupidASM demos.
kernel/util/demos_programs_gen.cc: $(DEMO_ASM_SRCS) $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) install-source demos --demos $(DEMO_ASM_SRCS) -o $@

kernel/util/demos_programs_gen.o: kernel/util/demos_programs_gen.cc \
	drivers/serial.h kernel/core/types.h kernel/fs/ramfs.h kernel/fs/vfs.h \
	$(CUPIDC_PRODUCTION_COMPILE_INPUTS)
	$(CUPIDC_PRODUCTION_COMPILE) --source $< --output $@

test-generated-cupidc-frontier: kernel/util/bin_programs_gen.cc \
	kernel/util/docs_programs_gen.cc kernel/util/demos_programs_gen.cc \
	kernel/util/bin_programs_gen.o kernel/util/docs_programs_gen.o \
	kernel/util/demos_programs_gen.o $(CUPIDC_PRODUCTION_FRONTIER_INPUTS)
	$(PYTHON) tools/cupidc_production_frontier.py \
		--root . --cohort generated-install

# Pattern rule: embed any bin/*.cc file with CupidObj.
bin/%.o: bin/%.cc $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) wrap-text $< -o $@

# Pattern rule: embed any bin/browser/*.cc library file with CupidObj.
# These live in ramfs at /bin/browser/<n>.cc and are #include'd by
# bin/browser.cc at JIT time. They are NOT in BIN_CC_NAMES.
bin/browser/%.o: bin/browser/%.cc $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) wrap-text $< -o $@

# Pattern rule: embed any bin/*.h file with CupidObj (output keeps .h in name).
bin/%.h.o: bin/%.h $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) wrap-text $< -o $@

# Pattern rule: embed any cupidos-txt/*.CTXT file with CupidObj.
cupidos-txt/%.o: cupidos-txt/%.CTXT $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) wrap-text $< -o $@

%.bmp.o: %.bmp $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) wrap $< -o $@

%.png.o: %.png $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) wrap $< -o $@

%.jpg.o: %.jpg tools/hostbuild.py $(CHECKED_SEED_INPUTS)
	$(PYTHON) tools/hostbuild.py embed-jpeg \
		--seed-manifest $(BOOTSTRAP_SEED_MANIFEST) $< $@

%.jpeg.o: %.jpeg tools/hostbuild.py $(CHECKED_SEED_INPUTS)
	$(PYTHON) tools/hostbuild.py embed-jpeg \
		--seed-manifest $(BOOTSTRAP_SEED_MANIFEST) $< $@

# Pattern rule: embed any system/fonts/*.ttf file with CupidObj.
# Object exposes _binary_system_fonts_<name>_ttf_{start,end} symbols
# (dashes in the filename get translated to underscores by CupidObj).
system/fonts/%.ttf.o: system/fonts/%.ttf $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) wrap $< -o $@

# Pattern rule: embed any demos/*.asm file with CupidObj.
demos/%.o: demos/%.asm $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) wrap-text $< -o $@

# Pattern rule: embed any god/*.DD file with CupidObj.
god/%.o: god/%.DD $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) wrap-text $< -o $@

# Link kernel objects.
#
# Two-pass link so the panic backtrace can decode addresses to function
# names:
#   Pass 1: link all KERNEL_OBJS into kernel.elf.pass1.  ksyms.o ships a
#           weak empty .ksyms blob, so this link succeeds with no symbol
#           data.
#   mksyms: CupidDis reads kernel.elf.pass1's symbol table, then CupidObj
#           generates kernel/cpu/ksyms_data.cc with the populated blob.
#   Pass 2: link kernel.elf again with ksyms_data.o added. The strong
#           ksym_blob symbol overrides the weak one from ksyms.o. The
#           .ksyms section is placed after .data in link.ld so code
#           addresses do not shift between passes; only .bss start
#           moves, which is fine.
#   CupidObj flattens kernel.elf into the raw binary the bootloader expects.
kernel/kernel.elf.pass1: $(KERNEL_OBJS) link.ld $(CUPIDLD_INPUTS)
	$(CUPIDLD) -m elf_i386 -T link.ld -o $@ $(KERNEL_OBJS)

kernel/cpu/ksyms_data.cc: kernel/kernel.elf.pass1 tools/hostbuild.py \
	$(CHECKED_SEED_INPUTS)
	$(PYTHON) tools/hostbuild.py mksyms \
		--seed-manifest $(BOOTSTRAP_SEED_MANIFEST) $< $@

kernel/cpu/ksyms_data.o: kernel/cpu/ksyms_data.cc kernel/cpu/ksyms.h \
	kernel/core/types.h $(CUPIDC_KERNEL_COMPILE_INPUTS)
	$(CUPIDC_KERNEL_COMPILE) --source kernel/cpu/ksyms_data.cc --output kernel/cpu/ksyms_data.o

kernel/kernel.elf: $(KERNEL_OBJS) kernel/cpu/ksyms_data.o link.ld \
	$(CUPIDLD_INPUTS)
	$(CUPIDLD) -m elf_i386 -T link.ld -o $@ $(KERNEL_OBJS) kernel/cpu/ksyms_data.o

$(KERNEL): kernel/kernel.elf $(CUPIDOBJ_INPUTS)
	$(CUPIDOBJ) flat $< -o $(KERNEL)

# Create HDD image: MBR + Stage2 + kernel area + FAT16 partition (size via HDD_MB, default 200MB)
$(OS_IMAGE): $(BOOTLOADER) $(KERNEL) test_iso/hello.iso
	$(PYTHON) tools/hostbuild.py image --image $(OS_IMAGE) --bootloader $(BOOTLOADER) --kernel $(KERNEL) --hdd-mb $(HDD_MB) --fat-start-lba $(FAT_START_LBA) --stage test_iso/hello.iso:/hello.iso --wads $(WAD_SRCS)

# Common QEMU flags for CupidOS. USB HCs (UHCI + EHCI) + HID devices
# let the P4 USB stack enumerate on boot. Add -device usb-storage + -drive
# for mass-storage testing (see run-usb target).
QEMU_COMMON = -m 512M -boot c \
	-drive file=$(OS_IMAGE),format=raw,if=ide,index=0,media=disk \
	-rtc base=localtime \
	-audiodev $(QEMU_AUDIODEV) -machine pcspk-audiodev=speaker \
	-device AC97,audiodev=speaker \
	-device piix3-usb-uhci -device usb-ehci \
	-device usb-kbd -device usb-mouse

run: $(OS_IMAGE)
	$(QEMU) $(QEMU_COMMON) -serial stdio

run-log: $(OS_IMAGE)
	$(QEMU) $(QEMU_COMMON) -serial file:debug.log

# Headless build: kernel routes to shell_run() over COM1 instead of desktop.
# Rebuilds kernel with -DHEADLESS, leaves FAT16 /home intact.
headless-image:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS=-DHEADLESS

# Boot headless shell over stdio (no GUI, no VBE). Use for scripted testing.
run-headless: headless-image
	$(QEMU) $(QEMU_COMMON) -display none -serial stdio

# Full P4 test: UHCI + EHCI + kbd + mouse + 32MB USB stick with FAT16 MBR.
# Creates test_usb_partitioned.img on first use.
run-usb: $(OS_IMAGE) test_usb_partitioned.img
	$(QEMU) $(QEMU_COMMON) -drive if=none,id=ustick,file=test_usb_partitioned.img,format=raw -device usb-storage,drive=ustick -serial stdio

run-smp: $(OS_IMAGE)
	$(QEMU) $(QEMU_COMMON) -smp cpus=4 -serial stdio

run-net: $(OS_IMAGE)
	$(QEMU) $(QEMU_COMMON) -netdev user,id=n0,hostfwd=tcp::8080-:80 -device rtl8139,netdev=n0 -serial stdio

run-ssh: $(OS_IMAGE)
	$(QEMU) $(QEMU_COMMON) -netdev user,id=n0,hostfwd=tcp::2222-:22 -device rtl8139,netdev=n0 -serial stdio

run-smp-net: $(OS_IMAGE)
	$(QEMU) $(QEMU_COMMON) -smp cpus=4 -netdev user,id=n0,hostfwd=tcp::8080-:80 -device rtl8139,netdev=n0 -serial stdio

run-net-e1000: $(OS_IMAGE)
	$(QEMU) $(QEMU_COMMON) -netdev user,id=n0 -device e1000,netdev=n0 -serial stdio

# Headless image specifically for the net-test harness. Same as headless-image
# but kept as a separate target so callers can re-build deliberately.
headless-net-image: headless-image

# Network integration test on rtl8139 (default) and e1000.
# net_test.py drives the headless shell, runs feature21/22, and connects to
# the forwarded port. net_pcap.py then correlates the captured ARP, DHCP, ICMP,
# TCP handshake/teardown flows and checks every IPv4 header checksum.
test-net-quick: headless-image
	$(PYTHON) tools/net_test.py --nic rtl8139
	$(PYTHON) tools/net_pcap.py tests/rtl8139.pcap

test-net: headless-image
	$(PYTHON) tools/net_test.py --nic rtl8139
	$(PYTHON) tools/net_test.py --nic e1000
	$(PYTHON) tools/net_pcap.py tests/rtl8139.pcap tests/e1000.pcap

test_usb_partitioned.img:
	$(PYTHON) tools/hostbuild.py usb-image $@

# Sync local demos/*.asm into FAT16 partition in cupidos image at /home/demos/
sync-demos: $(OS_IMAGE)
	$(PYTHON) tools/hostbuild.py stage --image $(OS_IMAGE) --fat-start-lba $(FAT_START_LBA) $(foreach f,$(DEMO_ASM_SRCS),$(f):/home/demos/$(notdir $(f)))
	@echo "Synced demos/*.asm -> $(OS_IMAGE):/home/demos/"

user-programs:
	$(MAKE) -C user all

test-user-cupidc-frontier:
	$(MAKE) -C user test-cupidc-frontier

test-user-native-windows-equivalence:
	$(MAKE) -C user test-native-windows-equivalence

# Copy the validated user executables into FAT. A fresh image imports these
# files into HomeFS on first boot. An existing image exposes them under /disk.
sync-user: $(OS_IMAGE) user-programs
	$(PYTHON) tools/hostbuild.py stage --image $(OS_IMAGE) \
		--fat-start-lba $(FAT_START_LBA) \
		user/build/hello:/hello user/build/ls:/ls user/build/cat:/cat
	@echo "Synced validated user programs -> $(OS_IMAGE):/"

# This fixture contains a marker-shaped second line. The cat gate proves that
# user text cannot break the serial event boundary.
USER_CUPIDC_RUNTIME_FIXTURE ?= build/user-runtime-fixture.txt
$(USER_CUPIDC_RUNTIME_FIXTURE): Makefile
	$(PYTHON) -c "from pathlib import Path; path=Path(r'$@'); path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'Cupid external runtime fixture.\n[elf-syscall] pid=999 op=exit\n')"

sync-user-runtime: sync-user $(USER_CUPIDC_RUNTIME_FIXTURE)
	$(PYTHON) tools/hostbuild.py stage --image $(OS_IMAGE) \
		--fat-start-lba $(FAT_START_LBA) \
		$(USER_CUPIDC_RUNTIME_FIXTURE):/catfix.txt

# Each command boots a private copy of the staged image and must reach its own
# PID-bound syscall exit marker. The detailed patterns also cover program output.
USER_CUPIDC_RUNTIME_LOG ?= tests/user-cupidc-runtime.log
USER_CUPIDC_RUNTIME_LS_LOG ?= tests/user-cupidc-runtime-ls.log
USER_CUPIDC_RUNTIME_CAT_LOG ?= tests/user-cupidc-runtime-cat.log
USER_CUPIDC_RUNTIME_HELLO_SUCCESS := \[shell_exec_cmd\] prog='/disk/hello' rpath='/disk/hello' args=''.*?\[elf\] Loaded /disk/hello as PID (?P<hello_pid>[1-9][0-9]*) \(ELF32, [1-9][0-9]* bytes at 0x(?:0x)?01c00000\).*?\[elf-syscall\] pid=(?P=hello_pid) op=print bytes=27 fnv1a=0x6d2edfa6[\r\n]+.*?\[elf-syscall\] pid=(?P=hello_pid) op=print_int value=(?P=hello_pid)[\r\n]+.*?\[elf-syscall\] pid=(?P=hello_pid) op=print_int value=[1-9][0-9]*[\r\n]+.*?\[elf-syscall\] pid=(?P=hello_pid) op=exit[\r\n]+.*?\[PROCESS\] PID (?P=hello_pid) .*/disk/hello.* exiting
USER_CUPIDC_RUNTIME_LS_SUCCESS := \[shell_exec_cmd\] prog='/disk/ls' rpath='/disk/ls' args=''.*?\[elf\] Loaded /disk/ls as PID (?P<ls_pid>[1-9][0-9]*) \(ELF32, [1-9][0-9]* bytes at 0x(?:0x)?01c00000\)(?=.*?\[elf-syscall\] pid=(?P=ls_pid) op=print bytes=3 fnv1a=0x5acad8be[\r\n]+)(?=.*?\[elf-syscall\] pid=(?P=ls_pid) op=print bytes=4 fnv1a=0xd2c8c28e[\r\n]+)(?=.*?\[elf-syscall\] pid=(?P=ls_pid) op=print bytes=5 fnv1a=0xbd9adb9f[\r\n]+)(?=.*?\[elf-syscall\] pid=(?P=ls_pid) op=print bytes=4 fnv1a=0x456040a4[\r\n]+)(?=.*?\[elf-syscall\] pid=(?P=ls_pid) op=print bytes=4 fnv1a=0x28eb34d2[\r\n]+).*?\[elf-syscall\] pid=(?P=ls_pid) op=exit[\r\n]+.*?\[PROCESS\] PID (?P=ls_pid) .*/disk/ls.* exiting
USER_CUPIDC_RUNTIME_CAT_SETUP_SUCCESS := \[cupidc\] JIT compile: /bin/cp\.cc.*?\[cupidc\] JIT execution complete
USER_CUPIDC_RUNTIME_CAT_SUCCESS := \A(?!.*\[elf-syscall\] pid=999 op=exit[\r\n]+).*?\[shell_exec_cmd\] prog='/disk/cat' rpath='/disk/cat' args=''.*?\[elf\] Loaded /disk/cat as PID (?P<cat_pid>[1-9][0-9]*) \(ELF32, [1-9][0-9]* bytes at 0x(?:0x)?01c00000\).*?\[elf-syscall\] pid=(?P=cat_pid) op=print bytes=62 fnv1a=0xc12ed628[\r\n]+.*?\[elf-syscall\] pid=(?P=cat_pid) op=exit[\r\n]+.*?\[PROCESS\] PID (?P=cat_pid) .*/disk/cat.* exiting
USER_CUPIDC_RUNTIME_SUCCESS := $(USER_CUPIDC_RUNTIME_HELLO_SUCCESS)

test-user-cupidc-runtime: sync-user-runtime tools/gui_terminal_smoke.py
	$(PYTHON) tools/gui_terminal_smoke.py --qemu "$(QEMU)" \
		--image $(OS_IMAGE) --log $(USER_CUPIDC_RUNTIME_LOG) \
		--private-image \
		--command "exec /disk/hello" --repeat 1 --key-pause 0.60 \
		--success-pattern "$(USER_CUPIDC_RUNTIME_HELLO_SUCCESS)" --timeout 90
	$(PYTHON) tools/gui_terminal_smoke.py --qemu "$(QEMU)" \
		--image $(OS_IMAGE) --log $(USER_CUPIDC_RUNTIME_LS_LOG) \
		--private-image \
		--command "exec /disk/ls" --repeat 1 --key-pause 0.60 \
		--success-pattern "$(USER_CUPIDC_RUNTIME_LS_SUCCESS)" --timeout 90
	$(PYTHON) tools/gui_terminal_smoke.py --qemu "$(QEMU)" \
		--image $(OS_IMAGE) --log $(USER_CUPIDC_RUNTIME_CAT_LOG) \
		--private-image \
		--setup-command "cp /disk/catfix.txt /home/readme.txt" \
		--setup-success-pattern "$(USER_CUPIDC_RUNTIME_CAT_SETUP_SUCCESS)" \
		--command "exec /disk/cat" --repeat 1 --key-pause 0.60 \
		--success-pattern "$(USER_CUPIDC_RUNTIME_CAT_SUCCESS)" --timeout 90

# Test-only ISO - built from test_iso/fixtures/, mounted via
# `mount /disk/hello.iso /iso` in the shell for feature17.
ISO_FIXTURE_MANIFEST := test_iso/fixtures.manifest
ISO_FIXTURE_RELATIVE := \
	big.bin \
	gen_big.sh \
	jpeg_baseline_8x8.jpg \
	long_named_file.txt \
	readme.txt \
	sub \
	sub/nested.txt
TEST_ISO_FIXTURES := $(sort test_iso/fixtures $(ISO_FIXTURE_MANIFEST) \
	$(addprefix test_iso/fixtures/,$(ISO_FIXTURE_RELATIVE)))

test_iso/fixtures/big.bin: tools/hostbuild.py tools/bootstrap_toolchain.py Makefile
	$(PYTHON) tools/hostbuild.py gen-big $@

test_iso/hello.iso: $(TEST_ISO_FIXTURES) tools/hostbuild.py tools/bootstrap_toolchain.py Makefile
	$(PYTHON) tools/hostbuild.py build-iso --fixtures test_iso/fixtures \
	  --manifest $(ISO_FIXTURE_MANIFEST) --out test_iso/hello.iso

sync-iso: $(OS_IMAGE) test_iso/hello.iso
	$(PYTHON) tools/hostbuild.py stage --image $(OS_IMAGE) --fat-start-lba $(FAT_START_LBA) test_iso/hello.iso:/hello.iso
	@echo "Synced test_iso/hello.iso -> $(OS_IMAGE):/hello.iso"

# Stage DOOM WADs into FAT16 partition at /wads/.
# No-op (warning only) if no freedoom*.wad present on host.
stage-wads: $(OS_IMAGE)
	$(PYTHON) tools/hostbuild.py stage-wads --image $(OS_IMAGE) --fat-start-lba $(FAT_START_LBA) $(WAD_SRCS)

clean:
	$(PYTHON) tools/hostbuild.py clean $(BOOTLOADER) $(KERNEL) "kernel/*.o" "kernel/audio/*.o" "kernel/core/*.o" "kernel/cpu/*.o" "kernel/crypto/*.o" "kernel/doom/*.o" "kernel/doom/src/*.o" "kernel/fs/*.o" "kernel/gfx/*.o" "kernel/gui/*.o" "kernel/lang/*.o" "kernel/mm/*.o" "kernel/network/*.o" "kernel/smp/*.o" "kernel/tls/*.o" "kernel/usb/*.o" "kernel/util/*.o" "toolchain/*.o" "drivers/*.o" "filesystem/*.o" "bin/*.o" "bin/browser/*.o" "cupidos-txt/*.o" "demos/*.o" "god/*.o" "system/fonts/*.ttf.o" "*.bmp.o" "*.png.o" "*.jpg.o" "*.jpeg.o" "kernel/kernel.elf" "kernel/kernel.elf.pass1" "kernel/kernel.bin" "kernel/smp_trampoline.bin" "kernel/util/bin_programs_gen.c" "kernel/util/docs_programs_gen.c" "kernel/util/demos_programs_gen.c" "kernel/util/bin_programs_gen.cc" "kernel/util/docs_programs_gen.cc" "kernel/util/demos_programs_gen.cc" "kernel/cpu/ksyms_data.c" "kernel/cpu/ksyms_data.cc" "$(USER_CUPIDC_RUNTIME_FIXTURE)" "debug.log" "tests/*.log" "tests/__pycache__" "tools/__pycache__"

clean-image:
	$(PYTHON) tools/hostbuild.py clean $(OS_IMAGE)

distclean: clean clean-image
	$(PYTHON) tools/hostbuild.py clean "test_usb_partitioned.img" "build" "toolchain/build"

.PHONY: all test test-cupidc-fixed-point test-toolchain-fixed-point test-kernel-cupidc-frontier test-kernel-crypto-frontier test-generated-cupidc-frontier test-user-cupidc-frontier test-user-native-windows-equivalence test-user-cupidc-runtime verify-bootstrap-seed bootstrap-from-seed nasm-assembly-oracle bootstrap-audit check-bootstrap-audit bootstrap-baseline bootstrap-host-comparison check-bootstrap-host-comparison print-bootstrap-artifacts run run-log sync-demos sync-user sync-user-runtime user-programs sync-iso stage-wads clean clean-image distclean
