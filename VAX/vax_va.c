/* vax_va.c: QDSS video simulator

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

   va           QDSS (VCB02)

   Related documents:

        EK-104AA-TM - VCB02 Video Subsystem Technical Manual
        MP02083     - VCB02 Field Maintenance Print Set
*/

#if !defined(VAX_620)

#include "vax_defs.h"
#include "sim_video.h"
#include "vax_gpx.h"
#include "vax_2681.h"
#include "vax_lk.h"
#include "vax_vs.h"
#include "vax_vcb02_bin.h"

/* QBus memory space offsets */

#define VA_RAM_OF       0x4000                          /* RAM */
#define VA_ADP_OF       0x6000                          /* address processor */
#define VA_DGA_OF       0x6100                          /* DMA gate array */
#define VA_COM1_OF      0x6200                          /* DUART */
#define VA_COM2_OF      0x6300                          /* memory registers */
#define VA_CSR_OF       0x6400                          /* CSR registers */
#define VA_RED_OF       0x6500                          /* red colour map */
#define VA_BLU_OF       0x6600                          /* blue colour map */
#define VA_GRN_OF       0x6700                          /* green colour map */
#define VA_RSV_OF       0x6800

#define VA_ROMSIZE      (1u << 14)

#define VA_FIFOSIZE     64
#define VA_DGA_FIFOSIZE 64

/* RAM offsets */

#define VA_FFO_OF       0x000                           /* FIFO */
#define VA_TMP_OF       0x040                           /* template RAM */
#define VA_CUR_OF       0x7E0                           /* cursor image */

/* I/O page CSR */

#define CSR_RAM         0x80                            /* 1 = 8KW, 0 = 2KW */
#define CSR_OPT2        0x40                            /* option 2 not present */
#define CSR_OPT1        0x20                            /* option 1 not present */
#define CSR_MBO         0x10                            /* must be one */
#define CSR_FPS         0x04                            /* full page system */
#define CSR_HPS         0x02                            /* half page system */
#define CSR_QPS         0x01                            /* quarter page system */

/* DMA gate array registers */

#define DGA_CSR         0x0                             /* CSR */
#define DGA_ADL         0x1                             /* DMA address  counter 15:00 */
#define DGA_ADH         0x2                             /* DMA address counter 21:16 (write only) */
#define DGA_BCL         0x3                             /* DMA byte counter 15:00 */
#define DGA_BCH         0x4                             /* DMA byte counter 21:16 */
#define DGA_FFO         0x5                             /* FIFO register */
#define DGA_CX          0x6                             /* Cursor X pos (write only) */
#define DGA_CY          0x7                             /* Cursor Y pos (write only) */
#define DGA_INT         0x8                             /* Interrupt register */
#define DGA_MAXREG      0x8

#define CUR_PLNA        VA_CUR_OF                       /* cursor plane A */
#define CUR_PLNB        (VA_CUR_OF + 16)                /* cursor plane B */
#define CUR_FG          255                             /* cursor foreground */
#define CUR_BG          254                             /* cursor background */
#define CUR_V           (va_dga_csr & 0x1)              /* cursor visible */
#define CUR_X           (va_dga_curx)                   /* cursor X */
#define CUR_Y           (va_dga_cury)                   /* cursor Y */
#define CUR_X_OF        232                             /* cursor X offset */
#define CUR_Y_OF        15                              /* cursor Y offset */

#define RAM_SIZE        (1u << 11)                      /* template RAM size */
#define RAM_MASK        (RAM_SIZE - 1)

#define IOLN_QDSS       002

/* DMA gate array registers */

#define DGACSR_PACK     0x0100                          /* byte/word */
#define DGACSR_DE       0x0080
#define DGACSR_WR       0x471F                          /* write mask */
#define DGACSR_V_MODE   9
#define DGACSR_M_MODE   0x3
#define GET_MODE(x)     ((x >> DGACSR_V_MODE) & DGACSR_M_MODE)

#define DGAINT_WR       0x01F0

/* DGA modes */

#define MODE_HALT       0                               /* halted */
#define MODE_DL         1                               /* display list */
#define MODE_BTP        2                               /* bitmap to processor */
#define MODE_PTB        3                               /* processor to bitmap */

/* interrupt sources */

#define INT_DGA         1                               /* DMA gate array */
#define INT_COM         2                               /* UART */

/* Debugging Bitmaps */

#define DBG_DGA         0x0001                          /* DMA gate array activity */
#define DBG_INT         0x0002                          /* interrupt activity */
#define DBG_CURSOR      0x0004                          /* Cursor content, function and visibility activity */

extern int32 int_req[IPL_HLVL];
extern int32 tmxr_poll;                                 /* calibrated delay */
extern int32 fault_PC;
extern int32 trpirq;

uint8 va_red_map[256];                                  /* red colour map */
uint8 va_blu_map[256];                                  /* blue colour map */
uint8 va_grn_map[256];                                  /* green colour map */
uint16 va_ram[RAM_SIZE];                                /* template RAM */

