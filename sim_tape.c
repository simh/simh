/* sim_tape.c: simulator tape support library

   Copyright (c) 1993-2021, Robert M Supnik

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

   15-Dec-21    JDB     Added extended SIMH format support
   10-Oct-21    JDB     Improved tape_erase_fwd corrupt image error checking
   06-Oct-21    JDB     Added sim_tape_erase global, tape_erase internal functions
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


   The tape formats and functions implemented herein are described in separate
   monographs titled, "SIMH Magtape Representation and Handling" and "Writing a
   Simulator for the SIMH System," respectively.


   Public routines:

   sim_tape_attach      attach tape unit
   sim_tape_detach      detach tape unit
   sim_tape_rdrecf      read tape record forward
   sim_tape_rdrecr      read tape record reverse
   sim_tape_wrrecf      write tape record forward
   sim_tape_sprecf      space tape record forward
   sim_tape_sprecr      space tape record reverse
   sim_tape_wrmrk       write private marker
   sim_tape_wrtmk       write tape mark
   sim_tape_wreom       erase remainder of tape
   sim_tape_wrgap       write erase gap
   sim_tape_errecf      erase record forward
   sim_tape_errecr      erase record reverse
   sim_tape_erase       erase a specified number of bytes
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

static const struct sim_tape_fmt fmts [] = {            /* format table, indexed by MTUF_F number */
/*     name          uflags                  bot          */
/*    -------  -------------------  --------------------- */
    { "SIMH",  MT_F_STD,            sizeof (t_mtrlnt) - 1 },    /* 0 = MTUF_F_STD */
    { "E11",   MT_F_E11,            sizeof (t_mtrlnt) - 1 },    /* 1 = MTUF_F_E11 */
    { "TPC",   MT_F_TPC | UNIT_RO,  sizeof (t_tpclnt) - 1 },    /* 2 = MTUF_F_TPC */
    { "P7B",   MT_F_P7B,            0                     },    /* 3 = MTUF_F_P7B */
    { "   ",   MT_F_TDF | UNIT_RO,  0                     },    /* 4 = MTUF_F_TDF (not implemented) */
    { "SIMH",  MT_F_EXT,            sizeof (t_mtrlnt) - 1 }     /* 5 = MTUF_F_EXT */
    };

#define FMT_COUNT       (sizeof fmts / sizeof fmts [0]) /* count of format table entries */

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
static t_stat tape_read      (UNIT *uptr, uint8 *buffer, t_mtrlnt *class_count, t_mtrlnt bufsize, t_bool reverse);
static t_stat tape_erase     (UNIT *uptr, t_mtrlnt byte_count);
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
   read error           unchanged, PNU set if initial read
   end of file/medium   updated if a gap precedes, else unchanged and PNU set
   tape mark            updated
   other marker         updated
   tape runaway         updated
   data record          updated, sim_fread will read record forward

   This routine is called to set up a record read or spacing in the forward
   direction.  On return, status is MTSE_OK if a data record, private marker, or
   reserved marker was read, or an MTSE error code if a standard marker (e.g.
   tape mark) was read, or an error occurred.  The file is positioned at the
   first byte of a data record, after a tape mark or private marker, or
   otherwise as indicated above.  In all cases, the successfully read marker or
   data record length word is returned via the "bc" pointer.

   When the extended SIMH format is enabled, then the variable addressed by the
   "bc" parameter must be set on entry to a bitmap of the object classes to
   return.  Each of the classes is represented by its corresponding bit, i.e.,
   bit 0 represents class 0, bit 1 for class 1, etc.  The routine will return
   only objects from the selected classes.  Unselected class objects will be
   ignored by skipping over them until the first selected class object is seen.
   This allows a simulator to declare those classes it understands (e.g.,
   standard classes 0 and 8, plus private classes 2 and 7) and those classes it
   wishes to ignore.  Erase gap markers are always skipped, and standard markers
   are always returned, so specifying an empty bitmap will perform the
   equivalent of a "space file forward," returning only when a tape mark or
   EOM/EOF is encountered.

   When standard SIMH format is enabled, standard classes 0 and 8 are
   automatically selected, and the entry value addressed by "bc" is ignored.

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

   Erase gaps are currently supported only in standard and extended SIMH tape
   formats.  Because gaps may be partially overwritten with data records, gap
   metadata must be examined marker-by-marker.  To reduce the number of file
   read calls, a buffer of metadata elements is used.  The buffer size is
   initially established at 256 elements but may be set to any size desired.  To
   avoid a large read for the typical case where an erase gap is not present,
   the first read is of a single metadatum marker.  If that is a gap marker,
   then additional buffered reads are performed.

   The permissibility of data record lengths that are not multiples of the
   metadatum size presents a difficulty when reading through gaps.  If such an
   "odd length" record is written over a gap, half of a gap marker will exist
   immediately after the trailing record length.

   This condition is detected when reading forward by the appearance of a
   "reversed" marker.  The value appears reversed because the value is made up
   of half of one marker and half of the next.  This is handled by seeking
   forward two bytes to resync (it is illegal to overwrite and leave only two
   bytes of gap, so at least one "whole" metadata marker will follow the
   half-gap).


   Implementation notes:

    1. For programming convenience, erase gap processing is performed for both
       SIMH and E11 tape formats, although the latter will never contain erase
       gaps, as the "tape_erase_fwd" call takes no action for the E11 format.

    2. The "runaway_counter" cannot decrement to zero (or below) in the presence
       of an error that terminates the gap-search loop.  Therefore, the test
       after the loop exit need not check for error status, except to check
       whether an EOM occurred while reading a gap.

    3. The dynamic start/stop test of the HP 3000 magnetic tape diagnostic
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

    4. Because an erase gap may precede the logical end-of-medium, represented
       either by the physical end-of-file or by an EOM marker, the "position not
       updated" flag is set only if the tape is positioned at the EOM when the
       routine is entered.  If at least one gap marker precedes the EOM, then
       the PNU flag is not set.  This ensures that a backspace-and-retry
       sequence will work correctly in both cases.

    5. When a data record length word is seen, a check is made to see if the
       word is the last word in the metadata buffer.  If it is, then the file
       stream is correctly positioned to read the data, i.e., is positioned
       immediately after the length word.  If the word is somewhere within the
       buffer, then the stream is repositioned to the location of the start of
       the data.

    6. A skipped data record may reside entirely within the metadata buffer.
       However, the buffer consists of four-byte elements, and a data record may
       not end on an element boundary.  Rather than testing for this and
       succeeding only half of the time, we unilaterally reposition the file
       stream and invalidate the buffer to force a read.

    7. A partial buffer read without a host I/O error occurs when the physical
       EOF is reached.  If the buffer contains only erase gap markers, i.e., the
       tape image ends with a gap, then the next read will return zero because
       the EOF flag is set.  In this case, we could avoid this read by calling
       the "feof" function first.  We do not, because the common case -- entry
       with the file positioned at EOF -- will not have the EOF flag set, as the
       preceding "fseek" resets it, so the read call would be made anyway.
       Thus, we would incur a small overhead on every call to save some overhead
       on a rare corner-case.
*/

static t_stat sim_tape_rdlntf (UNIT *uptr, t_mtrlnt *bc)
{
const uint32 f = MT_GET_FMT (uptr);                     /* the tape format */
uint8        c;
t_bool       all_eof;
t_mtrlnt     sbc;
t_tpclnt     tpcbc;
t_mtrlnt     buffer [256];                              /* local tape buffer */
uint32       bufcntr, bufcap;                           /* buffer counter and capacity */
uint32       classbit;                                  /* bit representing the object class */
int32        runaway_counter, max_gap, sizeof_gap;      /* bytes remaining before runaway and bytes per gap */
t_addr       next_pos;                                  /* next record position */
t_stat       status = MTSE_OK;                          /* preset status return */
uint32       accept = MTB_STANDARD;                     /* preset the standard class acceptance set */

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then quit with an error */

if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) {   /* set the initial tape position; if it fails */
    MT_SET_PNU (uptr);                                  /*   then set position not updated */
    status = sim_tape_ioerr (uptr);                     /*     and quit with I/O error status */
    }

