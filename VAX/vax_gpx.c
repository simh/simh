/* vax_gpx.h: GPX video common components

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
*/

#if !defined(VAX_620)

#include "vax_gpx.h"

#define VA_FIFOSIZE     64

struct vdp_t {
    uint32 rg[0x18];
    };

typedef struct vdp_t VDP;

int32 va_adp[ADP_NUMREG];                               /* Address processor registers */
uint32 va_adp_fifo[VA_FIFOSIZE];                        /* ADP FIFO */
uint32 va_adp_fifo_wp;                                  /* write pointer */
uint32 va_adp_fifo_rp;                                  /* read pointer */
uint32 va_adp_fifo_sz;                                  /* data size */

VDP va_vdp[8];                                          /* 8 video processors */
uint32 va_ucs = 0;                                      /* update chip select */
uint32 va_scs = 0;                                      /* scroll chip select */

typedef struct {
    int32 x;
    int32 y;
    int32 dx;
    int32 dy;
    int32 err;
    int32 xstep;
    int32 ystep;
    int32 pix;
    int32 spix;
} VA_LINE;

VA_LINE s1_slow, s1_fast, dst_slow, dst_fast;
VA_LINE s2_slow, s2_fast;
int32 dx, dy;
int32 s2_pixf, s2_pixs;
uint32 s2_xmask, s2_ymask;
DEVICE *gpx_dev;

const char *va_adp_rgd[] = {                            /* address processor registers */
    "Address Counter",
    "Request Enable",
    "Interrupt Enable",
    "Status",
    "Reserved - Test Function 1",
    "Spare",
    "Reserved - Test Function 2",
    "I/D Data",
    "Command",
    "Mode",
    "Command",
    "Reserved - Test Function 3",
    "I/D Scroll Data",
    "I/D Scroll Command",
    "Scroll X Min",
    "Scroll X Max",
    "Scroll Y Min",
    "Scroll Y Max",
    "Pause",
    "Y Offset",
    "Y Scroll Constant",
    "Pending X Index",
    "Pending Y Index",
    "New X Index",
    "New Y Index",
    "Old X Index",
    "Old Y Index",
    "Clip X Min",
    "Clip X Max",
    "Clip Y Min",
    "Clip Y Max",
    "Spare",
    "Fast Source 1 DX",
    "Slow Source 1 DY",
    "Source 1 X Origin",
    "Source 1 Y Origin",
    "Destination X Origin",
    "Destination Y Origin",
    "Fast Destination DX",
    "Fast Destination DY",
    "Slow Destination DX",
    "Slow Destination DY",
    "Fast Scale",
    "Slow Scale",
    "Source 2 X Origin",
    "Source 2 Y Origin",
    "Source 2 Height & Width",
    "Error 1",
    "Error 2",
    "Y Scan Count 0",
    "Y Scan Count 1",
    "Y Scan Count 2",
    "Y Scan Count 3",
    "X Scan Configuration",
    "X Limit",
    "Y Limit",
    "X Scan Count 0",
    "X Scan Count 1",
    "X Scan Count 2",
    "X Scan Count 3",
    "X Scan Count 4",
    "X Scan Count 5",
    "X Scan Count 6",
    "Sync Phase"
    };

const char *va_vdp_rgd[] = {                            /* video processor registers */
    "Resolution Mode",
    "Bus Width",
    "Scroll Constant",
    "Plane Address",
    "Logic Function 0",
    "Logic Function 1",
    "Logic Function 2",
    "Logic Function 3",
    "Mask 1",
    "Mask 2",
    "Source",
    "Fill",
    "Left Scroll Boundary",
    "Right Scroll Boundary",
    "Background Colour",
    "Foreground Colour",
    "CSR0",
    "CSR1",
    "CSR2",
    "Reserved",
    "CSR4",
    "CSR5",
    "CSR6",
    "Reserved"
    };

const char *va_fnc[] = {                                /* logic functions */
    "ZEROs",
    "NOT (D OR S)",
    "NOT (D) AND S",
    "NOT (D)",
    "D AND NOT (S)",
    "NOT (S)",
    "D XOR S",
    "NOT (D AND S)",
    "D AND S",
    "NOT (D XOR S)",
    "S",
    "NOT (S) OR S",
    "D",
    "D OR NOT (S)",
    "D OR S",
    "ONEs"
    };

void va_adpstat (uint32 set, uint32 clr);
void va_fifo_clr (void);
void va_cmd (int32 cmd);
void va_scmd (int32 cmd);
void va_fill_setup (void);
void va_adp_setup (void);
void va_erase (uint32 x0, uint32 x1, uint32 y0, uint32 y1);

void va_adpstat (uint32 set, uint32 clr)
{
uint32 chg = (va_adp[ADP_STAT] ^ set) & set;

if (va_adp[ADP_INT] & set)                              /* unmasked ints 0->1? */
    va_setint (INT_ADP);
va_adp[ADP_STAT] = va_adp[ADP_STAT] | set;
va_adp[ADP_STAT] = va_adp[ADP_STAT] & ~clr;
}

void va_fifo_clr (void)
{
sim_debug (DBG_FIFO, gpx_dev, "va_fifo_clr\n");
va_adp_fifo[0] = 0;                                     /* clear top word */
va_adp_fifo_wp = 0;                                     /* reset pointers */
va_adp_fifo_rp = 0;
va_adp_fifo_sz = 0;                                     /* empty */
va_adpstat (ADPSTAT_ITR, ADPSTAT_IRR);
}

void va_fifo_wr (uint32 val)
{
if (va_adp[ADP_STAT] & ADPSTAT_AC)                      /* addr output complete? */
    va_fifo_clr ();
sim_debug (DBG_FIFO, gpx_dev, "fifo_wr: %d, %X (%d) at %08X\n",
    va_adp_fifo_wp, val, (va_adp_fifo_sz + 1), fault_PC);
va_adp_fifo[va_adp_fifo_wp++] = val;                    /* store value */
if (va_adp_fifo_wp == VA_FIFOSIZE)                      /* pointer wrap? */
    va_adp_fifo_wp = 0;
va_adp_fifo_sz++;

va_adpstat (ADPSTAT_IRR, 0);                            /* I/D data rcv rdy */

if (va_adp_fifo_sz < VA_FIFOSIZE)                       /* space in FIFO? */
    va_adpstat (ADPSTAT_ITR, 0);                        /* I/D data xmt rdy */
else
    va_adpstat (0, ADPSTAT_ITR);                        /* I/D data xmt not rdy */
}

uint32 va_fifo_rd (void)
{
uint32 val;

if (va_adp_fifo_sz == 0)                                /* reading empty fifo */
    return 0;                                           /* should not get here */
val = va_adp_fifo[va_adp_fifo_rp++];                    /* get value */
sim_debug (DBG_FIFO, gpx_dev, "fifo_rd: %d, %X (%d) at %08X\n",
    (va_adp_fifo_rp - 1), val, va_adp_fifo_sz, fault_PC);
if (va_adp_fifo_rp == VA_FIFOSIZE)                      /* pointer wrap? */
    va_adp_fifo_rp = 0;
va_adp_fifo_sz--;

va_adpstat (ADPSTAT_ITR, 0);                            /* I/D data xmt rdy */

if (va_adp_fifo_sz > 0)                                 /* data in FIFO? */
    va_adpstat (ADPSTAT_IRR, 0);                        /* I/D data rcv rdy */
else
    va_adpstat (0, ADPSTAT_IRR);                        /* I/D data rcv not rdy */
return val;
}

/* ADP Register descriptions on page 3-58 */

int32 va_adp_rd (int32 rg)
{
int32 data = 0;

switch (rg) {

    case ADP_ADCT:
        rg = va_adp[ADP_ADCT];
        data = va_adp[rg];
        va_adp[ADP_ADCT]++;
        va_adp[ADP_ADCT] = va_adp[ADP_ADCT] & 0x3F;
        break;

    case ADP_IDD:                                       /* I/D data */
        switch (va_unit[1].CMD) {

            case CMD_BTPX:
            case CMD_BTPZ:
                if (va_adp_fifo_sz == 0)
                    va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
                break;
                }
        data = va_fifo_rd ();
        switch (va_unit[1].CMD) {

            case CMD_BTPX:
            case CMD_BTPZ:
                if (va_adp_fifo_sz == 0)
                    va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
                break;
                }
        break;

    default:
        if (rg <= ADP_MAXREG)
            data = va_adp[rg];
        }

if (rg <= ADP_MAXREG)
    sim_debug (DBG_ADP, gpx_dev, "adp_rd: %s, %X at %08X\n", va_adp_rgd[rg], data, fault_PC);
else
    sim_debug (DBG_ADP, gpx_dev, "adp_rd: %X, %X at %08X\n", rg, data, fault_PC);

return data;
}

/* ADP Register descriptions on page 3-58 */

void va_adp_wr (int32 rg, int32 val)
{
if (rg == ADP_ADCT) {                                   /* special processing for address counter */
    if (va_adp[ADP_ADCT] == ADP_IDD) {                  /* write full word to I/D data */
        rg = ADP_IDD;
        va_adp[ADP_ADCT]++;
        }
    else if (va_adp[ADP_ADCT] == ADP_IDS) {             /* write full word to I/D scroll data */
        rg = ADP_IDS;
        va_adp[ADP_ADCT]++;
        }
    else if (val & 0x8000)                              /* update address counter */
        val = val & 0x3F;
    else {                                              /* write low 13 bits to pointed rg */
        rg = va_adp[ADP_ADCT];
        val = val & 0x3FFF;
        va_adp[ADP_ADCT]++;
        }
    va_adp[ADP_ADCT] = va_adp[ADP_ADCT] & 0x3F;
    }

if (rg <= ADP_MAXREG)
    sim_debug (DBG_ADP, gpx_dev, "adp_wr: %s, %X at %08X\n", va_adp_rgd[rg], val, fault_PC);
else
    sim_debug (DBG_ADP, gpx_dev, "adp_wr: %X, %X at %08X\n", rg, val, fault_PC);

switch (rg) {

    case ADP_STAT:
        va_adp[ADP_STAT] = va_adp[ADP_STAT] & ~(~val & ADPSTAT_W0C);
        va_adpstat (ADPSTAT_ISR, 0);                    /* FIXME: temp */
        break;

    case ADP_IDD:                                       /* I/D data */
        va_fifo_wr (val);
        switch (va_unit[1].CMD) {

            case CMD_PTBX:
            case CMD_PTBZ:
                va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
                break;
                }
        break;

    case ADP_PYSC:                                      /* y scroll constant */
        if (val & 0x2000)                               /* erase scroll region */
            va_erase (va_adp[ADP_PXMN], va_adp[ADP_PXMX], va_adp[ADP_PYMN], va_adp[ADP_PYMX]);
        else
            va_adp[rg] = (val | 0x8000);                /* set valid flag */
        break;

    case ADP_CMD1:                                      /* command */
    case ADP_CMD2:
        va_adp[ADP_CMD1] = val;
        va_cmd (val);
        break;

    case ADP_ICS:                                       /* I/D scroll command */
        va_adp[ADP_ICS] = val;
        va_scmd (val);
        break;

    case ADP_CXMN:                                      /* clip X min */
    case ADP_CXMX:                                      /* clip X max */
    case ADP_CYMN:                                      /* clip Y min */
    case ADP_CYMX:                                      /* clip Y max */
    case ADP_SXO:                                       /* source 1 X origin */
    case ADP_SYO:                                       /* source 1 Y origin */
    case ADP_DXO:                                       /* dest X origin */
    case ADP_DYO:                                       /* dest Y origin */
    case ADP_FSDX:                                      /* fast source 1 DX */
    case ADP_SSDY:                                      /* slow source 1 DY */
    case ADP_FDX:                                       /* fast dest DX */
    case ADP_FDY:                                       /* fast dest DY */
    case ADP_SDX:                                       /* slow dest DX */
    case ADP_SDY:                                       /* slow dest DY */
        if (val & 0x2000)
            val = val | 0xFFFFC000;                     /* sign extend */
        va_adp[rg] = val;
        break;

    default:
        if (rg <= ADP_MAXREG)
            va_adp[rg] = val;
        }
}

