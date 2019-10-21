/* vax4xx_ve.c: SPX video simulator

   Copyright (c) 2019, Matt Burke

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   ve          SPX colour video
*/

#include "vax_defs.h"
#include "sim_video.h"
#include "vax_lk.h"
#include "vax_vs.h"

#include "vax_ka4xx_spx_bin.h"

#define VE_XSIZE        1280                            /* visible width */
#define VE_YSIZE        1024                            /* visible height */

#define VE_BXSIZE       1280                            /* buffer width */
#define VE_BYSIZE       2048                            /* buffer height */
#define VE_BUFSIZE      0x280000                        /* number of bytes */
#define VE_ORSC         3                               /* screen origin multiplier */

#define PUTL(b,x,v)     b[x+3] = (v >> 24) & 0xFF; \
                        b[x+2] = (v >> 16) & 0xFF; \
                        b[x+1] = (v >> 8) & 0xFF; \
                        b[x] = v & 0xFF
#define PUTW(b,x,v)     b[x+1] = (v >> 8) & 0xFF; \
                        b[x] = v & 0xFF
#define GETL(b,x)       ((b[x+3] << 24) | \
                         (b[x+2] << 16) | \
                         (b[x+1] << 8) | \
                          b[x])
#define GETW(b,x)       ((b[x+1] << 8)|b[x])

#define CURSOR_X_OFFSET 216
#define CURSOR_Y_OFFSET 33

#define CMD_TEST        0x8000
#define CMD_HSHI        0x4000
#define CMD_VBHI        0x2000
#define CMD_LODSA       0x1000
#define CMD_FORG2       0x0800
#define CMD_ENRG2       0x0400
#define CMD_FORG1       0x0200
#define CMD_ENRG1       0x0100
#define CMD_XHWID       0x0080
#define CMD_XHCL1       0x0040
#define CMD_XHCLP       0x0020
#define CMD_XHAIR       0x0010
#define CMD_FOPB        0x0008
#define CMD_ENPB        0x0004
#define CMD_FOPA        0x0002
#define CMD_ENPA        0x0001

#define TBC_CMD_F0EN    0x00000001
#define TBC_CMD_F0DS    0x00000002
#define TBC_CMD_F0OP    0x00000004
#define TBC_CMD_F0IN    0x00000008

#define TBC_CMD_FIFOEN(x)  (TBC_CMD_F0EN << ((x * 5) + ((x == 3) ? 1 : 0)))
#define TBC_CMD_FIFODIS(x) (TBC_CMD_F0DS << ((x * 5) + ((x == 3) ? 1 : 0)))
#define TBC_CMD_FIFOOUT(x) (TBC_CMD_F0OP << ((x * 5) + ((x == 3) ? 1 : 0)))
#define TBC_CMD_FIFOIN(x)  (TBC_CMD_F0IN << ((x * 5) + ((x == 3) ? 1 : 0)))

#define TBC_CMD_F1EN    0x00000020
#define TBC_CMD_F1DS    0x00000040
#define TBC_CMD_F2EN    0x00000400
#define TBC_CMD_F2DS    0x00000800
#define TBC_CMD_F3EN    0x00010000
#define TBC_CMD_F3DS    0x00020000
#define TBC_CMD_F3IN    0x00080000
#define TBC_CMD_STRW    0x40000000
#define TBC_CMD_STRR    0x80000000

#define TBC_CSR_F0DR    0x00100000
#define TBC_CSR_F1DR    0x00200000
#define TBC_CSR_F2DR    0x00400000
#define TBC_CSR_F3DR    0x00800000
#define TBC_CSR_F0EN    0x00010000
#define TBC_CSR_F1EN    0x00020000
#define TBC_CSR_F2EN    0x00040000
#define TBC_CSR_F3EN    0x00080000

#define TBC_CSR_FIFODIR(x) (TBC_CSR_F0DR << x)
#define TBC_CSR_FIFOEN(x)  (TBC_CSR_F0EN << x)
#define TBC_CSR_STRDIR   0x00000400
#define TBC_CSR_STRSTAT  0x00000800

#define INTSTS_F0_GE_THRSH  0x00000100
#define INTSTS_F0_LT_THRSH  0x00000200

#define GET_FIFO(x)     ((x >> 3) - 2)
#define FIFO_LEN        0x4000

extern int32 tmxr_poll;
extern int32 ka_cfgtst;
extern uint32 vc_org;
extern uint32 vc_last_org;

struct fifo_reg_t {
    uint32 buf[FIFO_LEN >> 2];
    uint32 put_ptr;
    uint32 get_ptr;
    uint32 count;
    uint32 threshold;
    uint32 semaphore;
    };

typedef struct fifo_reg_t FIFO_REG;

uint32 bt459_addr = 0;
uint32 bt459_cmap_p = 0;
uint32 bt459_cmap[3];
uint32 cp_fb_format = 0;
uint32 cp_int_status = 0;
uint32 cp_int_mask = 0;
uint32 gf_fb_format = 0;
uint32 spx_xstart = 0;
uint32 spx_ystart = 0;
uint32 spx_xend = 0;
uint32 spx_yend = 0;
uint32 spx_dstpix = 0;
uint32 spx_srcpix = 0;
uint32 spx_fg = 0;
uint32 spx_cmd = 0;
uint32 spx_rmask = 0;
uint32 spx_wmask = 0;
uint32 spx_smask = 0;
uint32 spx_dmask = 0;
uint32 spx_strx = 0;
uint32 spx_stry = 0;
uint32 spx_destloop = 0;
uint32 spx_upc = 0;                                     /* micro pc */
uint32 spx_status = 0;
uint32 tbc_csr = 0;
FIFO_REG tbc_fifo[4];
uint32 tbc_table = 0;
uint32 tbc_timing_setup = 0;
uint32 spx_timing_csr = 0;
uint32 tbc_ltrr = 0;
uint32 tbc_timing = 0;
t_bool ve_input_captured = FALSE;                       /* Mouse and Keyboard input captured in video window */
uint8 *ve_buf = NULL;                                   /* Video memory */
uint32 *ve_lines = NULL;                                /* Video Display Lines */
uint32 ve_palette[256];
t_bool ve_updated[VE_YSIZE];
t_bool ve_active = FALSE;

