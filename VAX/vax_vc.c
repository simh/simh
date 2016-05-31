/* vax_vc.c: QVSS video simulator (VCB01)

   Copyright (c) 2011-2013, Matt Burke

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

   vc           Qbus video subsystem

   08-Nov-2013  MB      Implemented mouse position register
   06-Nov-2013  MB      Increased the speed of v-sync interrupts, which
                        was too slow for some O/S drivers.
   11-Jun-2013  MB      First version
*/

#if !defined(VAX_620)

#include "vax_defs.h"
#include "sim_video.h"
#include "vax_2681.h"

/* CSR - control/status register */

BITFIELD vc_csr_bits[] = {
    BIT(MOD),                           /* Monitor size (1 -> VR260(19"), 0 -> (15") */
#define CSR_V_MOD 0
#define CSR_MOD     (1<<CSR_V_MOD)
    BITNCF(1),                          /* unused */
    BIT(VID),                           /* Video output Enable */
#define CSR_V_VID     2
#define CSR_VID     (1<<CSR_V_VID)
    BIT(FNC),                           /* Cursor function */
#define CSR_V_FNC     3
#define CSR_FNC     (1<<CSR_V_FNC)
    BIT(VRB),                           /* Video readback Enable */
#define CSR_V_VRB     4
#define CSR_VRB     (1<<CSR_V_VRB)
    BIT(TST),                           /* Test bit */
#define CSR_V_TST     5
#define CSR_TST     (1<<CSR_V_TST)
    BIT(IEN),                           /* Interrupt Enable */
#define CSR_V_IEN     6
#define CSR_IEN     (1<<CSR_V_IEN)
    BIT(CUR),                           /* Cursor active */
#define CSR_V_CUR     7
#define CSR_CUR     (1<<CSR_V_CUR)
    BIT(MSA),                           /* Mouse Button A */
#define CSR_V_MSA     8
#define CSR_MSA     (1<<CSR_V_MSA)
    BIT(MSA),                           /* Mouse Button B */
#define CSR_V_MSB     9
#define CSR_MSB     (1<<CSR_V_MSB)
    BIT(MSA),                           /* Mouse Button C */
#define CSR_V_MSC     10
#define CSR_MSC     (1<<CSR_V_MSC)
    BITF(MA,8),                         /* Memory Bank Switch (Base Address) */
#define CSR_V_MA      11
#define CSR_S_MA      4
#define CSR_M_MA  (((1<<CSR_S_MA)-1)<<CSR_V_MA)
    BITNCF(1),                          /* unused */
    ENDBITS
};
#define CSR_RW          (CSR_IEN|CSR_TST|CSR_VRB|CSR_FNC|CSR_VID)

/* ICSR - interrupt controller command/status register */

BITFIELD vc_icsr_bits[] = {
    BITF(IRRVEC,3),                     /* IRR Vector */
#define ICSR_V_IRRVEC  0
#define ICSR_S_IRRVEC  3
#define ICSR_M_IRRVEC  (((1<<ICSR_S_IRRVEC)-1)<<ICSR_V_IRRVEC)
    BIT(MMS),                           /* Master Mask */
#define ICSR_V_MMS     3
#define ICSR_MMS       (1<<ICSR_V_MMS)
    BIT(INM),                           /* Interrupt Mode */
#define ICSR_V_INM     4
#define ICSR_INM       (1<<ICSR_V_INM)
    BIT(PRM),                           /* Priority Mode */
#define ICSR_V_PRM     5
#define ICSR_PRM       (1<<ICSR_V_PRM)
    BIT(ENA),                           /* Enable */
#define ICSR_V_ENA     6
#define ICSR_ENA       (1<<ICSR_V_ENA)
    BIT(GRI),                           /* Group Interrupt */
#define ICSR_V_GRI     7
#define ICSR_GRI       (1<<ICSR_V_GRI)
    BITNCF(8),                          /* unused */
    ENDBITS
};


const char *vc_icm_rp_names[] = {"ISR", "IMR", "IRR", "ACR"};

/* mode - interrupt controller mode register */

BITFIELD vc_ic_mode_bits[] = {
    BIT(PM),                            /* Priority Mode */
#define ICM_V_PM     0
#define ICM_PM       (1<<ICM_V_PM)
    BIT(PM),                            /* Vector Selection */
#define ICM_V_VS     1
#define ICM_VS       (1<<ICM_V_VS)
    BIT(IM),                            /* Interrupt Mode */
#define ICM_V_IM     2
#define ICM_IM       (1<<ICM_V_IM)
    BIT(GIP),                           /* Group Interrupt Polarity */
#define ICM_V_GIP    3
#define ICM_GIP      (1<<ICM_V_GIP)
    BIT(REQP),                          /* Interrupt Request Polarity */
#define ICM_V_REQP  4
#define ICM_REQP     (1<<ICM_V_REQP)
    BITFNAM(RP,2,vc_icm_rp_names),      /* Register Preselect */
#define ICM_V_RP    5
#define ICM_S_RP    2
#define ICM_M_RP     (((1<<ICM_S_RP)-1)<<ICM_V_RP)
    BIT(MM),                            /* Master Mask */
#define ICM_V_MM    7
#define ICM_MM       (1<<ICM_V_MM)
    ENDBITS
};

#define CRTCP_REG       0x001F                          /* CRTC internal register address */
#define CRTCP_VB        0x0020                          /* Vertical blank */
#define CRTCP_LPF       0x0040                          /* Light pen register full */
#define CRTCP_US        0x0080                          /* Update strobe */
#define CRTCP_RW        CRTCP_REG