void va_vdp_wr (uint32 cn, uint32 rg, uint32 val)
{
VDP *vptr = &va_vdp[cn];

if (rg <= VDP_MAXREG) {
    sim_debug (DBG_VDP, gpx_dev, "vdp_wr: [%d], %s, %X at %08X\n", cn, va_vdp_rgd[rg], val, fault_PC);
    vptr->rg[rg] = val;
    if (rg == VDP_MSK1)
        vptr->rg[VDP_MSK2] = val;
    }
else
    sim_debug (DBG_VDP, gpx_dev, "vdp_wr: [%d], %X, %X at %08X\n", cn, rg, val, fault_PC);
}

/* Initialise line drawing */

void va_line_init (VA_LINE *ln, int32 dx, int32 dy, int32 pix)
{
ln->x = 0;
ln->y = 0;
ln->dx = dx;
ln->dy = dy;
ln->pix = pix;
ln->spix = pix;
ln->xstep = (dx < 0) ? -1 : 1;
ln->ystep = (dy < 0) ? -1 : 1;
ln->err = (abs(dx) > abs(dy)) ? (ln->xstep * -dx) : (ln->ystep * -dy);
}

/* Step to the next point on a line */

t_bool va_line_step (VA_LINE *ln)
{
if ((ln->dx == 0) && (ln->dy == 0))                     /* null line? */
    return TRUE;                                        /* done */
else if (ln->dx == 0) {                                 /* no X component? */
    ln->y = ln->y + ln->ystep;                          /* just step Y */
    ln->pix = ln->pix + (VA_XSIZE * ln->ystep);
    }
else if (ln->dy == 0) {                                 /* no Y component? */
    ln->x = ln->x + ln->xstep;                          /* just step X */
    ln->pix = ln->pix + ln->xstep;
    }
else {
    if (abs(ln->dx) > abs(ln->dy)) {                    /* determine major axis */
        ln->x = ln->x + ln->xstep;
        ln->pix = ln->pix + ln->xstep;
        ln->err = ln->err + (2 * ln->dy * ln->ystep);
        if (ln->err > 0) {
            ln->y = ln->y + ln->ystep;
            ln->pix = ln->pix + (VA_XSIZE * ln->ystep);
            ln->err = ln->err - (2 * ln->dx * ln->xstep);
            }
        }
    else {
        ln->y = ln->y + ln->ystep;
        ln->pix = ln->pix + (VA_XSIZE * ln->ystep);
        ln->err = ln->err + (2 * ln->dx * ln->xstep);
        if (ln->err > 0) {
            ln->x = ln->x + ln->xstep;
            ln->pix = ln->pix + ln->xstep;
            ln->err = ln->err - (2 * ln->dy * ln->ystep);
            }
        }
    }
ln->pix = ln->pix & VA_BUFMASK;                         /* wrap within video buffer */

if ((ln->x == ln->dx) && (ln->y == ln->dy)) {           /* finished? */
    ln->x = 0;
    ln->y = 0;
    ln->pix = ln->spix;
    return TRUE;                                        /* done */
    }
return FALSE;                                           /* more steps to do */
}

void va_viper_rop (int32 cn, uint32 sc, uint32 *pix)
{
uint32 cmd = va_adp[ADP_CMD1];
uint32 lu = (cmd >> 4) & 0x3;
uint32 fnc = va_vdp[cn].rg[VDP_FNC0 + lu];
int32 mask = (1u << va_vdp[cn].rg[VDP_PA]);

uint32 mask1 = (va_vdp[cn].rg[VDP_MSK1] >> sc) & 0x1;
uint32 mask2 = (va_vdp[cn].rg[VDP_MSK2] >> sc) & 0x1;
uint32 src = (va_vdp[cn].rg[VDP_SRC] >> sc) & 0x1;

uint32 dest = (*pix >> va_vdp[cn].rg[VDP_PA]) & 0x1;

if (fnc & 0x10)
    mask1 = ~mask1;
if (fnc & 0x20)
    mask2 = ~mask2;
if ((fnc & 0x40) == 0)
    src = ~src;

if ((mask1 & mask2 & 0x1) == 0)
    return;

switch (fnc & 0xF) {
    case 0x0:                                           /* ZEROs */
        dest = 0;
        break;

    case 0x1:                                           /* NOT (D OR S) */
        dest = ~(dest | src);
        break;

    case 0x2:                                           /* NOT (D) AND S */
        dest = ~(dest) & src;
        break;

    case 0x3:                                           /* NOT (D) */
        dest = ~(dest);
        break;

    case 0x4:                                           /* D AND NOT (S) */
        dest = dest & ~(src);
        break;

    case 0x5:                                           /* NOT (S) */
        dest = ~(src);
        break;

    case 0x6:                                           /* D XOR S */
        dest = dest ^ src;
        break;

    case 0x7:                                           /* NOT (D AND S) */
        dest = ~(dest & src);
        break;

    case 0x8:                                           /* D AND S */
        dest = (dest & src);
        break;

    case 0x9:                                           /* NOT (D XOR S) */
        dest = ~(dest ^ src);
        break;

    case 0xA:                                           /* S */
        dest = src;
        break;

    case 0xB:                                           /* NOT (S) OR S */
        dest = ~(src) | src;
        break;

    case 0xC:                                           /* D */
        break;

    case 0xD:                                           /* D OR NOT (S) */
        dest = dest | ~(src);
        break;

    case 0xE:                                           /* D OR S */
        dest = (dest | src);
        break;

    case 0xF:                                           /* ONEs */
        dest = 0xFFFF;
        break;
        }

if (dest & 0x1)
    dest = (va_vdp[cn].rg[VDP_FG] >> sc) & 0x1;
else
    dest = (va_vdp[cn].rg[VDP_BG] >> sc) & 0x1;
dest = (dest << va_vdp[cn].rg[VDP_PA]);
*pix = (*pix & ~mask) | (dest & mask);
}

t_stat va_fill (UNIT *uptr)
{
uint32 cmd = va_adp[ADP_CMD1];
int32 old_y, x0, x1;
int32 sel, cn;
int32 bs2 = -1;
t_bool clip;
uint32 s2_temp;
uint32 s2_csr;

if (cmd & 0x4)
    s2_csr = VDP_CSR5;
else
    s2_csr = VDP_CSR1;

for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) {
    if (sel & 1) {                                      /* chip selected? */
        if (cmd & 0x1000) {                             /* source 2 enabled? */
            if (va_vdp[cn].rg[s2_csr] & 0x10) {         /* broadcast enabled? */
                bs2 = cn;
                }
            }
        }
    }

