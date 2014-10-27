/* id_pt.c: Interdata paper tape reader

   Copyright (c) 2000-2008, Robert M. Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   pt           paper tape reader and punch

   25-Apr-03    RMS     Revised for extended file support
   10-Apr-03    RMS     Fixed type problem in ptr service (Mark Pizzolato)
*/

#include "id_defs.h"
#include <ctype.h>

/* Device definitions */

#define PTR             0                               /* unit subscripts */
#define PTP             1

#define STA_OVR         0x80                            /* overrun */
#define STA_NMTN        0x10                            /* no motion */
#define STA_MASK        (STA_BSY | STA_OVR | STA_DU)    /* static bits */
#define SET_EX          (STA_OVR | STA_NMTN)            /* set EX */

#define CMD_V_RUN       4                               /* run/stop */
#define CMD_V_SLEW      2                               /* slew/step */
#define CMD_V_RD        0                               /* read/write */

extern uint32 int_req[INTSZ], int_enb[INTSZ];

uint32 pt_run = 0, pt_slew = 0;                         /* ptr modes */
uint32 pt_rd = 1, pt_chp = 0;                           /* pt state */
uint32 pt_arm = 0;                                      /* int arm */
uint32 pt_sta = STA_BSY;                                /* status */
uint32 ptr_stopioe = 0, ptp_stopioe = 0;                /* stop on error */

DEVICE pt_dev;
uint32 pt (uint32 dev, uint32 op, uint32 dat);
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat pt_boot (int32 unitno, DEVICE *dptr);
t_stat pt_reset (DEVICE *dptr);

/* PT data structures

   pt_dev       PT device descriptor
   pt_unit      PT unit descriptors
   pt_reg       PT register list
*/

DIB pt_dib = { d_PT, -1, v_PT, NULL, &pt, NULL };

UNIT pt_unit[] = {
    { UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
             SERIAL_IN_WAIT },
    { UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT }
    };

