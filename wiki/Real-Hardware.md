# Running CupidOS on Real Hardware

CupidOS is a 32-bit x86 BIOS-boot OS. It boots on real hardware, but the hardware must look like an early-2000s PC: legacy BIOS, IDE/AHCI disk, VBE-capable GPU, PS/2 or USB HID input. This page walks through exactly what is needed and how to install.

> **Warning:** The install procedure overwrites the target disk's first sectors and partition table. Any existing data on the target is lost. Double-check the device path before running `dd`.

---

## 1. Hardware requirements

| Component | Requirement | Notes |
|-----------|-------------|-------|
| CPU | 32-bit x86 with FPU + SSE/SSE2 | Any Pentium 4 or newer. Long-mode CPUs also work (CupidOS stays in 32-bit protected mode). |
| Firmware | **Legacy BIOS** or **UEFI with CSM** | No pure-UEFI boot. CSM / "Legacy Boot" / "Other OS" must be enabled. |
| RAM | 32 MB minimum, 128 MB+ recommended | Kernel identity-maps 0 – 32 MB; heap and JIT regions live below that. |
| Storage | IDE / SATA / USB-attached disk ≥ 200 MB | Kernel uses ATA PIO (primary channel) or USB Mass Storage. NVMe is not supported. |
| GPU | VBE 3.0 with mode 0x118 (640×480×32bpp linear framebuffer) | Most pre-2015 Intel, AMD, and NVIDIA GPUs work in CSM mode. Some newer GPUs lose VBE when CSM is off. |
| Keyboard | PS/2 or USB HID | USB 1.1 / 2.0 keyboards work via the built-in UHCI + EHCI HID driver. |
| Mouse | PS/2 or USB HID | Same as keyboard. |
| NIC (optional) | Realtek RTL8139 or Intel E1000 | Anything else will not get networking. |
| Audio (optional) | PC speaker on port 0x61 | No HDA / AC'97 driver. |
| SMP (optional) | ACPI MADT / Intel MP table | Up to 32 CPUs. |

What does **not** work:
- Pure-UEFI boot (no CSM)
- Secure Boot (must be disabled)
- NVMe, SCSI HBAs, RAID controllers
- GPUs in non-CSM mode that drop VBE
- Wi-Fi, Bluetooth, modern sound, GPU acceleration
- Laptops with no PS/2 port where USB HID is not enumerated by the BIOS during boot (rare)

---

## 2. Build the disk image on a Linux host

On a Linux machine with `nasm`, `gcc-multilib`, `make`, `mtools`, and `dosfstools`:

```bash
git clone <your cupid-os repo>
cd cupid-os
make HDD_MB=200
```

Result: `cupidos.img` — a full 200 MB MBR disk image containing:

```
LBA 0        MBR + Stage 1 bootloader
LBA 1-4      Stage 2 bootloader
LBA 5-4095   Kernel (up to ~2 MB)
LBA 4096+    FAT16 partition (mounted as /disk and backing /home)
```

Because the image already contains a real MBR + partition table, it can be written byte-for-byte to any disk that is at least `HDD_MB` in size.

Optional: seed `/home` with files before flashing.

```bash
mcopy -o -i cupidos.img@@2097152 myfile.txt ::/myfile.txt
mdir  -i cupidos.img@@2097152 ::/
```

---

## 3. Pick the install target

Two common paths:

1. **USB stick** (recommended for first boot) — boot the machine off the stick, leave internal storage alone.
2. **Dedicated internal disk** — permanent install. Make sure nothing else on the machine needs it.

Both use the same procedure: `dd` the image to the whole device (not a partition).

### Identify the device

Plug in the target and find its node:

```bash
lsblk -o NAME,SIZE,MODEL,TRAN
# or
sudo dmesg | tail -20
```

You want the **whole device**, e.g. `/dev/sdb`, not `/dev/sdb1`. Confirm the size matches what you expect so you don't overwrite the wrong disk.

### Unmount anything already mounted from it

```bash
sudo umount /dev/sdb*  2>/dev/null
```

---

## 4. Flash the image

