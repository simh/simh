/* sim_tape.c: simulator tape support library

   Copyright (c) 1993-2018, Robert M Supnik

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

   Ultimately, this will be a place to hide processing of various tape formats,
   as well as OS-specific direct hardware access.

   27-Dec-18    JDB     Added missing fall through comment in sim_tape_wrrecf
   03-May-17    JDB     Added support for erasing tape marks to sim_tape_errec[fr]
   02-May-17    JDB     Added error checks to sim_fseek calls
   18-Jul-16    JDB     Added sim_tape_errecf, sim_tape_errecr functions
   12-Oct-15    JDB     Fixed bug in sim_tape_rdlntf if gap buffer read ends at EOM
   15-Dec-14    JDB     Changed sim_tape_set_dens to check validity of density change
   04-Nov-14    JDB     Restrict sim_tape_set_fmt to unit unattached
   31-Oct-14    JDB     Fixed gap skip on reverse read
                        Fixed write EOM bug (should not update position)
                        Added set/show density functions, changed sim_tape_wrgap API
                        Buffered forward/reverse gap skip to improve execution time
   22-Sep-14    JDB     Added tape runaway support
   08-Jun-08    JDB     Fixed signed/unsigned warning in sim_tape_set_fmt
   23-Jan-07    JDB     Fixed backspace over gap at BOT
   22-Jan-07    RMS     Fixed bug in P7B format read reclnt rev (found by Rich Cornwell)
   15-Dec-06    RMS     Added support for small capacity tapes
   30-Aug-06    JDB     Added erase gap support
   14-Feb-06    RMS     Added variable tape capacity
   23-Jan-06    JDB     Fixed odd-byte-write problem in sim_tape_wrrecf
   17-Dec-05    RMS     Added write support for Paul Pierce 7b format
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   02-May-05    RMS     Added support for Pierce 7b format
   28-Jul-04    RMS     Fixed bug in writing error records (found by Dave Bryan)
                RMS     Fixed incorrect error codes (found by Dave Bryan)
   05-Jan-04    RMS     Revised for file I/O library
   25-Apr-03    RMS     Added extended file support
   28-Mar-03    RMS     Added E11 and TPC format support

   Public routines:

   sim_tape_attach      attach tape unit
   sim_tape_detach      detach tape unit
   sim_tape_rdrecf      read tape record forward
   sim_tape_rdrecr      read tape record reverse
   sim_tape_wrrecf      write tape record forward
   sim_tape_sprecf      space tape record forward
   sim_tape_sprecr      space tape record reverse
   sim_tape_wrtmk       write tape mark
   sim_tape_wreom       erase remainder of tape
   sim_tape_wrgap       write erase gap
   sim_tape_errecf      erase record forward
   sim_tape_errecr      erase record reverse
   sim_tape_rewind      rewind
   sim_tape_reset       reset unit
   sim_tape_bot         TRUE if at beginning of tape
   sim_tape_eot         TRUE if at or beyond end of tape
   sim_tape_wrp         TRUE if write protected
   sim_tape_set_fmt     set tape format
   sim_tape_show_fmt    show tape format
   sim_tape_set_capac   set tape capacity
   sim_tape_show_capac  show tape capacity
   sim_tape_set_dens    set tape density
   sim_tape_show_dens   show tape density
*/

#include "sim_defs.h"
#include "sim_tape.h"

struct sim_tape_fmt {
    char                *name;                          /* name */
    int32               uflags;                         /* unit flags */
    t_addr              bot;                            /* bot test */
    };

static struct sim_tape_fmt fmts[MTUF_N_FMT] = {
    { "SIMH", 0,       sizeof (t_mtrlnt) - 1 },
    { "E11",  0,       sizeof (t_mtrlnt) - 1 },
    { "TPC",  UNIT_RO, sizeof (t_tpclnt) - 1 },
    { "P7B",  0,       0 },
/*  { "TPF",  UNIT_RO, 0 }, */
    { NULL,   0,       0 }
    };

static const uint32 bpi [] = {                          /* tape density table, indexed by MT_DENS constants */
    0,                                                  /*   0 = MT_DENS_NONE -- density not set */
    200,                                                /*   1 = MT_DENS_200  -- 200 bpi NRZI */
    556,                                                /*   2 = MT_DENS_556  -- 556 bpi NRZI */
    800,                                                /*   3 = MT_DENS_800  -- 800 bpi NRZI */
    1600,                                               /*   4 = MT_DENS_1600 -- 1600 bpi PE */
    6250                                                /*   5 = MT_DENS_6250 -- 6250 bpi GCR */
    };

#define BPI_COUNT       (sizeof (bpi) / sizeof (bpi [0]))   /* count of density table entries */

static t_stat sim_tape_ioerr (UNIT *uptr);
static t_stat sim_tape_wrdata (UNIT *uptr, uint32 dat);
static uint32 sim_tape_tpc_map (UNIT *uptr, t_addr *map);
static t_addr sim_tape_tpc_fnd (UNIT *uptr, t_addr *map);
static t_stat tape_erase_fwd (UNIT *uptr, t_mtrlnt gap_size);
static t_stat tape_erase_rev (UNIT *uptr, t_mtrlnt gap_size);


/* Attach tape unit */

t_stat sim_tape_attach (UNIT *uptr, char *cptr)
{
uint32 objc;
char gbuf[CBUFSIZE];
t_stat r;

if (sim_switches & SWMASK ('F')) {                      /* format spec? */
    cptr = get_glyph (cptr, gbuf, 0);                   /* get spec */
    if (*cptr == 0)                                     /* must be more */
        return SCPE_2FARG;
    if (sim_tape_set_fmt (uptr, 0, gbuf, NULL) != SCPE_OK)
        return SCPE_ARG;
    }
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
switch (MT_GET_FMT (uptr)) {                            /* case on format */

    case MTUF_F_TPC:                                    /* TPC */
        objc = sim_tape_tpc_map (uptr, NULL);           /* get # objects */
        if (objc == 0) {                                /* tape empty? */
            sim_tape_detach (uptr);
            return SCPE_FMT;                            /* yes, complain */
            }
        uptr->filebuf = calloc (objc + 1, sizeof (t_addr));
        if (uptr->filebuf == NULL) {                    /* map allocated? */
            sim_tape_detach (uptr);
            return SCPE_MEM;                            /* no, complain */
            }
        uptr->hwmark = objc + 1;                        /* save map size */
        sim_tape_tpc_map (uptr, (t_addr *) uptr->filebuf);      /* fill map */
        break;

    default:
        break;
        }

sim_tape_rewind (uptr);
return SCPE_OK;
}

/* Detach tape unit */

t_stat sim_tape_detach (UNIT *uptr)
{
uint32 f = MT_GET_FMT (uptr);
t_stat r;

r = detach_unit (uptr);                                 /* detach unit */
if (r != SCPE_OK)
    return r;
switch (f) {                                            /* case on format */

    case MTUF_F_TPC:                                    /* TPC */
        if (uptr->filebuf)                              /* free map */
            free (uptr->filebuf);
        uptr->filebuf = NULL;
        uptr->hwmark = 0;
        break;

    default:
        break;
        }

sim_tape_rewind (uptr);
return SCPE_OK;
}

