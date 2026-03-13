# ShofEL2 for Jibo (Tegra T124)

A Fusée Gelée / ShofEL2 exploit port for the NVIDIA Tegra T124 (K1), targeting the **Jibo social robot** (Toradex Apalis TK1 module). Allows booting arbitrary payloads via USB RCM mode, dumping fuses and memory, and reading/writing the eMMC.

Based on the original ShofEL2 code and Katherine Temkin's research. See:
- https://fail0verflow.com/blog/2018/shofel2/
- https://github.com/Qyriad/fusee-launcher/blob/master/report/fusee_gelee.md

---

## Obligatory Disclaimer

This code is provided without any warranty. Use at your own responsibility.

---

## Hardware

| Component | Details |
|-----------|---------|
| Device | Jibo social robot |
| SoC | NVIDIA Tegra T124 (K1) |
| Module | Toradex Apalis TK1 |
| eMMC | Hynix HAG4a2, ~14.79 GiB |
| USB VID:PID (RCM) | 0x0955:0x7740 |

---

## Full Setup — Fresh Ubuntu PC

Tested on **Ubuntu 20.04 LTS** and **22.04 LTS**. Earlier versions (18.04) compile but USB does not reliably smash the stack. **Ubuntu 20.04+ is required.**

### 1. Install dependencies

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    libusb-1.0-0-dev \
    git \
    usbutils
```

### 2. Clone the repo

```bash
git clone https://github.com/bliteknight/ShofEL2-for-Jibo.git
cd ShofEL2-for-Jibo
git checkout t124
```

### 3. Build

```bash
make
```

This produces:
- `shofel2_t124` — the x86 host tool
- `emmc_server.bin` — ARM payload for eMMC read/write
- `mem_dumper_usb_server.bin` — ARM payload for memory dumps
- `boot_bct.bin`, `intermezzo.bin`, and other payloads

### 4. USB permissions (avoid running as root every time)

Create a udev rule so your user can access the Jibo USB device without `sudo`:

```bash
sudo tee /etc/udev/rules.d/99-jibo-rcm.rules <<'EOF'
# Jibo (Tegra T124 RCM)
SUBSYSTEM=="usb", ATTR{idVendor}=="0955", ATTR{idProduct}=="7740", MODE="0666"
# Jetson TK1 RCM (fallback)
SUBSYSTEM=="usb", ATTR{idVendor}=="0955", ATTR{idProduct}=="7140", MODE="0666"
# Shield TK1 RCM (fallback)
SUBSYSTEM=="usb", ATTR{idVendor}=="0955", ATTR{idProduct}=="7f40", MODE="0666"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

> **VM users:** If running in VirtualBox or VMware, make sure USB 3.0 (xHCI) is enabled for the VM. USB 2.0 (EHCI) may fail to smash the stack correctly.

---

## Putting Jibo into RCM Mode

Jibo exposes the Tegra T124 RCM (Recovery Mode) button on its base. There are two buttons on the underside — a reset button and a smaller button below it. The smaller button below the reset button is the RCM button.

**Method 1 — Power on:**
1. Power off Jibo completely.
2. Hold down the **RCM button** (the button below the reset button on the base).
3. While holding it, apply power.
4. Release the button once powered on.

**Method 2 — Reset:**
1. Hold down the **RCM button** (the button below the reset button on the base).
2. While holding it, press the reset button.
3. Release both buttons.

In either case, Jibo will enumerate on USB as `0955:7740` instead of booting normally.

Verify with:
```bash
lsusb | grep 0955
```

---

## Usage

```
./shofel2_t124 [--port SYSFS_PATH] ( MEM_DUMP | READ_FUSES | BOOT_BCT | PAYLOAD | DUMP_STACK | EMMC_STATUS | EMMC_READ | EMMC_WRITE ) [options]
```

| Option / Command | Arguments | Description |
|------------------|-----------|-------------|
| `--port SYSFS_PATH` | e.g. `1-2` or `1-2.3` | Only connect to the device at this USB port. Useful when multiple Jibos are connected simultaneously. |
| `MEM_DUMP` | `address length out_file` | Dump `length` bytes from `address` to file |
| `READ_FUSES` | `out_file` | Dump T124 fuses to file and print to console |
| `BOOT_BCT` | — | Boot BCT without applying locks |
| `PAYLOAD` | `payload.bin [arm\|thumb]` | Execute a custom payload (thumb mode default) |
| `DUMP_STACK` | — | Dump the stack before smashing it |
| `EMMC_STATUS` | — | Initialize eMMC and dump all diagnostic registers |
| `EMMC_READ` | `start_sector num_sectors out_file` | Read sectors from eMMC to file |
| `EMMC_WRITE` | `start_sector in_file` | Write file to eMMC at start_sector |

All sector/address arguments are hex values.

### Targeting a specific USB port

When multiple Jibos are connected, use `--port` to target a specific one:

```bash
# Find the sysfs port paths
ls /sys/bus/usb/devices/ | grep -v :

# Back up two Jibos simultaneously in separate terminals
sudo ./shofel2_t124 --port 1-2 EMMC_READ 0 1D70000 ~/jibo1.img
sudo ./shofel2_t124 --port 1-3 EMMC_READ 0 1D70000 ~/jibo2.img
```

The port path is the sysfs device name (e.g. `1-2` = bus 1, port 2). For a device connected through a USB hub it will look like `1-2.3`.

---

## Dumping the Jibo eMMC

### Step 1 — Verify eMMC initialization

Always run `EMMC_STATUS` first to confirm the controller initialized correctly before attempting a full dump.

Put Jibo in RCM mode, then:

```bash
sudo ./shofel2_t124 EMMC_STATUS
```

A successful run ends with:
```
RESULT: === FULLY INITIALIZED! ===
```

Key things to check in the output:
- `Int Clock Stable: YES`
- `Card Inserted: YES`
- `Initialized: 1`
- `Init Error: 0x00000000 (none)`
- `Sector 0 read: (OK)`

> **Note:** After each command Jibo reboots back into RCM mode automatically. You will need to re-enter RCM mode (re-short the pin or re-power) before each subsequent command.

### Step 2 — Full eMMC dump

Put Jibo in RCM mode again, then:

```bash
sudo ./shofel2_t124 EMMC_READ 0 1D70000 ~/jibo_emmc.img
```

| Parameter | Value | Notes |
|-----------|-------|-------|
| Start sector | `0` | Beginning of user area |
| Sector count | `0x1D70000` | 30,932,992 sectors (~14.75 GiB, covers full GPT including skills partition) |
| Output | `~/jibo_emmc.img` | Change path as needed |

Progress is printed every ~4 MB. A full dump takes approximately **1–2 hours**.

### Step 3 — Inspect the image

```bash
# Check the image looks sane
xxd ~/jibo_emmc.img | head -4

# List partitions
fdisk -l ~/jibo_emmc.img

# Mount a partition (example: partition 1 — get offset from fdisk output)
sudo mount -o loop,offset=$((sector * 512)) ~/jibo_emmc.img /mnt
```

---

## Writing an Image Back to Jibo's eMMC

> **Warning:** Writing to the eMMC will overwrite existing data. Double-check your sector offset and image file before running. There is no undo.

This is useful for restoring a previously dumped image, flashing a modified partition, or recovering a bricked Jibo.

### Step 1 — Verify eMMC initialization

Always run `EMMC_STATUS` first before any write operation:

```bash
sudo ./shofel2_t124 EMMC_STATUS
```

Confirm you see `RESULT: === FULLY INITIALIZED! ===` before proceeding.

### Step 2 — Write the full image back

To restore a complete eMMC dump (e.g. `jibo_emmc.img`) starting at sector 0:

> **Note:** Re-enter RCM mode before running this command.

```bash
sudo ./shofel2_t124 EMMC_WRITE 0 ~/jibo_emmc.img
```

The tool reads the file size automatically and calculates the sector count. Progress is printed every ~4 MB. Writing a full 14.79 GiB image takes approximately **1–2 hours**.

### Step 3 — Write a single partition

If you only want to restore or flash a specific partition rather than the full eMMC, first identify the partition's start sector from your image:

```bash
fdisk -l ~/jibo_emmc.img
```

Example output:
```
Device             Start      End  Sectors  Size Type
jibo_emmc.img1      2048   526335   524288  256M Linux filesystem
jibo_emmc.img2    526336  1574911  1048576  512M Linux filesystem
```

Extract the partition to a file:

```bash
# Extract partition 1 (start=2048, size=524288 sectors)
dd if=~/jibo_emmc.img of=~/part1.img bs=512 skip=2048 count=524288
```

Put Jibo in RCM mode, then write only that partition starting at its original sector:

```bash
sudo ./shofel2_t124 EMMC_WRITE 800 ~/part1.img
```

> Sector offsets are in hex. `2048` decimal = `0x800` hex.

### Notes

- The `EMMC_WRITE` command streams the file in 16 KB chunks. If the write is interrupted, re-enter RCM mode and re-run from the beginning — partial writes may leave the eMMC in an inconsistent state.
- The file size must be a multiple of 512 bytes. If it is not, the tool will truncate to the nearest sector boundary with a warning.
- After writing, Jibo reboots into RCM mode. Remove the RCM short and power-cycle to boot normally.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Couldn't open the usb` | Jibo not in RCM mode, or USB permissions not set. Try `sudo`. Check `lsusb`. |
| `Hacky Get Status finished correctly... Not cool` | Stack was not smashed. Use Ubuntu 20.04+, enable USB 3.0 (xHCI) in VM settings. |
| `EMMC_STATUS` shows `Init Error: 0xE000000x` | eMMC command failed. See `CMD error details` section of output. Re-enter RCM and retry. |
| `Int Clock Stable: NO` | IROM `device_init_generic` failed. Check `IRAM[0x400022FC]` in output — should be an IROM pointer (`0x100000–0x110000`). |
| USB receive timeout during `EMMC_READ` | Transient USB error. Re-enter RCM and restart the dump from the failed sector. |
| Build error: `arm-none-eabi-gcc: not found` | Run `sudo apt install gcc-arm-none-eabi` |