#define CRTC_HTOT       0                               /* Horizontal total */
#define CRTC_HDSP       1                               /* Horizontal displayed */
#define CRTC_HPOS       2                               /* HSYNC position */
#define CRTC_HVWD       3                               /* HSYNC/VSYNC widths */
#define CRTC_VTOT       4                               /* Vertical total */
#define CRTC_VTOA       5                               /* Vertical total adjust */
#define CRTC_VDSP       6                               /* Vertical displayed */
#define CRTC_VPOS       7                               /* VSYNC position */
#define CRTC_MODE       8                               /* Mode */
#define CRTC_MSCN       9                               /* Maximum scan line */
#define CRTC_CSCS       10                              /* Cursor scan start */
#define CRTC_CSCE       11                              /* Cursor scan end */
#define CRTC_SAH        12                              /* Start address high */
#define CRTC_SAL        13                              /* Start address low */
#define CRTC_CAH        14                              /* Cursor address high */
#define CRTC_CAL        15                              /* Cursor address low */
#define CRTC_LPPL       16                              /* Light pen position low */
#define CRTC_LPPH       17                              /* Light pen position high */
#define CRTC_SIZE       18                              /* Number of registers */

#define IRQ_DUART       0                               /* UART chip */
#define IRQ_VSYNC       1                               /* VSYNC */
#define IRQ_MOUSE       2                               /* Mouse movement */
#define IRQ_CSTRT       3                               /* Cursor start */
#define IRQ_MBA         4                               /* Mouse button A */
#define IRQ_MBB         5                               /* Mouse button B */
#define IRQ_MBC         6                               /* Mouse button C */
#define IRQ_SPARE       7                               /* (spare) */

#define VC_XSIZE        1024                            /* screen size */
#define VC_YSIZE        864
#define VC_MEMSIZE      (1u << 16)                      /* video memory size */

#define VC_MOVE_MAX     49                              /* mouse movement max (per update) */

#define VCMAP_VLD       0x80000000                      /* valid */
#define VCMAP_LN        0x00000FFF                      /* buffer line */

#define VC_OFF(x,y)     ((x >> 5) | (y << 5))           /* index into framebuffer */
#define CUR_X           (vc_curx & 0x3FF)               /* cursor X */
#define CUR_Y           ((vc_crtc[CRTC_CAH] * \
                         (vc_crtc[CRTC_MSCN] + 1)) + \
                         vc_crtc[CRTC_CSCS])            /* cursor Y */
#define CUR_V            ((vc_crtc[CRTC_CSCS] & 0x20) == 0) /* cursor visible */
#define CUR_F            (vc_csr & CSR_FNC)             /* cursor function (0->AND, 1->OR) */

#define VSYNC_TIME      8000                            /* vertical sync interval */

#define IOLN_QVSS       0100

extern int32 tmxr_poll;                                 /* calibrated delay */

extern t_stat lk_wr (uint8 c);
extern t_stat lk_rd (uint8 *c);
extern t_stat vs_wr (uint8 c);
extern t_stat vs_rd (uint8 *c);

struct vc_int_t {
    uint32 ptr;
    uint32 vec[8];                                      /* Interrupt vectors */
    uint32 irr;                                         /* Interrupt request */
    uint32 imr;                                         /* Interrupt mask */
    uint32 isr;                                         /* Interrupt status */
    uint32 acr;                                         /* Auto-clear mask */
    uint32 mode;
    };

struct vc_int_t vc_intc;                                /* Interrupt controller */

uint32 vc_csr = 0;                                      /* Control/status */
uint32 vc_curx = 0;                                     /* Cursor X-position */
uint32 vc_cur_x = 0;                                    /* Last cursor X-position */
uint32 vc_cur_y = 0;                                    /* Last cursor Y-position */
uint32 vc_cur_f = 0;                                    /* Last cursor function (0->AND, 1->OR) */
t_bool vc_cur_v = FALSE;                                /* Last cursor visible */
t_bool vc_cur_new_data = FALSE;                         /* New Cursor image data */
t_bool vc_input_captured = FALSE;                       /* Mouse and Keyboard input captured in video window */
uint32 vc_mpos = 0;                                     /* Mouse position */
uint32 vc_crtc[CRTC_SIZE];                              /* CRTC registers */
uint32 vc_crtc_p = 0;                                   /* CRTC pointer */
uint32 vc_icdr = 0;                                     /* Interrupt controller data */
uint32 vc_icsr = 0;                                     /* Interrupt controller status */
uint32 *vc_map;                                         /* Scanline map */
uint32 *vc_buf = NULL;                                  /* Video memory */
uint32 *vc_lines = NULL;                                /* Video Display Lines */
uint8 vc_cur[256];                                      /* Cursor image */

