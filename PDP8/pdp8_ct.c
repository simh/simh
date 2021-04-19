/* pdp8_ct.c: PDP-8 cassette tape simulator

   Copyright (c) 2006-2013, Robert M Supnik

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

   ct           TA8E/TU60 cassette tape

   17-Sep-07    RMS     Changed to use central set_bootpc routine
   13-Aug-07    RMS     Fixed handling of BEOT
   06-Aug-07    RMS     Foward op at BOT skips initial file gap
   30-May-07    RMS     Fixed typo (Norm Lastovica)

   Magnetic tapes are represented as a series of variable records
   of the form:

        32b byte count
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.

   Cassette format differs in one very significant way: it has file gaps
   rather than file marks.  If the controller spaces or reads into a file
   gap and then reverses direction, the file gap is not seen again.  This
   is in contrast to magnetic tapes, where the file mark is a character
   sequence and is seen again if direction is reversed.  In addition,
   cassettes have an initial file gap which is automatically skipped on
   forward operations from beginning of tape.

   Note that the read and write sequences for the cassette are asymmetric:

   Read:    KLSA            /SELECT READ
            KGOA            /INIT READ, CLEAR DF
            <data flag sets, char in buf>
            KGOA            /READ 1ST CHAR, CLEAR DF
            DCA CHAR
            :
            <data flag sets, char in buf>
            KGOA            /READ LAST CHAR, CLEAR DF
            DCA CHAR
            <data flag sets, CRC1 in buf>
            KLSA            /SELECT CRC MODE
            KGOA            /READ 1ST CRC
            <data flag sets, CRC2 in buf>
            KGOA            /READ 2ND CRC
            <ready flag/CRC error flag sets>

   Write:   KLSA            /SELECT WRITE
            TAD CHAR        /1ST CHAR
            KGOA            /INIT WRITE, CHAR TO BUF, CLEAR DF
            <data flag sets, char to tape>
            :
            TAD CHAR        /LAST CHAR
            KGOA            /CHAR TO BUF, CLEAR DF
            <data flag sets, char to tape>
            KLSA            /SELECT CRC MODE
            KGOA            /WRITE CRC, CLEAR DF
            <ready flag sets, CRC on tape>
*/

#include "pdp8_defs.h"
#include "sim_tape.h"

#define CT_NUMDR        2                               /* #drives */
#define FNC             u3                              /* unit function */
#define UST             u4                              /* unit status */
#define CT_MAXFR        (CT_SIZE)                       /* max record lnt */
#define CT_SIZE         93000                           /* chars/tape */

/* Status Register A */

#define SRA_ENAB        0200                            /* enable */
#define SRA_V_UNIT      6                               /* unit */
#define SRA_M_UNIT      (CT_NUMDR - 1)
#define SRA_V_FNC       3                               /* function */
#define SRA_M_FNC       07
#define  SRA_READ        00
#define  SRA_REW         01
#define  SRA_WRITE       02
#define  SRA_SRF         03
#define  SRA_WFG         04
#define  SRA_SRB         05
#define  SRA_CRC         06
#define  SRA_SFF         07
#define SRA_2ND         010
#define SRA_IE          0001                            /* int enable */
#define GET_UNIT(x)     (((x) >> SRA_V_UNIT) & SRA_M_UNIT)
#define GET_FNC(x)      (((x) >> SRA_V_FNC) & SRA_M_FNC)

/* Function code flags */

#define OP_WRI          01                              /* op is a write */
#define OP_REV          02                              /* op is rev motion */
#define OP_FWD          04                              /* op is fwd motion */

/* Unit status flags */

#define UST_REV         (OP_REV)                        /* last op was rev */
#define UST_GAP         01                              /* last op hit gap */

/* Status Register B, ^ = computed on the fly */