```bash
sudo dd if=cupidos.img of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

Replace `/dev/sdX` with the device node from step 3. `conv=fsync` + `sync` make sure the write reaches the disk before you pull the stick.

If the target disk is larger than `HDD_MB`, the extra space is unused. You can later extend the FAT16 partition with `fdisk` + `mkfs.fat` if you want it.

---

## 5. BIOS / firmware setup

Enter firmware setup on the target machine (usually `Del`, `F2`, `F10`, or `Esc` at power-on) and set:

| Setting | Value |
|---------|-------|
| Boot mode | **Legacy** / **CSM** / **Other OS** |
| Secure Boot | **Disabled** |
| Fast Boot | **Disabled** (so USB devices enumerate before boot) |
| SATA mode | **IDE** or **AHCI** (AHCI works via the ATA driver's LBA fallback; IDE is the safest choice) |
| USB Legacy Support | **Enabled** (lets the BIOS present USB HID as PS/2 until CupidOS takes over) |
| Boot order | Put the flashed device first |

Save and exit.

---

## 6. First boot

Power on. You should see, in order:

1. A brief `CupidOS Stage 1` / `Stage 2` banner from the bootloader (serial only if a null-modem cable is attached; otherwise silent until VBE).
2. VBE mode switch to 640×480×32bpp — screen goes black then shows the pastel desktop.
3. The taskbar with clock, desktop icons, and mouse cursor.

If the GPU refuses VBE mode 0x118 the screen stays black. See *Troubleshooting* below.

Open the **Terminal** icon and try:

```sh
sysinfo
mount
ls /home
ps
```

`mount` should list `/`, `/dev`, `/disk` (fat16), and `/home` (homefs).

On the very first boot CupidOS imports any existing FAT16 files into `homefs` and writes `HOMEFS.SYS`. Subsequent boots read `/home` straight from `HOMEFS.SYS`.

Always run `sync` before powering off, or you may lose writes that are still in the 64-entry block cache.

---

## 7. Serial console (optional but recommended)

The kernel logs every subsystem init and any panic to COM1 at 115200 8N1. On real hardware this is the fastest way to see what is happening during a silent hang.

1. Connect a USB-to-serial adapter from the target machine's COM1 header (or a PCI serial card) to the host.
2. On the host:

   ```bash
   sudo minicom -b 115200 -D /dev/ttyUSB0
   # or
   screen /dev/ttyUSB0 115200
   ```

3. Power on the target. You should see boot messages, driver probes, ATA IDENTIFY output, and so on.

If you ever get a silent boot, attach serial first and reboot — panics are printed there before the machine halts.

---

## 8. Networking on real hardware

CupidOS only drives two NICs:

| Chip | PCI IDs | Notes |
|------|---------|-------|
| Realtek RTL8139 | `10ec:8139` | Cheapest option; common on PCI add-in cards. |
| Intel E1000 / 82540-series | `8086:100e`, `8086:100f`, `8086:1004` | Common on Intel desktop boards 2000 – 2008. |

Check with `lspci -nn` on any live Linux on the same machine. If the NIC is anything else (Realtek RTL8169, Intel I210/I219/I225, Broadcom, etc.) there is no driver — networking will not come up.

At runtime:

```sh
ifconfig        # show link + IP
dhcp            # force DHCP refresh
ping 1.1.1.1
resolve example.com
```

DHCP has a static fallback configured at build time if your network has no DHCP server.

---

## 9. SMP notes

CupidOS discovers additional cores through the ACPI MADT (preferred) or the Intel MP table. On modern BIOSes ACPI is always present. No configuration is needed — boot, then run `ps` or `sysinfo` and you should see more than one CPU listed.

If only one CPU shows:
- ACPI might be disabled in firmware — re-enable it.
- The machine may have > 32 logical CPUs; CupidOS caps at 32 and ignores the rest.

---

## 10. Troubleshooting

### Black screen after the Stage 2 banner
VBE mode 0x118 was rejected by the GPU. Try:
- Enable CSM in firmware (pure-UEFI GPUs often lose VBE).
- Swap in an older PCIe GPU.
- On some Intel iGPUs, disabling "Above 4G Decoding" re-enables legacy VBE.

Serial will show `VBE: mode 0x118 not supported` in this case.

### `Disk error!` / machine reboots after the MBR
The BIOS booted the device but the kernel load failed. Causes:
- Image was written to a partition (`/dev/sdb1`) instead of the whole device (`/dev/sdb`).
- The target disk is smaller than the image — `dd` silently truncated.
- SATA controller is in a mode the ATA driver doesn't understand. Switch to IDE/legacy in firmware.

### Keyboard / mouse dead on the desktop
- Enable "USB Legacy Support" in firmware. Without it, USB HID is only alive after CupidOS's USB stack initializes, which takes ~1 s after boot — any input before then is dropped.
- If the keyboard is Bluetooth or has a proprietary dongle, it will not work; use a wired PS/2 or standard USB HID device.

### `ATA: no drive on primary channel`
- SATA mode is set to something exotic (RAID, Optane). Switch to AHCI or IDE.
- The target disk is on the secondary channel or on an add-in HBA. CupidOS only probes the primary ATA channel and any USB MSC devices.

### Clock is wrong
The RTC driver reads CMOS in BCD. If firmware stores time in binary, the clock will look garbled. Set the firmware clock to BCD mode, or ignore the taskbar clock and use `date` manually.

### Power off
Always run `sync` in the shell, then hold the power button. CupidOS has no ACPI shutdown path — the `reboot` command triggers a keyboard-controller reset, and pulling power is otherwise safe once `sync` has returned.

---

## 11. Restoring the disk afterwards

A CupidOS-flashed disk has a valid MBR partition table with one FAT16 partition. To reuse the disk for something else:

```bash
sudo wipefs -a /dev/sdX
sudo parted /dev/sdX mklabel gpt   # or msdos
```

Then repartition normally.

---

## See also

- [Getting Started](Getting-Started) — build and QEMU workflow
- [Disk Setup](Disk-Setup) — FAT16 layout and mtools recipes
- [Networking](Networking) — NIC drivers, sockets, DHCP/DNS
- [SMP](SMP) — multi-CPU discovery and scheduling
- [USB](USB) — UHCI/EHCI, HID, MSC, hubs