for (;;) {
    x0 = (dst_slow.x + va_adp[ADP_DXO]);
    x1 = (s1_slow.x + va_adp[ADP_SXO]);
    sim_debug (DBG_ROP, gpx_dev, "Fill line %d from %d to %d\n", (dst_slow.y + dy), x0, x1);
    va_line_init (&dst_fast, (x1 - x0), 0, dst_slow.pix);

    for (;;) {
        if (cmd & 0x1000) {                             /* source 2 enabled? */
            s2_fast.x = (dst_fast.x + va_adp[ADP_DXO]) & s2_xmask;
            s2_slow.y = (dst_slow.y + va_adp[ADP_DYO]) & s2_ymask;
            s2_pixf = s2_pixs + (s2_slow.y * VA_XSIZE);
            s2_pixf = s2_pixf + s2_fast.x;
            s2_pixf = s2_pixf & VA_BUFMASK;
            sim_debug (DBG_ROP, gpx_dev, "Source 2 X: %d, Y: %d, pix: %X\n", s2_fast.x, s2_slow.y, va_buf[s2_pixf]);
            /* get source pixel and put in Viper source register */
            for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) { /* internal load */
                if (sel & 1) {                          /* chip selected? */
                    if ((va_vdp[cn].rg[s2_csr] & 0xC) == 0)
                        continue;
                    s2_temp = va_buf[s2_pixf];          /* FIXME: implement fast mode */
                    s2_temp >>= va_vdp[cn].rg[VDP_PA];
                    s2_temp <<= (dst_fast.x & 0xF);
                    switch (va_vdp[cn].rg[s2_csr] & 0xC) {
                        case 0x4:
                            va_vdp[cn].rg[VDP_SRC] = s2_temp;
                            break;
                        case 0x8:
                            va_vdp[cn].rg[VDP_MSK1] = s2_temp;
                            va_vdp[cn].rg[VDP_MSK2] = s2_temp;
                            break;
                        case 0xC:
                            va_vdp[cn].rg[VDP_MSK2] = s2_temp;
                            break;
                            }
                    }
                }
            if (bs2 >= 0) {
                s2_temp = va_buf[s2_pixf];              /* FIXME: implement fast mode */
                s2_temp >>= va_vdp[bs2].rg[VDP_PA];
                s2_temp <<= (dst_fast.x & 0xF);
                for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) { /* external load */
                    if (sel & 1) {                      /* chip selected? */
                        if ((va_vdp[cn].rg[s2_csr] & 0x3) == 0)
                            continue;
                        switch (va_vdp[cn].rg[s2_csr] & 0x3) {
                            case 0x1:
                                va_vdp[cn].rg[VDP_SRC] = s2_temp;
                                break;
                            case 0x2:
                                va_vdp[cn].rg[VDP_MSK1] = s2_temp;
                                va_vdp[cn].rg[VDP_MSK2] = s2_temp;
                                break;
                            case 0x3:
                                va_vdp[cn].rg[VDP_MSK2] = s2_temp;
                                break;
                                }
                        }
                    }
                }
            }
        clip = FALSE;
        if ((dst_slow.x + dst_fast.x + dx) < va_adp[ADP_CXMN]) {
            va_adp[ADP_STAT] |= ADPSTAT_CL;
            clip = TRUE;
            }
        else if ((dst_slow.x + dst_fast.x + dx) > va_adp[ADP_CXMX]) {
            va_adp[ADP_STAT] |= ADPSTAT_CR;
            clip = TRUE;
            }
        if ((dst_slow.y + dst_fast.y + dy) < va_adp[ADP_CYMN]) {
            va_adp[ADP_STAT] |= ADPSTAT_CT;
            clip = TRUE;
            }
        else if ((dst_slow.y + dst_fast.y + dy) > va_adp[ADP_CYMX]) {
            va_adp[ADP_STAT] |= ADPSTAT_CB;
            clip = TRUE;
            }
        if ((cmd & 0x400) && (va_adp[ADP_MDE] & 0x80) && !clip) {    /* dest enabled, pen down? */
            /* Call all enabled Vipers to process the current pixel */
            for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) {
                if (sel & 1)                            /* chip selected? */
                    va_viper_rop (cn, (dst_fast.x & 0xF), &va_buf[dst_fast.pix]);
                }
            sim_debug (DBG_ROP, gpx_dev, "-> Dest X: %d, Y: %d, pix: %X\n", dst_fast.x, dst_slow.y, va_buf[dst_fast.pix]);
            va_updated[dst_slow.y + dst_fast.y + dy] = TRUE;
            }

        if (va_line_step (&dst_fast))                   /* fast vector exhausted? */
            break;
        }

    for (old_y = dst_slow.y; dst_slow.y == old_y;) {    /* step vector A */
        if (va_line_step (&dst_slow)) {
            if ((va_adp[ADP_STAT] & ADPSTAT_CP) == 0)
                va_adp[ADP_STAT] |= ADPSTAT_CN;
            sim_debug (DBG_ROP, gpx_dev, "Fill Complete\n");
            uptr->CMD = 0;
            va_adpstat (ADPSTAT_AC | ADPSTAT_RC, 0);
            return SCPE_OK;
            }
        }
    for (old_y = s1_slow.y; s1_slow.y == old_y;) {      /* step vector B */
        if (va_line_step (&s1_slow)) {
            if ((va_adp[ADP_STAT] & ADPSTAT_CP) == 0)
                va_adp[ADP_STAT] |= ADPSTAT_CN;
            sim_debug (DBG_ROP, gpx_dev, "Fill Complete\n");
            uptr->CMD = 0;
            va_adpstat (ADPSTAT_AC | ADPSTAT_RC, 0);
            return SCPE_OK;
            }
        }
    }
}

t_stat va_rop (UNIT *uptr)
{
uint32 cmd = va_adp[ADP_CMD1];
int32 sel, cn;
int32 bs1 = -1;
int32 bs2 = -1;
t_bool clip, scale, wrap;
uint32 s1_temp;
uint32 s2_temp;
uint32 s1_csr;
uint32 s2_csr;
uint32 acf = 0;                                         /* fast scale accumulator */
uint32 acs = 0;                                         /* slow scale accumulator */

scale = FALSE;
if ((va_adp[ADP_FS] & 0x1FFF) != 0x1FFF)                /* fast scale != unity? */
    scale = TRUE;                                       /* enable scaling */
if ((va_adp[ADP_SS] & 0x1FFF) != 0x1FFF)                /* slow scale != unity? */
    scale = TRUE;                                       /* enable scaling */

if (cmd & 0x4) {
    s1_csr = VDP_CSR4;                                  /* CSR bank 2 */
    s2_csr = VDP_CSR5;
    }
else {
    s1_csr = VDP_CSR0;                                  /* CSR bank 1 */
    s2_csr = VDP_CSR1;
    }

for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) {
    if (sel & 1) {                                      /* chip selected? */
        if (cmd & 0x800) {                              /* source 1 enabled? */
            if (va_vdp[cn].rg[s1_csr] & 0x10) {         /* broadcast enabled? */
                bs1 = cn;
                }
            }
        if (cmd & 0x1000) {                             /* source 2 enabled? */
            if (va_vdp[cn].rg[s2_csr] & 0x10) {         /* broadcast enabled? */
                bs2 = cn;
                }
            }
        }
    }

for (;;) {
    if (cmd & 0x800) {                                  /* source 1 enabled? */
        sim_debug (DBG_ROP, gpx_dev, "Source X: %d, Y: %d, pix: %X\n", s1_fast.x, s1_slow.y, va_buf[s1_fast.pix]);
        /* get source pixel and put in Viper source register */
        for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) { /* internal load */
            if (sel & 1) {                              /* chip selected? */
                if ((va_vdp[cn].rg[s1_csr] & 0xC) == 0)
                    continue;
                s1_temp = va_buf[s1_fast.pix];          /* FIXME: implement fast mode */
                s1_temp >>= va_vdp[cn].rg[VDP_PA];
                s1_temp <<= (dst_fast.x & 0xF);
                switch (va_vdp[cn].rg[s1_csr] & 0xC) {
                    case 0x4:
                        va_vdp[cn].rg[VDP_SRC] = s1_temp;
                        break;
                    case 0x8:
                        va_vdp[cn].rg[VDP_MSK1] = s1_temp;
                        va_vdp[cn].rg[VDP_MSK2] = s1_temp;
                        break;
                    case 0xC:
                        va_vdp[cn].rg[VDP_MSK2] = s1_temp;
                        break;
                        }
                }
            }
        if (bs1 >= 0) {
            s1_temp = va_buf[s1_fast.pix];              /* FIXME: implement fast mode */
            s1_temp >>= va_vdp[bs1].rg[VDP_PA];
            s1_temp <<= (dst_fast.x & 0xF);
            for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) { /* external load */
                if (sel & 1) {                          /* chip selected? */
                    if ((va_vdp[cn].rg[s1_csr] & 0x3) == 0)
                        continue;
                    switch (va_vdp[cn].rg[s1_csr] & 0x3) {
                        case 0x1:
                            va_vdp[cn].rg[VDP_SRC] = s1_temp;
                            break;
                        case 0x2:
                            va_vdp[cn].rg[VDP_MSK1] = s1_temp;
                            va_vdp[cn].rg[VDP_MSK2] = s1_temp;
                            break;
                        case 0x3:
                            va_vdp[cn].rg[VDP_MSK2] = s1_temp;
                            break;
                            }
                    }
                }
            }
        }
    if (cmd & 0x1000) {                                 /* source 2 enabled? */
        s2_fast.x = (dst_fast.x + va_adp[ADP_DXO]) & s2_xmask;
        s2_slow.y = (dst_slow.y + va_adp[ADP_DYO]) & s2_ymask;
        s2_pixf = s2_pixs + (s2_slow.y * VA_XSIZE);
        s2_pixf = s2_pixf + s2_fast.x;
        s2_pixf = s2_pixf & VA_BUFMASK;
        sim_debug (DBG_ROP, gpx_dev, "Source 2 X: %d, Y: %d, pix: %X\n", s2_fast.x, s2_slow.y, va_buf[s2_pixf]);
        /* get source pixel and put in Viper source register */
        for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) { /* internal load */
            if (sel & 1) {                              /* chip selected? */
                if ((va_vdp[cn].rg[s2_csr] & 0xC) == 0)
                    continue;
                s2_temp = va_buf[s2_pixf];              /* FIXME: implement fast mode */
                s2_temp >>= va_vdp[cn].rg[VDP_PA];
                s2_temp <<= (dst_fast.x & 0xF);
                switch (va_vdp[cn].rg[s2_csr] & 0xC) {
                    case 0x4:
                        va_vdp[cn].rg[VDP_SRC] = s2_temp;
                        break;
                    case 0x8:
                        va_vdp[cn].rg[VDP_MSK1] = s2_temp;
                        va_vdp[cn].rg[VDP_MSK2] = s2_temp;
                        break;
                    case 0xC:
                        va_vdp[cn].rg[VDP_MSK2] = s2_temp;
                        break;
                        }
                }
            }
        if (bs2 >= 0) {
            s2_temp = va_buf[s2_pixf];                  /* FIXME: implement fast mode */
            s2_temp >>= va_vdp[bs2].rg[VDP_PA];
            s2_temp <<= (dst_fast.x & 0xF);
            for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) { /* external load */
                if (sel & 1) {                          /* chip selected? */
                    if ((va_vdp[cn].rg[s2_csr] & 0x3) == 0)
                        continue;
                    switch (va_vdp[cn].rg[s2_csr] & 0x3) {
                        case 0x1:
                            va_vdp[cn].rg[VDP_SRC] = s2_temp;
                            break;
                        case 0x2:
                            va_vdp[cn].rg[VDP_MSK1] = s2_temp;
                            va_vdp[cn].rg[VDP_MSK2] = s2_temp;
                            break;
                        case 0x3:
                            va_vdp[cn].rg[VDP_MSK2] = s2_temp;
                            break;
                            }
                    }
                }
            }
        }
    clip = FALSE;
    if ((dst_slow.x + dst_fast.x + dx) < va_adp[ADP_CXMN]) {
        va_adp[ADP_STAT] |= ADPSTAT_CL;
        clip = TRUE;
        }
    else if ((dst_slow.x + dst_fast.x + dx) > va_adp[ADP_CXMX]) {
        va_adp[ADP_STAT] |= ADPSTAT_CR;
        clip = TRUE;
        }
    if ((dst_slow.y + dst_fast.y + dy) < va_adp[ADP_CYMN]) {
        va_adp[ADP_STAT] |= ADPSTAT_CT;
        clip = TRUE;
        }
    else if ((dst_slow.y + dst_fast.y + dy) > va_adp[ADP_CYMX]) {
        va_adp[ADP_STAT] |= ADPSTAT_CB;
        clip = TRUE;
        }
    if ((cmd & 0x400) && (va_adp[ADP_MDE] & 0x80) && !clip) {    /* dest enabled, pen down? */
        /* Call all enabled Vipers to process the current pixel */
        for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) {
            if (sel & 1)                                /* chip selected? */
                va_viper_rop (cn, (dst_fast.x & 0xF), &va_buf[dst_fast.pix]);
            }
        sim_debug (DBG_ROP, gpx_dev, "-> Dest X: %d, Y: %d, pix: %X\n", dst_fast.x, dst_slow.y, va_buf[dst_fast.pix]);
        va_updated[dst_slow.y + dst_fast.y + dy] = TRUE;
        }

    if ((va_adp[ADP_MDE] & 3) == 2) {                   /* linear pattern mode? */
        if (cmd & 0x800)                                /* source 1 enabled? */
            (void)va_line_step (&s1_fast);              /* step fast vector */
        if (va_line_step (&dst_fast)) {                 /* fast vector exhausted? */
            if (va_line_step (&dst_slow))               /* slow vector exhausted? */
                break;                                  /* finished */
            if (cmd & 0x800) {                          /* source 1 enabled? */
                (void)va_line_step (&s1_slow);          /* step slow vector */
                s1_fast.pix = s1_slow.pix;
                s1_fast.spix = s1_slow.pix;
                }
            dst_fast.pix = dst_slow.pix;
            }
        }
    else {
        if (cmd & 0x800) {                              /* source 1 enabled? */
            if (scale) {
                acf = acf + (va_adp[ADP_FS] & 0x1FFF) + 1; /* increment fast accumulator */
                wrap = FALSE;
                if ((va_adp[ADP_FS] & 0x2000) || (acf & 0x2000)) /* all but upscaling, no overflow */
                    wrap = wrap | va_line_step (&s1_fast); /* fast vector exhausted? */
                if (((va_adp[ADP_FS] & 0x2000) == 0) || (acf & 0x2000)) /* all but downscaling, no overflow */
                    wrap = wrap | va_line_step (&dst_fast); /* fast vector exhausted? */
                if (wrap) {
                    acs = acs + (va_adp[ADP_SS] & 0x1FFF) + 1; /* increment slow accumulator */
                    if ((va_adp[ADP_SS] & 0x2000) || (acs & 0x2000)) { /* all but upscaling, no overflow */
                        if (va_line_step (&s1_slow))    /* slow vector exhausted? */
                            break;                      /* finished */
                        }
                    s1_fast.x = 0;
                    s1_fast.y = 0;
                    s1_fast.pix = s1_slow.pix;
                    if (((va_adp[ADP_FS] & 0x2000) == 0) || (acf & 0x2000)) { /* all but downscaling, no overflow */
                        if (va_line_step (&dst_slow))   /* slow vector exhausted? */
                            break;                      /* finished */
                        }
                    dst_fast.x = 0;
                    dst_fast.y = 0;
                    dst_fast.pix = dst_slow.pix;
                    acf = 0;
                    }
                acf = acf & 0x1FFF;                     /* clear overflow bits */
                acs = acs & 0x1FFF;
                }
            else {
                if (va_line_step (&s1_fast)) {          /* fast vector exhausted? */
                    if (va_line_step (&s1_slow))        /* slow vector exhausted? */
                        break;                          /* finished */
                    s1_fast.pix = s1_slow.pix;
                    }
                if (va_line_step (&dst_fast)) {         /* fast vector exhausted? */
                    if (va_line_step (&dst_slow))       /* slow vector exhausted? */
                        break;                          /* finished */
                    dst_fast.pix = dst_slow.pix;
                    }
                }
            }
        else {
            if (va_line_step (&dst_fast)) {             /* fast vector exhausted? */
                if (va_line_step (&dst_slow))           /* slow vector exhausted? */
                    break;                              /* finished */
                dst_fast.pix = dst_slow.pix;
                }
            }
        }
    }