/* Read record length forward (internal routine).

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to returned record length
   Outputs:
        status  =       operation status

   exit condition       tape position
   ------------------   -----------------------------------------------------
   unit unattached      unchanged
   read error           unchanged, PNU set
   end of file/medium   updated if a gap precedes, else unchanged and PNU set
   tape mark            updated
   tape runaway         updated
   data record          updated, sim_fread will read record forward

   This routine is called to set up a record read or spacing in the forward
   direction.  On return, status is MTSE_OK and the tape is positioned at the
   first data byte if a record was encountered, or status is an MTSE error code
   giving the reason that the operation did not succeed and the tape position is
   as indicated above.

   The ANSI standards for magnetic tape recording (X3.22, X3.39, and X3.54) and
   the equivalent ECMA standard (ECMA-62) specify a maximum erase gap length of
   25 feet (7.6 meters).  While gaps of any length may be written, gaps longer
   than this are non-standard and may indicate that an unrecorded or erased tape
   is being read.

   If the tape density has been set via a previous "sim_tape_set_dens" call,
   then the length is monitored when skipping over erase gaps.  If the length
   reaches 25 feet, motion is terminated, and MTSE_RUNAWAY status is returned.
   Runaway status is also returned if an end-of-medium marker or the physical
   end of file is encountered while spacing over a gap; however, MTSE_EOM is
   returned if the tape is positioned at the EOM or EOF on entry.

   If the density has not been set, then a gap of any length is skipped, and
   MTSE_RUNAWAY status is never returned.  In effect, erase gaps present in the
   tape image file will be transparent to the caller.

   Erase gaps are currently supported only in SIMH (MTUF_F_STD) tape format.
   Because gaps may be partially overwritten with data records, gap metadata
   must be examined marker-by-marker.  To reduce the number of file read calls,
   a buffer of metadata elements is used.  The buffer size is initially
   established at 256 elements but may be set to any size desired.  To avoid a
   large read for the typical case where an erase gap is not present, the first
   read is of a single metadatum marker.  If that is a gap marker, then
   additional buffered reads are performed.

   See the notes at "tape_erase_fwd" regarding the erase gap implementation.


   Implementation notes:

    1. For programming convenience, erase gap processing is performed for both
       SIMH standard and E11 tape formats, although the latter will never
       contain erase gaps, as the "tape_erase_fwd" call takes no action for the
       E11 format.

    2. The "feof" call cannot return a non-zero value on the first pass through
       the loop, because the "sim_fseek" call resets the internal end-of-file
       indicator.  Subsequent passes only occur if an erase gap is present, so
       a non-zero return indicates an EOF was seen while reading through a gap.

    3. The "runaway_counter" cannot decrement to zero (or below) in the presence
       of an error that terminates the gap-search loop.  Therefore, the test
       after the loop exit need not check for error status.

    4. The dynamic start/stop test of the HP 3000 magnetic tape diagnostic
       heavily exercises the erase gap scanning code.  Sample test execution
       times for various buffer sizes on a 2 GHz host platform are:

         buffer size    execution time
         (elements)     (CPU seconds)
         -----------    --------------
               1             7200
              32              783
             128              237
             256              203
             512              186
            1024              171

    5. Because an erase gap may precede the logical end-of-medium, represented
       either by the physical end-of-file or by an EOM marker, the "position not
       updated" flag is set only if the tape is positioned at the EOM when the
       routine is entered.  If at least one gap marker precedes the EOM, then
       the PNU flag is not set.  This ensures that a backspace-and-retry
       sequence will work correctly in both cases.
*/

static t_stat sim_tape_rdlntf (UNIT *uptr, t_mtrlnt *bc)
{
uint8    c;
t_bool   all_eof;
uint32   f = MT_GET_FMT (uptr);
t_mtrlnt sbc;
t_tpclnt tpcbc;
t_mtrlnt buffer [256];                                  /* local tape buffer */
uint32   bufcntr, bufcap;                               /* buffer counter and capacity */
int32    runaway_counter, sizeof_gap;                   /* bytes remaining before runaway and bytes per gap */
t_stat   status = MTSE_OK;

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then quit with an error */

if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) {   /* set the initial tape position; if it fails */
    MT_SET_PNU (uptr);                                  /*   then set position not updated */
    status = sim_tape_ioerr (uptr);                     /*     and quit with I/O error status */
    }

else switch (f) {                                       /* otherwise the read method depends on the tape format */

    case MTUF_F_STD:
    case MTUF_F_E11:
        runaway_counter = 25 * 12 * bpi [MT_DENS (uptr->dynflags)]; /* set the largest legal gap size in bytes */

        if (runaway_counter == 0) {                     /* if tape density has not been not set */
            sizeof_gap = 0;                             /*   then disable runaway detection */
            runaway_counter = INT_MAX;                  /*     to allow gaps of any size */
            }

        else                                            /* otherwise */
            sizeof_gap = sizeof (t_mtrlnt);             /*   set the size of the gap */

        bufcntr = 0;                                    /* force an initial read */
        bufcap = 0;                                     /*   but of just one metadata marker */

        do {                                            /* loop until a record, gap, or error is seen */
            if (bufcntr == bufcap) {                    /* if the buffer is empty then refill it */
                if (feof (uptr->fileref)) {             /* if we hit the EOF while reading a gap */
                    if (sizeof_gap > 0)                 /*   then if detection is enabled */
                        status = MTSE_RUNAWAY;          /*     then report a tape runaway */
                    else                                /*   otherwise report the physical EOF */
                        status = MTSE_EOM;              /*     as the end-of-medium */
                    break;
                    }

                else if (bufcap == 0)                   /* otherwise if this is the initial read */
                    bufcap = 1;                         /*   then start with just one marker */

                else                                    /* otherwise reset the capacity */
                    bufcap = sizeof (buffer)            /*   to the full size of the buffer */
                               / sizeof (buffer [0]);

                bufcap = sim_fread (buffer,             /* fill the buffer */
                                    sizeof (t_mtrlnt),  /*   with tape metadata */
                                    bufcap,
                                    uptr->fileref);

                if (ferror (uptr->fileref)) {           /* if a file I/O error occurred */
                    if (bufcntr == 0)                   /*   then if this is the initial read */
                        MT_SET_PNU (uptr);              /*     then set position not updated */

                    status = sim_tape_ioerr (uptr);     /* report the error and quit */
                    break;
                    }

                else if (bufcap == 0                    /* otherwise if positioned at the physical EOF */
                  || buffer [0] == MTR_EOM)             /*   or at the logical EOM */
                    if (bufcntr == 0) {                 /*     then if this is the initial read */
                        MT_SET_PNU (uptr);              /*       then set position not updated */
                        status = MTSE_EOM;              /*         and report the end-of-medium and quit */
                        break;
                        }

                    else {                              /*     otherwise some gap has already been skipped */
                        if (sizeof_gap > 0)             /*       so if detection is enabled */
                            status = MTSE_RUNAWAY;      /*         then report a tape runaway */
                        else                            /*       otherwise report the physical EOF */
                            status = MTSE_EOM;          /*         as the end-of-medium */
                        break;
                        }

                else                                    /* otherwise reset the index */
                    bufcntr = 0;                        /*   to the start of the buffer */
                }

            *bc = buffer [bufcntr++];                   /* store the metadata marker value */

            if (*bc == MTR_EOM) {                       /* if an end-of-medium marker is seen */
                if (sizeof_gap > 0)                     /*   then if detection is enabled */
                    status = MTSE_RUNAWAY;              /*     then report a tape runaway */
                else                                    /*   otherwise report the physical EOF */
                    status = MTSE_EOM;                  /*     as the end-of-medium */
                break;
                }

            uptr->pos = uptr->pos + sizeof (t_mtrlnt);  /* space over the marker */

            if (*bc == MTR_TMK) {                       /* if the value is a tape mark */
                status = MTSE_TMK;                      /*   then quit with tape mark status */
                break;
                }

            else if (*bc == MTR_GAP)                    /* otherwise if the value is a full gap */
                runaway_counter -= sizeof_gap;          /*   then decrement the gap counter */

            else if (*bc == MTR_FHGAP) {                        /* otherwise if the value if a half gap */
                uptr->pos = uptr->pos - sizeof (t_mtrlnt) / 2;  /*   then back up and resync */

                if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) {   /* set the tape position; if it fails */
                    status = sim_tape_ioerr (uptr);                     /*   then quit with I/O error status */
                    break;
                    }

                bufcntr = bufcap;                       /* mark the buffer as invalid to force a read */

                *bc = MTR_GAP;                          /* reset the marker */
                runaway_counter -= sizeof_gap / 2;      /*   and decrement the gap counter */
                }

            else {                                                      /* otherwise it's a record marker */
                if (bufcntr < bufcap                                    /* if the position is within the buffer */
                  && sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) {  /*   then seek to the data area; if it fails */
                    status = sim_tape_ioerr (uptr);                     /*     then quit with I/O error status */
                    break;
                    }

                sbc = MTR_L (*bc);                              /* extract the record length */
                uptr->pos = uptr->pos + sizeof (t_mtrlnt)       /* position to the start */
                  + (f == MTUF_F_STD ? (sbc + 1) & ~1 : sbc);   /*   of the record */
                }
            }
        while (*bc == MTR_GAP && runaway_counter > 0);  /* continue until data or runaway occurs */

        if (runaway_counter <= 0)                       /* if a tape runaway occurred */
            status = MTSE_RUNAWAY;                      /*   then report it */

        break;                                          /* otherwise the operation succeeded */

    case MTUF_F_TPC:
        sim_fread (&tpcbc, sizeof (t_tpclnt), 1, uptr->fileref);
        *bc = tpcbc;                                    /* save rec lnt */

        if (ferror (uptr->fileref)) {                   /* error? */
            MT_SET_PNU (uptr);                          /* pos not upd */
            status = sim_tape_ioerr (uptr);
            }
        else if (feof (uptr->fileref)) {                /* eof? */
            MT_SET_PNU (uptr);                          /* pos not upd */
            status = MTSE_EOM;
            }
        else {
            uptr->pos = uptr->pos + sizeof (t_tpclnt);  /* spc over reclnt */
            if (tpcbc == TPC_TMK)                       /* tape mark? */
                status = MTSE_TMK;
            else
                uptr->pos = uptr->pos + ((tpcbc + 1) & ~1); /* spc over record */
            }
        break;

    case MTUF_F_P7B:
        for (sbc = 0, all_eof = 1; ; sbc++) {           /* loop thru record */
            sim_fread (&c, sizeof (uint8), 1, uptr->fileref);

            if (ferror (uptr->fileref)) {               /* error? */
                MT_SET_PNU (uptr);                      /* pos not upd */
                status = sim_tape_ioerr (uptr);
                break;
                }
            else if (feof (uptr->fileref)) {            /* eof? */
                if (sbc == 0)                           /* no data? eom */
                    status = MTSE_EOM;
                break;                                  /* treat like eor */
                }
            else if ((sbc != 0) && (c & P7B_SOR))       /* next record? */
                break;
            else if ((c & P7B_DPAR) != P7B_EOF)
                all_eof = 0;
            }

        if (status == MTSE_OK) {
            *bc = sbc;                                      /* save rec lnt */
            sim_fseek (uptr->fileref, uptr->pos, SEEK_SET); /* for read */
            uptr->pos = uptr->pos + sbc;                    /* spc over record */
            if (all_eof)                                    /* tape mark? */
                status = MTSE_TMK;
            }
        break;

    default:
        status = MTSE_FMT;
    }