#define SRB_WLE         0400                            /* "write lock err" */
#define SRB_CRC         0200                            /* CRC error */
#define SRB_TIM         0100                            /* timing error */
#define SRB_BEOT        0040                            /* ^BOT/EOT */
#define SRB_EOF         0020                            /* end of file */
#define SRB_EMP         0010                            /* ^drive empty */
#define SRB_REW         0004                            /* rewinding */
#define SRB_WLK         0002                            /* ^write locked */
#define SRB_RDY         0001                            /* ^ready */
#define SRB_ALLERR      (SRB_WLE|SRB_CRC|SRB_TIM|SRB_BEOT|SRB_EOF|SRB_EMP)
#define SRB_XFRERR      (SRB_WLE|SRB_CRC|SRB_TIM|SRB_EOF)

extern int32 int_req, stop_inst;
extern UNIT cpu_unit;

uint32 ct_sra = 0;                                      /* status reg A */
uint32 ct_srb = 0;                                      /* status reg B */
uint32 ct_db = 0;                                       /* data buffer */
uint32 ct_df = 0;                                       /* data flag */
uint32 ct_write = 0;                                    /* TU60 write flag */
uint32 ct_bptr = 0;                                     /* buf ptr */
uint32 ct_blnt = 0;                                     /* buf length */
int32 ct_stime = 1000;                                  /* start time */
int32 ct_ctime = 100;                                   /* char latency */
uint32 ct_stopioe = 1;                                  /* stop on error */
uint8 *ct_xb = NULL;                                    /* transfer buffer */
static uint8 ct_fnc_tab[SRA_M_FNC + 1] = {
    OP_FWD,        0     , OP_WRI|OP_FWD, OP_REV,
    OP_WRI|OP_FWD, OP_REV, 0,             OP_FWD
    };

int32 ct70 (int32 IR, int32 AC);
t_stat ct_svc (UNIT *uptr);
t_stat ct_reset (DEVICE *dptr);
t_stat ct_attach (UNIT *uptr, CONST char *cptr);
t_stat ct_detach (UNIT *uptr);
t_stat ct_boot (int32 unitno, DEVICE *dptr);
const char *ct_description (DEVICE *dptr);
uint32 ct_updsta (UNIT *uptr);
int32 ct_go_start (int32 AC);
int32 ct_go_cont (UNIT *uptr, int32 AC);
t_stat ct_map_err (UNIT *uptr, t_stat st);
UNIT *ct_busy (void);
void ct_set_df (t_bool timchk);
t_bool ct_read_char (void);
uint32 ct_crc (uint8 *buf, uint32 cnt);

/* CT data structures

   ct_dev       CT device descriptor
   ct_unit      CT unit list
   ct_reg       CT register list
   ct_mod       CT modifier list
*/

DIB ct_dib = { DEV_CT, 1, { &ct70 } };

UNIT ct_unit[] = {
    { UDATA (&ct_svc, UNIT_ATTABLE+UNIT_ROABLE, CT_SIZE) },
    { UDATA (&ct_svc, UNIT_ATTABLE+UNIT_ROABLE, CT_SIZE) },
    };