if ((va_adp[ADP_STAT] & ADPSTAT_CP) == 0)
    va_adp[ADP_STAT] |= ADPSTAT_CN;
sim_debug (DBG_ROP, gpx_dev, "ROP Complete\n");
uptr->CMD = 0;
va_adpstat (ADPSTAT_AC | ADPSTAT_RC, 0);
return SCPE_OK;
}

void va_cmd (int32 cmd)
{
uint32 sel, cn, val, rg;
uint32 adp_opc = (cmd >> 8) & 0x7;
uint32 lu;

/* Commands on page 3-74 */

switch (adp_opc) {                                      /* address processor opcode */

    case 0:                                             /* cancel */
        sim_debug (DBG_ROP, gpx_dev, "Command: Cancel\n");
        va_adpstat (0, ADPSTAT_ITR);
        va_unit[1].CMD = CMD_NOP;
        va_adpstat (ADPSTAT_IC|ADPSTAT_RC|ADPSTAT_AC, 0); /* addr output complete */
        va_fifo_clr ();
        return;

    case 1:                                             /* register load */
        /* Video processor chip registers on page 3-82 */
        if (cmd & 0x80) {
            if (cmd & 0x20) {                           /* I/D Bus Z-Axis Register Load */
                rg = ((cmd >> 2) & 3);
                val = va_fifo_rd ();
                sim_debug (DBG_VDP, gpx_dev, "vdp_wr: z-reg[%X, %X] = %X\n", rg, (cmd & 0x3), val);
                switch (rg) {
                    case 0:
                        rg = VDP_SRC;
                        break;
                    case 1:
                        rg = VDP_FG;
                        break;
                    case 2:
                        rg = VDP_FILL;
                        break;
                    case 3:
                        rg = VDP_BG;
                        break;
                        }
                for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) {
                    if (sel & 1) {                      /* chip selected? */
                        if (val & (1u << cn))
                            va_vdp_wr (cn, rg, 0xFFFF);
                        else
                            va_vdp_wr (cn, rg, 0);
                        }
                    }
                }
            else {                                      /* I/D Bus Video Processor Register Load */
                rg = (cmd & 0x1F);
                val = va_fifo_rd ();
                for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) {
                    if (sel & 1)                        /* chip selected? */
                        va_vdp_wr (cn, rg, val);
                    }
                }
            }
        else {                                          /* I/D Bus External Register Load */
            switch (cmd & 0xff) {
                case 0x40:                              /* scroll chip select */
                    va_scs = va_fifo_rd ();
                    va_scs = va_scs & VA_PLANE_MASK;
                    sim_debug (DBG_VDP, gpx_dev, "scs_sel: %X (%X) at %08X\n", va_scs, (cmd & 0x7F), fault_PC);
                    break;

                case 0x60:                              /* update chip select (green update mask) */
                    va_ucs = va_fifo_rd ();
                    va_ucs = va_ucs & VA_PLANE_MASK;
                    sim_debug (DBG_VDP, gpx_dev, "ucs_sel: %X (%X) at %08X\n", va_ucs, (cmd & 0x7F), fault_PC);
                    break;

                case 0x30:                              /* red update mask */
                    break;

                case 0x18:                              /* blue update mask */
                    break;
                }
            }
        return;

    case 3:                                             /* bitmap to processor */
        sim_debug (DBG_ROP, gpx_dev, "Command: BTP\n");
        sim_debug (DBG_ROP, gpx_dev, "   Mode: %s\n", (cmd & 0x40) ? "X-Mode" : "Z-Mode");
        sim_debug (DBG_ROP, gpx_dev, "   Select: %X\n", va_ucs);
        sim_debug (DBG_ROP, gpx_dev, "   X Index: %d\n", va_adp[ADP_NXI]);
        sim_debug (DBG_ROP, gpx_dev, "   Y Index: %d\n", va_adp[ADP_NYI]);
        sim_debug (DBG_ROP, gpx_dev, "   Source 1 Indexing: %s\n", (va_adp[ADP_MDE]& 0x20) ? "Enabled" : "Disabled");
        sim_debug (DBG_ROP, gpx_dev, "   Source 1 X Origin: %d\n", va_adp[ADP_SXO]);
        sim_debug (DBG_ROP, gpx_dev, "   Source 1 Y Origin: %d\n", va_adp[ADP_SYO]);
        sim_debug (DBG_ROP, gpx_dev, "   Fast Source 1 DX: %d\n", va_adp[ADP_FSDX]);
        sim_debug (DBG_ROP, gpx_dev, "   Slow Source 1 DY: %d\n", va_adp[ADP_SSDY]);
        sim_debug (DBG_ROP, gpx_dev, "   Fast Scale: %d\n", va_adp[ADP_FS]);
        sim_debug (DBG_ROP, gpx_dev, "   Slow Scale: %d\n", va_adp[ADP_SS]);
        
        va_fifo_clr ();
        va_adpstat (ADPSTAT_IC, (ADPSTAT_AC | ADPSTAT_RC));
        if (cmd & 0x40)
            va_unit[1].CMD = CMD_BTPX;                  /* X-Mode */
        else
            va_unit[1].CMD = CMD_BTPZ;                  /* Z-Mode */
        va_adp_setup ();
        if (va_adp[ADP_STAT] & ADPSTAT_ITR)             /* space in FIFO? */
            va_btp (&va_unit[1], (va_unit[1].CMD == CMD_BTPZ));
        return;

    case 6:                                             /* rasterop */
        lu = (cmd >> 4) & 0x3;                          /* get logic unit */
        sim_debug (DBG_ROP, gpx_dev, "Command: ROP\n");
        sim_debug (DBG_ROP, gpx_dev, "   Mode: %s\n", (cmd & 0x40) ? "X-Mode" : "Z-Mode");
        sim_debug (DBG_ROP, gpx_dev, "   Select: %X\n", va_ucs);
        sim_debug (DBG_ROP, gpx_dev, "   Source 1: %s\n", (cmd & 0x800) ? "Enabled" : "Disabled");
        sim_debug (DBG_ROP, gpx_dev, "   Source 2: %s\n", (cmd & 0x1000) ? "Enabled" : "Disabled");
        sim_debug (DBG_ROP, gpx_dev, "   Clip: (%d, %d, %d, %d)\n", va_adp[ADP_CXMN], va_adp[ADP_CYMN], va_adp[ADP_CXMX], va_adp[ADP_CYMX]);
        switch (va_adp[ADP_MDE] & 0x3) {
            case 0:
                sim_debug (DBG_ROP, gpx_dev, "   Mode: Normal\n");
                break;
            case 1:
                sim_debug (DBG_ROP, gpx_dev, "   Mode: Reserved\n");
                break;
            case 2:
                sim_debug (DBG_ROP, gpx_dev, "   Mode: Linear Pattern\n");
                break;
            case 3:
                sim_debug (DBG_ROP, gpx_dev, "   Mode: Fill (%s, %s)\n", (va_adp[ADP_MDE] & 0x4) ? "Y" : "X",
                    (va_adp[ADP_MDE] & 0x8) ? "Baseline" : "Normal");
                break;
                }
        sim_debug (DBG_ROP, gpx_dev, "   Hole Fill: %s\n", (va_adp[ADP_MDE] & 0x10) ? "Enabled" : "Disabled");
        sim_debug (DBG_ROP, gpx_dev, "   Pen: %s\n", (va_adp[ADP_MDE] & 0x80) ? "Down" : "Up");
        sim_debug (DBG_ROP, gpx_dev, "   Logic Unit: %d\n", lu);
        rg = (cmd & 0x4);
        for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) {
            if (sel & 1) {                              /* chip selected? */
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Function: %s\n", cn, va_fnc[va_vdp[cn].rg[0x4 + lu] & 0xF]);
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Mask 1: %04X (%s)\n", cn, va_vdp[cn].rg[VDP_MSK1], (va_vdp[cn].rg[0x4 + lu] & 0x10) ? "Complement" : "Enabled");
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Mask 2: %04X (%s)\n", cn, va_vdp[cn].rg[VDP_MSK2], (va_vdp[cn].rg[0x4 + lu] & 0x20) ? "Complement" : "Enabled");
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Source: %04X (%s)\n", cn, va_vdp[cn].rg[VDP_SRC], (va_vdp[cn].rg[0x4 + lu] & 0x40) ? "Enabled" : "Complement");
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Resolution Mode: %s\n", cn, (va_vdp[cn].rg[0x4 + lu] & 0x40) ? "Disabled" : "Enabled");
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Foreground: %04X\n", cn, va_vdp[cn].rg[VDP_FG]);
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Background: %04X\n", cn, va_vdp[cn].rg[VDP_BG]);
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Fill: %04X\n", cn, va_vdp[cn].rg[VDP_FILL]);
                if (va_vdp[cn].rg[VDP_CSR0 + rg] & 0x10) {
                    sim_debug (DBG_ROP, gpx_dev, "      [%d] Broadcast: Enabled\n", cn);
                    }
                if (va_vdp[cn].rg[VDP_CSR1 + rg] & 0x10) {
                    sim_debug (DBG_ROP, gpx_dev, "      [%d] S2 Broadcast: Enabled\n", cn);
                    }
                if (cmd & 0x800) {                      /* source 1 enabled? */
                    switch (va_vdp[cn].rg[VDP_CSR0 + rg] & 0xC) {
                        case 0x0:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 1 Internal: None\n", cn);
                            break;
                        case 0x4:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 1 Internal: Source\n", cn);
                            break;
                        case 0x8:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 1 Internal: Mask 1 & 2\n", cn);
                            break;
                        case 0xC:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 1 Internal: Mask 2\n", cn);
                            break;
                            }
                    switch (va_vdp[cn].rg[VDP_CSR0 + rg] & 0x3) {
                        case 0x0:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 1 External: None\n", cn);
                            break;
                        case 0x1:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 1 External: Source\n", cn);
                            break;
                        case 0x2:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 1 External: Mask 1 & 2\n", cn);
                            break;
                        case 0x3:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 1 External: Mask 2\n", cn);
                            break;
                            }
                    }
                if (cmd & 0x1000) {                      /* source 2 enabled? */
                    switch (va_vdp[cn].rg[VDP_CSR1 + rg] & 0xC) {
                        case 0x0:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 2 Internal: None\n", cn);
                            break;
                        case 0x4:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 2 Internal: Source\n", cn);
                            break;
                        case 0x8:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 2 Internal: Mask 1 & 2\n", cn);
                            break;
                        case 0xC:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 2 Internal: Mask 2\n", cn);
                            break;
                            }
                    switch (va_vdp[cn].rg[VDP_CSR1 + rg] & 0x3) {
                        case 0x0:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 2 External: None\n", cn);
                            break;
                        case 0x1:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 2 External: Source\n", cn);
                            break;
                        case 0x2:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 2 External: Mask 1 & 2\n", cn);
                            break;
                        case 0x3:
                            sim_debug (DBG_ROP, gpx_dev, "      [%d] Source 2 External: Mask 2\n", cn);
                            break;
                            }
                    }
                }
            }
        sim_debug (DBG_ROP, gpx_dev, "   X Index: %d\n", va_adp[ADP_NXI]);
        sim_debug (DBG_ROP, gpx_dev, "   Y Index: %d\n", va_adp[ADP_NYI]);
        if (cmd & 0x800) {
            sim_debug (DBG_ROP, gpx_dev, "   Source 1 Indexing: %s\n", (va_adp[ADP_MDE]& 0x20) ? "Enabled" : "Disabled");
            sim_debug (DBG_ROP, gpx_dev, "   Source 1 X Origin: %d\n", va_adp[ADP_SXO]);
            sim_debug (DBG_ROP, gpx_dev, "   Source 1 Y Origin: %d\n", va_adp[ADP_SYO]);
            sim_debug (DBG_ROP, gpx_dev, "   Fast Source 1 DX: %d\n", va_adp[ADP_FSDX]);
            sim_debug (DBG_ROP, gpx_dev, "   Slow Source 1 DY: %d\n", va_adp[ADP_SSDY]);
            }
        if (cmd & 0x1000) {
            sim_debug (DBG_ROP, gpx_dev, "   Source 2 X Origin: %d\n", va_adp[ADP_S2XO]);
            sim_debug (DBG_ROP, gpx_dev, "   Source 2 Y Origin: %d\n", va_adp[ADP_S2YO]);
            sim_debug (DBG_ROP, gpx_dev, "   Source 2 Height/Width: %04X\n", va_adp[ADP_S2HW]);
            }
        sim_debug (DBG_ROP, gpx_dev, "   Destination Indexing: %s\n", (va_adp[ADP_MDE]& 0x40) ? "Enabled" : "Disabled");
        sim_debug (DBG_ROP, gpx_dev, "   Destination X Origin: %d\n", va_adp[ADP_DXO]);
        sim_debug (DBG_ROP, gpx_dev, "   Destination Y Origin: %d\n", va_adp[ADP_DYO]);
        sim_debug (DBG_ROP, gpx_dev, "   Fast Destination DX: %d\n", va_adp[ADP_FDX]);
        sim_debug (DBG_ROP, gpx_dev, "   Fast Destination DY: %d\n", va_adp[ADP_FDY]);
        sim_debug (DBG_ROP, gpx_dev, "   Slow Destination DX: %d\n", va_adp[ADP_SDX]);
        sim_debug (DBG_ROP, gpx_dev, "   Slow Destination DY: %d\n", va_adp[ADP_SDY]);
        sim_debug (DBG_ROP, gpx_dev, "   Fast Scale: %d\n", va_adp[ADP_FS]);
        sim_debug (DBG_ROP, gpx_dev, "   Slow Scale: %d\n", va_adp[ADP_SS]);
        switch (va_adp[ADP_MDE] & 0x3) {
            case 0:                                     /* normal */
            case 2:                                     /* linear pattern */
                va_fifo_clr ();
                va_adpstat (ADPSTAT_IC, (ADPSTAT_AC | ADPSTAT_RC));
                va_unit[1].CMD = CMD_ROP;
                va_adp_setup ();
                va_rop (&va_unit[1]);
                break;
            case 3:                                     /* fill */
                va_fifo_clr ();
                va_adpstat (ADPSTAT_IC, (ADPSTAT_AC | ADPSTAT_RC));
                va_unit[1].CMD = CMD_ROP;
                va_fill_setup ();
                va_fill (&va_unit[1]);
                break;
                }
        return;

    case 7:                                             /* processor to bitmap */
        lu = (cmd >> 4) & 0x3;                          /* get logic unit */
        sim_debug (DBG_ROP, gpx_dev, "Command: PTB\n");
        sim_debug (DBG_ROP, gpx_dev, "   Mode: %s\n", (cmd & 0x40) ? "X-Mode" : "Z-Mode");
        sim_debug (DBG_ROP, gpx_dev, "   Select: %X\n", va_ucs);
        sim_debug (DBG_ROP, gpx_dev, "   Clip: (%d, %d, %d, %d)\n", va_adp[ADP_CXMN], va_adp[ADP_CYMN], va_adp[ADP_CXMX], va_adp[ADP_CYMX]);
        sim_debug (DBG_ROP, gpx_dev, "   Pen: %s\n", (va_adp[ADP_MDE] & 0x80) ? "Down" : "Up");
        sim_debug (DBG_ROP, gpx_dev, "   Logic Unit: %d\n", lu);
        if ((cmd & 0x40) == 0) {
            lu = 2;                                     /* always use logic unit 2 for Z-mode */
            sim_debug (DBG_ROP, gpx_dev, "   Z-Mode: %s\n", (cmd & 0x8) ? "Background" : "Foreground");
            if (cmd & 0x8)
                sim_printf ("Warning: PTB-Z with background selected at %08X\n", fault_PC);
            }
        rg = (cmd & 0x7);
        for (sel = va_ucs, cn = 0; sel; sel >>=1, cn++) {
            if (sel & 1) {                              /* chip selected? */
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Function: %s\n", cn, va_fnc[va_vdp[cn].rg[0x4 + lu] & 0xF]);
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Mask 1: %04X (%s)\n", cn, va_vdp[cn].rg[VDP_MSK1], (va_vdp[cn].rg[0x4 + lu] & 0x10) ? "Complement" : "Enabled");
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Mask 2: %04X (%s)\n", cn, va_vdp[cn].rg[VDP_MSK2], (va_vdp[cn].rg[0x4 + lu] & 0x20) ? "Complement" : "Enabled");
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Source: %04X (%s)\n", cn, va_vdp[cn].rg[VDP_SRC], (va_vdp[cn].rg[0x4 + lu] & 0x40) ? "Enabled" : "Complement");
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Resolution Mode: %s\n", cn, (va_vdp[cn].rg[0x4 + lu] & 0x40) ? "Disabled" : "Enabled");
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Foreground: %04X\n", cn, va_vdp[cn].rg[VDP_FG]);
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Background: %04X\n", cn, va_vdp[cn].rg[VDP_BG]);
                sim_debug (DBG_ROP, gpx_dev, "      [%d] Fill: %04X\n", cn, va_vdp[cn].rg[VDP_FILL]);
                if (va_vdp[cn].rg[VDP_CSR0 + rg] & 0x10) {
                    sim_debug (DBG_ROP, gpx_dev, "      [%d] Broadcast: Enabled\n", cn);
                    }
                switch (va_vdp[cn].rg[VDP_CSR0 + rg] & 0xC) {
                    case 0x0:
                        sim_debug (DBG_ROP, gpx_dev, "      [%d] Internal: None\n", cn);
                        break;
                    case 0x4:
                        sim_debug (DBG_ROP, gpx_dev, "      [%d] Internal: Source\n", cn);
                        break;
                    case 0x8:
                        sim_debug (DBG_ROP, gpx_dev, "      [%d] Internal: Mask 1 & 2\n", cn);
                        break;
                    case 0xC:
                        sim_debug (DBG_ROP, gpx_dev, "      [%d] Internal: Mask 2\n", cn);
                        break;
                        }
                switch (va_vdp[cn].rg[VDP_CSR0 + rg] & 0x3) {
                    case 0x0:
                        sim_debug (DBG_ROP, gpx_dev, "      [%d] External: None\n", cn);
                        break;
                    case 0x1:
                        sim_debug (DBG_ROP, gpx_dev, "      [%d] External: Source\n", cn);
                        break;
                    case 0x2:
                        sim_debug (DBG_ROP, gpx_dev, "      [%d] External: Mask 1 & 2\n", cn);
                        break;
                    case 0x3:
                        sim_debug (DBG_ROP, gpx_dev, "      [%d] External: Mask 2\n", cn);
                        break;
                        }
                }
            }
        sim_debug (DBG_ROP, gpx_dev, "   X Index: %d\n", va_adp[ADP_NXI]);
        sim_debug (DBG_ROP, gpx_dev, "   Y Index: %d\n", va_adp[ADP_NYI]);
        sim_debug (DBG_ROP, gpx_dev, "   Destination Indexing: %s\n", (va_adp[ADP_MDE]& 0x40) ? "Enabled" : "Disabled");
        sim_debug (DBG_ROP, gpx_dev, "   Destination X Origin: %d\n", va_adp[ADP_DXO]);
        sim_debug (DBG_ROP, gpx_dev, "   Destination Y Origin: %d\n", va_adp[ADP_DYO]);
        sim_debug (DBG_ROP, gpx_dev, "   Fast Destination DX: %d\n", va_adp[ADP_FDX]);
        sim_debug (DBG_ROP, gpx_dev, "   Fast Destination DY: %d\n", va_adp[ADP_FDY]);
        sim_debug (DBG_ROP, gpx_dev, "   Slow Destination DX: %d\n", va_adp[ADP_SDX]);
        sim_debug (DBG_ROP, gpx_dev, "   Slow Destination DY: %d\n", va_adp[ADP_SDY]);
        sim_debug (DBG_ROP, gpx_dev, "   Fast Scale: %d\n", va_adp[ADP_FS]);
        sim_debug (DBG_ROP, gpx_dev, "   Slow Scale: %d\n", va_adp[ADP_SS]);
        va_fifo_clr ();
        va_adpstat (ADPSTAT_IC, (ADPSTAT_AC | ADPSTAT_RC));
        if (cmd & 0x40)
            va_unit[1].CMD = CMD_PTBX;                  /* X-Mode */
        else
            va_unit[1].CMD = CMD_PTBZ;                  /* Z-Mode */
        va_adp_setup ();
        if (va_adp[ADP_STAT] & ADPSTAT_IRR)             /* data in FIFO? */
            va_ptb (&va_unit[1], (va_unit[1].CMD == CMD_PTBZ));
        return;
        }
