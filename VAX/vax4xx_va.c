/* vax4xx_va.c: GPX video simulator

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

   va           GPX colour video
*/

#include "vax_defs.h"
#include "sim_video.h"
#include "vax_gpx.h"
#include "vax_lk.h"
#include "vax_vs.h"

#if VA_PLANES == 4
#include "vax_ka4xx_4pln_bin.h"
#else
#include "vax_ka4xx_8pln_bin.h"
#define BT458           1
#endif

#define VA_ADP_OF       0x0000                          /* address processor */
#define VA_FCC_OF       0x0100                          /* FIFO compression chip */
#define VA_DAC_OF       0x0180                          /* D/A converter */
#define VA_CCR_OF       0x0200                          /* cursor chip */
#define VA_CBR_OF       0x0280                          /* colour board readback */
#define VA_FFW_OF       0x4000                          /* FIFO window */

#define VA_TMP_OF       0x40

#define INT_FCC         1

#define CUR_X_OF        216
#define CUR_Y_OF        33

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

#define CUR_X           ((va_xpos < CUR_X_OF) ? 0 : (va_xpos - CUR_X_OF))
#define CUR_Y           ((va_ypos < CUR_Y_OF) ? 0 : (va_ypos - CUR_Y_OF))
#define CUR_V           ((va_ccmd & CMD_LODSA) == 0)
#define CUR_F           (0)

#define CUR_PLNA        0
#define CUR_PLNB        16

#if defined(BT458)
#define CUR_COL         4
#define CUR_HOT         (VA_BPP + 1)
#define CUR_BG          (VA_BPP + 2)
#define CUR_FG          (VA_BPP + 3)
#else
#define CUR_COL         8
#define CUR_HOT         (VA_BPP + VA_BPP + 1)
#define CUR_BG          (VA_BPP + VA_BPP + 2)
#define CUR_FG          (VA_BPP + VA_BPP + 3)
#endif

#define RAM_SIZE        (1u << 16)                      /* FIFO RAM */

#define FCCCSR_PACK     0x0100                          /* byte/word */
#define FCCCSR_WR       0x5F83
#define FCCCSR_V_MODE   9
#define FCCCSR_M_MODE   0x3
#define GET_MODE(x)     ((x >> FCCCSR_V_MODE) & FCCCSR_M_MODE)

#define MODE_HALT       0
#define MODE_DL         1
#define MODE_BTP        2
#define MODE_PTB        3

/* Debugging Bitmaps */

#define DBG_FCC         0x0001                          /* fifo compression chip activity */
#define DBG_CURSOR      0x0002                          /* Cursor content, function and visibility activity */

/* FIFO compression chip registers */

#define FCC_CCSR        0x0                             /* colour board CSR */
#define FCC_ICSR        0x1                             /* interrupt CSR */
#define FCC_FCSR        0x2                             /* FIFO CSR */
#define FCC_FWU         0x3                             /* FIFO words used */
#define FCC_FT          0x4                             /* FIFO threshold */
#define FCC_RSVD        0x5                             /* reserved */
#define FCC_PUT         0x6                             /* FIFO put pointer */
#define FCC_GET         0x7                             /* FIFO get pointer */
#define FCC_DIAG        0x8                             /* diag */
#define FCC_CMPA        0x9                             /* CMPA */
#define FCC_CMPB        0xA                             /* CMPB */
#define FCC_CMPC        0xB                             /* CPMC - PLA address */
#define FCC_MAXREG      0xB

extern int32 int_req[IPL_HLVL];
extern int32 tmxr_poll;                                 /* calibrated delay */
extern int32 fault_PC;
extern int32 trpirq;

uint16 va_ram[RAM_SIZE];

uint32 va_fcc_csr = 0;
uint32 va_fcc_int = 0;
uint32 va_fcc_fcsr = 0;
uint32 va_fcc_data = 0;
uint32 va_fcc_sc = 0;
int32 va_fcc_fifo_sz = 0;                               /* data size */
int32 va_fcc_fifo_th = 0;                               /* threshold */
uint32 va_fcc_fifo_wp = 0;                              /* write pointer */
uint32 va_fcc_fifo_rp = 0;                              /* read pointer */

uint32 va_ccmd = 0;                                     /* cursor command reg */
uint32 va_xpos = 0;                                     /* cursor x position */
uint32 va_ypos = 0;                                     /* cursor y position */
uint32 va_xmin1 = 0;                                    /* region 1 left edge */
uint32 va_xmax1 = 0;                                    /* region 1 right edge */
uint32 va_ymin1 = 0;                                    /* region 1 top edge */
uint32 va_ymax1 = 0;                                    /* region 1 bottom edge */
uint32 va_xmin2 = 0;                                    /* region 2 left edge */
uint32 va_xmax2 = 0;                                    /* region 2 right edge */
uint32 va_ymin2 = 0;                                    /* region 2 top edge */
uint32 va_ymax2 = 0;                                    /* region 2 bottom edge */
uint16 va_cur[32];                                      /* cursor image data */
uint32 va_cur_p = 0;                                    /* cursor image pointer */

uint32 va_cur_x = 0;                                    /* Last cursor X-position */
uint32 va_cur_y = 0;                                    /* Last cursor Y-position */
uint32 va_cur_f = 0;                                    /* Last cursor function */
t_bool va_cur_v = FALSE;                                /* Last cursor visible */

t_bool va_active = FALSE;
t_bool va_updated[2048];
t_bool va_input_captured = FALSE;                       /* Mouse and Keyboard input captured in video window */
uint32 *va_buf = NULL;                                  /* Video memory */
uint32 *va_lines = NULL;                                /* Video Display Lines */
#if defined(BT458)
uint32 va_palette[VA_BPP + CUR_COL];                    /* Colour palette (screen, cursor)*/
uint32 va_cmap2[VA_BPP + CUR_COL];                      /* Colour palette (screen, cursor)*/
#else
uint32 va_palette[VA_BPP + VA_BPP + CUR_COL];           /* Colour palette (fg, bg, cursor)*/
#endif

uint32 va_dla = 0;                                      /* display list addr */
int32 va_yoff = 0;
int32 va_dpln = 0;
uint32 va_white = 0;
uint32 va_black = 0;

uint32 va_bt458_addr = 0;
uint32 va_cmap_p = 0;
uint32 va_cmap[3];