t_stat vc_rd (int32 *data, int32 PA, int32 access);
t_stat vc_wr (int32 data, int32 PA, int32 access);
t_stat vc_svc (UNIT *uptr);
t_stat vc_reset (DEVICE *dptr);
t_stat vc_detach (UNIT *dptr);
t_stat vc_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat vc_set_capture (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat vc_show_capture (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
void vc_setint (int32 src);
int32 vc_inta (void);
void vc_clrint (int32 src);
void vc_uart_int (uint32 set);
t_stat vc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *vc_description (DEVICE *dptr);


/* QVSS data structures

   vc_dev       QVSS device descriptor
   vc_unit      QVSS unit list
   vc_reg       QVSS register list
   vc_mod       QVSS modifier list
*/

DIB vc_dib = {
    IOBA_AUTO, IOLN_QVSS, &vc_rd, &vc_wr,
    2, IVCL (QVSS), VEC_AUTO, { &vc_inta, &vc_inta }
    };

/* Debugging Bitmaps */

#define DBG_REG         0x0100                          /* register activity */
#define DBG_CRTC        0x0200                          /* crtc register activity */
#define DBG_CURSOR      0x0400                          /* Cursor content, function and visibility activity */
#define DBG_TCURSOR     0x0800                          /* Cursor content, function and visibility activity */
#define DBG_SCANL       0x1000                          /* Scanline map activity */
#define DBG_INT0        0x0001                          /* interrupt 0 */
#define DBG_INT1        0x0002                          /* interrupt 1 */
#define DBG_INT2        0x0004                          /* interrupt 2 */
#define DBG_INT3        0x0008                          /* interrupt 3 */
#define DBG_INT4        0x0010                          /* interrupt 4 */
#define DBG_INT5        0x0020                          /* interrupt 5 */
#define DBG_INT6        0x0040                          /* interrupt 6 */
#define DBG_INT7        0x0080                          /* interrupt 7 */
#define DBG_INT         0x00FF                          /* interrupt 0-7 */

DEBTAB vc_debug[] = {
    {"REG",     DBG_REG,                "Register activity"},
    {"CRTC",    DBG_CRTC,               "CRTC register activity"},
    {"CURSOR",  DBG_CURSOR,             "Cursor content, function and visibility activity"},
    {"TCURSOR", DBG_TCURSOR,            "Cursor content, function and visibility activity"},
    {"SCANL",   DBG_SCANL,              "Scanline map activity"},
    {"DUART",   DBG_INT0,               "interrupt 0"},
    {"VSYNC",   DBG_INT1,               "interrupt 1"},
    {"MOUSE",   DBG_INT2,               "interrupt 2"},
    {"CSTRT",   DBG_INT3,               "interrupt 3"},
    {"MBA",     DBG_INT4,               "interrupt 4"},
    {"MBB",     DBG_INT5,               "interrupt 5"},
    {"MBC",     DBG_INT6,               "interrupt 6"},
    {"SPARE",   DBG_INT7,               "interrupt 7"},
    {"INT",     DBG_INT0|DBG_INT1|DBG_INT2|DBG_INT3|DBG_INT4|DBG_INT5|DBG_INT6|DBG_INT7, "interrupt 0-7"},
    {"VMOUSE",  SIM_VID_DBG_MOUSE,      "Video Mouse"},
    {"VCURSOR", SIM_VID_DBG_CURSOR,     "Video Cursor"},
    {"VKEY",    SIM_VID_DBG_KEY,        "Video Key"},
    {"VVIDEO",  SIM_VID_DBG_VIDEO,      "Video Video"},
    {0}
    };

UNIT vc_unit = { UDATA (&vc_svc, UNIT_IDLE, 0) };

REG vc_reg[] = {
    { HRDATADF (CSR,        vc_csr, 16, "Control and status register",                  vc_csr_bits) },
    { HRDATAD  (CURX,      vc_curx,  9, "Cursor X-position") },
    { HRDATAD  (MPOS,      vc_mpos, 16, "Mouse position register") },
    { HRDATAD  (ICDR,      vc_icdr, 16, "Interrupt controller data register") },
    { HRDATADF (ICSR,      vc_icsr, 16, "Interrupt controller command/status register", vc_icsr_bits) },
    { HRDATAD  (IRR,   vc_intc.irr,  8, "Interrupt controller request") },
    { HRDATAD  (IMR,   vc_intc.imr,  8, "Interrupt controller mask") },
    { HRDATAD  (ISR,   vc_intc.isr,  8, "Interrupt controller status") },
    { HRDATAD  (ACR,   vc_intc.acr,  8, "Interrupt controller Auto-clear mask") },
    { HRDATADF (MODE, vc_intc.mode,  8, "Interrupt controller mode",                    vc_ic_mode_bits) },
    { HRDATA   (IPTR,  vc_intc.ptr,  8), REG_HRO },
    { BRDATA   (VEC,   vc_intc.vec, 16, 32, 8) },
    { BRDATAD  (CRTC,      vc_crtc, 16, 8, CRTC_SIZE, "CRTC registers") },
    { HRDATAD  (CRTCP,   vc_crtc_p,  8, "CRTC pointer") },
    { NULL }
    };

MTAB vc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 1, NULL, "ENABLE",
        &vc_set_enable, NULL, NULL, "Enable VCB01 (QVSS)" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLE",
        &vc_set_enable, NULL, NULL, "Disable VCB01 (QVSS)" },
    { MTAB_XTD|MTAB_VDV, TRUE, NULL, "CAPTURE",
        &vc_set_capture, &vc_show_capture, NULL, "Enable Captured Input Mode" },
    { MTAB_XTD|MTAB_VDV, FALSE, NULL, "NOCAPTURE",
        &vc_set_capture, NULL, NULL, "Disable Captured Input Mode" },
    { MTAB_XTD|MTAB_VDV, TRUE, "OSCURSOR", NULL,
        NULL, &vc_show_capture, NULL, "Display Input Capture mode" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "VIDEO", NULL,
        NULL, &vid_show_video, NULL, "Display the host system video capabilities" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 }
    };

DEVICE vc_dev = {
    "QVSS", &vc_unit, vc_reg, vc_mod,
    1, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &vc_reset,
    NULL, NULL, &vc_detach,
    &vc_dib, DEV_DIS | DEV_QBUS | DEV_DEBUG, 0,
    vc_debug, NULL, NULL, &vc_help, NULL, NULL, 
    &vc_description
    };

UART2681 vc_uart = {
    &vc_uart_int, NULL,
    { { &lk_wr, &lk_rd }, { &vs_wr, &vs_rd } }
    };

const char *vc_regnames[] = {
    "CSR",          /* +0 */
    "CUR-X",        /* +2 */
    "MPOS",         /* +4 */
    "",             /* +6 spare */
    "CRTCA",        /* +8 */
    "CRTCD",        /* +10 */
    "ICDR",         /* +12 */
    "ICSR",         /* +14 */
    "",             /* +16 spare */
    "",             /* +18 spare */
    "",             /* +20 spare */
    "",             /* +22 spare */
    "",             /* +24 spare */
    "",             /* +26 spare */
    "",             /* +28 spare */
    "",             /* +30 spare */
    "UART1A2A",     /* +32 */
    "UARTSTCLA",    /* +34 */
    "UARTCMDA",     /* +36 */
    "UARTBUFA",     /* +38 */
    "",             /* +40 spare */
    "UARTIMSK",     /* +42 */
    "",             /* +44 spare */
    "",             /* +46 spare */
    "UART1B2B",     /* +48 */
    "UARTSTCLB",    /* +50 */
    "UARTCMDB",     /* +52 */
    "UARTBUFB",     /* +54 */
    "",             /* +56 spare */
    "",             /* +56 spare */
    "",             /* +58 spare */
    "",             /* +60 spare */
    "",             /* +62 spare */
};