REG pt_reg[] = {
    { HRDATA (STA, pt_sta, 8) },
    { HRDATA (RBUF, pt_unit[PTR].buf, 8) },
    { DRDATA (RPOS, pt_unit[PTR].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (RTIME, pt_unit[PTR].wait, 24), PV_LEFT },
    { FLDATA (RSTOP_IOE, ptr_stopioe, 0) },
    { HRDATA (PBUF, pt_unit[PTP].buf, 8) },
    { DRDATA (PPOS, pt_unit[PTP].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (PTIME, pt_unit[PTP].wait, 24), PV_LEFT },
    { FLDATA (PSTOP_IOE, ptp_stopioe, 0) },
    { FLDATA (IREQ, int_req[l_PT], i_PT) },
    { FLDATA (IENB, int_enb[l_PT], i_PT) },
    { FLDATA (IARM, pt_arm, 0) },
    { FLDATA (RD, pt_rd, 0) },
    { FLDATA (RUN, pt_run, 0) },
    { FLDATA (SLEW, pt_slew, 0) },
    { FLDATA (CHP, pt_chp, 0) },
    { HRDATA (DEVNO, pt_dib.dno, 8), REG_HRO },
    { NULL }
    };

MTAB pt_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "devno", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE pt_dev = {
    "PT", pt_unit, pt_reg, pt_mod,
    2, 10, 31, 1, 16, 8,
    NULL, NULL, &pt_reset,
    &pt_boot, NULL, NULL,
    &pt_dib, DEV_DISABLE
    };

/* Paper tape: IO routine */

uint32 pt (uint32 dev, uint32 op, uint32 dat)
{
uint32 t, old_rd, old_run;

switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        return BY;                                      /* byte only */

    case IO_OC:                                         /* command */
        old_rd = pt_rd;                                 /* save curr rw */
        old_run = pt_run;                               /* save curr run */
        pt_arm = int_chg (v_PT, dat, pt_arm);           /* upd int ctrl */
        pt_rd = io_2b (dat, CMD_V_RD, pt_rd);           /* upd read/wr */
        if (old_rd != pt_rd) {                          /* rw change? */
            pt_sta = pt_sta & ~STA_OVR;                 /* clr overrun */
            if (sim_is_active (&pt_unit[pt_rd? PTR: PTP])) {
                pt_sta = pt_sta | STA_BSY;              /* busy = 1 */
                CLR_INT (v_PT);                         /* clear int */
                }
            else {                                      /* not active */
                pt_sta = pt_sta & ~STA_BSY;             /* busy = 0 */
                if (pt_arm)                             /* no, set int */
                    SET_INT (v_PT);
                }
            }
        if (pt_rd) {                                    /* reader? */
            pt_run = io_2b (dat, CMD_V_RUN, pt_run);    /* upd run/stop */
            pt_slew = io_2b (dat, CMD_V_SLEW, pt_slew); /* upd slew/inc */
            if (pt_run) {                               /* run set? */
                if (old_run == 0) {                     /* run 0 -> 1? */
                    sim_activate (&pt_unit[PTR], pt_unit[PTR].wait);
                    pt_sta = pt_sta & ~STA_DU;          /* clear eof */
                    }
                }
            else sim_cancel (&pt_unit[PTR]);            /* clr, stop rdr */
            }
        else pt_sta = pt_sta & ~STA_DU;                 /* punch, clr eof */
        break;

    case IO_RD:                                         /* read */
        if (pt_run && !pt_slew) {                       /* incremental? */
            sim_activate (&pt_unit[PTR], pt_unit[PTR].wait);
            pt_sta = pt_sta & ~STA_DU;                  /* clr eof */
            }
        pt_chp = 0;                                     /* clr char pend */
        if (pt_rd)                                      /* set busy */
            pt_sta = pt_sta | STA_BSY;
        return (pt_unit[PTR].buf & 0xFF);               /* return char */

    case IO_WD:                                         /* write */
        pt_unit[PTP].buf = dat & DMASK8;                /* save char */
        if (!pt_rd)                                     /* set busy */
            pt_sta = pt_sta | STA_BSY;
        sim_activate (&pt_unit[PTP], pt_unit[PTP].wait);
        break;

    case IO_SS:                                         /* status */
        t = pt_sta & STA_MASK;                          /* get status */
        if (pt_rd && !pt_run && !sim_is_active (&pt_unit[PTR]))
            t = t | STA_NMTN;                           /* stopped? */
        if ((pt_unit[pt_rd? PTR: PTP].flags & UNIT_ATT) == 0)
            t = t | STA_DU;                             /* offline? */
        if (t & SET_EX)                                 /* test for EX */
            t = t | STA_EX;
        return t;
        }

return 0;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return IORETURN (ptr_stopioe, SCPE_UNATT);
if (pt_rd) {                                            /* read mode? */
    pt_sta = pt_sta & ~STA_BSY;                         /* clear busy */
    if (pt_arm)                                         /* if armed, intr */
        SET_INT (v_PT);
    if (pt_chp)                                         /* overrun? */
        pt_sta = pt_sta | STA_OVR;
    }
pt_chp = 1;                                             /* char pending */
if ((temp = getc (uptr->fileref)) == EOF) {             /* error? */
    if (feof (uptr->fileref)) {                         /* eof? */
        pt_sta = pt_sta | STA_DU;                       /* set DU */
        if (ptr_stopioe)
            printf ("PTR end of file\n");
        else return SCPE_OK;
        }
    else perror ("PTR I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
uptr->buf = temp & DMASK8;                              /* store char */
uptr->pos = uptr->pos + 1;                              /* incr pos */
if (pt_slew)                                            /* slew? continue */
    sim_activate (uptr, uptr->wait);
return SCPE_OK;
}

t_stat ptp_svc (UNIT *uptr)
{
if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return IORETURN (ptp_stopioe, SCPE_UNATT);
if (!pt_rd) {                                           /* write mode? */
    pt_sta = pt_sta & ~STA_BSY;                         /* clear busy */
    if (pt_arm)                                         /* if armed, intr */
        SET_INT (v_PT);
    }
if (putc (uptr->buf, uptr -> fileref) == EOF) {         /* write char */
    perror ("PTP I/O error");
    clearerr (uptr -> fileref);
    return SCPE_IOERR;
    }
uptr -> pos = uptr -> pos + 1;                          /* incr pos */
return SCPE_OK;
}

/* Reset routine */

t_stat pt_reset (DEVICE *dptr)
{
sim_cancel (&pt_unit[PTR]);                             /* deactivate units */
sim_cancel (&pt_unit[PTP]);
pt_rd = 1;                                              /* read */
pt_chp = pt_run = pt_slew = 0;                          /* stop, inc, disarm */
pt_sta = STA_BSY;                                       /* buf empty */
CLR_INT (v_PT);                                         /* clear int */
CLR_ENB (v_PT);                                         /* disable int */
pt_arm = 0;                                             /* disarm int */
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START      0x50
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (uint8))
#define BOOT3_START     0x3E
#define BOOT3_LEN       (sizeof (boot_rom) / sizeof (uint8))

static uint8 boot_rom[] = {
    0xD5, 0x00, 0x00, 0xCF,                             /* ST   AL CF */
    0x43, 0x00, 0x00, 0x80                              /*      BR 80 */
    };

static uint8 boot3_rom[] = {
    0xC8, 0x20, 0x00, 0x80,                             /* ST   LHI 2,80 */
    0xC8, 0x30, 0x00, 0x01,                             /*      LHI 3,1 */
    0xC8, 0x40, 0x00, 0xCF,                             /*      LHI 4,CF */
    0xD3, 0xA0, 0x00, 0x78,                             /*      LB A,78 */
    0xDE, 0xA0, 0x00, 0x79,                             /*      OC A,79 */
    0x9D, 0xAE,                                         /* LP   SSR A,E */
    0x42, 0xF0, 0x00, 0x52,                             /*      BTC F,LP */
    0x9B, 0xAE,                                         /*      RDR A,E */
    0x08, 0xEE,                                         /*      LHR E,E */
    0x43, 0x30, 0x00, 0x52,                             /*      BZ LP */
    0x43, 0x00, 0x00, 0x6C,                             /*      BR STO */
    0x9D, 0xAE,                                         /* LP1  SSR A,E */
    0x42, 0xF0, 0x00, 0x64,                             /*      BTC F,LP1 */
    0x9B, 0xAE,                                         /*      RDR A,E */
    0xD2, 0xE2, 0x00, 0x00,                             /* STO  STB E,0(2) */
    0xC1, 0x20, 0x00, 0x64,                             /*      BXLE 2,LP1 */
    0x43, 0x00, 0x00, 0x80                              /*      BR 80 */
    };

t_stat pt_boot (int32 unitno, DEVICE *dptr)
{
extern uint32 PC, dec_flgs;
extern uint16 decrom[];

if (decrom[0xD5] & dec_flgs)                            /* AL defined? */
    IOWriteBlk (BOOT3_START, BOOT3_LEN, boot3_rom);     /* no, 50 seq */
else IOWriteBlk (BOOT_START, BOOT_LEN, boot_rom);       /* copy AL boot */
IOWriteB (AL_DEV, pt_dib.dno);                          /* set dev no */
IOWriteB (AL_IOC, 0x99);                                /* set dev cmd */
IOWriteB (AL_SCH, 0);                                   /* clr sch dev no */
PC = BOOT_START;
return SCPE_OK;
}

/* Dump routine */

#define LOAD_START      0x80
#define LOAD_LO         0x8A
#define LOAD_HI         0x8E
#define LOAD_CS         0x93
#define LOAD_LEN        (sizeof (load_rom) / sizeof (uint8))
#define LOAD_LDR        50

static uint8 load_rom[] = {
    0x24, 0x21,                                         /* BOOT LIS R2,1 */
    0x23, 0x03,                                         /*      BS BOOT */
    0x00, 0x00,                                         /* 32b psw pointer */
    0x00, 0x00,                                         /* 32b reg pointer */
    0xC8, 0x10,                                         /* ST   LHI R1,lo */
    0x00, 0x00,
    0xC8, 0x30,                                         /*      LHI R3,hi */
    0x00, 0x00,
    0xC8, 0x60,                                         /*      LHI R3,cs */
    0x00, 0x00,
    0xD3, 0x40,                                         /*      LB R4,X'78' */
    0x00, 0x78,
    0xDE, 0x40,                                         /*      OC R4,X'79' */
    0x00, 0x79,
    0x9D, 0x45,                                         /* LDR  SSR R4,R5 */
    0x20, 0x91,                                         /*      BTBS 9,.-2 */
    0x9B, 0x45,                                         /*      RDR R4,R5 */
    0x08, 0x55,                                         /*      L(H)R R5,R5 */
    0x22, 0x34,                                         /*      BZS LDR */
    0xD2, 0x51,                                         /* LOOP STB R5,0(R1) */
    0x00, 0x00,
    0x07, 0x65,                                         /*      X(H)R R6,R5 */
    0x9A, 0x26,                                         /*      WDR R2,R6 */
    0x9D, 0x45,                                         /*      SSR R4,R5 */
    0x20, 0x91,                                         /*      BTBS 9,.-2 */
    0x9B, 0x45,                                         /*      RDR R4,R5 */
    0xC1, 0x10,                                         /*      BXLE R1,LOOP */
    0x00, 0xA6,
    0x24, 0x78,                                         /*      LIS R7,8 */
    0x91, 0x7C,                                         /*      SLLS R7,12 */
    0x95, 0x57,                                         /*      EPSR R5,R7 */
    0x22, 0x03                                          /*      BS .-6 */
    };

t_stat pt_dump (FILE *of, char *cptr, char *fnam)
{
uint32 i, lo, hi, cs;
const char *tptr;
extern DEVICE cpu_dev;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_2FARG;
tptr = get_range (NULL, cptr, &lo, &hi, cpu_dev.aradix, 0xFFFF, 0);
if ((tptr == NULL) || (lo < INTSVT))
    return SCPE_ARG;
if (*tptr != 0)
    return SCPE_2MARG;
for (i = lo, cs = 0; i <= hi; i++)
    cs = cs ^ IOReadB (i);
IOWriteBlk (LOAD_START, LOAD_LEN, load_rom);
IOWriteB (LOAD_LO, (lo >> 8) & 0xFF);
IOWriteB (LOAD_LO + 1, lo & 0xFF);
IOWriteB (LOAD_HI, (hi >> 8) & 0xFF);
IOWriteB (LOAD_HI + 1, hi & 0xFF);
IOWriteB (LOAD_CS, cs & 0xFF);
for (i = 0; i < LOAD_LDR; i++)
    fputc (0, of);
for (i = LOAD_START; i < (LOAD_START + LOAD_LEN); i++)
    fputc (IOReadB (i), of);
for (i = 0; i < LOAD_LDR; i++)
    fputc (0, of);
for (i = lo; i <= hi; i++)
    fputc (IOReadB (i), of);
for (i = 0; i < LOAD_LDR; i++)
    fputc (0, of);
return SCPE_OK;
}