t_stat ve_svc (UNIT *uptr);
t_stat ve_micro_svc (UNIT *uptr);
t_stat ve_reset (DEVICE *dptr);
t_stat ve_detach (UNIT *dptr);
t_stat ve_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ve_set_capture (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ve_show_capture (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat ve_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *ve_description (DEVICE *dptr);
int32 tbc_rd (int32 rg);
void tbc_wr (int32 rg, int32 val, int32 lnt);
int32 scn_rd (int32 rg);
void scn_wr (int32 rg, int32 val, int32 lnt);


/* VE data structures

   ve_dev      VE device descriptor
   ve_unit     VE unit descriptor
   ve_reg      VE register list
*/

DIB ve_dib = {
    VE_ROM_INDEX, BOOT_CODE_ARRAY, BOOT_CODE_SIZE
    };

UNIT ve_unit[] = {
    { UDATA (&ve_svc, UNIT_IDLE, 0) },
    { UDATA (&ve_micro_svc, UNIT_IDLE+UNIT_DIS, 0) }
    };

REG ve_reg[] = {
    { NULL }
    };

/* Debugging Bitmaps */

#define DBG_REG         0x0100                          /* register activity */
#define DBG_ROP         0x0200                          /* raster operations */

DEBTAB ve_debug[] = {
    {"REG",     DBG_REG,                "Register activity"},
    {"ROP",     DBG_ROP,                "Raster operations"},
    {"VMOUSE",  SIM_VID_DBG_MOUSE,      "Video Mouse"},
    {"VCURSOR", SIM_VID_DBG_CURSOR,     "Video Cursor"},
    {"VKEY",    SIM_VID_DBG_KEY,        "Video Key"},
    {"VVIDEO",  SIM_VID_DBG_VIDEO,      "Video Video"},
    { 0 }
    };

MTAB ve_mod[] = {
    { MTAB_XTD|MTAB_VDV, 1, NULL, "ENABLE",
        &ve_set_enable, NULL, NULL, "Enable VCB01 (QVSS)" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLE",
        &ve_set_enable, NULL, NULL, "Disable VCB01 (QVSS)" },
    { MTAB_XTD|MTAB_VDV, TRUE, NULL, "CAPTURE",
        &ve_set_capture, &ve_show_capture, NULL, "Enable Captured Input Mode" },
    { MTAB_XTD|MTAB_VDV, FALSE, NULL, "NOCAPTURE",
        &ve_set_capture, NULL, NULL, "Disable Captured Input Mode" },
    { MTAB_XTD|MTAB_VDV, TRUE, "OSCURSOR", NULL,
        NULL, &ve_show_capture, NULL, "Display Input Capture mode" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "VIDEO", NULL,
        NULL, &vid_show_video, NULL, "Display the host system video capabilities" },
    { 0 }
    };

DEVICE ve_dev = {
    "VE", ve_unit, ve_reg, ve_mod,
    2, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &ve_reset,
    NULL, NULL, &ve_detach,
    &ve_dib, DEV_DIS | DEV_DEBUG, 0,
    ve_debug, NULL, NULL, &ve_help, NULL, NULL,
    &ve_description
    };

/* VE routines

   ve_rd       I/O page read
   ve_wr       I/O page write
   ve_svc      process event
   ve_reset    process reset
   ve_detach   process detach
*/

int32 ve_rd (int32 pa)
{
int32 rg = ((pa - 0x38000000) >> 2);
uint32 data = 0;

if (pa >= 0x39bc0000) {                                 /* ROMCFG */
        data = 0xC0000202;                              /* from real hardware */
        sim_debug (DBG_REG, &ve_dev, "rom_cfg %X rd %X at %08X\n", pa, data, fault_PC);
        return data;
        }
if (pa >= 0x39b20000) {
    rg = rg & 0xFFF;
    if (pa >= 0x39b23000) {
        sim_debug (DBG_REG, &ve_dev, "direct_fifo 3 %X rd at %08X\n", pa, fault_PC);
        return 0;
        }
    else if (pa >= 0x39b22000) {
        sim_debug (DBG_REG, &ve_dev, "direct_fifo 2 %X rd at %08X\n", pa, fault_PC);
        return 0;
        }
    else if (pa >= 0x39b21000) {
        sim_debug (DBG_REG, &ve_dev, "direct_fifo 1 %X rd at %08X\n", pa, fault_PC);
        return 0;
        }
    else {
        data = tbc_fifo[0].buf[rg];
        sim_debug (DBG_REG, &ve_dev, "direct_fifo 0 %X rd %X at %08X\n", pa, data, fault_PC);
        return data;
        }
    }
if (pa >= 0x39b1c000) {
    sim_debug (DBG_REG, &ve_dev, "bt459 cmap %X rd\n", (bt459_addr & 0xFF));
    return 0;
    }
if (pa >= 0x39b18000) {
    sim_debug (DBG_REG, &ve_dev, "bt459 reg  %X rd\n", bt459_addr);
    return 0;
    }
if (pa == 0x39b14000) {
    /* sim_debug (DBG_REG, &ve_dev, "bt459 adrh %08X rd %X\n", pa); */
    return (bt459_addr >> 8) & 0xFF;
    }
if (pa == 0x39b10000) {
    /* sim_debug (DBG_REG, &ve_dev, "bt459 adrl %08X rd %X\n", pa); */
    return bt459_addr & 0xFF;
    }
if (pa >= 0x39b00000) {
    /* sim_debug (DBG_REG, &ve_dev, "spx   reg1 %08X rd %X\n", pa); */
    rg = rg & 0xFF;
    data = tbc_rd (rg);
    return data;
    }
/* if (pa >= 0x39300000) { */
if ((pa & 0xFF000000) >= 0x39000000) {
    /* sim_debug (DBG_REG, &ve_dev, "spx   reg2 %08X rd %X\n", pa); */
    rg = rg & 0xFF;
    /* if (pa >= 0x39303000) { */
    if ((pa & 0xFF00FFFF) > 0x39003000) {
        switch (rg) {
            case 0x5B:  /* STATUS */
                sim_debug (DBG_REG, &ve_dev, "scanproc cmdalt rd at %08X\n", fault_PC);
                break;

            case 0x5C:
                sim_debug (DBG_REG, &ve_dev, "scanproc write_mask rd at %08X\n", fault_PC);
                break;

            case 0x5D:
                sim_debug (DBG_REG, &ve_dev, "scanproc read_mask rd at %08X\n", fault_PC);
                break;

            default:
                sim_debug (DBG_REG, &ve_dev, "scanproc %08X rd\n", pa);
                break;
                }
        }
    /* else if (pa >= 0x39302000) { */
    else if ((pa & 0xFF00FFFF) > 0x39002000) {
        return scn_rd (rg);
        }
    else {
        sim_debug (DBG_REG, &ve_dev, "scanproc %08X rd at %08X\n", pa, fault_PC);
        }
    return data;
    }

rg = pa & 0x3FFFFF;
return (GETL (ve_buf, rg) & spx_rmask);
}

void ve_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = ((pa - 0x38000000) >> 2);
uint32 scrln;

if (pa >= 0x39b20000) {
    rg = rg & 0xFFF;
    if (pa >= 0x39b23000) {
        sim_debug (DBG_REG, &ve_dev, "direct_fifo 3 %X wr %X\n", pa, val);
        return;
        }
    else if (pa >= 0x39b22000) {
        sim_debug (DBG_REG, &ve_dev, "direct_fifo 2 %X wr %X\n", pa, val);
        return;
        }
    else if (pa >= 0x39b21000) {
        sim_debug (DBG_REG, &ve_dev, "direct_fifo 1 %X wr %X\n", pa, val);
        return;
        }
    else {
        sim_debug (DBG_REG, &ve_dev, "direct_fifo 0 %X wr %X\n", pa, val);
        tbc_fifo[0].buf[rg] = val;
        return;
        }
    }
if (pa >= 0x39b1c000) {
    /* sim_debug (DBG_REG, &ve_dev, "bt459 cmap %X wr %X\n", bt459_addr, val); */
    bt459_cmap[bt459_cmap_p++] = val;
    if (bt459_cmap_p == 3) {
        bt459_cmap_p = 0;
        sim_debug (DBG_REG, &ve_dev, "bt459 cmap %X wr %X, %X, %X at %08X\n", (bt459_addr & 0xFF), bt459_cmap[0], bt459_cmap[1], bt459_cmap[2], fault_PC);
        ve_palette[(bt459_addr & 0xFF)] = vid_map_rgb (bt459_cmap[0], bt459_cmap[1], bt459_cmap[2]);
        sim_debug (DBG_REG, &ve_dev, "ve_palette[%d] = 0x%08X\n", (bt459_addr & 0xFF), ve_palette[(bt459_addr & 0xFF)]);
        bt459_addr++;
        if (bt459_addr == 0x100) bt459_addr = 0;
        }
    return;
    }
if (pa >= 0x39b18000) {
    sim_debug (DBG_REG, &ve_dev, "bt459 reg  %X wr %X\n", bt459_addr, val);
    return;
    }
if (pa == 0x39b14000) {
    bt459_addr = (bt459_addr & 0xFF) | ((val & 0xFF) << 8);
    /* sim_debug (DBG_REG, &ve_dev, "bt459 adrh %08X wr %X (%X)\n", pa, val, bt459_addr); */
    return;
    }
if (pa == 0x39b10000) {
    bt459_addr = (bt459_addr & ~0xFF) | (val & 0xFF);
    /* sim_debug (DBG_REG, &ve_dev, "bt459 adrl %08X wr %X (%X)\n", pa, val, bt459_addr); */
    return;
    }
if (pa >= 0x39b00000) {
    /* sim_debug (DBG_REG, &ve_dev, "tbc reg %X wr %X (%08X)\n", rg, val, pa); */
    rg = rg & 0xFF;
    tbc_wr (rg, val, lnt);
    return;
    }
if (pa >= 0x39300000) {
    /* sim_debug (DBG_REG, &ve_dev, "spx reg %X wr %X (%08X)\n", rg, val, pa); */
    rg = rg & 0xFF;
    if (pa > 0x39303000) {
        switch (rg) {
            case 0x5B:  /* STATUS */
                sim_debug (DBG_REG, &ve_dev, "scanproc cmdalt wr %X at %08X\n", val, fault_PC);
                break;

            case 0x5C:
                sim_debug (DBG_REG, &ve_dev, "scanproc write_mask wr %X at %08X\n", val, fault_PC);
                spx_wmask = val;
                break;

            case 0x5D:
                sim_debug (DBG_REG, &ve_dev, "scanproc read_mask wr %X at %08X\n", val, fault_PC);
                spx_rmask = val;
                break;

            default:
                sim_debug (DBG_REG, &ve_dev, "tbc %08X wr %X\n", pa, val);
                break;
                }
        }
    else {
        scn_wr (rg, val, lnt);
        return;
        }
    return;
    }
if (pa >= 0x39000000) {
    /* sim_debug (DBG_REG, &ve_dev, "spx reg %X wr %X (%08X)\n", rg, val, pa); */
    rg = rg & 0xFF;
    scn_wr (rg, val, lnt);
    return;
    }

rg = pa & 0x3FFFFF;

if (lnt > L_WORD) {
    PUTL (ve_buf, rg, (val & spx_wmask));
    }
else if (lnt > L_BYTE) {
    PUTW (ve_buf, rg, (val & ((pa & 2) ? (spx_wmask >> 16) : spx_wmask)));
    }
else
    ve_buf[rg] = (val & (spx_wmask >> ((pa & 3) << 3)));

scrln = ((rg / VE_BXSIZE) + (vc_org << VE_ORSC));
if (scrln < VE_YSIZE)
    ve_updated[scrln] = TRUE;                           /* flag as updated */
return;
}

void ve_put_fifo (uint32 id, uint32 data)
{
if (tbc_fifo[id].count > 0) {
    tbc_fifo[id].buf[tbc_fifo[id].put_ptr++] = data;
    if (tbc_fifo[id].put_ptr == FIFO_LEN)
        tbc_fifo[id].put_ptr = 0;
    tbc_fifo[id].count = tbc_fifo[id].count - 4;
    if (tbc_fifo[id].count >= tbc_fifo[id].threshold) {
        }
    }
}

void ve_get_fifo (uint32 id, uint32 *data)
{
if (tbc_fifo[id].count < FIFO_LEN) {
    *data = tbc_fifo[id].buf[tbc_fifo[id].get_ptr++];
    if (tbc_fifo[id].get_ptr == FIFO_LEN)
        tbc_fifo[id].get_ptr = 0;
    tbc_fifo[id].count = tbc_fifo[id].count + 4;
    }
}

void ve_clear_fifo (uint32 id)
{
tbc_fifo[id].put_ptr = 0;
tbc_fifo[id].get_ptr = 0;
tbc_fifo[id].count = FIFO_LEN;
}

int32 tbc_rd (int32 rg)
{
uint32 data = 0;

switch (rg) {
    case 0x0:
        data = tbc_csr;
        sim_debug (DBG_REG, &ve_dev, "tbc csr rd %X at %08X\n", data, fault_PC);
        break;

    case 0x1:
        sim_debug (DBG_REG, &ve_dev, "tbc cmd rd at %08X\n", fault_PC);           /* no read? */
        break;

    case 0x3:
        sim_debug (DBG_REG, &ve_dev, "tbc diag rd at %08X\n", fault_PC);
        break;

    case 0x4:
        /* sim_debug (DBG_REG, &ve_dev, "tbc cp_fb_format rd at %08X\n", fault_PC); */
        data = cp_fb_format;
        break;

    case 0x5:
        /* sim_debug (DBG_REG, &ve_dev, "tbc cp_int_mask rd at %08X\n", fault_PC); */
        data = cp_int_mask;
        break;

    case 0x6:
        /* sim_debug (DBG_REG, &ve_dev, "tbc cp_int_status rd at %08X\n", fault_PC); */
        data = cp_int_status;
        break;

    case 0x8:
        /* sim_debug (DBG_REG, &ve_dev, "tbc gf_fb_format rd at %08X\n", fault_PC); */
        data = gf_fb_format;
        break;

    case 0x9:
        sim_debug (DBG_REG, &ve_dev, "tbc gf_int_mask rd at %08X\n", fault_PC);
        break;

    case 0xA:
        sim_debug (DBG_REG, &ve_dev, "tbc gf_int_status rd at %08X\n", fault_PC);
        break;

    case 0x10:                                          /* FIFO0 Data */
    case 0x18:
    case 0x20:
    case 0x28:
        ve_get_fifo (GET_FIFO(rg), &data);
        if (tbc_csr & TBC_CSR_STRSTAT) {                /* stream in progress? */
            if (tbc_fifo[GET_FIFO(rg)].count >= tbc_fifo[GET_FIFO(rg)].threshold)
                sim_activate (&ve_unit[1], 200);        /* get more data */
            }
        sim_debug (DBG_REG, &ve_dev, "tbc fifo%d data rd %X at %08X\n", GET_FIFO(rg), data, fault_PC);
        break;

    case 0x11:
    case 0x19:
    case 0x21:
    case 0x29:
        data = tbc_fifo[GET_FIFO(rg)].put_ptr;
        break;

    case 0x12:
    case 0x1A:
    case 0x22:
    case 0x2A:
        data = tbc_fifo[GET_FIFO(rg)].get_ptr;
        break;

    case 0x13:  /* FIFO0 Count */
    case 0x1B:
    case 0x23:
    case 0x2B:
        data = tbc_fifo[GET_FIFO(rg)].count;
        sim_debug (DBG_REG, &ve_dev, "tbc fifo%d count rd %X at %08X\n", GET_FIFO(rg), data, fault_PC);
        break;

    case 0x14:
    case 0x1C:
    case 0x24:
    case 0x2C:
        data = tbc_fifo[GET_FIFO(rg)].threshold;
        sim_debug (DBG_REG, &ve_dev, "tbc fifo%d threshold rd %X at %08X\n", GET_FIFO(rg), data, fault_PC);
        break;

    case 0x15:
    case 0x1D:
    case 0x25:
    case 0x2D:
        data = tbc_fifo[GET_FIFO(rg)].semaphore;
        sim_debug (DBG_REG, &ve_dev, "tbc fifo%d semaphore rd %X at %08X\n", GET_FIFO(rg), data, fault_PC);
        break;

    case 0x40:
        tbc_timing = tbc_timing ^ 0x14000;
        data = tbc_timing;
        sim_debug (DBG_REG, &ve_dev, "tbc timing_csr rd %X at %08X\n", data, fault_PC);
        break;

    case 0x41:
        sim_debug (DBG_REG, &ve_dev, "tbc hsync rd at %08X\n", fault_PC);
        break;

    case 0x42:
        sim_debug (DBG_REG, &ve_dev, "tbc hsync2 rd at %08X\n", fault_PC);
        break;

    case 0x43:
        sim_debug (DBG_REG, &ve_dev, "tbc early_hblank rd at %08X\n", fault_PC);
        break;

    case 0x44:
        sim_debug (DBG_REG, &ve_dev, "tbc vsync rd at %08X\n", fault_PC);
        break;

    case 0x45:
        sim_debug (DBG_REG, &ve_dev, "tbc vblank rd at %08X\n", fault_PC);
        break;

    case 0x46:
        data = tbc_table;
        sim_debug (DBG_REG, &ve_dev, "tbc table rd %X at %08X\n", data, fault_PC);
        break;

    case 0x47:
        data = tbc_timing_setup;
        sim_debug (DBG_REG, &ve_dev, "tbc timing_setup rd %X at %08X\n", data, fault_PC);
        break;

    case 0x48:
        data = tbc_ltrr;
        tbc_ltrr++;
        if (tbc_ltrr == 5) tbc_ltrr = 0;
        sim_debug (DBG_REG, &ve_dev, "tbc ltrr rd %X at %08X\n", data, fault_PC);
        break;

    case 0x50:
        sim_debug (DBG_REG, &ve_dev, "tbc sp_bus_loop rd at %08X\n", fault_PC);
        break;

    default:
        sim_debug (DBG_REG, &ve_dev, "tbc %X rd at %08X\n", rg, fault_PC);
        break;
        }
return data;
}

void tbc_wr (int32 rg, int32 val, int32 lnt)
{
uint32 i;

switch (rg) {
    case 0x0:
        sim_debug (DBG_REG, &ve_dev, "tbc csr wr %X at %08X\n", val, fault_PC);
        break;

    case 0x1:
        sim_debug (DBG_REG, &ve_dev, "tbc cmd wr %X at %08X\n", val, fault_PC);
        for (i = 0; i < 4; i++) {
            if (val & TBC_CMD_FIFOEN(i)) {
                tbc_csr |= TBC_CSR_FIFOEN(i);
                sim_debug (DBG_REG, &ve_dev, "fifo %d enable\n", i);
                }
            else if (val & TBC_CMD_FIFODIS(i)) {
                tbc_csr &= ~TBC_CSR_FIFOEN(i);
                sim_debug (DBG_REG, &ve_dev, "fifo %d disable\n", i);
                }
            if (val & TBC_CMD_FIFOOUT(i)) {
                tbc_csr |= TBC_CSR_FIFODIR(i);
                sim_debug (DBG_REG, &ve_dev, "fifo %d output\n", i);
                }
            else if (val & TBC_CMD_FIFOIN(i)) {
                tbc_csr &= ~TBC_CSR_FIFODIR(i);
                sim_debug (DBG_REG, &ve_dev, "fifo %d input\n", i);
                }
            }
        if (val & 0x800000) { /* Input FIFO Load */
            tbc_csr = (tbc_csr & ~0x3) | ((val >> 21) & 0x3);
            }
        if (val & 0x4000000) { /* Output FIFO Load */
            tbc_csr = (tbc_csr & ~0x300) | ((val >> 16) & 0x300);
            }
        /* NOTE:
         * Set STREAM_STATUS and STREAM direction for the
         * following commands.
         * 0 = INPUT (to card)
         * 1 = OUTPUT (from card)
         */
        if (val & TBC_CMD_STRW) {
            sim_debug (DBG_REG, &ve_dev, "stream write\n");
            tbc_csr |= TBC_CSR_STRDIR;                  /* stream write */
            tbc_csr |= TBC_CSR_STRSTAT;                 /* stream active */
            }
        else if (val & TBC_CMD_STRR) {
            sim_debug (DBG_REG, &ve_dev, "stream read\n");
            tbc_csr &= ~TBC_CSR_STRDIR;                 /* stream read */
            tbc_csr |= TBC_CSR_STRSTAT;                 /* stream active */
            }
        else {
            sim_debug (DBG_REG, &ve_dev, "stream disable\n");
            tbc_csr &= ~TBC_CSR_STRDIR;
            tbc_csr &= ~TBC_CSR_STRSTAT;                /* stream inactive */
            }
        break;

    case 0x3:
        sim_debug (DBG_REG, &ve_dev, "tbc diag wr %X at %08X\n", val, fault_PC);
        if (val & 0x10) { /* synchronous reset */
            for (i = 0; i < 4; i++) {
                ve_clear_fifo (i);
                }
            sim_debug (DBG_REG, &ve_dev, "tbc reset\n");
            bt459_cmap_p = 0;
        }
        else if (val & 0x1) {
            ve_clear_fifo (0);
            sim_debug (DBG_REG, &ve_dev, "fifo0 reset\n");
            }
        else if (val & 0x2) {
            ve_clear_fifo (1);
            sim_debug (DBG_REG, &ve_dev, "fifo1 reset\n");
            }
        else if (val & 0x4) {
            ve_clear_fifo (2);
            sim_debug (DBG_REG, &ve_dev, "fifo2 reset\n");
            }
        else if (val & 0x8) {
            ve_clear_fifo (3);
            sim_debug (DBG_REG, &ve_dev, "fifo3 reset\n");
            }
        break;

    case 0x4:
        /* sim_debug (DBG_REG, &ve_dev, "tbc cp_fb_format wr %X at %08X\n", val, fault_PC); */
        cp_fb_format = val;
        break;

    case 0x5:
        sim_debug (DBG_REG, &ve_dev, "tbc cp_int_mask wr %X at %08X\n", val, fault_PC);
        cp_int_mask = val;
        break;

    case 0x6:
        /* sim_debug (DBG_REG, &ve_dev, "tbc cp_int_status wr %X at %08X\n", val, fault_PC); */
        cp_int_status = (cp_int_status & ~val);
        break;

    case 0x8:
        /* sim_debug (DBG_REG, &ve_dev, "tbc gf_fb_format wr %X at %08X\n", val, fault_PC); */
        gf_fb_format = val;
        break;

    case 0x9:
        sim_debug (DBG_REG, &ve_dev, "tbc gf_int_mask wr %X at %08X\n", val, fault_PC);
        break;

    case 0xA:
        sim_debug (DBG_REG, &ve_dev, "tbc gf_int_status wr %X at %08X\n", val, fault_PC);
        break;

    case 0x10:  /* FIFO0 Data */
    case 0x18:
    case 0x20:
    case 0x28:
        sim_debug (DBG_REG, &ve_dev, "tbc fifo%d data wr %X at %08X (ptr = %d)\n", GET_FIFO(rg), val, fault_PC, tbc_fifo[GET_FIFO(rg)].put_ptr);
        ve_put_fifo (GET_FIFO(rg), val);
        if (tbc_csr & TBC_CSR_STRSTAT) {                /* stream in progress? */
            if (tbc_fifo[GET_FIFO(rg)].count < tbc_fifo[GET_FIFO(rg)].threshold)
                sim_activate (&ve_unit[1], 200);        /* flush data */
            }
        break;

    case 0x11:
    case 0x19:
    case 0x21:
    case 0x29:
        sim_debug (DBG_REG, &ve_dev, "tbc fifo%d put_ptr wr %X at %08X\n", GET_FIFO(rg), val, fault_PC);
        tbc_fifo[GET_FIFO(rg)].put_ptr = val;
        break;

    case 0x12:
    case 0x1A:
    case 0x22:
    case 0x2A:
        sim_debug (DBG_REG, &ve_dev, "tbc fifo%d get_ptr wr %X at %08X\n", GET_FIFO(rg), val, fault_PC);
        /* tbc_fifo[GET_FIFO(rg)].get_ptr = val; */
        break;

    case 0x13:  /* FIFO0 Count */
    case 0x1B:
    case 0x23:
    case 0x2B:
        sim_debug (DBG_REG, &ve_dev, "tbc fifo%d count wr %X at %08X\n", GET_FIFO(rg), val, fault_PC);
        tbc_fifo[GET_FIFO(rg)].count = val;
        break;

    case 0x14:
    case 0x1C:
    case 0x24:
    case 0x2C:
        sim_debug (DBG_REG, &ve_dev, "tbc fifo%d threshold wr %X at %08X\n", GET_FIFO(rg), val, fault_PC);
        tbc_fifo[GET_FIFO(rg)].threshold = val;
        break;

    case 0x15:
    case 0x1D:
    case 0x25:
    case 0x2D:
        sim_debug (DBG_REG, &ve_dev, "tbc fifo%d semaphore wr %X at %08X\n", GET_FIFO(rg), val, fault_PC);
        tbc_fifo[GET_FIFO(rg)].semaphore = val;
        break;

    case 0x40:
        sim_debug (DBG_REG, &ve_dev, "tbc timing_csr wr %X at %08X\n", val, fault_PC);
        tbc_timing = val;
        break;

    case 0x41:
        sim_debug (DBG_REG, &ve_dev, "tbc hsync wr %X at %08X\n", val, fault_PC);
        break;

    case 0x42:
        sim_debug (DBG_REG, &ve_dev, "tbc hsync2 wr %X at %08X\n", val, fault_PC);
        break;

    case 0x43:
        sim_debug (DBG_REG, &ve_dev, "tbc early_hblank wr %X at %08X\n", val, fault_PC);
        break;

    case 0x44:
        sim_debug (DBG_REG, &ve_dev, "tbc vsync wr %X at %08X\n", val, fault_PC);
        break;

    case 0x45:
        sim_debug (DBG_REG, &ve_dev, "tbc vblank wr %X at %08X\n", val, fault_PC);
        break;

    case 0x46:
        sim_debug (DBG_REG, &ve_dev, "tbc table wr %X at %08X\n", val, fault_PC);
        tbc_table = val;
        break;

    case 0x47:
        sim_debug (DBG_REG, &ve_dev, "tbc timing_setup wr %X at %08X\n", val, fault_PC);
        tbc_timing_setup = val;
        break;

    case 0x48:
        sim_debug (DBG_REG, &ve_dev, "tbc ltrr wr %X at %08X\n", val, fault_PC);
        break;

    case 0x50:
        sim_debug (DBG_REG, &ve_dev, "tbc sp_bus_loop wr %X at %08X\n", val, fault_PC);
        break;

    default:
        sim_debug (DBG_REG, &ve_dev, "tbc %X wr %X at %08X\n", rg, val, fault_PC);
        break;
        }
}

int32 scn_rd (int32 rg)
{
uint32 data = 0;

switch (rg) {
    case 0x40:                                          /* STATUS */
        data = spx_status;
        sim_debug (DBG_REG, &ve_dev, "scanproc status rd %X at %08X\n", data, fault_PC);
        break;

    case 0x46:
        sim_debug (DBG_REG, &ve_dev, "scanproc config rd at %08X\n", fault_PC);
        break;

    case 0x7F:
        sim_debug (DBG_REG, &ve_dev, "scanproc micropc rd at %08X\n", fault_PC);
        break;

    case 0xFE:
        /* sim_debug (DBG_REG, &ve_dev, "scanproc microlo rd at %08X\n", fault_PC); */
        break;

    case 0x4B:
        /* sim_debug (DBG_REG, &ve_dev, "scanproc microhi rd at %08X\n", fault_PC); */
        break;

    case 0x80:
        sim_debug (DBG_REG, &ve_dev, "scanproc rowframe rd at %08X\n", fault_PC);
        break;

    case 0x0:
        sim_debug (DBG_REG, &ve_dev, "scanproc rowframe_mask rd at %08X\n", fault_PC);
        break;

    case 0x64:
        sim_debug (DBG_REG, &ve_dev, "scanproc maingreg0 rd at %08X\n", fault_PC);
        break;

    case 0x24:
        sim_debug (DBG_REG, &ve_dev, "scanproc mainreg0_mask rd at %08X\n", fault_PC);
        break;

    case 0x82:
        sim_debug (DBG_REG, &ve_dev, "scanproc winoffset rd at %08X\n", fault_PC);
        break;

    case 0x7B:
        sim_debug (DBG_REG, &ve_dev, "scanproc command rd at %08X\n", fault_PC);
        break;

    case 0x74:
        sim_debug (DBG_REG, &ve_dev, "scanproc xstart rd at %08X\n", fault_PC);
        break;

    case 0x75:
        sim_debug (DBG_REG, &ve_dev, "scanproc ystart rd at %08X\n", fault_PC);
        break;

    case 0x76:
        sim_debug (DBG_REG, &ve_dev, "scanproc xend rd at %08X\n", fault_PC);
        break;

    case 0x77:
        sim_debug (DBG_REG, &ve_dev, "scanproc yend rd at %08X\n", fault_PC);
        break;

    case 0x78:
        sim_debug (DBG_REG, &ve_dev, "scanproc dstpix rd at %08X\n", fault_PC);
        break;

    case 0x38:
        sim_debug (DBG_REG, &ve_dev, "scanproc dstpix1 rd at %08X\n", fault_PC);
        break;

    case 0x79:
        sim_debug (DBG_REG, &ve_dev, "scanproc srcpix rd at %08X\n", fault_PC);
        break;

    case 0x39:
        sim_debug (DBG_REG, &ve_dev, "scanproc srcpix1 rd at %08X\n", fault_PC);
        break;

    case 0x67:
        sim_debug (DBG_REG, &ve_dev, "scanproc main3 rd at %08X\n", fault_PC);
        break;

    case 0x7A:
        sim_debug (DBG_REG, &ve_dev, "scanproc stride rd at %08X\n", fault_PC);
        break;

    case 0x7C:
        sim_debug (DBG_REG, &ve_dev, "scanproc srcmask rd at %08X\n", fault_PC);
        break;

    case 0x7D:
        sim_debug (DBG_REG, &ve_dev, "scanproc dstmask rd at %08X\n", fault_PC);
        break;

    case 0x98:
        sim_debug (DBG_REG, &ve_dev, "scanproc fg rd at %08X\n", fault_PC);
        break;

    case 0x99:
        sim_debug (DBG_REG, &ve_dev, "scanproc bg rd at %08X\n", fault_PC);
        break;

    case 0x9C:
        sim_debug (DBG_REG, &ve_dev, "scanproc destloop rd at %08X\n", fault_PC);
        break;

    default:
        sim_debug (DBG_REG, &ve_dev, "scanproc %X rd at %08X\n", rg, fault_PC);
        break;
    }
return data;
}

void scn_wr (int32 rg, int32 val, int32 lnt)
{
switch (rg) {
    case 0x40:  /* STATUS */
        sim_debug (DBG_REG, &ve_dev, "scanproc status wr %X at %08X\n", val, fault_PC);
        spx_status = (spx_status & ~val);
        break;

    case 0x46:
        sim_debug (DBG_REG, &ve_dev, "scanproc config wr %X at %08X\n", val, fault_PC);
        break;

    case 0x7F:
        sim_debug (DBG_REG, &ve_dev, "scanproc micropc wr %X at %08X\n", val, fault_PC);
        spx_upc = val & 0xFFFF;
        sim_activate (&ve_unit[1], 200);
        break;

    case 0xFE:
        /* sim_debug (DBG_REG, &ve_dev, "scanproc microlo wr %X at %08X\n", val, fault_PC); */
        break;

    case 0x4B:
        /* sim_debug (DBG_REG, &ve_dev, "scanproc microhi wr %X at %08X\n", val, fault_PC); */
        break;

    case 0x80:
        sim_debug (DBG_REG, &ve_dev, "scanproc rowframe wr %X at %08X\n", val, fault_PC);
        break;

    case 0x0:
        sim_debug (DBG_REG, &ve_dev, "scanproc rowframe_mask wr %X at %08X\n", val, fault_PC);
        break;

    case 0x64:
        sim_debug (DBG_REG, &ve_dev, "scanproc maingreg0 wr %X at %08X\n", val, fault_PC);
        break;

    case 0x24:
        sim_debug (DBG_REG, &ve_dev, "scanproc mainreg0_mask wr %X at %08X\n", val, fault_PC);
        break;

    case 0x82:
        sim_debug (DBG_REG, &ve_dev, "scanproc winoffset wr %X at %08X\n", val, fault_PC);
        break;

    case 0x7B:
        sim_debug (DBG_REG, &ve_dev, "scanproc command wr %X at %08X\n", val, fault_PC);
        spx_cmd = val;
        break;

    case 0xF4:
    case 0x74:
        sim_debug (DBG_REG, &ve_dev, "scanproc xstart wr %X at %08X\n", val, fault_PC);
        spx_xstart = val;
        break;

    case 0xF5:
    case 0x75:
        sim_debug (DBG_REG, &ve_dev, "scanproc ystart wr %X at %08X\n", val, fault_PC);
        spx_ystart = val;
        break;

    case 0xF6:
    case 0x76:
        sim_debug (DBG_REG, &ve_dev, "scanproc xend wr %X at %08X\n", val, fault_PC);
        spx_xend = val;
        break;

    case 0xF7:
    case 0x77:
        sim_debug (DBG_REG, &ve_dev, "scanproc yend wr %X at %08X\n", val, fault_PC);
        spx_yend = val;
        break;

    case 0x78:
        sim_debug (DBG_REG, &ve_dev, "scanproc dstpix wr %X at %08X\n", val, fault_PC);
        spx_dstpix = val;
        break;

    case 0x38:
        sim_debug (DBG_REG, &ve_dev, "scanproc dstpix1 wr %X at %08X\n", val, fault_PC);
        break;

    case 0x79:
        sim_debug (DBG_REG, &ve_dev, "scanproc srcpix wr %X at %08X\n", val, fault_PC);
        spx_srcpix = val;
        break;

    case 0x39:
        sim_debug (DBG_REG, &ve_dev, "scanproc srcpix1 wr %X at %08X\n", val, fault_PC);
        break;

    case 0x67:
        sim_debug (DBG_REG, &ve_dev, "scanproc main3 wr %X at %08X\n", val, fault_PC);
        break;

    case 0xFA:
    case 0x7A:
        sim_debug (DBG_REG, &ve_dev, "scanproc stride wr %X at %08X\n", val, fault_PC);
        break;

    case 0x7C:
        sim_debug (DBG_REG, &ve_dev, "scanproc srcmask wr %X at %08X\n", val, fault_PC);
        spx_smask = val;
        break;

    case 0x7D:
        sim_debug (DBG_REG, &ve_dev, "scanproc dstmask wr %X at %08X\n", val, fault_PC);
        spx_dmask = val;
        break;

    case 0xD8:
    case 0x98:
        sim_debug (DBG_REG, &ve_dev, "scanproc fg wr %X at %08X\n", val, fault_PC);
        spx_fg = val;
        break;

    case 0x99:
        sim_debug (DBG_REG, &ve_dev, "scanproc bg wr %X at %08X\n", val, fault_PC);
        break;

    case 0x9C:
        sim_debug (DBG_REG, &ve_dev, "scanproc destloop wr %X at %08X\n", val, fault_PC);
        spx_destloop = val;
        break;

    default:
        sim_debug (DBG_REG, &ve_dev, "scanproc %X wr %X\n", rg, val);
        break;
        }
}

void spx_fill_rect (void)
{
uint32 xstart, xend, ystart, yend;
uint32 dstpix;
uint32 x, y;

xstart = (spx_xstart >> 16);
ystart = (spx_ystart >> 16);
xend = (spx_xend >> 16);
yend = (spx_yend >> 16);
if (spx_cmd & 0x400)                                    /* absolute coordinates */
    dstpix = 0;
else
    dstpix = spx_dstpix & 0xFFFFFF;

sim_debug (DBG_ROP, &ve_dev, "fill_rect: xs = %d, xe = %d, ys = %d, ye = %d, dx = %d, dy = %d, fg = %X\n",
    xstart, xend, ystart, yend, (dstpix % 1280), (dstpix / 1280), (spx_fg & 0xFF));

if (xend > VE_BXSIZE)                                   /* clip X */
    xend = VE_BXSIZE;
if (yend > VE_BYSIZE)                                   /* clip Y */
    yend = VE_BYSIZE;

if ((spx_destloop & 0xFFFF) != 0x2006) {
    for (y = ystart; y < yend; y++) {
        for (x = xstart; x < xend; x++) {
            ve_buf[((y * 1280) + x + dstpix)] = (spx_fg & 0xFF);
            }
        if (y < VE_YSIZE)
            ve_updated[y] = TRUE;                       /* FIXME: map to screen line */
        }
    }
cp_int_status |= 0x2;
}

void spx_copy_rect (void)
{
uint32 xstart, xend, ystart, yend;
uint32 srcpix, dstpix;
uint32 x, y;

xstart = (spx_xstart >> 16);
ystart = (spx_ystart >> 16);
xend = (spx_xend >> 16);
yend = (spx_yend >> 16);
srcpix = spx_srcpix & 0xFFFFFF;
if (spx_cmd & 0x400)                                    /* absolute coordinates */
    dstpix = 0;
else
    dstpix = spx_dstpix & 0xFFFFFF;

sim_debug (DBG_ROP, &ve_dev, "copy_rect: xs = %d, xe = %d, ys = %d, ye = %d, sx = %d, sy = %d, dx = %d, dy = %d\n",
    xstart, xend, ystart, yend, (srcpix % 1280), (srcpix / 1280), (dstpix % 1280), (dstpix / 1280));

if (xend > VE_BXSIZE)                                   /* clip X */
    xend = VE_BXSIZE;
if (yend > VE_BYSIZE)                                   /* clip Y */
    yend = VE_BYSIZE;

for (y = ystart; y < yend; y++) {
    for (x = xstart; x < xend; x++) {
        ve_buf[((y * 1280) + x + dstpix)] = \
            (ve_buf[((y * 1280) + x + dstpix)] & ~spx_dmask) | \
            (ve_buf[(((y - ystart) * 1280) + (x - xstart) + srcpix)] & spx_smask);
        }
    if (y < VE_YSIZE)
        ve_updated[y] = TRUE;                           /* FIXME: map to screen line */
    }
cp_int_status |= 0x2;
}

void spx_stream_data ()
{
uint32 xstart, xend, ystart, yend;
uint32 dstpix;
uint32 data, i;

xstart = (spx_xstart >> 16);
ystart = (spx_ystart >> 16);
xend = (spx_xend >> 16);
yend = (spx_yend >> 16);

if (spx_cmd & 0x400)                                    /* absolute coordinates */
    dstpix = 0;
else
    dstpix = spx_dstpix & 0xFFFFFF;
spx_strx = xstart;
spx_stry = ystart;

sim_debug (DBG_ROP, &ve_dev, "stream_data: xs = %d, xe = %d, ys = %d, ye = %d, dx = %d, dy = %d\n",
    xstart, xend, ystart, yend, (dstpix % 1280), (dstpix / 1280));

if (tbc_csr & TBC_CSR_STRDIR) {                         /* write */
    while (tbc_fifo[0].count < FIFO_LEN) {
        ve_get_fifo (0, &data);
        for (i = 0; i < 4; i++) {
            sim_debug (DBG_REG, &ve_dev, "buffer[%X] = %X\n", ((spx_stry * 1280) + spx_strx + dstpix), ((data >> (i << 3)) &  0xFF));
            ve_buf[((spx_stry * 1280) + spx_strx + dstpix)] = ((data >> (i << 3)) & 0xFF);
            spx_strx++;
            if (spx_stry < VE_YSIZE)
                ve_updated[spx_stry] = TRUE;
            if (spx_strx > xend) {
                spx_strx = xstart;
                spx_stry++;
                }
            if (spx_stry > yend) {
                cp_int_status |= 0x2;       /* Done */
                spx_status |= 0x100;        /* Done */
                tbc_csr &= ~TBC_CSR_STRSTAT;
                sim_debug (DBG_REG, &ve_dev, "stream done\n");
                return;
                }
            }
        }
    }
else {                                                  /* read */
    while (tbc_fifo[0].count > 0) {
        data = 0;
        for (i = 0; i < 4; i++) {
            if (spx_strx > xend) {
                spx_strx = xstart;
                spx_stry++;
                }
            if (spx_stry > yend) break;
            /* sim_debug (DBG_REG, &ve_dev, "buffer[%X] = %X\n", ((spx_stry * 1280) + spx_strx + dstpix), ((val >> (i << 3)) & spx_wmask & 0xFF)); */
            data |= (((ve_buf[((spx_stry * 1280) + spx_strx + dstpix)]) & spx_rmask & 0xFF) << (i << 3));
            spx_strx++;

            if (spx_stry > yend) {
                cp_int_status |= 0x2;
                return;
                }
            }
        ve_put_fifo (0, data);
        }
    }
}

t_stat ve_micro_svc (UNIT *uptr)
{
switch (spx_upc) {

    case 0x23AE:
    case 0x2019:
        spx_fill_rect ();
        break;

    case 0x1F:
    case 0x239C:                                        /* Read? */
    case 0x23A2:                                        /* Bitmap write? */
    case 0x53:
        spx_stream_data ();
        break;

    case 0x23CB:
    case 0xA9:
        spx_copy_rect ();
        break;

    case 0x2153:                                        /* Load microcode? */
        break;

    default:
        sim_debug (DBG_REG, &ve_dev, "unknown scanproc micropc %X\n", spx_upc);
        }
return SCPE_OK;
}

static SIM_INLINE void ve_invalidate (uint32 y1, uint32 y2)
{
uint32 ln;

for (ln = y1; ln < y2; ln++)
    ve_updated[ln] = TRUE;                              /* flag as updated */
}

t_stat ve_svc (UNIT *uptr)
{
SIM_MOUSE_EVENT mev;
SIM_KEY_EVENT kev;
t_bool updated = FALSE;                                 /* flag for refresh */
uint32 lines;
uint32 ln, col, off;
uint32 i, c;
uint32 rg, val;

for (i = 0; i < 4; i++) {
    if (tbc_csr & TBC_CSR_FIFOEN(i)) {
        if (tbc_fifo[i].count >= tbc_fifo[i].threshold)
            cp_int_status |= (INTSTS_F0_GE_THRSH << (4 * i));
        else
            cp_int_status |= (INTSTS_F0_LT_THRSH << (4 * i));
        if ((tbc_fifo[i].count & 1) == 0) {
            while (tbc_fifo[i].count < FIFO_LEN) {      /* while fifo not empty */
                /* NOTE:
                 * if ((tbc_csr & STREAM_STATUS) && ((tbc_csr & STREAM_DIRECTION) == INPUT))
                 *     stream to bitmap
                 * else if ((tbc_csr & STREAM_STATUS) && ((tbc_csr & STREAM_DIRECTION) == OUTPUT))
                 *     stream from bitmap
                 * else
                 *     register writes
                 */
                sim_debug (DBG_REG, &ve_dev, "get_ptr = %d, put_ptr = %d\n", tbc_fifo[i].get_ptr, tbc_fifo[i].put_ptr);
                if ((tbc_csr & TBC_CSR_STRSTAT) == 0) {
                    ve_get_fifo (i, &rg);
                    switch ((rg >> 20) & 0x3) {
                        case 0:                         /* SCN reg SWZ=0 */
                        case 1:                         /* SCN reg SWZ=1 */
                            rg = (rg >> 2) & 0xFF;
                            ve_get_fifo (i, &val);
                            sim_debug (DBG_REG, &ve_dev, "scn_wr(%X, %X)\n", rg, val);
                            scn_wr (rg, val, L_LONG);
                            break;

                        case 2:                         /* TBC reg */
                            rg = (rg >> 2) & 0xFF;
                            ve_get_fifo (i, &val);
                            sim_debug (DBG_REG, &ve_dev, "tbc_wr(%X, %X)\n", rg, val);
                            tbc_wr (rg, val, L_LONG);
                            break;
                            }
                    }
                }
            }
        }
    }
cp_int_status |= 0x10;  /* VBLANK Finish */
cp_int_status |= 0x1;   /* Ready */
spx_status |= 0x200;    /* Ready */

if (vid_poll_kb (&kev) == SCPE_OK)                      /* poll keyboard */
    lk_event (&kev);                                    /* push event */
if (vid_poll_mouse (&mev) == SCPE_OK)                   /* poll mouse */
    vs_event (&mev);                                    /* push event */

if (vc_org != vc_last_org)                              /* origin moved? */
    ve_invalidate (0, (VE_YSIZE - 1));                  /* redraw whole screen */

vc_last_org = vc_org;                                   /* store video origin */

lines = 0;
for (ln = 0; ln < VE_YSIZE; ln++) {
    if (ve_updated[ln]) {                               /* line invalid? */
        off = ((ln + (vc_org << VE_ORSC)) * VE_BXSIZE); /* get video buf offet */
        for (col = 0; col < VE_XSIZE; col++)
            ve_lines[ln*VE_XSIZE + col] = ve_palette[ve_buf[off + col]];
                                                        /* 8bpp to 32bpp */
#if 0
        if (CUR_V) {                                    /* cursor visible? */
            if ((ln >= CUR_Y) && (ln < (CUR_Y + 16))) { /* cursor on this line? */
                plna = &vc_cur[(CUR_PLNA + ln - CUR_Y)];/* get plane A base */
                plnb = &vc_cur[(CUR_PLNB + ln - CUR_Y)];/* get plane B base */
                for (col = 0; col < 16; col++) {
                    if ((CUR_X + col) >= VC_XSIZE)      /* Part of cursor off screen? */
                        continue;                       /* Skip */
                    if (vc_cmd & CMD_FOPA)              /* force plane A to 1? */
                        bita = 1;
                    else if (vc_cmd & CMD_ENPA)         /* plane A enabled? */
                        bita = (*plna >> col) & 1;
                    else bita = 0;
                    if (vc_cmd & CMD_FOPB)              /* force plane B to 1? */
                        bitb = 1;
                    else if (vc_cmd & CMD_ENPB)         /* plane B enabled? */
                        bitb = (*plnb >> col) & 1;
                    else bitb = 0;
                    line[CUR_X + col] = ve_palette[((line[CUR_X + col] == ve_palette[1]) & ~bitb) ^ bita];
                    }
                }
            }
#endif
        ve_updated[ln] = FALSE;                         /* set valid */
        if ((ln == (VE_YSIZE-1)) ||                     /* if end of window OR */
            (ve_updated[ln+1] == FALSE)) {              /* next is already valid? */
            vid_draw (0, ln-lines, VE_XSIZE, lines+1, ve_lines+(ln-lines)*VE_XSIZE); /* update region */
            lines = 0;
            }
        else
            lines++;
        updated = TRUE;
        }
    }

if (updated)                                            /* video updated? */
    vid_refresh ();                                     /* put to screen */

if (cp_int_status & cp_int_mask)
    SET_INT (VC2);
sim_activate (uptr, tmxr_poll);
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
return SCPE_OK;
}

t_stat ve_reset (DEVICE *dptr)
{
t_stat r;
uint32 i;

CLR_INT (VC2);
sim_cancel (&ve_unit[0]);                               /* deactivate units */
sim_cancel (&ve_unit[1]);

bt459_addr = 0;
cp_fb_format = 0;
cp_int_status = 0;
cp_int_mask = 0;
gf_fb_format = 0;
spx_xstart = 0;
spx_ystart = 0;
spx_xend = 0;
spx_yend = 0;
spx_dstpix = 0;
spx_srcpix = 0;
bt459_cmap_p = 0;
bt459_cmap[0] = 0;
bt459_cmap[1] = 0;
bt459_cmap[2] = 0;
spx_fg = 0;
tbc_csr = 0;
spx_cmd = 0;
spx_rmask = 0;
spx_wmask = 0;
spx_smask = 0;
spx_dmask = 0;
spx_strx = 0;
spx_stry = 0;
spx_destloop = 0;
tbc_timing = 0;
spx_status = 0;

for (i = 0; i < 4; i++)
    ve_clear_fifo (i);

for (i = 0; i < VE_YSIZE; i++)
    ve_updated[i] = FALSE;

if (dptr->flags & DEV_DIS) {
    if (ve_active) {
        free (ve_buf);
        ve_buf = NULL;
        free (ve_lines);
        ve_lines = NULL;
        ve_active = FALSE;
        return vid_close ();
        }
    else
        return SCPE_OK;
    }

if (!vid_active && !ve_active)  {
    r = vid_open (dptr, NULL, VE_XSIZE, VE_YSIZE, ve_input_captured ? SIM_VID_INPUTCAPTURED : 0);/* display size & capture mode */
    if (r != SCPE_OK)
        return r;
    ve_buf = (uint8 *) calloc (VE_BUFSIZE, sizeof (uint8));
    if (ve_buf == NULL) {
        vid_close ();
        return SCPE_MEM;
        }
    ve_lines = (uint32 *) calloc (VE_XSIZE*VE_YSIZE, sizeof (uint32));
    if (ve_lines == NULL) {
        free (ve_buf);
        vid_close ();
        return SCPE_MEM;
        }
    sim_printf ("SPX Video Display Created.  ");
    ve_show_capture (stdout, NULL, 0, NULL);
    if (sim_log)
        ve_show_capture (sim_log, NULL, 0, NULL);
    sim_printf ("\n");
    ve_active = TRUE;
    }
sim_activate_abs (&ve_unit[0], tmxr_poll);
return SCPE_OK;
}

t_stat ve_detach (UNIT *uptr)
{
if ((ve_dev.flags & DEV_DIS) == 0) {
    ve_dev.flags |= DEV_DIS;
    ve_reset(&ve_dev);
    }
return SCPE_OK;
}

t_stat ve_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
return cpu_set_model (NULL, 0, (val ? "VAXSTATIONSPX" : "MICROVAX"), NULL);
}

t_stat ve_set_capture (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (vid_active)
    return sim_messagef (SCPE_ALATT, "Capture Mode Can't be changed with device enabled\n");
ve_input_captured = val;
return SCPE_OK;
}

t_stat ve_show_capture (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
if (ve_input_captured) {
    fprintf (st, "Captured Input Mode, ");
    vid_show_release_key (st, uptr, val, desc);
    }
else
    fprintf (st, "Uncaptured Input Mode");
return SCPE_OK;
}

t_stat ve_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "SPX Colour Video Subsystem (%s)\n\n", dptr->name);
fprintf (st, "Use the Control-Right-Shift key combination to regain focus from the simulated\n");
fprintf (st, "video display\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *ve_description (DEVICE *dptr)
{
return "SPX Colour Graphics Adapter";
}
