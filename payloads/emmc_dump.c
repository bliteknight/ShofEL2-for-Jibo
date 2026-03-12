#include "types.h"
#include "t124.h"
#include "mem_dumper_usb_server.h"

typedef void (*ep1_x_imm_t)(void *buffer, u32 size, u32 *num_xfer);
static inline u32 mmio_r32(u32 addr) { return *(volatile u32 *)addr; }
static inline void mmio_w32(u32 addr, u32 val) { *(volatile u32 *)addr = val; }
static inline void mmio_w16(u32 addr, u16 val) { *(volatile u16 *)addr = val; }
static inline u16  mmio_r16(u32 addr)          { return *(volatile u16*)addr; }
static inline void mmio_w8(u32 addr, u8 val)   { *(volatile u8 *)addr = val; }
static inline u8   mmio_r8(u32 addr)           { return *(volatile u8 *)addr; }

#define CAR_BASE            0x60006000
#define CLK_OUT_ENB_L       0x010
#define RST_DEVICES_L       0x004
#define CLK_SOURCE_SDMMC4   0x164
#define SDMMC4_BIT          (1 << 15)
#define SDMMC4_BASE         0x700b0600

/* PMC_BASE already defined in t124.h */
#define PMC_NO_IOPOWER      0x044
#define PMC_PWR_DET_VAL     0x048
#define PMC_IO_DPD2_STATUS  0x1BC
#define SDMMC4_PAD_BIT      (1 << 12)

#define PMUX_SDMMC4_CLK     0x70003358
#define PMUX_SDMMC4_RST_N   0x70003354
#define PMUX_SDMMC4_CMD     0x7000325c
#define PMUX_SDMMC4_DAT0    0x70003260
#define PMUX_SDMMC4_DAT1    0x70003264
#define PMUX_SDMMC4_DAT2    0x70003268
#define PMUX_SDMMC4_DAT3    0x7000326c
#define PMUX_SDMMC4_DAT4    0x70003270
#define PMUX_SDMMC4_DAT5    0x70003274
#define PMUX_SDMMC4_DAT6    0x70003278
#define PMUX_SDMMC4_DAT7    0x7000327c
#define PMUX_CLK_VAL        0x21  /* fn1=SDMMC4, input-en, no pull */
#define PMUX_DAT_VAL        0x29  /* fn1=SDMMC4, input-en, pull-up */

#define SDHCI_BLK_SIZE      0x04
#define SDHCI_BLK_COUNT     0x06
#define SDHCI_ARG           0x08
#define SDHCI_XFER_MODE     0x0C
#define SDHCI_CMD           0x0E
#define SDHCI_RSP0          0x10
#define SDHCI_BUF_DATA      0x20
#define SDHCI_PRESENT       0x24
#define SDHCI_HOSTCTL       0x28
#define SDHCI_PWRCON        0x29
#define SDHCI_CLK_CTRL      0x2C
#define SDHCI_TIMEOUT_CTRL  0x2E
#define SDHCI_INT_STATUS    0x30
#define SDHCI_INT_ENABLE    0x34
#define SDHCI_SIG_ENABLE    0x38
#define VENDOR_CLOCK_CNTRL  0x100
#define VENDOR_MISC_CNTRL2  0x124
#define SDHCI_SW_RESET      0x2F
#define SW_RESET_CMD        (1u<<1)

#define PWRCON_1V8          0x0B  /* bits[3:1]=101=1.8V, bit0=power on — Apalis TK1 VDDIO=1.8V */
#define HOSTCTL_CD_FORCE    0xC0
#define CARD_RST_N_BIT      (1<<8)
#define CLK_SRC_CLKM        0xC0000000  /* CLKM (crystal osc) — always running */
#define CLK_SRC_PLLP        0x00000000  /* PLLP_OUT0 + div=0 → in bypass: 12MHz direct */
#define PLLP_BASE_OFF       0x0A8       /* CAR offset for PLLP control/status */
#define PLLP_MISC_OFF       0x0AC       /* CAR PLLP_MISC: bit18=LOCK_ENABLE */
#define PLLP_LOCK_BIT       (1u<<27)    /* PLLP_BASE bit27 = PLL locked */
#define PLLP_ENABLE_BIT     (1u<<30)    /* PLLP_BASE bit30 = PLL enable */
/* 12MHz CLKM → 408MHz PLLP: DIVM=4, DIVN=136 (CF=3MHz, VCO=408MHz).
 * DIVM field is 3 bits wide (confirmed: writing 12 reads back 4 = 12&7).
 * DIVM=4 fits in 3 bits as-is. CPCON=8 per table for CF=2-3MHz. */
