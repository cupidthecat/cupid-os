#include "cupidbuild.h"
#include "ctool.h"
#include "ctool_host.h"
#include "cupidbuild_host.h"
#include "elf32.h"
#if defined(_WIN32)
#include "pe32_impl.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CUPIDBUILD_PATH_BYTES 8192u
#define CUPIDBUILD_MANIFEST_BYTES 1048576u
#define CUPIDBUILD_TOOL_BYTES 67108864u
#define CUPIDBUILD_JSON_TOKENS 2048u
#define CUPIDBUILD_SEED_ARTIFACTS 6u
#define CUPIDBUILD_CODE_INPUTS 500u
#define CUPIDBUILD_COMPILE_INPUTS 90u
#define CUPIDBUILD_PROFILE_SNAPSHOT_BYTES 4194304u
#define CUPIDBUILD_PROFILE_TRANSACTION_INPUTS 512u
#define CUPIDBUILD_PROFILE_OUTPUT "build/bootstrap/doom-cupidc-inputs.json"
typedef struct {
  char file[CUPIDBUILD_PATH_BYTES];
  char sha256[65];
  size_t size;
} cupidbuild_seed_artifact_t;

typedef struct {
  char manifest_path[CUPIDBUILD_PATH_BYTES];
  char directory[CUPIDBUILD_PATH_BYTES];
  cupidbuild_seed_artifact_t artifacts[CUPIDBUILD_SEED_ARTIFACTS];
  const char *frozen_tools[CUPIDBUILD_SEED_ARTIFACTS];
  cupidbuild_host_snapshot_t tool_snapshots[CUPIDBUILD_SEED_ARTIFACTS];
  const char *expected_files[CUPIDBUILD_SEED_ARTIFACTS];
  unsigned char *manifest;
  size_t manifest_size;
  size_t artifact_count;
  int current_windows_plan;
} cupidbuild_seed_capture_t;

static const char *const cupidbuild_seed_names[CUPIDBUILD_SEED_ARTIFACTS] = {
    "CupidASM", "CupidC", "CupidDis", "CupidLD", "CupidObj", "CupidBuild"};

typedef enum {
  CUPIDBUILD_JSON_OBJECT,
  CUPIDBUILD_JSON_ARRAY,
  CUPIDBUILD_JSON_STRING,
  CUPIDBUILD_JSON_PRIMITIVE
} cupidbuild_json_type_t;

typedef struct {
  cupidbuild_json_type_t type;
  size_t start;
  size_t end;
  size_t count;
  size_t parent;
} cupidbuild_json_token_t;

typedef struct {
  const unsigned char *bytes;
  size_t size;
  size_t position;
  cupidbuild_json_token_t *tokens;
  size_t token_count;
} cupidbuild_json_reader_t;

typedef struct {
  const char *name;
  const char *path;
  int gnu_extensions;
} cupidbuild_seed_source_t;

typedef struct {
  unsigned int address;
  size_t order;
  size_t name_start;
  size_t name_size;
} cupidbuild_ksyms_symbol_t;

typedef struct {
  unsigned char *bytes;
  size_t size;
  size_t capacity;
} cupidbuild_ksyms_buffer_t;

typedef struct {
  unsigned int address;
  unsigned int order;
  ctool_bytes_t contents;
} cupidbuild_flat_region_t;

typedef struct {
  unsigned char *bytes;
  size_t size;
  size_t capacity;
} cupidbuild_profile_buffer_t;

typedef struct {
  const char *path;
  const char *frozen_path;
  size_t size;
  unsigned char sha256[32];
} cupidbuild_profile_header_t;

typedef struct {
  cupidbuild_host_path_list_t headers;
  cupidbuild_host_path_list_t sources;
} cupidbuild_profile_membership_t;

typedef struct {
  const char *source;
  const char *inputs[CUPIDBUILD_COMPILE_INPUTS];
  size_t count;
} cupidbuild_compile_closure_t;

typedef struct {
  const char *path;
  const char *frozen_path;
  cupidbuild_host_snapshot_t snapshot;
} cupidbuild_compile_input_t;

static const cupidbuild_compile_closure_t cupidbuild_compile_closures[] = {
    {"drivers/ata.cc", {
        "drivers/ata.cc",
        "drivers/ata.h",
        "kernel/core/debug.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/blockdev.h"
    }, 8u},
    {"drivers/e1000.cc", {
        "drivers/e1000.cc",
        "drivers/pci.h",
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/network/net_if.h"
    }, 8u},
    {"drivers/keyboard.cc", {
        "drivers/keyboard.cc",
        "drivers/keyboard.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/vga.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.h",
        "kernel/lang/shell.h",
        "kernel/util/calendar.h"
    }, 15u},
    {"drivers/mouse.cc", {
        "drivers/mouse.cc",
        "drivers/mouse.h",
        "drivers/serial.h",
        "drivers/vga.h",
        "kernel/core/ports.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/pic.h",
        "kernel/gfx/graphics.h"
    }, 10u},
    {"drivers/pci.cc", {
        "drivers/pci.cc",
        "drivers/pci.h",
        "drivers/serial.h",
        "kernel/core/ports.h",
        "kernel/core/types.h"
    }, 5u},
    {"drivers/pit.cc", {
        "drivers/pit.cc",
        "drivers/pit.h",
        "kernel/core/ports.h",
        "kernel/core/types.h"
    }, 4u},
    {"drivers/rtc.cc", {
        "drivers/rtc.cc",
        "drivers/rtc.h",
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h"
    }, 7u},
    {"drivers/rtl8139.cc", {
        "drivers/pci.h",
        "drivers/rtl8139.cc",
        "drivers/serial.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/network/net_if.h"
    }, 9u},
    {"drivers/serial.cc", {
        "drivers/serial.cc",
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/smp/bkl.h"
    }, 9u},
    {"drivers/speaker.cc", {
        "drivers/pit.h",
        "drivers/speaker.cc",
        "drivers/speaker.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h"
    }, 8u},
    {"drivers/timer.cc", {
        "drivers/keyboard.h",
        "drivers/timer.cc",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/math.h"
    }, 9u},
    {"drivers/vga.cc", {
        "drivers/timer.h",
        "drivers/vga.cc",
        "drivers/vga.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/simd.h",
        "kernel/mm/memory.h"
    }, 10u},
    {"kernel/audio/ac97.cc", {
        "drivers/pci.h",
        "drivers/serial.h",
        "kernel/audio/ac97.cc",
        "kernel/audio/ac97.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h"
    }, 10u},
    {"kernel/audio/memio.cc", {
        "kernel/audio/memio.cc",
        "kernel/audio/memio.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/mm/memory.h"
    }, 5u},
    {"kernel/audio/midiopl.cc", {
        "drivers/serial.h",
        "kernel/audio/midiopl.cc",
        "kernel/audio/midiopl.h",
        "kernel/audio/nuked_opl3.h",
        "kernel/core/string.h",
        "kernel/core/types.h"
    }, 6u},
    {"kernel/audio/mixer.cc", {
        "kernel/audio/mixer.cc",
        "kernel/audio/mixer.h",
        "kernel/core/types.h"
    }, 3u},
    {"kernel/audio/mus2midi.cc", {
        "kernel/audio/memio.h",
        "kernel/audio/mus2midi.cc",
        "kernel/audio/mus2midi.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/mm/memory.h"
    }, 6u},
    {"kernel/audio/nuked_opl3.cc", {
        "kernel/audio/nuked_opl3.cc",
        "kernel/audio/nuked_opl3.h",
        "kernel/core/string.h",
        "kernel/core/types.h"
    }, 4u},
    {"kernel/audio/opl_smoke.cc", {
        "drivers/serial.h",
        "kernel/audio/ac97.h",
        "kernel/audio/mixer.h",
        "kernel/audio/nuked_opl3.h",
        "kernel/audio/opl_smoke.cc",
        "kernel/audio/opl_smoke.h",
        "kernel/core/types.h"
    }, 7u},
    {"kernel/core/app_launch.cc", {
        "kernel/core/app_launch.cc",
        "kernel/core/app_launch.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gui/ctxt_image_worker.h",
        "kernel/gui/gui.h",
        "kernel/gui/terminal_app.h",
        "kernel/lang/cupidc.h",
        "kernel/lang/dis.h",
        "kernel/lang/shell.h",
        "toolchain/ctool.h",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 16u},
    {"kernel/core/kernel.cc", {
        "drivers/ata.h",
        "drivers/keyboard.h",
        "drivers/mouse.h",
        "drivers/pci.h",
        "drivers/pit.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/speaker.h",
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/core/debug.h",
        "kernel/core/kernel.cc",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/ports.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/syscall.h",
        "kernel/core/types.h",
        "kernel/cpu/fpu.h",
        "kernel/cpu/idt.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/pic.h",
        "kernel/cpu/simd.h",
        "kernel/crypto/csprng.h",
        "kernel/fs/blockcache.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/devfs.h",
        "kernel/fs/fat16.h",
        "kernel/fs/fat16_vfs.h",
        "kernel/fs/fs.h",
        "kernel/fs/homefs.h",
        "kernel/fs/iso9660_vfs.h",
        "kernel/fs/ramfs.h",
        "kernel/fs/vfs.h",
        "kernel/gfx/fontsys.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_assets.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/clipboard.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.h",
        "kernel/gui/gui_containers.h",
        "kernel/gui/gui_events.h",
        "kernel/gui/gui_menus.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/gui_widgets.h",
        "kernel/gui/ui.h",
        "kernel/lang/as.h",
        "kernel/lang/ctool_kernel.h",
        "kernel/lang/exec.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/network/net_if.h",
        "kernel/smp/bkl.h",
        "kernel/smp/ioapic.h",
        "kernel/smp/lapic.h",
        "kernel/smp/percpu.h",
        "kernel/smp/smp.h",
        "kernel/tls/tls_selftest.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
        "kernel/util/calendar.h",
        "toolchain/ctool.h"
    }, 65u},
    {"kernel/core/panic.cc", {
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.cc",
        "kernel/core/panic.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/fpu.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/ksyms.h",
        "kernel/cpu/math.h",
        "kernel/mm/memory.h"
    }, 12u},
    {"kernel/core/process.cc", {
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/debug.h",
        "kernel/core/kernel.h",
        "kernel/core/process.cc",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/simd.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_icons.h",
        "kernel/gui/gui.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/smp/bkl.h",
        "kernel/smp/percpu.h",
        "kernel/smp/smp.h"
    }, 18u},
    {"kernel/core/string.cc", {
        "kernel/core/string.cc",
        "kernel/core/string.h",
        "kernel/core/types.h"
    }, 3u},
    {"kernel/core/syscall.cc", {
        "drivers/ata.h",
        "drivers/pci.h",
        "drivers/pit.h",
        "drivers/serial.h",
        "drivers/speaker.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/syscall.cc",
        "kernel/core/syscall.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/vfs.h",
        "kernel/fs/vfs_helpers.h",
        "kernel/lang/exec.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/network/arp.h",
        "kernel/network/dns.h",
        "kernel/network/icmp.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h",
        "kernel/network/socket.h",
        "kernel/network/udp.h",
        "kernel/smp/bkl.h",
        "kernel/smp/lapic.h"
    }, 29u},
    {"kernel/cpu/fpu.cc", {
        "drivers/serial.h",
        "kernel/core/panic.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/fpu.cc",
        "kernel/cpu/fpu.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/libm.h"
    }, 8u},
    {"kernel/cpu/idt.cc", {
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/types.h",
        "kernel/cpu/idt.cc",
        "kernel/cpu/idt.h",
        "kernel/cpu/isr.h"
    }, 7u},
    {"kernel/cpu/irq.cc", {
        "kernel/core/types.h",
        "kernel/cpu/irq.cc",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/math.h",
        "kernel/cpu/pic.h",
        "kernel/smp/bkl.h",
        "kernel/smp/ioapic.h",
        "kernel/smp/lapic.h"
    }, 9u},
    {"kernel/cpu/ksyms.cc", {
        "kernel/core/types.h",
        "kernel/cpu/ksyms.cc",
        "kernel/cpu/ksyms.h"
    }, 3u},
    {"kernel/cpu/ksyms_data.cc", {
        "kernel/core/types.h",
        "kernel/cpu/ksyms.h",
        "kernel/cpu/ksyms_data.cc"
    }, 3u},
    {"kernel/cpu/libm.cc", {
        "kernel/core/types.h",
        "kernel/cpu/libm.cc",
        "kernel/cpu/libm.h"
    }, 3u},
    {"kernel/cpu/math.cc", {
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/math.cc",
        "kernel/cpu/math.h"
    }, 5u},
    {"kernel/cpu/pic.cc", {
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/pic.cc",
        "kernel/cpu/pic.h"
    }, 5u},
    {"kernel/cpu/simd.cc", {
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/simd.cc",
        "kernel/cpu/simd.h"
    }, 8u},
    {"kernel/crypto/aes.cc", {
        "kernel/core/types.h",
        "kernel/crypto/aes.cc",
        "kernel/crypto/aes.h"
    }, 3u},
    {"kernel/crypto/aes_gcm.cc", {
        "kernel/core/types.h",
        "kernel/crypto/aes.h",
        "kernel/crypto/aes_gcm.cc",
        "kernel/crypto/aes_gcm.h",
        "kernel/crypto/ct.h"
    }, 5u},
    {"kernel/crypto/asn1.cc", {
        "kernel/core/types.h",
        "kernel/crypto/asn1.cc",
        "kernel/crypto/asn1.h"
    }, 3u},
    {"kernel/crypto/bigint.cc", {
        "kernel/core/types.h",
        "kernel/crypto/bigint.cc",
        "kernel/crypto/bigint.h"
    }, 3u},
    {"kernel/crypto/chacha20.cc", {
        "kernel/core/types.h",
        "kernel/crypto/chacha20.cc",
        "kernel/crypto/chacha20.h"
    }, 3u},
    {"kernel/crypto/chacha20poly1305.cc", {
        "kernel/core/types.h",
        "kernel/crypto/chacha20.h",
        "kernel/crypto/chacha20poly1305.cc",
        "kernel/crypto/chacha20poly1305.h",
        "kernel/crypto/ct.h",
        "kernel/crypto/poly1305.h"
    }, 6u},
    {"kernel/crypto/csprng.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/crypto/chacha20.h",
        "kernel/crypto/csprng.cc",
        "kernel/crypto/csprng.h"
    }, 5u},
    {"kernel/crypto/ct.cc", {
        "kernel/core/types.h",
        "kernel/crypto/ct.cc",
        "kernel/crypto/ct.h"
    }, 3u},
    {"kernel/crypto/ecdsa.cc", {
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/crypto/ecdsa.cc",
        "kernel/crypto/ecdsa.h",
        "kernel/crypto/hmac.h",
        "kernel/crypto/p256.h",
        "kernel/crypto/sha256.h"
    }, 7u},
    {"kernel/crypto/ed25519.cc", {
        "kernel/core/types.h",
        "kernel/crypto/ed25519.cc",
        "kernel/crypto/ed25519.h",
        "kernel/crypto/sha512.h"
    }, 4u},
    {"kernel/crypto/hkdf.cc", {
        "kernel/core/types.h",
        "kernel/crypto/hkdf.cc",
        "kernel/crypto/hkdf.h",
        "kernel/crypto/hmac.h",
        "kernel/crypto/sha256.h"
    }, 5u},
    {"kernel/crypto/hmac.cc", {
        "kernel/core/types.h",
        "kernel/crypto/hmac.cc",
        "kernel/crypto/hmac.h",
        "kernel/crypto/sha256.h"
    }, 4u},
    {"kernel/crypto/p256.cc", {
        "kernel/core/types.h",
        "kernel/crypto/p256.cc",
        "kernel/crypto/p256.h"
    }, 3u},
    {"kernel/crypto/poly1305.cc", {
        "kernel/core/types.h",
        "kernel/crypto/ct.h",
        "kernel/crypto/poly1305.cc",
        "kernel/crypto/poly1305.h"
    }, 4u},
    {"kernel/crypto/rsa.cc", {
        "kernel/core/types.h",
        "kernel/crypto/bigint.h",
        "kernel/crypto/ct.h",
        "kernel/crypto/rsa.cc",
        "kernel/crypto/rsa.h",
        "kernel/crypto/sha256.h"
    }, 6u},
    {"kernel/crypto/sha256.cc", {
        "kernel/core/types.h",
        "kernel/crypto/sha256.cc",
        "kernel/crypto/sha256.h"
    }, 3u},
    {"kernel/crypto/sha512.cc", {
        "kernel/core/types.h",
        "kernel/crypto/sha512.cc",
        "kernel/crypto/sha512.h"
    }, 3u},
    {"kernel/crypto/x25519.cc", {
        "kernel/core/types.h",
        "kernel/crypto/x25519.cc",
        "kernel/crypto/x25519.h"
    }, 3u},
    {"kernel/crypto/x509.cc", {
        "kernel/core/types.h",
        "kernel/crypto/asn1.h",
        "kernel/crypto/x509.cc",
        "kernel/crypto/x509.h"
    }, 4u},
    {"kernel/crypto/x509_chain.cc", {
        "kernel/core/types.h",
        "kernel/crypto/asn1.h",
        "kernel/crypto/ecdsa.h",
        "kernel/crypto/p256.h",
        "kernel/crypto/rsa.h",
        "kernel/crypto/sha256.h",
        "kernel/crypto/sha512.h",
        "kernel/crypto/x509.h",
        "kernel/crypto/x509_chain.cc",
        "kernel/crypto/x509_chain.h"
    }, 10u},
    {"kernel/fs/blockcache.cc", {
        "kernel/core/debug.h",
        "kernel/core/kernel.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/blockcache.cc",
        "kernel/fs/blockcache.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/homefs.h",
        "kernel/fs/vfs.h",
        "kernel/mm/memory.h",
        "kernel/smp/bkl.h"
    }, 12u},
    {"kernel/fs/blockdev.cc", {
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/blockdev.cc",
        "kernel/fs/blockdev.h"
    }, 5u},
    {"kernel/fs/devfs.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/crypto/csprng.h",
        "kernel/fs/devfs.cc",
        "kernel/fs/devfs.h",
        "kernel/fs/vfs.h",
        "kernel/mm/memory.h"
    }, 8u},
    {"kernel/fs/fat16.cc", {
        "drivers/serial.h",
        "kernel/core/debug.h",
        "kernel/core/kernel.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/blockcache.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/fat16.cc",
        "kernel/fs/fat16.h",
        "kernel/fs/fat16_control.h"
    }, 11u},
    {"kernel/fs/fat16_vfs.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/fat16.h",
        "kernel/fs/fat16_control.h",
        "kernel/fs/fat16_vfs.cc",
        "kernel/fs/fat16_vfs.h",
        "kernel/fs/vfs.h",
        "kernel/mm/memory.h"
    }, 10u},
    {"kernel/fs/fs.cc", {
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/fs.cc",
        "kernel/fs/fs.h"
    }, 4u},
    {"kernel/fs/homefs.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/blockcache.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/fat16.h",
        "kernel/fs/fat16_control.h",
        "kernel/fs/homefs.cc",
        "kernel/fs/homefs.h",
        "kernel/fs/vfs.h",
        "kernel/mm/memory.h"
    }, 12u},
    {"kernel/fs/iso9660.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/iso9660.cc",
        "kernel/fs/iso9660.h",
        "kernel/fs/vfs.h"
    }, 7u},
    {"kernel/fs/iso9660_vfs.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/iso9660.h",
        "kernel/fs/iso9660_vfs.cc",
        "kernel/fs/iso9660_vfs.h",
        "kernel/fs/loopdev.h",
        "kernel/fs/vfs.h",
        "kernel/mm/memory.h"
    }, 10u},
    {"kernel/fs/loopdev.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/loopdev.cc",
        "kernel/fs/loopdev.h",
        "kernel/fs/vfs.h",
        "kernel/mm/memory.h"
    }, 8u},
    {"kernel/fs/ramfs.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/ramfs.cc",
        "kernel/fs/ramfs.h",
        "kernel/fs/vfs.h",
        "kernel/mm/memory.h"
    }, 7u},
    {"kernel/fs/vfs.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/vfs.cc",
        "kernel/fs/vfs.h",
        "kernel/mm/memory.h"
    }, 6u},
    {"kernel/fs/vfs_helpers.cc", {
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/vfs.h",
        "kernel/fs/vfs_helpers.cc",
        "kernel/fs/vfs_helpers.h"
    }, 5u},
    {"kernel/gfx/bmp.cc", {
        "drivers/serial.h",
        "drivers/vga.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/vfs.h",
        "kernel/gfx/bmp.cc",
        "kernel/gfx/bmp.h",
        "kernel/gfx/gfx2d.h",
        "kernel/mm/memory.h"
    }, 9u},
    {"kernel/gfx/deflate.cc", {
        "kernel/core/types.h",
        "kernel/gfx/deflate.cc",
        "kernel/gfx/deflate.h"
    }, 3u},
    {"kernel/gfx/font_8x8.cc", {
        "kernel/core/types.h",
        "kernel/gfx/font_8x8.cc",
        "kernel/gfx/font_8x8.h"
    }, 3u},
    {"kernel/gfx/fontsys.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/vfs.h",
        "kernel/fs/vfs_helpers.h",
        "kernel/gfx/fontsys.cc",
        "kernel/gfx/fontsys.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/glyph_raster.h",
        "kernel/gfx/ttf.h",
        "kernel/mm/memory.h"
    }, 11u},
    {"kernel/gfx/gfx2d.cc", {
        "drivers/keyboard.h",
        "drivers/mouse.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/simd.h",
        "kernel/fs/vfs.h",
        "kernel/gfx/font_8x8.h",
        "kernel/gfx/fontsys.h",
        "kernel/gfx/gfx2d.cc",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_handoff.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.h",
        "kernel/gui/ui.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/util/calendar.h"
    }, 26u},
    {"kernel/gfx/gfx2d_assets.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/vfs.h",
        "kernel/fs/vfs_helpers.h",
        "kernel/gfx/bmp.h",
        "kernel/gfx/font_8x8.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_assets.cc",
        "kernel/gfx/gfx2d_assets.h",
        "kernel/gfx/jpeg.h",
        "kernel/gfx/png.h",
        "kernel/mm/memory.h"
    }, 13u},
    {"kernel/gfx/gfx2d_effects.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/simd.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_effects.cc",
        "kernel/gfx/gfx2d_effects.h",
        "kernel/mm/memory.h"
    }, 8u},
    {"kernel/gfx/gfx2d_icons.cc", {
        "drivers/serial.h",
        "kernel/core/app_launch.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/vfs.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_icons.cc",
        "kernel/gfx/gfx2d_icons.h",
        "kernel/mm/memory.h"
    }, 10u},
    {"kernel/gfx/gfx2d_transform.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/math.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_assets.h",
        "kernel/gfx/gfx2d_transform.cc",
        "kernel/gfx/gfx2d_transform.h"
    }, 8u},
    {"kernel/gfx/glyph_raster.cc", {
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gfx/glyph_raster.cc",
        "kernel/gfx/glyph_raster.h",
        "kernel/mm/memory.h"
    }, 5u},
    {"kernel/gfx/graphics.cc", {
        "drivers/vga.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/simd.h",
        "kernel/gfx/font_8x8.h",
        "kernel/gfx/fontsys.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/graphics.cc",
        "kernel/gfx/graphics.h"
    }, 9u},
    {"kernel/gfx/jpeg.cc", {
        "kernel/core/types.h",
        "kernel/cpu/libm.h",
        "kernel/gfx/jpeg.cc",
        "kernel/gfx/jpeg.h",
        "kernel/mm/memory.h"
    }, 5u},
    {"kernel/gfx/png.cc", {
        "kernel/core/types.h",
        "kernel/gfx/png.cc",
        "kernel/gfx/png.h",
        "kernel/mm/memory.h"
    }, 4u},
    {"kernel/gfx/ttf.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gfx/ttf.cc",
        "kernel/gfx/ttf.h"
    }, 5u},
    {"kernel/gui/ansi.cc", {
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gui/ansi.cc",
        "kernel/gui/ansi.h"
    }, 4u},
    {"kernel/gui/clipboard.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gui/clipboard.cc",
        "kernel/gui/clipboard.h"
    }, 5u},
    {"kernel/gui/ctxt_image_worker.cc", {
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/vfs.h",
        "kernel/fs/vfs_helpers.h",
        "kernel/gui/ctxt_image_worker.cc",
        "kernel/gui/ctxt_image_worker.h",
        "kernel/mm/memory.h",
        "kernel/network/dns.h",
        "kernel/network/socket.h"
    }, 13u},
    {"kernel/gui/desktop.cc", {
        "drivers/keyboard.h",
        "drivers/mouse.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/core/app_launch.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/simd.h",
        "kernel/fs/vfs.h",
        "kernel/gfx/bmp.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_icons.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/desktop.cc",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/gui_widgets.h",
        "kernel/gui/terminal_app.h",
        "kernel/gui/ui.h",
        "kernel/lang/cupidc.h",
        "kernel/lang/dis.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/util/calendar.h",
        "toolchain/ctool.h",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 36u},
    {"kernel/gui/ed.cc", {
        "drivers/keyboard.h",
        "kernel/core/kernel.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/math.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/fat16.h",
        "kernel/fs/fs.h",
        "kernel/gui/ed.cc",
        "kernel/gui/ed.h",
        "kernel/mm/memory.h"
    }, 13u},
    {"kernel/gui/gui.cc", {
        "drivers/mouse.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/vga.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/simd.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.cc",
        "kernel/gui/gui.h",
        "kernel/gui/gui_themes.h",
        "kernel/mm/memory.h",
        "kernel/smp/bkl.h",
        "kernel/util/calendar.h"
    }, 18u},
    {"kernel/gui/gui_containers.cc", {
        "drivers/vga.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gfx/font_8x8.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/gui_containers.cc",
        "kernel/gui/gui_containers.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/ui.h"
    }, 10u},
    {"kernel/gui/gui_events.cc", {
        "drivers/vga.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gfx/font_8x8.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/gui.h",
        "kernel/gui/gui_events.cc",
        "kernel/gui/gui_events.h",
        "kernel/gui/ui.h",
        "kernel/mm/memory.h"
    }, 11u},
    {"kernel/gui/gui_menus.cc", {
        "drivers/vga.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gfx/font_8x8.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/gui_menus.cc",
        "kernel/gui/gui_menus.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/ui.h"
    }, 10u},
    {"kernel/gui/gui_themes.cc", {
        "drivers/rtc.h",
        "drivers/vga.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/vfs.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui_themes.cc",
        "kernel/gui/gui_themes.h",
        "kernel/mm/memory.h",
        "kernel/util/calendar.h"
    }, 11u},
    {"kernel/gui/gui_widgets.cc", {
        "drivers/vga.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/math.h",
        "kernel/gfx/font_8x8.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/gui_widgets.cc",
        "kernel/gui/gui_widgets.h",
        "kernel/gui/ui.h"
    }, 11u},
    {"kernel/gui/terminal_app.cc", {
        "drivers/keyboard.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/gfx/font_8x8.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/gui.h",
        "kernel/gui/terminal_ansi.h",
        "kernel/gui/terminal_app.cc",
        "kernel/gui/terminal_app.h",
        "kernel/lang/shell.h"
    }, 17u},
    {"kernel/gui/ui.cc", {
        "drivers/vga.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gfx/font_8x8.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/ui.cc",
        "kernel/gui/ui.h"
    }, 9u},
    {"kernel/lang/as.cc", {
        "drivers/ata.h",
        "drivers/keyboard.h",
        "drivers/mouse.h",
        "drivers/pci.h",
        "drivers/pit.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/speaker.h",
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/audio/ac97.h",
        "kernel/audio/midiopl.h",
        "kernel/audio/mixer.h",
        "kernel/audio/opl_smoke.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/ports.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/syscall.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/libm.h",
        "kernel/cpu/math.h",
        "kernel/doom/dglibc.h",
        "kernel/fs/blockcache.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/fat16.h",
        "kernel/fs/vfs.h",
        "kernel/fs/vfs_helpers.h",
        "kernel/gfx/bmp.h",
        "kernel/gfx/deflate.h",
        "kernel/gfx/fontsys.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_assets.h",
        "kernel/gfx/gfx2d_icons.h",
        "kernel/gfx/gfx2d_transform.h",
        "kernel/gfx/graphics.h",
        "kernel/gfx/jpeg.h",
        "kernel/gfx/png.h",
        "kernel/gui/ansi.h",
        "kernel/gui/clipboard.h",
        "kernel/gui/desktop.h",
        "kernel/gui/ed.h",
        "kernel/gui/gui.h",
        "kernel/gui/notepad.h",
        "kernel/lang/as.cc",
        "kernel/lang/as.h",
        "kernel/lang/as_elf.h",
        "kernel/lang/ctool_kernel.h",
        "kernel/lang/cupidc.h",
        "kernel/lang/dis.h",
        "kernel/lang/exec.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/mm/swap.h",
        "kernel/network/arp.h",
        "kernel/network/dhcp.h",
        "kernel/network/dns.h",
        "kernel/network/icmp.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h",
        "kernel/network/socket.h",
        "kernel/network/udp.h",
        "kernel/smp/bkl.h",
        "kernel/smp/lapic.h",
        "kernel/smp/percpu.h",
        "kernel/smp/smp.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
        "kernel/util/calendar.h",
        "toolchain/ctool.h",
        "toolchain/cupidasm.h",
        "toolchain/cupiddis.h",
        "toolchain/cupidld.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 79u},
    {"kernel/lang/as_elf.cc", {
        "kernel/lang/as_elf.cc",
        "kernel/lang/as_elf.h",
        "toolchain/ctool.h",
        "toolchain/cupidasm.h",
        "toolchain/cupidld.h",
        "toolchain/elf32.h",
        "toolchain/x86.h"
    }, 7u},
    {"kernel/lang/ctool_kernel.cc", {
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/vfs.h",
        "kernel/fs/vfs_helpers.h",
        "kernel/lang/ctool_kernel.cc",
        "kernel/lang/ctool_kernel.h",
        "kernel/lang/dis.h",
        "kernel/mm/memory.h",
        "toolchain/ctool.h",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 17u},
    {"kernel/lang/cupidc.cc", {
        "drivers/ata.h",
        "drivers/keyboard.h",
        "drivers/mouse.h",
        "drivers/pci.h",
        "drivers/pit.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/speaker.h",
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/audio/ac97.h",
        "kernel/audio/midiopl.h",
        "kernel/audio/mixer.h",
        "kernel/audio/opl_smoke.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/ports.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/libm.h",
        "kernel/cpu/math.h",
        "kernel/crypto/chacha20.h",
        "kernel/crypto/csprng.h",
        "kernel/crypto/ed25519.h",
        "kernel/crypto/hmac.h",
        "kernel/crypto/poly1305.h",
        "kernel/crypto/rsa.h",
        "kernel/crypto/sha256.h",
        "kernel/crypto/sha512.h",
        "kernel/crypto/x25519.h",
        "kernel/doom/dglibc.h",
        "kernel/fs/blockcache.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/fat16.h",
        "kernel/fs/vfs.h",
        "kernel/fs/vfs_helpers.h",
        "kernel/gfx/bmp.h",
        "kernel/gfx/deflate.h",
        "kernel/gfx/fontsys.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_assets.h",
        "kernel/gfx/gfx2d_effects.h",
        "kernel/gfx/gfx2d_icons.h",
        "kernel/gfx/gfx2d_transform.h",
        "kernel/gfx/graphics.h",
        "kernel/gfx/jpeg.h",
        "kernel/gfx/png.h",
        "kernel/gui/ansi.h",
        "kernel/gui/clipboard.h",
        "kernel/gui/ctxt_image_worker.h",
        "kernel/gui/desktop.h",
        "kernel/gui/ed.h",
        "kernel/gui/gui.h",
        "kernel/gui/gui_containers.h",
        "kernel/gui/gui_events.h",
        "kernel/gui/gui_menus.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/gui_widgets.h",
        "kernel/gui/ui.h",
        "kernel/lang/cupidc.cc",
        "kernel/lang/cupidc.h",
        "kernel/lang/dis.h",
        "kernel/lang/exec.h",
        "kernel/lang/shell.h",
        "kernel/lang/ssh_io.h",
        "kernel/mm/memory.h",
        "kernel/mm/swap.h",
        "kernel/network/arp.h",
        "kernel/network/dhcp.h",
        "kernel/network/dns.h",
        "kernel/network/icmp.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h",
        "kernel/network/socket.h",
        "kernel/network/udp.h",
        "kernel/smp/bkl.h",
        "kernel/smp/lapic.h",
        "kernel/smp/percpu.h",
        "kernel/smp/smp.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
        "kernel/util/calendar.h",
        "toolchain/ctool.h",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 90u},
    {"kernel/lang/cupidc_elf.cc", {
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/vfs.h",
        "kernel/lang/cupidc.h",
        "kernel/lang/cupidc_elf.cc",
        "kernel/lang/dis.h",
        "kernel/lang/exec.h",
        "toolchain/ctool.h",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 15u},
    {"kernel/lang/cupidc_lex.cc", {
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/lang/cupidc.h",
        "kernel/lang/cupidc_lex.cc",
        "kernel/lang/dis.h",
        "toolchain/ctool.h",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 10u},
    {"kernel/lang/cupidc_parse.cc", {
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/lang/cupidc.h",
        "kernel/lang/cupidc_parse.cc",
        "kernel/lang/dis.h",
        "toolchain/ctool.h",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 13u},
    {"kernel/lang/cupidc_string.cc", {
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/lang/cupidc_string.cc",
        "kernel/lang/cupidc_string.h",
        "kernel/mm/memory.h"
    }, 5u},
    {"kernel/lang/cupidscript_arrays.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/lang/cupidscript_arrays.cc",
        "kernel/lang/cupidscript_arrays.h",
        "kernel/mm/memory.h"
    }, 6u},
    {"kernel/lang/cupidscript_exec.cc", {
        "drivers/rtc.h",
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/fat16.h",
        "kernel/fs/fs.h",
        "kernel/fs/vfs.h",
        "kernel/gui/ansi.h",
        "kernel/lang/cupidc.h",
        "kernel/lang/cupidscript.h",
        "kernel/lang/cupidscript_arrays.h",
        "kernel/lang/cupidscript_exec.cc",
        "kernel/lang/cupidscript_jobs.h",
        "kernel/lang/cupidscript_streams.h",
        "kernel/lang/dis.h",
        "kernel/lang/exec.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/util/calendar.h",
        "toolchain/ctool.h",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 28u},
    {"kernel/lang/cupidscript_jobs.cc", {
        "drivers/serial.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/math.h",
        "kernel/lang/cupidscript_jobs.cc",
        "kernel/lang/cupidscript_jobs.h",
        "kernel/mm/memory.h"
    }, 8u},
    {"kernel/lang/cupidscript_lex.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gui/ansi.h",
        "kernel/lang/cupidscript.h",
        "kernel/lang/cupidscript_arrays.h",
        "kernel/lang/cupidscript_jobs.h",
        "kernel/lang/cupidscript_lex.cc",
        "kernel/lang/cupidscript_streams.h"
    }, 9u},
    {"kernel/lang/cupidscript_parse.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gui/ansi.h",
        "kernel/lang/cupidscript.h",
        "kernel/lang/cupidscript_arrays.h",
        "kernel/lang/cupidscript_jobs.h",
        "kernel/lang/cupidscript_parse.cc",
        "kernel/lang/cupidscript_streams.h",
        "kernel/mm/memory.h"
    }, 10u},
    {"kernel/lang/cupidscript_runtime.cc", {
        "drivers/rtc.h",
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/gui/ansi.h",
        "kernel/lang/cupidscript.h",
        "kernel/lang/cupidscript_arrays.h",
        "kernel/lang/cupidscript_jobs.h",
        "kernel/lang/cupidscript_runtime.cc",
        "kernel/lang/cupidscript_streams.h",
        "kernel/mm/memory.h"
    }, 13u},
    {"kernel/lang/cupidscript_streams.cc", {
        "drivers/keyboard.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/fs/vfs.h",
        "kernel/gui/ansi.h",
        "kernel/lang/cupidscript.h",
        "kernel/lang/cupidscript_arrays.h",
        "kernel/lang/cupidscript_jobs.h",
        "kernel/lang/cupidscript_streams.cc",
        "kernel/lang/cupidscript_streams.h",
        "kernel/mm/memory.h"
    }, 13u},
    {"kernel/lang/cupidscript_strings.cc", {
        "drivers/serial.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gui/ansi.h",
        "kernel/lang/cupidscript.h",
        "kernel/lang/cupidscript_arrays.h",
        "kernel/lang/cupidscript_jobs.h",
        "kernel/lang/cupidscript_streams.h",
        "kernel/lang/cupidscript_strings.cc",
        "kernel/mm/memory.h"
    }, 10u},
    {"kernel/lang/dis.cc", {
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/vfs.h",
        "kernel/lang/ctool_kernel.h",
        "kernel/lang/dis.cc",
        "kernel/lang/dis.h",
        "toolchain/ctool.h",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 12u},
    {"kernel/lang/exec.cc", {
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/syscall.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/vfs.h",
        "kernel/lang/exec.cc",
        "kernel/lang/exec.h",
        "kernel/mm/memory.h"
    }, 11u},
    {"kernel/lang/godspeak.cc", {
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/lang/godspeak.cc",
        "kernel/lang/godspeak.h"
    }, 7u},
    {"kernel/lang/shell.cc", {
        "drivers/keyboard.h",
        "drivers/pci.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/core/app_launch.h",
        "kernel/core/assert.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/ports.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/math.h",
        "kernel/fs/blockcache.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/fat16.h",
        "kernel/fs/fs.h",
        "kernel/fs/vfs.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_icons.h",
        "kernel/gui/ansi.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/terminal_app.h",
        "kernel/lang/as.h",
        "kernel/lang/as_elf.h",
        "kernel/lang/cupidc.h",
        "kernel/lang/cupidscript.h",
        "kernel/lang/cupidscript_arrays.h",
        "kernel/lang/cupidscript_jobs.h",
        "kernel/lang/cupidscript_streams.h",
        "kernel/lang/dis.h",
        "kernel/lang/exec.h",
        "kernel/lang/shell.cc",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/mm/swap.h",
        "kernel/network/arp.h",
        "kernel/network/dns.h",
        "kernel/network/icmp.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h",
        "kernel/network/socket.h",
        "kernel/network/sshd.h",
        "kernel/smp/bkl.h",
        "kernel/smp/percpu.h",
        "kernel/smp/smp.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
        "kernel/util/calendar.h",
        "toolchain/ctool.h",
        "toolchain/cupidasm.h",
        "toolchain/cupiddis.h",
        "toolchain/cupidld.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h"
    }, 62u},
    {"kernel/lang/ssh_io.cc", {
        "drivers/keyboard.h",
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/crypto/ecdsa.h",
        "kernel/crypto/p256.h",
        "kernel/gui/gui.h",
        "kernel/gui/terminal_app.h",
        "kernel/lang/shell.h",
        "kernel/lang/ssh_io.cc",
        "kernel/lang/ssh_io.h"
    }, 14u},
    {"kernel/mm/memory.cc", {
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.cc",
        "kernel/mm/memory.h",
        "kernel/smp/bkl.h"
    }, 10u},
    {"kernel/mm/paging.cc", {
        "kernel/core/types.h",
        "kernel/mm/memory.h",
        "kernel/mm/paging.cc"
    }, 3u},
    {"kernel/mm/swap.cc", {
        "drivers/serial.h",
        "kernel/core/panic.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/vfs.h",
        "kernel/mm/memory.h",
        "kernel/mm/swap.cc",
        "kernel/mm/swap.h",
        "kernel/mm/swap_disk.h"
    }, 9u},
    {"kernel/mm/swap_disk.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/fs/vfs.h",
        "kernel/mm/swap_disk.cc",
        "kernel/mm/swap_disk.h"
    }, 5u},
    {"kernel/network/arp.cc", {
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/network/arp.cc",
        "kernel/network/arp.h",
        "kernel/network/net_if.h"
    }, 8u},
    {"kernel/network/dhcp.cc", {
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/network/dhcp.cc",
        "kernel/network/dhcp.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h"
    }, 9u},
    {"kernel/network/dns.cc", {
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/network/dns.cc",
        "kernel/network/dns.h",
        "kernel/network/net_if.h",
        "kernel/network/socket.h",
        "kernel/smp/bkl.h"
    }, 10u},
    {"kernel/network/icmp.cc", {
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/network/icmp.cc",
        "kernel/network/icmp.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h"
    }, 8u},
    {"kernel/network/ip.cc", {
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/network/arp.h",
        "kernel/network/icmp.h",
        "kernel/network/ip.cc",
        "kernel/network/ip.h",
        "kernel/network/net_if.h",
        "kernel/network/tcp.h",
        "kernel/network/udp.h"
    }, 12u},
    {"kernel/network/net_if.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/network/arp.h",
        "kernel/network/dhcp.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.cc",
        "kernel/network/net_if.h",
        "kernel/network/tcp.h"
    }, 8u},
    {"kernel/network/socket.cc", {
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/crypto/sha256.h",
        "kernel/crypto/x509.h",
        "kernel/crypto/x509_chain.h",
        "kernel/mm/memory.h",
        "kernel/network/socket.cc",
        "kernel/network/socket.h",
        "kernel/network/tcp.h",
        "kernel/network/udp.h",
        "kernel/smp/bkl.h",
        "kernel/tls/tls_ctx.h",
        "kernel/tls/tls_record.h"
    }, 18u},
    {"kernel/network/sshd.cc", {
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/crypto/chacha20.h",
        "kernel/crypto/csprng.h",
        "kernel/crypto/ecdsa.h",
        "kernel/crypto/hkdf.h",
        "kernel/crypto/hmac.h",
        "kernel/crypto/p256.h",
        "kernel/crypto/poly1305.h",
        "kernel/crypto/sha256.h",
        "kernel/crypto/x25519.h",
        "kernel/fs/vfs.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/network/socket.h",
        "kernel/network/sshd.cc",
        "kernel/network/sshd.h"
    }, 22u},
    {"kernel/network/tcp.cc", {
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/cpu.h",
        "kernel/cpu/isr.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h",
        "kernel/network/socket.h",
        "kernel/network/tcp.cc",
        "kernel/network/tcp.h",
        "kernel/smp/bkl.h"
    }, 12u},
    {"kernel/network/udp.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/network/dhcp.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h",
        "kernel/network/udp.cc",
        "kernel/network/udp.h"
    }, 7u},
    {"kernel/smp/acpi.cc", {
        "drivers/serial.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/smp/acpi.cc",
        "kernel/smp/acpi.h",
        "kernel/smp/ioapic.h",
        "kernel/smp/mp_tables.h",
        "kernel/smp/percpu.h"
    }, 8u},
    {"kernel/smp/bkl.cc", {
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/smp/bkl.cc",
        "kernel/smp/bkl.h",
        "kernel/smp/percpu.h"
    }, 5u},
    {"kernel/smp/ioapic.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/mm/memory.h",
        "kernel/smp/ioapic.cc",
        "kernel/smp/ioapic.h"
    }, 5u},
    {"kernel/smp/lapic.cc", {
        "drivers/serial.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/mm/memory.h",
        "kernel/smp/lapic.cc",
        "kernel/smp/lapic.h"
    }, 6u},
    {"kernel/smp/mp_tables.cc", {
        "drivers/serial.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/smp/ioapic.h",
        "kernel/smp/mp_tables.cc",
        "kernel/smp/mp_tables.h",
        "kernel/smp/percpu.h"
    }, 7u},
    {"kernel/smp/percpu.cc", {
        "drivers/serial.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/smp/percpu.cc",
        "kernel/smp/percpu.h"
    }, 5u},
    {"kernel/smp/smp.cc", {
        "drivers/serial.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/fpu.h",
        "kernel/cpu/idt.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/smp/acpi.h",
        "kernel/smp/bkl.h",
        "kernel/smp/ioapic.h",
        "kernel/smp/lapic.h",
        "kernel/smp/mp_tables.h",
        "kernel/smp/percpu.h",
        "kernel/smp/smp.cc",
        "kernel/smp/smp.h"
    }, 15u},
    {"kernel/tls/tls12_handshake.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/crypto/asn1.h",
        "kernel/crypto/ct.h",
        "kernel/crypto/ecdsa.h",
        "kernel/crypto/p256.h",
        "kernel/crypto/rsa.h",
        "kernel/crypto/sha256.h",
        "kernel/crypto/x25519.h",
        "kernel/crypto/x509.h",
        "kernel/crypto/x509_chain.h",
        "kernel/tls/tls12_handshake.cc",
        "kernel/tls/tls12_handshake.h",
        "kernel/tls/tls_ctx.h",
        "kernel/tls/tls_kdf.h",
        "kernel/tls/tls_record.h"
    }, 16u},
    {"kernel/tls/tls_ca_bundle.cc", {
        "kernel/core/types.h",
        "kernel/crypto/x509.h",
        "kernel/crypto/x509_chain.h",
        "kernel/tls/tls_ca_bundle.cc"
    }, 4u},
    {"kernel/tls/tls_ca_bundle_data.cc", {
        "kernel/core/types.h",
        "kernel/crypto/x509.h",
        "kernel/crypto/x509_chain.h",
        "kernel/tls/tls_ca_bundle_data.cc"
    }, 4u},
    {"kernel/tls/tls_ctx.cc", {
        "kernel/core/types.h",
        "kernel/crypto/csprng.h",
        "kernel/crypto/ct.h",
        "kernel/crypto/p256.h",
        "kernel/crypto/sha256.h",
        "kernel/crypto/x25519.h",
        "kernel/crypto/x509.h",
        "kernel/crypto/x509_chain.h",
        "kernel/tls/tls_ctx.cc",
        "kernel/tls/tls_ctx.h",
        "kernel/tls/tls_record.h"
    }, 11u},
    {"kernel/tls/tls_handshake.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/crypto/asn1.h",
        "kernel/crypto/csprng.h",
        "kernel/crypto/ct.h",
        "kernel/crypto/ecdsa.h",
        "kernel/crypto/hkdf.h",
        "kernel/crypto/hmac.h",
        "kernel/crypto/p256.h",
        "kernel/crypto/rsa.h",
        "kernel/crypto/sha256.h",
        "kernel/crypto/x25519.h",
        "kernel/crypto/x509.h",
        "kernel/crypto/x509_chain.h",
        "kernel/tls/tls12_handshake.h",
        "kernel/tls/tls_ctx.h",
        "kernel/tls/tls_handshake.cc",
        "kernel/tls/tls_kdf.h",
        "kernel/tls/tls_record.h"
    }, 19u},
    {"kernel/tls/tls_kdf.cc", {
        "kernel/core/types.h",
        "kernel/crypto/hkdf.h",
        "kernel/crypto/hmac.h",
        "kernel/crypto/sha256.h",
        "kernel/tls/tls_kdf.cc",
        "kernel/tls/tls_kdf.h"
    }, 6u},
    {"kernel/tls/tls_record.cc", {
        "kernel/core/types.h",
        "kernel/crypto/aes.h",
        "kernel/crypto/aes_gcm.h",
        "kernel/crypto/chacha20poly1305.h",
        "kernel/crypto/ct.h",
        "kernel/tls/tls_record.cc",
        "kernel/tls/tls_record.h"
    }, 7u},
    {"kernel/tls/tls_selftest.cc", {
        "drivers/serial.h",
        "kernel/core/panic.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/crypto/aes.h",
        "kernel/crypto/aes_gcm.h",
        "kernel/crypto/asn1.h",
        "kernel/crypto/bigint.h",
        "kernel/crypto/chacha20poly1305.h",
        "kernel/crypto/ecdsa.h",
        "kernel/crypto/ed25519.h",
        "kernel/crypto/hkdf.h",
        "kernel/crypto/hmac.h",
        "kernel/crypto/p256.h",
        "kernel/crypto/rsa.h",
        "kernel/crypto/sha256.h",
        "kernel/crypto/sha512.h",
        "kernel/crypto/x25519.h",
        "kernel/crypto/x509.h",
        "kernel/crypto/x509_chain.h",
        "kernel/tls/tls_selftest.cc",
        "kernel/tls/tls_selftest.h"
    }, 22u},
    {"kernel/usb/ehci.cc", {
        "drivers/pci.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/usb/ehci.cc",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h"
    }, 13u},
    {"kernel/usb/uhci.cc", {
        "drivers/pci.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/usb/uhci.cc",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h"
    }, 13u},
    {"kernel/usb/usb.cc", {
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/usb/usb.cc",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h"
    }, 10u},
    {"kernel/usb/usb_hid.cc", {
        "drivers/keyboard.h",
        "drivers/mouse.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
        "kernel/usb/usb_hid.cc"
    }, 12u},
    {"kernel/usb/usb_hub.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/mm/memory.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
        "kernel/usb/usb_hub.cc"
    }, 6u},
    {"kernel/usb/usb_msc.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/fs/blockdev.h",
        "kernel/mm/memory.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
        "kernel/usb/usb_msc.cc"
    }, 7u},
    {"kernel/util/calendar.cc", {
        "drivers/rtc.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/fs/vfs.h",
        "kernel/util/calendar.cc",
        "kernel/util/calendar.h"
    }, 6u},
    {"toolchain/ctool.cc", {
        "toolchain/ctool.cc",
        "toolchain/ctool.h"
    }, 2u},
    {"toolchain/cupidasm.cc", {
        "toolchain/ctool.h",
        "toolchain/cupidasm.cc",
        "toolchain/cupidasm.h",
        "toolchain/elf32.h",
        "toolchain/x86.h"
    }, 5u},
    {"toolchain/cupiddis.cc", {
        "toolchain/ctool.h",
        "toolchain/cupiddis.cc",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/pe32_impl.h",
        "toolchain/x86.h"
    }, 7u},
    {"toolchain/cupidld.cc", {
        "toolchain/ctool.h",
        "toolchain/cupidld.cc",
        "toolchain/cupidld.h",
        "toolchain/elf32.h"
    }, 4u},
    {"toolchain/elf32.cc", {
        "toolchain/ctool.h",
        "toolchain/elf32.cc",
        "toolchain/elf32.h"
    }, 3u},
    {"toolchain/x86.cc", {
        "toolchain/ctool.h",
        "toolchain/x86.cc",
        "toolchain/x86.h"
    }, 3u},
};