const char *vc_crtc_regnames[] = {
    "HTOT",         /* Horizontal Total The total number of character times in a line, minus 1 */
    "HDSP",         /* Horizontal Displayed The total number of displayed characters in a line. */
    "HPOS",         /* HSYNC Position Defines the number of character times until HSYNC (horizontal sync). */
    "HVWD",         /* HSYNC/VSYNC Widths Four bits each are used to define the HSYNC
                       pulse width and the VSYNC (vertical sync) pulse width. */
    "VTOT",         /* Vertical Total Total number of character rows on the screen, minus 1. */
    "VTOA",         /* Vertical Total Adjust The number of scan lines to complete the screen. */
    "VDSP",         /* Vertical Displayed The number of character rows displayed. */
    "VPOS",         /* VSYNC Position The number of character rows until VSYNC. */
    "MODE",         /* Mode Controls addressing, interlace, and cursor. */
    "MSCN",         /* Maximum Scan Line The number of scan lines in a character row, minus 1. */
    "CSCS",         /* Cursor Scan Start Defines the scan line at which the cursor starts. */
    "CSCE",         /* Cursor Scan End Defines where the cursor ends. */
    "SAH",          /* Start Address High Defines the RAM location where video refresh */
    "SAL",          /* Start Address Low begins. */
    "CAH",          /* Cursor Address High Defines the cursor position in RAM. */
    "CAL",          /* Cursor Address Low */
    "LPPL",         /* Light Pen Position High Contains the position of the light pen. */
    "LPPH",         /* Light Pen Position Low */
    "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31"
};


t_stat vc_rd (int32 *data, int32 PA, int32 access)
{
uint32 rg = (PA >> 1) & 0x1F;
uint32 crtc_rg, i;

*data = 0;
switch (rg) {

    case 0:                                             /* CSR */
        *data = vc_csr;
        break;

    case 1:                                             /* Cursor X */
        *data = 0;
        break;

    case 2:                                             /* Mouse position */
        *data = vc_mpos;
        break;

    case 4:                                             /* CRTC addr ptr */
        *data = vc_crtc_p;
        sim_debug (DBG_CRTC, &vc_dev, "CRTC-Addr Read: %d - %s\n", vc_crtc_p, vc_crtc_regnames[vc_crtc_p & CRTCP_REG]);
        break;

    case 5:                                             /* CRTC data */
        crtc_rg = vc_crtc_p & CRTCP_REG;
        *data = vc_crtc[crtc_rg];
        if ((crtc_rg == CRTC_LPPL) || (crtc_rg == CRTC_LPPH))
            vc_crtc_p &= ~CRTCP_LPF;                   /* Clear light pen full */
        sim_debug (DBG_CRTC, &vc_dev, "CRTC-Data:%s[%d] Read: 0x%x\n", vc_crtc_regnames[crtc_rg], crtc_rg, *data);
        break;

    case 6:                                             /* ICDR */
        switch ((vc_intc.mode & ICM_M_RP) >> ICM_V_RP) {
        
            case 0:                                     /* ISR */
                *data = vc_intc.isr;
                break;

            case 1:                                     /* IMR */
                *data = vc_intc.imr;
                break;

            case 2:                                     /* IRR */
                *data = vc_intc.irr;
                break;

            case 3:                                     /* ACR */
                *data = vc_intc.acr;
                break;
                }
        break;

    case 7:                                             /* ICSR */
        *data = vc_icsr | 0x40;                         /* Chip enabled */
        *data |= (vc_intc.mode & ICM_PM) ? 0x20 : 0;    /* Priority mode */
        *data |= (vc_intc.mode & ICM_IM) ? 0x10 : 0;    /* Interrupt mode */
        *data |= (vc_intc.mode & ICM_MM) ? 0x8 : 0;     /* Master mask */
        if (vc_icsr & 0x80) {                           /* Group int pending */
            for (i = 0; i < 8; i++) {
                if (vc_intc.isr & (1u << i)) {
                    *data |= i;
                    break;
                    }
                }
            }
        break;

    case 16:                                            /* UART mode 1A,2A */
    case 17:                                            /* UART status/clock A */
    case 18:                                            /* UART command A */
    case 19:                                            /* UART tx/rx buf A */
    case 21:                                            /* UART interrupt status/mask */
    case 24:                                            /* UART mode 1B,2B */
    case 25:                                            /* UART status/clock B */
    case 26:                                            /* UART command B */
    case 27:                                            /* UART tx/rx buf B */
        *data = ua2681_rd (&vc_uart, (rg - 16));
        break;
    
    default:                                            /* Spares */
        break;
        }                                               /* end switch PA */
sim_debug (DBG_REG, &vc_dev, "vc_rd(%s) data=0x%04X\n", vc_regnames[(PA >> 1) & 0x1F], *data);
return SCPE_OK;
}