return status;
}

/* Read record length reverse (internal routine).

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to returned record length
   Outputs:
        status  =       operation status

   exit condition       tape position
   ------------------   -------------------------------------------
   unit unattached      unchanged
   beginning of tape    unchanged
   read error           unchanged
   end of file          unchanged
   end of medium        updated
   tape mark            updated
   tape runaway         updated
   data record          updated, sim_fread will read record forward

   This routine is called to set up a record read or spacing in the reverse
   direction.  On return, status is MTSE_OK and the tape is positioned at the
   first data byte if a record was encountered, or status is an MTSE error code
   giving the reason that the operation did not succeed and the tape position is
   as indicated above.


   Implementation notes:

    1. The "sim_fread" call cannot return 0 in the absence of an error
       condition.  The preceding "sim_tape_bot" test ensures that "pos" >= 4, so
       "sim_fseek" will back up at least that far, so "sim_fread" will read at
       least one element.  If the call returns zero, an error must have
       occurred, so the "ferror" call must succeed.

    2. See the notes at "sim_tape_rdlntf" and "tape_erase_fwd" regarding tape
       runaway and the erase gap implementation, respectively.
*/

static t_stat sim_tape_rdlntr (UNIT *uptr, t_mtrlnt *bc)
{
uint8    c;
t_bool   all_eof;
uint32   f = MT_GET_FMT (uptr);
t_addr   ppos;
t_mtrlnt sbc;
t_tpclnt tpcbc;
t_mtrlnt buffer [256];                                  /* local tape buffer */
uint32   bufcntr, bufcap;                               /* buffer counter and capacity */
int32    runaway_counter, sizeof_gap;                   /* bytes remaining before runaway and bytes per gap */
t_stat   status = MTSE_OK;

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then quit with an error */

if (sim_tape_bot (uptr))                                /* if the unit is positioned at the BOT */
    status = MTSE_BOT;                                  /*   then reading backward is not possible */

else switch (f) {                                       /* otherwise the read method depends on the tape format */

    case MTUF_F_STD:
    case MTUF_F_E11:
        runaway_counter = 25 * 12 * bpi [MT_DENS (uptr->dynflags)]; /* set the largest legal gap size in bytes */

        if (runaway_counter == 0) {                     /* if tape density has not been not set */
            sizeof_gap = 0;                             /*   then disable runaway detection */
            runaway_counter = INT_MAX;                  /*     to allow gaps of any size */
            }

        else                                            /* otherwise */
            sizeof_gap = sizeof (t_mtrlnt);             /*   set the size of the gap */

        bufcntr = 0;                                    /* force an initial read */
        bufcap = 0;                                     /*   but of just one metadata marker */

        do {                                            /* loop until a record, gap, or error is seen */
            if (bufcntr == 0) {                         /* if the buffer is empty then refill it */
                if (sim_tape_bot (uptr)) {              /* if the search has backed into the BOT */
                    status = MTSE_BOT;                  /*   then quit with an error */
                    break;
                    }

                else if (bufcap == 0)                   /* otherwise if this is the initial read */
                    bufcap = 1;                         /*   then start with just one marker */

                else if (uptr->pos < sizeof (buffer))   /* otherwise if less than a full buffer remains */
                    bufcap = (uint32) uptr->pos         /*   then reduce the capacity accordingly */
                               / sizeof (t_mtrlnt);

                else                                    /* otherwise reset the capacity */
                    bufcap = sizeof (buffer)            /*   to the full size of the buffer */
                               / sizeof (buffer [0]);

                if (sim_fseek (uptr->fileref,                           /* seek back to the location */
                               uptr->pos - bufcap * sizeof (t_mtrlnt),  /*   corresponding to the start */
                               SEEK_SET)) {                             /*     of the buffer; if it fails */
                    status = sim_tape_ioerr (uptr);                     /*         and fail with I/O error status */
                    break;
                    }

                bufcntr = sim_fread (buffer, sizeof (t_mtrlnt), /* fill the buffer */
                                     bufcap, uptr->fileref);    /*   with tape metadata */

                if (ferror (uptr->fileref)) {           /* if a file I/O error occurred */
                    status = sim_tape_ioerr (uptr);     /*   then report the error and quit */
                    break;
                    }
                }

            *bc = buffer [--bufcntr];                   /* store the metadata marker value */

            uptr->pos = uptr->pos - sizeof (t_mtrlnt);  /* backspace over the marker */

            if (*bc == MTR_TMK) {                       /* if the marker is a tape mark */
                status = MTSE_TMK;                      /*   then quit with tape mark status */
                break;
                }

            else if (*bc == MTR_GAP)                    /* otherwise if the marker is a full gap */
                runaway_counter -= sizeof_gap;          /*   then decrement the gap counter */

            else if ((*bc & MTR_M_RHGAP) == MTR_RHGAP           /* otherwise if the marker */
              || *bc == MTR_RRGAP) {                            /*   is a half gap */
                uptr->pos = uptr->pos + sizeof (t_mtrlnt) / 2;  /*     then position forward to resync */
                bufcntr = 0;                                    /* mark the buffer as invalid to force a read */

                *bc = MTR_GAP;                                  /* reset the marker */
                runaway_counter -= sizeof_gap / 2;              /*   and decrement the gap counter */
                }

            else {                                              /* otherwise it's a record marker */
                sbc = MTR_L (*bc);                              /* extract the record length */
                uptr->pos = uptr->pos - sizeof (t_mtrlnt)       /* position to the start */
                  - (f == MTUF_F_STD ? (sbc + 1) & ~1 : sbc);   /*   of the record */

                if (sim_fseek (uptr->fileref,                   /* seek to the start of the data area; if it fails */
                               uptr->pos + sizeof (t_mtrlnt),   /*   then return with I/O error status */
                               SEEK_SET)) {
                    status = sim_tape_ioerr (uptr);
                    break;
                    }
                }
            }
        while (*bc == MTR_GAP && runaway_counter > 0);  /* continue until data or runaway occurs */

        if (runaway_counter <= 0)                       /* if a tape runaway occurred */
            status = MTSE_RUNAWAY;                      /*   then report it */

        break;                                          /* otherwise the operation succeeded */

    case MTUF_F_TPC:
        ppos = sim_tape_tpc_fnd (uptr, (t_addr *) uptr->filebuf); /* find prev rec */
        sim_fseek (uptr->fileref, ppos, SEEK_SET);      /* position */
        sim_fread (&tpcbc, sizeof (t_tpclnt), 1, uptr->fileref);
        *bc = tpcbc;                                    /* save rec lnt */

        if (ferror (uptr->fileref))                     /* error? */
            status = sim_tape_ioerr (uptr);
        else if (feof (uptr->fileref))                  /* eof? */
            status = MTSE_EOM;
        else {
            uptr->pos = ppos;                           /* spc over record */
            if (*bc == MTR_TMK)                         /* tape mark? */
                status = MTSE_TMK;
            else
                sim_fseek (uptr->fileref, uptr->pos + sizeof (t_tpclnt), SEEK_SET);
            }
        break;

    case MTUF_F_P7B:
        for (sbc = 1, all_eof = 1; (t_addr) sbc <= uptr->pos ; sbc++) {
            sim_fseek (uptr->fileref, uptr->pos - sbc, SEEK_SET);
            sim_fread (&c, sizeof (uint8), 1, uptr->fileref);

            if (ferror (uptr->fileref)) {               /* error? */
                status = sim_tape_ioerr (uptr);
                break;
                }
            else if (feof (uptr->fileref)) {            /* eof? */
                status = MTSE_EOM;
                break;
                }
            else {
                if ((c & P7B_DPAR) != P7B_EOF)
                    all_eof = 0;
                if (c & P7B_SOR)                        /* start of record? */
                    break;
                }
            }

        if (status == MTSE_OK) {
            uptr->pos = uptr->pos - sbc;                    /* update position */
            *bc = sbc;                                      /* save rec lnt */
            sim_fseek (uptr->fileref, uptr->pos, SEEK_SET); /* for read */
            if (all_eof)                                    /* tape mark? */
                status = MTSE_TMK;
            }
        break;

    default:
        status = MTSE_FMT;
        }

return status;
}

