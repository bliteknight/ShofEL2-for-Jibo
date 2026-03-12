# Jibo eMMC Dump — Research Notes

## Hardware

| Component | Details |
|-----------|---------|
| Device | Jibo social robot |
| SoC | NVIDIA Tegra T124 (K1), ARMv7 quad-core + ARM7TDMI boot CPU |
| Module | Toradex Apalis TK1 |
| eMMC | Hynix HAG4a2, 14.79 GiB (HS200 capable) |
| eMMC controller | SDMMC4 (sdhci-tegra.3) |
| USB VID:PID (RCM) | 0x0955:0x7740 |

---

## Exploit

**ShofEL2 / Fusée Gelée** — Tegra T124 bootrom USB exploit via RCM mode.

- The bootrom's `GET_STATUS` USB control transfer is abused to overflow a stack buffer, redirecting execution to a payload loaded into IRAM.
- Payload is loaded as an RCM message via EP1-OUT bulk transfers.
- Execution lands in IRAM at a known address; the payload runs on the ARM7TDMI boot CPU.

**Repo:** `ShofEL2-for-T124-Jibo` (fork of ShofEL2, `t124` branch)

---

## Key Register Map

### SDMMC4 (eMMC controller)
| Symbol | Address | Notes |
|--------|---------|-------|
| `SDMMC4_BASE` | `0x700B0600` | SDHCI base |
| `SDMMC_VENDOR_CLK_CTRL` | `0x700B0700` (offset `0x100`) | Hardware default: `0x0005d00d` — do not destroy |
| `SDMMC_VENDOR_MISC_CTRL` | `0x700B0720` (offset `0x120`) | |
| `SDMMC_SDMEMCOMP_PADCTRL` | offset `0x1E0` | bit31 = E_INPUT force |
| `SDMMC_AUTO_CAL_CONFIG` | offset `0x1E4` | bit31=START, bit29=ENABLE |
| `SDMMC_AUTO_CAL_STATUS` | offset `0x1EC` | bit31=ACTIVE (poll until clear) |

### CAR (Clock and Reset Controller)
| Symbol | Address/Offset |
|--------|---------------|
| `CAR_BASE` | `0x60006000` |
| `CLK_OUT_ENB_L` | `+0x10` |
| `RST_DEVICES_L` | `+0x04` |
| `CLK_SOURCE_SDMMC4` | `+0x164` |
| `CAR_CLK_ENB_L_SET` | `+0x320` |
| `CAR_RST_DEV_L_CLR` | `+0x304` |
| `CAR_SDMMC4_BIT` | `(1 << 15)` |

### PMC
| Symbol | Address/Offset | Notes |
|--------|---------------|-------|
| `PMC_BASE` | `0x7000E400` | |
| `PMC_IO_DPD_REQ` | `+0x1B8` | Write `0x7FFFFFFF` to release DPD |
| `PMC_IO_DPD2_REQ` | `+0x1C0` | Write `0x7FFFFFFF` to release DPD2 |
| `PMC+0xE8` | `+0xE8` | Clear bit1 before `device_init_generic` |

### Timer
| Symbol | Address | Notes |
|--------|---------|-------|
| `TIMERUS_CNTR_1US` | `0x60005010` | 1 MHz free-running counter, used for timeouts |

---

## IROM — Critical Discovery

### `device_init_generic`

**Address:** `0x101EA8` (ARM) / `0x101EA9` (Thumb call)

**Signature:** `int device_init_generic(int device_index, int voltage_mode)`

**Called as:** `device_init_generic(0, 3)` — device 0 = SDMMC4, voltage 3 = 3.3V mode

**What it does:**
- Configures pinmux for SDMMC4 pads
- Sets pad drive strength and voltage from IROM internal data tables
- Reads its table pointer from IRAM at `0x400022FC`
- This pointer survives the RCM exploit and points back into the IROM (`0x100000–0x110000`)

**This was the root cause of all prior failures.** Without this call, `INT_CLK_STABLE` never sets regardless of clock source, divider, or pad calibration settings.

### IRAM Table Pointer
```
IRAM[0x400022FC] = 0x0010be3c   ← IROM ptr (survives exploit)
```

---

## eMMC Initialization Sequence (Working)

```
1. Release DPD:
   write32(PMC + 0x1B8, 0x7FFFFFFF)   // IO_DPD_REQ
   write32(PMC + 0x1C0, 0x7FFFFFFF)   // IO_DPD2_REQ

2. Clear PMC+0xE8 bit1

3. Call IROM:
   ((int(*)(int,int))0x101EA9)(0, 3)  // Thumb call

4. CAR: assert SDMMC4 reset
   or32(CAR + 0x04, BIT15)

5. Set clock source (PLLP, ~24 MHz):
   write32(CAR + 0x164, 0x00000020)   // CLK_SOURCE_SDMMC4

6. Enable SDMMC4 clock:
   or32(CAR + 0x10, BIT15)

7. Deassert SDMMC4 reset:
   and32(CAR + 0x04, ~BIT15)

8. Pad auto-calibration:
   or32(SDMMC4 + 0x1E0, BIT31)        // force E_INPUT
   or32(SDMMC4 + 0x1E4, BIT31|BIT29)  // start + enable
   poll (SDMMC4 + 0x1EC) & BIT31 == 0 // wait complete
   and32(SDMMC4 + 0x1E0, ~BIT31)      // release E_INPUT

9. Clock Control — enable internal clock, divider=0x20:
   write32(SDMMC4 + 0x2C, 0x00002001)
   poll (SDMMC4 + 0x2C) & 0x0002 != 0 // INT_CLK_STABLE (100ms timeout)

10. Power on (1.8V, bus width 4):
    write32(SDMMC4 + 0x28, 0x00000D00)

11. Set timeout, enable interrupts:
    write8(SDMMC4 + 0x2E, 0x0E)
    write32(SDMMC4 + 0x34, 0x007F00FB)

12. Enable SD clock:
    or32(SDMMC4 + 0x2C, 0x0004)

13. MMC command sequence:
    CMD0  (GO_IDLE)
    CMD1  (SEND_OP_COND, arg=0x40FF8080) — poll until OCR bit31 set (up to ~24s)
    CMD2  (ALL_SEND_CID)
    CMD3  (SET_RELATIVE_ADDR, RCA=0x0001)
    CMD7  (SELECT_CARD, RCA=0x0001)
    CMD16 (SET_BLOCKLEN, 512)
```