REG ct_reg[] = {
    { ORDATAD (CTSRA, ct_sra, 8, "status register A") },
    { ORDATAD (CTSRB, ct_srb, 8, "status register B") },
    { ORDATAD (CTDB, ct_db, 8, "data buffer") },
    { FLDATAD (CTDF, ct_df, 0, "data flag") },
    { FLDATAD (RDY, ct_srb, 0, "ready flag") },
    { FLDATAD (WLE, ct_srb, 8, "write lock error") },
    { FLDATAD (WRITE, ct_write, 0, "TA60 write operation flag") },
    { FLDATAD (INT, int_req, INT_V_CT, "interrupt request") },
    { DRDATAD (BPTR, ct_bptr, 17, "buffer pointer") },
    { DRDATAD (BLNT, ct_blnt, 17, "buffer length") },
    { DRDATAD (STIME, ct_stime, 24, "operation start time"), PV_LEFT + REG_NZ },
    { DRDATAD (CTIME, ct_ctime, 24, "character latency"), PV_LEFT + REG_NZ },
    { FLDATAD (STOP_IOE, ct_stopioe, 0, "stop on I/O errors flag") },
    { URDATA (UFNC, ct_unit[0].FNC, 8, 4, 0, CT_NUMDR, REG_HRO) },
    { URDATA (UST, ct_unit[0].UST, 8, 2, 0, CT_NUMDR, REG_HRO) },
    { URDATAD (POS, ct_unit[0].pos, 10, T_ADDR_W, 0,
              CT_NUMDR, PV_LEFT | REG_RO, "position, units 0-1") },
    { FLDATA (DEVNUM, ct_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB ct_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable cassette tape" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock cassette tape" },
//    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
//      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { MTAB_XTD|MTAB_VUN, 0, "CAPACITY", NULL,
      NULL, &sim_tape_show_capac, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE ct_dev = {
    "CT", ct_unit, ct_reg, ct_mod,
    CT_NUMDR, 10, 31, 1, 8, 8,
    NULL, NULL, &ct_reset,
    &ct_boot, &ct_attach, &ct_detach,
    &ct_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_TAPE,
    0, NULL, NULL, NULL, NULL, NULL, NULL,
    &ct_description
    };

/* IOT routines */

int32 ct70 (int32 IR, int32 AC)
{
int32 srb;
UNIT *uptr;

srb = ct_updsta (NULL);                                 /* update status */
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 0:                                             /* KCLR */
        ct_reset (&ct_dev);                             /* reset the world */
        break;

    case 1:                                             /* KSDR */
        if (ct_df)
            AC |= IOT_SKP;
        break;

    case 2:                                             /* KSEN */
        if (srb & SRB_ALLERR)
            AC |= IOT_SKP;
        break;

    case 3:                                             /* KSBF */
        if ((srb & SRB_RDY) && !(srb & SRB_EMP))
            AC |= IOT_SKP;
        break;

    case 4:                                             /* KLSA */
        ct_sra = AC & 0377;
        ct_updsta (NULL);
        return ct_sra ^ 0377;

    case 5:                                             /* KSAF */
        if (ct_df || (srb & (SRB_ALLERR|SRB_RDY)))
            AC |= IOT_SKP;
        break;

    case 6:                                             /* KGOA */
        ct_df = 0;                                      /* clear data flag */
        if ((uptr = ct_busy ()))                        /* op in progress? */
            AC = ct_go_cont (uptr, AC);                 /* yes */
        else AC = ct_go_start (AC);                     /* no, start */
        ct_updsta (NULL);
        break;

    case 7:                                             /* KSRB */
        return srb & 0377;
        }                                               /* end switch */

return AC;
}

/* Start a new operation - cassette is not busy */

int32 ct_go_start (int32 AC)
{
UNIT *uptr = ct_dev.units + GET_UNIT (ct_sra);
uint32 fnc = GET_FNC (ct_sra);
uint32 flg = ct_fnc_tab[fnc];
uint32 old_ust = uptr->UST;

if (DEBUG_PRS (ct_dev)) fprintf (sim_deb,
    ">>CT start: op=%o, old_sta = %o, pos=%d\n",
    fnc, uptr->UST, uptr->pos);
if ((ct_sra & SRA_ENAB) && (uptr->flags & UNIT_ATT)) {  /* enabled, att? */
    ct_srb &= ~(SRB_XFRERR|SRB_REW);                    /* clear err, rew */
    if (flg & OP_WRI) {                                 /* write-type op? */
        if (sim_tape_wrp (uptr)) {                      /* locked? */
            ct_srb |= SRB_WLE;                          /* set flag, abort */
            return AC;
            }
        ct_write = 1;                                   /* set TU60 wr flag */
        ct_db = AC & 0377;
        }
    else {
        ct_write = 0;
        ct_db = 0;
        }
    ct_srb &= ~SRB_BEOT;                                /* tape in motion */
    if (fnc == SRA_REW)                                 /* rew? set flag */
        ct_srb |= SRB_REW;
    if ((fnc != SRA_REW) && !(flg & OP_WRI)) {          /* read cmd? */
        t_mtrlnt t;
        t_stat st;
        uptr->UST = flg & UST_REV;                      /* save direction */
        if (sim_tape_bot (uptr) && (flg & OP_FWD)) {    /* spc/read fwd bot? */
            st = sim_tape_rdrecf (uptr, ct_xb, &t, CT_MAXFR); /* skip file gap */
            if (st != MTSE_TMK)                         /* not there? */
                sim_tape_rewind (uptr);                 /* restore tap pos */
            else old_ust = 0;                           /* defang next */
            }
        if ((old_ust ^ uptr->UST) == (UST_REV|UST_GAP)) { /* rev in gap? */
            if (DEBUG_PRS (ct_dev)) fprintf (sim_deb,
                ">>CT skip gap: op=%o, old_sta = %o, pos=%d\n",
                fnc, uptr->UST, uptr->pos);
            if (uptr->UST)                              /* skip file gap */
                (void)sim_tape_rdrecr (uptr, ct_xb, &t, CT_MAXFR);
            else
                (void)sim_tape_rdrecf (uptr, ct_xb, &t, CT_MAXFR);
            }
        }
    else uptr->UST = 0;
    ct_bptr = 0;                                        /* init buffer */
    ct_blnt = 0;
    uptr->FNC = fnc;                                    /* save function */
    sim_activate (uptr, ct_stime);                      /* schedule op */
    }
if ((fnc == SRA_READ) || (fnc == SRA_CRC))              /* read or CRC? */
    return 0;                                           /* get "char" */
return AC;
}

/* Continue an in-progress operation - cassette is in motion */

int32 ct_go_cont (UNIT *uptr, int32 AC)
{
int32 fnc = GET_FNC (ct_sra);

switch (fnc) {                                          /* case on function */

    case SRA_READ:                                      /* read */
        return ct_db;                                   /* return data */

    case SRA_WRITE:                                     /* write */
        ct_db = AC & 0377;                              /* save data */
        break;

    case SRA_CRC:                                       /* CRC */
        if ((uptr->FNC & SRA_M_FNC) != SRA_CRC)         /* if not CRC */
            uptr->FNC = SRA_CRC;                        /* start CRC seq */
        if (!ct_write)                                  /* read? AC <- buf */
            return ct_db;
        break;

    default:
        break;
    }

return AC;
}

/* Unit service */

t_stat ct_svc (UNIT *uptr)
{
uint32 i, crc;
uint32 flgs = ct_fnc_tab[uptr->FNC & SRA_M_FNC];
t_mtrlnt tbc;
t_stat st, r;

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    ct_updsta (uptr);                                   /* update status */
    return (ct_stopioe? SCPE_UNATT: SCPE_OK);
    }
if (((flgs & OP_REV) && sim_tape_bot (uptr)) ||         /* rev at BOT or */
    ((flgs & OP_FWD) && sim_tape_eot (uptr))) {         /* fwd at EOT? */
    ct_srb |= SRB_BEOT;                                 /* error */
    ct_updsta (uptr);                                   /* op done */
    return SCPE_OK;
    }

r = SCPE_OK;
switch (uptr->FNC) {                                    /* case on function */

    case SRA_READ:                                      /* read start */
        st = sim_tape_rdrecf (uptr, ct_xb, &ct_blnt, CT_MAXFR); /* get rec */
        if (st == MTSE_RECE)                            /* rec in err? */
            ct_srb |= SRB_CRC;
        else if (st != MTSE_OK) {                       /* other error? */
            r = ct_map_err (uptr, st);                  /* map error */
            break;
            }
        crc = ct_crc (ct_xb, ct_blnt);                  /* calculate CRC */
        ct_xb[ct_blnt++] = (crc >> 8) & 0377;           /* append to buffer */
        ct_xb[ct_blnt++] = crc & 0377;
        uptr->FNC |= SRA_2ND;                           /* next state */
        sim_activate (uptr, ct_ctime);                  /* sched next char */
        return SCPE_OK;

    case SRA_READ|SRA_2ND:                              /* read char */
        if (!ct_read_char ())                           /* read, overrun? */
            break;
        ct_set_df (TRUE);                               /* set data flag */
        sim_activate (uptr, ct_ctime);                  /* sched next char */
        return SCPE_OK;

    case SRA_WRITE:                                     /* write start */
        for (i = 0; i < CT_MAXFR; i++)                  /* clear buffer */
            ct_xb[i] = 0;
        uptr->FNC |= SRA_2ND;                           /* next state */
        sim_activate (uptr, ct_ctime);                  /* sched next char */
        return SCPE_OK;

    case SRA_WRITE|SRA_2ND:                             /* write char */
        if ((ct_bptr < CT_MAXFR) &&                     /* room in buf? */
            ((uptr->pos + ct_bptr) < uptr->capac))      /* room on tape? */
            ct_xb[ct_bptr++] = ct_db;                   /* store char */
        ct_set_df (TRUE);                               /* set data flag */
        sim_activate (uptr, ct_ctime);                  /* sched next char */
        return SCPE_OK;

    case SRA_CRC:                                       /* CRC */
        if (ct_write) {                                 /* write? */
           if ((st = sim_tape_wrrecf (uptr, ct_xb, ct_bptr)))/* write, err? */
               r = ct_map_err (uptr, st);               /* map error */
           break;                                       /* write done */
           }
        ct_read_char ();                                /* get second CRC */
        ct_set_df (FALSE);                              /* set df */
        uptr->FNC |= SRA_2ND;                           /* next state */
        sim_activate (uptr, ct_ctime);
        return SCPE_OK;

    case SRA_CRC|SRA_2ND:                               /* second read CRC */
        if (ct_bptr != ct_blnt) {                       /* partial read? */
            crc = ct_crc (ct_xb, ct_bptr);              /* actual CRC */
            if (crc != 0)                               /* must be zero */
                ct_srb |= SRB_CRC;
            }
         break;                                         /* read done */

    case SRA_WFG:                                       /* write file gap */
        if ((st = sim_tape_wrtmk (uptr)))               /* write tmk, err? */
            r = ct_map_err (uptr, st);                  /* map error */
        break;

    case SRA_REW:                                       /* rewind */
        sim_tape_rewind (uptr);
        ct_srb |= SRB_BEOT;                             /* set BOT */
        break;

    case SRA_SRB:                                       /* space rev blk */
        if ((st = sim_tape_sprecr (uptr, &tbc)))        /* space rev, err? */
            r = ct_map_err (uptr, st);                  /* map error */
         break;

    case SRA_SRF:                                       /* space rev file */
        while ((st = sim_tape_sprecr (uptr, &tbc)) == MTSE_OK) ;
        r = ct_map_err (uptr, st);                      /* map error */
        break;

    case SRA_SFF:                                       /* space fwd file */
        while ((st = sim_tape_sprecf (uptr, &tbc)) == MTSE_OK) ;
        r = ct_map_err (uptr, st);                      /* map error */
        break;

    default:                                            /* never get here! */
        return SCPE_IERR;        
        }                                               /* end case */

ct_updsta (uptr);                                       /* update status */
if (DEBUG_PRS (ct_dev)) fprintf (sim_deb,
    ">>CT done: op=%o, statusA = %o, statusB = %o, pos=%d\n",
    uptr->FNC, ct_sra, ct_srb, uptr->pos);
return r;
}