/* Read record forward

   Inputs:
        uptr    =       pointer to tape unit
        buf     =       pointer to buffer
        bc      =       pointer to returned record length
        max     =       maximum record size
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   read error           unchanged, PNU set
   end of file/medium   unchanged, PNU set
   invalid record       unchanged, PNU set
   tape mark            updated
   data record          updated
   data record error    updated
*/

t_stat sim_tape_rdrecf (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max)
{
uint32 f = MT_GET_FMT (uptr);
t_mtrlnt i, tbc, rbc;
t_addr opos;
t_stat st;

opos = uptr->pos;                                       /* old position */
st = sim_tape_rdlntf (uptr, &tbc);                      /* read rec lnt */
if (st != MTSE_OK)
    return st;
*bc = rbc = MTR_L (tbc);                                /* strip error flag */
if (rbc > max) {                                        /* rec out of range? */
    MT_SET_PNU (uptr);
    uptr->pos = opos;
    return MTSE_INVRL;
    }
i = (t_mtrlnt) sim_fread (buf, sizeof (uint8), rbc, uptr->fileref); /* read record */
if (ferror (uptr->fileref)) {                           /* error? */
    MT_SET_PNU (uptr);
    uptr->pos = opos;
    return sim_tape_ioerr (uptr);
    }
for ( ; i < rbc; i++)                                   /* fill with 0's */
    buf[i] = 0;
if (f == MTUF_F_P7B)                                    /* p7b? strip SOR */
    buf[0] = buf[0] & P7B_DPAR;
return (MTR_F (tbc)? MTSE_RECE: MTSE_OK);
}

/* Read record reverse

   Inputs:
        uptr    =       pointer to tape unit
        buf     =       pointer to buffer
        bc      =       pointer to returned record length
        max     =       maximum record size
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   read error           unchanged
   end of file          unchanged
   end of medium        updated
   invalid record       unchanged
   tape mark            updated
   data record          updated
   data record error    updated
*/

t_stat sim_tape_rdrecr (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max)
{
uint32 f = MT_GET_FMT (uptr);
t_mtrlnt i, rbc, tbc;
t_stat st;

st = sim_tape_rdlntr (uptr, &tbc);                      /* read rec lnt */
if (st != MTSE_OK)
    return st;
*bc = rbc = MTR_L (tbc);                                /* strip error flag */
if (rbc > max)                                          /* rec out of range? */
    return MTSE_INVRL;
i = (t_mtrlnt) sim_fread (buf, sizeof (uint8), rbc, uptr->fileref); /* read record */
if (ferror (uptr->fileref))                             /* error? */
    return sim_tape_ioerr (uptr);
for ( ; i < rbc; i++)                                   /* fill with 0's */
    buf[i] = 0;
if (f == MTUF_F_P7B)                                    /* p7b? strip SOR */
    buf[0] = buf[0] & P7B_DPAR;
return (MTR_F (tbc)? MTSE_RECE: MTSE_OK);
}

/* Write record forward

   Inputs:
        uptr    =       pointer to tape unit
        buf     =       pointer to buffer
        bc      =       record length
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   write protect        unchanged
   write error          unchanged, PNU set
   data record          updated
*/

t_stat sim_tape_wrrecf (UNIT *uptr, uint8 *buf, t_mtrlnt bc)
{
uint32 f = MT_GET_FMT (uptr);
t_mtrlnt sbc;

MT_CLR_PNU (uptr);
sbc = MTR_L (bc);
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return MTSE_UNATT;
if (sim_tape_wrp (uptr))                                /* write prot? */
    return MTSE_WRP;
if (sbc == 0)                                           /* nothing to do? */
    return MTSE_OK;
sim_fseek (uptr->fileref, uptr->pos, SEEK_SET);         /* set pos */
switch (f) {                                            /* case on format */

    case MTUF_F_STD:                                    /* standard */
        sbc = MTR_L ((bc + 1) & ~1);                    /* pad odd length */
    
    /* fall through into the E11 handler */
    
    case MTUF_F_E11:                                    /* E11 */
        sim_fwrite (&bc, sizeof (t_mtrlnt), 1, uptr->fileref);
        sim_fwrite (buf, sizeof (uint8), sbc, uptr->fileref);
        sim_fwrite (&bc, sizeof (t_mtrlnt), 1, uptr->fileref);
        if (ferror (uptr->fileref)) {                   /* error? */
            MT_SET_PNU (uptr);
            return sim_tape_ioerr (uptr);
            }
        uptr->pos = uptr->pos + sbc + (2 * sizeof (t_mtrlnt));  /* move tape */
        break;

    case MTUF_F_P7B:                                    /* Pierce 7B */
        buf[0] = buf[0] | P7B_SOR;                      /* mark start of rec */
        sim_fwrite (buf, sizeof (uint8), sbc, uptr->fileref);
        sim_fwrite (buf, sizeof (uint8), 1, uptr->fileref); /* delimit rec */
        if (ferror (uptr->fileref)) {                   /* error? */
            MT_SET_PNU (uptr);
            return sim_tape_ioerr (uptr);
            }
        uptr->pos = uptr->pos + sbc;                    /* move tape */
        break;
        }

return MTSE_OK;
}