const char *va_fcc_rgd[] = {                            /* FIFO compression chip registers */
    "Colour Board CSR",
    "Interrupt CSR",
    "FIFO CSR",
    "FIFO Words Used",
    "FIFO Threshold",
    "Reserved",
    "FIFO Put Pointer",
    "FIFO Get Pointer",
    "Diag",
    "CMPA",
    "CMPB",
    "CMPC - PLA Address"
    };

const char *va_dac_rgd[] = {                            /* D/A converter registers */
    "Active Region Colour 0",
    "Active Region Colour 1",
    "Active Region Colour 2",
    "Active Region Colour 3",
    "Active Region Colour 4",
    "Active Region Colour 5",
    "Active Region Colour 6",
    "Active Region Colour 7",
    "Active Region Colour 8",
    "Active Region Colour 9",
    "Active Region Colour 10",
    "Active Region Colour 11",
    "Active Region Colour 12",
    "Active Region Colour 13",
    "Active Region Colour 14",
    "Active Region Colour 15",
    "Background Colour 0",
    "Background Colour 1",
    "Background Colour 2",
    "Background Colour 3",
    "Background Colour 4",
    "Background Colour 5",
    "Background Colour 6",
    "Background Colour 7",
    "Background Colour 8",
    "Background Colour 9",
    "Background Colour 10",
    "Background Colour 11",
    "Background Colour 12",
    "Background Colour 13",
    "Background Colour 14",
    "Background Colour 15",
    "Reserved",
    "Active Cursor Colour A",
    "Active Cursor Colour B",
    "Active Cursor Colour C",
    "Reserved",
    "Background Cursor Colour A",
    "Background Cursor Colour B",
    "Background Cursor Colour C",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Mode",
    "Delay Adjust - Sync",
    "Delay Adjust - Blank",
    "Delay Adjust - Active Region"
    };

const char *va_ccr_rgd[] = {                            /* cusror chip registers */
    "Command Register",
    "X Position",
    "Y Position",
    "X Minimum 1",
    "X Maximum 1",
    "Y Minimum 1",
    "Y Maximum 1",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "X Minimum 2",
    "X Maximum 2",
    "Y Minimum 2",
    "Y Maximum 2",
    "Cursor Bitmap"
    };