else switch (f) {                                       /* otherwise the read method depends on the tape format */

    case MTUF_F_EXT:
        accept = (uint32) (*bc);                        /* get the set of acceptable classes */

    /* fall through into the standard SIMH and E11 handler */

    case MTUF_F_STD:
    case MTUF_F_E11:
        max_gap = 25 * 12                               /* set the largest legal gap size in bytes */
                    * bpi [MT_DENS (uptr->dynflags)];   /*   corresponding to 25 feet of tape */

        if (max_gap == 0) {                             /* if tape density has not been not set */
            sizeof_gap = 0;                             /*   then disable runaway detection */
            max_gap = INT_MAX;                          /*     and allow gaps of any size */
            }

        else                                            /* otherwise */
            sizeof_gap = sizeof (t_mtrlnt);             /*   set the size of the gap */

        runaway_counter = max_gap;                      /* initialize the runaway counter */

        bufcntr = 0;                                    /* force an initial read */
        bufcap = 0;                                     /*   but of just one metadata marker */

        do {                                            /* loop until an object is accepted or an error occurs */
            if (bufcntr == bufcap) {                    /* if the buffer is empty */
                if (bufcap == 0)                        /*   then if this is the initial read */
                    bufcap = 1;                         /*     then start with just one marker */
                else                                    /*   otherwise reset the capacity */
                    bufcap = sizeof (buffer)            /*     to the full size of the buffer */
                               / sizeof (buffer [0]);

                bufcap = sim_fread (buffer, sizeof (t_mtrlnt),  /* fill the buffer */
                                    bufcap, uptr->fileref);     /*   with tape metadata */

                if (ferror (uptr->fileref)) {           /* if a file I/O error occurred */
                    if (bufcntr == 0)                   /*   then if this is the initial read */
                        MT_SET_PNU (uptr);              /*     then set position-not-updated */

                    status = sim_tape_ioerr (uptr);     /* report the error and quit */
                    break;
                    }

                else if (bufcap == 0                    /* otherwise if positioned at the physical EOF */
                  || buffer [0] == MTR_EOM) {           /*   or at the logical EOM */
                    if (bufcntr == 0)                   /*     then if this is the initial read */
                        MT_SET_PNU (uptr);              /*       then set position not updated */

                    if (bufcap == 0)                    /* if an EOM marker was not read */
                        *bc = 0;                        /*   then zero the marker value */
                    else                                /* otherwise */
                        *bc = MTR_EOM;                  /*   store the EOM value */

                    status = MTSE_EOM;                  /* report the end-of-medium */
                    break;                              /*   and quit */
                    }

                else                                    /* otherwise reset the index */
                    bufcntr = 0;                        /*   to the start of the buffer */
                }

            *bc = buffer [bufcntr++];                   /* store the metadata marker value */

            if (*bc == MTR_EOM) {                       /* if an end-of-medium marker is seen */
                status = MTSE_EOM;                      /*   then report the end-of-medium */
                break;                                  /*     and quit */
                }

            uptr->pos = uptr->pos + sizeof (t_mtrlnt);  /* space over the marker */

            if (*bc == MTR_TMK) {                       /* if the marker is a tape mark */
                status = MTSE_TMK;                      /*   then quit with tape mark status */
                break;
                }

            else if (*bc == MTR_GAP)                    /* otherwise if the marker is a full gap */
                runaway_counter -= sizeof_gap;          /*   then decrement the gap counter */

            else if (*bc == MTR_FHGAP) {                        /* otherwise if the marker if a half gap */
                uptr->pos = uptr->pos - sizeof (t_mtrlnt) / 2;  /*   then back up to resync */

                if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) {   /* set the tape position; if it fails */
                    status = sim_tape_ioerr (uptr);                     /*   then quit with I/O error status */
                    break;
                    }

                bufcntr = bufcap;                       /* mark the buffer as invalid to force a read */
                runaway_counter -= sizeof_gap / 2;      /*   and decrement the gap counter by half */
                }

            else {                                      /* otherwise it is not a known marker */
                classbit = MTR_FB (*bc);                /*   so get the bit corresponding to the class */

                next_pos = uptr->pos + MTR_RL (*bc)     /* it it's a data record */
                             + sizeof (t_mtrlnt);       /*   then preset to the end of the record */

                if (f != MTUF_F_E11)                    /* if the format is not E11 */
                    next_pos += MTR_RL (*bc) & 1;       /*   then record sizes are an even number of bytes */

                if (classbit & accept) {                /* if the class is accepted */
                    if (classbit == MTB_SMARK)          /*   then if it's a SIMH-reserved marker */
                        status = MTSE_RESERVED;         /*     then return reserved status */

                    else if (classbit == MTB_PMARK)     /*   otherwise if it's a private marker */
                        status = MTSE_OK;               /*     then return successful status */

                    else if (bufcntr == bufcap                  /*   otherwise if the record starts after the buffer */
                      || sim_fseek (uptr->fileref,              /*     or repositioning to the start */
                                    uptr->pos, SEEK_SET) == 0)  /*       of the data area succeeds */
                        uptr->pos = next_pos;                   /*         then position past the record */

                    else                                /*   otherwise the seek failed */
                        status = sim_tape_ioerr (uptr); /*     so quit with I/O error status */

                    break;                              /* acceptance terminates the search */
                    }

                else if (classbit & MTB_RECORDSET) {    /* otherwise if ignoring a data record */
                    uptr->pos = next_pos;               /*   then position past the record */

                    if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) {   /* set the new position; if it fails */
                        status = sim_tape_ioerr (uptr);                     /*   then quit with I/O error status */
                        break;
                        }

                    bufcntr = bufcap;                   /* mark the buffer as invalid to force a read */
                    }

                runaway_counter = max_gap;              /* ignoring a marker or record resets the counter */
                }
            }
        while (runaway_counter > 0);                    /* continue searching until runaway occurs */

        if (sizeof_gap > 0                              /* if gap detection is enabled */
          && (runaway_counter <= 0                      /*   and a tape runaway occurred */
          || status == MTSE_EOM                         /*   or EOM/EOF was seen */
          && runaway_counter < max_gap))                /*     while a gap was being skipped */
            status = MTSE_RUNAWAY;                      /*       then report it */

        break;                                          /* end of case */


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
   read error           unchanged, PNU set if initial read
   beginning of tape    unchanged
   tape mark            updated
   other marker         updated
   tape runaway         updated
   data record          updated, sim_fread will read record forward

   This routine is called to set up a record read or spacing in the reverse
   direction.  On return, status is MTSE_OK if a data record, private marker, or
   reserved marker was read, or an MTSE error code if a standard marker (e.g.
   tape mark) was read, or an error occurred.  The file is positioned at the
   first byte of a data record, before a tape mark or private marker, or
   otherwise as indicated above.  In all cases, the successfully read marker or
   data record length word is returned via the "bc" pointer.

   When the extended SIMH format is enabled, then the variable addressed by the
   "bc" parameter must be set on entry to a bitmap of the object classes to
   return.  Each of the classes is represented by its corresponding bit, i.e.,
   bit 0 represents class 0, bit 1 for class 1, etc.  The routine will return
   only objects from the selected classes.  Unselected class objects will be
   ignored by skipping over them until the first selected class object is seen.
   This allows a simulator to declare those classes it understands (e.g.,
   standard classes 0 and 8, plus private classes 2 and 7) and those classes it
   wishes to ignore.  Erase gap markers are always skipped, and standard markers
   are always returned, so specifying an empty bitmap will perform the
   equivalent of a "space file forward," returning only when a tape mark or
   EOM/EOF is encountered.

   When standard SIMH format is enabled, standard classes 0 and 8 are
   automatically selected, and the entry value addressed by "bc" is ignored.

   The permissibility of data record lengths that are not multiples of the
   metadatum size presents a difficulty when reading through gaps.  If such an
   "odd length" record is written over a gap, half of a gap marker will exist
   immediately after the trailing record length.

   Reading in reverse presents a more complex problem than reading forward
   through gaps, because half of the marker is from the preceding trailing
   record length marker and therefore could be any of a range of values.
   However, that range is restricted by the SIMH tape specification to permit
   unambiguous detection of the condition.  The Class F assignments are:

     F0000000 - FFFDFFFF  Reserved for future use (available)
     FFFE0000 - FFFFFFFD  Reserved for erase gap interpretation
     FFFFFFFE             Erase gap (primary value)
     FFFFFFFF             End of medium

   Values within the reserved erase-gap interpretation subrange are as follows:

     FFFE0000 - FFFEFFFE  Illegal (would be seen as full gap in reverse reads)
     FFFEFFFF             Interpret as half-gap in forward reads
     FFFF0000 - FFFFFFFD  Interpret as half-gap in reverse reads

   A conforming writer will never write the illegal marker values, so that a
   conforming reader will be able to recognize the half-gap marker values.


   Implementation notes:

    1. The "sim_fread" call cannot return 0 in the absence of an error
       condition.  The preceding "sim_tape_bot" test ensures that "pos" >= 4, so
       "sim_fseek" will back up at least that far, so "sim_fread" will read at
       least one element.  If the call returns zero, an error must have
       occurred, so the "ferror" call must succeed.

    2. The "runaway_counter" cannot decrement to zero (or below) in the presence
       of an error that terminates the gap-search loop.  Therefore, the test
       after the loop exit need not check for error status.

    3. See the notes at "sim_tape_rdlntf" regarding the implementation of tape
       runaway detection.