sim_debug (DBG_ROP, gpx_dev, "Command: Unknown(%02X)\n", cmd);
}

void va_scmd (int32 cmd)
{
uint32 sel, cn, val, rg;
uint32 adp_opc = (cmd >> 8) & 0x7;

/* Commands on page 3-74 */

switch (adp_opc) {                                      /* address processor opcode */

    case 0:                                             /* cancel */
        sim_debug (DBG_ROP, gpx_dev, "Scroll Command: Cancel\n");
        return;

    case 1:                                             /* register load */
        /* Video processor chip registers on page 3-82 */
        if (cmd & 0x80) {
            if (cmd & 0x20) {                           /* I/D Bus Z-Axis Register Load */
                rg = ((cmd >> 2) & 3);
                val = va_adp[ADP_IDS];
                sim_debug (DBG_VDP, gpx_dev, "vdp_wr: z-reg[%X, %X] = %X\n", rg, (cmd & 0x3), val);
                switch (rg) {
                    case 0:
                        rg = VDP_SRC;
                        break;
                    case 1:
                        rg = VDP_FG;
                        break;
                    case 2:
                        rg = VDP_FILL;
                        break;
                    case 3:
                        rg = VDP_BG;
                        break;
                        }
                for (sel = va_scs, cn = 0; sel; sel >>=1, cn++) {
                    if (sel & 1) {                      /* chip selected? */
                        if (val & (1u << cn))
                            va_vdp_wr (cn, rg, 0xFFFF);
                        else
                            va_vdp_wr (cn, rg, 0);
                        }
                    }
                }
            else {                                      /* I/D Bus Video Processor Register Load */
                rg = (cmd & 0x1F);
                val = va_adp[ADP_IDS];
                for (sel = va_scs, cn = 0; sel; sel >>=1, cn++) {
                    if (sel & 1)                        /* chip selected? */
                        va_vdp_wr (cn, rg, val);
                    }
                }
            }
        else {                                          /* I/D Bus External Register Load */
            switch (cmd & 0xff) {
                case 0x40:                              /* scroll chip select */
                    va_scs = va_adp[ADP_IDS];
                    va_scs = va_scs & VA_PLANE_MASK;
                    sim_debug (DBG_VDP, gpx_dev, "scs_sel: %X (%X) at %08X\n", va_scs, (cmd & 0x7F), fault_PC);
                    break;

                case 0x60:                              /* update chip select (green update mask) */
                    va_ucs = va_adp[ADP_IDS];
                    va_ucs = va_ucs & VA_PLANE_MASK;
                    sim_debug (DBG_VDP, gpx_dev, "ucs_sel: %X (%X) at %08X\n", va_ucs, (cmd & 0x7F), fault_PC);
                    break;

                case 0x30:                              /* red update mask */
                    break;

                case 0x18:                              /* blue update mask */
                    break;
                }
            }
        return;

    case 3:                                             /* bitmap to processor */
        sim_debug (DBG_ROP, gpx_dev, "Scroll Command: BTP\n");
        return;

    case 6:                                             /* rasterop */
        sim_debug (DBG_ROP, gpx_dev, "Scroll Command: ROP\n");
        return;

    case 7:                                             /* processor to bitmap */
        sim_debug (DBG_ROP, gpx_dev, "Scroll Command: PTB\n");
        return;
        }
sim_debug (DBG_ROP, gpx_dev, "Scroll Command: Unknown(%02X)\n", cmd);
}