/* Update controller status */

uint32 ct_updsta (UNIT *uptr)
{
int32 srb;

if (uptr == NULL) {                                     /* unit specified? */
    uptr = ct_busy ();                                  /* use busy unit */
    if ((uptr == NULL) && (ct_sra & SRA_ENAB))          /* none busy? */
        uptr = ct_dev.units + GET_UNIT (ct_sra);        /* use sel unit */
    }
else if (ct_srb & SRB_EOF)                              /* save gap */
    uptr->UST |= UST_GAP;
if (uptr) {                                             /* any unit? */
    ct_srb &= ~(SRB_WLK|SRB_EMP|SRB_RDY);               /* clear dyn flags */
    if ((uptr->flags & UNIT_ATT) == 0)                  /* unattached? */
        ct_srb = (ct_srb | SRB_EMP|SRB_WLK) & ~SRB_REW; /* empty, locked */
    if (!sim_is_active (uptr)) {                        /* not busy? */
        ct_srb = (ct_srb | SRB_RDY) & ~SRB_REW;         /* ready, ~rew */
        }
    if (sim_tape_wrp (uptr) || (ct_srb & SRB_REW))      /* locked or rew? */
        ct_srb |= SRB_WLK;                              /* set locked */
    }
if (ct_sra & SRA_ENAB)                                  /* can TA see TU60? */
    srb = ct_srb;
else srb = 0;                                           /* no */
if ((ct_sra & SRA_IE) &&                                /* int enabled? */
    (ct_df || (srb & (SRB_ALLERR|SRB_RDY))))            /* any flag? */
    int_req |= INT_CT;                                  /* set int req */
else int_req &= ~INT_CT;                                /* no, clr int req */
return srb;
}