static const char *const cupidbuild_compile_profile[] = {
    "--gnu",
    "--freestanding",
    "-D",
    "__GNUC__=1",
    "-D",
    "__ORDER_LITTLE_ENDIAN__=1234",
    "-D",
    "__ORDER_BIG_ENDIAN__=4321",
    "-D",
    "__ORDER_PDP_ENDIAN__=3412",
    "-D",
    "__BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__",
    "-D",
    "__SSE2__=1",
    "-D",
    "DEBUG=1",
    "-I",
    "/kernel",
    "-I",
    "/kernel/audio",
    "-I",
    "/kernel/core",
    "-I",
    "/kernel/cpu",
    "-I",
    "/kernel/crypto",
    "-I",
    "/kernel/doom",
    "-I",
    "/kernel/fs",
    "-I",
    "/kernel/gfx",
    "-I",
    "/kernel/gui",
    "-I",
    "/kernel/lang",
    "-I",
    "/kernel/mm",
    "-I",
    "/kernel/network",
    "-I",
    "/kernel/smp",
    "-I",
    "/kernel/tls",
    "-I",
    "/kernel/usb",
    "-I",
    "/kernel/util",
    "-I",
    "/drivers",
    "-I",
    "/toolchain"
};

typedef enum {
  CUPIDBUILD_COMPILE_KERNEL,
  CUPIDBUILD_COMPILE_DOOM,
  CUPIDBUILD_COMPILE_PRODUCTION
} cupidbuild_compile_kind_t;

static const cupidbuild_compile_closure_t cupidbuild_compile_production_closures[] = {
    {"kernel/util/bin_programs_gen.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/fs/homefs.h",
        "kernel/fs/ramfs.h",
        "kernel/fs/vfs.h",
        "kernel/util/bin_programs_gen.cc"
    }, 6u},
    {"kernel/util/demos_programs_gen.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/fs/homefs.h",
        "kernel/fs/ramfs.h",
        "kernel/fs/vfs.h",
        "kernel/util/demos_programs_gen.cc"
    }, 6u},
    {"kernel/util/docs_programs_gen.cc", {
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/fs/homefs.h",
        "kernel/fs/ramfs.h",
        "kernel/fs/vfs.h",
        "kernel/util/docs_programs_gen.cc"
    }, 6u}
};

static const char *const cupidbuild_compile_doom_profile[] = {
    "--gnu",
    "--doom-compat",
    "--freestanding",
    "-D",
    "__GNUC__=1",
    "-D",
    "__ORDER_LITTLE_ENDIAN__=1234",
    "-D",
    "__ORDER_BIG_ENDIAN__=4321",
    "-D",
    "__ORDER_PDP_ENDIAN__=3412",
    "-D",
    "__BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__",
    "-D",
    "__SSE2__=1",
    "-I",
    "/kernel",
    "-I",
    "/kernel/audio",
    "-I",
    "/kernel/core",
    "-I",
    "/kernel/cpu",
    "-I",
    "/kernel/crypto",
    "-I",
    "/kernel/doom",
    "-I",
    "/kernel/fs",
    "-I",
    "/kernel/gfx",
    "-I",
    "/kernel/gui",
    "-I",
    "/kernel/lang",
    "-I",
    "/kernel/mm",
    "-I",
    "/kernel/network",
    "-I",
    "/kernel/smp",
    "-I",
    "/kernel/tls",
    "-I",
    "/kernel/usb",
    "-I",
    "/kernel/util",
    "-I",
    "/drivers",
    "-I",
    "/toolchain",
    "-I",
    "/kernel/doom/src",
    "-I",
    "/kernel/doom/src/include_stubs",
};

static const char *const cupidbuild_compile_doom_tree_extra[] = {
    "-D",
    "DEFAULT_SAVEGAMEDIR=\"/home/doom/\"",
    "-D",
    "DOOM_PORT_CUPIDOS=1",
    "-include",
    "/kernel/doom/dglibc_compat.h",
};

static int cupidbuild_finish_publication(
    cupidbuild_host_transaction_t *transaction, int result,
    const char *artifact) {
  int committed = cupidbuild_host_publication_committed(transaction);
  if (cupidbuild_host_transaction_close(transaction)) {
    return result;
  }
  if (committed != 0) {
    (void)fprintf(
        stderr,
        "cupidbuild: %s was published, but transaction cleanup was incomplete\n",
        artifact);
    return result;
  }
  (void)fprintf(stderr, "cupidbuild: %s transaction cleanup failed\n",
                artifact);
  return 1;
}

static const char *const cupidbuild_profile_header_roots[] = {
    "drivers", "kernel", "toolchain"};
static const char *const cupidbuild_profile_header_suffixes[] = {".h",
                                                                 ".inc"};
static const char *const cupidbuild_profile_source_roots[] = {"kernel/doom"};
static const char *const cupidbuild_profile_source_suffixes[] = {".c", ".cc"};
static const char *const cupidbuild_profile_compat_sources[] = {
    "kernel/doom/dglibc.cc",
    "kernel/doom/doom_libc_stubs.cc",
    "kernel/doom/doomgeneric_cupidos.cc"};