#define PLLP_BASE_DIVS      ((136u<<8) | 4u)           /* DIVN=136, DIVM=4 → CF=3MHz, VCO=408MHz */
#define PLLP_BYPASS_BIT     (1u<<28)
#define PLLP_BASE_LOCK_VAL  (PLLP_ENABLE_BIT | PLLP_BASE_DIVS)  /* NO BYPASS: VCO→408MHz */
#define OSC_CTRL_OFF        0x050   /* CAR OSC_CTRL: bits[31:28]=freq (5=38.4MHz) */
#define VENDOR_CLK_INIT     0x0005d80d  /* HW default 0x0005d00d + PADPIPE_CLKEN_OVERRIDE(bit11) */
#define CLK_INT_EN          (1<<0)
#define CLK_SD_EN           (1<<2)
/* CLKM (12MHz crystal) as source: always-on, guaranteed stable.
 * SDCLKFS=0x10: ÷32 → 12MHz/32 = 375kHz (valid power-of-2 SDHCI divider) */
#define CLK_DIV_SLOW        (0x10u << 8)
#define CLK_INIT_VAL        (CLK_DIV_SLOW | CLK_INT_EN | CLK_SD_EN)
#define CMD_INHIBIT         (1<<0)
#define INT_CMD_DONE        (1<<0)
#define INT_ERROR           (1<<15)
#define EMMC_RCA            1
#define BLOCK_BUF_ADDR      0x4000F000

/* Tegra SDHCI pad autocalibration — must run before enabling INT_CLK_EN.
 * tegra_sdhci_pad_autocalib() in Linux sets START+ENABLE and polls until done. */
#define SDMMC_AUTO_CAL_CONFIG  0x1E4   /* bit31=START, bit29=ENABLE, bits[14:8]=PD_OFFSET, bits[6:0]=PU_OFFSET */
#define SDMMC_AUTO_CAL_STATUS  0x1E8   /* bit31=AUTO_CAL_ACTIVE (poll until 0) */
#define AUTO_CAL_START         (1u<<31)
#define AUTO_CAL_ENABLE        (1u<<29)
#define AUTO_CAL_ACTIVE        (1u<<31)

static ep1_x_imm_t g_fn;
static u32 g_nxfr;
static u32 g_last_err_st;
static u16 g_clk_ctrl_saved;
static u32 g_vendor_clk_saved;
static u32 g_pllp_before;
static u32 g_pllp_after;
static u32 g_vendor_clk_before;
static void delay(u32 n) { volatile u32 i; for(i=0;i<n;i++); }
static void D(u32 t, u32 v) { u32 r[2]; r[0]=t; r[1]=v; g_fn(r,8,&g_nxfr); }

static void reset_cmd_line(void) {
    u32 t = 100000;
    mmio_w8(SDMMC4_BASE + SDHCI_SW_RESET, SW_RESET_CMD);
    while (t-- && (mmio_r8(SDMMC4_BASE + SDHCI_SW_RESET) & SW_RESET_CMD))
        delay(5);
}

static int wait_cmd(u32 timeout) {
    u32 st;
    while (timeout--) {
        st = mmio_r32(SDMMC4_BASE + SDHCI_INT_STATUS);
        if (st & INT_ERROR) {
            g_last_err_st = st;
            mmio_w32(SDMMC4_BASE + SDHCI_INT_STATUS, 0xFFFFFFFF);
            return -(int)(st >> 16);
        }
        if (st & INT_CMD_DONE) {
            mmio_w32(SDMMC4_BASE + SDHCI_INT_STATUS, INT_CMD_DONE);
            return 0;
        }
        delay(10);
    }
    return -2;
}