/* Set data flag */

void ct_set_df (t_bool timchk)
{
if (ct_df && timchk)                                    /* flag still set? */
    ct_srb |= SRB_TIM;
ct_df = 1;                                              /* set data flag */
if (ct_sra & SRA_IE)                                    /* if ie, int req */
    int_req |= INT_CT;
return;
}

/* Read character */

t_bool ct_read_char (void)
{
if (ct_bptr < ct_blnt) {                                /* more chars? */
    ct_db = ct_xb[ct_bptr++];
    return TRUE;
    }
ct_db = 0;
ct_srb |= SRB_CRC;                                      /* overrun */
return FALSE;
}

/* Test if controller busy */

UNIT *ct_busy (void)
{
uint32 u;
UNIT *uptr;

for (u = 0; u < CT_NUMDR; u++) {                        /* loop thru units */
    uptr = ct_dev.units + u;
    if (sim_is_active (uptr))
        return uptr;
    }
return NULL;
}

/* Calculate CRC on buffer */

uint32 ct_crc (uint8 *buf, uint32 cnt)
{
uint32 crc, i, j;

crc = 0;
for (i = 0; i < cnt; i++) {
    crc = crc ^ (((uint32) buf[i]) << 8);
    for (j = 0; j < 8; j++) {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xA001;
        else crc = crc >> 1;
        }
    }
return crc;
}