t_stat va_svc (UNIT *uptr);
t_stat va_dmasvc (UNIT *uptr);
t_stat va_reset (DEVICE *dptr);
t_stat va_attach (UNIT *uptr, CONST char *cptr);
t_stat va_detach (UNIT *uptr);
t_stat va_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat va_set_capture (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat va_show_capture (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat va_set_yoff (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat va_show_yoff (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat va_set_dpln (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat va_show_dpln (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat va_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *va_description (DEVICE *dptr);
void va_dlist (void);

extern int32 eval_int (void);

/* GPX data structures

   va_dev       GPX device descriptor
   va_unit      GPX unit list
   va_reg       GPX register list
   va_mod       GPX modifier list
*/

DIB va_dib = {
    VA_ROM_INDEX, BOOT_CODE_ARRAY, BOOT_CODE_SIZE
    };

UNIT va_unit[] = {
    { UDATA (&va_svc, UNIT_IDLE, 0) },
    { UDATA (&va_dmasvc, UNIT_IDLE+UNIT_DIS, 0) },
    };

REG va_reg[] = {
    { HRDATAD (AADCT, va_adp[ADP_ADCT], 16, "address counter") },
    { HRDATAD (AREQ,  va_adp[ADP_REQ],  16, "request enable") },
    { HRDATAD (AINT,  va_adp[ADP_INT],  16, "interrupt enable") },
    { HRDATAD (ASTAT, va_adp[ADP_STAT], 16, "status") },
    { HRDATAD (AMDE,  va_adp[ADP_MDE],  16, "mode") },
    { NULL }
    };

MTAB va_mod[] = {
    { MTAB_XTD|MTAB_VDV, 1, NULL, "ENABLE",
        &va_set_enable, NULL, NULL, "Enable GPX" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLE",
        &va_set_enable, NULL, NULL, "Disable GPX" },
    { MTAB_XTD|MTAB_VDV, TRUE, NULL, "CAPTURE",
        &va_set_capture, &va_show_capture, NULL, "Enable Captured Input Mode" },
    { MTAB_XTD|MTAB_VDV, FALSE, NULL, "NOCAPTURE",
        &va_set_capture, NULL, NULL, "Disable Captured Input Mode" },
    { MTAB_XTD|MTAB_VDV, TRUE, "OSCURSOR", NULL,
        NULL, &va_show_capture, NULL, "Display Input Capture mode" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "VIDEO", NULL,
        NULL, &vid_show_video, NULL, "Display the host system video capabilities" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "OFFSET", "OFFSET=n",
        &va_set_yoff, &va_show_yoff, NULL, "Display the Y offset" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "DPLANE", "DPLANE=n",
        &va_set_dpln, &va_show_dpln, NULL, "Display the debug plane" },
    { 0 }
    };

DEVICE va_dev = {
    "VA", va_unit, va_reg, va_mod,
    2, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &va_reset,
    NULL, NULL, &va_detach,
    &va_dib, DEV_DIS, 0,
    NULL, NULL, NULL, &va_help, NULL, NULL,
    &va_description
    };

void va_fcc_fifo_clr (void)
{
sim_debug (DBG_FCC, &va_dev, "fcc_fifo_clr\n");
va_fcc_fifo_wp = 0;                                     /* reset pointers */
va_fcc_fifo_rp = 0;
va_fcc_fifo_sz = 0;                                     /* empty */
}

void va_fcc_fifo_wr (uint32 val)
{
sim_debug (DBG_FCC, &va_dev, "fcc_fifo_wr: %d, %X (%d) at %08X\n", va_fcc_fifo_wp, val, (va_fcc_fifo_sz + 1), fault_PC);
if (va_fcc_fifo_sz == RAM_SIZE) {                       /* writing full fifo? */
    sim_debug (DBG_FCC, &va_dev, "fcc fifo overflow\n");
    return;                                             /* should not get here */
    }
va_ram[va_fcc_fifo_wp++] = val;                         /* store value */
if (va_fcc_fifo_wp == RAM_SIZE)                         /* pointer wrap? */
    va_fcc_fifo_wp = 0;
va_fcc_fifo_sz++;
}

uint32 va_fcc_fifo_rd (void)
{
uint32 val;

if (va_fcc_fifo_sz == 0) {                              /* reading empty fifo */
    sim_debug (DBG_FCC, &va_dev, "fcc fifo underflow\n");
    return 0;                                           /* should not get here */
    }
val = va_ram[va_fcc_fifo_rp++];                         /* get value */
sim_debug (DBG_FCC, &va_dev, "fcc_fifo_rd: %d, %X (%d) at %08X\n", (va_fcc_fifo_rp - 1), val, va_fcc_fifo_sz, fault_PC);
if (va_fcc_fifo_rp == RAM_SIZE)                         /* pointer wrap? */
    va_fcc_fifo_rp = 0;
va_fcc_fifo_sz--;
if (va_fcc_fifo_sz == 0)
    va_fcc_fifo_clr ();
return val;
}

void va_ccr_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa >> 1) & 0x1F;

if (rg <= 0xF)
    sim_debug (DBG_REG, &va_dev, "ccr_wr: %s, %X at %08X\n", va_ccr_rgd[rg], val, fault_PC);
else
    sim_debug (DBG_REG, &va_dev, "ccr_wr: %X, %X at %08X\n", rg, val, fault_PC);

switch (rg) {

    case 0:                                             /* CUR_CMD */
#if 0
        if ((val & CMD_TEST) == 0) {
            if (val & (CMD_ENRG2|CMD_FORG2|CMD_ENRG1|CMD_FORG1|CMD_FOPB|CMD_FOPA))
                ka_cfgtst = ka_cfgtst & ~(1 << 4);
            else ka_cfgtst = ka_cfgtst | (1 << 4);
            }
        else ka_cfgtst = ka_cfgtst | (1 << 4);
#endif
        if ((va_ccmd ^ val) & CMD_LODSA)                /* toggle sprite display? */
            va_cur_p = 0;                               /* reset array ptr */
        va_ccmd = val;
        break;

    case 1:                                             /* CUR_XPOS */
        va_xpos = val;
        break;

    case 2:                                             /* CUR_YPOS */
        va_ypos = val;
        break;

    case 3:                                             /* CUR_XMIN_1 */
        va_xmin1 = val;
        break;

    case 4:                                             /* CUR_XMAX_1 */
        va_xmax1 = val;
        break;

    case 5:                                             /* CUR_YMIN_1 */
        va_ymin1 = val;
        break;

    case 6:                                             /* CUR_YMAX_1 */
        va_ymax1 = val;
        break;

    case 11:                                            /* CUR_XMIN_2 */
        va_xmin2 = val;
        break;

    case 12:                                            /* CUR_XMAX_2 */
        va_xmax2 = val;
        break;

    case 13:                                            /* CUR_YMIN_2 */
        va_ymin2 = val;
        break;

    case 14:                                            /* CUR_YMAX_2 */
        va_ymax2 = val;
        break;

    case 15:                                            /* CUR_LOAD */
        va_cur[va_cur_p++] = val;
        if (va_cur_p == 32)
            va_cur_p--;
        break;
    }
}

#if defined(BT458)
int32 va_dac_rd (int32 pa)
{
uint32 rg = (pa >> 1) & 0x3F;
uint32 data = 0;

switch (rg) {
    case 0:                                             /* address */
        data = va_bt458_addr;
        break;

    case 1:                                             /* colour map */
        if (va_cmap_p == 0) {
            va_cmap[0] = va_cmap2[va_bt458_addr] & 0xFF;
            va_cmap[1] = (va_cmap2[va_bt458_addr] >> 8) & 0xFF;
            va_cmap[2] = (va_cmap2[va_bt458_addr] >> 16) & 0xFF;
            }
        data = va_cmap[va_cmap_p++];
        if (va_cmap_p == 3) {
            va_cmap_p = 0;
            va_bt458_addr++;
            va_bt458_addr = va_bt458_addr & 0xFF;
            }
        break;

    case 2:                                             /* control */
        va_bt458_addr++;
        va_bt458_addr = va_bt458_addr & 0x7;
        break;

    case 3:                                             /* overlay map */
        if (va_cmap_p == 0) {
            va_cmap[0] = va_cmap2[VA_BPP + va_bt458_addr] & 0xFF;
            va_cmap[1] = (va_cmap2[VA_BPP + va_bt458_addr] >> 8) & 0xFF;
            va_cmap[2] = (va_cmap2[VA_BPP + va_bt458_addr] >> 16) & 0xFF;
            }
        data = va_cmap[va_cmap_p++];
        if (va_cmap_p == 3) {
            va_cmap_p = 0;
            va_bt458_addr++;
            va_bt458_addr = va_bt458_addr & 0x7;
            }
        break;
        }

return data;
}

void va_dac_wr (int32 pa, int32 val, int32 lnt)
{
uint32 rg = (pa >> 1) & 0x3F;
int32 i;

switch (rg) {
    case 0:                                             /* address */
        va_bt458_addr = val & 0xFF;
        va_cmap_p = 0;
        break;

    case 1:                                             /* colour map */
        va_cmap[va_cmap_p++] = val;
        if (va_cmap_p == 3) {
            va_cmap_p = 0;
            va_palette[va_bt458_addr] = vid_map_rgb (va_cmap[0], va_cmap[1], va_cmap[2]);
            va_cmap2[va_bt458_addr] = va_cmap[0];
            va_cmap2[va_bt458_addr] |= (va_cmap[1] << 8);
            va_cmap2[va_bt458_addr] |= (va_cmap[2] << 16);
            for (i = 0; i < VA_YSIZE; i++)
                va_updated[i] = TRUE;
            va_bt458_addr++;
            va_bt458_addr = va_bt458_addr & 0xFF;
            }
        break;

    case 2:                                             /* control */
        va_bt458_addr++;
        va_bt458_addr = va_bt458_addr & 0x7;
        break;

    case 3:                                             /* overlay map */
        va_cmap[va_cmap_p++] = val;
        if (va_cmap_p == 3) {
            va_cmap_p = 0;
            va_palette[VA_BPP + va_bt458_addr] = vid_map_rgb (va_cmap[0], va_cmap[1], va_cmap[2]);
            va_cmap2[VA_BPP + va_bt458_addr] = va_cmap[0];
            va_cmap2[VA_BPP + va_bt458_addr] |= (va_cmap[1] << 8);
            va_cmap2[VA_BPP + va_bt458_addr] |= (va_cmap[2] << 16);
            for (i = 0; i < VA_YSIZE; i++)
                va_updated[i] = TRUE;
            va_bt458_addr++;
            va_bt458_addr = va_bt458_addr & 0x7;
            }
        break;
        }
}
#else
int32 va_dac_rd (int32 pa)
{
uint32 rg = (pa >> 1) & 0x3F;
uint32 data = 0;

if (rg <= 0x33)
    sim_debug (DBG_REG, &va_dev, "dac_rd: %s, %X at %08X\n", va_dac_rgd[rg], data, fault_PC);
else
    sim_debug (DBG_REG, &va_dev, "dac_rd: %X, %X at %08X\n", rg, data, fault_PC);

return data;
}

void va_dac_wr (int32 pa, int32 val, int32 lnt)
{
uint32 rg = (pa >> 1) & 0x3F;
int32 red, grn, blu, i;

if (rg <= 0x33)
    sim_debug (DBG_REG, &va_dev, "dac_wr: %s, %X at %08X\n", va_dac_rgd[rg], val, fault_PC);
else
    sim_debug (DBG_REG, &va_dev, "dac_wr: %X, %X at %08X\n", rg, val, fault_PC);

if (rg < 0x28) {                                        /* active colour map? */
    rg = rg & 0xF;
    red = val & 0xF;
    blu = (val >> 4) & 0xF;
    grn = (val >> 8) & 0xF;
    red = red | (red << 4);                             /* 12 bit to 24 bit */
    blu = blu | (blu << 4);
    grn = grn | (grn << 4);
    va_palette[rg] = vid_map_rgb (red, grn, blu);
    for (i = 0; i < VA_YSIZE; i++)
        va_updated[i] = TRUE;
    }
}
#endif

int32 va_fcc_rd (int32 pa)
{
uint32 rg = (pa >> 1) & 0xF;
uint32 data = 0;

switch (rg) {
    case FCC_CCSR:                                      /* colour board CSR */
        data = va_fcc_csr;
        if (va_adp[ADP_REQ] & va_adp[ADP_STAT])
            data = data | 0x2000;                       /* Adder requesting I/O */
        break;

    case FCC_ICSR:                                      /* interrupt CSR */
        data = va_fcc_int;
        break;

    case FCC_FCSR:                                      /* FIFO CSR */
        data = va_fcc_fcsr;
        if (va_fcc_fifo_sz == RAM_SIZE)
            data = data | 0x8000;                       /* FIFO full */
        else if (va_fcc_fifo_sz == 0)
            data = data | 0x80;                         /* FIFO empty */
        else if (va_fcc_fifo_sz < 0)
            data = data | 0x4;                          /* FIFO data is wrapping around */
        switch (GET_MODE (va_fcc_csr)) {
            case MODE_BTP:
                if (va_fcc_fifo_wp == va_fcc_fifo_th)
                    data = data | 0x4000;               /* FIFO pointers at threshold */
                break;
            case MODE_PTB:
            case MODE_DL:
                if (va_fcc_fifo_rp == va_fcc_fifo_th)
                    data = data | 0x4000;               /* FIFO pointers at threshold */
                break;
                }
        break;

    case FCC_FWU:                                       /* FIFO words used */
        if (va_fcc_fifo_sz < 0)
            data = 0xC000 - (va_fcc_fifo_rp - va_fcc_fifo_wp);
        else
            data = va_fcc_fifo_sz;
        break;

    case FCC_FT:                                        /* threshold */
        data = va_fcc_fifo_th;
        break;

    case FCC_PUT:                                       /* put pointer */
        data = va_fcc_fifo_wp;
        break;

    case FCC_GET:                                       /* get pointer */
        data = va_fcc_fifo_rp;
        break;

    default:
        data = 0;
        break;
        }

if (rg <= FCC_MAXREG)
    sim_debug (DBG_REG, &va_dev, "fcc_rd: %s, %X at %08X\n", va_fcc_rgd[rg], data, fault_PC);
else
    sim_debug (DBG_REG, &va_dev, "fcc_rd: %X, %X at %08X\n", rg, data, fault_PC);

return data;
}

void va_fcc_wr (int32 pa, int32 val, int32 lnt)
{
uint32 rg = (pa >> 1) & 0xF;

if (rg <= FCC_MAXREG)
    sim_debug (DBG_REG, &va_dev, "fcc_wr: %s, %X at %08X\n", va_fcc_rgd[rg], val, fault_PC);
else
    sim_debug (DBG_REG, &va_dev, "fcc_wr: %X, %X at %08X\n", rg, val, fault_PC);

switch (rg) {
    case FCC_CCSR:                                      /* colour board CSR */
        if (GET_MODE (val) != MODE_DL)
            va_dla = 0;
        va_fcc_csr = (va_fcc_csr & ~FCCCSR_WR);
        va_fcc_csr = va_fcc_csr | (val & FCCCSR_WR);
        if (val & 0x4000) {                             /* flush FIFO? */
#if 0
            va_fcc_fifo_wp = 0;
            va_fcc_fifo_rp = 0;
            va_fcc_fifo_sz = 0;
            if (va_fcc_fifo_sz < va_fcc_fifo_th)
#endif
                va_fcc_int |= 0x80;                     /* interrupt */
            }
        if (GET_MODE (va_fcc_csr) != MODE_HALT)
            va_dmasvc (&va_unit[1]);
        break;

    case FCC_ICSR:                                      /* interrupt CSR */
        if ((val & 0x42) == 0)
            CLR_INT (VC2);
        else if ((va_fcc_int & 0x3) == 0x1)             /* pixel interrupt */
            SET_INT (VC2);
        else if ((va_fcc_int & 0xC0) == 0x80)           /* threshold interrupt */
            SET_INT (VC2);
        va_fcc_int = va_fcc_int & ~(val & 0x81);        /* W1C */
        va_fcc_int = va_fcc_int & ~(0x42);              /* WR */
        va_fcc_int = va_fcc_int | (val & 0x42);
        break;

    case FCC_FCSR:                                      /* FIFO CSR */
        va_fcc_fcsr = val & 0xFFFF;
        break;

    case FCC_FWU:                                       /* FIFO words used */
        break;

    case FCC_FT:                                        /* threshold */
        va_fcc_fifo_th = val;
        break;

    case FCC_PUT:                                       /* put pointer */
        va_fcc_fifo_wp = val;
        va_fcc_fifo_sz = va_fcc_fifo_wp - va_fcc_fifo_rp;
        sim_debug (DBG_FCC, &va_dev, "Put pointer wr: mode = %d\n", GET_MODE (va_fcc_csr));
        if ((GET_MODE (va_fcc_csr) != MODE_HALT) && (va_fcc_fifo_sz > 0))
            va_dmasvc (&va_unit[1]);
        break;

    case FCC_GET:                                       /* get pointer */
        va_fcc_fifo_rp = val;
        va_fcc_fifo_sz = va_fcc_fifo_wp - va_fcc_fifo_rp;
        sim_debug (DBG_FCC, &va_dev, "Get pointer wr: mode = %d\n", GET_MODE (va_fcc_csr));
        if ((GET_MODE (va_fcc_csr) != MODE_HALT) && (va_fcc_fifo_sz > 0))
            va_dmasvc (&va_unit[1]);
        break;

    default:
        break;
        }
}

int32 va_rd (int32 pa)
{
int32 rg = (pa >> 1) & 0x7FFF;
int32 data;

if (rg >= VA_FFW_OF) {
    rg = rg & 0x3FFF;
    rg = rg | ((va_fcc_csr & 0x3) << 14);               /* add A15,A16 */
    data = va_ram[rg];
    sim_debug (DBG_REG, &va_dev, "ffw_rd: %X, %X at %08X\n", rg, data, fault_PC);
    return data;
    }
if (rg >= VA_CBR_OF) {
#if defined(BT458)
    data = (VA_PLANES << 4);                            /* Brooktree 4 or 8-plane */
#else
    data = 0x00F0;                                      /* original 4-plane */
#endif
    return data;
    }
if (rg >= VA_CCR_OF) {
    data = 0;
    sim_debug (DBG_REG, &va_dev, "ccr_rd: %X, %X at %08X\n", pa, data, fault_PC);
    return data;
    }
if (rg >= VA_DAC_OF) {
    data = va_dac_rd (pa);
    return data;
    }
if (rg >= VA_FCC_OF) {
    data = va_fcc_rd (pa);
    return data;
    }
if (rg >= VA_ADP_OF) {                                  /* address processor */
    data = va_adp_rd (rg);
    SET_IRQL;
    return data;
    }
return 0;
}

void va_wr (int32 pa, int32 val, int32 lnt)
{
int32 nval;
int32 rg = (pa >> 1) & 0x7FFF;
int32 sc;

if (rg >= VA_FFW_OF) {
    rg = rg & 0x3FFF;
    rg = rg | ((va_fcc_csr & 0x3) << 14);               /* add A15,A16 */
    if (lnt < L_WORD) {
        int32 t = va_ram[rg];
        sc = (pa & 1) << 3;
        nval = ((val & BMASK) << sc) | (t & ~(BMASK << sc));
        }
    else nval = val;
    va_ram[rg] = nval;
    sim_debug (DBG_REG, &va_dev, "ffw_wr: %X, %X at %08X\n", rg, val, fault_PC);
    return;
    }
if (rg >= VA_CBR_OF) {
    sim_debug (DBG_REG, &va_dev, "cbr_wr: %X, %X at %08X\n", pa, val, fault_PC);
    return;
    }
if (rg >= VA_CCR_OF) {
    va_ccr_wr (pa, val, lnt);
    return;
    }
if (rg >= VA_DAC_OF) {
    va_dac_wr (pa, val, lnt);
    return;
    }
if (rg >= VA_FCC_OF) {
    va_fcc_wr (pa, val, lnt);
    return;
    }
if (rg >= VA_ADP_OF) {                                  /* address processor */
    va_adp_wr (rg, val);
    SET_IRQL;
    return;
    }
}

void va_dlist ()
{
t_bool nodec = FALSE;
uint32 inst, saved_inst;
int32 val;

saved_inst = (va_dla >> 16) & 0xFFFF;                   /* get saved instruction */
va_dla = va_dla & 0x0000FFFF;                           /* get saved address */
if ((va_dla < VA_TMP_OF) || (saved_inst & 0x2000)) {
    if (va_fcc_fifo_sz == 0)
        return;
    inst = va_fcc_fifo_rd ();
    }
else
    inst = va_ram[va_dla++];
if (saved_inst & 0x1000)                                /* saved decode flag */
    nodec = TRUE;

sim_debug (DBG_ROP, &va_dev, "Begin display list\n");
sim_debug (DBG_ROP, &va_dev, "DLIST: %04X = %04X ", (va_dla == 0) ? 0 : (va_dla - 1), inst);
for (;;) {
    if (nodec) {                                        /* decode disabled? */
        sim_debug (DBG_ROP, &va_dev, "(data - full word)\n");
        va_adp_wr (ADP_ADCT, inst);                     /* write to adder (full word) */
        nodec = FALSE;                                  /* enable decode */
        }
    else if (inst & 0x8000) {                           /* command? */
        if (inst & 0x800) {
            if (va_dla < VA_TMP_OF) {
                if (va_fcc_fifo_sz == 0)
                    break;
                va_dla = va_fcc_fifo_rd ();
                }
            else
                va_dla = va_ram[va_dla];
            va_dla = va_dla & 0x1FFF;
            if (va_dla >= VA_TMP_OF)
                va_dla = va_dla | 0xC000;
            sim_debug (DBG_ROP, &va_dev, "(JMPTD @ %X)\n", va_dla);
            }
        else {
            sim_debug (DBG_ROP, &va_dev, "(command");
            if (inst & 0x4000)
                sim_debug (DBG_ROP, &va_dev, ", write disable");
            if (inst & 0x2000)                              /* read fifo? */
                sim_debug (DBG_ROP, &va_dev, ", read fifo");
            if (inst & 0x1000)                              /* decode disable? */
                sim_debug (DBG_ROP, &va_dev, ", decode disable");
            sim_debug (DBG_ROP, &va_dev, ")\n");
            if ((inst & 0x4000) == 0)                       /* write enabled? */
                va_adp_wr (ADP_ADCT, (0x8000 | (inst & 0xFFF))); /* update counter */
            if (inst & 0x1000)                              /* decode disable? */
                nodec = TRUE;
            if (inst & 0x2000) {                            /* read fifo? */
                if (va_fcc_fifo_sz == 0) {
                    va_dla = va_dla | (inst << 16);         /* save current instruction */
                    break;
                    }
                inst = va_fcc_fifo_rd ();
                sim_debug (DBG_ROP, &va_dev, "DLIST: fifo = %04X\n", inst);
                continue;
                }
            }
        }
    else if (inst & 0x4000) {                           /* write disable? */
        if (inst & 0x2000) {                            /* read fifo? */
            val = inst & 0x1FFF;
            val = 0x2000 - val;                         /* FIXME: # words is negative? */
            sim_debug (DBG_ROP, &va_dev, "(PTB %d words)\n", val);
            for (; val > 0; val--) {
                if (va_fcc_fifo_sz == 0)
                    break;
                inst = va_fcc_fifo_rd ();
                va_adp_wr (ADP_IDD, inst);
                }
            va_dla = 0;                                 /* always returns to FIFO */
            }
        else {
           va_dla = inst & 0x1FFF;
           if (va_dla >= VA_TMP_OF)
               va_dla = va_dla | 0xC000;
           sim_debug (DBG_ROP, &va_dev, "(JMPT @ %X)\n", va_dla);
           }
        }
    else {
        sim_debug (DBG_ROP, &va_dev, "(data)\n");
        va_adp_wr (ADP_ADCT, (inst & 0x3FFF));          /* write to adder */
        }
    if (va_dla < VA_TMP_OF) {
        if (va_fcc_fifo_sz == 0)
            break;
        inst = va_fcc_fifo_rd ();
        }
    else
        inst = va_ram[va_dla++];
    sim_debug (DBG_ROP, &va_dev, "DLIST: %04X = %04X ", (va_dla == 0) ? 0 : (va_dla - 1), inst);
    }
sim_debug (DBG_ROP, &va_dev, "Display list complete\n");
}

void va_setint (int32 src)
{
switch (src) {
    case INT_FCC:                                       /* DMA IRQ */
        SET_INT (VC2);
        break;
    case INT_ADP:                                       /* ADP IRQ */
        SET_INT (VC1);
        break;
        }
}

static SIM_INLINE void va_invalidate (uint32 y1, uint32 y2)
{
uint32 ln;

for (ln = y1; ln < y2; ln++)
    va_updated[ln] = TRUE;                              /* flag as updated */
}

t_stat va_svc (UNIT *uptr)
{
SIM_MOUSE_EVENT mev;
SIM_KEY_EVENT kev;
t_bool updated = FALSE;                                 /* flag for refresh */
uint32 lines;
uint32 ln, col, off;
uint16 *plna, *plnb;
uint16 bita, bitb;
uint32 c;

va_adp_svc (uptr);

if (va_cur_v != CUR_V) {                                /* visibility changed? */
    if (CUR_V)                                          /* visible? */
        va_invalidate (CUR_Y, (CUR_Y + 16));            /* invalidate new pos */
    else
        va_invalidate (va_cur_y, (va_cur_y + 16));      /* invalidate old pos */
    }
else if (va_cur_y != CUR_Y) {                           /* moved (Y)? */
    va_invalidate (CUR_Y, (CUR_Y + 16));                /* invalidate new pos */
    va_invalidate (va_cur_y, (va_cur_y + 16));          /* invalidate old pos */
    }
else if (va_cur_x != CUR_X) {                           /* moved (X)? */
    va_invalidate (CUR_Y, (CUR_Y + 16));                /* invalidate new pos */
    }

va_cur_x = CUR_X;                                       /* store cursor data */
va_cur_y = CUR_Y;
va_cur_v = CUR_V;
va_cur_f = CUR_F;

if (vid_poll_kb (&kev) == SCPE_OK)                      /* poll keyboard */
    lk_event (&kev);                                    /* push event */
if (vid_poll_mouse (&mev) == SCPE_OK)                   /* poll mouse */
    vs_event (&mev);                                    /* push event */

lines = 0;
for (ln = 0; ln < VA_YSIZE; ln++) {
    if (va_updated[ln + va_yoff]) {                     /* line updated? */
        off = (ln + va_yoff) * VA_XSIZE;                /* get video buf offet */
        if (va_dpln > 0) {
            for (col = 0; col < VA_XSIZE; col++)
                va_lines[ln*VA_XSIZE + col] = (va_buf[off + col] & va_dpln) ? va_white : va_black;
            }
        else {
            for (col = 0; col < VA_XSIZE; col++)
                va_lines[ln*VA_XSIZE + col] = va_palette[va_buf[off + col] & VA_PLANE_MASK];
            }

        if (CUR_V &&                                    /* cursor visible && need to draw cursor? */
            (va_input_captured || (va_dev.dctrl & DBG_CURSOR))) {
            if ((ln >= CUR_Y) && (ln < (CUR_Y + 16))) { /* cursor on this line? */
                plna = &va_cur[(CUR_PLNA + ln - CUR_Y)];/* get plane A base */
                plnb = &va_cur[(CUR_PLNB + ln - CUR_Y)];/* get plane B base */
                for (col = 0; col < 16; col++) {
                    if ((CUR_X + col) >= VA_XSIZE)      /* Part of cursor off screen? */
                        continue;                       /* Skip */
                    if (va_ccmd & CMD_FOPA)             /* force plane A to 1? */
                        bita = 1;
                    else if (va_ccmd & CMD_ENPA)        /* plane A enabled? */
                        bita = (*plna >> col) & 1;
                    else bita = 0;
                    if (va_ccmd & CMD_FOPB)             /* force plane B to 1? */
                        bitb = 1;
                    else if (va_ccmd & CMD_ENPB)        /* plane B enabled? */
                        bitb = (*plnb >> col) & 1;
                    else bitb = 0;
                    if (bita & bitb)
                        va_lines[ln*VA_XSIZE + CUR_X + col] = va_palette[CUR_FG];
                    else if (bita ^ bitb)
                        va_lines[ln*VA_XSIZE + CUR_X + col] = va_palette[CUR_BG];
                    }
                }
            }
        va_updated[ln + va_yoff] = FALSE;               /* set valid */
        if ((ln == (VA_YSIZE-1)) ||                     /* if end of window OR */
            (va_updated[ln+va_yoff+1] == FALSE)) {      /* next is already valid? */
            vid_draw (0, ln-lines, VA_XSIZE, lines+1, va_lines+(ln-lines)*VA_XSIZE); /* update region */
            lines = 0;
            }
        else
            lines++;
        updated = TRUE;
        }
    }

if (updated)                                            /* video updated? */
    vid_refresh ();                                     /* put to screen */

sim_activate (uptr, tmxr_poll);                         /* reactivate */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
return SCPE_OK;
}

t_bool va_fcc_rdn (uint32 *data, uint32 bits)
{
int32 mask = (1u << bits) - 1;
if (va_fcc_sc == 0) {                                   /* need to read FIFO? */
    if (va_fcc_fifo_sz == 0)                            /* empty? */
        return FALSE;                                   /* no more data to read */
    va_fcc_data = va_fcc_fifo_rd ();
    }
*data = (va_fcc_data >> va_fcc_sc) & mask;              /* extract bits */
va_fcc_sc = (va_fcc_sc + bits) & 0xF;
return TRUE;
}

t_bool va_fcc_wrn (uint32 data, uint32 bits)
{
int32 mask = (1u << bits) - 1;
mask = mask << va_fcc_sc;
va_fcc_data = va_fcc_data & ~mask;                      /* clear bits */
va_fcc_data = va_fcc_data | ((data << va_fcc_sc) & mask); /* insert bits */
va_fcc_sc = (va_fcc_sc + bits) & 0xF;
if (va_fcc_sc == 0) {                                   /* need to write FIFO? */
    if (va_fcc_fifo_sz == RAM_SIZE)                     /* full? */
        return FALSE;                                   /* no more space to write */
    va_fcc_fifo_wr (va_fcc_data);
    va_fcc_data = 0;
    }
return TRUE;
}

void va_fcc_decomp (UNIT *uptr)
{
uint32 pix, last_pix, len1, len2, i, sc, temp;

last_pix = 0xFF;
for (;uptr->CMD != 0;) {
    if (!va_fcc_rdn (&pix, 4))                          /* read pixel from FIFO */
        return;
    va_fifo_wr (pix);                                   /* output pixel */
    if ((va_adp[ADP_STAT] & ADPSTAT_ITR) == 0)
        va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
    if (pix == last_pix) {                              /* same as previous? */
        if (!va_fcc_rdn (&len1, 4))                     /* get run length */
            return;
        len2 = 0;
        if (len1 & 0x8) {                               /* deferred? */
            for (i = 0, sc = 0; i < (len1 & 0x7); i++) {
                if (!va_fcc_rdn (&temp, 4))
                    return;
                len2 = len2 | (temp << sc);
                sc = sc + 4;
                }
            }
        else
            len2 = len1 & 0x7;                          /* immediate */
        for (i = 0; i < len2; i++) {
            va_fifo_wr (pix);                           /* output pixel */
            if ((va_adp[ADP_STAT] & ADPSTAT_ITR) == 0)
                va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
            }
        }
    last_pix = pix;
    }
}

void va_fcc_comp (UNIT *uptr)
{
uint32 pix, last_pix, len1, len2, temp;

va_fcc_sc = 0;
va_fcc_data = 0;
if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0) {
    va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
    if (va_adp[ADP_STAT] & ADPSTAT_AC)
        return;
    }
last_pix = va_fifo_rd ();
for (;;) {
    va_fcc_wrn (last_pix, 4);
    if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0) {
        va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
        if (va_adp[ADP_STAT] & ADPSTAT_AC)
            return;
        }
    pix = va_fifo_rd ();
    va_fcc_wrn (pix, 4);
    len1 = 0;
    while (pix == last_pix) {
        if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0) {
            va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
            if (va_adp[ADP_STAT] & ADPSTAT_AC)
                return;
            }
        pix = va_fifo_rd ();
        len1++;
        }
    if (len1 > 0) {
        len1--;
        if (len1 > 0x7) {
            temp = len1;
            for (len2 = 0; temp != 0; len2++)
                temp = temp >> 4;
            len2 = len2 | 0x8;
            va_fcc_wrn (len2, 4);
            for (;len1 != 0;) {
                va_fcc_wrn (len1 & 0xF, 4);
                len1 = len1 >> 4;
                }
            }
        else
            va_fcc_wrn (len1, 4);
        }
    last_pix = pix;
    }
}

int32 debug_flag = 0;

t_stat va_dmasvc (UNIT *uptr)
{
uint16 data;

if (GET_MODE (va_fcc_csr) == MODE_HALT)
    return SCPE_OK;

sim_debug (DBG_FCC, &va_dev, "DMA service\n");
switch (GET_MODE (va_fcc_csr)) {

    case MODE_PTB:
        sim_debug (DBG_FCC, &va_dev, "DMA mode PTB\n");
        if (va_fcc_csr & 0x1800) {                      /* compressed? */
            va_fcc_decomp (uptr);
            }
        else if (va_fcc_csr & FCCCSR_PACK) {
            for (;uptr->CMD != 0;) {
                if ((va_adp[ADP_STAT] & ADPSTAT_ITR) == 0)
                    va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
                if (va_fcc_fifo_sz == 0)
                    break;
                data = va_fcc_fifo_rd ();
                va_fifo_wr (data & BMASK);
                if ((va_adp[ADP_STAT] & ADPSTAT_ITR) == 0)
                    va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
                va_fifo_wr ((data >> 8) & BMASK);
                }
            }
        else {
            for (;uptr->CMD != 0;) {
                if ((va_adp[ADP_STAT] & ADPSTAT_ITR) == 0)
                    va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
                if (va_fcc_fifo_sz == 0)
                    break;
                data = va_fcc_fifo_rd ();
                va_fifo_wr (data);
                }
            }
        va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
        if (va_fcc_fifo_sz < va_fcc_fifo_th)
            va_fcc_int |= 0x80;
        if (uptr->CMD == 0)
            va_fcc_int |= 0x1;
        break;

    case MODE_BTP:
        sim_debug (DBG_FCC, &va_dev, "DMA mode BTP\n");
        if ((va_fcc_csr & 0x1880) == 0x1800) {          /* compressed, not diag? */
            va_fcc_comp (uptr);
            }
        else if (va_fcc_csr & FCCCSR_PACK) {
            for (;;) {
                if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0) {
                    va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
                    if (va_adp[ADP_STAT] & ADPSTAT_AC)
                        break;
                    }
                data = (va_fifo_rd () & BMASK);
                if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0) {
                    va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
                    if (va_adp[ADP_STAT] & ADPSTAT_AC)
                        break;
                    }
                data |= ((va_fifo_rd () & BMASK) << 8);
                va_fcc_fifo_wr (data);
                }
            }
        else {
            for (;;) {
                if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0) {
                    va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
                    if (va_adp[ADP_STAT] & ADPSTAT_AC)
                        break;
                    }
                data = va_fifo_rd ();
                va_fcc_fifo_wr (data);
                }
            }
        if (va_fcc_fifo_sz > va_fcc_fifo_th)
            va_fcc_int |= 0x80;
        if (uptr->CMD == 0)
            va_fcc_int |= 0x1;
        break;

    case MODE_DL:
        sim_debug (DBG_FCC, &va_dev, "DMA mode DL\n");
        va_dlist ();
        if (va_fcc_fifo_sz < va_fcc_fifo_th)
            va_fcc_int |= 0x80;
        break;

    default:
        sim_debug (DBG_FCC, &va_dev, "DMA mode %X\n", GET_MODE(va_fcc_csr));
        break;
        }

