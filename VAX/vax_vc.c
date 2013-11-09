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

#include "vax_defs.h"
#include "sim_video.h"
#include "vax_2681.h"

/* CSR - control/status register */

BITFIELD vc_csr_bits[] = {
    BIT(MOD),                           /* Monitor size */
#define CSR_V_MDO 0
#define CSR_MOD     (1<<CSR_V_MDO)
    BITNCF(1),                          /* unused */
    BIT(VID),                           /* Video output en */
#define CSR_V_VID     2
#define CSR_VID     (1<<CSR_V_VID)
    BIT(FNC),                           /* Cursor function */
#define CSR_V_FNC     3
#define CSR_FNC     (1<<CSR_V_FNC)
    BIT(VRB),                           /* Video readback en */
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
#define CUR_F            (vc_csr & CSR_FNC)             /* cursor function */

#define VSYNC_TIME      8000                            /* vertical sync interval */

#define IOLN_QVSS       0100

extern int32 int_req[IPL_HLVL];
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
uint32 vc_cur_f = 0;                                    /* Last cursor function */
t_bool vc_cur_v = FALSE;                                /* Last cursor visible */
uint32 vc_mpos = 0;                                     /* Mouse position */
uint32 vc_crtc[CRTC_SIZE];                              /* CRTC registers */
uint32 vc_crtc_p = 0;                                   /* CRTC pointer */
uint32 vc_icdr = 0;                                     /* Interrupt controller data */
uint32 vc_icsr = 0;                                     /* Interrupt controller status */
uint32 vc_map[1024];                                    /* Scanline map */
uint32 *vc_buf = NULL;                                  /* Video memory */
uint8 vc_cur[256];                                      /* Cursor image */

DEVICE vc_dev;
t_stat vc_rd (int32 *data, int32 PA, int32 access);
t_stat vc_wr (int32 data, int32 PA, int32 access);
t_stat vc_svc (UNIT *uptr);
t_stat vc_reset (DEVICE *dptr);
t_stat vc_set_enable (UNIT *uptr, int32 val, char *cptr, void *desc);
void vc_setint (int32 src);
int32 vc_inta (void);
void vc_clrint (int32 src);
void vc_uart_int (uint32 set);
t_stat vc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *vc_description (DEVICE *dptr);


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

/* Debugging Bisim_tmaps */

#define DBG_REG         0x0100                          /* register activity */
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
    {"REG",     DBG_REG},
    {"DUART",   DBG_INT0},
    {"VSYNC",   DBG_INT1},
    {"MOUSE",   DBG_INT2},
    {"CSTRT",   DBG_INT3},
    {"MBA",     DBG_INT4},
    {"MBB",     DBG_INT5},
    {"MBC",     DBG_INT6},
    {"SPARE",   DBG_INT7},
    {"INT",     DBG_INT0|DBG_INT1|DBG_INT2|DBG_INT3|DBG_INT4|DBG_INT5|DBG_INT6|DBG_INT7},
    {"VMOUSE",  SIM_VID_DBG_MOUSE},
    {"VKEY",    SIM_VID_DBG_KEY},
    {"VVIDEO",  SIM_VID_DBG_VIDEO},
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
    { BRDATAD  (MAP,        vc_map, 16, 16, 1024, "Scanline map") },
    { NULL }
    };

MTAB vc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 1, NULL, "ENABLE",
        &vc_set_enable, NULL, NULL, "Enable VCB01 (QVSS)" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLE",
        &vc_set_enable, NULL, NULL, "Disable VCB01 (QVSS)" },
    { MTAB_XTD|MTAB_VDV, 0, "RELEASEKEY", NULL,
        NULL, &vid_show_release_key, NULL, "Display the window focus release key" },
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
    NULL, NULL, NULL,
    &vc_dib, DEV_DIS | DEV_QBUS | DEV_DEBUG, 0,
    vc_debug, NULL, NULL, &vc_help, NULL, NULL, 
    &vc_description
    };