/* Write metadata forward (internal routine) */

static t_stat sim_tape_wrdata (UNIT *uptr, uint32 dat)
{
MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return MTSE_UNATT;
if (sim_tape_wrp (uptr))                                /* write prot? */
    return MTSE_WRP;
sim_fseek (uptr->fileref, uptr->pos, SEEK_SET);         /* set pos */
sim_fwrite (&dat, sizeof (t_mtrlnt), 1, uptr->fileref);
if (ferror (uptr->fileref)) {                           /* error? */
    MT_SET_PNU (uptr);
    return sim_tape_ioerr (uptr);
    }
uptr->pos = uptr->pos + sizeof (t_mtrlnt);              /* move tape */
return MTSE_OK;
}

/* Write tape mark */

t_stat sim_tape_wrtmk (UNIT *uptr)
{
if (MT_GET_FMT (uptr) == MTUF_F_P7B) {                  /* P7B? */
    uint8 buf = P7B_EOF;                                /* eof mark */
    return sim_tape_wrrecf (uptr, &buf, 1);             /* write char */
    }
return sim_tape_wrdata (uptr, MTR_TMK);
}

/* Write end of medium */

t_stat sim_tape_wreom (UNIT *uptr)
{
t_stat result;

if (MT_GET_FMT (uptr) == MTUF_F_P7B)                    /* cant do P7B */
    return MTSE_FMT;

result = sim_tape_wrdata (uptr, MTR_EOM);               /* write the EOM marker */

uptr->pos = uptr->pos - sizeof (t_mtrlnt);              /* restore original tape position */
MT_SET_PNU (uptr);                                      /* indicate that position was not updated */

return result;
}

/* Erase a gap in the forward direction (internal routine).

   An erase gap is written in the forward direction on the tape unit specified
   by "uptr" for the number of bytes specified by "bc".  The status of the
   operation is returned, and the file position is altered as follows:

     Exit Condition       File Position
     ------------------   ------------------
     unit unattached      unchanged
     unsupported format   unchanged
     write protected      unchanged
     read error           unchanged, PNU set
     write error          unchanged, PNU set
     gap written          updated

   If the requested byte count equals the metadatum size, then the routine
   succeeds only if it can overlay a single metadatum (i.e., a tape mark, an
   end-of-medium marker, or an existing erase gap marker); otherwise, the file
   position is not altered, PNU is set, and MTSE_INVRL (invalid record length)
   status is returned.

   An erase gap is represented in the tape image file by a special metadata
   value repeated throughout the gap.  The value is chosen so that it is still
   recognizable even if it has been "cut in half" by a subsequent data overwrite
   that does not end on a metadatum-sized boundary.  In addition, a range of
   metadata values are reserved for detection in the reverse direction.

   This implementation supports erasing gaps in the middle of a populated tape
   image and will always produce a valid image.  It also produces valid images
   when overwriting gaps with data records, with one exception: a data write
   that leaves only two bytes of gap remaining will produce an invalid tape.
   This limitation is deemed acceptable, as it is analogous to the existing
   limitation that data records cannot overwrite other data records without
   producing an invalid tape.

   To write an erase gap, the implementation uses one of two approaches,
   depending on whether or not the current tape position is at EOM.  Erasing at
   EOM presents no special difficulties; gap metadata markers are written for
   the prescribed number of bytes.  If the tape is not at EOM, then erasing must
   take into account the existing record structure to ensure that a valid tape
   image is maintained.

   The general approach is to erase for the nominal number of bytes but to
   increase that length, if necessary, to ensure that a partially overwritten
   data record at the end of the gap can be altered to maintain validity.
   Because the smallest legal tape record requires space for two metadata
   markers plus two data bytes, an erasure that would leave less than that
   is increased to consume the entire record.  Otherwise, the final record is
   truncated by rewriting the leading and trailing length words appropriately.

   When reading in either direction, gap metadata markers are ignored (skipped)
   until a record length header, EOF marker, EOM marker, or physical EOF is
   encountered.  Thus, tape images containing gap metadata are transparent to
   the calling simulator (unless tape runaway support is enabled -- see the
   notes at "sim_tape_rdlntf" for details).

   The permissibility of data record lengths that are not multiples of the
   metadatum size presents a difficulty when reading.  If such an "odd length"
   record is written over a gap, half of a metadata marker will exist
   immediately after the trailing record length.

   This condition is detected when reading forward by the appearance of a
   "reversed" marker.  The value appears reversed because the value is made up
   of half of one marker and half of the next.  This is handled by seeking
   forward two bytes to resync (the stipulation above that the overwrite cannot
   leave only two bytes of gap means that at least one "whole" metadata marker
   will follow).  Reading in reverse presents a more complex problem, because
   half of the marker is from the preceding trailing record length marker and
   therefore could be any of a range of values.  However, that range is
   restricted by the SIMH tape specification requirement that record length
   metadata values must have bits 30:24 set to zero.  This allows unambiguous
   detection of the condition.

   The value chosen for gap metadata and the values reserved for "half-gap"
   detection are:

     0xFFFFFFFE            - primary gap value
     0xFFFEFFFF            - reserved (indicates half-gap in forward reads)
     0xFFFF0000:0xFFFF00FF - reserved (indicates half-gap in reverse reads)
     0xFFFF8000:0xFFFF80FF - reserved (indicates half-gap in reverse reads)

   If the current tape format supports erase gaps, then this routine will write
   a gap of the requested size.  If the format does not, then no action will be
   taken, and MTSE_OK status will be returned.  This allows a device simulator
   that supports writing erase gaps to use the same code without worrying about
   the tape format currently selected by the user.  A request for an erase gap
   of zero length also succeeds with no action taken.


   Implementation notes:

    1. Erase gaps are currently supported only in SIMH (MTUF_F_STD) tape format.
*/

static t_stat tape_erase_fwd (UNIT *uptr, t_mtrlnt gap_size)
{
size_t   xfer;
t_stat   st;
t_mtrlnt meta, sbc, new_len, rec_size;
uint32   file_size, marker_count;
int32    gap_needed = (int32) gap_size;                 /* the gap remaining to be allocated from the tape */
uint32   gap_alloc = 0;                                 /* the gap currently allocated from the tape */
const t_addr gap_pos = uptr->pos;                       /* the file position where the gap will start */
const uint32 format = MT_GET_FMT (uptr);                /* the tape format */
const uint32 meta_size = sizeof (t_mtrlnt);             /* the number of bytes per metadatum */
const uint32 min_rec_size = 2 + sizeof (t_mtrlnt) * 2;  /* the smallest data record size */

MT_CLR_PNU (uptr);

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then we cannot proceed */

else if (sim_tape_wrp (uptr))                           /* otherwise if the unit is write protected */
    return MTSE_WRP;                                    /*   then we cannot write */

else if (gap_size == 0 || format != MTUF_F_STD)         /* otherwise if zero length or gaps aren't supported */
    return MTSE_OK;                                     /*   then take no action */

file_size = sim_fsize (uptr->fileref);                  /* get the file size */

if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) {   /* position the tape; if it fails */
    MT_SET_PNU (uptr);                                  /*   then set position not updated */
    return sim_tape_ioerr (uptr);                       /*     and quit with I/O error status */
    }