/* Map error status */

t_stat ct_map_err (UNIT *uptr, t_stat st)
{
switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* unattached */
        ct_srb |= SRB_CRC;
    case MTSE_OK:                                       /* no error */
        return SCPE_IERR;                               /* never get here! */

    case MTSE_TMK:                                      /* end of file */
        ct_srb |= SRB_EOF;
        break;

    case MTSE_IOERR:                                    /* IO error */
        ct_srb |= SRB_CRC;                              /* set crc err */
        if (ct_stopioe)
            return SCPE_IOERR;
        break;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        ct_srb |= SRB_CRC;                              /* set crc err */
        return SCPE_MTRLNT;

    case MTSE_RECE:                                     /* record in error */
    case MTSE_EOM:                                      /* end of medium */
        ct_srb |= SRB_CRC;                              /* set crc err */
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        ct_srb |= SRB_BEOT;                             /* set BOT */
        break;

    case MTSE_WRP:                                      /* write protect */
        ct_srb |= SRB_WLE;                              /* set wlk err */
        break;
        }

return SCPE_OK;
}

/* Reset routine */

t_stat ct_reset (DEVICE *dptr)
{
uint32 u;
UNIT *uptr;

ct_sra = 0;
ct_srb = 0;
ct_df = 0;
ct_db = 0;
ct_write = 0;
ct_bptr = 0;
ct_blnt = 0;
int_req = int_req & ~INT_CT;                            /* clear interrupt */
for (u = 0; u < CT_NUMDR; u++) {                        /* loop thru units */
    uptr = ct_dev.units + u;
    sim_cancel (uptr);                                  /* cancel activity */
    sim_tape_reset (uptr);                              /* reset tape */
    }
if (ct_xb == NULL)
    ct_xb = (uint8 *) calloc (CT_MAXFR + 2, sizeof (uint8));
if (ct_xb == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

/* Attach routine */

t_stat ct_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);
if (r != SCPE_OK)
    return r;
ct_updsta (NULL);
uptr->UST = 0;
return r;
}