sim_debug (DBG_FCC, &va_dev, "DMA service complete\n");
if ((va_fcc_int & 0x3) == 0x3)                          /* pixel interrupt */
    va_setint (INT_FCC);
else if ((va_fcc_int & 0xC0) == 0xC0)                   /* threshold interrupt */
    va_setint (INT_FCC);
return SCPE_OK;
}

t_stat va_reset (DEVICE *dptr)
{
int32 i;
t_stat r;

CLR_INT (VC2);                                          /* clear int req */
sim_cancel (&va_unit[0]);                               /* stop poll */
sim_cancel (&va_unit[1]);
va_adp_reset (dptr);

va_fcc_fifo_clr ();
va_fcc_csr = 0x8000;
va_dla = 0;

va_bt458_addr = 0;
va_cmap_p = 0;

for (i = 0; i < VA_YSIZE; i++)
    va_updated[i] = TRUE;

if (dptr->flags & DEV_DIS) {
    if (va_active) {
        free (va_buf);
        va_buf = NULL;
        free (va_lines);
        va_lines = NULL;
        va_active = FALSE;
        return vid_close ();
        }
    else
        return SCPE_OK;
    }

if (!vid_active)  {
    r = vid_open (dptr, NULL, VA_XSIZE, VA_YSIZE, va_input_captured ? SIM_VID_INPUTCAPTURED : 0);/* display size & capture mode */
    if (r != SCPE_OK)
        return r;
    va_buf = (uint32 *) calloc (VA_BUFSIZE, sizeof (uint32));
    if (va_buf == NULL) {
        vid_close ();
        return SCPE_MEM;
        }
    va_lines = (uint32 *) calloc (VA_XSIZE * VA_YSIZE, sizeof (uint32));
    if (va_lines == NULL) {
        free (va_buf);
        vid_close ();
        return SCPE_MEM;
        }
#if defined (BT458)
    for (i = 0; i < (VA_BPP + CUR_COL); i++)
        va_palette[i] = vid_map_rgb (0x00, 0x00, 0x00); /* black */
    va_palette[VA_BPP - 1] = vid_map_rgb (0xFF, 0xFF, 0xFF); /* white */
    va_palette[CUR_FG] = vid_map_rgb (0xFF, 0xFF, 0xFF);
#else
    for (i = 0; i < (VA_BPP + VA_BPP + CUR_COL); i++)
        va_palette[i] = vid_map_rgb (0x00, 0x00, 0x00); /* black */
    va_palette[CUR_FG] = vid_map_rgb (0xFF, 0xFF, 0xFF);
#endif
    va_black = vid_map_rgb (0x00, 0x00, 0x00);          /* black */
    va_white = vid_map_rgb (0xFF, 0xFF, 0xFF);          /* white */
    sim_printf ("GPX Display Created.  ");
    va_show_capture (stdout, NULL, 0, NULL);
    if (sim_log)
        va_show_capture (sim_log, NULL, 0, NULL);
    sim_printf ("\n");
    va_active = TRUE;
    }
sim_activate_abs (&va_unit[0], tmxr_poll);
return SCPE_OK;
}