static const char *const cupidbuild_profile_tree_sources[] = {
    "kernel/doom/i_sound_cupidos.cc",
    "kernel/doom/src/am_map.cc",
    "kernel/doom/src/d_event.cc",
    "kernel/doom/src/d_items.cc",
    "kernel/doom/src/d_iwad.cc",
    "kernel/doom/src/d_loop.cc",
    "kernel/doom/src/d_main.cc",
    "kernel/doom/src/d_mode.cc",
    "kernel/doom/src/d_net.cc",
    "kernel/doom/src/doomdef.cc",
    "kernel/doom/src/doomgeneric.cc",
    "kernel/doom/src/doomstat.cc",
    "kernel/doom/src/dstrings.cc",
    "kernel/doom/src/dummy.cc",
    "kernel/doom/src/f_finale.cc",
    "kernel/doom/src/f_wipe.cc",
    "kernel/doom/src/g_game.cc",
    "kernel/doom/src/gusconf.cc",
    "kernel/doom/src/hu_lib.cc",
    "kernel/doom/src/hu_stuff.cc",
    "kernel/doom/src/i_endoom.cc",
    "kernel/doom/src/i_input.cc",
    "kernel/doom/src/i_joystick.cc",
    "kernel/doom/src/i_scale.cc",
    "kernel/doom/src/i_system.cc",
    "kernel/doom/src/i_timer.cc",
    "kernel/doom/src/i_video.cc",
    "kernel/doom/src/icon.cc",
    "kernel/doom/src/info.cc",
    "kernel/doom/src/m_argv.cc",
    "kernel/doom/src/m_bbox.cc",
    "kernel/doom/src/m_cheat.cc",
    "kernel/doom/src/m_config.cc",
    "kernel/doom/src/m_controls.cc",
    "kernel/doom/src/m_fixed.cc",
    "kernel/doom/src/m_menu.cc",
    "kernel/doom/src/m_misc.cc",
    "kernel/doom/src/m_random.cc",
    "kernel/doom/src/p_ceilng.cc",
    "kernel/doom/src/p_doors.cc",
    "kernel/doom/src/p_enemy.cc",
    "kernel/doom/src/p_floor.cc",
    "kernel/doom/src/p_inter.cc",
    "kernel/doom/src/p_lights.cc",
    "kernel/doom/src/p_map.cc",
    "kernel/doom/src/p_maputl.cc",
    "kernel/doom/src/p_mobj.cc",
    "kernel/doom/src/p_plats.cc",
    "kernel/doom/src/p_pspr.cc",
    "kernel/doom/src/p_saveg.cc",
    "kernel/doom/src/p_setup.cc",
    "kernel/doom/src/p_sight.cc",
    "kernel/doom/src/p_spec.cc",
    "kernel/doom/src/p_switch.cc",
    "kernel/doom/src/p_telept.cc",
    "kernel/doom/src/p_tick.cc",
    "kernel/doom/src/p_user.cc",
    "kernel/doom/src/r_bsp.cc",
    "kernel/doom/src/r_data.cc",
    "kernel/doom/src/r_draw.cc",
    "kernel/doom/src/r_main.cc",
    "kernel/doom/src/r_plane.cc",
    "kernel/doom/src/r_segs.cc",
    "kernel/doom/src/r_sky.cc",
    "kernel/doom/src/r_things.cc",
    "kernel/doom/src/s_sound.cc",
    "kernel/doom/src/sha1.cc",
    "kernel/doom/src/sounds.cc",
    "kernel/doom/src/st_lib.cc",
    "kernel/doom/src/st_stuff.cc",
    "kernel/doom/src/statdump.cc",
    "kernel/doom/src/tables.cc",
    "kernel/doom/src/v_video.cc",
    "kernel/doom/src/w_checksum.cc",
    "kernel/doom/src/w_file.cc",
    "kernel/doom/src/w_file_stdc.cc",
    "kernel/doom/src/w_main.cc",
    "kernel/doom/src/w_wad.cc",
    "kernel/doom/src/wi_stuff.cc",
    "kernel/doom/src/z_zone.cc"};

typedef enum {
  CUPIDBUILD_KSYMS_EMPTY = 0,
  CUPIDBUILD_KSYMS_IGNORED,
  CUPIDBUILD_KSYMS_TEXT,
  CUPIDBUILD_KSYMS_OMITTED_ADDRESS,
  CUPIDBUILD_KSYMS_MALFORMED,
  CUPIDBUILD_KSYMS_INVALID_ADDRESS,
  CUPIDBUILD_KSYMS_ADDRESS_OUTSIDE_I386
} cupidbuild_ksyms_row_kind_t;

static int cupidbuild_path_safe(const char *path, int relative) {
  const char *cursor;
  if (path == (const char *)0 || path[0] == '\0' || strchr(path, '"') != 0) {
    return 0;
  }
  if (relative != 0 &&
      (path[0] == '/' || path[0] == '\\' || strchr(path, ':') != 0)) {
    return 0;
  }
  cursor = path;
  while (*cursor != '\0') {
    const char *start = cursor;
    while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
      cursor++;
    }
    if ((cursor - start == 1 && start[0] == '.') ||
        (cursor - start == 2 && start[0] == '.' && start[1] == '.')) {
      return 0;
    }
    if (*cursor != '\0') {
      cursor++;
    }
  }
  return 1;
}

static int cupidbuild_join(char *destination, size_t capacity, const char *left,
                           const char *right) {
  size_t left_size = strlen(left);
  size_t right_size = strlen(right);
  int separator = left_size != 0u && left[left_size - 1u] != '/' &&
                          left[left_size - 1u] != '\\'
                      ? 1
                      : 0;
  if (left_size + (size_t)separator + right_size + 1u > capacity) {
    return 0;
  }
  (void)memcpy(destination, left, left_size);
  if (separator != 0) {
    destination[left_size++] = '/';
  }
  (void)memcpy(destination + left_size, right, right_size + 1u);
  return 1;
}

static int cupidbuild_repository_prefix(const char *path, const char *root) {
  size_t index;
  size_t root_size = strlen(root);
  for (index = 0u; index < root_size; index++) {
    char left = path[index];
    char right = root[index];
#if defined(_WIN32)
    if (left == '\\') {
      left = '/';
    }
    if (right == '\\') {
      right = '/';
    }
    if (left >= 'A' && left <= 'Z') {
      left = (char)(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = (char)(right - 'A' + 'a');
    }
#endif
    if (left != right) {
      return 0;
    }
  }
  return path[root_size] == '/' || path[root_size] == '\\';
}

static void cupidbuild_json_space(cupidbuild_json_reader_t *reader) {
  while (reader->position < reader->size &&
         (reader->bytes[reader->position] == ' ' ||
          reader->bytes[reader->position] == '\t' ||
          reader->bytes[reader->position] == '\r' ||
          reader->bytes[reader->position] == '\n')) {
    reader->position++;
  }
}

static int cupidbuild_json_token(cupidbuild_json_reader_t *reader,
                                 cupidbuild_json_type_t type, size_t parent,
                                 size_t *index_out) {
  cupidbuild_json_token_t *token;
  if (reader->token_count >= CUPIDBUILD_JSON_TOKENS) {
    return 0;
  }
  *index_out = reader->token_count++;
  token = &reader->tokens[*index_out];
  token->type = type;
  token->start = reader->position;
  token->end = reader->position;
  token->count = 0u;
  token->parent = parent;
  return 1;
}

static int cupidbuild_json_hex(unsigned char byte) {
  return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
         (byte >= 'A' && byte <= 'F');
}

static int cupidbuild_json_string(cupidbuild_json_reader_t *reader,
                                  size_t parent, size_t *index_out) {
  cupidbuild_json_token_t *token;
  if (reader->position >= reader->size ||
      reader->bytes[reader->position] != '"' ||
      !cupidbuild_json_token(reader, CUPIDBUILD_JSON_STRING, parent,
                             index_out)) {
    return 0;
  }
  token = &reader->tokens[*index_out];
  reader->position++;
  token->start = reader->position;
  while (reader->position < reader->size) {
    unsigned char byte = reader->bytes[reader->position++];
    if (byte == '"') {
      token->end = reader->position - 1u;
      return 1;
    }
    if (byte < 0x20u) {
      return 0;
    }
    if (byte == '\\') {
      size_t remaining;
      unsigned char escape;
      if (reader->position >= reader->size) {
        return 0;
      }
      escape = reader->bytes[reader->position++];
      if (escape == 'u') {
        remaining = reader->size - reader->position;
        if (remaining < 4u ||
            !cupidbuild_json_hex(reader->bytes[reader->position]) ||
            !cupidbuild_json_hex(reader->bytes[reader->position + 1u]) ||
            !cupidbuild_json_hex(reader->bytes[reader->position + 2u]) ||
            !cupidbuild_json_hex(reader->bytes[reader->position + 3u])) {
          return 0;
        }
        reader->position += 4u;
      } else if (escape != '"' && escape != '\\' && escape != '/' &&
                 escape != 'b' && escape != 'f' && escape != 'n' &&
                 escape != 'r' && escape != 't') {
        return 0;
      }
    }
  }
  return 0;
}

static int cupidbuild_json_number(const unsigned char *bytes, size_t size) {
  size_t position = 0u;
  if (position < size && bytes[position] == '-') {
    position++;
  }
  if (position >= size) {
    return 0;
  }
  if (bytes[position] == '0') {
    position++;
  } else if (bytes[position] >= '1' && bytes[position] <= '9') {
    do {
      position++;
    } while (position < size && bytes[position] >= '0' &&
             bytes[position] <= '9');
  } else {
    return 0;
  }
  if (position < size && bytes[position] == '.') {
    position++;
    if (position >= size || bytes[position] < '0' || bytes[position] > '9') {
      return 0;
    }
    do {
      position++;
    } while (position < size && bytes[position] >= '0' &&
             bytes[position] <= '9');
  }
  if (position < size && (bytes[position] == 'e' || bytes[position] == 'E')) {
    position++;
    if (position < size && (bytes[position] == '+' || bytes[position] == '-')) {
      position++;
    }
    if (position >= size || bytes[position] < '0' || bytes[position] > '9') {
      return 0;
    }
    do {
      position++;
    } while (position < size && bytes[position] >= '0' &&
             bytes[position] <= '9');
  }
  return position == size;
}

static int cupidbuild_json_value(cupidbuild_json_reader_t *reader,
                                 size_t parent, unsigned int depth,
                                 size_t *index_out);

static int cupidbuild_json_object(cupidbuild_json_reader_t *reader,
                                  size_t parent, unsigned int depth,
                                  size_t *index_out) {
  cupidbuild_json_token_t *token;
  if (!cupidbuild_json_token(reader, CUPIDBUILD_JSON_OBJECT, parent,
                             index_out)) {
    return 0;
  }
  token = &reader->tokens[*index_out];
  reader->position++;
  cupidbuild_json_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == '}') {
    reader->position++;
    token->end = reader->position;
    return 1;
  }
  for (;;) {
    size_t key;
    size_t value;
    if (!cupidbuild_json_string(reader, *index_out, &key)) {
      return 0;
    }
    cupidbuild_json_space(reader);
    if (reader->position >= reader->size ||
        reader->bytes[reader->position++] != ':') {
      return 0;
    }
    cupidbuild_json_space(reader);
    if (!cupidbuild_json_value(reader, *index_out, depth + 1u, &value)) {
      return 0;
    }
    token->count++;
    cupidbuild_json_space(reader);
    if (reader->position >= reader->size) {
      return 0;
    }
    if (reader->bytes[reader->position] == '}') {
      reader->position++;
      token->end = reader->position;
      return 1;
    }
    if (reader->bytes[reader->position++] != ',') {
      return 0;
    }
    cupidbuild_json_space(reader);
  }
}

static int cupidbuild_json_array(cupidbuild_json_reader_t *reader,
                                 size_t parent, unsigned int depth,
                                 size_t *index_out) {
  cupidbuild_json_token_t *token;
  if (!cupidbuild_json_token(reader, CUPIDBUILD_JSON_ARRAY, parent,
                             index_out)) {
    return 0;
  }
  token = &reader->tokens[*index_out];
  reader->position++;
  cupidbuild_json_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == ']') {
    reader->position++;
    token->end = reader->position;
    return 1;
  }
  for (;;) {
    size_t value;
    if (!cupidbuild_json_value(reader, *index_out, depth + 1u, &value)) {
      return 0;
    }
    token->count++;
    cupidbuild_json_space(reader);
    if (reader->position >= reader->size) {
      return 0;
    }
    if (reader->bytes[reader->position] == ']') {
      reader->position++;
      token->end = reader->position;
      return 1;
    }
    if (reader->bytes[reader->position++] != ',') {
      return 0;
    }
    cupidbuild_json_space(reader);
  }
}

static int cupidbuild_json_value(cupidbuild_json_reader_t *reader,
                                 size_t parent, unsigned int depth,
                                 size_t *index_out) {
  cupidbuild_json_token_t *token;
  size_t start;
  if (depth > 64u || reader->position >= reader->size) {
    return 0;
  }
  if (reader->bytes[reader->position] == '{') {
    return cupidbuild_json_object(reader, parent, depth, index_out);
  }
  if (reader->bytes[reader->position] == '[') {
    return cupidbuild_json_array(reader, parent, depth, index_out);
  }
  if (reader->bytes[reader->position] == '"') {
    return cupidbuild_json_string(reader, parent, index_out);
  }
  if (!cupidbuild_json_token(reader, CUPIDBUILD_JSON_PRIMITIVE, parent,
                             index_out)) {
    return 0;
  }
  token = &reader->tokens[*index_out];
  start = reader->position;
  while (reader->position < reader->size &&
         reader->bytes[reader->position] != ' ' &&
         reader->bytes[reader->position] != '\t' &&
         reader->bytes[reader->position] != '\r' &&
         reader->bytes[reader->position] != '\n' &&
         reader->bytes[reader->position] != ',' &&
         reader->bytes[reader->position] != ']' &&
         reader->bytes[reader->position] != '}') {
    reader->position++;
  }
  token->end = reader->position;
  if (token->end == start) {
    return 0;
  }
  if ((token->end - start == 4u &&
       memcmp(reader->bytes + start, "true", 4u) == 0) ||
      (token->end - start == 5u &&
       memcmp(reader->bytes + start, "false", 5u) == 0) ||
      (token->end - start == 4u &&
       memcmp(reader->bytes + start, "null", 4u) == 0)) {
    return 1;
  }
  return cupidbuild_json_number(reader->bytes + start, token->end - start);
}

static int cupidbuild_json_parse(const unsigned char *bytes, size_t size,
                                 cupidbuild_json_token_t *tokens,
                                 size_t *count_out) {
  cupidbuild_json_reader_t reader;
  size_t root;
  reader.bytes = bytes;
  reader.size = size;
  reader.position = 0u;
  reader.tokens = tokens;
  reader.token_count = 0u;
  cupidbuild_json_space(&reader);
  if (!cupidbuild_json_value(&reader, (size_t)-1, 0u, &root)) {
    return 0;
  }
  cupidbuild_json_space(&reader);
  if (root != 0u || reader.position != reader.size) {
    return 0;
  }
  *count_out = reader.token_count;
  return 1;
}

static int cupidbuild_json_text(const unsigned char *bytes,
                                const cupidbuild_json_token_t *token,
                                const char *expected) {
  size_t size = token->end - token->start;
  return strlen(expected) == size &&
         memcmp(bytes + token->start, expected, size) == 0;
}

static size_t cupidbuild_json_next(const cupidbuild_json_token_t *tokens,
                                   size_t count, size_t index) {
  size_t next = index + 1u;
  while (next < count && tokens[next].start < tokens[index].end) {
    next++;
  }
  return next;
}

static size_t cupidbuild_json_field(const unsigned char *bytes,
                                    const cupidbuild_json_token_t *tokens,
                                    size_t count, size_t object,
                                    const char *name, size_t *matches_out) {
  size_t cursor = object + 1u;
  size_t matches = 0u;
  size_t value = count;
  size_t pair;
  for (pair = 0u; pair < tokens[object].count && cursor + 1u < count; pair++) {
    size_t candidate = cursor + 1u;
    if (cupidbuild_json_text(bytes, &tokens[cursor], name)) {
      matches++;
      value = candidate;
    }
    cursor = cupidbuild_json_next(tokens, count, candidate);
  }
  *matches_out = matches;
  return value;
}

static int cupidbuild_json_exact(const unsigned char *bytes,
                                 const cupidbuild_json_token_t *tokens,
                                 size_t count, size_t object,
                                 const char *const *names, size_t name_count) {
  size_t index;
  if (tokens[object].type != CUPIDBUILD_JSON_OBJECT ||
      tokens[object].count != name_count) {
    return 0;
  }
  for (index = 0u; index < name_count; index++) {
    size_t matches;
    (void)cupidbuild_json_field(bytes, tokens, count, object, names[index],
                                &matches);
    if (matches != 1u) {
      return 0;
    }
  }
  return 1;
}

static size_t cupidbuild_json_required(const unsigned char *bytes,
                                       const cupidbuild_json_token_t *tokens,
                                       size_t count, size_t object,
                                       const char *name) {
  size_t matches;
  size_t value =
      cupidbuild_json_field(bytes, tokens, count, object, name, &matches);
  return matches == 1u ? value : count;
}

static int cupidbuild_json_size(const unsigned char *bytes,
                                const cupidbuild_json_token_t *token,
                                size_t *value_out) {
  size_t value = 0u;
  size_t position;
  if (token->type != CUPIDBUILD_JSON_PRIMITIVE || token->start == token->end) {
    return 0;
  }
  for (position = token->start; position < token->end; position++) {
    unsigned int digit;
    if (bytes[position] < '0' || bytes[position] > '9') {
      return 0;
    }
    digit = (unsigned int)(bytes[position] - '0');
    if (value > ((size_t)-1 - digit) / 10u) {
      return 0;
    }
    value = value * 10u + digit;
  }
  *value_out = value;
  return 1;
}

static int cupidbuild_json_boolean(const unsigned char *bytes,
                                   const cupidbuild_json_token_t *token,
                                   int *value_out) {
  if (token->type != CUPIDBUILD_JSON_PRIMITIVE) {
    return 0;
  }
  if (cupidbuild_json_text(bytes, token, "true")) {
    *value_out = 1;
    return 1;
  }
  if (cupidbuild_json_text(bytes, token, "false")) {
    *value_out = 0;
    return 1;
  }
  return 0;
}

static int cupidbuild_json_copy(const unsigned char *bytes,
                                const cupidbuild_json_token_t *token,
                                char *destination, size_t capacity) {
  size_t size = token->end - token->start;
  if (token->type != CUPIDBUILD_JSON_STRING || size + 1u > capacity ||
      memchr(bytes + token->start, '\\', size) != (void *)0) {
    return 0;
  }
  (void)memcpy(destination, bytes + token->start, size);
  destination[size] = '\0';
  return 1;
}

static int cupidbuild_json_string_field(const unsigned char *bytes,
                                        const cupidbuild_json_token_t *tokens,
                                        size_t count, size_t object,
                                        const char *name,
                                        const char *expected) {
  size_t value = cupidbuild_json_required(bytes, tokens, count, object, name);
  return value < count && tokens[value].type == CUPIDBUILD_JSON_STRING &&
         cupidbuild_json_text(bytes, &tokens[value], expected);
}

static int cupidbuild_json_string_field_pair(
    const unsigned char *bytes, const cupidbuild_json_token_t *tokens,
    size_t count, size_t object, const char *name, const char *first,
    const char *second) {
  size_t value = cupidbuild_json_required(bytes, tokens, count, object, name);
  return value < count && tokens[value].type == CUPIDBUILD_JSON_STRING &&
         (cupidbuild_json_text(bytes, &tokens[value], first) ||
          cupidbuild_json_text(bytes, &tokens[value], second));
}

static int cupidbuild_json_lower_hex_field(
    const unsigned char *bytes, const cupidbuild_json_token_t *tokens,
    size_t count, size_t object, const char *name, size_t expected_size) {
  size_t value = cupidbuild_json_required(bytes, tokens, count, object, name);
  size_t index;
  if (value >= count || tokens[value].type != CUPIDBUILD_JSON_STRING ||
      tokens[value].end - tokens[value].start != expected_size) {
    return 0;
  }
  for (index = tokens[value].start; index < tokens[value].end; index++) {
    unsigned char digit = bytes[index];
    if (!((digit >= '0' && digit <= '9') ||
          (digit >= 'a' && digit <= 'f'))) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_json_number_field(const unsigned char *bytes,
                                        const cupidbuild_json_token_t *tokens,
                                        size_t count, size_t object,
                                        const char *name, size_t expected) {
  size_t value = cupidbuild_json_required(bytes, tokens, count, object, name);
  size_t actual;
  return value < count &&
         cupidbuild_json_size(bytes, &tokens[value], &actual) &&
         actual == expected;
}

#if !defined(_WIN32)
static int cupidbuild_json_string_array(const unsigned char *bytes,
                                        const cupidbuild_json_token_t *tokens,
                                        size_t count, size_t array,
                                        const char *const *expected,
                                        size_t expected_count) {
  size_t cursor;
  size_t index;
  if (array >= count || tokens[array].type != CUPIDBUILD_JSON_ARRAY ||
      tokens[array].count != expected_count) {
    return 0;
  }
  cursor = array + 1u;
  for (index = 0u; index < expected_count; index++) {
    if (cursor >= count || tokens[cursor].type != CUPIDBUILD_JSON_STRING ||
        !cupidbuild_json_text(bytes, &tokens[cursor], expected[index])) {
      return 0;
    }
    cursor = cupidbuild_json_next(tokens, count, cursor);
  }
  return 1;
}
#endif

static int cupidbuild_json_lineage(const unsigned char *bytes,
                                   const cupidbuild_json_token_t *tokens,
                                   size_t count, size_t object, int windows) {
  static const char *const names[] = {"assembly", "c", "link"};
  const char *assembly =
      windows ? "native stage-three CupidASM from the checked i386 Windows "
                "bootstrap"
              : "stage-three CupidASM from the checked-seed bootstrap";
  const char *c =
      windows
          ? "native stage-three CupidC from the checked i386 Windows bootstrap"
          : "stage-three CupidC from the checked-seed bootstrap";
  const char *link =
      windows
          ? "native stage-three CupidLD from the checked i386 Windows bootstrap"
          : "stage-three CupidLD from the checked-seed bootstrap";
  return object < count &&
         cupidbuild_json_exact(bytes, tokens, count, object, names, 3u) &&
         cupidbuild_json_string_field(bytes, tokens, count, object, "assembly",
                                      assembly) &&
         cupidbuild_json_string_field(bytes, tokens, count, object, "c", c) &&
         cupidbuild_json_string_field(bytes, tokens, count, object, "link",
                                      link);
}

static int cupidbuild_json_provenance(const unsigned char *bytes,
                                      const cupidbuild_json_token_t *tokens,
                                      size_t count, size_t object,
                                      int windows, int promoted) {
  static const char legacy_revision[] =
      "a17c9465911da41d59b7ada71733d36c39faa5ea";
  static const char legacy_snapshot[] =
      "46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67";
  static const char legacy_linux_manifest[] =
      "b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b";
  static const char preceding_parent_revision[] =
      "9d2529a718672edcd970f24960535db6ddbde5e4";
  static const char preceding_parent_linux_manifest[] =
      "30adaac167ee6cdde136ce5d957e6b4ca0b0a2bdc23ff376436634a6a0db027e";
  static const char preceding_parent_windows_manifest[] =
      "6960e4cb8bd26c3711db85aede44655f0f9b83a0a2fc3612053bb5d91674ff6a";
  static const char active_parent_revision[] =
      "83d00ce70e5607dc5c011bb97c6478121f24a21c";
  static const char active_parent_linux_manifest[] =
      "a11c8af08eb1170d040dc6b361c30df321c088fcb4ae5becd6c2864995380622";
  static const char active_parent_windows_manifest[] =
      "f5124cbddbeb55a61ce2f8ae93923daae512d6fec6732a532b1e8f0d15bed590";
  static const char *const linux_v1_names[] = {
      "fixed_point_command",   "fixed_point_result", "producer_lineage",
      "seed_generation",       "source_input_count", "source_revision",
      "source_snapshot_sha256"};
  static const char *const linux_v2_names[] = {
      "artifact_generation",         "fixed_point_command",
      "fixed_point_result",          "parent_seed_manifest_sha256",
      "parent_seed_source_revision", "producer_lineage",
      "seed_generation",             "source_input_count",
      "source_revision",             "source_snapshot_sha256"};
  static const char *const windows_v1_names[] = {
      "artifact_generation",         "fixed_point_command",
      "fixed_point_result",          "parent_seed_manifest_sha256",
      "parent_seed_source_revision", "producer_lineage",
      "source_input_count",          "source_revision",
      "source_snapshot_sha256"};
  static const char *const windows_v2_names[] = {
      "artifact_generation",
      "fixed_point_command",
      "fixed_point_result",
      "parent_execution_seed_manifest_sha256",
      "parent_execution_seed_source_revision",
      "linux_candidate_build_plan_sha256",
      "native_build_plan_sha256",
      "plan_seed_manifest_sha256",
      "parent_plan_seed_manifest_sha256",
      "parent_plan_seed_source_revision",
      "producer_lineage",
      "source_input_count",
      "source_revision",
      "source_snapshot_sha256"};
  const char *const *names =
      windows ? (promoted ? windows_v2_names : windows_v1_names)
              : (promoted ? linux_v2_names : linux_v1_names);
  size_t name_count = windows ? (promoted ? 14u : 9u)
                              : (promoted ? 10u : 7u);
  size_t lineage;
  int source_count_matches;
  if (object >= count)
    return 0;
  source_count_matches =
      promoted
          ? (cupidbuild_json_number_field(bytes, tokens, count, object,
                                           "source_input_count", 58u) ||
             cupidbuild_json_number_field(bytes, tokens, count, object,
                                           "source_input_count", 59u))
          : cupidbuild_json_number_field(bytes, tokens, count, object,
                                         "source_input_count", 50u);
  if (!cupidbuild_json_exact(bytes, tokens, count, object, names,
                             name_count) ||
      !cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "fixed_point_result", "pass") ||
      !source_count_matches ||
      (promoted
           ? (!cupidbuild_json_lower_hex_field(
                  bytes, tokens, count, object, "source_revision", 40u) ||
              !cupidbuild_json_lower_hex_field(
                  bytes, tokens, count, object, "source_snapshot_sha256",
                  64u))
           : (!cupidbuild_json_string_field(
                  bytes, tokens, count, object, "source_revision",
                  legacy_revision) ||
              !cupidbuild_json_string_field(
                  bytes, tokens, count, object, "source_snapshot_sha256",
                  legacy_snapshot)))) {
    return 0;
  }
  lineage = cupidbuild_json_required(bytes, tokens, count, object,
                                     "producer_lineage");
  if (!cupidbuild_json_lineage(bytes, tokens, count, lineage, windows)) {
    return 0;
  }
  if (windows) {
    if (!cupidbuild_json_string_field(
            bytes, tokens, count, object, "artifact_generation",
            promoted ? "paired-stage-four-six-tool-native-windows"
                     : "paired-stage-four-native-windows") ||
        !cupidbuild_json_string_field(bytes, tokens, count, object,
                                      "fixed_point_command",
                                      "make bootstrap-windows-from-seed")) {
      return 0;
    }
    if (!promoted) {
      return cupidbuild_json_string_field(
                 bytes, tokens, count, object,
                 "parent_seed_manifest_sha256", legacy_linux_manifest) &&
             cupidbuild_json_string_field(
                 bytes, tokens, count, object,
                 "parent_seed_source_revision", legacy_revision);
    }
    return cupidbuild_json_string_field(
                bytes, tokens, count, object,
                "linux_candidate_build_plan_sha256",
                "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd") &&
            cupidbuild_json_string_field_pair(
                bytes, tokens, count, object, "native_build_plan_sha256",
                "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14",
                "98e09aab876a9fa37ec07c38a0a57a014549a14c0ab10c740b3f80ede9d65669") &&
            cupidbuild_json_lower_hex_field(
                bytes, tokens, count, object, "plan_seed_manifest_sha256",
                64u) &&
            ((cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_execution_seed_manifest_sha256",
                   preceding_parent_windows_manifest) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_execution_seed_source_revision",
                   preceding_parent_revision) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_plan_seed_manifest_sha256",
                   preceding_parent_linux_manifest) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_plan_seed_source_revision",
                   preceding_parent_revision)) ||
              (cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_execution_seed_manifest_sha256",
                   active_parent_windows_manifest) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_execution_seed_source_revision",
                   active_parent_revision) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_plan_seed_manifest_sha256",
                   active_parent_linux_manifest) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_plan_seed_source_revision",
                   active_parent_revision)));
  }
  if (!cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "fixed_point_command",
                                    "make bootstrap-from-seed") ||
      !cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "seed_generation", "stage-four")) {
    return 0;
  }
  return !promoted ||
         (cupidbuild_json_string_field(
              bytes, tokens, count, object, "artifact_generation",
              "paired-stage-four-six-tool") &&
          ((cupidbuild_json_string_field(
                bytes, tokens, count, object, "parent_seed_manifest_sha256",
                preceding_parent_linux_manifest) &&
            cupidbuild_json_string_field(
                bytes, tokens, count, object, "parent_seed_source_revision",
                preceding_parent_revision)) ||
           (cupidbuild_json_string_field(
                bytes, tokens, count, object, "parent_seed_manifest_sha256",
                active_parent_linux_manifest) &&
            cupidbuild_json_string_field(
                bytes, tokens, count, object, "parent_seed_source_revision",
                active_parent_revision))));
}