---

## How It Works

### The Exploit

The Tegra T124 bootrom's USB `GET_STATUS` control transfer copies data onto the stack without bounds checking. By crafting an oversized RCM payload, the memcpy overwrites the return address on the ARM7TDMI boot CPU's stack, redirecting execution into our payload in IRAM.

### eMMC Initialization — Key Discovery

The critical step that makes eMMC work from a payload is calling the IROM's own `device_init_generic` function **before** touching the SDHCI controller:

```c
// Thumb call to IROM function at 0x101EA8
((int(*)(int,int))0x101EA9)(0, 3);  // device=0 (SDMMC4), voltage=3 (3.3V mode)
```

This function reads a table pointer from IRAM at `0x400022FC` (which survives the exploit) and uses it to configure pinmux, pad drive strength, and voltage from the IROM's internal data tables. Without this call, `INT_CLK_STABLE` never sets regardless of clock source or divider.

Full init sequence: Release PMC DPD → call IROM → CAR reset cycle → pad autocalibration → clock stable poll → CMD0 → CMD1 (poll OCR) → CMD2 → CMD3 → CMD7 → CMD16 → increase clock to 12 MHz → CMD6 (4-bit bus) → update HOST_CONTROL.

### eMMC Transfer Speed

The payload switches to high-speed mode after the identification sequence completes:

- **Clock:** identification runs at 375 KHz (SDCLKFS=0x20); after CMD16 the divider is changed to SDCLKFS=0x01 → 12 MHz (within the 26 MHz default-speed limit, no timing mode switch needed).
- **Bus width:** CMD6 SWITCH sets EXT_CSD[183]=1 (4-bit), and HOST_CONTROL is updated to match.
- **Multi-block transfers:** reads use CMD18 (READ_MULTIPLE_BLOCK) and writes use CMD25 (WRITE_MULTIPLE_BLOCK) with SDHCI auto-CMD12, eliminating per-sector command overhead.
- **Chunk size:** 32 sectors (16 KB) per USB bulk transfer.

Combined these give roughly a 64× speedup over identification speed.

---

## IROM Function Table

| Function | Address | Description |
|----------|---------|-------------|
| `ep1_in_write_imm` | `0x001065C0` | Write to USB EP1_IN |
| `ep1_out_read_imm` | `0x00106612` | Read from USB EP1_OUT |
| `do_bct_boot` | `0x00100624` | Boot BCT without locks |
| `device_init_generic` | `0x00101EA8` | Pad/pinmux init from IROM tables |

---

## Notes on Development Environment

- **Ubuntu 20.04 LTS or newer required** — USB stack behavior in 18.04 prevents reliable stack smashing.
- **VM users:** Enable USB 3.0 xHCI in your VM settings. EHCI (USB 2.0) is unreliable.
- `gcc-arm-none-eabi` from apt works fine — no need to build Crosstool-ng.
- The tool polls for the device every 200ms; plug in USB before or after running the command.

---

## Interesting Bootrom Facts

- RCM payload loads to IRAM at `0x4000E000`.
- RCM cmd header is `0x284` bytes; bulk transfers must be multiples of `0x1000`.
- RCM cmd length minus `0x284` must be a multiple of `0x10` (length must end in `4`).
- Min RCM cmd length: `0x1004` bytes. Max usable payload size: `0x31000` bytes (USB buffer 2 limit).
- The poisoned `GET_STATUS` copies `0x30C` bytes of stack before the payload landing.
- USB buffers are at `0x40004000` and `0x40008000`; they alternate on each transaction.
- memcpy return address location: `0x4000DCD8`.
- Stack smash position in RCM cmd: `0x5C4C`.
- RCM runs on an ARM7TDMI core (not the main Cortex-A15 cluster).
- JTAG is disabled by the bootrom during execution (can be re-enabled via payload).

| Function | IROM Address | Description |
|----------|-------------|-------------|
| `ep1_in_write_imm(void *buf, u32 size, u32 *num_xfer)` | `0x001065C0` | Writes EP1_IN |
| `ep1_out_read_imm(void *buf, u32 size, u32 *num_xfer)` | `0x00106612` | Reads EP1_OUT |
| `do_bct_boot()` | `0x00100624` | Boots BCT without applying locks |
| `device_init_generic(int device, int voltage)` | `0x00101EA8` | Pad/pinmux/drive init from IROM tables |