/* Detach routine */

t_stat ct_detach (UNIT* uptr)
{
t_stat r;

if (!(uptr->flags & UNIT_ATT))                          /* check attached */
    return SCPE_OK;
r = sim_tape_detach (uptr);
ct_updsta (NULL);
uptr->UST = 0;
return r;
}

/* Bootstrap routine */

#define BOOT_START 04000
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    01237,          /* BOOT,    TAD M50     /change CRC to REW */
    01206,          /* CRCCHK,  TAD L260    /crc op */
    06704,          /*          KLSA        /load op */
    06706,          /*          KGOA        /start */
    06703,          /*          KSBF        /ready? */
    05204,          /* RDCOD,   JMP .-1     /loop */
    07264,          /* L260,    CML STA RAL /L = 1, AC = halt */
    06702,          /*          KSEN        /error? */
    07610,          /*          SKP CLA     /halt on any error */
    03211,          /*          DCA .       /except REW or FFG */
    03636,          /*          DCA I PTR   /TAD I PTR mustn't change L */
    01205,          /*          TAD RDCOD   /read op */
    06704,          /*          KLSA        /load op */
    06706,          /*          KGOA        /start */
    06701,          /* LOOP,    KSDF        /data ready? */
    05216,          /*          JMP .-1     /loop */
    07002,          /*          BSW         /to upper 6b */
    07430,          /*          SZL         /second byte? */
    01636,          /*          TAD I PTR   /yes */
    07022,          /*          CML BSW     /swap back */
    03636,          /*          DCA I PTR   /store in mem */
    07420,          /*          SNL         /done with both bytes? */
    02236,          /*          ISZ PTR     /yes, bump mem ptr */
    02235,          /*          ISZ KNT     /done with record? */
    05215,          /*          JMP LOOP    /next byte */
    07346,          /*          STA CLL RTL */
    07002,          /*          BSW         /AC = 7757 */
    03235,          /*          STA KNT     /now read 200 byte record */
    05201,          /*          JMP CRCCHK  /go check CRC */
    07737,          /* KNT,     7737        /1's compl of byte count */
    03557,          /* PTR,     3557        /load point */
    07730,          /* M50,     7730        /CLA SPA SZL */
    };

t_stat ct_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern uint16 M[];

if ((ct_dib.dev != DEV_CT) || unitno)                   /* only std devno */
     return STOP_NOTSTD;
for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = boot_rom[i];
cpu_set_bootpc (BOOT_START);
return SCPE_OK;
}

const char *ct_description (DEVICE *dptr)
{
return "TA8E/TU60 cassette tape";
}