t_stat va_detach (UNIT *uptr)
{
if ((va_dev.flags & DEV_DIS) == 0) {
    va_dev.flags |= DEV_DIS;
    va_reset(&va_dev);
    }
return SCPE_OK;
}

t_stat va_set_yoff (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
va_yoff = (int32) get_uint (cptr, 10, 2048, &r);
for (i = 0; i < VA_YSIZE; i++)
    va_updated[i + va_yoff] = TRUE;
return r;
}

t_stat va_show_yoff (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
fprintf (st, "%d", va_yoff);
return SCPE_OK;
}

t_stat va_set_dpln (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
va_dpln = (int32) get_uint (cptr, 10, VA_PLANES, &r);
if (va_dpln > 0) {
    va_dpln = va_dpln - 1;
    va_dpln = (1u << va_dpln);
    }
for (i = 0; i < VA_YSIZE; i++)
    va_updated[i + va_yoff] = TRUE;
return r;
}

t_stat va_show_dpln (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
fprintf (st, "%d", va_dpln);
return SCPE_OK;
}

t_stat va_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
return cpu_set_model (NULL, 0, (val ? "VAXSTATIONGPX" : "MICROVAX"), NULL);
}

t_stat va_set_capture (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (vid_active)
    return sim_messagef (SCPE_ALATT, "Capture Mode Can't be changed with device enabled\n");
va_input_captured = val;
return SCPE_OK;
}

t_stat va_show_capture (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
if (va_input_captured) {
    fprintf (st, "Captured Input Mode, ");
    vid_show_release_key (st, uptr, val, desc);
    }
else
    fprintf (st, "Uncaptured Input Mode");
return SCPE_OK;
}

t_stat va_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "GPX 8-Bit Colour Video Subsystem (%s)\n\n", dptr->name);
fprintf (st, "Use the Control-Right-Shift key combination to regain focus from the simulated\n");
fprintf (st, "video display\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *va_description (DEVICE *dptr)
{
return "GPX Colour Graphics Adapter";
}