UART2681 vc_uart = {
    &vc_uart_int,
    { { &lk_wr, &lk_rd }, { &vs_wr, &vs_rd } }
    };

char *vc_regnames[] = {
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
        break;

    case 5:                                             /* CRTC data */
        crtc_rg = vc_crtc_p & CRTCP_REG;
        *data = vc_crtc[crtc_rg];
        if ((crtc_rg == CRTC_LPPL) || (crtc_rg == CRTC_LPPH))
            vc_crtc_p &= ~CRTCP_LPF;                   /* Clear light pen full */
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

sim_debug (DBG_REG, &vc_dev, "vc_wr(%s) data=0x%04X\n", vc_regnames[(PA >> 1) & 0x1F], data);
switch (rg) {

    case 0:                                             /* CSR */
        if ((data & CSR_IEN) && ((vc_csr & CSR_IEN) == 0)) {
            sim_cancel (&vc_unit);                      /* reactivate with short delay */
            sim_activate (&vc_unit, VSYNC_TIME);        /* in case software checks for vsync */
            }
        vc_csr = (vc_csr & ~CSR_RW) | (data & CSR_RW);
        break;

    case 1:                                             /* Cursor X */
        vc_curx = data;
        break;

    case 2:                                             /* Mouse position */
        break;

    case 4:                                             /* CRTC addr ptr */
        vc_crtc_p = (vc_crtc_p & ~CRTCP_RW) | (data & CRTCP_RW);
        break;

    case 5:                                             /* CRTC data */
        crtc_rg = vc_crtc_p & CRTCP_REG;
        vc_crtc[crtc_rg] = data & BMASK;
        break;

    case 6:                                             /* ICDR */
        if (vc_intc.ptr == 8)                           /* IMR */
            vc_intc.imr = data & 0xFFFF;
        else if (vc_intc.ptr == 9)                      /* ACR */
            vc_intc.acr = data & 0xFFFF;
        else  
            vc_intc.vec[vc_intc.ptr] = data & 0xFFFF;   /* Vector */
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

extern jmp_buf save_env;
extern int32 p1;

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
int32 nval, i;
int32 sc;
uint32 scrln, bufln;
uint32 idx;

if (!vc_buf)                                            /* QVSS disabled? */
    MACH_CHECK (MCHK_WRITE);                            /* Invalid memory reference */

if (lnt < L_LONG) {
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    int32 t = vc_buf[rg];
    sc = (pa & 3) << 3;
    nval = ((val & mask) << sc) | (t & ~(mask << sc));
    }
else nval = val;

if (rg >= 0xFFF8) {                                     /* cursor image */
    idx = (pa << 3) & 0xFF;                             /* get byte index */
    for (i = 0; i < (lnt << 3); i++)
        vc_cur[idx++] = (val >> i) & 1;                 /* 1bpp to 8bpp */
    }
else if (rg >= 0xFE00) {                                /* scanline map */
    if (vc_buf[rg] != nval) {
        scrln = (pa >> 1) & 0x3FF;                      /* screen line */
        sc = (scrln & 1) ? 16 : 0;                      /* odd line? (upper word) */
        bufln = (nval >> sc) & 0x7FF;                   /* buffer line */
        vc_map[scrln] = bufln;                          /* update map */

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
        vc_map[scrln] = vc_map[scrln] & ~VCMAP_VLD;     /* invalidate map */
        }
    }
vc_buf[rg] = nval;
}

SIM_INLINE void vc_invalidate (uint32 y1, uint32 y2)
{
uint32 ln;
for (ln = y1; ln < y2; ln++)
    vc_map[ln] = vc_map[ln] & ~VCMAP_VLD;               /* invalidate map entry */
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
        result = (vc_intc.vec[i] + VEC_Q);
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
uint32 line[1024];
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
else if ((vc_cur_x != CUR_X) || (vc_cur_f != CUR_F)) {  /* moved (X) or mask changed? */
    vc_invalidate (CUR_Y, (CUR_Y + 16));                /* invalidate new pos */
    }

vc_cur_x = CUR_X;                                       /* store cursor data */
vc_cur_y = CUR_Y;
vc_cur_v = CUR_V;
vc_cur_f = CUR_F;

xpos = vc_mpos & 0xFF;                                  /* get current mouse position */
ypos = (vc_mpos >> 8) & 0xFF;
dx = vid_mouse_xrel;                                    /* get relative movement */
dy = vid_mouse_yrel;
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
vid_mouse_xrel = 0;                                     /* reset counters for next poll */
vid_mouse_yrel = 0;

vc_csr |= (CSR_MSA | CSR_MSB | CSR_MSC);                /* reset button states */
if (vid_mouse_b3)                                       /* set new button states */
    vc_csr &= ~CSR_MSA;
if (vid_mouse_b2)
    vc_csr &= ~CSR_MSB;
if (vid_mouse_b1)
    vc_csr &= ~CSR_MSC;

for (ln = 0; ln < VC_YSIZE; ln++) {
    if ((vc_map[ln] & VCMAP_VLD) == 0) {                /* line invalid? */
        off = vc_map[ln] * 32;                          /* get video buf offet */
        for (col = 0; col < 1024; col++)  
            line[col] = vid_mono_palette[(vc_buf[off + (col >> 5)] >> (col & 0x1F)) & 1];
                                                        /* 1bpp to 32bpp */
        if (CUR_V) {                                    /* cursor visible? */
            if ((ln >= CUR_Y) && (ln < (CUR_Y + 16))) { /* cursor on this line? */
                cur = &vc_cur[((ln - CUR_Y) << 4)];     /* get image base */
                for (col = 0; col < 16; col++) {
                    if ((CUR_X + col) >= 1024)          /* Part of cursor off screen? */
                        continue;                       /* Skip */
                    if (CUR_F)                          /* mask function */
                        line[CUR_X + col] = vid_mono_palette[(line[CUR_X + col] == vid_mono_palette[1]) | (cur[col] & 1)];
                    else
                        line[CUR_X + col] = vid_mono_palette[(line[CUR_X + col] == vid_mono_palette[1]) & (~cur[col] & 1)];
                    }
                }
            }
        vid_draw (0, ln, 1024, 1, &line[0]);            /* update line */
        updated = TRUE;
        vc_map[ln] = vc_map[ln] | VCMAP_VLD;            /* set valid */
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
    return vid_close ();
    }

if (!vid_active)  {
    r = vid_open (dptr, VC_XSIZE, VC_YSIZE);            /* display size */
    if (r != SCPE_OK)
        return r;
    vc_buf = (uint32 *) calloc (VC_MEMSIZE, sizeof (uint32));
    if (vc_buf == NULL) {
        vid_close ();
        return SCPE_MEM;
        }
    printf ("QVSS Display Created.  ");
    vid_show_release_key (stdout, NULL, 0, NULL);
    printf ("\n");
    if (sim_log) {
        fprintf (sim_log, "QVSS Display Created.  ");
        vid_show_release_key (sim_log, NULL, 0, NULL);
        fprintf (sim_log, "\n");
        }
    }
sim_activate_abs (&vc_unit, tmxr_poll);
return auto_config (NULL, 0);                           /* run autoconfig */
}

t_stat vc_set_enable (UNIT *uptr, int32 val, char *cptr, void *desc)
{
return cpu_set_model (NULL, 0, (val ? "VAXSTATION" : "MICROVAX"), NULL);
}

t_stat vc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "VCB01 Monochrome Video Subsystem (%s)\n\n", dptr->name);
fprintf (st, "Use the Control-Right-Shift key combination to regain focus from the simulated\n");
fprintf (st, "video display\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

char *vc_description (DEVICE *dptr)
{
return "VCB01 Monochrome Graphics Adapter";
}