static int send_cmd(u32 arg, u16 xfer, u16 cmd) {
    u32 t = 500000;
    u32 inhibit = (cmd & (1<<5)) ? (CMD_INHIBIT|(1<<1)) : CMD_INHIBIT;
    while (t--) {
        if (!(mmio_r32(SDMMC4_BASE + SDHCI_PRESENT) & inhibit)) break;
        delay(5);
    }
    if (!t) return -3;
    mmio_w32(SDMMC4_BASE + SDHCI_INT_STATUS, 0xFFFFFFFF);
    mmio_w32(SDMMC4_BASE + SDHCI_ARG, arg);
    mmio_w16(SDMMC4_BASE + SDHCI_XFER_MODE, xfer);
    mmio_w16(SDMMC4_BASE + SDHCI_CMD, cmd);
    if ((cmd & 0x3) == 0) { delay(200000); return 0; }
    return wait_cmd(500000);
}

static int read_block(u32 lba) {
    int r; u32 i;
    u32 *buf = (u32 *)BLOCK_BUF_ADDR;
    mmio_w16(SDMMC4_BASE + SDHCI_BLK_SIZE,  512);
    mmio_w16(SDMMC4_BASE + SDHCI_BLK_COUNT, 1);
    r = send_cmd(lba, (1<<4)|(1<<1), (17<<8)|2|(1<<3)|(1<<4)|(1<<5));
    if (r < 0) return r;
    for (i = 0; i < 1000000; i++) {
        if (mmio_r32(SDMMC4_BASE + SDHCI_INT_STATUS) & (1<<5)) break;
        delay(5);
    }
    if (i == 1000000) return -4;
    mmio_w32(SDMMC4_BASE + SDHCI_INT_STATUS, (1<<5));
    for (i = 0; i < 128; i++)
        buf[i] = mmio_r32(SDMMC4_BASE + SDHCI_BUF_DATA);
    for (i = 0; i < 200000; i++) {
        if (mmio_r32(SDMMC4_BASE + SDHCI_INT_STATUS) & (1<<1)) break;
        delay(5);
    }
    mmio_w32(SDMMC4_BASE + SDHCI_INT_STATUS, 0xFFFFFFFF);
    return 0;
}