/* Read tape records and allocate them to the gap until the amount required is
   consumed.

   Read the next metadatum from tape:
    - EOF or EOM: allocate remainder of bytes needed.
    - TMK or GAP: allocate sizeof(metadatum) bytes.
    - Reverse GAP: allocate sizeof(metadatum) / 2 bytes.
    - Data record: see below.

   Loop until the bytes needed = 0.
*/

do {
    xfer = sim_fread (&meta, meta_size, 1, uptr->fileref);  /* read a metadatum */

    if (ferror (uptr->fileref)) {                       /* read error? */
        uptr->pos = gap_pos;                            /* restore original position */
        MT_SET_PNU (uptr);                              /* position not updated */
        return sim_tape_ioerr (uptr);                   /* translate error */
        }

    else if (xfer != 1 && feof (uptr->fileref) == 0) {  /* otherwise if a partial metadatum was read */
        uptr->pos = gap_pos;                            /*   then restore the original position */
        MT_SET_PNU (uptr);                              /* set the position-not-updated flag */
        return MTSE_INVRL;                              /*   and return an invalid record length error */
        }

    else                                                /* otherwise we had a good read */
        uptr->pos = uptr->pos + meta_size;              /*   so move the tape over the datum */

    if (feof (uptr->fileref) || (meta == MTR_EOM)) {    /* at eof or eom? */
        gap_alloc = gap_alloc + gap_needed;             /* allocate remainder */
        gap_needed = 0;
        }

    else if ((meta == MTR_GAP) || (meta == MTR_TMK)) {  /* gap or tape mark? */
        gap_alloc = gap_alloc + meta_size;              /* allocate marker space */
        gap_needed = gap_needed - meta_size;            /* reduce requirement */
        }

    else if (gap_size == meta_size) {                   /* otherwise if the request is for a single metadatum */
        uptr->pos = gap_pos;                            /*   then restore the original position */
        MT_SET_PNU (uptr);                              /* set the position-not-updated flag */
        return MTSE_INVRL;                              /*   and return an invalid record length error */
        }

    else if (meta == MTR_FHGAP) {                       /* half gap? */
        uptr->pos = uptr->pos - meta_size / 2;          /* backup to resync */

        if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) /* position the tape; if it fails */
            return sim_tape_ioerr (uptr);                   /*   then quit with I/O error status */

        gap_alloc = gap_alloc + meta_size / 2;          /* allocate marker space */
        gap_needed = gap_needed - meta_size / 2;        /* reduce requirement */
        }

    else if (uptr->pos + MTR_L (meta) + meta_size > file_size) {    /* rec len out of range? */
        gap_alloc = gap_alloc + gap_needed;                         /* presume overwritten tape */
        gap_needed = 0;                                             /* allocate remainder */
        }

/* Allocate a data record:
    - Determine record size in bytes (including metadata)
    - If record size - bytes needed < smallest allowed record size,
      allocate entire record to gap, else allocate needed amount and
      truncate data record to reflect remainder.
*/

    else {                                              /* data record */
        sbc = MTR_L (meta);                             /* get record data length */
        rec_size = ((sbc + 1) & ~1) + meta_size * 2;    /* overall size in bytes */

        if (rec_size < gap_needed + min_rec_size) {         /* rec too small? */
            uptr->pos = uptr->pos - meta_size + rec_size;   /* position past record */

            if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) /* position the tape; if it fails */
                return sim_tape_ioerr (uptr);                   /*   then quit with I/O error status */

            gap_alloc = gap_alloc + rec_size;               /* allocate record */
            gap_needed = gap_needed - rec_size;             /* reduce requirement */
            }

        else {                                              /* record size OK */
            uptr->pos = uptr->pos - meta_size + gap_needed; /* position to end of gap */
            new_len = MTR_F (meta) | (sbc - gap_needed);    /* truncate to new len */
            st = sim_tape_wrdata (uptr, new_len);           /* write new rec len */

            if (st != MTSE_OK) {                            /* write OK? */
                uptr->pos = gap_pos;                        /* restore orig pos */
                return st;                                  /* PNU was set by wrdata */
                }

            uptr->pos = uptr->pos + sbc - gap_needed;       /* position to end of data */
            st = sim_tape_wrdata (uptr, new_len);           /* write new rec len */

            if (st != MTSE_OK) {                            /* write OK? */
                uptr->pos = gap_pos;                        /* restore orig pos */
                return st;                                  /* PNU was set by wrdata */
                }

            gap_alloc = gap_alloc + gap_needed;             /* allocate remainder */
            gap_needed = 0;
            }
        }
    }
while (gap_needed > 0);                                 /* loop until all of the gap has been allocated */

uptr->pos = gap_pos;                                    /* reposition to gap start */

if (gap_alloc & (meta_size - 1)) {                      /* gap size "odd?" */
    st = sim_tape_wrdata (uptr, MTR_FHGAP);             /* write half gap marker */

    if (st != MTSE_OK) {                                /* write OK? */
        uptr->pos = gap_pos;                            /* restore orig pos */
        return st;                                      /* PNU was set by wrdata */
        }

    uptr->pos = uptr->pos - meta_size / 2;              /* realign position */
    gap_alloc = gap_alloc - 2;                          /* decrease gap to write */
    }

marker_count = gap_alloc / meta_size;                   /* count of gap markers */

do {
    st = sim_tape_wrdata (uptr, MTR_GAP);               /* write gap markers */

    if (st != MTSE_OK) {                                /* write OK? */
        uptr->pos = gap_pos;                            /* restore orig pos */
        return st;                                      /* PNU was set by wrdata */
        }
    }
while (--marker_count > 0);

return MTSE_OK;
}

/* Erase a gap in the reverse direction (internal routine).

   An erase gap is written in the reverse direction on the tape unit specified
   by "uptr" for the number of bytes specified by "bc".  The status of the
   operation is returned, and the file position is altered as follows:

     Exit Condition       File Position
     ------------------   ------------------
     unit unattached      unchanged
     unsupported format   unchanged
     write protected      unchanged
     read error           unchanged, PNU set
     write error          unchanged, PNU set
     gap written          updated

   If the requested byte count equals the metadatum size, then the routine
   succeeds only if it can overlay a single metadatum (i.e., a tape mark or an
   existing erase gap marker); otherwise, the file position is not altered, and
   MTSE_INVRL (invalid record length) status is returned.


   Implementation notes:

    1. Erase gaps are currently supported only in SIMH (MTUF_F_STD) tape format.

    2. Erasing a record in the reverse direction currently succeeds only if the
       gap requested occupies the same space as the record located immediately
       before the current file position.  This limitation may be lifted in a
       future update.

    3. The "sim_fread" call cannot return 0 in the absence of an error
       condition.  The preceding "sim_tape_bot" test ensures that "pos" >= 4, so
       "sim_fseek" will back up at least that far, so "sim_fread" will read at
       least one element.  If the call returns zero, an error must have
       occurred, so the "ferror" call must succeed.
*/