uint32 va_dga_csr = 0;                                  /* control/status */
uint32 va_dga_addr = 0;                                 /* DMA address */
uint32 va_dga_count = 0;                                /* DMA counter */
int32 va_dga_curx = 0;                                  /* cursor X */
int32 va_dga_cury = 0;                                  /* cursor Y */
uint32 va_dga_int = 0;                                  /* interrupt register */
uint32 va_dga_fifo[VA_DGA_FIFOSIZE];                    /* FIFO */
uint32 va_dga_fifo_wp = 0;                              /* write pointer */
uint32 va_dga_fifo_rp = 0;                              /* read pointer */
uint32 va_dga_fifo_sz = 0;                              /* data size */

uint32 va_rdbk = 0;                                     /* video readback */
uint32 va_mcsr = 0;                                     /* memory csr */

int32 va_cur_x = 0;                                     /* last cursor X-position */
int32 va_cur_y = 0;                                     /* last cursor Y-position */
t_bool va_cur_v = FALSE;                                /* last cursor visible */

t_bool va_active = FALSE;
t_bool va_updated[VA_BYSIZE];
t_bool va_input_captured = FALSE;                       /* Mouse and Keyboard input captured in video window */
uint32 *va_buf = NULL;                                  /* Video memory */
uint32 va_addr;                                         /* QDSS Qbus memory window address */
uint32 *va_lines = NULL;                                /* Video Display Lines */
uint32 va_palette[256];                                 /* Colour palette */

uint32 va_dla = 0;                                      /* display list addr */
uint32 va_rom_poll = 0;

/* debug variables */

int32 va_yoff = 0;                                      /* debug Y offset */
int32 va_dpln = 0;                                      /* debug plane */
uint32 va_white = 0;                                    /* white pixel */
uint32 va_black = 0;                                    /* black pixel */

const char *va_dga_rgd[] = {                            /* DMA gate array registers */
    "Control/Status",
    "DMA Address Counter (15:00)",
    "DMA Address Counter (21:16)",
    "DMA Byte Counter (15:00)",
    "DMA Byte Counter (21:16)",
    "FIFO",
    "Cursor X Position",
    "Cursor Y Position",
    "Interrupt Register"
    };