t_stat vc_wr (int32 data, int32 PA, int32 access)
{
uint32 rg = (PA >> 1) & 0x1F;
uint32 crtc_rg;
uint32 old_data;

sim_debug (DBG_REG, &vc_dev, "vc_wr(%s) data=0x%04X\n", vc_regnames[(PA >> 1) & 0x1F], data);
switch (rg) {

    case 0:                                             /* CSR */
        if ((data & CSR_IEN) && ((vc_csr & CSR_IEN) == 0)) {
            sim_cancel (&vc_unit);                      /* reactivate with short delay */
            sim_activate (&vc_unit, VSYNC_TIME);        /* in case software checks for vsync */
            }
        old_data = vc_csr;
        vc_csr = (vc_csr & ~CSR_RW) | (data & CSR_RW);
        if ((vc_csr ^ old_data) & CSR_FNC) {
            sim_debug (DBG_CURSOR, &vc_dev, "Cursor Function changed to: %s\n", CUR_F ? "OR" : "AND");
            }
        break;

    case 1:                                             /* Cursor X */
        vc_curx = data;
        sim_debug (SIM_VID_DBG_MOUSE, &vc_dev, "Cursor-X set: %d\n", vc_curx);
        vid_set_cursor_position (CUR_X, CUR_Y);
        break;

    case 2:                                             /* Mouse position */
        break;

    case 4:                                             /* CRTC addr ptr */
        vc_crtc_p = (vc_crtc_p & ~CRTCP_RW) | (data & CRTCP_RW);
        sim_debug (DBG_CRTC, &vc_dev, "CRTC-Addr Set: %d - %s\n", vc_crtc_p, vc_crtc_regnames[vc_crtc_p & CRTCP_REG]);
        break;

    case 5:                                             /* CRTC data */
        crtc_rg = vc_crtc_p & CRTCP_REG;
        old_data = vc_crtc[crtc_rg];
        vc_crtc[crtc_rg] = data & BMASK;
        sim_debug (DBG_CRTC, &vc_dev, "CRTC-Data:%s[%d] Set: 0x%x\n", vc_crtc_regnames[crtc_rg], crtc_rg, vc_crtc[crtc_rg]);
        if (crtc_rg == CRTC_CAH) {
            sim_debug (SIM_VID_DBG_MOUSE, &vc_dev, "Cursor-Y-High set (%d). Y value: %d\n", vc_crtc[crtc_rg], CUR_Y);
            vid_set_cursor_position (CUR_X, CUR_Y);
            }
        if (crtc_rg == CRTC_CAL) {
            sim_debug (SIM_VID_DBG_MOUSE, &vc_dev, "Cursor-Y-Low set (%d). Y value: %d\n", vc_crtc[crtc_rg], CUR_Y);
            }
        if (crtc_rg == CRTC_MSCN) {
            sim_debug (SIM_VID_DBG_MOUSE, &vc_dev, "Maximum Scan Line set (%d). Y value: %d\n", vc_crtc[crtc_rg], CUR_Y);
            }
        if (crtc_rg == CRTC_CSCS) {
            if (0x20 & (old_data ^ vc_crtc[crtc_rg])) {
                sim_debug (DBG_CURSOR, &vc_dev, "Visibility Changed to: %s\n", CUR_V ? "Visible" : "Invisible");
                }
            sim_debug (SIM_VID_DBG_MOUSE, &vc_dev, "CSCS set (%d). Y value: %d\n", vc_crtc[crtc_rg], CUR_Y);
            }
        break;

    case 6:                                             /* ICDR */
        if (vc_intc.ptr == 8)                           /* IMR */
            vc_intc.imr = data & 0xFFFF;
        else if (vc_intc.ptr == 9)                      /* ACR */
            vc_intc.acr = data & 0xFFFF;
        else  
            /* 
               Masking the vector with 0x1FC is probably storing 
               one more bit than the original hardware did.  
               Doing this allows a maximal simulated hardware 
               configuration use a reasonable vector where real 
               hardware could never be assembled with that many 
               devices.
             */
            vc_intc.vec[vc_intc.ptr] = data & 0x1FC;    /* Vector */ 
        break;

    case 7:                                             /* ICSR */
        switch ((data >> 4) & 0xF) {
        
            case 0:                                     /* Reset */
                vc_intc.imr = 0xFF;
                vc_intc.irr = 0;
                vc_intc.isr = 0;
                vc_intc.acr = 0;
                break;

            case 2:                                     /* Clear IRR & IMR */
                if (data & 0x8) {                       /* one bit */
                    vc_intc.irr &= ~(1u << (data & 0x7));
                    vc_intc.imr &= ~(1u << (data & 0x7));
                    }
                else {                                  /* all bits */
                    vc_intc.irr = 0;
                    vc_intc.imr = 0;
                    }
                break;

            case 3:                                     /* Set IMR */
                if (data & 0x8)                         /* one bit */
                    vc_intc.imr |= (1u << (data & 0x7));
                else                                    /* all bits */
                    vc_intc.imr = 0xFF;
                break;

            case 4:                                     /* Clear IRR */
                if (data & 0x8)                         /* one bit */
                    vc_intc.irr &= ~(1u << (data & 0x7));
                else                                    /* all bits */
                    vc_intc.irr = 0;
                break;

            case 6:                                     /* Clear highest priority ISR */
                break;

            case 7:                                     /* Clear ISR */
                if (data & 0x8)                         /* one bit */
                    vc_intc.isr &= ~(1u << (data & 0x7));
                else                                    /* all bits */
                    vc_intc.isr = 0;
                break;

            case 8:                                     /* Load mode bits */
            case 9:
                vc_intc.mode &= ~0x1F | (data & 0x1F);
                break;

            case 10:                                    /* Control mode bits */
                vc_intc.mode &= ~0x60 | ((data << 3) & 0x60); /* mode<06:05> = data<03:02> */
                if (((data & 0x3) == 0x1) || ((data & 0x3) == 2))
                    vc_intc.mode &= ~0x80 | ((data << 7) & 0x80);
                break;

            case 11:                                    /* Preselect IMR */
                vc_intc.ptr = 8;
                break;
            
            case 12:                                    /* Preselect ACR */
                vc_intc.ptr = 9;
                break;

            case 14:                                    /* Preselect response mem */
                vc_intc.ptr = (data & 0x7);
                break;
                }
        break;

    case 16:                                            /* UART mode 1A,2A */
    case 17:                                            /* UART status/clock A */
    case 18:                                            /* UART command A */
    case 19:                                            /* UART tx/rx buf A (keyboard) */
    case 21:                                            /* UART interrupt status/mask */
    case 24:                                            /* UART mode 1B,2B */
    case 25:                                            /* UART status/clock B */
    case 26:                                            /* UART command B */
    case 27:                                            /* UART tx/rx buf B (mouse) */
        ua2681_wr (&vc_uart, (rg - 16), data);
        break;
    
    default:                                            /* Spares */
        break;
        }

return SCPE_OK;
}