static t_stat tape_erase_rev (UNIT *uptr, t_mtrlnt gap_size)
{
const uint32 format = MT_GET_FMT (uptr);                /* the tape format */
const uint32 meta_size = sizeof (t_mtrlnt);             /* the number of bytes per metadatum */
t_stat   status;
t_mtrlnt rec_size, metadatum;
t_addr   gap_pos;
size_t   xfer;

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then we cannot proceed */

else if (sim_tape_wrp (uptr))                           /* otherwise if the unit is write protected */
    return MTSE_WRP;                                    /*   then we cannot write */

else if (gap_size == 0 || format != MTUF_F_STD)         /* otherwise if the gap length is zero or unsupported */
    return MTSE_OK;                                     /*   then take no action */

gap_pos = uptr->pos;                                    /* save the starting position */

if (gap_size == meta_size) {                            /* if the request is for a single metadatum */
    if (sim_tape_bot (uptr))                            /*   then if the unit is positioned at the BOT */
        return MTSE_BOT;                                /*     then erasing backward is not possible */
    else                                                /*   otherwise */
        uptr->pos -= meta_size;                         /*     back up the file pointer */

    if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) /* position the tape; if it fails */
        return sim_tape_ioerr (uptr);                   /*   then quit with I/O error status */

    sim_fread (&metadatum, meta_size, 1, uptr->fileref);    /* read a metadatum */

    if (ferror (uptr->fileref))                             /* if a file I/O error occurred */
        return sim_tape_ioerr (uptr);                       /*   then report the error and quit */

    else if (metadatum == MTR_TMK)                          /* otherwise if a tape mark is present */
        if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) /*   then reposition the tape; if it fails */
            return sim_tape_ioerr (uptr);                   /*     then quit with I/O error status */

        else {                                              /*   otherwise */
            metadatum = MTR_GAP;                            /*     replace it with an erase gap marker */

            xfer = sim_fwrite (&metadatum, meta_size,   /* write the gap marker */
                               1, uptr->fileref);

            if (ferror (uptr->fileref) || xfer == 0)    /* if a file I/O error occurred */
                return sim_tape_ioerr (uptr);           /* report the error and quit */
            else                                        /* otherwise the write succeeded */
                status = MTSE_OK;                       /*   so return success */
            }

    else if (metadatum == MTR_GAP)                      /* otherwise if a gap already exists */
        status = MTSE_OK;                               /*   then take no additional action */

    else {                                              /* otherwise a data record is present */
        uptr->pos = gap_pos;                            /*   so restore the starting position */
        return MTSE_INVRL;                              /*     and fail with invalid record length status */
        }
    }

else {                                                  /* otherwise it's an erase record request */
    status = sim_tape_rdlntr (uptr, &rec_size);         /*   so get the length of the preceding record */

    if (status == MTSE_OK                               /* if the read succeeded */
      && gap_size == rec_size + 2 * meta_size) {        /*   and the gap will exactly overlay the record */
        gap_pos = uptr->pos;                            /*     then save the gap start position */

        status = tape_erase_fwd (uptr, gap_size);       /* erase the record */

        if (status == MTSE_OK)                          /* if the gap write succeeded */
            uptr->pos = gap_pos;                        /*   the reposition back to the start of the gap */
        }

    else {                                              /* otherwise the read failed or is the wrong size */
        uptr->pos = gap_pos;                            /*   so restore the starting position */

        if (status != MTSE_OK)                          /* if the record was not found */
            return status;                              /*   then return the failure reason */
        else                                            /* otherwise the record is the wrong size */
            return MTSE_INVRL;                          /*   so report an invalid record length */
        }
    }

return status;                                          /* return the status of the erase operation */
}

/* Write an erase gap.

   An erase gap is written in on the tape unit specified by "uptr" for the
   length specified by "gap_size" in tenths of an inch, and the status of the
   operation is returned.  The tape density must have been set via a previous
   sim_tape_set_dens call; if it has not, then no action is taken, and
   MTSE_IOERR is returned.

   If the requested gap length is zero, or the tape format currently selected
   does not support erase gaps, the call succeeds with no action taken.  This
   allows a device simulator that supports writing erase gaps to use the same
   code without worrying about the tape format currently selected by the user.

   Because SIMH tape images do not carry physical parameters (e.g., recording
   density), overwriting a tape image file containing a gap is problematic if
   the density setting is not the same as that used during recording.  There is
   no way to establish a gap of a certain length unequivocally in an image file,
   so this implementation establishes a gap of a certain number of bytes that
   reflect the desired gap length at the tape density in bits per inch used
   during writing.
*/

t_stat sim_tape_wrgap (UNIT *uptr, uint32 gaplen)
{
const uint32 density = bpi [MT_DENS (uptr->dynflags)];  /* the tape density in bits per inch */
const uint32 byte_length = (gaplen * density) / 10;     /* the size of the requested gap in bytes */

if (density == 0)                                       /* if the density has not been set */
    return MTSE_IOERR;                                  /*   then report an I/O error */
else                                                    /* otherwise */
    return tape_erase_fwd (uptr, byte_length);          /*   erase the requested gap size in bytes */
}

/* Erase a record forward.

   An erase gap is written in the forward direction on the tape unit specified
   by "uptr" for a length corresponding to a record containing the number of
   bytes specified by "bc", and the status of the operation is returned.  The
   resulting gap will occupy "bc" bytes plus the size of the record length
   metadata.  This function may be used to erase a record of length "n" in place
   by requesting a gap of length "n".  After erasure, the tape will be
   positioned at the end of the gap.

   If a length of 0 is specified, then the metadatum marker at the current tape
   position will be erased.  If the tape is not positioned at a metadatum
   marker, the routine fails with MTSE_INVRL, and the tape position is
   unchanged.
*/

t_stat sim_tape_errecf (UNIT *uptr, t_mtrlnt bc)
{
const t_mtrlnt meta_size = sizeof (t_mtrlnt);           /* the number of bytes per metadatum */
const t_mtrlnt gap_size = bc + 2 * meta_size;           /* the requested gap size in bytes */

if (bc == 0)                                            /* if a zero-length erase is requested */
    return tape_erase_fwd (uptr, meta_size);            /*   then erase a metadatum marker */
else                                                    /* otherwise */
    return tape_erase_fwd (uptr, gap_size);             /*   erase the requested gap */
}

/* Erase a record reverse.

   An erase gap is written in the reverse direction on the tape unit specified
   by "uptr" for a length corresponding to a record containing the number of
   bytes specified by "bc", and the status of the operation is returned.  The
   resulting gap will occupy "bc" bytes plus the size of the record length
   metadata.  This function may be used to erase a record of length "n" in place
   by requesting a gap of length "n".  After erasure, the tape will be
   positioned at the start of the gap.

   If a length of 0 is specified, then the metadatum marker preceding the
   current tape position will be erased.  If the tape is not positioned after a
   metadatum marker, the routine fails with MTSE_INVRL, and the tape position is
   unchanged.
*/

t_stat sim_tape_errecr (UNIT *uptr, t_mtrlnt bc)
{
const t_mtrlnt meta_size = sizeof (t_mtrlnt);           /* the number of bytes per metadatum */
const t_mtrlnt gap_size = bc + 2 * meta_size;           /* the requested gap size in bytes */

if (bc == 0)                                            /* if a zero-length erase is requested */
    return tape_erase_rev (uptr, meta_size);            /*   then erase a metadatum marker */
else                                                    /* otherwise */
    return tape_erase_rev (uptr, gap_size);             /*   erase the requested gap */
}

/* Space record forward

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to size of record skipped
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   read error           unchanged, PNU set
   end of file/medium   unchanged, PNU set
   tape mark            updated
   data record          updated
   data record error    updated
*/

t_stat sim_tape_sprecf (UNIT *uptr, t_mtrlnt *bc)
{
t_stat st;

st = sim_tape_rdlntf (uptr, bc);                        /* get record length */
*bc = MTR_L (*bc);
return st;
}

/* Space record reverse

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to size of records skipped
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   beginning of tape    unchanged
   read error           unchanged
   end of file          unchanged
   end of medium        updated
   tape mark            updated
   data record          updated
*/

t_stat sim_tape_sprecr (UNIT *uptr, t_mtrlnt *bc)
{
t_stat st;

if (MT_TST_PNU (uptr)) {
    MT_CLR_PNU (uptr);
    *bc = 0;
    return MTSE_OK;
    }
st = sim_tape_rdlntr (uptr, bc);                        /* get record length */
*bc = MTR_L (*bc);
return st;
}

