/* sim_tape.h: simulator tape support library definitions

   Copyright (c) 1993-2016, Robert M Supnik
   Copyright (c) 2017-2021, J. David Bryan

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

   15-Dec-21    JDB     Added extended SIMH format support
   06-Oct-21    JDB     Added sim_tape_erase global
   22-Apr-17    JDB     Added MTSE_LEOT value for 4.x compatibility
   18-Jul-16    JDB     Added sim_tape_errecf, sim_tape_errecr functions
   15-Dec-14    JDB     Added tape density validity flags
   04-Nov-14    JDB     Added tape density flags
   11-Oct-14    JDB     Added reverse read half gap, set/show density
   22-Sep-14    JDB     Added tape runaway support
   30-Aug-06    JDB     Added erase gap support
   14-Feb-06    RMS     Added variable tape capacity
   17-Dec-05    RMS     Added write support for Paul Pierce 7b format
   02-May-05    RMS     Added support for Paul Pierce 7b format
*/

#ifndef _SIM_TAPE_H_
#define _SIM_TAPE_H_    0

/* SIMH/E11 tape format.

    31  30  29  29  27  26  25  24  23  22  21   [...]   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | 0   0   0   0   0   0   0   0 | 0   0   0   [...]   0   0   0 | tape mark
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | 0 | 0   0   0   0   0   0   0 |          length > 0           | good data record
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | 1 | 0   0   0   0   0   0   0 |          length > 0           | bad data record
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | 1   1   1   1   1   1   1   1 | 1   1   1   [...]   1   1   0 | erase gap
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | 1   1   1   1   1   1   1   1 | 1   1   1   [...]   1   1   1 | end of medium
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

typedef uint32          t_mtrlnt;                       /* record length type */

#define MTR_TMK         0x00000000                      /* tape mark */
#define MTR_EOM         0xFFFFFFFF                      /* end of medium */
#define MTR_GAP         0xFFFFFFFE                      /* primary gap */
#define MTR_FHGAP       0xFFFEFFFF                      /* fwd half gap (overwrite) */
#define MTR_RHGAP       0xFFFF0000                      /* rev half gap (overwrite) */
#define MTR_MAXLEN      0x00FFFFFF                      /* max len is 24b */
#define MTR_ERF         0x80000000                      /* error flag */
#define MTR_F(x)        ((x) & MTR_ERF)                 /* record error flg */
#define MTR_L(x)        ((x) & ~MTR_ERF)                /* record length */

/* Extended SIMH tape format.

    31  30  29  29  27  26  25  24  23  22  21   [...]   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | control class |             marker-specific value             | marker
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | control class |               data length value               | record
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define MTR_V_RECLN     0                               /* record length offset */
#define MTR_W_RECLN     28                              /* record length width */
#define MTR_M_RECLN     ((1u << MTR_W_RECLN) - 1)       /* record length mask */
#define MTR_RECLN       (MTR_M_RECLN << MTR_V_RECLN)    /* record length field mask */

#define MTR_V_CLASS     MTR_W_RECLN                     /* class field offset */
#define MTR_W_CLASS     4                               /* class field width */
#define MTR_M_CLASS     ((1u << MTR_W_CLASS) - 1)       /* class number mask */
#define MTR_CLASS       (MTR_M_CLASS << MTR_V_CLASS)    /* class field mask */

#define MTR_RL(m)       ((m) & MTR_RECLN)               /* record length */
#define MTR_CF(m)       ((m) & MTR_CLASS)               /* class field */

#define MTR_NF(cn)      ((t_mtrlnt) (((cn) << MTR_V_CLASS) & MTR_CLASS))    /* class number to class field */
#define MTR_FN(cf)      ((uint32) (((cf) & MTR_CLASS) >> MTR_V_CLASS))      /* class field to class number */

#define MTCN_GOOD       0u                              /* good data record class number */
#define MTCN_PMARK      7u                              /* private marker class number */
#define MTCN_BAD        8u                              /* bad data record class number */
#define MTCN_DESC       14u                             /* standard tape description record class number */
#define MTCN_SMARK      15u                             /* standard marker class number */