int32 vc_mem_rd (int32 pa)
{
uint32 rg = (pa >> 2) & 0xFFFF;

if (!vc_buf)                                            /* QVSS disabled? */
    MACH_CHECK (MCHK_READ);                             /* Invalid memory reference */

return vc_buf[rg];
}

void vc_mem_wr (int32 pa, int32 val, int32 lnt)
{
uint32 rg = (pa >> 2) & 0xFFFF;
uint32 nval;
int32 i;
int32 sc;
uint32 scrln, bufln;
uint32 idx;

if (!vc_buf)                                            /* QVSS disabled? */
    MACH_CHECK (MCHK_WRITE);                            /* Invalid memory reference */

if (lnt < L_LONG) {
    uint32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    uint32 t = vc_buf[rg];
    sc = (pa & 3) << 3;
    nval = ((val & mask) << sc) | (t & ~(mask << sc));
    }
else nval = (uint32)val;

if (rg >= 0xFFF8) {                                     /* cursor image */
    idx = (pa << 3) & 0xFF;                             /* get byte index */
    if (sim_deb) {
        char binary[40];
        int32 i;

        for (i=0; i<8*lnt; i++)
            binary[i] = '0' + ((val & (1 << i)) != 0);
        binary[i] = '\0';
        sim_debug (DBG_CURSOR, &vc_dev, "Cursor Data at 0x%X set to 0x%0*X - %s\n", rg, 2*lnt, val, binary);
        }
    for (i = 0; i < (lnt << 3); i++)
        vc_cur[idx++] = (val >> i) & 1;                 /* 1bpp to 8bpp */
    vc_cur_new_data = TRUE;
    }
else if (rg >= 0xFE00) {                                /* scanline map */
    if (vc_buf[rg] != nval) {
        scrln = (pa >> 1) & 0x3FF;                      /* screen line */
        sc = (scrln & 1) ? 16 : 0;                      /* odd line? (upper word) */
        bufln = (nval >> sc) & 0x7FF;                   /* buffer line */
        vc_map[scrln] = bufln;                          /* update map */
        sim_debug (DBG_SCANL, &vc_dev, "Scan Line 0x%X set to 0x%X\n", scrln, bufln);

        if (lnt > L_WORD) {                             /* remapping 2 lines? */
            scrln++;                                    /* next screen line */
            bufln = (val >> 16) & 0x7FF;                /* buffer line */
            vc_map[scrln] = bufln;                      /* update map */
            }
        }
    }
bufln = rg / 32;
for (scrln = 0; scrln < 1024; scrln++) {
    if ((vc_map[scrln] & 0x7FF) == bufln) {
        vc_map[scrln] &= ~VCMAP_VLD;                    /* invalidate map */
        }
    }
vc_buf[rg] = nval;
}

static SIM_INLINE void vc_invalidate (uint32 y1, uint32 y2)
{
uint32 ln;

if ((!vc_input_captured) && (!(vc_dev.dctrl & DBG_CURSOR)))
    return;
for (ln = y1; ln < y2; ln++)
    vc_map[ln] &= ~VCMAP_VLD;                           /* invalidate map entry */
}

static void vc_set_vid_cursor (t_bool visible, int func, uint8 *cur_bits)
{
uint8 data[2*16];
uint8 mask[2*16];
int i, d, m;

sim_debug (DBG_CURSOR, &vc_dev, "vc_set_vid_cursor(%s, %s)\n", visible ? "Visible" : "Invisible", func ? "OR" : "AND");
memset (data, 0, sizeof(data));
memset (mask, 0, sizeof(mask));
for (i=0; i<16*16; i++) {
    if (func) {     /* OR */
        if (cur_bits[i]) { 
            /* White */
            d = 0; m = 1;
            }
        else {
            /* Transparent */
            d = 0; m = 0;
            }
        }
    else {          /* AND */
        if (cur_bits[i]) { 
            /* Black */
            d = 1; m = 1;
            }
        else {
            /* Transparent */
            d = 0; m = 0;
            }
        }
    data[i>>3] |= d<<(7-(i&7));
    mask[i>>3] |= m<<(7-(i&7));
    }
if ((vc_dev.dctrl & DBG_CURSOR) && (vc_dev.dctrl & DBG_TCURSOR)) {
    /* box the cursor image */
    for (i=0; i<16*16; i++) {
        if ((0 == i>>4) || (0xF == i>>4) || (0 == (i&0xF)) || (0xF == (i&0xF))) {
            data[i>>3] |= 1<<(7-(i&7));
            mask[i>>3] |= 1<<(7-(i&7));
            }
        if ((1 == i>>4) || (0xE == i>>4) || (1 == (i&0xF)) || (0xE == (i&0xF))) {
            data[i>>3] &= ~(1<<(7-(i&7)));
            mask[i>>3] |= 1<<(7-(i&7));
            }
        }
    }
vid_set_cursor (visible, 16, 16, data, mask, 0, 0);
}