/* Rewind tape */

t_stat sim_tape_rewind (UNIT *uptr)
{
uptr->pos = 0;
MT_CLR_PNU (uptr);
return MTSE_OK;
}

/* Reset tape */

t_stat sim_tape_reset (UNIT *uptr)
{
MT_CLR_PNU (uptr);
return SCPE_OK;
}

/* Test for BOT */

t_bool sim_tape_bot (UNIT *uptr)
{
uint32 f = MT_GET_FMT (uptr);

return (uptr->pos <= fmts[f].bot)? TRUE: FALSE;
}

/* Test for end of tape */

t_bool sim_tape_eot (UNIT *uptr)
{
return (uptr->capac && (uptr->pos >= uptr->capac))? TRUE: FALSE;
}

/* Test for write protect */

t_bool sim_tape_wrp (UNIT *uptr)
{
return ((uptr->flags & MTUF_WRP) || (MT_GET_FMT (uptr) == MTUF_F_TPC))? TRUE: FALSE;
}

/* Process I/O error */

static t_stat sim_tape_ioerr (UNIT *uptr)
{
perror ("Magtape library I/O error");
clearerr (uptr->fileref);
return MTSE_IOERR;
}

/* Set tape format */

t_stat sim_tape_set_fmt (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 f;

if (uptr == NULL)
    return SCPE_IERR;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
if (cptr == NULL)
    return SCPE_ARG;
for (f = 0; f < MTUF_N_FMT; f++) {
    if (fmts[f].name && (strcmp (cptr, fmts[f].name) == 0)) {
        uptr->flags = (uptr->flags & ~MTUF_FMT) |
            (f << MTUF_V_FMT) | fmts[f].uflags;
        return SCPE_OK;
        }
    }
return SCPE_ARG;
}

/* Show tape format */

t_stat sim_tape_show_fmt (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 f = MT_GET_FMT (uptr);

if (fmts[f].name)
    fprintf (st, "%s format", fmts[f].name);
else fprintf (st, "invalid format");
return SCPE_OK;
}

/* Map a TPC format tape image */

static uint32 sim_tape_tpc_map (UNIT *uptr, t_addr *map)
{
t_addr tpos;
t_tpclnt bc;
uint32 i, objc;

if ((uptr == NULL) || (uptr->fileref == NULL))
    return 0;
for (objc = 0, tpos = 0;; ) {
    sim_fseek (uptr->fileref, tpos, SEEK_SET);
    i = sim_fread (&bc, sizeof (t_tpclnt), 1, uptr->fileref);
    if (i == 0)
        break;
    if (map)
        map[objc] = tpos;
    objc++;
    tpos = tpos + ((bc + 1) & ~1) + sizeof (t_tpclnt);
    }
if (map) map[objc] = tpos;
return objc;
}

/* Find the preceding record in a TPC file */

static t_addr sim_tape_tpc_fnd (UNIT *uptr, t_addr *map)
{
uint32 lo, hi, p;


if (map == NULL)
    return 0;
lo = 0;
hi = uptr->hwmark - 1;
do {
    p = (lo + hi) >> 1;
    if (uptr->pos == map[p])
        return ((p == 0)? map[p]: map[p - 1]);
    else if (uptr->pos < map[p])
        hi = p - 1;
    else lo = p + 1;
    }
while (lo <= hi);
return ((p == 0)? map[p]: map[p - 1]);
}

/* Set tape capacity */

t_stat sim_tape_set_capac (UNIT *uptr, int32 val, char *cptr, void *desc)
{
t_addr cap;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_ARG;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
cap = (t_addr) get_uint (cptr, 10, sim_taddr_64? 2000000: 2000, &r);
if (r != SCPE_OK)
    return SCPE_ARG;
uptr->capac = cap * ((t_addr) 1000000);
return SCPE_OK;
}

/* Show tape capacity */

t_stat sim_tape_show_capac (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (uptr->capac) {
    if (uptr->capac >= (t_addr) 1000000)
        fprintf (st, "capacity=%dMB", (uint32) (uptr->capac / ((t_addr) 1000000)));
    else if (uptr->capac >= (t_addr) 1000)
        fprintf (st, "capacity=%dKB", (uint32) (uptr->capac / ((t_addr) 1000)));
    else fprintf (st, "capacity=%dB", (uint32) uptr->capac);
    }
else fprintf (st, "unlimited capacity");
return SCPE_OK;
}

/* Set the tape density.

   Set the density of the specified tape unit either to the value supplied or to
   the value represented by the supplied character string.

   If "desc" is NULL, then "val" must be set to one of the MT_DENS_* constants
   in sim_tape.h other than MT_DENS_NONE; the supplied value is used as the tape
   density, and the character string is ignored.  Otherwise, "desc" must point
   at an int32 value containing a set of allowed densities constructed as a
   bitwise OR of the appropriate MT_*_VALID values.  In this case, the string
   pointed to by "cptr" will be parsed for a decimal value corresponding to the
   desired density in bits per inch and validated against the set of allowed
   values.

   In either case, SCPE_ARG is returned if the density setting is not valid or
   allowed.  If the setting is OK, the new density is set into the unit
   structure, and SCPE_OK is returned.
*/

t_stat sim_tape_set_dens (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 density, new_bpi;
t_stat result = SCPE_OK;

if (uptr == NULL)                                               /* if the unit pointer is null */
    return SCPE_IERR;                                           /*   then the caller has screwed up */

else if (desc == NULL)                                          /* otherwise if a validation set was not supplied */
    if (val > 0 && val < (int32) BPI_COUNT)                     /*   then if a valid density code was supplied */
        uptr->dynflags = uptr->dynflags & ~MTVF_DENS_MASK       /*     then insert the code */
                           | val << UNIT_V_DF_TAPE;             /*       in the unit flags */
    else                                                        /*   otherwise the code is invalid */
        return SCPE_ARG;                                        /*     so report a bad argument */

else {                                                          /* otherwise a validation set was supplied */
    if (cptr == NULL || *cptr == 0)                             /*   but if no value is present */
        return SCPE_MISVAL;                                     /*     then report a missing value */

    new_bpi = (uint32) get_uint (cptr, 10, UINT_MAX, &result);  /* convert the string value */

    if (result != SCPE_OK)                                      /* if the conversion failed */
        result = SCPE_ARG;                                      /*   then report a bad argument */

    else for (density = 0; density < BPI_COUNT; density++)      /* otherwise validate the density */
        if (new_bpi == bpi [density]                            /* if it matches a value in the list */
          && ((1 << density) & *(int32 *) desc)) {              /*   and it's an allowed value */
            uptr->dynflags = uptr->dynflags & ~MTVF_DENS_MASK   /*     then store the index of the value */
                               | density << UNIT_V_DF_TAPE;     /*       in the unit flags */
            return SCPE_OK;                                     /*         and return success */
            }

    result = SCPE_ARG;                                          /* if no match, then report a bad argument */
    }

return result;                                                  /* return the result of the operation */
}

/* Show the tape density */

t_stat sim_tape_show_dens (FILE *st, UNIT *uptr, int32 val, void *desc)
{
uint32 tape_density;

if (uptr == NULL)                                       /* if the unit pointer is null */
    return SCPE_IERR;                                   /*   then the caller has screwed up */

else {                                                  /* otherwise get the density */
    tape_density = bpi [MT_DENS (uptr->dynflags)];      /*   of the tape from the unit flags */

    if (tape_density)                                   /* if it's set */
        fprintf (st, "density=%d bpi", tape_density);   /*   then report it */
    else                                                /* otherwise */
        fprintf (st, "density not set");                /*   it was never set by the caller */
    }

return SCPE_OK;
}