void va_scroll ()
{
uint32 x_min, x_max, y_min, y_max, x_lim;
uint32 src, dest;
int32 y_old, y_new;
uint32 x, y, x_size, y_size;
uint32 vscroll, hscroll;
uint32 sel, cn;

va_adpstat (ADPSTAT_SC, 0);                             /* scroll service */

if ((va_adp[ADP_PYSC] & 0x8000) == 0)                   /* scroll required? */
    return;

if (va_adp[ADP_PYSC] & 0x1000) {                        /* down scrolling? */
    vscroll = va_adp[ADP_PYSC] & 0xFFF;
    if (vscroll != 0) {                                 /* scroll required? */
        sel = 0;
        for (cn = 0; cn < VA_PLANES; cn++) {
            if (va_vdp[cn].rg[VDP_SC] & 0x20)           /* scrolling enabled? */
                sel = sel | (1u << va_vdp[cn].rg[VDP_PA]);
            }
        if (sel) {
            sim_debug (DBG_ROP, gpx_dev, "Scrolling planes %X down by %d pixels (%d, %d, %d, %d)\n", sel, vscroll, va_adp[ADP_PXMN], va_adp[ADP_PYMN], va_adp[ADP_PXMX], va_adp[ADP_PYMX]);
            x_size = va_adp[ADP_PXMX] - va_adp[ADP_PXMN];
            y_size = va_adp[ADP_PYMX] - va_adp[ADP_PYMN] - vscroll;
            y_old = va_adp[ADP_PYOF];
            y_new = va_adp[ADP_PYOF] - vscroll;
            if (y_new < 0)
                y_new = y_new + va_adp[ADP_YL];
            dest = (y_new * VA_XSIZE);
            src = (y_old * VA_XSIZE);
            for (y = 0; y < 864; y++) {
                if ((y_old >= va_adp[ADP_PYMN]) && (y_old < va_adp[ADP_PYMX])) {
                    for (x = 0; x < (uint32)va_adp[ADP_PXMN]; x++) {
                        va_buf[dest] = va_buf[dest] & ~sel;
                        va_buf[dest] |= (va_buf[src++] & sel);
                        sim_debug (DBG_ROP, gpx_dev, "(%d, %d) -> (%d, %d) = %X\n", x, y_old, x, y_new, va_buf[dest]);
                        dest++;
                        }
                    dest = dest + x_size;
                    src = src + x_size;
                    for (x = va_adp[ADP_PXMX]; x < 1024; x++) {
                        va_buf[dest] = va_buf[dest] & ~sel;
                        va_buf[dest] |= (va_buf[src++] & sel);
                        sim_debug (DBG_ROP, gpx_dev, "(%d, %d) -> (%d, %d) = %X\n", x, y_old, x, y_new, va_buf[dest]);
                        dest++;
                        }
                    }
                else {
                    for (x = 0; x < 1024; x++) {
                        va_buf[dest] = va_buf[dest] & ~sel;
                        va_buf[dest] |= (va_buf[src++] & sel);
                        sim_debug (DBG_ROP, gpx_dev, "(%d, %d) -> (%d, %d) = %X\n", x, y_old, x, y_new, va_buf[dest]);
                        dest++;
                        }
                    }
                va_updated[y_new] = TRUE;
                y_new++;
                if (y_new == va_adp[ADP_YL]) {
                    y_new = 0;
                    dest = 0;
                    }
                y_old++;
                if (y_old == va_adp[ADP_YL]) {
                    y_old = 0;
                    src = 0;
                    }
                }
            va_erase (va_adp[ADP_PXMN], va_adp[ADP_PXMX], va_adp[ADP_PYMN] - vscroll, va_adp[ADP_PYMN]);
            }
        }
    }
else {                                                  /* up, left or right */
    vscroll = va_adp[ADP_PYSC] & 0xFFF;
    if (vscroll != 0) {                                 /* scroll required? */
        sel = 0;
        for (cn = 0; cn < VA_PLANES; cn++) {
            if (va_vdp[cn].rg[VDP_SC] & 0x20)           /* scrolling enabled? */
                sel = sel | (1u << va_vdp[cn].rg[VDP_PA]);
            }
        if (sel) {
            sim_debug (DBG_ROP, gpx_dev, "Scrolling planes %X up by %d pixels (%d, %d, %d, %d)\n", sel, vscroll, va_adp[ADP_PXMN], va_adp[ADP_PYMN], va_adp[ADP_PXMX], va_adp[ADP_PYMX]);
            x_size = va_adp[ADP_PXMX] - va_adp[ADP_PXMN];
            y_size = va_adp[ADP_PYMX] - va_adp[ADP_PYMN] - vscroll;
            y_old = va_adp[ADP_PYMN] + vscroll;
            y_new = va_adp[ADP_PYMN];
            dest = (y_new * VA_XSIZE) + va_adp[ADP_PXMN];
            src = (y_old * VA_XSIZE) + va_adp[ADP_PXMN];
            for (y = 0; y < y_size; y++) {
                for (x = 0; x < x_size; x++) {
                    va_buf[dest] = va_buf[dest] & ~sel;
                    va_buf[dest] |= (va_buf[src++] & sel);
                    sim_debug (DBG_ROP, gpx_dev, "(%d, %d) -> (%d, %d) = %X\n", (x + va_adp[ADP_PXMN]), (y_old + y), (x + va_adp[ADP_PXMN]), (y_new + y), va_buf[dest]);
                    dest++;
                    }
                va_updated[y_new + y] = TRUE;
                dest = dest + (VA_XSIZE - x_size);
                src = src + (VA_XSIZE - x_size);
                }
            va_erase (va_adp[ADP_PXMN], va_adp[ADP_PXMX], va_adp[ADP_PYMX] - vscroll, va_adp[ADP_PYMX]);
            }
        }
    for (cn = 0; cn < VA_PLANES; cn++) {
        if (va_vdp[cn].rg[VDP_SC] & 0x20) {             /* scrolling enabled? */
            if (va_vdp[cn].rg[VDP_SC] & 0xF) {          /* scroll required? */
                sim_debug (DBG_ROP, gpx_dev, "Scrolling plane %d %s by %d pixels (%d, %d, %d, %d)\n", cn,
                    (va_vdp[cn].rg[VDP_SC] & 0x10) ? "right" : "left",
                    (va_vdp[cn].rg[VDP_SC] & 0xF),
                    va_adp[ADP_PXMN], va_adp[ADP_PYMN], va_adp[ADP_PXMX], va_adp[ADP_PYMX]);
                hscroll = va_vdp[cn].rg[VDP_SC] & 0xF;
                if (va_vdp[cn].rg[VDP_SC] & 0x10) {     /* right */
                    hscroll++;
                    y_min = va_adp[ADP_PYMN];
                    y_max = va_adp[ADP_PYMX];
                    if (y_max > VA_YSIZE)
                        y_max = VA_YSIZE;
                    x_min = va_adp[ADP_PXMN];
                    x_max = va_adp[ADP_PXMX] - 1;
                    if (x_max > VA_XSIZE)
                        x_max = VA_XSIZE;
                    x_lim = x_min + hscroll;
                    dest = (va_adp[ADP_PYMN] * VA_XSIZE) + va_adp[ADP_PXMX] - 1;
                    src = (va_adp[ADP_PYMN] * VA_XSIZE) + va_adp[ADP_PXMX] - hscroll - 1;
                    for (y = y_min; y < y_max; y++) {
                        for (x = x_max; x >= x_min; x--) {
                            va_buf[dest] = va_buf[dest] & ~(1u << va_vdp[cn].rg[VDP_PA]);
                            if (x >= x_lim) {
                                sim_debug (DBG_ROP, gpx_dev, "(%d, %d) copy pixel %X (%d = %d), %X (%d = %d) -> ", x, y, va_buf[src], src, (src & 1023), va_buf[dest], dest, (dest & 1023));
                                va_buf[dest] |= (va_buf[src--] & (1u << va_vdp[cn].rg[VDP_PA]));
                                sim_debug (DBG_ROP, gpx_dev, "%X\n", va_buf[dest]);
                                dest--;
                                }
                            else {
                                sim_debug (DBG_ROP, gpx_dev, "(%d, %d) fill pixel %X (%d = %d) -> ", x, y, va_buf[dest], dest, (dest & 1023));
                                va_buf[dest] |= (((va_vdp[cn].rg[VDP_FILL] >> (x & 0xF)) & 0x1) << va_vdp[cn].rg[VDP_PA]);
                                sim_debug (DBG_ROP, gpx_dev, "%X\n", va_buf[dest]);
                                dest--;
                                src--;
                                }
                            }
                        va_updated[y] = TRUE;
                        dest = dest + (VA_XSIZE + (x_max - x_min)) + 1;
                        src = src + (VA_XSIZE + (x_max - x_min)) + 1;
                        }
                    }
                else {                                  /* left */
                    y_min = va_adp[ADP_PYMN];
                    y_max = va_adp[ADP_PYMX];
                    if (y_max > VA_YSIZE)
                        y_max = VA_YSIZE;
                    x_min = va_adp[ADP_PXMN];
                    x_max = va_adp[ADP_PXMX];
                    if (x_max > VA_XSIZE)
                        x_max = VA_XSIZE;
                    x_lim = x_max - hscroll;
                    dest = (va_adp[ADP_PYMN] * VA_XSIZE) + va_adp[ADP_PXMN];
                    src = (va_adp[ADP_PYMN] * VA_XSIZE) + va_adp[ADP_PXMN] + hscroll;
                    for (y = y_min; y < y_max; y++) {
                        for (x = x_min; x < x_max; x++) {
                            va_buf[dest] = va_buf[dest] & ~(1u << va_vdp[cn].rg[VDP_PA]);
                            if (x < x_lim) {
                                sim_debug (DBG_ROP, gpx_dev, "(%d, %d) copy pixel %X (%d = %d), %X (%d = %d) -> ", x, y, va_buf[src], src, (src & 1023), va_buf[dest], dest, (dest & 1023));
                                va_buf[dest] |= (va_buf[src++] & (1u << va_vdp[cn].rg[VDP_PA]));
                                sim_debug (DBG_ROP, gpx_dev, "%X\n", va_buf[dest]);
                                dest++;
                                }
                            else {
                                sim_debug (DBG_ROP, gpx_dev, "(%d, %d) fill pixel %X (%d = %d) -> ", x, y, va_buf[dest], dest, (dest & 1023));
                                va_buf[dest] |= (((va_vdp[cn].rg[VDP_FILL] >> (x & 0xF)) & 0x1) << va_vdp[cn].rg[VDP_PA]);
                                sim_debug (DBG_ROP, gpx_dev, "%X\n", va_buf[dest]);
                                dest++;
                                src++;
                                }
                            }
                        va_updated[y] = TRUE;
                        dest = dest + (VA_XSIZE - (x_max - x_min));
                        src = src + (VA_XSIZE - (x_max - x_min));
                        }
                    }
                }
            }
        }
    }
va_adp[ADP_PYSC] = 0;
}