*/

static t_stat sim_tape_rdlntr (UNIT *uptr, t_mtrlnt *bc)
{
const uint32 f = MT_GET_FMT (uptr);                     /* the tape format */
uint8        c;
t_bool       all_eof;
t_addr       ppos;
t_mtrlnt     sbc;
t_tpclnt     tpcbc;
t_mtrlnt     buffer [256];                              /* local tape buffer */
uint32       bufcntr, bufcap;                           /* buffer counter and capacity */
uint32       classbit;                                  /* bit representing the object class */
int32        runaway_counter, max_gap, sizeof_gap;      /* bytes remaining before runaway and bytes per gap */
t_addr       next_pos;                                  /* next record position */
t_stat       status = MTSE_OK;                          /* preset status return */
uint32       accept = MTB_STANDARD;                     /* preset the standard class acceptance set */

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then quit with an error */

else if (sim_tape_bot (uptr))                           /* otherwise if the unit is positioned at the BOT */
    status = MTSE_BOT;                                  /*   then reading backward is not possible */

else switch (f) {                                       /* otherwise the read method depends on the tape format */

    case MTUF_F_EXT:
        accept = (uint32) (*bc);                        /* get the set of acceptable classes */

    /* fall through into the standard SIMH and E11 handler */

    case MTUF_F_STD:
    case MTUF_F_E11:
        max_gap = 25 * 12                               /* set the largest legal gap size in bytes */
                    * bpi [MT_DENS (uptr->dynflags)];   /*   corresponding to 25 feet of tape */

        if (max_gap == 0) {                             /* if tape density has not been not set */
            sizeof_gap = 0;                             /*   then disable runaway detection */
            max_gap = INT_MAX;                          /*     and allow gaps of any size */
            }

        else                                            /* otherwise */
            sizeof_gap = sizeof (t_mtrlnt);             /*   set the size of the gap */

        runaway_counter = max_gap;                      /* initialize the runaway counter */

        bufcntr = 0;                                    /* force an initial read */
        bufcap = 0;                                     /*   but of just one metadata marker */

        ppos = uptr->pos;                               /* save the initial tape position */

        do {                                            /* loop until an object is accepted or an error occurs */
            if (bufcntr == 0) {                         /*   then if the buffer is empty */
                if (sim_tape_bot (uptr)) {              /*     then if the search has backed into the BOT */
                    status = MTSE_BOT;                  /*       then quit with an error */
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

                sim_fseek (uptr->fileref,                           /* seek back to the location */
                           uptr->pos - bufcap * sizeof (t_mtrlnt),  /*   corresponding to the start */
                           SEEK_SET);                               /*     of the buffer */

                bufcntr = sim_fread (buffer, sizeof (t_mtrlnt), /* fill the buffer */
                                     bufcap, uptr->fileref);    /*   with tape metadata */

                if (ferror (uptr->fileref)) {           /* if a file I/O error occurred */
                    if (uptr->pos == ppos)              /*   then if this is the initial read */
                        MT_SET_PNU (uptr);              /*     then set position not updated */

                    status = sim_tape_ioerr (uptr);     /* report the error and quit */
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

            else if ((*bc & MTR_RHGAP) == MTR_RHGAP) {          /* otherwise if the marker is a half gap */
                uptr->pos = uptr->pos + sizeof (t_mtrlnt) / 2;  /*   then position forward to resync */

                bufcntr = 0;                            /* mark the buffer as invalid to force a read */
                runaway_counter -= sizeof_gap / 2;      /*   and decrement the gap counter by half */
                }

            else {                                      /* otherwise it is not a known marker */
                classbit = MTR_FB (*bc);                /*   so get the bit corresponding to the class */

                next_pos = uptr->pos - MTR_RL (*bc)     /* it it's a data record */
                             - sizeof (t_mtrlnt);       /*   then preset to the start of the record */

                if (f != MTUF_F_E11)                    /* if the format is not E11 */
                    next_pos -= MTR_RL (*bc) & 1;       /*   then record sizes are an even number of bytes */

                if (classbit & accept) {                /* if the class is accepted */
                    if (classbit == MTB_SMARK)          /*   then if it's a SIMH-reserved marker */
                        status = MTSE_RESERVED;         /*     then return reserved status */

                    else if (classbit == MTB_PMARK)     /*   otherwise if it's a private marker */
                        status = MTSE_OK;               /*     then return successful status */

                    else if (sim_fseek (uptr->fileref,                  /*   otherwise position to the start */
                                        next_pos + sizeof (t_mtrlnt),   /*     of the data area */
                                        SEEK_SET) == 0)                 /*       and if the seek succeeds */
                        uptr->pos = next_pos;                           /*         then position past the record */

                    else                                /*   otherwise the seek failed */
                        status = sim_tape_ioerr (uptr); /*     so quit with I/O error status */

                    break;                              /* acceptance terminates the search */
                    }

                else if (classbit & MTB_RECORDSET) {    /* otherwise if ignoring a data record */
                    uptr->pos = next_pos;               /*   then position before the record */

                    if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) {   /* set the new position; if it fails */
                        status = sim_tape_ioerr (uptr);                     /*   then quit with I/O error status */
                        break;
                        }

                    bufcntr = 0;                        /* mark the buffer as invalid to force a read */
                    }

                runaway_counter = max_gap;              /* ignoring a marker or record resets the counter */
                }
            }
        while (runaway_counter > 0);                    /* continue searching until runaway occurs */

        if (runaway_counter <= 0)                       /* if a tape runaway occurred */
            status = MTSE_RUNAWAY;                      /*   then report it */

        break;                                          /* end of case */


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


/* Read a data record or tape marker.

   Read the data record or tape marker a the current tape position in the
   indicated direction and return it via the supplied buffer or marker pointers,
   respectively.

   On entry, the "uptr" parameter points at the UNIT structure describing the
   tape device, "buffer" points at a buffer large enough to receive a retrieved
   data record, "class_count" points at a variable that receives the data record
   length word containing the record class and length or the tape marker value,
   "bufsize" indicates the size of the buffer in bytes, and "reverse" is TRUE if
   the record is to be read in the reverse direction and FALSE if the record is
   to be read in the forward direction.

   If the tape format is extended SIMH, then "class_count" must point at a value
   containing a bitmap of the desired record and marker classes to read.  Each
   class is represented by its corresponding bit.  Classes present in the tape
   image but not in the bitmap are skipped until an object of the specified
   class is read.  The standard markers (tape mark, etc.) are always read and
   interpreted.

   A successful read of a data record returns the data in the buffer and the
   record class and length via the "class_count" parameter.  A successful marker
   read returns the marker value via the "class_count" parameter; the buffer is
   not used.

   For all other tape formats, the entry value indicated by "class_count" is
   ignored, and all items supported by the specified format are returned.
   Successful reads return data in the buffer and the record length via the
   "class_count" parameter.

   The result of the read is returned as the value of the function, as follows:

     Status          Condition
     -------------   ---------------------------------------------
     MTSE_OK         Successful read of a good data record
     MTSE_RECE       Successful read of a bad data record
     MTSE_TMK        Successful read of a tape mark
     MTSE_RESERVED   Successful read of a reserved marker

     MTSE_UNATT      The tape unit is not attached
     MTSE_IOERR      A host I/O error occurred
     MTSE_FMT        An invalid tape format is selected
     MTSE_BOT        Reading stopped at the beginning of the tape
     MTSE_EOM        Reading stopped at the end of the tape
     MTSE_RUNAWAY    Reading did not encounter any data
     MTSE_INVRL      The record is larger than the supplied buffer
                     or the record is incomplete
*/

static t_stat tape_read (UNIT *uptr, uint8 *buffer, t_mtrlnt *class_count, t_mtrlnt bufsize, t_bool reverse)
{
const uint32 f = MT_GET_FMT (uptr);                     /* the tape format */
t_mtrlnt     cbc, rbc;
t_addr       opos;
t_stat       st;

cbc = *class_count;                                     /* get the acceptance mask */
opos = uptr->pos;                                       /*   and save the original file position */

if (reverse)                                            /* for a reverse read */
    st = sim_tape_rdlntr (uptr, &cbc);                  /*   get the preceding record length */
else                                                    /* otherwise */
    st = sim_tape_rdlntf (uptr, &cbc);                  /*   get the following record length */

if (st != MTSE_OK                                       /* if the read failed */
  || (MTR_FB (cbc) & MTB_MARKERSET)) {                  /*   or it returned a marker */
    if (f == MTUF_F_EXT)                                /*     then if the format is extended SIMH */
        *class_count = cbc;                             /*       then return the marker value */

    return st;                                          /* return the status */
    }

rbc = MTR_RL (cbc);                                     /* get the record length */

if (f == MTUF_F_EXT)                                    /* if the format is extended SIMH */
    *class_count = cbc;                                 /*   then return the class and length */
else                                                    /* otherwise */
    *class_count = rbc;                                 /*   return just the length */

if (rbc > bufsize)                                      /* if the record won't fit in the buffer */
    st = MTSE_INVRL;                                    /*   then return invalid length status */

else {                                                      /* otherwise */
    sim_fread (buffer, sizeof (uint8), rbc, uptr->fileref); /*   read the data payload into the supplied buffer */

    if (ferror (uptr->fileref))                         /* if a host I/O error occurred */
        st = sim_tape_ioerr (uptr);                     /*    then return I/O error status */

    else if (feof (uptr->fileref))                      /* otherwise if the read was incomplete */
        st = MTSE_INVRL;                                /*   then report a record length error */

    else if (f == MTUF_F_P7B)                            /* otherwise if the format is P7B */
        buffer [0] = buffer [0] & P7B_DPAR;              /*   then strip the start-of-record flag */
    }

if (st != MTSE_OK) {                                    /* if the read failed */
    MT_SET_PNU (uptr);                                  /*   then set the position not updated flag */
    uptr->pos = opos;                                   /*     and restore the original position */
    return st;                                          /*       and return the failure status */
    }

else if (MTR_CF (cbc) == MTC_BAD)                       /* otherwise if a bad record was read */
    return MTSE_RECE;                                   /*   then report a data error */

else                                                    /* otherwise */
    return MTSE_OK;                                     /*   report a successful read */
}


/* Read record or marker forward.

   Inputs:
        uptr    =       pointer to tape unit
        buf     =       pointer to buffer
        bc      =       pointer to returned class/record length
        max     =       maximum record size

   Outputs:
        status  =       operation status
*/

t_stat sim_tape_rdrecf (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max)
{
return tape_read (uptr, buf, bc, max, FALSE);           /* read and return the next record or marker */
}


/* Read record or marker reverse.

   Inputs:
        uptr    =       pointer to tape unit
        buf     =       pointer to buffer
        bc      =       pointer to returned class/record length
        max     =       maximum record size

   Outputs:
        status  =       operation status
*/

t_stat sim_tape_rdrecr (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max)
{
return tape_read (uptr, buf, bc, max, TRUE);           /* read and return the prior record or marker */
}


/* Write a data record forward.

   Write a data record at the current tape position and return the status of the
   operation.

   On entry, the "uptr" parameter points at the UNIT structure describing the
   tape device, "buf" points at the buffer containing the data, and "clbc"
   contains the class and record length.

   If the tape format is extended SIMH, then "clbc" must contain a standard or
   private data record class and if the class is 0 (i.e., a good data record),
   then the record length must be non-zero.  For all other tape formats, the
   class must be 0 or 8 (good or bad data record); a record length of zero is
   treated as a NOP for these formats.

   The result of the write is returned as the value of the function, as follows:

     Status          Condition
     -------------   -------------------------------------------------
     MTSE_OK         Successful write of the data record

     MTSE_UNATT      The tape unit is not attached
     MTSE_WRP        The tape unit is write protected
     MTSE_IOERR      A host I/O error occurred
     MTSE_INVRL      The record length is improper or too long
     MTSE_RESERVED   The record class is reserved or is a marker class
     MTSE_FMT        The tape format does not support the record class
*/

t_stat sim_tape_wrrecf (UNIT *uptr, uint8 *buf, t_mtrlnt clbc)
{
const uint32 f = MT_GET_FMT (uptr);                     /* the tape format */
t_mtrlnt     sbc;
uint32       classbit;

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */

sbc      = MTR_RL (clbc);                               /* get the record length */
classbit = MTR_FB (clbc);                               /*   and the class field bit */

if (f == MTUF_F_EXT) {                                  /* if the format is extended SIMH */
    if (! (classbit & MTB_EXTENDED))                    /*   then if not in the extended record class */
        return MTSE_RESERVED;                           /*     then report a reserved class error */
    else if (sbc == 0 && classbit == MTB_GOOD)          /*   otherwise if the length of a good record is zero */
        return MTSE_INVRL;                              /*     then report an invalid length error */
    }

else if (! (classbit & MTB_STANDARD))                   /* otherwise if the class is not a standard record */
    return MTSE_FMT;                                    /*   then report a format error */

else if (sbc == 0 && classbit == MTB_GOOD)              /* otherwise if the length of a good record is zero */
    return MTSE_OK;                                     /*   then treat it as a NOP */

else if (sbc > MTR_MAXLEN)                              /* otherwise if the record is too long */
    return MTSE_INVRL;                                  /*   then report an invalid length error */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then report it */

else if (sim_tape_wrp (uptr))                           /* otherwise if the tape is write protected */
    return MTSE_WRP;                                    /*   then report it */

sim_fseek (uptr->fileref, uptr->pos, SEEK_SET);         /* set the tape position */

switch (f) {                                            /* dispatch on the format */

    case MTUF_F_STD:                                    /* standard */
    case MTUF_F_EXT:                                    /* extended standard */
        sbc = (sbc + 1) & ~1;                           /* pad odd length */

    /* fall through into the E11 handler */

    case MTUF_F_E11:                                    /* E11 */
        sim_fwrite (&clbc, sizeof (t_mtrlnt), 1, uptr->fileref);
        sim_fwrite (buf, sizeof (uint8), sbc, uptr->fileref);
        sim_fwrite (&clbc, sizeof (t_mtrlnt), 1, uptr->fileref);
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


/* Write a private marker.

   Write a private marker value at the current tape position and return the
   status of the operation.

   On entry, the "uptr" parameter points at the UNIT structure describing the
   tape device, and "mk" contains the marker class and value.  The tape format
   must be extended SIMH, and "mk" must be a member of the private marker class.

   The result of the write is returned as the value of the function, as follows:

     Status          Condition
     -------------   ------------------------------------------------
     MTSE_OK         Successful write of the marker

     MTSE_UNATT      The tape unit is not attached
     MTSE_WRP        The tape unit is write protected
     MTSE_IOERR      A host I/O error occurred
     MTSE_RESERVED   The class is not the private marker class
     MTSE_FMT        The tape format does not support private markers
*/

t_stat sim_tape_wrmrk (UNIT *uptr, t_mtrlnt mk)
{
if (MT_GET_FMT (uptr) != MTUF_F_EXT)                    /* if the format is not extended SIMH */
    return MTSE_FMT;                                    /*   then report a format error */

else if (MTR_CF (mk) != MTC_PMARK)                      /* otherwise if the marker is not private */
    return MTSE_RESERVED;                               /*   then report a reserved class error */

else                                                    /* otherwise */
    return sim_tape_wrdata (uptr, mk);                  /*   write the marker to the tape */
}


/* Write tape a mark */

t_stat sim_tape_wrtmk (UNIT *uptr)
{
if (MT_GET_FMT (uptr) == MTUF_F_P7B) {                  /* P7B? */
    uint8 buf = P7B_EOF;                                /* eof mark */
    return sim_tape_wrrecf (uptr, &buf, 1);             /* write char */
    }
return sim_tape_wrdata (uptr, MTR_TMK);
}


/* Write an end of medium */

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


/* Erase a gap of the specified number of bytes (internal routine).

   This routine will write a gap of the requested number of bytes at the current
   position of the file attached to the supplied unit.

   On entry, the file is positioned to the start of the gap as indicated by the
   "uptr->pos" value.  The minimum gap size allowed is four bytes (one erase gap
   marker); smaller values will be rounded up.  As the SIMH tape format allows
   erasures only in multiples of two bytes, an odd byte count is rounded up to
   the next even value.

   If the requested size will not accommodate an integral number of gap markers,
   a leading half-gap marker is written first.  Then the required number of gap
   markers are written to fill the specified gap.  To improve efficiency, each
   "sim_fwrite" call writes multiple gaps.

   If a host file I/O error occurs while writing, the file position is restored,
   the position-not-updated flag is set, the error is reported to the console,
   and the routine returns MTSE_IOERR.  Otherwise, the routine returns MTSE_OK.


   Implementation notes:

    1. Erase gaps are currently supported only in standard and extended SIMH
       tape formats.

    2. There is no easy way to initialize the "gaps" array statically, so we do
       it at run-time.  However, being a static array, the initialization is
       only performed once and the elements are guaranteed to be zero when this
       routine is called for the first time

    3. The "half_gap" array wants to be constant, but "sim_fwrite" does not
       declare a compatible buffer pointer parameter.

    4. A half-gap cannot be written by itself, as interpretation when reading
       would be indeterminate.
*/

static t_stat tape_erase (UNIT *uptr, t_mtrlnt byte_count)
{
static t_mtrlnt gaps [256];                             /* a block of erase gaps */
static uint8    half_gap [2] = { 0xFF, 0xFF };          /* upper half of an erase gap */
const  uint32   buffer_size  = sizeof gaps / sizeof gaps [0];
const  uint32   meta_size    = sizeof (t_mtrlnt);       /* the number of bytes per metadatum */
const  t_addr   gap_pos      = uptr->pos;               /* the file position where the gap will start */
uint32          count, marker_count;

if (gaps [0] == 0)                                      /* if the gap block has not been initialized */
    for (count = 0; count < buffer_size; count++)       /*   then fill the block with erase gaps */
        gaps [count] = MTR_GAP;                         /*     to improve write performance */

sim_fseek (uptr->fileref, uptr->pos, SEEK_SET);         /* seek to the start of the gap */

byte_count = (byte_count + 1) & ~1;                     /* round the count to an even number */

if (byte_count < meta_size)                             /* if the size is smaller than an erase gap marker */
    byte_count = meta_size;                             /*   then increase the size to one marker */

else if (byte_count % meta_size > 0) {                  /* otherwise if an integral number of markers won't fit */
    sim_fwrite (half_gap, sizeof (uint8), 2,            /*   then start the gap */
                uptr->fileref);                         /*     with a half-gap marker */

    uptr->pos  = uptr->pos  + sizeof half_gap;          /* advance the tape position */
    byte_count = byte_count - sizeof half_gap;          /*   and drop the byte count for the half-gap */
    }

marker_count = byte_count / meta_size;                  /* get the count of full gap markers */

while (marker_count > 0) {                              /* while full gaps are needed */
    if (marker_count > buffer_size)                     /* if more than a full block is needed */
        count = buffer_size;                            /*   then write a full block of gaps */
    else                                                /* otherwise */
        count = marker_count;                           /*   write the remaining size needed */

    sim_fwrite (gaps, meta_size, count, uptr->fileref); /* write the erase gap */

    marker_count = marker_count - count;                /* reduce the count by the amount erased */
    }

if (ferror (uptr->fileref)) {                           /* if a host I/O error occurred */
    uptr->pos = gap_pos;                                /*   then reposition back to the gap start */

    MT_SET_PNU (uptr);                                  /* report that the position was not updated */
    return sim_tape_ioerr (uptr);                       /*   and that an error occurred */
    }

else {                                                  /* otherwise the writes were successful */
    uptr->pos = uptr->pos + byte_count;                 /*   so advance the tape position past the gap */

    MT_CLR_PNU (uptr);                                  /* report that the position was updated */
    return MTSE_OK;                                     /*   and return success */
    }
}


/* Erase a gap in the forward direction (internal routine).

   An erase gap is written in the forward direction on the tape unit specified
   by "uptr" for the number of bytes specified by "gap_size".  The status of the
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
   increase that length if necessary to ensure that a partially overwritten data
   record at the end of the gap can be altered to maintain validity.  Because
   the smallest legal tape record requires space for two metadata markers plus
   two data bytes, an erasure that would leave less than that is increased to
   consume the entire record.  Otherwise, the final record is truncated by
   rewriting the leading and trailing length words appropriately.

   When reading in either direction, gap metadata markers are ignored (skipped)
   until a record length header, non-gap marker, or physical EOF is encountered.
   Thus, tape images containing gap metadata are transparent to the calling
   simulator (unless tape runaway support is enabled -- see the notes at
   "sim_tape_rdlntf" for details).

   If the current tape format supports erase gaps, then this routine will write
   a gap of the requested size.  If the format does not, then no action will be
   taken, and MTSE_OK status will be returned.  This allows a device simulator
   that supports writing erase gaps to use the same code without worrying about
   the tape format currently selected by the user.  A request for an erase gap
   of zero length also succeeds with no action taken.

   Considerations when reading erase gaps are discussed in more detail in the
   comments of the "sim_tape_rdlntf" routine.


   The scan of an existing tape image before erasing proceeds as follows.

   After preliminary access checks (i.e., image is attached and is writable),
   the file is positioned to the start of the area to be erased.  The routine
   then enters a loop that reads data items and accumulates the areas to be
   erased until the required number of bytes have been examined.  When that
   happens, the gap is written at the original position, extended as necessary
   to maintain tape integrity.  If an error occurs, the scan is aborted, and the
   appropriate error code is returned to the caller.

   Each pass of the loop begins by reading the next metadatum in the file.  If a
   host I/O error occurs, the scan is aborted with an appropriate error return.
   If an EOM metadatum was read, or the physical EOF was encountered, the scan
   is completed by allocating the remaining gap space unconditionally.

   If a gap or tape mark metadatum was read, the position is advanced over the
   marker, and the scan continues.  If a half-gap was read, the position is
   adjusted to align with the next full metadatum, and the scan continues.  If
   the metadatum is not one of these items, it must be a data record leading
   length marker.  If the caller wants only a single metadatum erased, the
   routine returns an invalid record length error.

   Before adding part or all of the data record to the accumulated erase area,
   record integrity is checked.  Verification of the trailing length marker is
   done in two steps.  If the marker would be positioned beyond the end of the
   file, then the tape image is considered to be invalid, and the scan is
   terminated.  If the location is within the file, the position is temporarily
   moved to the location of the trailing length marker, which is read and
   compared to the leading length marker.  If the read fails, or the markers do
   not compare, then the image is invalid, and the scan is completed by
   allocating the remaining gap space unconditionally.

   If the markers compare, then a check is made to see if the data record is
   contained wholly within the area to be erased or if it extends beyond the end
   of the erasure.  In the first case, the space occupied by the record is
   simply added to the accumulated area.  In the second case, however, the
   record must be truncated to maintain image validity.

   Truncation is possible only if a valid record can be written into the
   space occupied by the remaining part of the record.  The smallest legal
   record is two data bytes long, and such a record occupies ten bytes,
   including the pair of four-byte length markers.  If the remaining space is
   too small, the gap is extended to consume the full record to avoid leaving an
   invalid area.

   If the size of the remaining record after the erasure is large enough, the
   trailing length word is rewritten for the new, shorter size, and then the
   leading length word immediately following the erased area is written to
   match.

   Once gap allocation is complete, the loop terminates, and the file is
   repositioned to the start of the gap area.  If the loop terminated for an
   error, it is returned with PNU set.  Otherwise, the new gap, lengthened if
   necessary, is written, and PNU is cleared.


   Implementation notes:

    1. Erase gaps are currently supported only in standard and extended SIMH
       tape formats.

    2. Metadatum reads either succeed and returns 1 or fail and returns 0.  If a
       read fails, and "ferror" returns false, then it must have read into the
       end of the file (only these three outcomes are possible).

    3. The area scan is necessary for tape image integrity to ensure that a data
       record straddling the end of the erasure is truncated appropriately.  The
       scan is guaranteed to succeed only if it begins at a valid metadatum.  If
       it begins in the middle of a previously overwritten data record, then the
       scan will interpret old data values as tape formatting markers.  The data
       record sanity checks attempt to recover from this situation, but it is
       still possible to corrupt valid data that follows an erasure of an
       invalid area (e.g., if the leading and trailing length words happen
       to match but actually represent previously recorded data rather than
       record metadata).  If an application knows that the erased area will
       not contain valid formatting, the "sim_tape_erase" routine should be used
       instead, as it erases without first scanning the area.

    4. Truncating an existing data record corresponds to overwriting part of a
       record on a real tape.  Reading such a record on a real drive would
       produce CRC errors, due to the lost portion.  In simulation, we could
       change a good record (Class 0) to a bad record (Class 8).  However, this
       is not possible for private or reserved record classes, as that would
       change the classification (consider that a private class that had
       been ignored would not be once it had been truncated and changed to Class
       8).  Given that there is no good general solution, we do not modify
       classes for truncated records, as reading a partially erased record is an
       "all bets are off" operation.
*/

static t_stat tape_erase_fwd (UNIT *uptr, t_mtrlnt gap_size)
{
const t_addr gap_pos = uptr->pos;                       /* the file position where the gap will start */
const uint32 format = MT_GET_FMT (uptr);                /* the tape format */
const uint32 meta_size = sizeof (t_mtrlnt);             /* the number of bytes per metadatum */
const uint32 min_rec_size = 2 + sizeof (t_mtrlnt) * 2;  /* the smallest data record size */
size_t       xfer;
t_mtrlnt     meta, sbc, new_len, rec_size;
uint32       file_size;
int32        gap_needed = (int32) gap_size;             /* the gap remaining to be allocated from the tape */
uint32       gap_alloc = 0;                             /* the gap currently allocated from the tape */
t_stat       status = MTSE_OK;                          /* the status of the last operation */

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then we cannot proceed */

else if (sim_tape_wrp (uptr))                           /* otherwise if the unit is write protected */
    return MTSE_WRP;                                    /*   then we cannot write */

else if (gap_size == 0                                  /* otherwise if the gap is zero length */
 || (format != MTUF_F_STD && format != MTUF_F_EXT))     /*   or gaps are not supported */
    return MTSE_OK;                                     /*     then take no action */

MT_SET_PNU (uptr);                                      /* errors from here on do not update the position */

file_size = sim_fsize (uptr->fileref);                  /* get the file size */

if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET))     /* position the tape; if it fails */
    return sim_tape_ioerr (uptr);                       /*   then quit with I/O error status */


do {                                                        /* scan the area to be erased */
    xfer = sim_fread (&meta, meta_size, 1, uptr->fileref);  /*   starting with the next metadatum in the file */

    if (ferror (uptr->fileref)) {                       /* if a read error occurred */
        status = sim_tape_ioerr (uptr);                 /*   then report an I/O error */
        break;                                          /*     and quit the search */
        }

    else if (xfer == 1)                                 /* otherwise if we had a good read */
        uptr->pos = uptr->pos + meta_size;              /*   then move the tape over the datum */

    if ((xfer == 0) || (meta == MTR_EOM)) {             /* if the physical EOF or an EOM marker is seen */
        gap_alloc = gap_alloc + gap_needed;             /*   then allocate the remainder of the space */
        break;                                          /*     and terminate the search */
        }

    else if ((meta == MTR_GAP) || (meta == MTR_TMK)) {  /* otherwise if a gap or tape mark is seen */
        gap_alloc = gap_alloc + meta_size;              /*   then allocate the marker space */
        gap_needed = gap_needed - meta_size;            /*     and reduce the amount remaining */
        }

    else if (gap_size == meta_size) {                   /* otherwise if the request is for a single metadatum */
        status = MTSE_INVRL;                            /*   then report an invalid record length error */
        break;                                          /*     as we're not erasing a metadatum as required */
        }

    else if (meta == MTR_FHGAP) {                       /* otherwise if a half-gap is seen */
        uptr->pos = uptr->pos - meta_size / 2;          /*   then back up to resync */

        if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) {   /* position the tape; if it fails */
            status = sim_tape_ioerr (uptr);                     /*   then report an I/O error */
            break;                                              /*     and quit the search */
            }

        gap_alloc = gap_alloc + meta_size / 2;          /* allocate the marker space */
        gap_needed = gap_needed - meta_size / 2;        /*   and reduce the amount remaining */
        }

    else if (uptr->pos + MTR_RL (meta) + meta_size > file_size) {   /* otherwise if it cannot be a data record */
        gap_alloc = gap_alloc + gap_needed;                         /*   then presume an overwritten tape */
        break;                                                      /*     and allocate the remainder of the space */
        }

    else {                                              /* otherwise it may be a data record */
        sbc = MTR_RL (meta);                            /*   so get the data length */
        rec_size = ((sbc + 1) & ~1) + meta_size * 2;    /*     and the overall record size in bytes */

        uptr->pos = uptr->pos + (sbc + 1) & ~1;         /* position to the trailing length marker */

        if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET)) {   /* position the tape; if it fails */
            status = sim_tape_ioerr (uptr);                     /*   then report an I/O error */
            break;                                              /*     and quit the search */
            }

        xfer = sim_fread (&meta, meta_size, 1, uptr->fileref);  /* read the metadatum */

        if (ferror (uptr->fileref)) {                   /* if a read error occurred */
            status = sim_tape_ioerr (uptr);             /*   then report an I/O error */
            break;                                      /*     and quit the search */
            }

        else if ((xfer != 1) || (sbc != MTR_RL (meta))) {   /* otherwise if the marker is bad or does not compare */
            gap_alloc = gap_alloc + gap_needed;             /*   then presume an overwritten tape */
            break;                                          /*     and allocate the remainder of the space */
            }

        else if (rec_size < gap_needed + min_rec_size) {    /* otherwise if the record is smaller than needed */
            uptr->pos = uptr->pos + meta_size;              /*   then skip over the record */
            gap_alloc = gap_alloc + rec_size;               /*     and allocate the record space */
            gap_needed = gap_needed - rec_size;             /*       and reduce the amount remaining */
            }

        else {                                              /* otherwise the size is larger than needed */
            new_len = MTR_CF (meta) | (sbc - gap_needed);   /*   so get the shortened record length */

            status = sim_tape_wrdata (uptr, new_len);   /* rewrite the trailing length marker */

            uptr->pos = uptr->pos - 2 * meta_size       /* move the position */
                          - sbc + gap_needed;           /*   back to the leading length marker */

            if (status == MTSE_OK)                          /* if the trailing write succeeded */
                status = sim_tape_wrdata (uptr, new_len);   /*   then rewrite the leading length marker */

            gap_alloc = gap_alloc + gap_needed;         /* the record provides the rest of the gap */
            break;                                      /*   and no more space is needed */
            }
        }
    }
while (gap_needed > 0);                                 /* loop until all of the gap has been allocated */


uptr->pos = gap_pos;                                    /* reposition to the start of the gap */

if (status == MTSE_OK)                                  /* if the scan was successful */
    return tape_erase (uptr, gap_alloc);                /*   then return the status of the erasure */
else                                                    /* otherwise the search failed */
    return status;                                      /*   so return the status code with PNU */
}


/* Erase a gap in the reverse direction (internal routine).

   An erase gap is written in the reverse direction on the tape unit specified
   by "uptr" for the number of bytes specified by "gap_size".  The status of the
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


   Implementation notes:

    1. Erase gaps are currently supported only in standard and extended SIMH
       tape formats.

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
t_stat       status;
t_mtrlnt     rec_size, metadatum;
t_addr       gap_pos;
size_t       xfer;

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then we cannot proceed */

else if (sim_tape_wrp (uptr))                           /* otherwise if the unit is write protected */
    return MTSE_WRP;                                    /*   then we cannot write */

else if (gap_size == 0                                  /* otherwise if the gap is zero length */
 || (format != MTUF_F_STD && format != MTUF_F_EXT))     /*   or gaps are not supported */
    return MTSE_OK;                                     /*     then take no action */

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

        status = tape_erase (uptr, gap_size);           /* erase the record */

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

   An erase gap is written on the tape unit specified by "uptr" for the length
   specified by "gap_size" in tenths of an inch, and the status of the operation
   is returned.  The tape density must have been set by a previous
   sim_tape_set_dens call; if it has not, then no action is taken, and MTSE_FMT
   is returned.

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
    return MTSE_FMT;                                    /*   then report a format error */
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


/* Erase a specified number of bytes.

   An erase gap is written on the tape unit specified by "uptr" for the number
   of bytes specified by "bc", and the status of the operation is returned.  No
   checking is done to preserve the tape structure while erasing, so the caller
   is responsible for ensuring that the format remains valid.

   If the requested byte count is zero, or the tape format currently selected
   does not support erase gaps, the call succeeds with no action taken.  This
   allows a device simulator that supports writing erase gaps to use the same
   code without worrying about the tape format currently selected by the user.
*/

t_stat sim_tape_erase (UNIT *uptr, t_mtrlnt bc)
{
const uint32 format = MT_GET_FMT (uptr);                /* the tape format */

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then we cannot proceed */

else if (sim_tape_wrp (uptr))                           /* otherwise if the unit is write protected */
    return MTSE_WRP;                                    /*   then we cannot write */

else if (bc == 0                                        /* if the count is zero */
 || (format != MTUF_F_STD && format != MTUF_F_EXT))     /*   or gaps are not supported */
    return MTSE_OK;                                     /*     then take no action */

else                                                    /* otherwise */
    return tape_erase (uptr, bc);                       /*   erase the requested number of bytes */
}


/* Space record forward.

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to returned record length or marker

   Outputs:
        status  =       operation status

   exit condition       tape position
   ------------------   -----------------------------------------------------
   unit unattached      unchanged
   read error           unchanged, PNU set if initial read
   end of file/medium   updated if a gap precedes, else unchanged and PNU set
   tape mark            updated
   other marker         updated
   tape runaway         updated
   data record          updated

   This routine is called to space over a record or metadatum marker in the
   forward direction.  On return, status is MTSE_OK if a data record, private
   marker, or reserved marker was read, or an MTSE error code if a standard
   marker (e.g. tape mark) was read, or an error occurred.  In all cases, the
   successfully read marker or data record length word is returned via the "bc"
   pointer.

   When the extended SIMH format is enabled, then the variable addressed by the
   "bc" parameter must be set on entry to a bitmap of the object classes to
   return.  Each of the classes is represented by its corresponding bit, i.e.,
   bit 0 represents class 0, bit 1 for class 1, etc.  The routine will return
   only objects from the selected classes.  Unselected class objects will be
   ignored by skipping over them until the first selected class object is seen.
   This allows a simulator to declare those classes it understands (e.g.,
   standard classes 0 and 8, plus private classes 2 and 7) and those classes it
   wishes to ignore.  Erase gap markers are always skipped, and standard markers
   are always returned, so specifying an empty bitmap will perform the
   equivalent of a "space file forward," returning only when a tape mark or
   EOM/EOF is encountered.

   When standard SIMH format is enabled, standard classes 0 and 8 are
   automatically selected, and the entry value addressed by "bc" is ignored.

   If the PNU ("position not updated") flag is set, then an error prevented a
   preceding tape read or write command from moving the tape position.  A space
   command immediately following such a failed command is assumed to be part of
   a reposition-and-retry error recovery sequence.  Because the tape was not
   actually moved, we skip the corresponding reposition here, so that the tape
   will be correctly positioned for the retry.


   Implementation notes:

    1. PNU is set by both read and write positioning failures.  A retry sequence
       would match a space reverse with a forward read, and vice versa.  We do
       not maintain separate PNUs for forward and reverse operations, so a space
       forward following a failed read forward would not move the tape.
       However, this condition is deemed so unlikely as not to warrant keeping
       direction-specific PNU flags.
*/

t_stat sim_tape_sprecf (UNIT *uptr, t_mtrlnt *bc)
{
const uint32 f = MT_GET_FMT (uptr);
t_stat       st;

if (MT_TST_PNU (uptr)) {                                /* if the PNU flag is set */
    MT_CLR_PNU (uptr);                                  /*   then clear it */

    *bc = 0;                                            /* report the record length as zero */
    return MTSE_OK;                                     /*   and return with no tape motion */
    }

st = sim_tape_rdlntf (uptr, bc);                        /* get the record length */

if (f != MTUF_F_EXT)                                    /* if the format is not extended SIMH */
    *bc = MTR_RL (*bc) & MTR_MAXLEN;                    /*   then return just the record length */

return st;
}


/* Space record reverse.

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to returned record length or marker

   Outputs:
        status  =       operation status

   exit condition       tape position
   ------------------   -----------------------------------------------------
   unit unattached      unchanged
   beginning of tape    unchanged
   read error           unchanged, PNU set if initial read
   tape mark            updated
   other marker         updated
   tape runaway         updated
   data record          updated

   This routine is called to space over a record or metadatum marker in the
   reverse direction.  On return, status is MTSE_OK if a data record, private
   marker, or reserved marker was read, or an MTSE error code if a standard
   marker (e.g. tape mark) was read, or an error occurred.  In all cases, the
   successfully read marker or data record length word is returned via the "bc"
   pointer.

   See the comments for the "sim_tape_sprecr" routine above for additional
   considerations.
*/

t_stat sim_tape_sprecr (UNIT *uptr, t_mtrlnt *bc)
{
const uint32 f = MT_GET_FMT (uptr);
t_stat       st;

if (MT_TST_PNU (uptr)) {                                /* if the PNU flag is set */
    MT_CLR_PNU (uptr);                                  /*   then clear it */

    *bc = 0;                                            /* report the record length as zero */
    return MTSE_OK;                                     /*   and return with no tape motion */
    }

st = sim_tape_rdlntr (uptr, bc);                        /* get the record length */

if (f != MTUF_F_EXT)                                    /* if the format is not extended SIMH */
    *bc = MTR_RL (*bc) & MTR_MAXLEN;                    /*   then return just the record length */

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
return MTSE_OK;
}

/* Test for BOT */

t_bool sim_tape_bot (UNIT *uptr)
{
uint32 f = MT_GET_FMT (uptr);

return (uptr->pos <= fmts[f].bot) ? TRUE : FALSE;
}

/* Test for end of tape */

t_bool sim_tape_eot (UNIT *uptr)
{
return (uptr->capac && (uptr->pos >= uptr->capac)) ? TRUE : FALSE;
}

/* Test for write protect */

t_bool sim_tape_wrp (UNIT *uptr)
{
return ((uptr->flags & MTUF_WRP) || (MT_GET_FMT (uptr) == MTUF_F_TPC)) ? TRUE : FALSE;
}

/* Process I/O error */

static t_stat sim_tape_ioerr (UNIT *uptr)
{
perror ("Magtape library I/O error");
clearerr (uptr->fileref);
return MTSE_IOERR;
}


/* Set the tape format.

   This validation routine is called to change the tape format of the unit
   addressed by "uptr".  If "desc" is NULL, then the string pointed to by "cptr"
   must contain one of the defined format names, and "val" is ignored.
   Otherwise, "desc" must point to a "uint32" variable that receives the current
   tape format code, "val" must contain one of the MTUF_F_* constants in
   sim_tape.h that specifies the new format code, and "cptr" is ignored.

   If "cptr" specifies the new format name, the "fmts" table is searched for a
   matching entry; if one is not found, SCPE_ARG is returned.  The "SIMH" name
   is used for both SIMH Standard and SIMH Extended format; which is selected
   depends on whether the unit was set for extended format when the routine was
   first entered.  A unit declared with the MT_F_EXT flag indicates that it is
   prepared to handle the extended-format calling sequence, so switching back to
   "SIMH" format from another format returns the unit to SIMH Extended format.
   In contrast, a unit originally declared with another format, including
   MT_F_STD, indicates that it is not prepared to handle the extended format, so
   switching back returns to SIMH Standard format.

   If "val" specifies the new format, this check is skipped.  This allows a
   simulator to change to SIMH Extended format temporarily, e.g., to run
   diagnostics, while ensuring that extended-format records are ignored during
   normal simulator operation.


   Implementation notes:

    1. "fmts" table entries with blank (not NULL) names correspond to format
       codes that are reserved for future use.  They are skipped automatically
       during the name search because the "cptr" string is stripped of blanks
       before entry and thus will never match a table entry with a blank name.

    2. While the "fmts" table contains entries for both SIMH Standard and SIMH
       Extended formats, both entries use the same "SIMH" name.  Therefore, only
       the first (standard format) entry will match during the search.
*/

t_stat sim_tape_set_fmt (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 *old_fmt = (uint32 *) desc;                      /* a pointer to receive the current format */

if (uptr == NULL)                                       /* if the unit is not supplied */
    return SCPE_IERR;                                   /*   then report an internal error */

else if (uptr->flags & UNIT_ATT)                        /* otherwise if the unit is attached */
    return SCPE_ALATT;                                  /*   then report an already attached error */

else if (desc == NULL) {                                /* otherwise if the format is set interactively */
    if (cptr == NULL || *cptr == '\0')                  /*   then if there is no format keyword */
        return SCPE_ARG;                                /*     then report an argument error */

    if (MT_GET_FMT (uptr) == MTUF_F_EXT)                /* if the unit accepts extended SIMH format */
        uptr->dynflags |= UNIT_EXTEND;                  /*   then enable changing back to that format */

    for (val = 0; val < (int32) FMT_COUNT; val++)       /* loop through the format name table */
        if (MATCH_CMD (cptr, fmts [val].name) == 0) {   /* if the name matches */
            if (val == MTUF_F_STD                       /*   then if it's SIMH format */
              && (uptr->dynflags & UNIT_EXTEND))        /*     and the unit handles extended format */
                val = MTUF_F_EXT;                       /*       then request extended format */

            uptr->flags = (uptr->flags & ~MTUF_FMT)     /*  change to the new format */
                            | fmts [val].uflags;        /*    including any required unit flags */
            return SCPE_OK;                             /*     and return success */
            }

    return SCPE_ARG;                                    /* the name is not in the format table */
    }

else                                                    /* otherwise set the format programmatically */
    if (val < 0 || val >= (int32) FMT_COUNT)            /*   then the supplied value is out of range */
        return SCPE_ARG;                                /*     then report an argument error */

    else {                                              /* otherwise the format code is valid */
        *old_fmt = MT_GET_FMT (uptr);                   /*   so return the current code */

        uptr->flags = (uptr->flags & ~MTUF_FMT)         /* change to the new format */
                        | fmts [val].uflags;            /*   including any required unit flags */
        return SCPE_OK;                                 /*     and return success */
        }
}


/* Show tape format */

t_stat sim_tape_show_fmt (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 f = MT_GET_FMT (uptr);

if (f < (int32) FMT_COUNT && fmts [f].name [0] != ' ')
    fprintf (st, "%s format", fmts [f].name);
else
    fprintf (st, "invalid format");
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
        return ((p == 0) ? map[p] : map[p - 1]);
    else if (uptr->pos < map[p])
        hi = p - 1;
    else lo = p + 1;
    }
while (lo <= hi);
return ((p == 0) ? map[p] : map[p - 1]);
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
cap = (t_addr) get_uint (cptr, 10, sim_taddr_64 ? 2000000 : 2000, &r);
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