#define MTC_GOOD        MTR_NF (MTCN_GOOD)              /* good data record class field */
#define MTC_PMARK       MTR_NF (MTCN_PMARK)             /* private marker class field */
#define MTC_BAD         MTR_NF (MTCN_BAD)               /* bad data record class field */
#define MTC_DESC        MTR_NF (MTCN_DESC)              /* standard tape description record class field */
#define MTC_SMARK       MTR_NF (MTCN_SMARK)             /* standard marker class field */

#define MTR_NB(cn)      (1u << (cn))                    /* class number to class bit */
#define MTR_FB(m)       (MTR_NB (MTR_FN (m)))           /* class field to class bit */

#define MTB_GOOD        MTR_NB (MTCN_GOOD)              /* good data record class bit */
#define MTB_PMARK       MTR_NB (MTCN_PMARK)             /* private marker class bit */
#define MTB_BAD         MTR_NB (MTCN_BAD)               /* bad data record class bit */
#define MTB_DESC        MTR_NB (MTCN_DESC)              /* standard tape description record class bit */
#define MTB_SMARK       MTR_NB (MTCN_SMARK)             /* standard marker class bit */

#define MTB_PRIVREC     (MTR_NB (1) | MTR_NB (2) | \
                         MTR_NB (3) | MTR_NB (4) | \
                         MTR_NB (5) | MTR_NB (6))       /* private record class bits */

#define MTB_RSVDREC     (MTR_NB  (9) | MTR_NB (10) | \
                         MTR_NB (11) | MTR_NB (12) | \
                         MTR_NB (13))                   /* reserved record class bits */

#define MTB_STANDARD    (MTB_GOOD | MTB_BAD)                    /* standard record class bits */
#define MTB_EXTENDED    (MTB_STANDARD | MTB_PRIVREC | MTB_DESC) /* extended record class bits */

#define MTB_MARKERSET   (MTB_PMARK | MTB_SMARK)                 /* all marker class bits */
#define MTB_RECORDSET   (~MTB_MARKERSET)                        /* all record class bits */

/* TPC tape format */

typedef uint16          t_tpclnt;                       /* magtape rec lnt */

/* P7B tape format */

#define P7B_SOR         0x80                            /* start of record */
#define P7B_PAR         0x40                            /* parity */
#define P7B_DATA        0x3F                            /* data */
#define P7B_DPAR        (P7B_PAR|P7B_DATA)              /* data and parity */
#define P7B_EOF         0x0F                            /* eof character */

#define TPC_TMK         0x0000                          /* tape mark */

/* Unit flags */

#define MTUF_F_STD      0                               /* SIMH standard format */
#define MTUF_F_E11      1                               /* E11 format */
#define MTUF_F_TPC      2                               /* TPC format */
#define MTUF_F_P7B      3                               /* P7B format */
#define MTUF_F_TDF      4                               /* TDF format (not implemented) */
#define MTUF_F_EXT      5                               /* SIMH extended format */

#define MTUF_V_PNU      (UNIT_V_UF + 0)                 /* position not upd */
#define MTUF_V_WLK      (UNIT_V_UF + 1)                 /* write locked */
#define MTUF_V_FMT      (UNIT_V_UF + 2)                 /* tape file format */
#define MTUF_W_FMT      3                               /* 3b of formats */
#define MTUF_N_FMT      (1u << MTUF_W_FMT)              /* number of formats */
#define MTUF_M_FMT      ((1u << MTUF_W_FMT) - 1)
#define MTUF_V_UF       (MTUF_V_FMT + MTUF_W_FMT)
#define MTUF_PNU        (1u << MTUF_V_PNU)
#define MTUF_WLK        (1u << MTUF_V_WLK)
#define MTUF_FMT        (MTUF_M_FMT << MTUF_V_FMT)
#define MTUF_WRP        (MTUF_WLK | UNIT_RO)

#define MT_F_STD        (MTUF_F_STD << MTUF_V_FMT)
#define MT_F_E11        (MTUF_F_E11 << MTUF_V_FMT)
#define MT_F_TPC        (MTUF_F_TPC << MTUF_V_FMT)
#define MT_F_P7B        (MTUF_F_P7B << MTUF_V_FMT)
#define MT_F_TDF        (MTUF_F_TDF << MTUF_V_FMT)
#define MT_F_EXT        (MTUF_F_EXT << MTUF_V_FMT)