---

## MMC Command Register Values

| Command | Value | Description |
|---------|-------|-------------|
| CMD0 | `0x0000` | GO_IDLE_STATE — no response |
| CMD1 | `0x0102` | SEND_OP_COND — R3, no CRC/index check |
| CMD2 | `0x0209` | ALL_SEND_CID — R2 (136-bit), CRC check |
| CMD3 | `0x031A` | SET_RELATIVE_ADDR — R1, CRC+index |
| CMD7 | `0x071B` | SELECT_CARD — R1b, CRC+index |
| CMD16 | `0x101A` | SET_BLOCKLEN — R1, CRC+index |
| CMD17 | `0x113A` | READ_SINGLE_BLOCK — R1, data, CRC+index |
| CMD24 | `0x183A` | WRITE_BLOCK — R1, data, CRC+index |

---

## Things That Did NOT Work (and Why)

| Attempt | Why it failed |
|---------|--------------|
| CLKM (12 MHz) as clock source | INT_CLK_STABLE never set — pads not initialized |
| PLLP bypass mode | Same — root cause was missing pad/pinmux init, not clock source |
| PLLP normal (408 MHz / divider) | PLLP LOCK bit (bit27) never set in early RCM attempts; also missing pad init |
| Manual pinmux writes | Wrong registers, incomplete — IROM tables are device-specific |
| VENDOR_MISC_CNTRL2 RST_N write | Triggers SDHCI boot-sequencer, leaves CMD_INHIBIT=1 |
| Writing 0x808 to VENDOR_CLK_CTRL | Destroys hardware default `0x0005d00d`, breaks clock path |
| AUTO_CAL_STATUS at offset `0x1E8` | Wrong — correct offset is `0x1EC` |
| PMC_PWR_DET_VAL bit12=1 | Forces 3.3V on 1.8V eMMC pads (Apalis TK1 uses 1.8V) |

---

## Confirmed Hardware State (from UART log / register dumps)

| Parameter | Value |
|-----------|-------|
| OSC frequency | 12 MHz (`OSC_CTRL = 0x800003f1`) |
| PLLP (in RCM) | Locked at ~408 MHz after `device_init_generic` |
| SDMMC4 CAR bit | bit15 of CLK_OUT_ENB_L / RST_DEVICES_L |
| eMMC voltage | 1.8V (Apalis TK1 board design) |
| CAPABILITIES base_clk | 208 MHz |

---

## Successful EMMC_STATUS Output (verification run)

```
Magic:           0xcafe0000 (OK)
Init Error:      0x00000000 (none)
Initialized:     1
Card Inserted:   YES
Int Clock Stable: YES
SD Clock Enabled: YES
IROM return:     0x00000000 (OK!)
CMD1 retries:    29
RESULT:          === FULLY INITIALIZED! ===
Sector 0 read:   OK
```

---

## eMMC Dump Command

```bash
# Full eMMC dump (14.79 GiB, ~30.6M sectors)
sudo ./shofel2_t124 EMMC_READ 0 1D94400 /home/bliteknight/Desktop/Jibo/emmc_mbr.img
```

| Parameter | Value |
|-----------|-------|
| Start sector | `0x0` |
| Sector count | `0x1D94400` (30,998,528 sectors) |
| Total size | ~14.79 GiB |
| Output file | `/home/bliteknight/Desktop/Jibo/emmc_mbr.img` |

---

## Files

| File | Purpose |
|------|---------|
| `payloads/emmc_server.c` | ARM payload — eMMC init + USB server loop (READ/WRITE/STATUS/EXIT) |
| `payloads/emmc_dump.c` | Old one-shot dump payload (deprecated, superseded by emmc_server) |
| `include/emmc.h` | SDHCI / SDMMC4 / CAR register definitions |
| `include/emmc_server.h` | USB command protocol definitions |
| `exploit/shofel2_t124.c` | x86 host tool — EMMC_STATUS, EMMC_READ, EMMC_WRITE commands |
| `Makefile` | Build system — produces `emmc_server.bin` |

---

## Reference

Original working implementation by **devsparx**:
`https://github.com/devsparx/ShofEL2-for-T124/commit/66d573fd2b28e72c56da20c9aae3d3c7be571fd1`