void va_adp_setup ()
{
int32 sx, sy;
uint32 pix;

sim_debug (DBG_ROP, gpx_dev, "ROP: ");
if (va_adp[ADP_CMD1] & 0x800) {                         /* source 1 enabled? */
    pix = 0;
    sx = 0;
    sy = 0;
    if (va_adp[ADP_MDE] & 0x20) {                       /* source indexing enable? */
        pix = pix + va_adp[ADP_NXI];                    /* apply indexing */
        pix = pix + (va_adp[ADP_NYI] * VA_XSIZE);
        sx = sx + va_adp[ADP_NXI];
        sy = sy + va_adp[ADP_NYI];
        }
    pix = pix + va_adp[ADP_SXO];                        /* apply offset */
    pix = pix + (va_adp[ADP_SYO] * VA_XSIZE);
    pix = pix + (va_adp[ADP_PYOF] * VA_XSIZE);
    pix = pix & VA_BUFMASK;
    sx = sx + va_adp[ADP_SXO];
    sy = sy + va_adp[ADP_SYO];

    if ((va_adp[ADP_MDE] & 0x3) == 0) {                 /* normal mode */
        if ((va_adp[ADP_FSDX] < 0) && (va_adp[ADP_FDX] > 0))
            va_line_init (&s1_fast, -va_adp[ADP_FDX], 0, pix);
        else
            va_line_init (&s1_fast, va_adp[ADP_FDX], 0, pix);

        if ((va_adp[ADP_SSDY] < 0) && (va_adp[ADP_SDY] > 0))
            va_line_init (&s1_slow, 0, -va_adp[ADP_SDY], pix);
        else
            va_line_init (&s1_slow, 0, va_adp[ADP_SDY], pix);
        }
    else {                                              /* linear pattern mode */
        va_line_init (&s1_fast, va_adp[ADP_FSDX], 0, pix);
        va_line_init (&s1_slow, 0, va_adp[ADP_SSDY], pix);
        }

    sim_debug (DBG_ROP, gpx_dev, "Source 1 (%d, %d, %d, %d) ", sx, sy, (sx + va_adp[ADP_FDX]), (sy + va_adp[ADP_SDY]));
    }

if (va_adp[ADP_CMD1] & 0x1000) {                        /* source 2 enabled? */
    s2_xmask = va_adp[ADP_S2HW] & 0x7;
    s2_xmask = (1u << (s2_xmask + 2)) - 1;
    s2_ymask = (va_adp[ADP_S2HW] >> 4) & 0x7;
    s2_ymask = (1u << (s2_ymask + 2)) - 1;
    s2_pixs = 0;
    s2_pixs = s2_pixs + va_adp[ADP_S2XO];               /* apply offset */
    s2_pixs = s2_pixs + (va_adp[ADP_S2YO] * VA_XSIZE);
    s2_pixs = s2_pixs & VA_BUFMASK;
    sx = va_adp[ADP_S2XO];
    sy = va_adp[ADP_S2YO];
    sim_debug (DBG_ROP, gpx_dev, "Source 2 (%d, %d, %d, %d) ", sx, sy, (sx + s2_xmask + 1), (sy + s2_ymask + 1));
    }

pix = 0;
dx = 0;
dy = 0;
if (va_adp[ADP_MDE] & 0x40) {                           /* dest indexing enable? */
    pix = pix + va_adp[ADP_NXI];                        /* apply indexing */
    pix = pix + (va_adp[ADP_NYI] * VA_XSIZE);
    dx = dx + va_adp[ADP_NXI];
    dy = dy + va_adp[ADP_NYI];
    }
pix = pix + va_adp[ADP_DXO];                            /* apply offset */
pix = pix + (va_adp[ADP_DYO] * VA_XSIZE);
pix = pix + (va_adp[ADP_PYOF] * VA_XSIZE);
pix = pix & VA_BUFMASK;
dx = dx + va_adp[ADP_DXO];
dy = dy + va_adp[ADP_DYO];

va_line_init (&dst_fast, va_adp[ADP_FDX], va_adp[ADP_FDY], pix);
va_line_init (&dst_slow, va_adp[ADP_SDX], va_adp[ADP_SDY], pix);

dst_slow.err = dst_slow.err + va_adp[ADP_ERR1];
dst_fast.err = dst_fast.err + va_adp[ADP_ERR2];

if ((va_adp[ADP_CMD1] & 0x400) && (va_adp[ADP_MDE] & 0x80)) /* dest enabled, pen down? */
    sim_debug (DBG_ROP, gpx_dev, "-> Dest (%d, %d, %d, %d)", dx, dy, (dx + va_adp[ADP_FDX]), (dy + va_adp[ADP_SDY]));
sim_debug (DBG_ROP, gpx_dev, "\n");
}

