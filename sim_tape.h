/* sim_tape.h: simulator tape support library definitions

   Copyright (c) 1993-2008, Robert M Supnik

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

   18-Jul-16    JDB     Added sim_tape_errecf, sim_tape_errecr functions
   15-Dec-14    JDB     Added tape density validity flags
   04-Nov-14    JDB     Added tape density flags
   11-Oct-14    JDB     Added reverse read half gap, set/show density
   22-Sep-14    JDB     Added tape runaway support
   23-Jan-12    MP      Added support for Logical EOT detection while positioning
   05-Feb-11    MP      Add Asynch I/O support
   30-Aug-06    JDB     Added erase gap support
   14-Feb-06    RMS     Added variable tape capacity
   17-Dec-05    RMS     Added write support for Paul Pierce 7b format
   02-May-05    RMS     Added support for Paul Pierce 7b format
*/

#ifndef SIM_TAPE_H_
#define SIM_TAPE_H_    0

#ifdef  __cplusplus
extern "C" {
#endif

/* SIMH/E11 tape format */

typedef uint32          t_mtrlnt;                       /* magtape rec lnt */

#define MTR_TMK         0x00000000                      /* tape mark */
#define MTR_EOM         0xFFFFFFFF                      /* end of medium */
#define MTR_GAP         0xFFFFFFFE                      /* primary gap */
#define MTR_RRGAP       0xFFFFFFFF                      /* reverse read half gap */
#define MTR_FHGAP       0xFFFEFFFF                      /* fwd half gap (overwrite) */
#define MTR_RHGAP       0xFFFF0000                      /* rev half gap (overwrite) */
#define MTR_M_RHGAP     (~0x000080FF)                   /* range mask for rev gap */
#define MTR_MAXLEN      0x00FFFFFF                      /* max len is 24b */
#define MTR_ERF         0x80000000                      /* error flag */
#define MTR_F(x)        ((x) & MTR_ERF)                 /* record error flg */
#define MTR_L(x)        ((t_mtrlnt)((x) & ~MTR_ERF))    /* record length */

/* TPC tape format */

typedef uint16          t_tpclnt;                       /* magtape rec lnt */

#define TPC_TMK         0x0000                          /* tape mark */
#define TPC_EOM         0xFFFF                          /* end of medium */


/* P7B tape format */

#define P7B_SOR         0x80                            /* start of record */
#define P7B_PAR         0x40                            /* parity */
#define P7B_DATA        0x3F                            /* data */
#define P7B_DPAR        (P7B_PAR|P7B_DATA)              /* data and parity */
#define P7B_EOF         0x0F                            /* eof character */

/* AWS tape format */

typedef uint16          t_awslnt;                       /* magtape rec lnt */
typedef struct {
    t_awslnt    nxtlen;
    t_awslnt    prelen;
    t_awslnt    rectyp;
#define AWS_TMK         0x0040
#define AWS_REC         0x00A0
    } t_awshdr;

/* TAR tape format */

#define TAR_DFLT_RECSIZE     10240                      /* Default Fixed record size */

/* Unit flags */

#define MTUF_V_UF       (UNIT_V_UF + 0)
#define MTUF_F_STD      0                               /* SIMH format */
#define MTUF_F_E11      1                               /* E11 format */
#define MTUF_F_TPC      2                               /* TPC format */
#define MTUF_F_P7B      3                               /* P7B format */
#define MTUF_F_AWS      4                               /* AWS format */
#define MTUF_F_TAR      5                               /* TAR format */
/* MT_GET_FMT() >= MTUF_F_ANSI is a MEMORY_TAPE image */
#define MTUF_F_ANSI     6                               /* ANSI format */
#define MTUF_F_FIXED    7                               /* FIXED format */
#define MTUF_F_DOS11    8                               /* DOS11 format */

#define MTAT_F_VMS      0                               /* VMS ANSI type */
#define MTAT_F_RSX11    1                               /* RSX-11 ANSI type */
#define MTAT_F_RT11     2                               /* RT-11 ANSI type */
#define MTAT_F_RSTS     3                               /* RSTS ANSI type */
#define MTAT_F_VAR      4                               /* RSTS VAR ANSI type */

#define MTUF_WLK        UNIT_WLK
#define MTUF_WRP        (MTUF_WLK | UNIT_RO)

#define MT_SET_PNU(u)   (u)->dynflags |= UNIT_TAPE_PNU
#define MT_CLR_PNU(u)   (u)->dynflags &= ~UNIT_TAPE_PNU
#define MT_TST_PNU(u)   ((u)->dynflags & UNIT_TAPE_PNU)
#define MT_SET_INMRK(u) (u)->dynflags = (u)->dynflags | UNIT_TAPE_MRK
#define MT_CLR_INMRK(u) (u)->dynflags = (u)->dynflags & ~UNIT_TAPE_MRK
#define MT_TST_INMRK(u) ((u)->dynflags & UNIT_TAPE_MRK)
#define MT_GET_FMT(u)   (((u)->dynflags >> UNIT_V_TAPE_FMT) & ((1 << UNIT_S_TAPE_FMT) - 1))
#define MT_GET_ANSI_TYP(u)   (((u)->dynflags >> UNIT_V_TAPE_ANSI) & ((1 << UNIT_S_TAPE_ANSI) - 1))

/* sim_tape_position Position Flags */

#define MTPOS_V_REW     3
#define MTPOS_M_REW     (1u << MTPOS_V_REW)            /* Rewind First */
#define MTPOS_V_REV     2
#define MTPOS_M_REV     (1u << MTPOS_V_REV)            /* Reverse Direction */
#define MTPOS_V_OBJ     1
#define MTPOS_M_OBJ     (1u << MTPOS_V_OBJ)            /* Objects vs Records/Files */
#define MTPOS_V_DLE     4
#define MTPOS_M_DLE     (1u << MTPOS_V_DLE)            /* Detect LEOT */

/* Tape density values */

#define MT_DENS_NONE    0                               /* density not set */
#define MT_DENS_200     1                               /* 200 bpi NRZI */
#define MT_DENS_556     2                               /* 556 bpi NRZI */
#define MT_DENS_800     3                               /* 800 bpi NRZI */
#define MT_DENS_1600    4                               /* 1600 bpi PE */
#define MT_DENS_6250    5                               /* 6250 bpi GCR */

#define MTVF_DENS_MASK  (((1u << UNIT_S_DF_TAPE) - 1) << UNIT_V_DF_TAPE)
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
#define MTSE_LEOT       10                              /* Logical End Of Tape */
#define MTSE_RUNAWAY    11                              /* tape runaway */
#define MTSE_MAX_ERR    11

typedef void (*TAPE_PCALLBACK)(UNIT *unit, t_stat status);

/* Tape Internal Debug flags */

#define MTSE_DBG_API   0x10000000                       /* API Trace */
#define MTSE_DBG_DAT   0x20000000                       /* Debug Data */
#define MTSE_DBG_POS   0x40000000                       /* Debug Positioning activities */
#define MTSE_DBG_STR   0x80000000                       /* Debug Tape Structure */

/* Prototypes */

t_stat sim_tape_init (void);
t_stat sim_tape_attach_ex (UNIT *uptr, const char *cptr, uint32 dbit, int completion_delay);
t_stat sim_tape_attach (UNIT *uptr, CONST char *cptr);
t_stat sim_tape_detach (UNIT *uptr);
t_stat sim_tape_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat sim_tape_rdrecf (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max);
t_stat sim_tape_rdrecf_a (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max, TAPE_PCALLBACK callback);
t_stat sim_tape_rdrecr (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max);
t_stat sim_tape_rdrecr_a (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max, TAPE_PCALLBACK callback);
t_stat sim_tape_wrrecf (UNIT *uptr, uint8 *buf, t_mtrlnt bc);
t_stat sim_tape_wrrecf_a (UNIT *uptr, uint8 *buf, t_mtrlnt bc, TAPE_PCALLBACK callback);
t_stat sim_tape_wrtmk (UNIT *uptr);
t_stat sim_tape_wrtmk_a (UNIT *uptr, TAPE_PCALLBACK callback);
t_stat sim_tape_wreom (UNIT *uptr);
t_stat sim_tape_wreom_a (UNIT *uptr, TAPE_PCALLBACK callback);
t_stat sim_tape_wreomrw (UNIT *uptr);
t_stat sim_tape_wreomrw_a (UNIT *uptr, TAPE_PCALLBACK callback);
t_stat sim_tape_wrgap (UNIT *uptr, uint32 gaplen);
t_stat sim_tape_wrgap_a (UNIT *uptr, uint32 gaplen, TAPE_PCALLBACK callback);
t_stat sim_tape_errecf (UNIT *uptr, t_mtrlnt bc);
t_stat sim_tape_errecr (UNIT *uptr, t_mtrlnt bc);
t_stat sim_tape_sprecf (UNIT *uptr, t_mtrlnt *bc);
t_stat sim_tape_sprecf_a (UNIT *uptr, t_mtrlnt *bc, TAPE_PCALLBACK callback);
t_stat sim_tape_sprecsf (UNIT *uptr, uint32 count, uint32 *skipped);
t_stat sim_tape_sprecsf_a (UNIT *uptr, uint32 count, uint32 *skipped, TAPE_PCALLBACK callback);
t_stat sim_tape_spfilef (UNIT *uptr, uint32 count, uint32 *skipped);
t_stat sim_tape_spfilef_a (UNIT *uptr, uint32 count, uint32 *skipped, TAPE_PCALLBACK callback);
t_stat sim_tape_spfilebyrecf (UNIT *uptr, uint32 count, uint32 *skipped, uint32 *recsskipped, t_bool check_leot);
t_stat sim_tape_spfilebyrecf_a (UNIT *uptr, uint32 count, uint32 *skipped, uint32 *recsskipped, t_bool check_leot, TAPE_PCALLBACK callback);
t_stat sim_tape_sprecr (UNIT *uptr, t_mtrlnt *bc);
t_stat sim_tape_sprecr_a (UNIT *uptr, t_mtrlnt *bc, TAPE_PCALLBACK callback);
t_stat sim_tape_sprecsr (UNIT *uptr, uint32 count, uint32 *skipped);
t_stat sim_tape_sprecsr_a (UNIT *uptr, uint32 count, uint32 *skipped, TAPE_PCALLBACK callback);
t_stat sim_tape_spfiler (UNIT *uptr, uint32 count, uint32 *skipped);
t_stat sim_tape_spfiler_a (UNIT *uptr, uint32 count, uint32 *skipped, TAPE_PCALLBACK callback);
t_stat sim_tape_spfilebyrecr (UNIT *uptr, uint32 count, uint32 *skipped, uint32 *recsskipped);
t_stat sim_tape_spfilebyrecr_a (UNIT *uptr, uint32 count, uint32 *skipped, uint32 *recsskipped, TAPE_PCALLBACK callback);
t_stat sim_tape_rewind (UNIT *uptr);
t_stat sim_tape_rewind_a (UNIT *uptr, TAPE_PCALLBACK callback);
t_stat sim_tape_position (UNIT *uptr, uint32 flags, uint32 recs, uint32 *recskipped, uint32 files, uint32 *fileskipped, uint32 *objectsskipped);
t_stat sim_tape_position_a (UNIT *uptr, uint32 flags, uint32 recs, uint32 *recsskipped, uint32 files, uint32 *filesskipped, uint32 *objectsskipped, TAPE_PCALLBACK callback);
t_stat sim_tape_reset (UNIT *uptr);
t_bool sim_tape_bot (UNIT *uptr);
t_bool sim_tape_wrp (UNIT *uptr);
t_bool sim_tape_eot (UNIT *uptr);
t_stat sim_tape_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sim_tape_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat sim_tape_set_capac (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sim_tape_show_capac (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat sim_tape_set_dens (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sim_tape_show_dens (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat sim_tape_density_supported (char *string, size_t string_size, int32 valid_bits);
t_stat sim_tape_set_asynch (UNIT *uptr, int latency);
t_stat sim_tape_clr_asynch (UNIT *uptr);
t_stat sim_tape_test (DEVICE *dptr, const char *cptr);
t_stat sim_tape_add_debug (DEVICE *dptr);

#ifdef  __cplusplus
}
#endif

#endif