#define MT_SET_PNU(u)   (u)->flags = (u)->flags | MTUF_PNU
#define MT_CLR_PNU(u)   (u)->flags = (u)->flags & ~MTUF_PNU
#define MT_TST_PNU(u)   ((u)->flags & MTUF_PNU)
#define MT_GET_FMT(u)   (((u)->flags >> MTUF_V_FMT) & MTUF_M_FMT)

#define MT_DENS_NONE    0                               /* density not set */
#define MT_DENS_200     1                               /* 200 bpi NRZI */
#define MT_DENS_556     2                               /* 556 bpi NRZI */
#define MT_DENS_800     3                               /* 800 bpi NRZI */
#define MT_DENS_1600    4                               /* 1600 bpi PE */
#define MT_DENS_6250    5                               /* 6250 bpi GCR */

#define MTVF_DENS_MASK  (((1u << UNIT_W_DF_TAPE) - 1) << UNIT_V_DF_TAPE)
#define MT_DENS(f)      (((f) & MTVF_DENS_MASK) >> UNIT_V_DF_TAPE)

#define MT_NONE_VALID   (1u << MT_DENS_NONE)            /* density not set is valid */
#define MT_200_VALID    (1u << MT_DENS_200)             /* 200 bpi is valid */
#define MT_556_VALID    (1u << MT_DENS_556)             /* 556 bpi is valid */
#define MT_800_VALID    (1u << MT_DENS_800)             /* 800 bpi is valid */
#define MT_1600_VALID   (1u << MT_DENS_1600)            /* 1600 bpi is valid */
#define MT_6250_VALID   (1u << MT_DENS_6250)            /* 6250 bpi is valid */

/* Return status codes */

#define MTSE_OK         0                               /* no error */
#define MTSE_TMK        1                               /* tape mark */
#define MTSE_UNATT      2                               /* unattached */
#define MTSE_IOERR      3                               /* IO error */
#define MTSE_INVRL      4                               /* invalid rec lnt */
#define MTSE_FMT        5                               /* invalid format */
#define MTSE_BOT        6                               /* beginning of tape */
#define MTSE_EOM        7                               /* end of medium */
#define MTSE_RECE       8                               /* error in record */
#define MTSE_WRP        9                               /* write protected */
#define MTSE_LEOT       10                              /* Logical End Of Tape (4.x) */
#define MTSE_RUNAWAY    11                              /* tape runaway */
#define MTSE_RESERVED   12                              /* reserved class */

/* Prototypes */

t_stat sim_tape_attach (UNIT *uptr, char *cptr);
t_stat sim_tape_detach (UNIT *uptr);
t_stat sim_tape_rdrecf (UNIT *uptr, uint8 *buf, t_mtrlnt *clbc, t_mtrlnt max);
t_stat sim_tape_rdrecr (UNIT *uptr, uint8 *buf, t_mtrlnt *clbc, t_mtrlnt max);
t_stat sim_tape_wrrecf (UNIT *uptr, uint8 *buf, t_mtrlnt clbc);
t_stat sim_tape_wrmrk (UNIT *uptr, t_mtrlnt mk);
t_stat sim_tape_wrtmk (UNIT *uptr);
t_stat sim_tape_wreom (UNIT *uptr);
t_stat sim_tape_wrgap (UNIT *uptr, uint32 gaplen);
t_stat sim_tape_erase  (UNIT *uptr, t_mtrlnt bc);
t_stat sim_tape_errecf (UNIT *uptr, t_mtrlnt bc);
t_stat sim_tape_errecr (UNIT *uptr, t_mtrlnt bc);
t_stat sim_tape_sprecf (UNIT *uptr, t_mtrlnt *bc);
t_stat sim_tape_sprecr (UNIT *uptr, t_mtrlnt *bc);
t_stat sim_tape_rewind (UNIT *uptr);
t_stat sim_tape_reset (UNIT *uptr);
t_bool sim_tape_bot (UNIT *uptr);
t_bool sim_tape_wrp (UNIT *uptr);
t_bool sim_tape_eot (UNIT *uptr);
t_stat sim_tape_set_fmt (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat sim_tape_show_fmt (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat sim_tape_set_capac (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat sim_tape_show_capac (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat sim_tape_set_dens (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat sim_tape_show_dens (FILE *st, UNIT *uptr, int32 val, void *desc);

#endif