void vc_checkint (void)
{
uint32 i;
uint32 msk = (vc_intc.irr & ~vc_intc.imr);              /* unmasked interrutps */
vc_icsr &= ~(ICSR_GRI|ICSR_M_IRRVEC);                   /* clear GRI & vector */

if ((vc_intc.mode & 0x80) && ~(vc_intc.mode & 0x4)) {   /* group int MM & not polled */
    for (i = 0; i < 8; i++) {
        if (msk & (1u << i)) {
            vc_icsr |= (ICSR_GRI | i);
            }
        }
    if ((vc_csr & CSR_IEN)  && (vc_icsr & ICSR_GRI)) {
        if (!(int_req[IPL_QVSS] & (INT_QVSS))) {
            sim_debug (DBG_INT, &vc_dev, "vc_checkint(SET_INT) icsr=0x%x\n", vc_icsr);
            }
        SET_INT (QVSS);
        }
    else {
        if ((int_req[IPL_QVSS] & (INT_QVSS))) {
            sim_debug (DBG_INT, &vc_dev, "vc_checkint(CLR_INT)\n");
            }
        CLR_INT (QVSS);
        }
    }
else {
    if ((int_req[IPL_QVSS] & (INT_QVSS))) {
        sim_debug (DBG_INT, &vc_dev, "vc_checkint(CLR_INT)\n");
        }
    CLR_INT (QVSS);
    }
}

void vc_clrint (int32 src)
{
uint32 msk = (1u << src);
vc_intc.irr &= ~msk;
vc_intc.isr &= ~msk;
sim_debug (msk, &vc_dev, "vc_clrint(%d)\n", src);
vc_checkint ();
}

void vc_setint (int32 src)
{
uint32 msk = (1u << src);
vc_intc.irr |= msk;
sim_debug (msk, &vc_dev, "vc_setint(%d)\n", src);
vc_checkint ();
}

void vc_uart_int (uint32 set)
{
if (set)
    vc_setint (IRQ_DUART);
else
    vc_clrint (IRQ_DUART);
}

int32 vc_inta (void)
{
uint32 i;
uint32 msk = (vc_intc.irr & ~vc_intc.imr);              /* unmasked interrutps */
int32 result;

for (i = 0; i < 8; i++) {
    if (msk & (1u << i)) {
        vc_intc.irr &= ~(1u << i);
        if (vc_intc.acr & (1u << i))
            vc_intc.isr &= ~(1u << i);
        else vc_intc.isr |= (1u << i);
        vc_checkint();
        result = vc_intc.vec[i];
        sim_debug (DBG_INT, &vc_dev, "Int Ack Vector: 0%03o (0x%X)\n", result, result);
        return result;
        }
    }
sim_debug (DBG_INT, &vc_dev, "Int Ack Vector: 0%03o\n", 0);
return 0;                                               /* no intr req */
}

t_stat vc_svc (UNIT *uptr)
{
t_bool updated = FALSE;                                 /* flag for refresh */
uint32 lines;
uint32 ln, col, off;
int32 xpos, ypos, dx, dy;
uint8 *cur;

vc_crtc_p = vc_crtc_p ^ CRTCP_VB;                       /* Toggle VBI */
vc_crtc_p = vc_crtc_p | CRTCP_LPF;                      /* Light pen full */

if (vc_cur_v != CUR_V) {                                /* visibility changed? */
    if (CUR_V)                                          /* visible? */
        vc_invalidate (CUR_Y, (CUR_Y + 16));            /* invalidate new pos */
    else
        vc_invalidate (vc_cur_y, (vc_cur_y + 16));      /* invalidate old pos */
    }
else if (vc_cur_y != CUR_Y) {                           /* moved (Y)? */
    vc_invalidate (CUR_Y, (CUR_Y + 16));                /* invalidate new pos */
    vc_invalidate (vc_cur_y, (vc_cur_y + 16));          /* invalidate old pos */
    }
else if ((vc_cur_x != CUR_X) ||                         /* moved (X)? or */
         (vc_cur_f != CUR_F) ||                         /* mask changed? or */
         (vc_cur_new_data)) {                           /* cursor image changed? */
    vc_invalidate (CUR_Y, (CUR_Y + 16));                /* invalidate new pos */
    }

if ((!vc_input_captured) &&                             /* OS cursor? AND*/
    ((vc_cur_f != CUR_F) ||                             /* (mask changed? OR */
     (vc_cur_new_data) ||                               /*  cursor image changed? OR) */
     (vc_cur_v != CUR_V))) {                            /*  visibility changed?) */
    vc_set_vid_cursor (CUR_V, CUR_F, vc_cur);
    }

vc_cur_x = CUR_X;                                       /* store cursor data */
vc_cur_y = CUR_Y;
vid_set_cursor_position (vc_cur_x, vc_cur_y);
vc_cur_v = CUR_V;
vc_cur_f = CUR_F;
vc_cur_new_data = FALSE;

xpos = vc_mpos & 0xFF;                                  /* get current mouse position */
ypos = (vc_mpos >> 8) & 0xFF;
dx = vid_mouse_xrel;                                    /* get relative movement */
dy = -vid_mouse_yrel;
if (dx > VC_MOVE_MAX)                                   /* limit movement */
    dx = VC_MOVE_MAX;
else if (dx < -VC_MOVE_MAX)
    dx = -VC_MOVE_MAX;
if (dy > VC_MOVE_MAX)
    dy = VC_MOVE_MAX;
else if (dy < -VC_MOVE_MAX)
    dy = -VC_MOVE_MAX;
xpos += dx;                                             /* add to counters */
ypos += dy;
vc_mpos = ((ypos & 0xFF) << 8) | (xpos & 0xFF);         /* update register */
vid_mouse_xrel -= dx;                                   /* reset counters for next poll */
vid_mouse_yrel += dy;

vc_csr |= (CSR_MSA | CSR_MSB | CSR_MSC);                /* reset button states */
if (vid_mouse_b3)                                       /* set new button states */
    vc_csr &= ~CSR_MSA;
if (vid_mouse_b2)
    vc_csr &= ~CSR_MSB;
if (vid_mouse_b1)
    vc_csr &= ~CSR_MSC;

lines = 0;
for (ln = 0; ln < VC_YSIZE; ln++) {
    if ((vc_map[ln] & VCMAP_VLD) == 0) {                /* line invalid? */
        off = vc_map[ln] * 32;                          /* get video buf offset */
        for (col = 0; col < VC_XSIZE; col++)  
            vc_lines[ln*VC_XSIZE + col] = vid_mono_palette[(vc_buf[off + (col >> 5)] >> (col & 0x1F)) & 1];
                                                        /* 1bpp to 32bpp */
        if (CUR_V &&                                    /* cursor visible && need to draw cursor? */
            (vc_input_captured || (vc_dev.dctrl & DBG_CURSOR))) {
            if ((ln >= CUR_Y) && (ln < (CUR_Y + 16))) { /* cursor on this line? */
                cur = &vc_cur[((ln - CUR_Y) << 4)];     /* get image base */
                for (col = 0; col < 16; col++) {
                    if ((CUR_X + col) >= VC_XSIZE)      /* Part of cursor off screen? */
                        continue;                       /* Skip */
                    if (CUR_F)                          /* mask function */
                        vc_lines[ln*VC_XSIZE + CUR_X + col] = vid_mono_palette[(vc_lines[ln*VC_XSIZE + CUR_X + col] == vid_mono_palette[1]) | (cur[col] & 1)];
                    else
                        vc_lines[ln*VC_XSIZE + CUR_X + col] = vid_mono_palette[(vc_lines[ln*VC_XSIZE + CUR_X + col] == vid_mono_palette[1]) & (~cur[col] & 1)];
                    }
                }
            }
        vc_map[ln] |= VCMAP_VLD;                        /* set valid */
        if ((ln == (VC_YSIZE-1)) ||                     /* if end of window OR */
            (vc_map[ln+1] & VCMAP_VLD)) {               /* next is already valid? */
            vid_draw (0, ln-lines, VC_XSIZE, lines+1, vc_lines+(ln-lines)*VC_XSIZE); /* update region */
            lines = 0;
            }
        else
            lines++;
        updated = TRUE;
        }
    }

if (updated)                                            /* video updated? */
    vid_refresh ();                                     /* put to screen */

ua2681_svc (&vc_uart);                                  /* service DUART */
vc_setint (IRQ_VSYNC);                                  /* VSYNC int */
sim_clock_coschedule (uptr, tmxr_poll);                 /* reactivate */
return SCPE_OK;
}