static int cupidbuild_json_target(const unsigned char *bytes,
                                  const cupidbuild_json_token_t *tokens,
                                  size_t count, size_t object, int windows) {
  static const char *const linux_names[] = {
      "abi",   "architecture", "byte_order",      "elf_class",
      "entry", "linkage",      "operating_system"};
  static const char *const windows_names[] = {
      "abi",     "architecture",     "byte_order", "entry",
      "linkage", "operating_system", "pe_class"};
  if (object >= count ||
      !cupidbuild_json_exact(bytes, tokens, count, object,
                             windows ? windows_names : linux_names, 7u) ||
      !cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "architecture", "i386") ||
      !cupidbuild_json_string_field(bytes, tokens, count, object, "byte_order",
                                    "little")) {
    return 0;
  }
  if (windows) {
    return cupidbuild_json_string_field(bytes, tokens, count, object, "abi",
                                        "windows-stdcall-imports") &&
           cupidbuild_json_number_field(bytes, tokens, count, object, "entry",
                                        4198400u) &&
           cupidbuild_json_string_field(bytes, tokens, count, object, "linkage",
                                        "kernel32-imports") &&
           cupidbuild_json_string_field(bytes, tokens, count, object,
                                        "operating_system", "windows") &&
           cupidbuild_json_number_field(bytes, tokens, count, object,
                                        "pe_class", 32u);
  }
  return cupidbuild_json_string_field(bytes, tokens, count, object, "abi",
                                      "linux-int80") &&
         cupidbuild_json_number_field(bytes, tokens, count, object, "entry",
                                      134512640u) &&
         cupidbuild_json_string_field(bytes, tokens, count, object, "linkage",
                                      "static") &&
         cupidbuild_json_string_field(bytes, tokens, count, object,
                                      "operating_system", "linux") &&
         cupidbuild_json_number_field(bytes, tokens, count, object, "elf_class",
                                      32u);
}

#if !defined(_WIN32)
static int cupidbuild_json_sources(const unsigned char *bytes,
                                   const cupidbuild_json_token_t *tokens,
                                   size_t count, size_t array, int promoted) {
  static const char *const names[] = {"gnu_extensions", "name", "path"};
  static const cupidbuild_seed_source_t expected[] = {
      {"runtime", "/toolchain/hosted/i386-linux/runtime.cc", 1},
      {"ctool", "/toolchain/ctool.cc", 0},
      {"ctool_host", "/toolchain/ctool_host.cc", 0},
      {"elf32", "/toolchain/elf32.cc", 0},
      {"x86", "/toolchain/x86.cc", 0},
      {"cupidasm", "/toolchain/cupidasm.cc", 0},
      {"cupidasm_main", "/toolchain/cupidasm_main.cc", 0},
      {"cupiddis", "/toolchain/cupiddis.cc", 0},
      {"cupiddis_main", "/toolchain/cupiddis_main.cc", 0},
      {"cupidobj", "/toolchain/cupidobj.cc", 0},
      {"cupidobj_main", "/toolchain/cupidobj_main.cc", 0},
      {"cupidld", "/toolchain/cupidld.cc", 0},
      {"cupidld_main", "/toolchain/cupidld_main.cc", 0},
      {"cupidc_pp", "/toolchain/cupidc_pp.cc", 0},
      {"cupidc_type", "/toolchain/cupidc_type.cc", 0},
      {"cupidc_frontend", "/toolchain/cupidc_frontend.cc", 0},
      {"cupidc_ir", "/toolchain/cupidc_ir.cc", 0},
      {"cupidc_emit", "/toolchain/cupidc_emit.cc", 0},
      {"cupidc_main", "/toolchain/cupidc_main.cc", 0},
      {"cupidbuild", "/toolchain/cupidbuild.cc", 0},
      {"cupidbuild_host", "/toolchain/cupidbuild_host.cc", 0},
      {"cupidbuild_main", "/toolchain/cupidbuild_main.cc", 0}};
  size_t cursor;
  size_t index;
  size_t expected_count = promoted ? 22u : 19u;
  if (array >= count || tokens[array].type != CUPIDBUILD_JSON_ARRAY ||
      tokens[array].count != expected_count) {
    return 0;
  }
  cursor = array + 1u;
  for (index = 0u; index < expected_count; index++) {
    size_t extensions;
    int actual_extensions;
    if (cursor >= count ||
        !cupidbuild_json_exact(bytes, tokens, count, cursor, names, 3u) ||
        !cupidbuild_json_string_field(bytes, tokens, count, cursor, "name",
                                      expected[index].name) ||
        !cupidbuild_json_string_field(bytes, tokens, count, cursor, "path",
                                      expected[index].path)) {
      return 0;
    }
    extensions = cupidbuild_json_required(bytes, tokens, count, cursor,
                                          "gnu_extensions");
    if (extensions >= count ||
        !cupidbuild_json_boolean(bytes, &tokens[extensions],
                                 &actual_extensions) ||
        actual_extensions != expected[index].gnu_extensions) {
      return 0;
    }
    cursor = cupidbuild_json_next(tokens, count, cursor);
  }
  return 1;
}

static int cupidbuild_json_links(const unsigned char *bytes,
                                 const cupidbuild_json_token_t *tokens,
                                 size_t count, size_t object, int promoted) {
  static const char *const names[] = {"cupidasm", "cupiddis", "cupidld",
                                      "cupidobj", "cupidc", "cupidbuild"};
  static const char *const cupidasm[] = {
      "start", "cupidasm_main", "cupidasm", "ctool_host",
      "ctool", "elf32",         "x86",      "runtime"};
  static const char *const cupiddis[] = {
      "start", "cupiddis_main", "cupiddis", "ctool_host",
      "ctool", "elf32",         "x86",      "runtime"};
  static const char *const cupidld[] = {"start",      "cupidld_main", "cupidld",
                                        "ctool_host", "ctool",        "elf32",
                                        "runtime"};
  static const char *const cupidobj[] = {
      "start", "cupidobj_main", "cupidobj", "ctool_host",
      "ctool", "elf32",         "runtime"};
  static const char *const cupidc[] = {
      "start",           "cupidc_main", "cupidc_emit", "cupidc_ir",
      "cupidc_frontend", "cupidc_type", "cupidc_pp",   "ctool_host",
      "ctool",           "elf32",       "x86",         "runtime"};
  static const char *const cupidbuild[] = {
      "start", "cupidbuild_main", "cupidbuild", "cupidbuild_host",
      "ctool_host", "ctool", "elf32", "runtime"};
  size_t value;
  if (object >= count ||
      !cupidbuild_json_exact(bytes, tokens, count, object, names,
                             promoted ? 6u : 5u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupidasm");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, cupidasm,
                                    8u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupiddis");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, cupiddis,
                                    8u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupidld");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, cupidld, 7u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupidobj");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, cupidobj,
                                    7u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupidc");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, cupidc,
                                    12u)) {
    return 0;
  }
  if (!promoted) {
    return 1;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupidbuild");
  return cupidbuild_json_string_array(bytes, tokens, count, value,
                                      cupidbuild, 8u);
}

static int cupidbuild_json_build_plan(const unsigned char *bytes,
                                      const cupidbuild_json_token_t *tokens,
                                      size_t count, size_t object,
                                      int promoted) {
  static const char *const names[] = {"include_arguments", "links",
                                      "producer_tools",    "sources",
                                      "startup",           "workers"};
  static const char *const includes[] = {
      "-I", "/toolchain", "--include-angle",
      "/toolchain/hosted/i386-linux/include"};
  static const char *const producers[] = {"cupidc", "cupidasm", "cupidld"};
  size_t value;
  if (object >= count ||
      !cupidbuild_json_exact(bytes, tokens, count, object, names, 6u) ||
      !cupidbuild_json_string_field(bytes, tokens, count, object, "startup",
                                    "/toolchain/hosted/i386-linux/start.asm") ||
      !cupidbuild_json_number_field(bytes, tokens, count, object, "workers",
                                    2u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object,
                                   "include_arguments");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, includes,
                                    4u)) {
    return 0;
  }
  value =
      cupidbuild_json_required(bytes, tokens, count, object, "producer_tools");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, producers,
                                    3u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "sources");
  if (!cupidbuild_json_sources(bytes, tokens, count, value, promoted)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "links");
  return cupidbuild_json_links(bytes, tokens, count, value, promoted);
}
#endif

static int cupidbuild_json_artifacts(const unsigned char *bytes,
                                     const cupidbuild_json_token_t *tokens,
                                     size_t count, size_t array, int windows,
                                     int promoted,
                                     cupidbuild_seed_artifact_t
                                         artifacts[CUPIDBUILD_SEED_ARTIFACTS]) {
  static const char *const fields[] = {"file", "name", "producer", "sha256",
                                       "size"};
  static const char *const names[] = {"cupidasm", "cupidc", "cupiddis",
                                      "cupidld", "cupidobj", "cupidbuild"};
  static const int producers[] = {1, 1, 0, 1, 0, 0};
  int seen[CUPIDBUILD_SEED_ARTIFACTS] = {0, 0, 0, 0, 0, 0};
  size_t cursor;
  size_t item;
  size_t expected_count = promoted ? CUPIDBUILD_SEED_ARTIFACTS : 5u;
  if (array >= count || tokens[array].type != CUPIDBUILD_JSON_ARRAY ||
      tokens[array].count != expected_count) {
    return 0;
  }
  cursor = array + 1u;
  for (item = 0u; item < expected_count; item++) {
    size_t name_token;
    size_t file_token;
    size_t digest_token;
    size_t producer_token;
    size_t size_token;
    size_t index;
    size_t actual_size;
    int actual_producer;
    char expected_file[32];
    if (cursor >= count ||
        !cupidbuild_json_exact(bytes, tokens, count, cursor, fields, 5u)) {
      return 0;
    }
    name_token = cupidbuild_json_required(bytes, tokens, count, cursor, "name");
    for (index = 0u; index < CUPIDBUILD_SEED_ARTIFACTS; index++) {
      if (name_token < count &&
          cupidbuild_json_text(bytes, &tokens[name_token], names[index])) {
        break;
      }
    }
    if (index == CUPIDBUILD_SEED_ARTIFACTS || seen[index] != 0) {
      return 0;
    }
    seen[index] = 1;
    file_token = cupidbuild_json_required(bytes, tokens, count, cursor, "file");
    digest_token =
        cupidbuild_json_required(bytes, tokens, count, cursor, "sha256");
    producer_token =
        cupidbuild_json_required(bytes, tokens, count, cursor, "producer");
    size_token = cupidbuild_json_required(bytes, tokens, count, cursor, "size");
    if (snprintf(expected_file, sizeof(expected_file), "%s.%s", names[index],
                 windows ? "exe" : "elf") < 0 ||
        file_token >= count || digest_token >= count ||
        producer_token >= count || size_token >= count ||
        !cupidbuild_json_text(bytes, &tokens[file_token], expected_file) ||
        !cupidbuild_json_copy(bytes, &tokens[file_token], artifacts[index].file,
                              sizeof(artifacts[index].file)) ||
        !cupidbuild_json_copy(bytes, &tokens[digest_token],
                              artifacts[index].sha256,
                              sizeof(artifacts[index].sha256)) ||
        strlen(artifacts[index].sha256) != 64u ||
        !cupidbuild_json_boolean(bytes, &tokens[producer_token],
                                 &actual_producer) ||
        actual_producer != producers[index] ||
        !cupidbuild_json_size(bytes, &tokens[size_token], &actual_size) ||
        actual_size == 0u) {
      return 0;
    }
    for (size_token = 0u; size_token < 64u; size_token++) {
      char digit = artifacts[index].sha256[size_token];
      if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) {
        return 0;
      }
    }
    artifacts[index].size = actual_size;
    cursor = cupidbuild_json_next(tokens, count, cursor);
  }
  for (item = 0u; item < CUPIDBUILD_SEED_ARTIFACTS; item++) {
    if (seen[item] != (item < expected_count ? 1 : 0)) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_json_manifest(const unsigned char *manifest,
                                    size_t manifest_size,
                                     cupidbuild_seed_artifact_t
                                         artifacts[CUPIDBUILD_SEED_ARTIFACTS],
                                     size_t *artifact_count_out,
                                     int *current_windows_plan_out,
                                     const char **reason_out) {
  cupidbuild_json_token_t *tokens;
  size_t count = 0u;
  size_t artifacts_token;
  size_t provenance;
  size_t schema;
  size_t target;
  int windows;
  int promoted;
  int current_windows_plan = 0;
#if defined(_WIN32)
  static const char legacy_schema[] = "cupid.execution-seed.v1";
  static const char promoted_schema[] = "cupid.execution-seed.v2";
  static const char *const top_names[] = {"artifacts", "provenance", "schema",
                                          "target"};
  windows = 1;
#else
  static const char legacy_schema[] = "cupid.bootstrap-seed.v1";
  static const char promoted_schema[] = "cupid.bootstrap-seed.v2";
  static const char *const top_names[] = {
      "artifacts",  "build_plan", "build_plan_sha256",
      "provenance", "schema",     "target"};
  windows = 0;
#endif
  tokens = (cupidbuild_json_token_t *)malloc(CUPIDBUILD_JSON_TOKENS *
                                             sizeof(cupidbuild_json_token_t));
  if (tokens == (cupidbuild_json_token_t *)0) {
    *reason_out = "JSON token storage is unavailable";
    return 0;
  }
  if (!cupidbuild_json_parse(manifest, manifest_size, tokens, &count) ||
      count == 0u) {
    *reason_out = "JSON syntax is malformed";
    free(tokens);
    return 0;
  }
  if (!cupidbuild_json_exact(manifest, tokens, count, 0u, top_names,
                             sizeof(top_names) / sizeof(top_names[0]))) {
    *reason_out = "top-level fields differ";
    free(tokens);
    return 0;
  }
  schema = cupidbuild_json_required(manifest, tokens, count, 0u, "schema");
  artifacts_token =
      cupidbuild_json_required(manifest, tokens, count, 0u, "artifacts");
  provenance =
      cupidbuild_json_required(manifest, tokens, count, 0u, "provenance");
  target = cupidbuild_json_required(manifest, tokens, count, 0u, "target");
  if (schema >= count || tokens[schema].type != CUPIDBUILD_JSON_STRING) {
    *reason_out = "schema differs";
    free(tokens);
    return 0;
  }
  promoted = cupidbuild_json_text(manifest, &tokens[schema], promoted_schema);
  if (!promoted &&
      !cupidbuild_json_text(manifest, &tokens[schema], legacy_schema)) {
    *reason_out = "schema differs";
    free(tokens);
    return 0;
  }
  if (!cupidbuild_json_artifacts(manifest, tokens, count, artifacts_token,
                                 windows, promoted, artifacts)) {
    *reason_out = "artifact inventory differs";
    free(tokens);
    return 0;
  }
  if (!cupidbuild_json_provenance(manifest, tokens, count, provenance,
                                  windows, promoted)) {
    *reason_out = "fixed-point provenance differs";
    free(tokens);
    return 0;
  }
#if defined(_WIN32)
  if (promoted) {
    current_windows_plan = cupidbuild_json_string_field(
        manifest, tokens, count, provenance, "native_build_plan_sha256",
        "98e09aab876a9fa37ec07c38a0a57a014549a14c0ab10c740b3f80ede9d65669");
  }
#endif
  if (!cupidbuild_json_target(manifest, tokens, count, target, windows)) {
    *reason_out = "target contract differs";
    free(tokens);
    return 0;
  }
#if !defined(_WIN32)
  {
    size_t plan =
        cupidbuild_json_required(manifest, tokens, count, 0u, "build_plan");
    const char *expected_plan_sha256 =
        promoted
            ? "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd"
            : "59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc";
    if (!cupidbuild_json_string_field(manifest, tokens, count, 0u,
                                      "build_plan_sha256",
                                      expected_plan_sha256) ||
        !cupidbuild_json_build_plan(manifest, tokens, count, plan,
                                    promoted)) {
      *reason_out = "build plan differs";
      free(tokens);
      return 0;
    }
  }
#endif
  *artifact_count_out = promoted ? CUPIDBUILD_SEED_ARTIFACTS : 5u;
  *current_windows_plan_out = current_windows_plan;
  free(tokens);
  return 1;
}

static int cupidbuild_manifest_directory(const char *manifest, char *directory,
                                         size_t capacity) {
  const char *cursor = manifest;
  const char *last = (const char *)0;
  size_t size;
  while (*cursor != '\0') {
    if (*cursor == '/' || *cursor == '\\') {
      last = cursor;
    }
    cursor++;
  }
  if (last == (const char *)0) {
    return 0;
  }
  size = (size_t)(last - manifest);
  if (size == 0u || size + 1u > capacity) {
    return 0;
  }
  (void)memcpy(directory, manifest, size);
  directory[size] = '\0';
  return 1;
}

static int cupidbuild_digest_matches_hex(const unsigned char digest[32],
                                         const char *hex) {
  static const char digits[] = "0123456789abcdef";
  size_t index;
  for (index = 0u; index < 32u; index++) {
    if (hex[index * 2u] != digits[digest[index] >> 4u] ||
        hex[index * 2u + 1u] != digits[digest[index] & 15u]) {
      return 0;
    }
  }
  return hex[64] == '\0';
}

static int
cupidbuild_artifact_matches(const cupidbuild_host_snapshot_t *snapshot,
                            const cupidbuild_seed_artifact_t *artifact) {
  return snapshot->present != 0 && snapshot->size == artifact->size &&
         cupidbuild_digest_matches_hex(snapshot->sha256, artifact->sha256);
}

static int cupidbuild_validate_relocatable(const unsigned char *bytes,
                                           size_t size,
                                           int require_executable) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_elf32_object_t object;
  ctool_u32 index;
  int relocatable = 0;
  int executable = 0;
  if (size > 4294967295u ||
      ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
    return 0;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  if (ctool_job_open(&config, &job) != CTOOL_OK) {
    return 0;
  }
  source.path.text = ctool_string("/candidate.o");
  source.contents = ctool_bytes(bytes, (ctool_u32)size);
  if (ctool_elf32_read(job, &source, &object) == CTOOL_OK &&
      object.file_type == CTOOL_ELF32_ET_REL) {
    relocatable = 1;
    for (index = 0u; index < object.section_count; index++) {
      const ctool_elf32_section_t *section = &object.sections[index];
      if (section->type == CTOOL_ELF32_SHT_PROGBITS &&
          (section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u &&
          section->size != 0u) {
        executable = 1;
      }
    }
  }
  ctool_job_close(job);
  return relocatable != 0 &&
         (require_executable == 0 || executable != 0);
}

static int cupidbuild_jpeg_symbol_name_matches(ctool_string_t actual,
                                                const char *identity,
                                                const char *suffix) {
  static const char prefix[] = "_binary_";
  size_t prefix_size = sizeof(prefix) - 1u;
  size_t identity_size = strlen(identity);
  size_t suffix_size = strlen(suffix);
  size_t index;
  if (identity_size > (size_t)-1 - prefix_size - suffix_size ||
      prefix_size + identity_size + suffix_size != (size_t)actual.size ||
      memcmp(actual.data, prefix, prefix_size) != 0 ||
      memcmp(actual.data + prefix_size + identity_size, suffix,
             suffix_size) != 0) {
    return 0;
  }
  for (index = 0u; index < identity_size; index++) {
    unsigned char character = (unsigned char)identity[index];
    char expected = ((character >= (unsigned char)'a' &&
                      character <= (unsigned char)'z') ||
                     (character >= (unsigned char)'A' &&
                      character <= (unsigned char)'Z') ||
                     (character >= (unsigned char)'0' &&
                      character <= (unsigned char)'9'))
                        ? (char)character
                        : '_';
    if (actual.data[prefix_size + index] != expected) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_string_equals(ctool_string_t actual,
                                     const char *expected);

int cupidbuild_validate_compiler_object_bytes(const unsigned char *bytes,
                                              size_t size) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_elf32_object_t object;
  ctool_u32 index;
  unsigned int required = 0u;
  int valid = 0;
  if (bytes == (const unsigned char *)0 || size > 4294967295u ||
      ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
    return 0;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  if (ctool_job_open(&config, &job) != CTOOL_OK) {
    return 0;
  }
  source.path.text = ctool_string("/compiler-candidate.o");
  source.contents = ctool_bytes(bytes, (ctool_u32)size);
  if (ctool_elf32_read(job, &source, &object) != CTOOL_OK ||
      object.file_type != CTOOL_ELF32_ET_REL ||
      object.program_header_count != 0u || object.symbol_count == 0u ||
      bytes[42] != 0u || bytes[43] != 0u) {
    goto done;
  }
  for (index = 0u; index < object.section_count; index++) {
    const ctool_elf32_section_t *section = &object.sections[index];
    ctool_u32 character;
    if (section->type == 4u ||
        (section->type == 9u &&
         object.sections[section->info].type == CTOOL_ELF32_SHT_NOBITS) ||
        (section->type != CTOOL_ELF32_SHT_NOBITS &&
         section->alignment > 1u &&
         section->file_offset % section->alignment != 0u)) {
      goto done;
    }
    for (character = 0u; character < section->name.size; character++) {
      if ((unsigned char)section->name.data[character] >= 128u) {
        goto done;
      }
    }
    if (cupidbuild_string_equals(section->name, ".symtab")) {
      if (section->type != 2u) {
        goto done;
      }
      required |= 1u;
    } else if (cupidbuild_string_equals(section->name, ".strtab")) {
      if (section->type != 3u) {
        goto done;
      }
      required |= 2u;
    } else if (cupidbuild_string_equals(section->name, ".shstrtab")) {
      if (section->type != 3u) {
        goto done;
      }
      required |= 4u;
    }
  }
  for (index = 0u; index < object.symbol_count; index++) {
    if (object.symbols[index].placement == CTOOL_ELF32_SYMBOL_RESERVED) {
      goto done;
    }
  }
  for (index = 0u; index < object.relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation = &object.relocations[index];
    if ((relocation->type != CTOOL_ELF32_R_386_32 &&
         relocation->type != CTOOL_ELF32_R_386_PC32) ||
        relocation->addend_known == CTOOL_FALSE ||
        (relocation->type == CTOOL_ELF32_R_386_PC32 &&
         relocation->addend != -4)) {
      goto done;
    }
  }
  valid = required == 7u;
done:
  ctool_job_close(job);
  return valid;
}

static ctool_u32 cupidbuild_user_elf_u16(const unsigned char *bytes) {
  return (ctool_u32)bytes[0] | ((ctool_u32)bytes[1] << 8u);
}

static ctool_u32 cupidbuild_user_elf_u32(const unsigned char *bytes) {
  return cupidbuild_user_elf_u16(bytes) |
         (cupidbuild_user_elf_u16(bytes + 2u) << 16u);
}

static int cupidbuild_user_elf_failure(char *reason, size_t capacity,
                                      const char *format,
                                      unsigned int index) {
  (void)snprintf(reason, capacity, format, index);
  return 0;
}

int cupidbuild_validate_user_executable_bytes(const unsigned char *bytes,
                                             size_t size, char *reason,
                                             size_t reason_capacity) {
  ctool_u32 entry;
  ctool_u32 program_offset;
  ctool_u32 program_count;
  ctool_u32 starts[16];
  ctool_u32 ends[16];
  unsigned int load_count = 0u;
  unsigned int index = 0u;
  int entry_is_executable = 0;
  if (reason == (char *)0 || reason_capacity == 0u) {
    return 0;
  }
  reason[0] = '\0';
  if (bytes == (const unsigned char *)0 || size < 52u) {
    return cupidbuild_user_elf_failure(reason, reason_capacity,
        "ELF header is outside the linked executable", index);
  }
  if (memcmp(bytes, "\177ELF\001\001\001", 7u) != 0) {
    return cupidbuild_user_elf_failure(reason, reason_capacity,
        "linked executable is not little-endian ELF32 version 1", index);
  }
  if (cupidbuild_user_elf_u16(bytes + 16u) != 2u ||
      cupidbuild_user_elf_u16(bytes + 18u) != 3u ||
      cupidbuild_user_elf_u32(bytes + 20u) != 1u) {
    return cupidbuild_user_elf_failure(reason, reason_capacity,
        "linked executable is not an i386 ELF32 executable", index);
  }
  entry = cupidbuild_user_elf_u32(bytes + 24u);
  program_offset = cupidbuild_user_elf_u32(bytes + 28u);
  program_count = cupidbuild_user_elf_u16(bytes + 44u);
  if (cupidbuild_user_elf_u16(bytes + 40u) != 52u ||
      cupidbuild_user_elf_u16(bytes + 42u) != 32u || program_count == 0u) {
    return cupidbuild_user_elf_failure(reason, reason_capacity,
        "linked executable has an invalid program table", index);
  }
  if (program_count > 16u) {
    return cupidbuild_user_elf_failure(reason, reason_capacity,
        "linked executable has more than 16 program headers", index);
  }
  if (program_offset < 52u || program_offset > 0x7fffffffu) {
    return cupidbuild_user_elf_failure(reason, reason_capacity,
        "linked executable has an invalid program-header offset", index);
  }
  if ((size_t)program_offset > size ||
      program_count * 32u > size - (size_t)program_offset) {
    return cupidbuild_user_elf_failure(reason, reason_capacity,
        "linked executable has a truncated program table", index);
  }
  for (index = 0u; index < program_count; index++) {
    const unsigned char *header = bytes + program_offset + index * 32u;
    ctool_u32 type = cupidbuild_user_elf_u32(header);
    ctool_u32 offset = cupidbuild_user_elf_u32(header + 4u);
    ctool_u32 address = cupidbuild_user_elf_u32(header + 8u);
    ctool_u32 file_size = cupidbuild_user_elf_u32(header + 16u);
    ctool_u32 memory_size = cupidbuild_user_elf_u32(header + 20u);
    ctool_u32 flags = cupidbuild_user_elf_u32(header + 24u);
    ctool_u32 alignment = cupidbuild_user_elf_u32(header + 28u);
    ctool_u32 file_end;
    ctool_u32 memory_end;
    unsigned int previous;
    if (type != 0u && type != 1u && type != 0x6474e551u) {
      return cupidbuild_user_elf_failure(reason, reason_capacity,
          "program header %u has an unsupported program type", index);
    }
    if ((flags & ~7u) != 0u) {
      return cupidbuild_user_elf_failure(reason, reason_capacity,
          "program header %u has unknown permission flags", index);
    }
    if (alignment != 0u && (alignment & (alignment - 1u)) != 0u) {
      return cupidbuild_user_elf_failure(reason, reason_capacity,
          "program header %u alignment is not a power of two", index);
    }
    if (type != 1u) {
      if (file_size != 0u || memory_size != 0u) {
        return cupidbuild_user_elf_failure(reason, reason_capacity,
            "non-load program header has a payload at index %u", index);
      }
      continue;
    }
    if (alignment > 1u &&
        (offset & (alignment - 1u)) != (address & (alignment - 1u))) {
      return cupidbuild_user_elf_failure(reason, reason_capacity,
          "load segment %u alignment is incongruent", index);
    }
    if (file_size > memory_size) {
      return cupidbuild_user_elf_failure(reason, reason_capacity,
          "load segment %u has more file bytes than memory bytes", index);
    }
    if (offset > 0xffffffffu - file_size) {
      return cupidbuild_user_elf_failure(reason, reason_capacity,
          "load file range overflows the i386 address space", index);
    }
    if (address > 0xffffffffu - memory_size) {
      return cupidbuild_user_elf_failure(reason, reason_capacity,
          "load memory range overflows the i386 address space", index);
    }
    file_end = offset + file_size;
    memory_end = address + memory_size;
    if ((size_t)file_end > size) {
      return cupidbuild_user_elf_failure(reason, reason_capacity,
          "load segment %u extends beyond the executable", index);
    }
    if (file_size > 0u && offset > 0x7fffffffu) {
      return cupidbuild_user_elf_failure(reason, reason_capacity,
          "load segment %u cannot be reached by the loader", index);
    }
    if (memory_size == 0u) {
      continue;
    }
    if (address < 0x01c00000u || memory_end > 0x01e00000u) {
      return cupidbuild_user_elf_failure(reason, reason_capacity,
          "load segment %u is outside the external executable arena", index);
    }
    for (previous = 0u; previous < load_count; previous++) {
      if (address < ends[previous] && starts[previous] < memory_end) {
        return cupidbuild_user_elf_failure(reason, reason_capacity,
            "linked executable load segments overlap", index);
      }
    }
    starts[load_count] = address;
    ends[load_count] = memory_end;
    load_count++;
    if ((flags & 1u) != 0u && address <= entry &&
        entry - address < file_size) {
      entry_is_executable = 1;
    }
  }
  if (load_count == 0u) {
    return cupidbuild_user_elf_failure(reason, reason_capacity,
        "linked executable has no nonempty loadable segment", index);
  }
  if (!entry_is_executable) {
    return cupidbuild_user_elf_failure(reason, reason_capacity,
        "entry point is not in executable file-backed bytes", index);
  }
  return 1;
}

int cupidbuild_validate_jpeg_object_bytes(
    const unsigned char *object_bytes, size_t object_size,
    const unsigned char *jpeg_bytes, size_t jpeg_size,
    const char *source_identity) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_elf32_object_t object;
  const ctool_elf32_section_t *data_section =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_symbol_t *start_symbol =
      (const ctool_elf32_symbol_t *)0;
  const ctool_elf32_symbol_t *end_symbol =
      (const ctool_elf32_symbol_t *)0;
  const ctool_elf32_symbol_t *size_symbol =
      (const ctool_elf32_symbol_t *)0;
  ctool_u32 index;
  int extra_allocated_data = 0;
  int valid = 0;
  if (object_bytes == (const unsigned char *)0 ||
      jpeg_bytes == (const unsigned char *)0 ||
      source_identity == (const char *)0 || source_identity[0] == '\0' ||
      object_size > 4294967295u || jpeg_size > 4294967295u ||
      ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
    return 0;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  if (ctool_job_open(&config, &job) != CTOOL_OK) {
    return 0;
  }
  source.path.text = ctool_string("/candidate.o");
  source.contents = ctool_bytes(object_bytes, (ctool_u32)object_size);
  if (ctool_elf32_read(job, &source, &object) != CTOOL_OK ||
      object.file_type != CTOOL_ELF32_ET_REL) {
    goto done;
  }
  for (index = 0u; index < object.section_count; index++) {
    const ctool_elf32_section_t *section = &object.sections[index];
    if (section->name.size == 5u &&
        memcmp(section->name.data, ".data", 5u) == 0) {
      if (data_section != (const ctool_elf32_section_t *)0) {
        goto done;
      }
      data_section = section;
    } else if ((section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u &&
               section->size != 0u) {
      extra_allocated_data = 1;
    }
  }
  for (index = 0u; index < object.symbol_count; index++) {
    const ctool_elf32_symbol_t *symbol = &object.symbols[index];
    if (cupidbuild_jpeg_symbol_name_matches(symbol->name, source_identity,
                                            "_start")) {
      if (start_symbol != (const ctool_elf32_symbol_t *)0) {
        goto done;
      }
      start_symbol = symbol;
    } else if (cupidbuild_jpeg_symbol_name_matches(
                   symbol->name, source_identity, "_end")) {
      if (end_symbol != (const ctool_elf32_symbol_t *)0) {
        goto done;
      }
      end_symbol = symbol;
    } else if (cupidbuild_jpeg_symbol_name_matches(
                   symbol->name, source_identity, "_size")) {
      if (size_symbol != (const ctool_elf32_symbol_t *)0) {
        goto done;
      }
      size_symbol = symbol;
    }
  }
  if (object.symbol_count != 4u ||
      data_section == (const ctool_elf32_section_t *)0 ||
      data_section->type != CTOOL_ELF32_SHT_PROGBITS ||
      data_section->flags !=
          (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE) ||
      data_section->alignment != 1u || data_section->entry_size != 0u ||
      data_section->size != (ctool_u32)jpeg_size ||
      data_section->contents.size != (ctool_u32)jpeg_size ||
      (jpeg_size != 0u &&
       memcmp(data_section->contents.data, jpeg_bytes, jpeg_size) != 0) ||
      extra_allocated_data != 0 || object.relocation_count != 0u ||
      start_symbol == (const ctool_elf32_symbol_t *)0 ||
      end_symbol == (const ctool_elf32_symbol_t *)0 ||
      size_symbol == (const ctool_elf32_symbol_t *)0 ||
      start_symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
      start_symbol->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      start_symbol->visibility != CTOOL_ELF32_VIS_DEFAULT ||
      start_symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      start_symbol->section_file_index != data_section->file_index ||
      start_symbol->value != 0u || start_symbol->size != 0u ||
      end_symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
      end_symbol->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      end_symbol->visibility != CTOOL_ELF32_VIS_DEFAULT ||
      end_symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      end_symbol->section_file_index != data_section->file_index ||
      end_symbol->value != (ctool_u32)jpeg_size || end_symbol->size != 0u ||
      size_symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
      size_symbol->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      size_symbol->visibility != CTOOL_ELF32_VIS_DEFAULT ||
      size_symbol->placement != CTOOL_ELF32_SYMBOL_ABSOLUTE ||
      size_symbol->section_file_index != CTOOL_ELF32_NO_SECTION ||
      size_symbol->value != (ctool_u32)jpeg_size || size_symbol->size != 0u) {
    goto done;
  }
  valid = 1;

done:
  ctool_job_close(job);
  return valid;
}

static int cupidbuild_jpeg_frame_marker(unsigned char marker) {
  switch (marker) {
  case 0xc0u:
  case 0xc1u:
  case 0xc2u:
  case 0xc3u:
  case 0xc5u:
  case 0xc6u:
  case 0xc7u:
  case 0xc9u:
  case 0xcau:
  case 0xcbu:
  case 0xcdu:
  case 0xceu:
  case 0xcfu:
    return 1;
  default:
    return 0;
  }
}

static int cupidbuild_jpeg_failure(char *reason, size_t reason_capacity,
                                   const char *message) {
  if (reason != (char *)0 && reason_capacity != 0u) {
    (void)snprintf(reason, reason_capacity, "%s", message);
  }
  return 0;
}

static int cupidbuild_jpeg_marker_failure(char *reason,
                                          size_t reason_capacity,
                                          const char *prefix,
                                          unsigned char marker,
                                          const char *suffix) {
  if (reason != (char *)0 && reason_capacity != 0u) {
    (void)snprintf(reason, reason_capacity, "%s0x%02x%s", prefix,
                   (unsigned int)marker, suffix);
  }
  return 0;
}

int cupidbuild_validate_jpeg_bytes(const unsigned char *bytes, size_t size,
                                   char *reason, size_t reason_capacity) {
  size_t offset = 2u;
  unsigned char frame_marker = 0u;
  int saw_scan = 0;
  int saw_eoi = 0;
  if (reason == (char *)0 || reason_capacity == 0u) {
    return 0;
  }
  reason[0] = '\0';
  if (bytes == (const unsigned char *)0 || size < 2u || bytes[0] != 0xffu ||
      bytes[1] != 0xd8u) {
    return cupidbuild_jpeg_failure(reason, reason_capacity,
                                   "JPEG input has no SOI marker");
  }
  while (offset < size) {
    size_t segment_size;
    unsigned char marker;
    if (bytes[offset] != 0xffu) {
      return cupidbuild_jpeg_failure(
          reason, reason_capacity,
          "JPEG marker stream is malformed outside a scan");
    }
    while (offset < size && bytes[offset] == 0xffu) {
      offset++;
    }
    if (offset >= size) {
      break;
    }
    marker = bytes[offset++];
    if (marker == 0x00u) {
      return cupidbuild_jpeg_failure(
          reason, reason_capacity,
          "JPEG marker stream contains stuffed data before a scan");
    }
    if (marker == 0xd9u) {
      saw_eoi = 1;
      if (offset != size) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG input has trailing bytes after the EOI marker");
      }
      break;
    }
    if (marker == 0x01u || marker == 0xd8u ||
        (marker >= 0xd0u && marker <= 0xd7u)) {
      if (marker != 0x01u) {
        return cupidbuild_jpeg_marker_failure(
            reason, reason_capacity, "unexpected standalone JPEG marker ",
            marker, "");
      }
      continue;
    }
    if (size - offset < 2u) {
      return cupidbuild_jpeg_failure(reason, reason_capacity,
                                     "JPEG marker length is truncated");
    }
    segment_size = ((size_t)bytes[offset] << 8u) |
                   (size_t)bytes[offset + 1u];
    if (segment_size < 2u || segment_size > size - offset) {
      return cupidbuild_jpeg_failure(reason, reason_capacity,
                                     "JPEG marker length is invalid");
    }
    if (cupidbuild_jpeg_frame_marker(marker)) {
      size_t component_count;
      if (frame_marker != 0u) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG input contains more than one frame header");
      }
      if (segment_size < 8u) {
        return cupidbuild_jpeg_failure(reason, reason_capacity,
                                       "JPEG frame header is truncated");
      }
      component_count = (size_t)bytes[offset + 7u];
      if (component_count == 0u ||
          segment_size != 8u + 3u * component_count) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG frame header has an invalid component table");
      }
      if (bytes[offset + 2u] == 0u) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG frame header has an invalid sample precision");
      }
      if ((bytes[offset + 3u] == 0u && bytes[offset + 4u] == 0u) ||
          (bytes[offset + 5u] == 0u && bytes[offset + 6u] == 0u)) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG frame header has an invalid image size");
      }
      frame_marker = marker;
    }
    if (marker == 0xdau) {
      size_t scan_components;
      if (frame_marker == 0u) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG scan appears before its frame header");
      }
      if (segment_size < 6u) {
        return cupidbuild_jpeg_failure(reason, reason_capacity,
                                       "JPEG scan header is truncated");
      }
      scan_components = (size_t)bytes[offset + 2u];
      if (scan_components == 0u ||
          segment_size != 6u + 2u * scan_components) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG scan header has an invalid component table");
      }
      saw_scan = 1;
      offset += segment_size;
      while (offset < size) {
        size_t scan_marker_offset;
        unsigned char scan_marker;
        if (bytes[offset] != 0xffu) {
          offset++;
          continue;
        }
        scan_marker_offset = offset;
        while (offset < size && bytes[offset] == 0xffu) {
          offset++;
        }
        if (offset >= size) {
          return cupidbuild_jpeg_failure(
              reason, reason_capacity,
              "JPEG entropy data ends with a partial marker");
        }
        scan_marker = bytes[offset++];
        if (scan_marker == 0x00u ||
            (scan_marker >= 0xd0u && scan_marker <= 0xd7u)) {
          continue;
        }
        offset = scan_marker_offset;
        break;
      }
      continue;
    }
    offset += segment_size;
  }
  if (frame_marker == 0xc2u) {
    return cupidbuild_jpeg_failure(
        reason, reason_capacity,
        "unsupported progressive JPEG frame; check in a baseline SOF0/SOF1 "
        "asset");
  }
  if (frame_marker != 0xc0u && frame_marker != 0xc1u) {
    if (frame_marker == 0u) {
      return cupidbuild_jpeg_failure(
          reason, reason_capacity,
          "JPEG input has no supported SOF0/SOF1 frame");
    }
    return cupidbuild_jpeg_marker_failure(
        reason, reason_capacity, "unsupported JPEG frame marker ",
        frame_marker, "; check in a baseline SOF0/SOF1 asset");
  }
  if (!saw_scan) {
    return cupidbuild_jpeg_failure(reason, reason_capacity,
                                   "JPEG input has no scan");
  }
  if (!saw_eoi) {
    return cupidbuild_jpeg_failure(reason, reason_capacity,
                                   "JPEG input has no EOI marker");
  }
  return 1;
}