t_stat va_rd (int32 *data, int32 PA, int32 access);
t_stat va_wr (int32 data, int32 PA, int32 access);
t_stat va_svc (UNIT *uptr);
t_stat va_dmasvc (UNIT *uptr);
t_stat va_intsvc (UNIT *uptr);
t_stat va_reset (DEVICE *dptr);
t_stat va_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat va_set_capture (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat va_show_capture (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat va_set_yoff (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat va_show_yoff (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat va_set_dpln (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat va_show_dpln (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat va_show_cmap (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
void va_setint (int32 src);
int32 va_inta (void);
void va_clrint (int32 src);
void va_uart_int (uint32 set);
t_stat va_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *va_description (DEVICE *dptr);
void va_checkint (void);
void va_dlist (void);

/* QDSS data structures

   va_dev       QDSS device descriptor
   va_unit      QDSS unit list
   va_reg       QDSS register list
   va_mod       QDSS modifier list
*/

DIB va_dib = {
    IOBA_AUTO, IOLN_QDSS, &va_rd, &va_wr,
    1, IVCL (QDSS), VEC_AUTO, { &va_inta }
    };

DEBTAB va_debug[] = {
    { "REG",     DBG_REG,            "Register activity" },
    { "FIFO",    DBG_FIFO,           "FIFO activity" },
    { "ADP",     DBG_ADP,            "Address Procesor (Adder) activity" },
    { "VDP",     DBG_VDP,            "Video Processor (Viper) activity" },
    { "ROP",     DBG_ROP,            "Raster operations" },
    { "ROM",     DBG_ROM,            "ROM reads" },
    { "DGA",     DBG_DGA,            "DMA Gate Array activity" },
    { "INT",     DBG_INT,            "Interrupt activity" },
    { "CURSOR",  DBG_CURSOR,         "Cursor content, function and visibility activity"},
    { "VMOUSE",  SIM_VID_DBG_MOUSE,  "Video Mouse" },
    { "VCURSOR", SIM_VID_DBG_CURSOR, "Video Cursor" },
    { "VKEY",    SIM_VID_DBG_KEY,    "Video Key" },
    { "VVIDEO",  SIM_VID_DBG_VIDEO,  "Video Video" },
    { 0 }
    };

UNIT va_unit[] = {
    { UDATA (&va_svc, UNIT_IDLE, 0) },
    { UDATA (&va_dmasvc, UNIT_IDLE+UNIT_DIS, 0) },
    { UDATA (&va_intsvc, UNIT_IDLE+UNIT_DIS, 0) },
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
        &va_set_enable, NULL, NULL, "Enable VCB02 (QDSS)" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLE",
        &va_set_enable, NULL, NULL, "Disable VCB02 (QDSS)" },
    { MTAB_XTD|MTAB_VDV, TRUE, NULL, "CAPTURE",
        &va_set_capture, &va_show_capture, NULL, "Enable Captured Input Mode" },
    { MTAB_XTD|MTAB_VDV, FALSE, NULL, "NOCAPTURE",
        &va_set_capture, NULL, NULL, "Disable Captured Input Mode" },
    { MTAB_XTD|MTAB_VDV, TRUE, "OSCURSOR", NULL,
        NULL, &va_show_capture, NULL, "Display Input Capture mode" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "VIDEO", NULL,
        NULL, &vid_show_video, NULL, "Display the host system video capabilities" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "OFFSET", "OFFSET=n",
        &va_set_yoff, &va_show_yoff, NULL, "Display the debug Y offset" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "DPLANE", "DPLANE=n",
        &va_set_dpln, &va_show_dpln, NULL, "Display the debug plane" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "CMAP", NULL,
        NULL, &va_show_cmap, NULL, "Display the colour map" },
    { 0 }
    };

DEVICE va_dev = {
    "QDSS", va_unit, va_reg, va_mod,
    3, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &va_reset,
    NULL, NULL, NULL,
    &va_dib, DEV_DIS | DEV_QBUS | DEV_DEBUG, 0,
    va_debug, NULL, NULL, &va_help, NULL, NULL,
    &va_description
    };

UART2681 va_uart = {
    &va_uart_int, NULL,
    { { &lk_wr, &lk_rd }, { &vs_wr, &vs_rd } }
    };

/* I/O Register descriptions on page 3-10 */

t_stat va_rd (int32 *data, int32 PA, int32 access)
{
int32 rg = (PA >> 1) & 0x1F;
if (rg == 0) {
    *data = CSR_MBO | CSR_FPS;
    if (RAM_SIZE >= 0x4000)
       *data = *data | CSR_RAM;                              /* 8KW system */
    if (VA_PLANES < 8)
        *data = *data | CSR_OPT2;                             /* option 2 not present */
    }
else *data = 0;
sim_debug (DBG_REG, &va_dev, "va_rd: %d, %X from PC %08X\n", rg, *data, fault_PC);
return SCPE_OK;
}

t_stat va_wr (int32 data, int32 PA, int32 access)
{
int32 rg = (PA >> 1) & 0x1F;
if (rg == 0) {
    if (data > 0)
        sim_activate_abs (&va_unit[0], tmxr_poll);
    else
        sim_cancel (&va_unit[0]);
    va_addr = ((uint32)data) << QDMAWIDTH;
    }
sim_debug (DBG_REG, &va_dev, "va_wr: %d, %X from PC %08X\n", rg, data, fault_PC);
return SCPE_OK;
}

void va_dga_fifo_clr (void)
{
sim_debug (DBG_DGA, &va_dev, "dga_fifo_clr\n");
va_ram[VA_FFO_OF] = 0;                                  /* clear top word */
va_dga_fifo_wp = VA_FFO_OF;                             /* reset pointers */
va_dga_fifo_rp = VA_FFO_OF;
va_dga_fifo_sz = 0;                                     /* empty */
}

void va_dga_fifo_wr (uint32 val)
{
sim_debug (DBG_DGA, &va_dev, "dga_fifo_wr: %d, %X (%d) from PC %08X\n", va_dga_fifo_wp, val, (va_dga_fifo_sz + 1), fault_PC);
#if 0
if (va_dga_fifo_sz == VA_DGA_FIFOSIZE) {                /* writing full fifo? */
    if (va_dga_count > 0) {                             /* DMA in progress? */
        switch (GET_MODE (va_dga_csr)) {

            case MODE_BTP:
                bc = va_dga_count;
                if (bc > (VA_DGA_FIFOSIZE << 1))
                    bc = (VA_DGA_FIFOSIZE << 1);
                wc = bc >> 1;
                r = Map_WriteW (va_dga_addr, bc, &va_ram[VA_FFO_OF]);
                va_dga_fifo_clr ();
                va_dga_count -= bc;
                va_dga_addr += bc;
                break;
                }
        }
    }
if (va_dga_fifo_sz == VA_DGA_FIFOSIZE) {                /* writing full fifo? */
    sim_debug (DBG_DGA, &va_dev, "dga fifo overflow\n");
    return;                                             /* should not get here */
    }
#endif
va_ram[va_dga_fifo_wp++] = val;                         /* store value */
if (va_dga_fifo_wp == VA_DGA_FIFOSIZE)                  /* pointer wrap? */
    va_dga_fifo_wp = VA_FFO_OF;
va_dga_fifo_sz++;
}

uint32 va_dga_fifo_rd (void)
{
uint32 val, wc, bc, r;

if (va_dga_fifo_sz == 0) {                              /* reading empty fifo */
    if (va_dga_count > 0) {                             /* DMA in progress? */
        switch (GET_MODE (va_dga_csr)) {

            case MODE_PTB:
            case MODE_DL:
                bc = va_dga_count;                      /* get remaining DMA size */
                if (bc > (VA_DGA_FIFOSIZE << 1))        /* limit to size of FIFO */
                    bc = (VA_DGA_FIFOSIZE << 1);
                wc = bc >> 1;                           /* bytes -> words */
                r = Map_ReadW (va_dga_addr, bc, &va_ram[VA_FFO_OF]);
                                                        /* do the DMA */
                va_dga_fifo_sz = wc;
                va_dga_fifo_wp = wc;
                va_dga_count -= bc;                     /* decrement DMA size */
                va_dga_addr += bc;                      /* increment DMA addr */
                break;
                }
        }
    }
if (va_dga_fifo_sz == 0) {                              /* reading empty fifo? */
    sim_debug (DBG_DGA, &va_dev, "dga fifo underflow\n");
    return 0;                                           /* should not get here */
    }
val = va_ram[va_dga_fifo_rp++];                         /* get value */
sim_debug (DBG_DGA, &va_dev, "dga_fifo_rd: %d, %X (%d) from PC %08X\n", (va_dga_fifo_rp - 1), val, va_dga_fifo_sz, fault_PC);
if (va_dga_fifo_rp == VA_DGA_FIFOSIZE)                  /* pointer wrap? */
    va_dga_fifo_rp = VA_FFO_OF;
va_dga_fifo_sz--;
if (va_dga_fifo_sz == 0)                                /* now empty? */
    va_dga_fifo_clr ();                                 /* reset pointers */
return val;
}

/* DGA Register descriptions on page 3-121 */

int32 va_dga_rd (int32 pa)
{
int32 rg = (pa >> 1) & 0xFF;
int32 data = 0;

switch (rg) {
    case DGA_CSR:                                       /* CSR */
        data = va_dga_csr;
        if (va_dga_csr & 0x4000) {
            if ((va_dga_count == 0) && (va_dga_fifo_sz == 0))
                data |= 0x8000;
            }
        else {
            if (va_dga_count == 0)
                data |= 0x8000;
            }
        break;

    case DGA_ADL:                                       /* DMA address  counter 15:00 */
        data = va_dga_addr & WMASK;
        break;

    case DGA_ADH:                                       /* DMA address counter 21:16 (write only) */
        break;

    case DGA_BCL:                                       /* DMA byte counter 15:00 */
        data = va_dga_count & WMASK;
        break;

    case DGA_BCH:                                       /* DMA byte counter 21:16 */
        data = ((va_dga_count >> 16) & BMASK) | (va_dga_fifo_sz << 8);
        break;

    case DGA_FFO:                                       /* FIFO register */
        data = va_dga_fifo_rd ();
        break;

    case DGA_CX:                                        /* Cursor X pos (write only) */
        break;

    case DGA_CY:                                        /* Cursor Y pos (write only) */
        break;

    case DGA_INT:                                       /* Interrupt register */
        data = va_dga_int;
        if (data & 0x4000)                              /* IRQ 2 */
            data = data | 0x8;
        else if (data & 0x2000)                         /* IRQ 1 */
            data = data | 0x4;
        else if (data & 0x1000)                         /* DMA IRQ */
            data = data | 0x0;
        if (data & 0x7000)                              /* pending int? */
            data = data | 0x8000;
        break;

    default:
        data = 0;
        sim_debug (DBG_DGA, &va_dev, "dga_rd: %X, %X from PC %08X\n", pa, data, fault_PC);
        }
if (rg <= DGA_MAXREG)
    sim_debug (DBG_DGA, &va_dev, "dga_rd: %s, %X from PC %08X\n", va_dga_rgd[rg], data, fault_PC);

return data;
}

/* DGA Register descriptions on page 3-121 */

void va_dga_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa >> 1) & 0xFF;
uint32 addr = VA_FFO_OF;

if (rg <= DGA_MAXREG)
    sim_debug (DBG_DGA, &va_dev, "dga_wr: %s, %X from PC %08X\n", va_dga_rgd[rg], val, fault_PC);

switch (rg) {
    case DGA_CSR:                                       /* CSR */
        if (val & DGACSR_DE)
            va_dga_csr = va_dga_csr & ~0x00E0;
        if ((val & 0x2) && ((va_dga_csr & 0x2) == 0)) {
            if (GET_MODE (val) != MODE_HALT)
                sim_activate (&va_unit[1], 30);
            }
        va_dga_csr = val & DGACSR_WR;
        va_checkint ();
        break;

    case DGA_ADL:                                       /* DMA address counter 15:00 */
        va_dga_addr = (va_dga_addr & ~WMASK) | (val & WMASK);
        break;

    case DGA_ADH:                                       /* DMA address counter 21:16 */
        va_dga_addr = (va_dga_addr & ~(WMASK << 16)) | ((val & WMASK) << 16);
        break;

    case DGA_BCL:                                       /* DMA byte counter 15:00 */
        va_dga_count = (va_dga_count & ~WMASK) | (val & WMASK);
        break;

    case DGA_BCH:                                       /* DMA byte counter 21:16 */
        va_dga_count = (va_dga_count & ~(WMASK << 16)) | ((val & WMASK) << 16);
        if (va_dga_count > 0)
            sim_activate (&va_unit[1], 30);
        break;

    case DGA_FFO:                                       /* FIFO register */
        va_dga_fifo_wr (val);
        if (GET_MODE (va_dga_csr) == MODE_DL)
            va_dlist ();
        break;

    case DGA_CX:                                        /* Cursor X pos */
        va_dga_curx = val & 0xFFC;                      /* get 2s complement component */
        va_dga_curx = va_dga_curx | 0xFFFFF000;         /* sign extend */
        va_dga_curx = -va_dga_curx;                     /* negate */
        va_dga_curx = va_dga_curx + (val & 0x3);        /* add non-2s complement component */
        va_dga_curx = va_dga_curx - CUR_X_OF;
        break;

    case DGA_CY:                                        /* Cursor Y pos */
        va_dga_cury = val & 0xFFF;                      /* get 2s complement component */
        va_dga_cury = va_dga_cury | 0xFFFFF000;         /* sign extend */
        va_dga_cury = -va_dga_cury;                     /* negate */
        va_dga_cury = va_dga_cury - CUR_Y_OF;
        break;

    case DGA_INT:                                       /* Interrupt register */
        va_dga_int = (va_dga_int & ~DGAINT_WR) | (val & DGAINT_WR);
        break;

    default:
        sim_debug (DBG_DGA, &va_dev, "dga_wr: %X, %X from PC %08X\n", pa, val, fault_PC);
        break;
        }
return;
}

int32 va_mem_rd (int32 pa)
{
int32 rg = (pa >> 1) & 0x7FFF;
int32 data;
UNIT *uptr = &va_dev.units[0];
uint16 *qr = (uint16*) vax_vcb02_bin;

if (rg >= VA_RSV_OF) {
    return 0;
    }
if (rg >= VA_GRN_OF) {                                  /* green colour map */
    rg = rg - VA_GRN_OF;
    data = va_grn_map[rg];
    sim_debug (DBG_REG, &va_dev, "grn_map_rd: %d, %X from PC %08X\n", rg, data, fault_PC);
    return data;
    }
if (rg >= VA_BLU_OF) {                                  /* blue colour map */
    rg = rg - VA_BLU_OF;
    data = va_blu_map[rg];
    sim_debug (DBG_REG, &va_dev, "blu_map_rd: %d, %X from PC %08X\n", rg, data, fault_PC);
    return data;
    }
if (rg >= VA_RED_OF) {                                  /* red colour map */
    rg = rg - VA_RED_OF;
    data = va_red_map[rg];
    sim_debug (DBG_REG, &va_dev, "red_map_rd: %d, %X from PC %08X\n", rg, data, fault_PC);
    return data;
    }
if (rg >= VA_COM2_OF) {                                 /* video readback register */
    data = va_rdbk;
    sim_debug (DBG_REG, &va_dev, "com2_rd: %X, %X from PC %08X\n", pa, data, fault_PC);
    return data;
    }
if (rg >= VA_COM1_OF) {                                 /* DUART */
    rg = rg & 0xF;
    data = ua2681_rd (&va_uart, rg);
    SET_IRQL;
    sim_debug (DBG_REG, &va_dev, "com1_rd: %X, %X from PC %08X\n", pa, data, fault_PC);
    return data;
    }
if (rg >= VA_DGA_OF) {                                  /* DMA gate array */
    data = va_dga_rd (pa);
    SET_IRQL;
    return data;
    }
if (rg >= VA_ADP_OF) {                                  /* address processor */
    rg = rg & 0xFF;
    data = va_adp_rd (rg);
    SET_IRQL;
    return data;
    }
if (rg >= VA_RAM_OF) {                                  /* RAM */
    rg = rg & RAM_MASK;
    data = va_ram[rg];
    sim_debug (DBG_REG, &va_dev, "ram_rd: %X, %X from PC %08X\n", pa, data, fault_PC);
    return data;
    }
rg = rg & 0x1FFF;                                       /* ROM */
data = qr[rg];
va_rom_poll = sim_grtime ();
sim_debug (DBG_ROM, &va_dev, "rom_rd: %X, %X from PC %08X\n", pa, data, fault_PC);
return sim_rom_read_with_delay (data);
}

void va_mem_wr (int32 pa, int32 val, int32 lnt)
{
int32 nval, i;
int32 rg = (pa >> 1) & 0x7FFF;
int32 sc;

if (rg >= VA_RSV_OF) {
    return;
    }

if (rg >= VA_GRN_OF) {                                  /* green colour map */
    rg = rg - VA_GRN_OF;
    va_grn_map[rg] = val & 0xFF;
    va_palette[rg] = vid_map_rgb (va_red_map[rg], va_grn_map[rg], va_blu_map[rg]);
    sim_debug (DBG_REG, &va_dev, "grn_map_wr: %d, %X from PC %08X\n", rg, val, fault_PC);
    for (i = 0; i < VA_YSIZE; i++)
        va_updated[i] = TRUE;
    return;
    }
if (rg >= VA_BLU_OF) {                                  /* blue colour map */
    rg = rg - VA_BLU_OF;
    va_blu_map[rg] = val & 0xFF;
    va_palette[rg] = vid_map_rgb (va_red_map[rg], va_grn_map[rg], va_blu_map[rg]);
    sim_debug (DBG_REG, &va_dev, "blu_map_wr: %d, %X from PC %08X\n", rg, val, fault_PC);
    for (i = 0; i < VA_YSIZE; i++)
        va_updated[i] = TRUE;
    return;
    }
if (rg >= VA_RED_OF) {                                  /* red colour map */
    rg = rg - VA_RED_OF;
    va_red_map[rg] = val & 0xFF;
    va_palette[rg] = vid_map_rgb (va_red_map[rg], va_grn_map[rg], va_blu_map[rg]);
    sim_debug (DBG_REG, &va_dev, "red_map_wr: %d, %X from PC %08X\n", rg, val, fault_PC);
    for (i = 0; i < VA_YSIZE; i++)
        va_updated[i] = TRUE;
    return;
    }
if (rg >= VA_COM2_OF) {                                 /* memory CSR */
    va_mcsr = val;
    sim_debug (DBG_REG, &va_dev, "com2_wr: %X, %X from PC %08X\n", pa, val, fault_PC);
    return;
    }
if (rg >= VA_COM1_OF) {                                 /* DUART */
    rg = rg & 0xF;
    sim_debug (DBG_REG, &va_dev, "com1_wr: %X, %X from PC %08X\n", pa, val, fault_PC);
    ua2681_wr (&va_uart, rg, val);
    SET_IRQL;
    return;
    }
if (rg >= VA_DGA_OF) {                                  /* DMA gate array */
    va_dga_wr (pa, val, lnt);
    SET_IRQL;
    return;
    }
if (rg >= VA_ADP_OF) {                                  /* address processor */
    rg = rg & 0xFF;
    va_adp_wr (rg, val);
    SET_IRQL;
    return;
    }
if (rg >= VA_RAM_OF) {                                  /* RAM */
    rg = rg & RAM_MASK;
    if (lnt < L_WORD) {
        int32 t = va_ram[rg];
        sc = (pa & 1) << 3;
        nval = ((val & BMASK) << sc) | (t & ~(BMASK << sc));
        }
    else nval = val;
    va_ram[rg] = nval;
    sim_debug (DBG_REG, &va_dev, "ram_wr: %X, %X from PC %08X\n", pa, val, fault_PC);
    return;
    }
}

/* Display list processing */

void va_dlist ()
{
t_bool nodec = FALSE;
uint32 inst, saved_inst;
int32 val;

saved_inst = (va_dla >> 16) & 0xFFFF;                   /* get saved instruction */
va_dla = va_dla & 0x0000FFFF;                           /* get saved address */
if ((va_dla < VA_TMP_OF) || (saved_inst & 0x2000)) {
    if (va_dga_fifo_sz == 0)
        return;
    inst = va_dga_fifo_rd ();
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
            if (va_dga_fifo_sz == 0) {
                va_dla = va_dla | (inst << 16);         /* save current instruction */
                break;
                }
            inst = va_dga_fifo_rd ();
            sim_debug (DBG_ROP, &va_dev, "DLIST: fifo = %04X\n", inst);
            continue;
            }
        }
    else if (inst & 0x4000) {                           /* write disable? */
        if (inst & 0x2000) {                            /* read fifo? */
            val = inst & 0x1FFF;
            val = 0x2000 - val;                         /* FIXME: # words is negative? */
            sim_debug (DBG_ROP, &va_dev, "(PTB %d words)\n", val);
            for (; val > 0; val--) {
                inst = va_dga_fifo_rd ();
                va_adp_wr (ADP_IDD, inst);
                }
            va_dla = 0;                                 /* always returns to FIFO */
            }
        else {
           va_dla = inst & 0x1FFF;
           sim_debug (DBG_ROP, &va_dev, "(JMPT @ %X)\n", va_dla);
           }
        }
    else {
        sim_debug (DBG_ROP, &va_dev, "(data)\n");
        va_adp_wr (ADP_ADCT, (inst & 0x3FFF));          /* write to adder */
        }
    if (va_dla < VA_TMP_OF) {
        if (va_dga_fifo_sz == 0)
            break;
        inst = va_dga_fifo_rd ();
        }
    else
        inst = va_ram[va_dla++];
    sim_debug (DBG_ROP, &va_dev, "DLIST: %04X = %04X ", (va_dla == 0) ? 0 : (va_dla - 1), inst);
    }
sim_debug (DBG_ROP, &va_dev, "Display list complete\n");
}

/* Interrupt handling */

void va_setint (int32 src)
{
switch (src) {
    case INT_DGA:                                       /* DMA IRQ */
        va_dga_int = va_dga_int | 0x1000;
        break;
    case INT_ADP:                                       /* IRQ 1 */
        va_dga_int = va_dga_int | 0x2000;
        break;
    case INT_COM:                                       /* IRQ 2 */
        va_dga_int = va_dga_int | 0x4000;
        break;
        }
va_checkint ();
}

void va_clrint (int32 src)
{
switch (src) {
    case INT_DGA:                                       /* DMA IRQ */
        va_dga_int = va_dga_int & ~0x1000;
        break;
    case INT_ADP:                                       /* IRQ 1 */
        va_dga_int = va_dga_int & ~0x2000;
        break;
    case INT_COM:                                       /* IRQ 2 */
        va_dga_int = va_dga_int & ~0x4000;
        break;
        }
va_checkint ();
}

void va_checkint (void)
{
if (va_dga_csr & 0x4) {                                 /* external int en?*/
    if (va_dga_int & 0x4000) {                          /* IRQ 2 */
        sim_debug (DBG_INT, &va_dev, "uart int\n");
        SET_INT (QDSS);
        return;
        }
    if (va_dga_int & 0x2000) {                          /* IRQ 1 */
        sim_debug (DBG_INT, &va_dev, "adp int\n");
        SET_INT (QDSS);
        return;
        }
    }
if ((va_dga_int & 0x1000) && (va_dga_csr & 0x2)) {     /* dma int & enabled? */
    sim_debug (DBG_INT, &va_dev, "dga int\n");
    SET_INT (QDSS);
    return;
    }
CLR_INT (QDSS);
}

int32 va_inta (void)
{
int32 vec = 0;

if (va_dga_int & 0x4000) {                              /* IRQ 2 */
    vec = (va_dga_int & 0x1FC) + 0x8;
    va_dga_int = va_dga_int & ~0x4000;
    }
else if (va_dga_int & 0x2000) {                         /* IRQ 1 */
    vec = (va_dga_int & 0x1FC) + 0x4;
    va_dga_int = va_dga_int & ~0x2000;
    }
else if (va_dga_int & 0x1000) {                         /* DMA IRQ */
    vec = (va_dga_int & 0x1FC) + 0x0;
    va_dga_int = va_dga_int & ~0x1000;
    }
va_checkint ();

sim_debug (DBG_INT, &va_dev, "returning vector: %X\n", vec);
return vec;
}

void va_uart_int (uint32 set)
{
if (set)
    va_setint (INT_COM);
else
    va_clrint (INT_COM);
}

t_stat va_intsvc (UNIT *uptr)
{
SET_INT (QDSS);
return SCPE_OK;
}

static SIM_INLINE void va_invalidate (uint32 y1, uint32 y2)
{
uint32 ln;

for (ln = y1; ln < y2; ln++)
    va_updated[ln] = TRUE;                              /* flag as updated */
}

/* Screen update service routine */

t_stat va_svc (UNIT *uptr)
{
SIM_MOUSE_EVENT mev;
SIM_KEY_EVENT kev;
t_bool updated = FALSE;                                 /* flag for refresh */
uint32 lines;
uint32 col, off, pix;
uint16 *plna, *plnb;
uint16 bita, bitb;
uint32 poll_time;
int32 ln;

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

if (vid_poll_kb (&kev) == SCPE_OK)                      /* poll keyboard */
    lk_event (&kev);                                    /* push event */
if (vid_poll_mouse (&mev) == SCPE_OK)                   /* poll mouse */
    vs_event (&mev);                                    /* push event */

va_rdbk = 0xF;
if (va_mcsr & 0x8) {                                    /* sync enable? */
    if (CUR_X < 0)                                      /* in horizontal front porch? */
        va_rdbk = va_rdbk & ~0x8;                       /* sync detect */
    }

lines = 0;
for (ln = 0; ln < VA_YSIZE; ln++) {
    if ((va_adp[ADP_PSE] > 0) && (ln >= va_adp[ADP_PSE])) {
        sim_debug (DBG_ROP, &va_dev, "pausing at line %d\n", ln);
        va_adpstat (ADPSTAT_PC, 0);
        va_adp[ADP_PSE] = 0;
        if ((CUR_X < 0) || (CUR_X >= VA_XSIZE))         /* cursor off screen? */
            break;                                      /* done */
        if (va_mcsr & 0x10) {                           /* video readback? */
            pix = va_buf[ln*VA_XSIZE + CUR_X] & VA_PLANE_MASK;
            sim_debug (DBG_ROP, &va_dev, "video readback enabled, pix = %x\n", pix);
            if (va_blu_map[pix] < va_red_map[pix])      /* test inverted */
                va_rdbk = va_rdbk & ~0x4;               /* blue > red */
            if (va_grn_map[pix] < va_blu_map[pix])      /* test inverted */
                va_rdbk = va_rdbk & ~0x2;               /* green > blue */
            if (va_red_map[pix] < va_grn_map[pix])      /* test inverted */
                va_rdbk = va_rdbk & ~0x1;               /* red > green */
            sim_debug (DBG_ROP, &va_dev, "video readback value = %x\n", va_rdbk);
            }
        break;
        }
    if (va_updated[ln + va_yoff]) {                     /* line updated? */
        off = (ln + va_yoff) * VA_XSIZE;                /* get video buf offet */
        if (va_dpln > 0) {                              /* debug plane enabled? */
            for (col = 0; col < VA_XSIZE; col++)        /* force monochrome */
                va_lines[ln*VA_XSIZE + col] = (va_buf[off + col] & va_dpln) ? va_white : va_black;
            }
        else {                                          /* normal mode */
            for (col = 0; col < VA_XSIZE; col++)
                va_lines[ln*VA_XSIZE + col] = va_palette[va_buf[off + col] & VA_PLANE_MASK];
            }

        if (CUR_V &&                                    /* cursor visible && need to draw cursor? */
            (va_input_captured || (va_dev.dctrl & DBG_CURSOR))) {
            if ((ln >= CUR_Y) && (ln < (CUR_Y + 16))) { /* cursor on this line? */
                plna = &va_ram[(CUR_PLNA + ln - CUR_Y)];/* get plane A base */
                plnb = &va_ram[(CUR_PLNB + ln - CUR_Y)];/* get plane B base */
                for (col = 0; col < 16; col++) {
                    if ((CUR_X + (int32)col) < 0)       /* Part of cursor off screen? */
                        continue;                       /* Skip */
                    if ((CUR_X + col) >= VA_XSIZE)      /* Part of cursor off screen? */
                        continue;                       /* Skip */
                    bita = (*plna >> col) & 1;
                    bitb = (*plnb >> col) & 1;
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

ua2681_svc (&va_uart);                                  /* service DUART */
poll_time = sim_grtime ();

/*
 * The interval tmxr_poll is too variable for use during the selftest. *
 * Instead we use a more deterministic value when we detect that we    *
 * are running from the VCB02 ROM. To detect this we have to look if   *
 * the ROM has been read recently. We can't use fault_PC as the VCB02  *
 * ROM calls subroutines within the main console ROM                   */

if ((poll_time - va_rom_poll) < 100000)                 /* executing from ROM? */
    sim_activate (uptr, 20000);                         /* yes, use fast poll */
else
    sim_activate (uptr, tmxr_poll);                     /* no, reactivate */
return SCPE_OK;
}

/* DMA service routine */

t_stat va_dmasvc (UNIT *uptr)
{
uint32 wc, bc, i, r;

if (GET_MODE (va_dga_csr) == MODE_HALT)
    return SCPE_OK;

while (va_dga_count > 0) {
    sim_debug (DBG_DGA, &va_dev, "DMA %d bytes left\n", va_dga_count);
    bc = va_dga_count;
    if (bc > (VA_DGA_FIFOSIZE << 1))
        bc = (VA_DGA_FIFOSIZE << 1);
    wc = bc >> 1;
    switch (GET_MODE (va_dga_csr)) {

        case MODE_PTB:
            r = Map_ReadW (va_dga_addr, bc, &va_ram[VA_FFO_OF]);
            va_dga_count -= bc;
            va_dga_addr += bc;
            for (i = 0; i < wc; i++) {
                if (va_dga_csr & DGACSR_PACK) {
                    if ((va_adp[ADP_STAT] & ADPSTAT_ITR) == 0)
                        va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
                    va_fifo_wr (va_ram[VA_FFO_OF + i] & BMASK);
                    if ((va_adp[ADP_STAT] & ADPSTAT_ITR) == 0)
                        va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
                    va_fifo_wr ((va_ram[VA_FFO_OF + i] >> 8) & BMASK);
                    }
                else {
                    if ((va_adp[ADP_STAT] & ADPSTAT_ITR) == 0)
                        va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
                    va_fifo_wr (va_ram[VA_FFO_OF + i]);
                    }
                }
            va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
            break;

        case MODE_BTP:
            va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
            for (i = 0; i < wc; i++) {
                if (va_dga_csr & DGACSR_PACK) {
                    if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0)
                        va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
                    va_ram[VA_FFO_OF + i] = (va_fifo_rd () & BMASK);
                    if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0)
                        va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
                    va_ram[VA_FFO_OF + i] |= ((va_fifo_rd () & BMASK) << 8);
                    }
                else {
                    if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0)
                        va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
                    va_ram[VA_FFO_OF + i] = va_fifo_rd ();
                    }
                }
            r = Map_WriteW (va_dga_addr, bc, &va_ram[VA_FFO_OF]);
            va_dga_count -= bc;
            va_dga_addr += bc;
            break;

        case MODE_DL:
            r = Map_ReadW (va_dga_addr, bc, &va_ram[VA_FFO_OF]);
            va_dga_count -= bc;
            va_dga_addr += bc;
            for (i = 0; i < wc; i++)
                va_dga_fifo_wr (va_ram[VA_FFO_OF + i]);
            va_dlist ();
            break;

        default:
            sim_debug (DBG_DGA, &va_dev, "DMA mode %X\n", GET_MODE(va_dga_csr));
            return SCPE_OK;
            }
    }
va_setint (INT_DGA);
return SCPE_OK;
}

t_stat va_reset (DEVICE *dptr)
{
int32 i;
t_stat r;

CLR_INT (QDSS);                                         /* clear int req */
sim_cancel (&va_unit[0]);                               /* stop poll */
sim_cancel (&va_unit[1]);
ua2681_reset (&va_uart);                                /* reset DUART */
va_adp_reset (dptr);

va_dga_fifo_clr ();
va_mcsr = 0;
va_rdbk = 0;
va_dla = 0;
va_rom_poll = 0;

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
    va_palette[0] = vid_map_rgb (0x00, 0x00, 0x00);     /* black */
    for (i = 1; i < 256; i++)
        va_palette[i] = vid_map_rgb (0xFF, 0xFF, 0xFF); /* white */
    va_black = vid_map_rgb (0x00, 0x00, 0x00);          /* black */
    va_white = vid_map_rgb (0xFF, 0xFF, 0xFF);          /* white */
    sim_printf ("QDSS Display Created.  ");
    va_show_capture (stdout, NULL, 0, NULL);
    if (sim_log)
        va_show_capture (sim_log, NULL, 0, NULL);
    sim_printf ("\n");
    va_active = TRUE;
    }
return auto_config (NULL, 0);                           /* run autoconfig */
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

t_stat va_show_cmap (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
int32 i;
for (i = 0; i < VA_BPP; i++)
    fprintf (st, "%d = (0x%02x, 0x%02x, 0x%02x)\n", i,
        va_red_map[i], va_grn_map[i], va_blu_map[i]);
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
fprintf (st, "VCB02 8-Bit Colour Video Subsystem (%s)\n\n", dptr->name);
fprintf (st, "Use the Control-Right-Shift key combination to regain focus from the simulated\n");
fprintf (st, "video display\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *va_description (DEVICE *dptr)
{
return "VCB02 Colour Graphics Adapter";
}

#else /* defined(VAX_620) */
static const char *dummy_declaration = "Something to compile";
#endif /* !defined(VAX_620) */