t_stat vc_reset (DEVICE *dptr)
{
uint32 i;
t_stat r;

CLR_INT (QVSS);                                         /* clear int req */
sim_cancel (&vc_unit);                                  /* stop poll */
ua2681_reset (&vc_uart);                                /* reset DUART */

vc_intc.ptr = 0;                                        /* interrupt controller */
vc_intc.irr = 0;
vc_intc.imr = 0xFF;
vc_intc.isr = 0;
vc_intc.acr = 0;
vc_intc.mode = 0x80;
vc_icsr = 0;

vc_csr = (((QVMBASE >> QVMAWIDTH) & ((1<<CSR_S_MA)-1)) << CSR_V_MA) | CSR_MOD;
vc_curx = 0;
vc_mpos = 0;

for (i = 0; i < CRTC_SIZE; i++)
    vc_crtc[i] = 0;
vc_crtc[CRTC_CSCS] = 0x20;                              /* hide cursor */
vc_crtc_p = (CRTCP_LPF | CRTCP_VB);

if (dptr->flags & DEV_DIS) {
    free (vc_buf);
    vc_buf = NULL;
    free (vc_lines);
    vc_lines = NULL;
    free (vc_map);
    vc_map = NULL;
    return vid_close ();
    }

if (!vid_active)  {
    r = vid_open (dptr, NULL, VC_XSIZE, VC_YSIZE, vc_input_captured ? SIM_VID_INPUTCAPTURED : 0);/* display size & capture mode */
    if (r != SCPE_OK)
        return r;
    vc_buf = (uint32 *) calloc (VC_MEMSIZE, sizeof (uint32));
    if (vc_buf == NULL) {
        vid_close ();
        return SCPE_MEM;
        }
    vc_lines = (uint32 *) calloc (VC_XSIZE*VC_YSIZE, sizeof (uint32));
    if (vc_lines == NULL) {
        free (vc_buf);
        vid_close ();
        return SCPE_MEM;
        }
    vc_map = (uint32 *) calloc (VC_XSIZE, sizeof (uint32));
    if (vc_map == NULL) {
        free (vc_lines);
        vc_lines = NULL;
        free (vc_buf);
        vid_close ();
        return SCPE_MEM;
        }
    sim_printf ("QVSS Display Created.  ");
    vc_show_capture (stdout, NULL, 0, NULL);
    if (sim_log)
        vc_show_capture (sim_log, NULL, 0, NULL);
    sim_printf ("\n");
    }
sim_activate_abs (&vc_unit, tmxr_poll);
return auto_config (NULL, 0);                           /* run autoconfig */
}

t_stat vc_detach (UNIT *uptr)
{
if ((vc_dev.flags & DEV_DIS) == 0) {
    vc_dev.flags |= DEV_DIS;
    vc_reset(&vc_dev);
    }
return SCPE_OK;
}

t_stat vc_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
return cpu_set_model (NULL, 0, (val ? "VAXSTATION" : "MICROVAX"), NULL);
}

t_stat vc_set_capture (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (vid_active)
    return sim_messagef (SCPE_ALATT, "Capture Mode Can't be changed with device enabled\n");
vc_input_captured = val;
return SCPE_OK;
}

t_stat vc_show_capture (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
if (vc_input_captured) {
    fprintf (st, "Captured Input Mode, ");
    vid_show_release_key (st, uptr, val, desc);
    }
else
    fprintf (st, "Uncaptured Input Mode");
return SCPE_OK;
}

t_stat vc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "VCB01 Monochrome Video Subsystem (%s)\n\n", dptr->name);
fprintf (st, "Use the Control-Right-Shift key combination to regain focus from the simulated\n");
fprintf (st, "video display\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *vc_description (DEVICE *dptr)
{
return "VCB01 Monochrome Graphics Adapter";
}

#else /* defined(VAX_620) */
static const char *dummy_declaration = "Something to compile";
#endif /* !defined(VAX_620) */