__attribute__((section(".init")))
void entry() {
    u32 enb, rst, st, lba, count;
    int ret, i;
    u32 status[2];
    struct mem_dumper_args_s args;
    ep1_x_imm_t ep1_out = (ep1_x_imm_t)(BOOTROM_EP1_OUT_READ_IMM | 1);
    ep1_x_imm_t ep1_in  = (ep1_x_imm_t)(BOOTROM_EP1_IN_WRITE_IMM | 1);

    /* Pinmux */
    mmio_w32(PMUX_SDMMC4_CLK,  PMUX_CLK_VAL);
    mmio_w32(PMUX_SDMMC4_RST_N, PMUX_DAT_VAL);
    mmio_w32(PMUX_SDMMC4_CMD,  PMUX_DAT_VAL);
    mmio_w32(PMUX_SDMMC4_DAT0, PMUX_DAT_VAL);
    mmio_w32(PMUX_SDMMC4_DAT1, PMUX_DAT_VAL);
    mmio_w32(PMUX_SDMMC4_DAT2, PMUX_DAT_VAL);
    mmio_w32(PMUX_SDMMC4_DAT3, PMUX_DAT_VAL);
    mmio_w32(PMUX_SDMMC4_DAT4, PMUX_DAT_VAL);
    mmio_w32(PMUX_SDMMC4_DAT5, PMUX_DAT_VAL);
    mmio_w32(PMUX_SDMMC4_DAT6, PMUX_DAT_VAL);
    mmio_w32(PMUX_SDMMC4_DAT7, PMUX_DAT_VAL);
    delay(10000);

    /* PMC: enable SDMMC4 pad power; set 1.8V signaling (bit12=0 in PWR_DET_VAL).
     * PMC_PWR_DET_VAL bit12=1 means 3.3V pads — wrong for Apalis TK1 1.8V eMMC.
     * Mismatch may prevent INT_CLK_STABLE from setting. */
    mmio_w32(PMC_BASE + PMC_NO_IOPOWER,
             mmio_r32(PMC_BASE + PMC_NO_IOPOWER) & ~SDMMC4_PAD_BIT);
    mmio_w32(PMC_BASE + PMC_PWR_DET_VAL,
             mmio_r32(PMC_BASE + PMC_PWR_DET_VAL) & ~SDMMC4_PAD_BIT); /* bit12=0: 1.8V */
    delay(50000);

    /* PLLP: Tegra SDHCI INT_CLK_STABLE may require PLLP as internal reference.
     * Two-step: write dividers first (ENABLE=0), then enable. DIVM is 3-bit field. */
    g_pllp_before = mmio_r32(CAR_BASE + PLLP_BASE_OFF);   /* save reset state */
    mmio_w32(CAR_BASE + PLLP_MISC_OFF, (1u<<18)|(8u<<8));  /* LOCK_ENABLE + CPCON=8 (req for 12MHz) */
    mmio_w32(CAR_BASE + PLLP_BASE_OFF, PLLP_BASE_DIVS);   /* write dividers, ENABLE=0 */
    delay(10000);
    mmio_w32(CAR_BASE + PLLP_BASE_OFF, PLLP_BASE_LOCK_VAL); /* now set ENABLE=1 */
    { u32 wi; for(wi=0; wi<2000000; wi++) { if(mmio_r32(CAR_BASE + PLLP_BASE_OFF) & PLLP_LOCK_BIT) break; delay(10); } }
    g_pllp_after = mmio_r32(CAR_BASE + PLLP_BASE_OFF);    /* bit27=LOCK, bit30=ENABLE */
    delay(2000000); /* extra wait: PLLP VCO needs time to stabilize (~100us typical) */

    /* CAR: PLLP_OUT0 ÷4 = 102MHz to SDHCI (T124 SDHCI needs ~100MHz+ for INT_CLK_STABLE) */
    mmio_w32(CAR_BASE + CLK_SOURCE_SDMMC4, CLK_SRC_PLLP_D4);
    delay(10000);
    enb = mmio_r32(CAR_BASE + CLK_OUT_ENB_L);
    mmio_w32(CAR_BASE + CLK_OUT_ENB_L, enb | SDMMC4_BIT);
    delay(10000);
    rst = mmio_r32(CAR_BASE + RST_DEVICES_L);
    mmio_w32(CAR_BASE + RST_DEVICES_L, rst | SDMMC4_BIT);   /* assert reset */
    delay(10000);
    mmio_w32(CAR_BASE + RST_DEVICES_L, rst & ~SDMMC4_BIT);  /* deassert reset */
    delay(50000);

    /* SW_RESET_ALL: confirmed completing (SW_RESET reads 0x00 after) */
    mmio_w8(SDMMC4_BASE + SDHCI_SW_RESET, 0x01);
    { u32 wi; for(wi=0; wi<500000; wi++) { if(!(mmio_r8(SDMMC4_BASE + SDHCI_SW_RESET) & 0x01)) break; delay(5); } }
    delay(10000);

    /* Vendor clock: read default first (was 0x0005d00d — had critical bits we were clearing) */
    g_vendor_clk_before = mmio_r32(SDMMC4_BASE + VENDOR_CLOCK_CNTRL);
    mmio_w32(SDMMC4_BASE + VENDOR_CLOCK_CNTRL, VENDOR_CLK_INIT);
    delay(10000);
    g_vendor_clk_saved = mmio_r32(SDMMC4_BASE + VENDOR_CLOCK_CNTRL); /* verify write took */

    /* 1.8V power + force card-detected */
    mmio_w8(SDMMC4_BASE + SDHCI_PWRCON,  PWRCON_1V8);
    mmio_w8(SDMMC4_BASE + SDHCI_HOSTCTL, HOSTCTL_CD_FORCE);
    delay(10000);

    /* Pad autocalibration: Linux tegra_sdhci_pad_autocalib() does this before sdhci_set_clock().
     * Required for INT_CLK_STABLE to set on T124. Write START|ENABLE, poll until not active. */
    { u32 acal = mmio_r32(SDMMC4_BASE + SDMMC_AUTO_CAL_CONFIG);
      mmio_w32(SDMMC4_BASE + SDMMC_AUTO_CAL_CONFIG, acal | AUTO_CAL_START | AUTO_CAL_ENABLE);
      delay(10000);
      { u32 wi; for(wi=0; wi<200000; wi++) { if(!(mmio_r32(SDMMC4_BASE + SDMMC_AUTO_CAL_STATUS) & AUTO_CAL_ACTIVE)) break; delay(5); } }
    }
    delay(10000);

    /* Clock + timeout: SDHCI spec requires INT_CLK_STABLE before enabling SD_CLK */
    mmio_w16(SDMMC4_BASE + SDHCI_CLK_CTRL, CLK_DIV_SLOW | CLK_INT_EN);
    { u32 wi; for(wi=0; wi<200000; wi++) { if(mmio_r16(SDMMC4_BASE + SDHCI_CLK_CTRL) & (1<<1)) break; delay(5); } }
    g_clk_ctrl_saved = mmio_r16(SDMMC4_BASE + SDHCI_CLK_CTRL); /* save: bit1=STABLE? */
    mmio_w16(SDMMC4_BASE + SDHCI_CLK_CTRL, CLK_DIV_SLOW | CLK_INT_EN | CLK_SD_EN);
    delay(100000);
    /* read CLK_CTRL after SD_CLK_EN: STABLE might set only after both INT+SD enabled */
    g_clk_ctrl_saved = mmio_r16(SDMMC4_BASE + SDHCI_CLK_CTRL);
    mmio_w8(SDMMC4_BASE + SDHCI_TIMEOUT_CTRL, 0x0E); /* max timeout */
    delay(200000);

    /* Deassert eMMC RST_N: required for eMMC to exit hardware reset.
     * This triggers the SDHCI boot-sequencer (CMD_INHIBIT=1).
     * If clock is running the sequencer completes; probe CMD_INHIBIT over time. */
    mmio_w32(SDMMC4_BASE + VENDOR_MISC_CNTRL2, CARD_RST_N_BIT); /* bit8=1: RST_N high */
    delay(1000000);
    { u32 wi; for(wi=0; wi<5000000; wi++) { if(!(mmio_r32(SDMMC4_BASE + SDHCI_PRESENT) & CMD_INHIBIT)) break; delay(5); } }

    mmio_w32(SDMMC4_BASE + SDHCI_INT_STATUS, 0xFFFFFFFF);
    mmio_w32(SDMMC4_BASE + SDHCI_INT_ENABLE, 0x00ff00ff);
    mmio_w32(SDMMC4_BASE + SDHCI_SIG_ENABLE, 0x00000000);

    ep1_out(&args, sizeof(args), &g_nxfr);
    g_fn = ep1_in;

    /* Report pad voltage state */
    D(0x01, mmio_r32(PMC_BASE + PMC_PWR_DET_VAL));   /* bit12=1:3.3V, 0:1.8V */
    D(0x02, mmio_r32(PMC_BASE + PMC_IO_DPD2_STATUS)); /* bit12=1:powered down */
    D(0x03, mmio_r32(PMC_BASE + PMC_NO_IOPOWER));     /* bit12=1:no power */
    D(0x04, mmio_r8(SDMMC4_BASE + SDHCI_PWRCON));    /* want 0x0B (1.8V) */
    D(0x05, mmio_r32(SDMMC4_BASE + SDHCI_PRESENT));
    D(0x06, g_clk_ctrl_saved);                        /* CLK_CTRL after STABLE poll (bit1=STABLE?) */
    D(0x07, mmio_r32(CAR_BASE + CLK_OUT_ENB_L));     /* bit15 must be set for SDMMC4 */
    D(0x08, mmio_r32(CAR_BASE + CLK_SOURCE_SDMMC4)); /* want 0xC0000000 (CLKM) */
    D(0x09, mmio_r32(CAR_BASE + RST_DEVICES_L));     /* bit15 must be 0 (SDMMC4 not in reset) */
    D(0x0A, mmio_r32(CAR_BASE + PLLP_BASE_OFF));     /* bit27=PLLP_LOCK, bit30=PLLP_ENABLE */
    D(0x0D, g_vendor_clk_saved);                     /* VENDOR_CLOCK_CNTRL readback after write */
    D(0x0E, g_pllp_after);                           /* PLLP_BASE after init: bit27=LOCK, bit30=EN */
    D(0x13, g_pllp_before);                          /* PLLP_BASE reset state (before our write) */
    D(0x14, mmio_r32(CAR_BASE + OSC_CTRL_OFF));      /* OSC_CTRL bits[31:28]=freq (8=12MHz) */
    D(0x15, mmio_r32(CAR_BASE + PLLP_MISC_OFF));     /* PLLP_MISC readback: bit18=LOCK_EN, bits[11:8]=CPCON */
    D(0x16, mmio_r32(SDMMC4_BASE + 0x40));          /* SDHCI CAPABILITIES: bits[13:8]=base_clk_MHz, bit26=1.8V */
    D(0x17, g_vendor_clk_before);                    /* VENDOR_CLOCK_CNTRL reset state (before our write) */
    D(0x18, mmio_r8(SDMMC4_BASE + SDHCI_SW_RESET)); /* SW_RESET reg state */
    D(0x19, mmio_r32(PMC_BASE + PMC_PWR_DET_VAL));          /* bit12 should now be 0 (1.8V) */
    D(0x1A, mmio_r32(SDMMC4_BASE + VENDOR_MISC_CNTRL2));   /* bit8=RST_N: want 1 */
    D(0x1B, mmio_r32(SDMMC4_BASE + SDMMC_AUTO_CAL_CONFIG)); /* autocalib config readback */
    D(0x1C, mmio_r32(SDMMC4_BASE + SDMMC_AUTO_CAL_STATUS)); /* bit31=0: cal done */

    /* CMD0 — force eMMC to idle state, no response */
    send_cmd(0, 0, 0x0000);
    delay(2000000);  /* wait for eMMC to process CMD0 and be ready for CMD1 */
    D(0x0B, mmio_r32(SDMMC4_BASE + SDHCI_PRESENT)); /* CMD_INHIBIT should be 0 after CMD0 */
    D(0x0C, mmio_r32(SDMMC4_BASE + VENDOR_MISC_CNTRL2)); /* bit8=CARD_RST_N: want 1 (deasserted) */

    /* CMD1 with arg=0 first (query OCR) — first response may glitch, treat as non-fatal */
    ret = send_cmd(0, 0, (1u<<8)|0x2);
    st  = mmio_r32(SDMMC4_BASE + SDHCI_RSP0);
    D(0x0F, mmio_r32(SDMMC4_BASE + SDHCI_PRESENT));
    D(0x10, (u32)ret);
    D(0x11, st);
    D(0x12, g_last_err_st);   /* raw INT_STATUS at time of error (0 if no error) */
    if (ret != 0) { reset_cmd_line(); }  /* recover CMD line, fall through to poll */

    /* CMD1 poll */
    for (i = 0; i < 200; i++) {
        ret = send_cmd(0x40FF8080, 0, (1u<<8)|0x2);
        st  = mmio_r32(SDMMC4_BASE + SDHCI_RSP0);
        if (i < 3) D(0x20+i, st);
        if (st & (1u<<31)) break;
        if (ret != 0) reset_cmd_line();
        delay(20000);
    }
    D(0x2F, st);
    if (!(st & (1u<<31))) { D(0xFF, 0xDEAD0002); goto reboot; }

    ret = send_cmd(0, 0, (2u<<8)|0x1|(1<<3));
    D(0x30, (u32)ret);
    ret = send_cmd(EMMC_RCA<<16, 0, (3u<<8)|0x2|(1<<3)|(1<<4));
    D(0x31, (u32)ret);
    ret = send_cmd(EMMC_RCA<<16, 0, (7u<<8)|0x3|(1<<3)|(1<<4));
    D(0x32, (u32)ret);
    delay(20000);

    send_cmd(EMMC_RCA<<16, 0, (13u<<8)|0x2|(1<<3)|(1<<4));
    st = mmio_r32(SDMMC4_BASE + SDHCI_RSP0);
    D(0x33, st);
    D(0x34, (st>>9)&0xF);
    if (((st>>9)&0xF) != 4) { D(0xFF, 0xDEAD0003); goto reboot; }

    D(0xAA, 0x600DC0DE);
    lba = args.start; count = args.len;
    while (count--) {
        ret = read_block(lba);
        status[0] = lba; status[1] = (u32)ret;
        ep1_in(status, 8, &g_nxfr);
        if (ret == 0)
            ep1_in((void*)BLOCK_BUF_ADDR, 512, &g_nxfr);
        lba++;
    }

reboot:
    *(volatile u32 *)(PMC_BASE + PMC_SCRATCH0) |= PMC_SCRATCH0_MODE_RCM;
    *(volatile u32 *)(PMC_BASE + PMC_CNTRL)    |= PMC_CNTRL_MAIN_RST;
}