static int cupidbuild_string_equals(ctool_string_t actual,
                                    const char *expected) {
  size_t expected_size = strlen(expected);
  return expected_size <= 4294967295u &&
         actual.size == (ctool_u32)expected_size &&
         memcmp(actual.data, expected, expected_size) == 0;
}

static int cupidbuild_validate_execution_profile(
                                                   cupidbuild_host_transaction_t *transaction,
                                                   const char *path,
                                                   size_t artifact_index,
                                                   int promoted,
                                                   int current_windows_plan) {
  unsigned char *bytes;
  size_t size = 0u;
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  int valid = 0;
  bytes = cupidbuild_host_read_frozen_input(
      transaction, path, CUPIDBUILD_TOOL_BYTES, &size);
  if (bytes == (unsigned char *)0 || size > 4294967295u ||
      ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
    free(bytes);
    return 0;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  if (ctool_job_open(&config, &job) != CTOOL_OK) {
    free(bytes);
    return 0;
  }
  source.path.text = ctool_string("/checked-seed-tool");
  source.contents = ctool_bytes(bytes, (ctool_u32)size);
#if defined(_WIN32)
  {
    static const char *const ordinary_seed_imports[] = {
        "CloseHandle",       "CreateFileA",      "ExitProcess",
        "GetCommandLineA",   "GetCurrentDirectoryA",
        "GetLastError",      "GetStdHandle",     "ReadFile",
        "SetFilePointer",    "VirtualAlloc",     "VirtualFree",
        "WriteFile"};
    static const char *const ordinary_current_imports[] = {
        "CloseHandle",       "CreateFileA",      "ExitProcess",
        "GetCommandLineA",   "GetCurrentDirectoryA",
        "GetFileInformationByHandle", "GetLastError", "GetStdHandle",
        "ReadFile",          "SetFilePointer",   "VirtualAlloc",
        "VirtualFree",       "WriteFile"};
    static const char *const linker_seed_imports[] = {
        "CloseHandle",       "CreateFileA",       "DeleteFileA",
        "ExitProcess",       "FlushFileBuffers",  "GetCommandLineA",
        "GetCurrentDirectoryA", "GetFullPathNameA", "GetLastError",
        "GetStdHandle",      "MoveFileExA",       "ReadFile",
        "SetFilePointer",    "VirtualAlloc",      "VirtualFree",
        "WriteFile"};
    static const char *const linker_current_imports[] = {
        "CloseHandle",       "CreateFileA",       "DeleteFileA",
        "ExitProcess",       "FlushFileBuffers",  "GetCommandLineA",
        "GetCurrentDirectoryA", "GetFileInformationByHandle",
        "GetFullPathNameA",  "GetLastError",      "GetStdHandle",
        "MoveFileExA",       "ReadFile",          "SetFilePointer",
        "VirtualAlloc",      "VirtualFree",       "WriteFile"};
    static const char *const cupidbuild_legacy_imports[] = {
        "CloseHandle",
        "CreateDirectoryA",
        "CreateFileA",
        "CreateProcessA",
        "DeleteFileA",
        "ExitProcess",
        "FindClose",
        "FindFirstFileA",
        "FindNextFileA",
        "FlushFileBuffers",
        "GetCommandLineA",
        "GetCurrentDirectoryA",
        "GetCurrentProcessId",
        "GetExitCodeProcess",
        "GetFileAttributesA",
        "GetFileInformationByHandle",
        "GetFullPathNameA",
        "GetLastError",
        "GetStdHandle",
        "MoveFileExA",
        "OpenProcess",
        "ReadFile",
        "RemoveDirectoryA",
        "SetFilePointer",
        "TerminateProcess",
        "VirtualAlloc",
        "VirtualFree",
        "WaitForSingleObject",
        "WriteFile"};
    static const char *const cupidbuild_current_imports[] = {
        "CloseHandle",
        "CreateDirectoryA",
        "CreateFileA",
        "CreateProcessA",
        "DeleteFileA",
        "DeleteProcThreadAttributeList",
        "ExitProcess",
        "FindClose",
        "FindFirstFileA",
        "FindNextFileA",
        "FlushFileBuffers",
        "GetCommandLineA",
        "GetCurrentDirectoryA",
        "GetCurrentProcessId",
        "GetExitCodeProcess",
        "GetFileAttributesA",
        "GetFileInformationByHandle",
        "GetFullPathNameA",
        "GetLastError",
        "GetStdHandle",
        "InitializeProcThreadAttributeList",
        "MoveFileExA",
        "OpenProcess",
        "ReadFile",
        "RemoveDirectoryA",
        "SetFilePointer",
        "SetHandleInformation",
        "TerminateProcess",
        "UpdateProcThreadAttribute",
        "VirtualAlloc",
        "VirtualFree",
        "WaitForSingleObject",
        "WriteFile"};
    static const char *const cupidbuild_legacy_ntdll_imports[] = {
        "NtSetInformationFile"};
    static const char *const cupidbuild_current_ntdll_imports[] = {
        "NtCreateFile", "NtQueryDirectoryFile", "NtSetInformationFile"};
    const char *const *expected_imports =
        current_windows_plan ? ordinary_current_imports
                             : ordinary_seed_imports;
    const char *const *expected_ntdll_imports =
        (const char *const *)0;
    size_t expected_count =
        current_windows_plan
            ? sizeof(ordinary_current_imports) /
                  sizeof(ordinary_current_imports[0])
            : sizeof(ordinary_seed_imports) / sizeof(ordinary_seed_imports[0]);
    size_t expected_ntdll_count = 0u;
    size_t expected_library_count = artifact_index == 5u ? 2u : 1u;
    ctool_pe32_image_t image;
    ctool_u32 index;
    if ((promoted && artifact_index == 0u) || artifact_index == 3u) {
      expected_imports = current_windows_plan ? linker_current_imports
                                              : linker_seed_imports;
      expected_count =
          current_windows_plan
              ? sizeof(linker_current_imports) /
                    sizeof(linker_current_imports[0])
              : sizeof(linker_seed_imports) / sizeof(linker_seed_imports[0]);
    }
    if (ctool_pe32_read(job, &source, &image) == CTOOL_OK &&
        image.entry_point == 0x00401000u &&
        image.import_library_count == (ctool_u32)expected_library_count &&
        cupidbuild_string_equals(image.import_libraries[0].name,
                                 "KERNEL32.dll")) {
      valid = 1;
      if (artifact_index == 5u) {
        size_t legacy_count =
            sizeof(cupidbuild_legacy_imports) /
            sizeof(cupidbuild_legacy_imports[0]);
        size_t legacy_ntdll_count =
            sizeof(cupidbuild_legacy_ntdll_imports) /
            sizeof(cupidbuild_legacy_ntdll_imports[0]);
        size_t current_count =
            sizeof(cupidbuild_current_imports) /
            sizeof(cupidbuild_current_imports[0]);
        size_t current_ntdll_count =
            sizeof(cupidbuild_current_ntdll_imports) /
            sizeof(cupidbuild_current_ntdll_imports[0]);
        if (current_windows_plan) {
          expected_imports = cupidbuild_current_imports;
          expected_count = current_count;
          expected_ntdll_imports = cupidbuild_current_ntdll_imports;
          expected_ntdll_count = current_ntdll_count;
        } else {
          expected_imports = cupidbuild_legacy_imports;
          expected_count = legacy_count;
          expected_ntdll_imports = cupidbuild_legacy_ntdll_imports;
          expected_ntdll_count = legacy_ntdll_count;
        }
        if (!cupidbuild_string_equals(image.import_libraries[1].name,
                                      "NTDLL.dll") ||
            (size_t)image.import_count !=
                expected_count + expected_ntdll_count) {
          valid = 0;
        }
      } else if ((size_t)image.import_count != expected_count) {
        valid = 0;
      }
      for (index = 0u; valid && index < (ctool_u32)expected_count; index++) {
        if (!cupidbuild_string_equals(image.imports[index].library_name,
                                      "KERNEL32.dll") ||
            !cupidbuild_string_equals(image.imports[index].procedure_name,
                                      expected_imports[index])) {
          valid = 0;
          break;
        }
      }
      if (valid && artifact_index == 5u) {
        for (index = 0u;
             valid && index < (ctool_u32)expected_ntdll_count; index++) {
          if (!cupidbuild_string_equals(
                  image.imports[expected_count + index].library_name,
                  "NTDLL.dll") ||
              !cupidbuild_string_equals(
                  image.imports[expected_count + index].procedure_name,
                  expected_ntdll_imports[index])) {
            valid = 0;
          }
        }
      }
    }
  }
#else
  {
    ctool_elf32_object_t object;
    ctool_u32 index;
    ctool_u32 load_count = 0u;
    int entry_in_code = 0;
    (void)artifact_index;
    (void)promoted;
    (void)current_windows_plan;
    if (ctool_elf32_read(job, &source, &object) == CTOOL_OK &&
        object.file_type == CTOOL_ELF32_ET_EXEC &&
        object.entry_point == 0x08048000u &&
        object.program_header_count != 0u) {
      valid = 1;
      for (index = 0u; index < object.program_header_count; index++) {
        const ctool_elf32_program_header_t *header =
            &object.program_headers[index];
        if (header->type == CTOOL_ELF32_PT_DYNAMIC ||
            header->type == CTOOL_ELF32_PT_INTERP) {
          valid = 0;
          break;
        }
        if (header->type != CTOOL_ELF32_PT_LOAD) {
          continue;
        }
        load_count++;
        if ((header->flags & (CTOOL_ELF32_PF_W | CTOOL_ELF32_PF_X)) ==
            (CTOOL_ELF32_PF_W | CTOOL_ELF32_PF_X)) {
          valid = 0;
          break;
        }
        if ((header->flags & CTOOL_ELF32_PF_X) != 0u &&
            object.entry_point >= header->virtual_address &&
            object.entry_point - header->virtual_address < header->file_size) {
          entry_in_code = 1;
        }
      }
      if (load_count == 0u || entry_in_code == 0) {
        valid = 0;
      }
    }
  }
#endif
  ctool_job_close(job);
  free(bytes);
  return valid;
}

static void cupidbuild_seed_capture_close(cupidbuild_seed_capture_t *seed) {
  if (seed != (cupidbuild_seed_capture_t *)0) {
    free(seed->manifest);
    seed->manifest = (unsigned char *)0;
  }
}

static int cupidbuild_seed_manifest_path(
    const char *working_directory, const char *requested, int require_inside,
    char *resolved, size_t capacity) {
  size_t requested_size = strlen(requested);
  int absolute = requested[0] == '/' || requested[0] == '\\' ||
                 (requested_size > 1u && requested[1] == ':');
  if (absolute != 0) {
    if ((require_inside != 0 &&
         (requested_size <= strlen(working_directory) ||
          !cupidbuild_repository_prefix(requested, working_directory))) ||
        requested_size + 1u > capacity) {
      return 0;
    }
    (void)memcpy(resolved, requested, requested_size + 1u);
    return 1;
  }
  return cupidbuild_join(resolved, capacity, working_directory, requested);
}

static int cupidbuild_seed_freeze(
    cupidbuild_host_transaction_t *transaction, const char *working_directory,
    const char *requested_manifest, int require_inside, int require_promoted,
    cupidbuild_seed_capture_t *seed) {
  const char *frozen_manifest = (const char *)0;
  const char *manifest_reason = "manifest path is invalid";
  size_t index;
  (void)memset(seed, 0, sizeof(*seed));
  if (!cupidbuild_seed_manifest_path(
          working_directory, requested_manifest, require_inside,
          seed->manifest_path, sizeof(seed->manifest_path))) {
    (void)fprintf(stderr,
                  "cupidbuild: checked seed manifest is outside the working "
                  "directory\n");
    return 0;
  }
  if (!cupidbuild_host_freeze_input(
          transaction, seed->manifest_path, "manifest.json", &frozen_manifest,
          (cupidbuild_host_snapshot_t *)0)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    return 0;
  }
  seed->manifest = cupidbuild_host_read_frozen_input(
      transaction, frozen_manifest, CUPIDBUILD_MANIFEST_BYTES,
      &seed->manifest_size);
  if (seed->manifest == (unsigned char *)0) {
    (void)fprintf(
        stderr,
        "cupidbuild: checked seed manifest is invalid: unreadable bytes\n");
    return 0;
  }
  if (!cupidbuild_json_manifest(seed->manifest, seed->manifest_size,
                                 seed->artifacts, &seed->artifact_count,
                                 &seed->current_windows_plan,
                                 &manifest_reason)) {
    (void)fprintf(stderr, "cupidbuild: checked seed manifest is invalid: %s\n",
                  manifest_reason);
    return 0;
  }
  if (require_promoted != 0 &&
      seed->artifact_count != CUPIDBUILD_SEED_ARTIFACTS) {
    (void)fprintf(stderr,
                  "cupidbuild: checked tool runner requires a promoted "
                  "six-tool seed\n");
    return 0;
  }
  if (!cupidbuild_manifest_directory(seed->manifest_path, seed->directory,
                                     sizeof(seed->directory))) {
    (void)fprintf(stderr,
                  "cupidbuild: checked seed manifest is invalid: artifact "
                  "path is invalid\n");
    return 0;
  }
  for (index = 0u; index < seed->artifact_count; index++) {
    seed->expected_files[index] = seed->artifacts[index].file;
  }
  if (!cupidbuild_host_seed_members_exact(
          transaction, seed->directory,
#if defined(_WIN32)
          ".exe",
#else
          ".elf",
#endif
          seed->expected_files, seed->artifact_count)) {
    (void)fprintf(stderr,
                  "cupidbuild: checked seed directory contains an unlisted "
                  "executable file\n");
    return 0;
  }
  for (index = 0u; index < seed->artifact_count; index++) {
    char live_path[CUPIDBUILD_PATH_BYTES];
    if (!cupidbuild_join(live_path, sizeof(live_path), seed->directory,
                         seed->artifacts[index].file) ||
        !cupidbuild_host_freeze_input(
            transaction, live_path, seed->artifacts[index].file,
            &seed->frozen_tools[index], &seed->tool_snapshots[index]) ||
        !cupidbuild_host_make_input_executable(
            transaction, seed->frozen_tools[index]) ||
        !cupidbuild_artifact_matches(&seed->tool_snapshots[index],
                                     &seed->artifacts[index])) {
      (void)fprintf(stderr, "cupidbuild: checked %s digest mismatch\n",
                    cupidbuild_seed_names[index]);
      return 0;
    }
  }
  for (index = 0u; index < seed->artifact_count; index++) {
    if (!cupidbuild_validate_execution_profile(
            transaction, seed->frozen_tools[index], index,
            seed->artifact_count == CUPIDBUILD_SEED_ARTIFACTS,
            seed->current_windows_plan)) {
      (void)fprintf(stderr,
                    "cupidbuild: checked seed execution profile mismatch\n");
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_seed_require_live(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_seed_capture_t *seed) {
  if (!cupidbuild_host_seed_members_exact(
          transaction, seed->directory,
#if defined(_WIN32)
          ".exe",
#else
          ".elf",
#endif
          seed->expected_files, seed->artifact_count)) {
    (void)fprintf(stderr,
                  "cupidbuild: checked seed directory membership changed "
                  "while checked tools ran\n");
    return 0;
  }
  if (!cupidbuild_host_require_inputs(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    return 0;
  }
  return 1;
}

typedef enum {
  CUPIDBUILD_ASSEMBLY_OBJECT,
  CUPIDBUILD_ASSEMBLY_BOOTLOADER,
  CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE,
  CUPIDBUILD_ASSEMBLY_ISO_PATTERN
} cupidbuild_assembly_kind_t;

static const unsigned char cupidbuild_iso_pattern_map[] =
    "cupid.raw-map.v2\n"
    "size 4096\n"
    "base 0x00000000\n"
    "edges 0\n"
    "range 0x00000000 data\n";

static const unsigned char cupidbuild_smp_trampoline_map[] =
    "cupid.raw-map.v2\n"
    "size 4096\n"
    "base 0x00008000\n"
    "edges 6\n"
    "range 0x00000000 code16\n"
    "range 0x0000001f data\n"
    "range 0x00000210 code32\n"
    "range 0x00000254 data\n"
    "edge 0x00000017 far local 0x00000210 0x00008210 32 0x00000008\n"
    "edge 0x0000022f relative local 0x0000023a 0x0000823a 32 0x00000000\n"
    "edge 0x00000235 relative local 0x00000229 0x00008229 32 0x00000000\n"
    "edge 0x00000238 relative local 0x00000237 0x00008237 32 0x00000000\n"
    "edge 0x00000250 indirect unprovable - - unknown -\n"
    "edge 0x00000252 relative local 0x00000237 0x00008237 32 0x00000000\n";

static int cupidbuild_assemble(
    const cupidbuild_assembly_request_t *request,
    cupidbuild_assembly_kind_t kind) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_seed_capture_t seed;
  const char *frozen_assembler = (const char *)0;
  const char *frozen_inspector = (const char *)0;
  cupidbuild_host_snapshot_t candidate_snapshot;
  cupidbuild_host_snapshot_t map_snapshot;
  unsigned char *candidate = (unsigned char *)0;
  unsigned char *map = (unsigned char *)0;
  const char *assembler_arguments[9];
  const char *inspector_arguments[9];
  int assembler_status;
  int inspector_status;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  if (request == (const cupidbuild_assembly_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->source, 1) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0)) {
    (void)fprintf(stderr, "cupidbuild: invalid guarded assembly request\n");
    return 1;
  }
  if (!cupidbuild_host_transaction_open(request->repository_root,
                                        request->source, request->output,
                                        &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_seed_freeze(transaction, request->repository_root,
                              request->seed_manifest, 1, 0, &seed)) {
    goto done;
  }
  frozen_assembler = seed.frozen_tools[0];
  frozen_inspector = seed.frozen_tools[2];
  assembler_arguments[0] = "--caller-owned-output";
  assembler_arguments[1] = "-f";
  assembler_arguments[2] =
      kind == CUPIDBUILD_ASSEMBLY_OBJECT ? "elf32" : "bin";
  if (kind == CUPIDBUILD_ASSEMBLY_OBJECT) {
    assembler_arguments[3] = "-o";
    assembler_arguments[4] = cupidbuild_host_candidate(transaction);
    assembler_arguments[5] = cupidbuild_host_frozen_source(transaction);
    assembler_arguments[6] = (const char *)0;
  } else {
    assembler_arguments[3] = "--map";
    assembler_arguments[4] = cupidbuild_host_private_output(transaction);
    assembler_arguments[5] = "-o";
    assembler_arguments[6] = cupidbuild_host_candidate(transaction);
    assembler_arguments[7] = cupidbuild_host_frozen_source(transaction);
    assembler_arguments[8] = (const char *)0;
  }
  assembler_status = cupidbuild_host_run(
      transaction, frozen_assembler, assembler_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (assembler_status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidASM failed\n");
    goto done;
  }
  if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot,
                                         &candidate)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (kind == CUPIDBUILD_ASSEMBLY_OBJECT) {
    if (!cupidbuild_validate_relocatable(candidate, candidate_snapshot.size,
                                         1)) {
      (void)fprintf(
          stderr,
          "cupidbuild: checked CupidASM relocatable object validation failed\n");
      goto done;
    }
  } else if ((kind == CUPIDBUILD_ASSEMBLY_BOOTLOADER &&
              candidate_snapshot.size != 2560u) ||
             ((kind == CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE ||
               kind == CUPIDBUILD_ASSEMBLY_ISO_PATTERN) &&
              candidate_snapshot.size != 4096u)) {
    (void)fprintf(stderr,
                  "cupidbuild: checked CupidASM raw output validation failed\n");
    goto done;
  }
  if (kind == CUPIDBUILD_ASSEMBLY_ISO_PATTERN) {
    size_t index;
    for (index = 0u; index < candidate_snapshot.size; index++) {
      if (candidate[index] != (unsigned char)(index & 0xffu)) {
        (void)fprintf(stderr,
                      "cupidbuild: checked CupidASM ISO pattern differs\n");
        goto done;
      }
    }
  }
  free(candidate);
  candidate = (unsigned char *)0;
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (kind != CUPIDBUILD_ASSEMBLY_OBJECT) {
    if (!cupidbuild_host_capture_private_output(transaction, &map_snapshot,
                                                &map) ||
        map_snapshot.size == 0u) {
      (void)fprintf(stderr,
                    "cupidbuild: checked CupidASM range map is missing or empty\n");
      goto done;
    }
    if (kind == CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE &&
        (map_snapshot.size != sizeof(cupidbuild_smp_trampoline_map) - 1u ||
         memcmp(map, cupidbuild_smp_trampoline_map,
                sizeof(cupidbuild_smp_trampoline_map) - 1u) != 0)) {
      (void)fprintf(stderr,
                    "cupidbuild: checked CupidASM range map does not match "
                    "the SMP layout policy\n");
      goto done;
    }
    if (kind == CUPIDBUILD_ASSEMBLY_ISO_PATTERN &&
        (map_snapshot.size != sizeof(cupidbuild_iso_pattern_map) - 1u ||
         memcmp(map, cupidbuild_iso_pattern_map,
                sizeof(cupidbuild_iso_pattern_map) - 1u) != 0)) {
      (void)fprintf(stderr,
                    "cupidbuild: checked CupidASM range map does not match "
                    "the ISO pattern layout policy\n");
      goto done;
    }
    free(map);
    map = (unsigned char *)0;
    if (!cupidbuild_host_require_private_output(transaction, &map_snapshot)) {
      (void)fprintf(stderr, "cupidbuild: %s\n",
                    cupidbuild_host_error(transaction));
      goto done;
    }
  }
  if (kind == CUPIDBUILD_ASSEMBLY_OBJECT) {
    inspector_arguments[0] = "--require-known";
    inspector_arguments[1] = "--require-local-targets";
    inspector_arguments[2] = "--require-code-anchors";
    inspector_arguments[3] = cupidbuild_host_candidate(transaction);
    inspector_arguments[4] = (const char *)0;
  } else {
    inspector_arguments[0] = "--raw";
    inspector_arguments[1] = "--range-map";
    inspector_arguments[2] = cupidbuild_host_private_output(transaction);
    inspector_arguments[3] = "--require-known";
    inspector_arguments[4] = "--require-local-targets";
    inspector_arguments[5] = "--require-source-edges";
    inspector_arguments[6] = cupidbuild_host_candidate(transaction);
    inspector_arguments[7] = (const char *)0;
  }
  inspector_status = cupidbuild_host_run(
      transaction, frozen_inspector, inspector_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot) ||
      (kind != CUPIDBUILD_ASSEMBLY_OBJECT &&
       !cupidbuild_host_require_private_output(transaction, &map_snapshot))) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (inspector_status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidDis failed\n");
    goto done;
  }
  if (kind == CUPIDBUILD_ASSEMBLY_ISO_PATTERN) {
    int changed;
    if (!cupidbuild_host_publish_if_changed(transaction, &changed)) {
      (void)fprintf(stderr, "cupidbuild: %s\n",
                    cupidbuild_host_error(transaction));
      goto done;
    }
  } else if (!cupidbuild_host_require_publication_boundary(transaction) ||
             !cupidbuild_host_publish(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = 0;

done:
  free(map);
  free(candidate);
  cupidbuild_seed_capture_close(&seed);
  return cupidbuild_finish_publication(transaction, result,
                                       "guarded assembly output");
}

int cupidbuild_assemble_object(
    const cupidbuild_assembly_request_t *request) {
  return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_OBJECT);
}

int cupidbuild_assemble_bootloader(
    const cupidbuild_assembly_request_t *request) {
  return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_BOOTLOADER);
}

int cupidbuild_assemble_smp_trampoline(
    const cupidbuild_assembly_request_t *request) {
  return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE);
}

int cupidbuild_assemble_iso_pattern(
    const cupidbuild_assembly_request_t *request) {
  return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_ISO_PATTERN);
}

int cupidbuild_embed_jpeg(const cupidbuild_jpeg_request_t *request) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_seed_capture_t seed;
  cupidbuild_host_snapshot_t candidate_snapshot;
  unsigned char *candidate = (unsigned char *)0;
  unsigned char *source = (unsigned char *)0;
  size_t source_size = 0u;
  char jpeg_reason[160];
  const char *object_arguments[7];
  int object_status;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  if (request == (const cupidbuild_jpeg_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->source, 1) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0)) {
    (void)fprintf(stderr, "cupidbuild: invalid JPEG embed request\n");
    return 1;
  }
  if (!cupidbuild_host_transaction_open(request->repository_root,
                                        request->source, request->output,
                                        &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_seed_freeze(transaction, request->repository_root,
                              request->seed_manifest, 1, 1, &seed)) {
    goto done;
  }
  object_arguments[0] = "wrap-jpeg";
  object_arguments[1] = cupidbuild_host_frozen_source(transaction);
  object_arguments[2] = "--identity";
  object_arguments[3] = request->source;
  object_arguments[4] = "-o";
  object_arguments[5] = cupidbuild_host_candidate(transaction);
  object_arguments[6] = (const char *)0;
  object_status = cupidbuild_host_run(transaction, seed.frozen_tools[4],
                                      object_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (object_status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidObj failed\n");
    goto done;
  }
  if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot,
                                         &candidate)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  source = cupidbuild_host_read_frozen_input(
      transaction, cupidbuild_host_frozen_source(transaction),
      CUPIDBUILD_TOOL_BYTES, &source_size);
  if (source == (unsigned char *)0) {
    (void)fprintf(stderr,
                  "cupidbuild: frozen JPEG input cannot be inspected\n");
    goto done;
  }
  if (!cupidbuild_validate_jpeg_object_bytes(
          candidate, candidate_snapshot.size, source, source_size,
          request->source)) {
    (void)fprintf(
        stderr,
        "cupidbuild: checked CupidObj JPEG object validation failed\n");
    goto done;
  }
  if (!cupidbuild_validate_jpeg_bytes(source, source_size, jpeg_reason,
                                      sizeof(jpeg_reason))) {
    (void)fprintf(stderr,
                  "cupidbuild: independent JPEG validation failed: %s\n",
                  jpeg_reason);
    goto done;
  }
  free(candidate);
  candidate = (unsigned char *)0;
  free(source);
  source = (unsigned char *)0;
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_host_require_publication_boundary(transaction) ||
      !cupidbuild_host_publish(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = 0;

done:
  free(source);
  free(candidate);
  cupidbuild_seed_capture_close(&seed);
  return cupidbuild_finish_publication(transaction, result, "JPEG object");
}

static int cupidbuild_ksyms_space(unsigned char character) {
  return character == (unsigned char)' ' ||
         character == (unsigned char)'\t' ||
         character == (unsigned char)'\r' ||
         character == (unsigned char)'\v' ||
         character == (unsigned char)'\f';
}

static cupidbuild_ksyms_row_kind_t cupidbuild_ksyms_address(
    const unsigned char *bytes, size_t size, unsigned int *address_out) {
  unsigned int value = 0u;
  size_t index = 0u;
  if (size >= 2u && bytes[0] == (unsigned char)'0' &&
      (bytes[1] == (unsigned char)'x' || bytes[1] == (unsigned char)'X')) {
    index = 2u;
  }
  if (index == size) {
    return CUPIDBUILD_KSYMS_INVALID_ADDRESS;
  }
  while (index < size) {
    unsigned char character = bytes[index];
    unsigned int digit;
    if (character >= (unsigned char)'0' &&
        character <= (unsigned char)'9') {
      digit = (unsigned int)(character - (unsigned char)'0');
    } else if (character >= (unsigned char)'a' &&
               character <= (unsigned char)'f') {
      digit = 10u + (unsigned int)(character - (unsigned char)'a');
    } else if (character >= (unsigned char)'A' &&
               character <= (unsigned char)'F') {
      digit = 10u + (unsigned int)(character - (unsigned char)'A');
    } else {
      return CUPIDBUILD_KSYMS_INVALID_ADDRESS;
    }
    if (value > (0xffffffffu - digit) / 16u) {
      return CUPIDBUILD_KSYMS_ADDRESS_OUTSIDE_I386;
    }
    value = value * 16u + digit;
    index++;
  }
  *address_out = value;
  return CUPIDBUILD_KSYMS_TEXT;
}

static cupidbuild_ksyms_row_kind_t cupidbuild_ksyms_parse_row(
    const unsigned char *bytes, size_t start, size_t end, size_t order,
    cupidbuild_ksyms_symbol_t *symbol_out) {
  size_t field_start[3];
  size_t field_size[3];
  size_t field_count = 0u;
  size_t index = start;
  unsigned int address = 0u;
  cupidbuild_ksyms_row_kind_t address_kind;
  while (index < end) {
    size_t token_start;
    while (index < end && cupidbuild_ksyms_space(bytes[index])) {
      index++;
    }
    if (index == end) {
      break;
    }
    if (field_count == 3u) {
      return CUPIDBUILD_KSYMS_MALFORMED;
    }
    token_start = index;
    while (index < end && !cupidbuild_ksyms_space(bytes[index])) {
      if (bytes[index] == 0u) {
        return CUPIDBUILD_KSYMS_MALFORMED;
      }
      index++;
    }
    field_start[field_count] = token_start;
    field_size[field_count] = index - token_start;
    field_count++;
  }
  if (field_count == 0u) {
    return CUPIDBUILD_KSYMS_EMPTY;
  }
  if (field_count == 2u) {
    if (field_size[0] == 1u &&
        (bytes[field_start[0]] == (unsigned char)'U' ||
         bytes[field_start[0]] == (unsigned char)'u' ||
         bytes[field_start[0]] == (unsigned char)'v' ||
         bytes[field_start[0]] == (unsigned char)'w')) {
      return CUPIDBUILD_KSYMS_IGNORED;
    }
    return CUPIDBUILD_KSYMS_OMITTED_ADDRESS;
  }
  if (field_count != 3u || field_size[1] != 1u) {
    return CUPIDBUILD_KSYMS_MALFORMED;
  }
  address_kind = cupidbuild_ksyms_address(
      bytes + field_start[0], field_size[0], &address);
  if (address_kind != CUPIDBUILD_KSYMS_TEXT) {
    return address_kind;
  }
  if (!(bytes[field_start[1]] == (unsigned char)'t' ||
        bytes[field_start[1]] == (unsigned char)'T' ||
        bytes[field_start[1]] == (unsigned char)'w' ||
        bytes[field_start[1]] == (unsigned char)'W')) {
    return CUPIDBUILD_KSYMS_IGNORED;
  }
  if (field_size[2] >= 2u && bytes[field_start[2]] == (unsigned char)'.' &&
      bytes[field_start[2] + 1u] == (unsigned char)'L') {
    return CUPIDBUILD_KSYMS_IGNORED;
  }
  symbol_out->address = address;
  symbol_out->order = order;
  symbol_out->name_start = field_start[2];
  symbol_out->name_size = field_size[2];
  return CUPIDBUILD_KSYMS_TEXT;
}

static int cupidbuild_ksyms_symbol_less(
    const cupidbuild_ksyms_symbol_t *left,
    const cupidbuild_ksyms_symbol_t *right) {
  return left->address < right->address ||
         (left->address == right->address && left->order < right->order);
}

static void cupidbuild_ksyms_symbol_swap(
    cupidbuild_ksyms_symbol_t *left, cupidbuild_ksyms_symbol_t *right) {
  cupidbuild_ksyms_symbol_t temporary = *left;
  *left = *right;
  *right = temporary;
}

static void cupidbuild_ksyms_symbol_sift_down(
    cupidbuild_ksyms_symbol_t *symbols, size_t root, size_t count) {
  for (;;) {
    size_t child;
    size_t selected;
    if (root >= count / 2u) {
      return;
    }
    child = root * 2u + 1u;
    selected = root;
    if (cupidbuild_ksyms_symbol_less(&symbols[selected], &symbols[child])) {
      selected = child;
    }
    if (child + 1u < count &&
        cupidbuild_ksyms_symbol_less(&symbols[selected],
                                     &symbols[child + 1u])) {
      selected = child + 1u;
    }
    if (selected == root) {
      return;
    }
    cupidbuild_ksyms_symbol_swap(&symbols[root], &symbols[selected]);
    root = selected;
  }
}

static void cupidbuild_ksyms_symbol_sort(
    cupidbuild_ksyms_symbol_t *symbols, size_t count) {
  size_t start = count / 2u;
  size_t end = count;
  while (start != 0u) {
    start--;
    cupidbuild_ksyms_symbol_sift_down(symbols, start, count);
  }
  while (end > 1u) {
    cupidbuild_ksyms_symbol_swap(&symbols[0], &symbols[end - 1u]);
    end--;
    cupidbuild_ksyms_symbol_sift_down(symbols, 0u, end);
  }
}

static void cupidbuild_ksyms_write_le32(unsigned char *bytes, size_t offset,
                                        unsigned int value) {
  bytes[offset] = (unsigned char)(value & 0xffu);
  bytes[offset + 1u] = (unsigned char)((value >> 8u) & 0xffu);
  bytes[offset + 2u] = (unsigned char)((value >> 16u) & 0xffu);
  bytes[offset + 3u] = (unsigned char)((value >> 24u) & 0xffu);
}

static int cupidbuild_ksyms_append(cupidbuild_ksyms_buffer_t *buffer,
                                   const void *bytes, size_t size) {
  size_t required;
  size_t capacity;
  unsigned char *grown;
  if (size > CUPIDBUILD_TOOL_BYTES ||
      buffer->size > CUPIDBUILD_TOOL_BYTES - size) {
    return 0;
  }
  required = buffer->size + size;
  if (required > buffer->capacity) {
    capacity = buffer->capacity == 0u ? 4096u : buffer->capacity;
    while (capacity < required) {
      if (capacity > CUPIDBUILD_TOOL_BYTES / 2u) {
        capacity = CUPIDBUILD_TOOL_BYTES;
        break;
      }
      capacity *= 2u;
    }
    grown = (unsigned char *)realloc(buffer->bytes, capacity);
    if (grown == (unsigned char *)0) {
      return 0;
    }
    buffer->bytes = grown;
    buffer->capacity = capacity;
  }
  if (size != 0u) {
    (void)memcpy(buffer->bytes + buffer->size, bytes, size);
  }
  buffer->size = required;
  return 1;
}

static int cupidbuild_ksyms_append_literal(
    cupidbuild_ksyms_buffer_t *buffer, const char *text) {
  return cupidbuild_ksyms_append(buffer, text, strlen(text));
}

static int cupidbuild_ksyms_append_word(cupidbuild_ksyms_buffer_t *buffer,
                                        unsigned int value) {
  static const char digits[] = "0123456789abcdef";
  char text[12];
  size_t index;
  text[0] = '0';
  text[1] = 'x';
  for (index = 0u; index < 8u; index++) {
    size_t shift = (7u - index) * 4u;
    text[2u + index] = digits[(value >> shift) & 0x0fu];
  }
  text[10] = 'u';
  text[11] = ',';
  return cupidbuild_ksyms_append(buffer, text, sizeof(text));
}

static int cupidbuild_ksyms_append_decimal(
    cupidbuild_ksyms_buffer_t *buffer, unsigned int value) {
  char reverse[10];
  char text[10];
  size_t count = 0u;
  size_t index;
  do {
    reverse[count++] = (char)('0' + (char)(value % 10u));
    value /= 10u;
  } while (value != 0u);
  for (index = 0u; index < count; index++) {
    text[index] = reverse[count - index - 1u];
  }
  return cupidbuild_ksyms_append(buffer, text, count);
}

static const char *cupidbuild_ksyms_row_reason(
    cupidbuild_ksyms_row_kind_t kind) {
  if (kind == CUPIDBUILD_KSYMS_OMITTED_ADDRESS) {
    return "symbol reader omitted an address";
  }
  if (kind == CUPIDBUILD_KSYMS_INVALID_ADDRESS) {
    return "symbol reader emitted an invalid address";
  }
  if (kind == CUPIDBUILD_KSYMS_ADDRESS_OUTSIDE_I386) {
    return "symbol reader address is outside i386";
  }
  return "symbol reader emitted a malformed row";
}

static int cupidbuild_render_ksyms_source(
    const unsigned char *contents, size_t contents_size,
    unsigned char **source_out, size_t *source_size_out,
    const char **reason_out, size_t *line_out) {
  cupidbuild_ksyms_symbol_t *symbols =
      (cupidbuild_ksyms_symbol_t *)0;
  unsigned char *blob = (unsigned char *)0;
  cupidbuild_ksyms_buffer_t output;
  size_t symbol_count = 0u;
  size_t unique_count = 0u;
  size_t offset = 0u;
  size_t line = 1u;
  size_t index;
  size_t string_offset;
  size_t blob_size;
  size_t padded_size;
  size_t string_cursor;
  int success = 0;
  (void)memset(&output, 0, sizeof(output));
  *source_out = (unsigned char *)0;
  *source_size_out = 0u;
  *reason_out = "kernel symbol source validation failed";
  *line_out = 0u;
  while (offset < contents_size) {
    size_t start = offset;
    cupidbuild_ksyms_symbol_t symbol;
    cupidbuild_ksyms_row_kind_t kind;
    while (offset < contents_size && contents[offset] != (unsigned char)'\n') {
      offset++;
    }
    kind = cupidbuild_ksyms_parse_row(contents, start, offset, symbol_count,
                                      &symbol);
    if (kind == CUPIDBUILD_KSYMS_TEXT) {
      symbol_count++;
    } else if (kind != CUPIDBUILD_KSYMS_EMPTY &&
               kind != CUPIDBUILD_KSYMS_IGNORED) {
      *reason_out = cupidbuild_ksyms_row_reason(kind);
      *line_out = line;
      goto done;
    }
    if (offset < contents_size) {
      offset++;
      line++;
    }
  }
  if (symbol_count == 0u) {
    *reason_out = "symbol reader reported no kernel text symbols";
    goto done;
  }
  if (symbol_count > (size_t)-1 / sizeof(*symbols)) {
    *reason_out = "kernel symbol inventory exceeds the host size limit";
    goto done;
  }
  symbols = (cupidbuild_ksyms_symbol_t *)malloc(
      symbol_count * sizeof(*symbols));
  if (symbols == (cupidbuild_ksyms_symbol_t *)0) {
    *reason_out = "kernel symbol inventory cannot be allocated";
    goto done;
  }
  offset = 0u;
  index = 0u;
  while (offset < contents_size) {
    size_t start = offset;
    cupidbuild_ksyms_symbol_t symbol;
    cupidbuild_ksyms_row_kind_t kind;
    while (offset < contents_size && contents[offset] != (unsigned char)'\n') {
      offset++;
    }
    kind = cupidbuild_ksyms_parse_row(contents, start, offset, index, &symbol);
    if (kind == CUPIDBUILD_KSYMS_TEXT) {
      symbols[index++] = symbol;
    }
    if (offset < contents_size) {
      offset++;
    }
  }
  cupidbuild_ksyms_symbol_sort(symbols, symbol_count);
  for (index = 0u; index < symbol_count; index++) {
    if (unique_count == 0u ||
        symbols[index].address != symbols[unique_count - 1u].address) {
      symbols[unique_count++] = symbols[index];
    }
  }
  if (unique_count > (0xffffffffu - 16u) / 8u) {
    *reason_out = "kernel symbol table size overflows i386";
    goto done;
  }
  string_offset = 16u + unique_count * 8u;
  blob_size = string_offset;
  for (index = 0u; index < unique_count; index++) {
    if (symbols[index].name_size >= 0xffffffffu ||
        blob_size > 0xffffffffu - symbols[index].name_size - 1u) {
      *reason_out = "kernel symbol strings overflow i386";
      goto done;
    }
    blob_size += symbols[index].name_size + 1u;
  }
  if (blob_size > 0xfffffffcu) {
    *reason_out = "word-packed kernel symbol source overflows i386";
    goto done;
  }
  padded_size = (blob_size + 3u) & ~(size_t)3u;
  if (padded_size > CUPIDBUILD_TOOL_BYTES) {
    *reason_out = "kernel symbol blob exceeds the validation limit";
    goto done;
  }
  blob = (unsigned char *)calloc(padded_size, 1u);
  if (blob == (unsigned char *)0) {
    *reason_out = "kernel symbol blob cannot be allocated";
    goto done;
  }
  cupidbuild_ksyms_write_le32(blob, 0u, 0x4d59534bu);
  cupidbuild_ksyms_write_le32(blob, 4u, (unsigned int)unique_count);
  cupidbuild_ksyms_write_le32(blob, 8u, (unsigned int)string_offset);
  cupidbuild_ksyms_write_le32(blob, 12u, (unsigned int)blob_size);
  string_cursor = string_offset;
  for (index = 0u; index < unique_count; index++) {
    cupidbuild_ksyms_write_le32(blob, 16u + index * 8u,
                                symbols[index].address);
    cupidbuild_ksyms_write_le32(
        blob, 20u + index * 8u,
        (unsigned int)(string_cursor - string_offset));
    (void)memcpy(blob + string_cursor,
                 contents + symbols[index].name_start,
                 symbols[index].name_size);
    string_cursor += symbols[index].name_size + 1u;
  }
  if (!cupidbuild_ksyms_append_literal(
          &output,
          "/* Auto-generated by tools/hostbuild.py -- do not edit. */\n"
          "#include \"ksyms.h\"\n\n"
          "/* i386 words preserve the blob bytes with fewer initializers. */\n"
          "const unsigned int\n"
          "__attribute__((section(\".ksyms\"), used, aligned(4)))\n"
          "ksym_blob[] = {\n")) {
    *reason_out = "kernel symbol source exceeds the validation limit";
    goto done;
  }
  for (offset = 0u; offset < padded_size; offset += 4u) {
    unsigned int value = (unsigned int)blob[offset] |
                         ((unsigned int)blob[offset + 1u] << 8u) |
                         ((unsigned int)blob[offset + 2u] << 16u) |
                         ((unsigned int)blob[offset + 3u] << 24u);
    size_t word_index = offset / 4u;
    if (!cupidbuild_ksyms_append_literal(
            &output, word_index % 8u == 0u ? "  " : " ") ||
        !cupidbuild_ksyms_append_word(&output, value) ||
        ((word_index % 8u == 7u || offset + 4u == padded_size) &&
         !cupidbuild_ksyms_append_literal(&output, "\n"))) {
      *reason_out = "kernel symbol source exceeds the validation limit";
      goto done;
    }
  }
  if (!cupidbuild_ksyms_append_literal(
          &output, "};\n\nconst unsigned int ksym_blob_size = ") ||
      !cupidbuild_ksyms_append_decimal(&output, (unsigned int)blob_size) ||
      !cupidbuild_ksyms_append_literal(&output, "u;\n")) {
    *reason_out = "kernel symbol source exceeds the validation limit";
    goto done;
  }
  *source_out = output.bytes;
  *source_size_out = output.size;
  output.bytes = (unsigned char *)0;
  success = 1;

done:
  free(output.bytes);
  free(blob);
  free(symbols);
  return success;
}

int cupidbuild_generate_ksyms(const cupidbuild_ksyms_request_t *request) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_seed_capture_t seed;
  cupidbuild_host_snapshot_t symbols_snapshot;
  cupidbuild_host_snapshot_t candidate_snapshot;
  unsigned char *symbols = (unsigned char *)0;
  unsigned char *candidate = (unsigned char *)0;
  unsigned char *expected = (unsigned char *)0;
  size_t expected_size = 0u;
  const char *validation_reason = (const char *)0;
  size_t validation_line = 0u;
  const char *disassembler_arguments[3];
  const char *object_arguments[5];
  int disassembler_status;
  int object_status;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  if (request == (const cupidbuild_ksyms_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->source, 1) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0)) {
    (void)fprintf(stderr,
                  "cupidbuild: invalid kernel symbol generation request\n");
    return 1;
  }
  if (!cupidbuild_host_transaction_open(request->repository_root,
                                        request->source, request->output,
                                        &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_seed_freeze(transaction, request->repository_root,
                              request->seed_manifest, 1, 1, &seed)) {
    goto done;
  }
  disassembler_arguments[0] = "-n";
  disassembler_arguments[1] = cupidbuild_host_frozen_source(transaction);
  disassembler_arguments[2] = (const char *)0;
  disassembler_status = cupidbuild_host_run_to_private_output(
      transaction, seed.frozen_tools[2], disassembler_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (disassembler_status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidDis failed\n");
    goto done;
  }
  if (!cupidbuild_host_capture_private_output(
          transaction, &symbols_snapshot, &symbols)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_render_ksyms_source(
          symbols, symbols_snapshot.size, &expected, &expected_size,
          &validation_reason, &validation_line)) {
    if (validation_line != 0u) {
      (void)fprintf(stderr,
                    "cupidbuild: independent kernel symbol validation "
                    "failed at line %u: %s\n",
                    (unsigned int)validation_line, validation_reason);
    } else {
      (void)fprintf(stderr,
                    "cupidbuild: independent kernel symbol validation "
                    "failed: %s\n",
                    validation_reason);
    }
    goto done;
  }
  free(symbols);
  symbols = (unsigned char *)0;
  if (!cupidbuild_host_require_private_output(transaction,
                                               &symbols_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  object_arguments[0] = "ksyms-source";
  object_arguments[1] = cupidbuild_host_private_output(transaction);
  object_arguments[2] = "-o";
  object_arguments[3] = cupidbuild_host_candidate(transaction);
  object_arguments[4] = (const char *)0;
  object_status = cupidbuild_host_run(transaction, seed.frozen_tools[4],
                                      object_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_private_output(transaction,
                                               &symbols_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (object_status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidObj failed\n");
    goto done;
  }
  if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot,
                                         &candidate)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (candidate_snapshot.size != expected_size ||
      memcmp(candidate, expected, expected_size) != 0) {
    (void)fprintf(stderr,
                  "cupidbuild: checked CupidObj kernel symbol source differs "
                  "from the independent renderer\n");
    goto done;
  }
  free(candidate);
  candidate = (unsigned char *)0;
  free(expected);
  expected = (unsigned char *)0;
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_private_output(transaction,
                                               &symbols_snapshot) ||
      !cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_host_require_publication_boundary(transaction) ||
      !cupidbuild_host_publish(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = 0;

done:
  free(expected);
  free(candidate);
  free(symbols);
  cupidbuild_seed_capture_close(&seed);
  return cupidbuild_finish_publication(transaction, result,
                                       "kernel symbol object");
}

static int cupidbuild_code_path_equal(const char *left, const char *right) {
  while (*left != '\0' && *right != '\0') {
    unsigned char left_byte = (unsigned char)*left;
    unsigned char right_byte = (unsigned char)*right;
    if (left_byte >= (unsigned char)'A' &&
        left_byte <= (unsigned char)'Z') {
      left_byte = (unsigned char)(left_byte + ('a' - 'A'));
    }
    if (right_byte >= (unsigned char)'A' &&
        right_byte <= (unsigned char)'Z') {
      right_byte = (unsigned char)(right_byte + ('a' - 'A'));
    }
    if (left_byte != right_byte) {
      return 0;
    }
    left++;
    right++;
  }
  return *left == '\0' && *right == '\0';
}

static int cupidbuild_code_manifest(
    unsigned char *bytes, size_t size, const char **paths, size_t *count_out,
    const char **reason_out, size_t *line_out) {
  size_t start = 0u;
  size_t count = 0u;
  size_t line = 1u;
  if (size == 0u) {
    *reason_out = "code input manifest may not be empty";
    return 0;
  }
  if (bytes[size - 1u] != (unsigned char)'\n') {
    *reason_out = "code input manifest must end with a newline";
    return 0;
  }
  while (start < size) {
    size_t end = start;
    size_t component = start;
    size_t index;
    while (end < size && bytes[end] != (unsigned char)'\n') {
      unsigned char byte = bytes[end];
      if (byte == (unsigned char)'\r') {
        *reason_out = "code input manifest must use LF newlines";
        *line_out = line;
        return 0;
      }
      if (byte == (unsigned char)'\\') {
        *reason_out = "code input path must use forward slashes";
        *line_out = line;
        return 0;
      }
      if (byte <= (unsigned char)' ' || byte == 127u) {
        *reason_out = "code input path may not contain whitespace or control bytes";
        *line_out = line;
        return 0;
      }
      end++;
    }
    if (end == start) {
      *reason_out = "code input manifest line is blank";
      *line_out = line;
      return 0;
    }
    if (bytes[start] == (unsigned char)'#') {
      *reason_out = "code input manifest line may not be a comment";
      *line_out = line;
      return 0;
    }
    if (bytes[start] == (unsigned char)'/' ||
        bytes[end - 1u] == (unsigned char)'/') {
      *reason_out = "code input path is not canonical and relative";
      *line_out = line;
      return 0;
    }
    for (index = start; index <= end; index++) {
      if (index != end && bytes[index] != (unsigned char)'/') {
        continue;
      }
      if (index == component ||
          (index - component == 1u &&
           bytes[component] == (unsigned char)'.') ||
          (index - component == 2u &&
           bytes[component] == (unsigned char)'.' &&
           bytes[component + 1u] == (unsigned char)'.')) {
        *reason_out = "code input path is not canonical and relative";
        *line_out = line;
        return 0;
      }
      component = index + 1u;
    }
    if (count == CUPIDBUILD_CODE_INPUTS) {
      *reason_out = "code input manifest exceeds the 500-input limit";
      *line_out = line;
      return 0;
    }
    bytes[end] = 0u;
    if (!cupidbuild_path_safe((const char *)(bytes + start), 1)) {
      *reason_out = "code input path is unsafe";
      *line_out = line;
      return 0;
    }
    for (index = 0u; index < count; index++) {
      if (cupidbuild_code_path_equal(paths[index],
                                     (const char *)(bytes + start))) {
        *reason_out = "code input is listed more than once";
        *line_out = line;
        return 0;
      }
    }
    paths[count++] = (const char *)(bytes + start);
    start = end + 1u;
    line++;
  }
  *count_out = count;
  return 1;
}

static int cupidbuild_flat_region_less(
    const cupidbuild_flat_region_t *left,
    const cupidbuild_flat_region_t *right) {
  return left->address < right->address ||
         (left->address == right->address && left->order < right->order);
}

static void cupidbuild_flat_region_swap(cupidbuild_flat_region_t *left,
                                        cupidbuild_flat_region_t *right) {
  cupidbuild_flat_region_t temporary = *left;
  *left = *right;
  *right = temporary;
}

static void cupidbuild_flat_region_sift_down(
    cupidbuild_flat_region_t *regions, size_t root, size_t count) {
  for (;;) {
    size_t child;
    size_t selected;
    if (root >= count / 2u) {
      return;
    }
    child = root * 2u + 1u;
    selected = root;
    if (cupidbuild_flat_region_less(&regions[selected], &regions[child])) {
      selected = child;
    }
    if (child + 1u < count &&
        cupidbuild_flat_region_less(&regions[selected],
                                    &regions[child + 1u])) {
      selected = child + 1u;
    }
    if (selected == root) {
      return;
    }
    cupidbuild_flat_region_swap(&regions[root], &regions[selected]);
    root = selected;
  }
}

static void cupidbuild_flat_region_sort(cupidbuild_flat_region_t *regions,
                                        size_t count) {
  size_t start = count / 2u;
  size_t end = count;
  while (start != 0u) {
    start--;
    cupidbuild_flat_region_sift_down(regions, start, count);
  }
  while (end > 1u) {
    cupidbuild_flat_region_swap(&regions[0], &regions[end - 1u]);
    end--;
    cupidbuild_flat_region_sift_down(regions, 0u, end);
  }
}

static int cupidbuild_render_flat_image(
    const unsigned char *bytes, size_t size, unsigned char **image_out,
    size_t *image_size_out, const char **reason_out) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_elf32_object_t object;
  cupidbuild_flat_region_t *regions = (cupidbuild_flat_region_t *)0;
  unsigned char *image = (unsigned char *)0;
  size_t load_count = 0u;
  size_t region_count = 0u;
  size_t position = 0u;
  size_t index;
  unsigned int base;
  unsigned int cursor;
  int success = 0;
  if (size > 4294967295u ||
      ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
    *reason_out = "linked kernel cannot be inspected";
    return 0;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  if (ctool_job_open(&config, &job) != CTOOL_OK) {
    *reason_out = "linked kernel inspection job cannot be opened";
    return 0;
  }
  source.path.text = ctool_string("/kernel/kernel.elf");
  source.contents = ctool_bytes(bytes, (ctool_u32)size);
  if (ctool_elf32_read(job, &source, &object) != CTOOL_OK ||
      object.file_type != CTOOL_ELF32_ET_EXEC) {
    *reason_out = "linked kernel is not a static i386 executable";
    goto done;
  }
  for (index = 0u; index < object.program_header_count; index++) {
    if (object.program_headers[index].type == CTOOL_ELF32_PT_LOAD) {
      load_count++;
      if (object.program_headers[index].file_size != 0u) {
        region_count++;
      }
    }
  }
  if (load_count == 0u) {
    for (index = 0u; index < object.section_count; index++) {
      const ctool_elf32_section_t *section = &object.sections[index];
      if ((section->flags & CTOOL_ELF32_SHF_ALLOC) == 0u ||
          section->type == CTOOL_ELF32_SHT_NOBITS) {
        continue;
      }
      if (section->type != CTOOL_ELF32_SHT_PROGBITS) {
        *reason_out = "linked kernel has unsupported allocated content";
        goto done;
      }
      if (section->size != 0u) {
        region_count++;
      }
    }
  }
  if (region_count == 0u) {
    *reason_out = "linked kernel has no initialized load";
    goto done;
  }
  regions = (cupidbuild_flat_region_t *)calloc(region_count,
                                                sizeof(*regions));
  if (regions == (cupidbuild_flat_region_t *)0) {
    *reason_out = "flat-image region table cannot be allocated";
    goto done;
  }
  if (load_count != 0u) {
    for (index = 0u; index < object.program_header_count; index++) {
      const ctool_elf32_program_header_t *header =
          &object.program_headers[index];
      if (header->type == CTOOL_ELF32_PT_LOAD && header->file_size != 0u) {
        regions[position].address = header->physical_address;
        regions[position].order = header->file_index;
        regions[position].contents = header->contents;
        position++;
      }
    }
  } else {
    for (index = 0u; index < object.section_count; index++) {
      const ctool_elf32_section_t *section = &object.sections[index];
      if ((section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u &&
          section->type == CTOOL_ELF32_SHT_PROGBITS && section->size != 0u) {
        regions[position].address = section->address;
        regions[position].order = section->file_index;
        regions[position].contents = section->contents;
        position++;
      }
    }
  }
  cupidbuild_flat_region_sort(regions, region_count);
  base = regions[0].address;
  cursor = base;
  for (index = 0u; index < region_count; index++) {
    const cupidbuild_flat_region_t *region = &regions[index];
    if (region->address < cursor ||
        region->address > 0xffffffffu - region->contents.size) {
      *reason_out = "linked kernel initialized load ranges overlap or overflow";
      goto done;
    }
    cursor = region->address + region->contents.size;
  }
  if ((size_t)(cursor - base) > CUPIDBUILD_TOOL_BYTES) {
    *reason_out = "flat kernel exceeds the 64 MiB transaction limit";
    goto done;
  }
  image = (unsigned char *)calloc((size_t)(cursor - base), 1u);
  if (image == (unsigned char *)0) {
    *reason_out = "flat kernel buffer cannot be allocated";
    goto done;
  }
  for (index = 0u; index < region_count; index++) {
    (void)memcpy(image + (regions[index].address - base),
                 regions[index].contents.data, regions[index].contents.size);
  }
  *image_out = image;
  *image_size_out = (size_t)(cursor - base);
  image = (unsigned char *)0;
  success = 1;

done:
  free(image);
  free(regions);
  ctool_job_close(job);
  return success;
}

static void cupidbuild_report_checked_failure(
    cupidbuild_host_transaction_t *transaction, const char *operation) {
  const char *error = cupidbuild_host_error(transaction);
  if (error != (const char *)0 && error[0] != '\0') {
    (void)fprintf(stderr, "cupidbuild: %s: %s\n", operation, error);
  } else {
    (void)fprintf(stderr, "cupidbuild: %s\n", operation);
  }
}

int cupidbuild_flatten_kernel(const cupidbuild_kernel_request_t *request) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_seed_capture_t seed;
  const char *logical_paths[CUPIDBUILD_CODE_INPUTS];
  const char *private_paths[CUPIDBUILD_CODE_INPUTS];
  const char *pass_one = (const char *)0;
  const char *linked = (const char *)0;
  const char *linked_frozen = (const char *)0;
  unsigned char *manifest = (unsigned char *)0;
  size_t manifest_size = 0u;
  size_t input_count = 0u;
  const char *manifest_reason = (const char *)0;
  size_t manifest_line = 0u;
  unsigned char *linked_bytes = (unsigned char *)0;
  size_t linked_size = 0u;
  unsigned char *expected = (unsigned char *)0;
  size_t expected_size = 0u;
  const char *flat_reason = (const char *)0;
  unsigned char *candidate = (unsigned char *)0;
  cupidbuild_host_snapshot_t candidate_snapshot;
  const char *disassembler_arguments[CUPIDBUILD_CODE_INPUTS + 2u];
  const char *linked_arguments[7];
  const char *object_arguments[5];
  size_t index;
  int status;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  if (request == (const cupidbuild_kernel_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->input_manifest, 1) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0)) {
    (void)fprintf(stderr, "cupidbuild: invalid kernel flatten request\n");
    return 1;
  }
  if (!cupidbuild_host_transaction_open(request->repository_root,
                                        request->input_manifest,
                                        request->output,
                                        &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  manifest = cupidbuild_host_read_frozen_input(
      transaction, cupidbuild_host_frozen_source(transaction),
      CUPIDBUILD_MANIFEST_BYTES, &manifest_size);
  if (manifest == (unsigned char *)0 ||
      !cupidbuild_code_manifest(manifest, manifest_size, logical_paths,
                                &input_count, &manifest_reason,
                                &manifest_line)) {
    if (manifest_reason == (const char *)0) {
      manifest_reason = "code input manifest cannot be read";
    }
    if (manifest_line != 0u) {
      (void)fprintf(stderr,
                    "cupidbuild: invalid code input manifest at line %u: %s\n",
                    (unsigned int)manifest_line, manifest_reason);
    } else {
      (void)fprintf(stderr, "cupidbuild: invalid code input manifest: %s\n",
                    manifest_reason);
    }
    goto done;
  }
  if (!cupidbuild_host_reserve_inputs(transaction, input_count + 8u)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  for (index = 0u; index < input_count; index++) {
    char live_path[CUPIDBUILD_PATH_BYTES];
    char private_name[32];
    const char *frozen_path = (const char *)0;
    int written = snprintf(private_name, sizeof(private_name),
                           "code-%03u.bin", (unsigned int)index);
    if (written <= 0 || (size_t)written >= sizeof(private_name) ||
        !cupidbuild_join(live_path, sizeof(live_path),
                         request->repository_root, logical_paths[index]) ||
        !cupidbuild_host_freeze_input(transaction, live_path, private_name,
                                      &frozen_path,
                                      (cupidbuild_host_snapshot_t *)0)) {
      (void)fprintf(stderr, "cupidbuild: %s\n",
                    cupidbuild_host_error(transaction));
      goto done;
    }
    private_paths[index] = frozen_path;
#if defined(_WIN32)
    /* The retained private directory is the child's working directory. */
    {
      const char *cursor = frozen_path;
      while (*cursor != '\0') {
        if (*cursor == '/' || *cursor == '\\') {
          private_paths[index] = cursor + 1;
        }
        cursor++;
      }
    }
#endif
    if (strcmp(logical_paths[index], "kernel/kernel.elf.pass1") == 0) {
      pass_one = private_paths[index];
    } else if (strcmp(logical_paths[index], "kernel/kernel.elf") == 0) {
      linked = private_paths[index];
      linked_frozen = frozen_path;
    }
  }
  if (pass_one == (const char *)0 || linked == (const char *)0) {
    (void)fprintf(
        stderr,
        "cupidbuild: code input manifest must include both linked kernels\n");
    goto done;
  }
  if (!cupidbuild_seed_freeze(transaction, request->repository_root,
                              request->seed_manifest, 1, 1, &seed)) {
    goto done;
  }
  disassembler_arguments[0] = "--require-known";
  for (index = 0u; index < input_count; index++) {
    disassembler_arguments[index + 1u] = private_paths[index];
  }
  disassembler_arguments[input_count + 1u] = (const char *)0;
  status = cupidbuild_host_run_in_private(
      transaction, seed.frozen_tools[2], disassembler_arguments, 300000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (status != 0) {
    cupidbuild_report_checked_failure(transaction, "checked CupidDis failed");
    goto done;
  }
  linked_arguments[0] = "--require-known";
  linked_arguments[1] = "--require-local-targets";
  linked_arguments[2] = "--require-code-anchors";
  linked_arguments[3] = pass_one;
  linked_arguments[4] = linked;
  linked_arguments[5] = (const char *)0;
  status = cupidbuild_host_run_in_private(
      transaction, seed.frozen_tools[2], linked_arguments, 600000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (status != 0) {
    cupidbuild_report_checked_failure(
        transaction, "checked CupidDis linked validation failed");
    goto done;
  }
  linked_bytes = cupidbuild_host_read_frozen_input(
      transaction, linked_frozen, CUPIDBUILD_TOOL_BYTES, &linked_size);
  if (linked_bytes == (unsigned char *)0 ||
      !cupidbuild_render_flat_image(linked_bytes, linked_size, &expected,
                                    &expected_size, &flat_reason)) {
    (void)fprintf(stderr,
                  "cupidbuild: independent flat kernel validation failed: %s\n",
                  flat_reason == (const char *)0 ? "unreadable linked kernel"
                                                 : flat_reason);
    goto done;
  }
  free(linked_bytes);
  linked_bytes = (unsigned char *)0;
  object_arguments[0] = "flat";
  object_arguments[1] = linked;
  object_arguments[2] = "-o";
  object_arguments[3] = cupidbuild_host_candidate(transaction);
  object_arguments[4] = (const char *)0;
  status = cupidbuild_host_run_in_private(
      transaction, seed.frozen_tools[4], object_arguments, 300000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (status != 0) {
    cupidbuild_report_checked_failure(transaction, "checked CupidObj failed");
    goto done;
  }
  if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot,
                                         &candidate)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (candidate_snapshot.size != expected_size ||
      memcmp(candidate, expected, expected_size) != 0) {
    (void)fprintf(stderr,
                  "cupidbuild: checked CupidObj flat kernel differs from the "
                  "independent renderer\n");
    goto done;
  }
  free(candidate);
  candidate = (unsigned char *)0;
  free(expected);
  expected = (unsigned char *)0;
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot) ||
      !cupidbuild_host_require_publication_boundary(transaction) ||
      !cupidbuild_host_publish(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = 0;

done:
  free(candidate);
  free(expected);
  free(linked_bytes);
  free(manifest);
  cupidbuild_seed_capture_close(&seed);
  return cupidbuild_finish_publication(transaction, result,
                                       "flattened kernel");
}

static char cupidbuild_profile_fold(char character) {
  return character >= 'A' && character <= 'Z'
             ? (char)(character - 'A' + 'a')
             : character;
}

static int cupidbuild_profile_path_equal_folded(const char *left,
                                                const char *right) {
  size_t index = 0u;
  while (left[index] != '\0' && right[index] != '\0') {
    if (cupidbuild_profile_fold(left[index]) !=
        cupidbuild_profile_fold(right[index])) {
      return 0;
    }
    index++;
  }
  return left[index] == right[index];
}

static int cupidbuild_profile_path_valid(const char *path) {
  size_t size;
  size_t component = 0u;
  size_t index;
  if (path == (const char *)0 || path[0] == '\0') {
    return 0;
  }
  size = strlen(path);
  if (size > 1024u || path[0] == '/' || path[size - 1u] == '/') {
    return 0;
  }
  for (index = 0u; index <= size; index++) {
    unsigned char byte = (unsigned char)path[index];
    if (index == size || byte == (unsigned char)'/') {
      size_t component_size = index - component;
      if (component_size == 0u ||
          (component_size == 1u && path[component] == '.') ||
          (component_size == 2u && path[component] == '.' &&
           path[component + 1u] == '.')) {
        return 0;
      }
      component = index + 1u;
    } else if (!((byte >= (unsigned char)'a' &&
                   byte <= (unsigned char)'z') ||
                  (byte >= (unsigned char)'A' &&
                   byte <= (unsigned char)'Z') ||
                  (byte >= (unsigned char)'0' &&
                   byte <= (unsigned char)'9') ||
                  byte == (unsigned char)'.' ||
                  byte == (unsigned char)'_' ||
                  byte == (unsigned char)'-')) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_profile_paths_valid(
    const cupidbuild_host_path_list_t *paths) {
  size_t index;
  size_t prior;
  if (paths == (const cupidbuild_host_path_list_t *)0 ||
      (paths->count != 0u &&
       (paths->paths == (char **)0 ||
        paths->snapshots == (cupidbuild_host_snapshot_t *)0))) {
    return 0;
  }
  for (index = 0u; index < paths->count; index++) {
    if (!cupidbuild_profile_path_valid(paths->paths[index])) {
      return 0;
    }
    for (prior = 0u; prior < index; prior++) {
      if (cupidbuild_profile_path_equal_folded(paths->paths[prior],
                                               paths->paths[index])) {
        return 0;
      }
    }
  }
  return 1;
}

static size_t cupidbuild_profile_compat_source_count(void) {
  return sizeof(cupidbuild_profile_compat_sources) /
         sizeof(cupidbuild_profile_compat_sources[0]);
}

static size_t cupidbuild_profile_tree_source_count(void) {
  return sizeof(cupidbuild_profile_tree_sources) /
         sizeof(cupidbuild_profile_tree_sources[0]);
}

static const char *cupidbuild_profile_expected_source(size_t index) {
  size_t compat_count = cupidbuild_profile_compat_source_count();
  return index < compat_count
             ? cupidbuild_profile_compat_sources[index]
             : cupidbuild_profile_tree_sources[index - compat_count];
}

static int cupidbuild_profile_sources_exact(
    const cupidbuild_host_path_list_t *sources) {
  size_t expected_count = cupidbuild_profile_compat_source_count() +
                          cupidbuild_profile_tree_source_count();
  size_t index;
  if (sources->count != expected_count) {
    return 0;
  }
  for (index = 0u; index < expected_count; index++) {
    if (strcmp(sources->paths[index],
               cupidbuild_profile_expected_source(index)) != 0) {
      return 0;
    }
  }
  return 1;
}

static void cupidbuild_profile_membership_close(
    cupidbuild_profile_membership_t *membership) {
  cupidbuild_host_path_list_close(&membership->headers);
  cupidbuild_host_path_list_close(&membership->sources);
}

static int cupidbuild_profile_discover(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_profile_membership_t *membership) {
  (void)memset(membership, 0, sizeof(*membership));
  if (!cupidbuild_host_discover_files(
          transaction, cupidbuild_profile_header_roots,
          sizeof(cupidbuild_profile_header_roots) /
              sizeof(cupidbuild_profile_header_roots[0]),
          cupidbuild_profile_header_suffixes,
          sizeof(cupidbuild_profile_header_suffixes) /
              sizeof(cupidbuild_profile_header_suffixes[0]),
          1, 0, &membership->headers) ||
      !cupidbuild_host_discover_files(
          transaction, cupidbuild_profile_source_roots,
          sizeof(cupidbuild_profile_source_roots) /
              sizeof(cupidbuild_profile_source_roots[0]),
          cupidbuild_profile_source_suffixes,
          sizeof(cupidbuild_profile_source_suffixes) /
              sizeof(cupidbuild_profile_source_suffixes[0]),
          0, 1, &membership->sources)) {
    cupidbuild_profile_membership_close(membership);
    return 0;
  }
  if (membership->headers.count == 0u ||
      !cupidbuild_profile_paths_valid(&membership->headers) ||
      !cupidbuild_profile_paths_valid(&membership->sources) ||
      !cupidbuild_profile_sources_exact(&membership->sources)) {
    cupidbuild_profile_membership_close(membership);
    return 0;
  }
  return 1;
}

static int cupidbuild_profile_membership_equal(
    const cupidbuild_profile_membership_t *left,
    const cupidbuild_profile_membership_t *right) {
  size_t index;
  if (left == (const cupidbuild_profile_membership_t *)0 ||
      right == (const cupidbuild_profile_membership_t *)0 ||
      left->headers.count != right->headers.count ||
      left->sources.count != right->sources.count) {
    return 0;
  }
  for (index = 0u; index < left->headers.count; index++) {
    if (strcmp(left->headers.paths[index], right->headers.paths[index]) != 0 ||
        !cupidbuild_host_snapshot_equal(&left->headers.snapshots[index],
                                        &right->headers.snapshots[index])) {
      return 0;
    }
  }
  for (index = 0u; index < left->sources.count; index++) {
    if (strcmp(left->sources.paths[index], right->sources.paths[index]) != 0 ||
        !cupidbuild_host_snapshot_equal(&left->sources.snapshots[index],
                                        &right->sources.snapshots[index])) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_profile_require_membership(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_profile_membership_t *expected) {
  cupidbuild_profile_membership_t current;
  int valid = cupidbuild_profile_discover(transaction, &current);
  if (valid != 0) {
    valid = cupidbuild_profile_membership_equal(expected, &current);
  }
  cupidbuild_profile_membership_close(&current);
  return valid;
}

static int cupidbuild_profile_buffer_reserve(
    cupidbuild_profile_buffer_t *buffer, size_t additional) {
  size_t needed;
  size_t capacity;
  unsigned char *grown;
  if (additional > CUPIDBUILD_PROFILE_SNAPSHOT_BYTES - buffer->size) {
    return 0;
  }
  needed = buffer->size + additional;
  if (needed <= buffer->capacity) {
    return 1;
  }
  capacity = buffer->capacity == 0u ? 4096u : buffer->capacity;
  while (capacity < needed) {
    if (capacity > CUPIDBUILD_PROFILE_SNAPSHOT_BYTES / 2u) {
      capacity = CUPIDBUILD_PROFILE_SNAPSHOT_BYTES;
      break;
    }
    capacity *= 2u;
  }
  grown = (unsigned char *)realloc(buffer->bytes, capacity);
  if (grown == (unsigned char *)0) {
    return 0;
  }
  buffer->bytes = grown;
  buffer->capacity = capacity;
  return 1;
}

static int cupidbuild_profile_append(cupidbuild_profile_buffer_t *buffer,
                                     const void *bytes, size_t size) {
  if (!cupidbuild_profile_buffer_reserve(buffer, size)) {
    return 0;
  }
  if (size != 0u) {
    (void)memcpy(buffer->bytes + buffer->size, bytes, size);
  }
  buffer->size += size;
  return 1;
}

static int cupidbuild_profile_append_text(cupidbuild_profile_buffer_t *buffer,
                                          const char *text) {
  return cupidbuild_profile_append(buffer, text, strlen(text));
}

static int cupidbuild_profile_append_u32(cupidbuild_profile_buffer_t *buffer,
                                         size_t value) {
  unsigned char bytes[4];
  if (value > 4294967295u) {
    return 0;
  }
  bytes[0] = (unsigned char)value;
  bytes[1] = (unsigned char)(value >> 8u);
  bytes[2] = (unsigned char)(value >> 16u);
  bytes[3] = (unsigned char)(value >> 24u);
  return cupidbuild_profile_append(buffer, bytes, sizeof(bytes));
}

static int cupidbuild_profile_append_snapshot_text(
    cupidbuild_profile_buffer_t *buffer, const char *text) {
  size_t size = strlen(text);
  return cupidbuild_profile_append_u32(buffer, size) &&
         cupidbuild_profile_append(buffer, text, size);
}

static int cupidbuild_profile_append_snapshot_sources(
    cupidbuild_profile_buffer_t *buffer, const char *const *sources,
    size_t count) {
  size_t index;
  if (!cupidbuild_profile_append_u32(buffer, count)) {
    return 0;
  }
  for (index = 0u; index < count; index++) {
    if (!cupidbuild_profile_append_snapshot_text(buffer, sources[index])) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_profile_append_snapshot_profile(
    cupidbuild_profile_buffer_t *buffer, const char *name,
    const cupidbuild_host_path_list_t *headers,
    const char *const *sources, size_t source_count) {
  size_t index;
  if (!cupidbuild_profile_append_snapshot_text(buffer, name) ||
      !cupidbuild_profile_append_u32(buffer, headers->count)) {
    return 0;
  }
  for (index = 0u; index < headers->count; index++) {
    if (!cupidbuild_profile_append_snapshot_text(buffer,
                                                 headers->paths[index])) {
      return 0;
    }
  }
  return cupidbuild_profile_append_snapshot_sources(buffer, sources,
                                                     source_count);
}

static int cupidbuild_profile_snapshot(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_profile_membership_t *membership,
    cupidbuild_profile_header_t *headers,
    cupidbuild_profile_buffer_t *snapshot) {
  static const unsigned char magic[8] = {'C', 'U', 'P', 'R',
                                          'O', 'F', '1', 0};
  size_t index;
  if (!cupidbuild_profile_append(snapshot, magic, sizeof(magic)) ||
      !cupidbuild_profile_append_snapshot_text(
          snapshot, "cupid.doom-profile-inputs.v1") ||
      !cupidbuild_profile_append_u32(snapshot, 2u) ||
      !cupidbuild_profile_append_snapshot_profile(
          snapshot, "doom-compat", &membership->headers,
          cupidbuild_profile_compat_sources,
          cupidbuild_profile_compat_source_count()) ||
      !cupidbuild_profile_append_snapshot_profile(
          snapshot, "doom-tree", &membership->headers,
          cupidbuild_profile_tree_sources,
          cupidbuild_profile_tree_source_count()) ||
      !cupidbuild_profile_append_u32(snapshot, membership->headers.count)) {
    return 0;
  }
  for (index = 0u; index < membership->headers.count; index++) {
    unsigned char *contents;
    size_t size = 0u;
    if (!cupidbuild_profile_append_snapshot_text(snapshot,
                                                 headers[index].path)) {
      return 0;
    }
    contents = cupidbuild_host_read_frozen_input(
        transaction, headers[index].frozen_path, CUPIDBUILD_TOOL_BYTES, &size);
    if (contents == (unsigned char *)0 ||
        !cupidbuild_profile_append_u32(snapshot, size) ||
        !cupidbuild_profile_append(snapshot, contents, size)) {
      free(contents);
      return 0;
    }
    headers[index].size = size;
    cupidbuild_host_sha256_bytes(contents, size, headers[index].sha256);
    free(contents);
  }
  return 1;
}

static int cupidbuild_profile_append_json_path_array(
    cupidbuild_profile_buffer_t *json, const char *const *paths,
    size_t count) {
  size_t index;
  for (index = 0u; index < count; index++) {
    if (!cupidbuild_profile_append_text(json, "      \"") ||
        !cupidbuild_profile_append_text(json, paths[index]) ||
        !cupidbuild_profile_append_text(
            json, index + 1u == count ? "\"\n" : "\",\n")) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_profile_append_json_digest(
    cupidbuild_profile_buffer_t *json, const unsigned char digest[32]) {
  static const char hex[] = "0123456789abcdef";
  char text[65];
  size_t index;
  for (index = 0u; index < 32u; index++) {
    text[index * 2u] = hex[digest[index] >> 4u];
    text[index * 2u + 1u] = hex[digest[index] & 15u];
  }
  text[64] = '\0';
  return cupidbuild_profile_append_text(json, text);
}

static int cupidbuild_profile_render_json(
    const cupidbuild_profile_membership_t *membership,
    const cupidbuild_profile_header_t *headers,
    cupidbuild_profile_buffer_t *json) {
  char size_text[32];
  size_t index;
  if (!cupidbuild_profile_append_text(json, "{\n  \"inputs\": [\n")) {
    return 0;
  }
  for (index = 0u; index < membership->headers.count; index++) {
    int written = snprintf(size_text, sizeof(size_text), "%u",
                           (unsigned int)headers[index].size);
    if (written <= 0 || (size_t)written >= sizeof(size_text) ||
        !cupidbuild_profile_append_text(json, "    {\n      \"bytes\": ") ||
        !cupidbuild_profile_append_text(json, size_text) ||
        !cupidbuild_profile_append_text(json, ",\n      \"path\": \"") ||
        !cupidbuild_profile_append_text(json, headers[index].path) ||
        !cupidbuild_profile_append_text(
            json, "\",\n      \"sha256\": \"") ||
        !cupidbuild_profile_append_json_digest(json,
                                               headers[index].sha256) ||
        !cupidbuild_profile_append_text(
            json, index + 1u == membership->headers.count
                      ? "\"\n    }\n"
                      : "\"\n    },\n")) {
      return 0;
    }
  }
  if (!cupidbuild_profile_append_text(
          json,
          "  ],\n  \"profiles\": {\n    \"doom-compat\": [\n") ||
      !cupidbuild_profile_append_json_path_array(
          json, (const char *const *)membership->headers.paths,
          membership->headers.count) ||
      !cupidbuild_profile_append_text(
          json, "    ],\n    \"doom-tree\": [\n") ||
      !cupidbuild_profile_append_json_path_array(
          json, (const char *const *)membership->headers.paths,
          membership->headers.count) ||
      !cupidbuild_profile_append_text(
          json,
          "    ]\n  },\n  \"schema\": "
          "\"cupid.doom-profile-inputs.v1\",\n  \"sources\": {\n"
          "    \"doom-compat\": [\n") ||
      !cupidbuild_profile_append_json_path_array(
          json, cupidbuild_profile_compat_sources,
          cupidbuild_profile_compat_source_count()) ||
      !cupidbuild_profile_append_text(
          json, "    ],\n    \"doom-tree\": [\n") ||
      !cupidbuild_profile_append_json_path_array(
          json, cupidbuild_profile_tree_sources,
          cupidbuild_profile_tree_source_count()) ||
      !cupidbuild_profile_append_text(json, "    ]\n  }\n}\n")) {
    return 0;
  }
  return 1;
}

int cupidbuild_generate_profile_manifest(
    const cupidbuild_profile_request_t *request) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_host_profile_parent_t *profile_parent =
      (cupidbuild_host_profile_parent_t *)0;
  cupidbuild_seed_capture_t seed;
  cupidbuild_profile_membership_t membership;
  cupidbuild_profile_header_t *headers =
      (cupidbuild_profile_header_t *)0;
  cupidbuild_profile_buffer_t snapshot;
  cupidbuild_profile_buffer_t expected;
  cupidbuild_host_snapshot_t snapshot_capture;
  cupidbuild_host_snapshot_t candidate_capture;
  unsigned char *candidate = (unsigned char *)0;
  const char *object_arguments[5];
  size_t index;
  int status;
  int changed = 0;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  (void)memset(&membership, 0, sizeof(membership));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  (void)memset(&expected, 0, sizeof(expected));
  if (request == (const cupidbuild_profile_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0) ||
      strlen(request->output) < 5u ||
      strcmp(request->output + strlen(request->output) - 5u, ".json") != 0) {
    (void)fprintf(stderr,
                  "cupidbuild: invalid profile manifest request\n");
    return 1;
  }
  if (strcmp(request->output, CUPIDBUILD_PROFILE_OUTPUT) == 0 &&
      !cupidbuild_host_profile_parent_prepare(request->repository_root,
                                              &profile_parent)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_profile_parent_error(profile_parent));
    goto done;
  }
  if (!cupidbuild_host_profile_transaction_open(
          request->repository_root, cupidbuild_profile_compat_sources[0],
          request->output, profile_parent, &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_profile_discover(transaction, &membership)) {
    (void)fprintf(
        stderr,
        "cupidbuild: Doom profile closure is malformed or differs from the "
        "approved source cohort\n");
    goto done;
  }
  if (!cupidbuild_host_seal_discovery(transaction)) {
    (void)fprintf(stderr,
                  "cupidbuild: Doom profile directory closure cannot be "
                  "sealed\n");
    goto done;
  }
  {
    char initial_source[CUPIDBUILD_PATH_BYTES];
    if (!cupidbuild_join(initial_source, sizeof(initial_source),
                         request->repository_root,
                         membership.sources.paths[0]) ||
        !cupidbuild_host_input_matches_snapshot(
            transaction, initial_source,
            &membership.sources.snapshots[0])) {
      (void)fprintf(
          stderr,
          "cupidbuild: Doom profile source changed after discovery\n");
      goto done;
    }
  }
  if (membership.headers.count + membership.sources.count + 7u >
          CUPIDBUILD_PROFILE_TRANSACTION_INPUTS ||
      !cupidbuild_host_reserve_inputs(
          transaction,
          membership.headers.count + membership.sources.count + 7u)) {
    (void)fprintf(stderr,
                  "cupidbuild: Doom profile closure exceeds the frozen input "
                  "limit\n");
    goto done;
  }
  headers = (cupidbuild_profile_header_t *)calloc(
      membership.headers.count, sizeof(*headers));
  if (headers == (cupidbuild_profile_header_t *)0) {
    (void)fprintf(stderr,
                  "cupidbuild: Doom profile header table cannot be allocated\n");
    goto done;
  }
  for (index = 0u; index < membership.headers.count; index++) {
    char live_path[CUPIDBUILD_PATH_BYTES];
    char private_name[32];
    cupidbuild_host_snapshot_t frozen_from;
    int written = snprintf(private_name, sizeof(private_name),
                           "profile-header-%03u", (unsigned int)index);
    headers[index].path = membership.headers.paths[index];
    if (written <= 0 || (size_t)written >= sizeof(private_name) ||
        !cupidbuild_join(live_path, sizeof(live_path),
                         request->repository_root, headers[index].path) ||
        !cupidbuild_host_freeze_input(
            transaction, live_path, private_name,
            &headers[index].frozen_path,
            &frozen_from)) {
      (void)fprintf(stderr,
                    "cupidbuild: Doom profile header cannot be frozen: %s\n",
                    cupidbuild_host_error(transaction));
      goto done;
    }
    if (!cupidbuild_host_snapshot_equal(
            &frozen_from, &membership.headers.snapshots[index])) {
      (void)fprintf(
          stderr,
          "cupidbuild: Doom profile header changed after discovery: %s\n",
          headers[index].path);
      goto done;
    }
  }
  for (index = 1u; index < membership.sources.count; index++) {
    char live_path[CUPIDBUILD_PATH_BYTES];
    char private_name[32];
    const char *frozen_source = (const char *)0;
    cupidbuild_host_snapshot_t frozen_from;
    int written = snprintf(private_name, sizeof(private_name),
                           "profile-source-%03u", (unsigned int)index);
    if (written <= 0 || (size_t)written >= sizeof(private_name) ||
        !cupidbuild_join(live_path, sizeof(live_path),
                         request->repository_root,
                         membership.sources.paths[index]) ||
        !cupidbuild_host_freeze_input(
            transaction, live_path, private_name, &frozen_source,
            &frozen_from)) {
      (void)fprintf(stderr,
                    "cupidbuild: Doom profile source cannot be frozen: %s\n",
                    cupidbuild_host_error(transaction));
      goto done;
    }
    if (!cupidbuild_host_snapshot_equal(
            &frozen_from, &membership.sources.snapshots[index])) {
      (void)fprintf(
          stderr,
          "cupidbuild: Doom profile source changed after discovery: %s\n",
          membership.sources.paths[index]);
      goto done;
    }
  }
  if (!cupidbuild_seed_freeze(transaction, request->repository_root,
                              request->seed_manifest, 1, 1, &seed)) {
    goto done;
  }
  if (!cupidbuild_profile_snapshot(transaction, &membership, headers,
                                   &snapshot) ||
      !cupidbuild_host_write_private_output(transaction, snapshot.bytes,
                                            snapshot.size) ||
      !cupidbuild_host_capture_private_output(
          transaction, &snapshot_capture, (unsigned char **)0)) {
    (void)fprintf(stderr,
                  "cupidbuild: CUPROF1 snapshot could not be frozen: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  object_arguments[0] = "profile-manifest";
  object_arguments[1] = cupidbuild_host_private_output(transaction);
  object_arguments[2] = "-o";
  object_arguments[3] = cupidbuild_host_candidate(transaction);
  object_arguments[4] = (const char *)0;
  status = cupidbuild_host_run_in_private(
      transaction, seed.frozen_tools[4], object_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (status != 0) {
    cupidbuild_report_checked_failure(transaction, "checked CupidObj failed");
    goto done;
  }
  if (!cupidbuild_host_require_private_output(transaction, &snapshot_capture) ||
      !cupidbuild_host_capture_candidate(transaction, &candidate_capture,
                                         &candidate)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_profile_render_json(&membership, headers, &expected)) {
    (void)fprintf(stderr,
                  "cupidbuild: independent profile manifest renderer failed\n");
    goto done;
  }
  if (candidate_capture.size != expected.size ||
      memcmp(candidate, expected.bytes, expected.size) != 0) {
    (void)fprintf(
        stderr,
        "cupidbuild: checked CupidObj profile manifest differs from the "
        "independent renderer\n");
    goto done;
  }
  free(candidate);
  candidate = (unsigned char *)0;
  if (!cupidbuild_profile_require_membership(transaction,
                                             &membership)) {
    (void)fprintf(stderr,
                  "cupidbuild: Doom profile input membership changed while "
                  "authoring the manifest\n");
    goto done;
  }
  if (!cupidbuild_seed_require_live(transaction, &seed) ||
      !cupidbuild_host_require_frozen_inputs(transaction) ||
      !cupidbuild_host_require_private_output(transaction, &snapshot_capture) ||
      !cupidbuild_host_require_candidate(transaction, &candidate_capture) ||
      (profile_parent != (cupidbuild_host_profile_parent_t *)0 &&
       !cupidbuild_host_profile_parent_bind(profile_parent, transaction)) ||
      !cupidbuild_host_require_publication_boundary(transaction)) {
    (void)fprintf(stderr,
                  "cupidbuild: profile manifest publication failed: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_profile_require_membership(transaction, &membership)) {
    (void)fprintf(
        stderr,
        "cupidbuild: Doom profile input membership changed at publication "
        "boundary\n");
    goto done;
  }
  if (!cupidbuild_host_publish_if_changed(transaction, &changed)) {
    (void)fprintf(stderr,
                  "cupidbuild: profile manifest publication failed: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  (void)changed;
  cupidbuild_host_profile_parent_commit(profile_parent);
  result = 0;

done:
  free(candidate);
  free(expected.bytes);
  free(snapshot.bytes);
  free(headers);
  cupidbuild_profile_membership_close(&membership);
  cupidbuild_seed_capture_close(&seed);
  result = cupidbuild_finish_publication(transaction, result,
                                         "profile manifest");
  if (!cupidbuild_host_profile_parent_close(profile_parent)) {
    (void)fprintf(stderr,
                  "cupidbuild: profile parent cleanup failed\n");
    result = 1;
  }
  return result;
}

static void cupidbuild_compile_put_u32(unsigned char *bytes, size_t value) {
  bytes[0] = (unsigned char)value;
  bytes[1] = (unsigned char)(value >> 8u);
  bytes[2] = (unsigned char)(value >> 16u);
  bytes[3] = (unsigned char)(value >> 24u);
}

static int cupidbuild_compile_write_bundle(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_compile_input_t *inputs, size_t count,
    cupidbuild_host_snapshot_t *bundle_snapshot) {
  size_t total = 12u;
  size_t offset = 12u;
  size_t index;
  unsigned char *bundle;
  int result = 0;
  if (count == 0u || count > CUPIDBUILD_PROFILE_TRANSACTION_INPUTS) {
    return 0;
  }
  for (index = 0u; index < count; index++) {
    size_t overhead = 9u + strlen(inputs[index].path);
    if ((index != 0u &&
         strcmp(inputs[index - 1u].path, inputs[index].path) >= 0) ||
        overhead > CUPIDBUILD_TOOL_BYTES - total ||
        inputs[index].snapshot.size > CUPIDBUILD_TOOL_BYTES - total - overhead) {
      return 0;
    }
    total += overhead + inputs[index].snapshot.size;
  }
  bundle = (unsigned char *)malloc(total);
  if (bundle == (unsigned char *)0) {
    return 0;
  }
  (void)memcpy(bundle, "CUPSRC1\n", 8u);
  cupidbuild_compile_put_u32(bundle + 8u, count);
  for (index = 0u; index < count; index++) {
    size_t size = 0u;
    size_t path_size = strlen(inputs[index].path) + 1u;
    unsigned char *contents = cupidbuild_host_read_frozen_input(
        transaction, inputs[index].frozen_path, CUPIDBUILD_TOOL_BYTES, &size);
    if (contents == (unsigned char *)0 || size != inputs[index].snapshot.size) {
      free(contents);
      goto done;
    }
    cupidbuild_compile_put_u32(bundle + offset, path_size);
    cupidbuild_compile_put_u32(bundle + offset + 4u, size);
    offset += 8u;
    bundle[offset++] = '/';
    (void)memcpy(bundle + offset, inputs[index].path, path_size - 1u);
    offset += path_size - 1u;
    if (size != 0u) {
      (void)memcpy(bundle + offset, contents, size);
    }
    offset += size;
    free(contents);
  }
  result = offset == total &&
           cupidbuild_host_write_private_output(transaction, bundle, total) &&
           cupidbuild_host_capture_private_output(transaction, bundle_snapshot,
                                                    (unsigned char **)0);
done:
  free(bundle);
  return result;
}

static int cupidbuild_compile_capture_input(
    cupidbuild_host_transaction_t *transaction, const char *root,
    const char *source, const char *path, size_t index,
    const cupidbuild_host_snapshot_t *discovered,
    cupidbuild_compile_input_t *input) {
  char live_path[CUPIDBUILD_PATH_BYTES];
  char private_name[32];
  int written = snprintf(private_name, sizeof(private_name),
                         "compile-input-%03u", (unsigned int)index);
  (void)memset(input, 0, sizeof(*input));
  input->path = path;
  if (written <= 0 || (size_t)written >= sizeof(private_name) ||
      !cupidbuild_join(live_path, sizeof(live_path), root, path)) {
    return 0;
  }
  if (strcmp(path, source) == 0) {
    unsigned char *contents;
    if (discovered != (const cupidbuild_host_snapshot_t *)0 &&
        !cupidbuild_host_input_matches_snapshot(transaction, live_path,
                                                 discovered)) {
      return 0;
    }
    input->frozen_path = cupidbuild_host_frozen_source(transaction);
    contents = cupidbuild_host_read_frozen_input(
        transaction, input->frozen_path, CUPIDBUILD_TOOL_BYTES,
        &input->snapshot.size);
    if (contents == (unsigned char *)0) {
      return 0;
    }
    free(contents);
    return 1;
  }
  return cupidbuild_host_freeze_input(transaction, live_path, private_name,
                                       &input->frozen_path, &input->snapshot) &&
         (discovered == (const cupidbuild_host_snapshot_t *)0 ||
          cupidbuild_host_snapshot_equal(&input->snapshot, discovered));
}

static int cupidbuild_compile_bundle(
    cupidbuild_host_transaction_t *transaction, const char *root,
    const cupidbuild_compile_closure_t *closure,
    cupidbuild_host_snapshot_t *bundle_snapshot) {
  cupidbuild_compile_input_t inputs[CUPIDBUILD_COMPILE_INPUTS];
  size_t index;
  int source_found = 0;
  if (closure == (const cupidbuild_compile_closure_t *)0 ||
      closure->source == (const char *)0 || closure->count == 0u ||
      closure->count > CUPIDBUILD_COMPILE_INPUTS ||
      !cupidbuild_host_reserve_inputs(transaction, closure->count + 7u)) {
    return 0;
  }
  for (index = 0u; index < closure->count; index++) {
    if (closure->inputs[index] == (const char *)0 ||
        !cupidbuild_compile_capture_input(transaction, root, closure->source,
                                           closure->inputs[index], index,
                                           (const cupidbuild_host_snapshot_t *)0,
                                           &inputs[index])) {
      return 0;
    }
    if (strcmp(closure->inputs[index], closure->source) == 0) {
      source_found = 1;
    }
  }
  return source_found && cupidbuild_compile_write_bundle(
                             transaction, inputs, closure->count, bundle_snapshot);
}

static int cupidbuild_compile_doom_bundle(
    cupidbuild_host_transaction_t *transaction, const char *root,
    const char *source, cupidbuild_host_snapshot_t *bundle_snapshot) {
  cupidbuild_profile_membership_t membership;
  cupidbuild_compile_input_t *inputs = (cupidbuild_compile_input_t *)0;
  cupidbuild_compile_input_t selected;
  size_t index;
  size_t insertion = 0u;
  size_t total;
  int source_found = 0;
  int result = 0;
  (void)memset(&membership, 0, sizeof(membership));
  (void)memset(&selected, 0, sizeof(selected));
  if (!cupidbuild_host_begin_compile_discovery(transaction) ||
      !cupidbuild_profile_discover(transaction, &membership) ||
      !cupidbuild_host_seal_compile_discovery(transaction)) {
    (void)fprintf(stderr, "cupidbuild: Doom compiler input membership is invalid\n");
    goto done;
  }
  total = membership.headers.count + membership.sources.count + 7u;
  if (total > CUPIDBUILD_PROFILE_TRANSACTION_INPUTS ||
      !cupidbuild_host_reserve_inputs(transaction, total)) {
    (void)fprintf(stderr, "cupidbuild: Doom compiler input count exceeds the limit\n");
    goto done;
  }
  inputs = (cupidbuild_compile_input_t *)calloc(membership.headers.count + 1u,
                                                sizeof(*inputs));
  if (inputs == (cupidbuild_compile_input_t *)0) {
    goto done;
  }
  for (index = 0u; index < membership.headers.count; index++) {
    if (!cupidbuild_compile_capture_input(
            transaction, root, source, membership.headers.paths[index], index,
            &membership.headers.snapshots[index], &inputs[index])) {
      goto done;
    }
  }
  for (index = 0u; index < membership.sources.count; index++) {
    cupidbuild_compile_input_t captured;
    if (!cupidbuild_compile_capture_input(
            transaction, root, source, membership.sources.paths[index],
            membership.headers.count + index,
            &membership.sources.snapshots[index], &captured)) {
      goto done;
    }
    if (strcmp(captured.path, source) == 0) {
      selected = captured;
      source_found = 1;
    }
  }
  if (!source_found) {
    goto done;
  }
  while (insertion < membership.headers.count &&
         strcmp(inputs[insertion].path, source) < 0) {
    insertion++;
  }
  for (index = membership.headers.count; index > insertion; index--) {
    inputs[index] = inputs[index - 1u];
  }
  inputs[insertion] = selected;
  result = cupidbuild_compile_write_bundle(transaction, inputs,
                                             membership.headers.count + 1u,
                                             bundle_snapshot);
done:
  free(inputs);
  cupidbuild_profile_membership_close(&membership);
  return result;
}

static int cupidbuild_compile(
    const cupidbuild_compile_request_t *request, cupidbuild_compile_kind_t kind) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_seed_capture_t seed;
  const cupidbuild_compile_closure_t *closure =
      (const cupidbuild_compile_closure_t *)0;
  cupidbuild_host_snapshot_t bundle_snapshot;
  cupidbuild_host_snapshot_t candidate_snapshot;
  unsigned char *candidate = (unsigned char *)0;
  char logical_source[CUPIDBUILD_PATH_BYTES];
  char compiler_root[CUPIDBUILD_PATH_BYTES];
  char logical_output[CUPIDBUILD_PATH_BYTES];
  char expected_output[CUPIDBUILD_PATH_BYTES];
  const char *arguments[80];
  const char *candidate_path;
  const char *separator;
  size_t index;
  size_t count = 0u;
  size_t source_size;
  int doom = kind == CUPIDBUILD_COMPILE_DOOM;
  int production = kind == CUPIDBUILD_COMPILE_PRODUCTION;
  const char *cohort = doom ? "Doom" : (production ? "production" : "kernel");
  const cupidbuild_compile_closure_t *closures = production
      ? cupidbuild_compile_production_closures : cupidbuild_compile_closures;
  size_t closure_count = production
      ? sizeof(cupidbuild_compile_production_closures) /
            sizeof(cupidbuild_compile_production_closures[0])
      : sizeof(cupidbuild_compile_closures) / sizeof(cupidbuild_compile_closures[0]);
  int doom_tree = 0;
  const char *const *profile = cupidbuild_compile_profile;
  size_t profile_count = sizeof(cupidbuild_compile_profile) /
                         sizeof(cupidbuild_compile_profile[0]);
  int status;
  int changed = 0;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  if (request == (const cupidbuild_compile_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->source, 1) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0)) {
    (void)fprintf(stderr, "cupidbuild: invalid %s compile request\n",
                  cohort);
    return 1;
  }
  if (doom != 0) {
    size_t source_count = cupidbuild_profile_compat_source_count() +
                          cupidbuild_profile_tree_source_count();
    for (index = 0u; index < source_count; index++) {
      if (strcmp(request->source, cupidbuild_profile_expected_source(index)) == 0) {
        break;
      }
    }
    if (index == source_count) {
      (void)fprintf(stderr, "cupidbuild: source is outside the approved Doom cohort\n");
      return 1;
    }
    doom_tree = index >= cupidbuild_profile_compat_source_count();
    profile = cupidbuild_compile_doom_profile;
    profile_count = sizeof(cupidbuild_compile_doom_profile) /
                    sizeof(cupidbuild_compile_doom_profile[0]);
  } else {
    for (index = 0u; index < closure_count; index++) {
      if (strcmp(request->source, closures[index].source) == 0) {
        closure = &closures[index];
        break;
      }
    }
    if (closure == (const cupidbuild_compile_closure_t *)0) {
      (void)fprintf(stderr, "cupidbuild: source has no approved frozen %s closure\n", cohort);
      return 1;
    }
  }
  source_size = strlen(request->source);
  (void)memcpy(expected_output, request->source, source_size - 2u);
  expected_output[source_size - 2u] = 'o';
  expected_output[source_size - 1u] = '\0';
  if (strcmp(request->output, expected_output) != 0) {
    (void)fprintf(stderr, "cupidbuild: %s source and output binding differ\n",
                  cohort);
    return 1;
  }
  logical_source[0] = '/';
  (void)memcpy(logical_source + 1u, request->source, source_size + 1u);
  if (!cupidbuild_host_transaction_open(request->repository_root,
                                        request->source, request->output,
                                        &transaction)) {
    goto host_failure;
  }
  if (!(doom != 0
            ? cupidbuild_compile_doom_bundle(transaction, request->repository_root,
                                               request->source, &bundle_snapshot)
            : cupidbuild_compile_bundle(transaction, request->repository_root,
                                          closure, &bundle_snapshot))) {
    (void)fprintf(stderr, "cupidbuild: frozen compiler closure cannot be captured\n");
    goto host_failure;
  }
  if (!cupidbuild_seed_freeze(transaction, request->repository_root,
                              request->seed_manifest, 1, 1, &seed)) {
    goto done;
  }
  candidate_path = cupidbuild_host_candidate(transaction);
  separator = strrchr(candidate_path, '/');
  if (separator == (const char *)0 || separator == candidate_path ||
      (size_t)(separator - candidate_path) >= sizeof(compiler_root) ||
      strlen(separator) >= sizeof(logical_output)) {
    goto host_failure;
  }
  (void)memcpy(compiler_root, candidate_path,
               (size_t)(separator - candidate_path));
  compiler_root[separator - candidate_path] = '\0';
  (void)memcpy(logical_output, separator, strlen(separator) + 1u);
  arguments[count++] = "-c";
  arguments[count++] = logical_source;
  arguments[count++] = "-o";
  arguments[count++] = logical_output;
  if (profile_count + 9u +
          (doom_tree ? sizeof(cupidbuild_compile_doom_tree_extra) /
                           sizeof(cupidbuild_compile_doom_tree_extra[0]) : 0u) >
      sizeof(arguments) / sizeof(arguments[0])) {
    goto host_failure;
  }
  for (index = 0u; index < profile_count; index++) {
    arguments[count++] = profile[index];
  }
  if (doom_tree) {
    for (index = 0u; index < sizeof(cupidbuild_compile_doom_tree_extra) /
                                sizeof(cupidbuild_compile_doom_tree_extra[0]); index++) {
      arguments[count++] = cupidbuild_compile_doom_tree_extra[index];
    }
  }
  arguments[count++] = "--root";
  arguments[count++] = compiler_root;
  arguments[count++] = "--source-bundle";
  arguments[count++] = cupidbuild_host_private_output(transaction);
  arguments[count] = (const char *)0;
  if (kind != CUPIDBUILD_COMPILE_KERNEL &&
      !cupidbuild_host_require_publication_boundary(transaction)) {
    goto host_failure;
  }
  status = cupidbuild_host_run(transaction, seed.frozen_tools[1], arguments,
                               strcmp(request->source, "kernel/cpu/ksyms_data.cc") == 0
                                   ? 600000u : 180000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_private_output(transaction, &bundle_snapshot)) {
    goto host_failure;
  }
  if (status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidC failed\n");
    goto done;
  }
  if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot,
                                         &candidate)) {
    goto host_failure;
  }
  if (!cupidbuild_validate_compiler_object_bytes(candidate,
                                                 candidate_snapshot.size)) {
    (void)fprintf(stderr, "cupidbuild: compiler object validation failed\n");
    goto done;
  }
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot) ||
      !cupidbuild_host_require_publication_boundary(transaction) ||
      !cupidbuild_host_publish_if_changed(transaction, &changed)) {
    goto host_failure;
  }
  result = 0;
  goto done;
host_failure:
  (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
done:
  free(candidate);
  cupidbuild_seed_capture_close(&seed);
  return cupidbuild_finish_publication(transaction, result,
                                         doom ? "Doom compiler object"
                                              : (production ? "production compiler object"
                                                            : "kernel compiler object"));
}

int cupidbuild_compile_kernel(const cupidbuild_compile_request_t *request) {
  return cupidbuild_compile(request, CUPIDBUILD_COMPILE_KERNEL);
}

int cupidbuild_compile_doom(const cupidbuild_compile_request_t *request) {
  return cupidbuild_compile(request, CUPIDBUILD_COMPILE_DOOM);
}

int cupidbuild_compile_production(const cupidbuild_compile_request_t *request) {
  return cupidbuild_compile(request, CUPIDBUILD_COMPILE_PRODUCTION);
}

int cupidbuild_run_checked_tool(const cupidbuild_run_request_t *request) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_seed_capture_t seed;
  size_t tool_index;
  int status;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  if (request == (const cupidbuild_run_request_t *)0 ||
      !cupidbuild_path_safe(request->working_directory, 0) ||
      !cupidbuild_path_safe(request->seed_manifest, 0) ||
      request->arguments == (const char *const *)0 ||
      request->tool == (const char *)0 ||
      (strcmp(request->tool, "cupidc") != 0 &&
       strcmp(request->tool, "cupidobj") != 0 &&
       strcmp(request->tool, "cupidld") != 0) ||
      request->timeout_seconds == 0u || request->timeout_seconds > 86400u) {
    (void)fprintf(stderr, "cupidbuild: invalid checked tool request\n");
    return 1;
  }
  if (strcmp(request->tool, "cupidc") == 0) {
    tool_index = 1u;
  } else if (strcmp(request->tool, "cupidld") == 0) {
    tool_index = 3u;
  } else {
    tool_index = 4u;
  }
  if (!cupidbuild_host_runner_open(request->working_directory,
                                    &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_seed_freeze(transaction, request->working_directory,
                              request->seed_manifest, 0, 1, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_frozen_inputs(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  status = cupidbuild_host_run_captured(
      transaction, seed.frozen_tools[tool_index], request->arguments,
      request->timeout_seconds * 1000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_frozen_inputs(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (status == -2) {
    (void)fprintf(stderr, "cupidbuild: checked %s timed out\n",
                  cupidbuild_seed_names[tool_index]);
    goto done;
  }
  if (status < 0) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_host_forward_captured(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = status;

done:
  cupidbuild_seed_capture_close(&seed);
  if (!cupidbuild_host_transaction_close(transaction)) {
    (void)fprintf(stderr,
                  "cupidbuild: private checked-tool cleanup failed\n");
    if (result == 0) {
      result = 1;
    }
  }
  return result;
}