void va_fill_setup ()
{
int32 sx, sy;
uint32 pix;

sim_debug (DBG_ROP, gpx_dev, "ROP: Fill ");

pix = 0;
if (va_adp[ADP_MDE] & 0x40) {                           /* dest indexing enable? */
    pix = pix + va_adp[ADP_NXI];                        /* apply indexing */
    pix = pix + (va_adp[ADP_NYI] * VA_XSIZE);
    }
pix = pix + va_adp[ADP_SXO];                            /* apply offset */
pix = pix + (va_adp[ADP_SYO] * VA_XSIZE);
pix = pix + (va_adp[ADP_PYOF] * VA_XSIZE);
pix = pix & VA_BUFMASK;

va_line_init (&s1_slow, va_adp[ADP_FSDX], va_adp[ADP_SSDY], pix);

if (va_adp[ADP_CMD1] & 0x1000) {                        /* source 2 enabled? */
    s2_xmask = va_adp[ADP_S2HW] & 0x7;
    s2_xmask = (1u << (s2_xmask + 2)) - 1;
    s2_ymask = (va_adp[ADP_S2HW] >> 4) & 0x7;
    s2_ymask = (1u << (s2_ymask + 2)) - 1;
    s2_pixs = 0;
    s2_pixs = s2_pixs + va_adp[ADP_S2XO];               /* apply offset */
    s2_pixs = s2_pixs + (va_adp[ADP_S2YO] * VA_XSIZE);
    s2_pixs = s2_pixs & VA_BUFMASK;
    sx = va_adp[ADP_S2XO];
    sy = va_adp[ADP_S2YO];
    sim_debug (DBG_ROP, gpx_dev, "Source 2 (%d, %d, %d, %d) ", sx, sy, (sx + s2_xmask + 1), (sy + s2_ymask + 1));
    }

pix = 0;
dx = 0;
dy = 0;
if (va_adp[ADP_MDE] & 0x40) {                           /* dest indexing enable? */
    pix = pix + va_adp[ADP_NXI];                        /* apply indexing */
    pix = pix + (va_adp[ADP_NYI] * VA_XSIZE);
    dx = dx + va_adp[ADP_NXI];
    dy = dy + va_adp[ADP_NYI];
    }
pix = pix + va_adp[ADP_DXO];                            /* apply offset */
pix = pix + (va_adp[ADP_DYO] * VA_XSIZE);
pix = pix + (va_adp[ADP_PYOF] * VA_XSIZE);
pix = pix & VA_BUFMASK;
dx = dx + va_adp[ADP_DXO];
dy = dy + va_adp[ADP_DYO];

va_line_init (&dst_slow, va_adp[ADP_SDX], va_adp[ADP_SDY], pix);

dst_slow.err = dst_slow.err + va_adp[ADP_ERR1];
s1_slow.err = s1_slow.err + va_adp[ADP_ERR2];

sim_debug (DBG_ROP, gpx_dev, "\n");
}

t_stat va_ptb (UNIT *uptr, t_bool zmode)
{
uint32 val = 0, sc;
t_bool clip;

if ((uptr->CMD != CMD_PTBX) && (uptr->CMD != CMD_PTBZ))
    return SCPE_OK;
for (;;) {
    if (zmode) {
        if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0)      /* no data in FIFO? */
            return SCPE_OK;
        val = va_fifo_rd ();                            /* read FIFO */
        }
    else {
        sc = (dst_fast.x + dx) & 0xF;
        if ((sc == 0) || (dst_fast.x == 0)) {
            if ((va_adp[ADP_STAT] & ADPSTAT_IRR) == 0)  /* no data in FIFO? */
                return SCPE_OK;
            val = va_fifo_rd ();                        /* read FIFO */
            }
        }
    clip = FALSE;
    if ((dst_fast.x + dx) < va_adp[ADP_CXMN]) {
        va_adp[ADP_STAT] |= ADPSTAT_CL;
        clip = TRUE;
        }
    else if ((dst_fast.x + dx) > va_adp[ADP_CXMX]) {
        va_adp[ADP_STAT] |= ADPSTAT_CR;
        clip = TRUE;
        }
    if ((dst_fast.y + dy) < va_adp[ADP_CYMN]) {
        va_adp[ADP_STAT] |= ADPSTAT_CT;
        clip = TRUE;
        }
    else if ((dst_fast.y + dy) > va_adp[ADP_CYMX]) {
        va_adp[ADP_STAT] |= ADPSTAT_CB;
        clip = TRUE;
        }
    if ((va_adp[ADP_CMD1] & 0x400) && (va_adp[ADP_MDE] & 0x80) && !clip) {   /* dest enabled, pen down? */
        if (zmode)
            va_buf[dst_fast.pix] = val & VA_PLANE_MASK;
        else {
            if (val & (1u << sc))
                va_buf[dst_fast.pix] = va_buf[dst_fast.pix] | va_ucs; /* set pixel in selected chips */
            else
                va_buf[dst_fast.pix] = va_buf[dst_fast.pix] & ~va_ucs; /* clear pixel in selected chips */
            }
        sim_debug (DBG_ROP, gpx_dev, "-> Dest X: %d, Y: %d, pix: %X\n", dst_fast.x, dst_slow.y, va_buf[dst_fast.pix]);
        va_updated[dst_slow.y + dst_fast.y + dy] = TRUE;
        }

    if (va_line_step (&dst_fast)) {                     /* fast vector exhausted? */
        if (va_line_step (&dst_slow))                   /* slow vector exhausted? */
            break;                                      /* finished */
        dst_fast.pix = dst_slow.pix;
        }
    }
sim_debug (DBG_ROP, gpx_dev, "PTB Complete\n");
uptr->CMD = 0;
va_adpstat (ADPSTAT_AC | ADPSTAT_RC, 0);
return SCPE_OK;
}

t_stat va_btp (UNIT *uptr, t_bool zmode)
{
uint32 val, sc;

if ((uptr->CMD != CMD_BTPX) && (uptr->CMD != CMD_BTPZ))
    return SCPE_OK;
if ((va_adp[ADP_STAT] & ADPSTAT_RC) && (va_adp_fifo_sz == 0)) {
    uptr->CMD = 0;
    va_adpstat (ADPSTAT_AC, 0);
    return SCPE_OK;
    }
sc = 0;
for (val = 0;;) {
    if (zmode) {
        if ((va_adp[ADP_STAT] & ADPSTAT_ITR) == 0)      /* no space in FIFO? */
            return SCPE_OK;
        val = 0;
        }
    else {
        sc = s1_fast.x & 0xF;
        if (sc == 0) {
            if ((va_adp[ADP_STAT] & ADPSTAT_ITR) == 0)  /* no space in FIFO? */
                return SCPE_OK;
            val = 0;
            }
        }
    if (va_adp[ADP_CMD1] & 0x800) {                    /* source 1 enabled? */
        sim_debug (DBG_ROP, gpx_dev, "Source X: %d, Y: %d, pix: %X\n", s1_fast.x, s1_slow.y, va_buf[s1_fast.pix]);
        if (zmode)
            val = (va_buf[s1_fast.pix] & VA_PLANE_MASK);
        else {
            if (va_buf[s1_fast.pix] & va_ucs)
                val |= (1u << sc);
            }
        }
    if (zmode || (sc == 0xF))
        va_fifo_wr (val);
    if (va_line_step (&s1_fast)) {                      /* fast vector exhausted? */
        if (!zmode && (sc != 0xF))
            va_fifo_wr (val);
        if (va_line_step (&s1_slow))                    /* slow vector exhausted? */
            break;                                      /* finished */
        s1_fast.pix = s1_slow.pix;
        }
    }
sim_debug (DBG_ROP, gpx_dev, "BTP Complete\n");
/* FIXME - This is a temporary workaround for the QDSS. Address output complete
   should not be set until the FIFO is empty */
#if defined(VAX_630)
uptr->CMD = 0;
va_adpstat (ADPSTAT_AC | ADPSTAT_RC, 0);
#else
va_adpstat (ADPSTAT_RC, 0);
#endif
return SCPE_OK;
}

void va_erase (uint32 x0, uint32 x1, uint32 y0, uint32 y1)
{
uint32 i, j, msk, val, x, y, dest;
uint8 zfill[16];

for (i = 0; i < 16; i++)
    zfill[i] = 0;
for (i = 0, msk = 1; i < 8; i++, msk <<= 1) {           /* for each viper */
    val = va_vdp[i].rg[VDP_FILL];                       /* get the fill constant */
    for (j = 0; j < 16; j++) {
        if (val & 1)
            zfill[j] = zfill[j] | msk;                  /* convert x-mode to z-mode */
        val >>= 1;
        }
    }
dest = (y0 * VA_XSIZE) + x0;
for (y = y0; y < y1; y++) {
    for (x = x0; x < x1; x++)
       va_buf[dest++] = zfill[x & 0xF];
    va_updated[y] = TRUE;
    dest = dest + VA_XSIZE - (x1 - x0);
    }
sim_debug (DBG_ROP, gpx_dev, "Erase Complete\n");
}

t_stat va_adp_reset (DEVICE *dptr)
{
gpx_dev = dptr;
va_fifo_clr ();
va_adp[ADP_STAT] |= 0x3FFF;
va_ucs = 0;
va_scs = 0;
return SCPE_OK;
}

t_stat va_adp_svc (UNIT *uptr)
{
va_adpstat (ADPSTAT_VB, 0);                             /* vertical blanking */

va_adp[ADP_OXI] = va_adp[ADP_NXI];                      /* load pending index values */
va_adp[ADP_OYI] = va_adp[ADP_NYI];
va_adp[ADP_NXI] = va_adp[ADP_PXI];
va_adp[ADP_NYI] = va_adp[ADP_PYI];

va_scroll ();

return SCPE_OK;
}

#else /* defined(VAX_620) */
static const char *dummy_declaration = "Something to compile";
#endif /* !defined(VAX_620) */
