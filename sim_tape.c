/* sim_tape.c: simulator tape support library

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

   Ultimately, this will be a place to hide processing of various tape formats,
   as well as OS-specific direct hardware access.

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
   sim_tape_rewind      rewind
   sim_tape_reset       reset unit
   sim_tape_bot         TRUE if at beginning of tape
   sim_tape_eot         TRUE if at or beyond end of tape
   sim_tape_wrp         TRUE if write protected
   sim_tape_set_fmt     set tape format
   sim_tape_show_fmt    show tape format
   sim_tape_set_capac   set tape capacity
   sim_tape_show_capac  show tape capacity
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

extern int32 sim_switches;

t_stat sim_tape_ioerr (UNIT *uptr);
t_stat sim_tape_wrdata (UNIT *uptr, uint32 dat);
uint32 sim_tape_tpc_map (UNIT *uptr, t_addr *map);
t_addr sim_tape_tpc_fnd (UNIT *uptr, t_addr *map);

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

/* Read record length forward (internal routine)

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to returned record length
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   read error           unchanged, PNU set
   end of file/medium   unchanged, PNU set
   tape mark            updated
   data record          updated, sim_fread will read record forward

   See notes at "sim_tape_wrgap" regarding erase gap implementation.
*/

t_stat sim_tape_rdlntf (UNIT *uptr, t_mtrlnt *bc)
{
uint8 c;
t_bool all_eof;
uint32 f = MT_GET_FMT (uptr);
t_mtrlnt sbc;
t_tpclnt tpcbc;

MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return MTSE_UNATT;
sim_fseek (uptr->fileref, uptr->pos, SEEK_SET);         /* set tape pos */
switch (f) {                                            /* switch on fmt */

    case MTUF_F_STD: case MTUF_F_E11:
        do {
            sim_fread (bc, sizeof (t_mtrlnt), 1, uptr->fileref);    /* read rec lnt */
            sbc = MTR_L (*bc);                          /* save rec lnt */
            if (ferror (uptr->fileref)) {               /* error? */
                MT_SET_PNU (uptr);                      /* pos not upd */
                return sim_tape_ioerr (uptr);
                }
            if (feof (uptr->fileref) || (*bc == MTR_EOM)) {     /* eof or eom? */
                MT_SET_PNU (uptr);                      /* pos not upd */
                return MTSE_EOM;
                }
            uptr->pos = uptr->pos + sizeof (t_mtrlnt);  /* spc over rec lnt */
            if (*bc == MTR_TMK)                         /* tape mark? */
                return MTSE_TMK;
            if (*bc == MTR_FHGAP) {                     /* half gap? */
                uptr->pos = uptr->pos + sizeof (t_mtrlnt) / 2;  /* half space fwd */
                sim_fseek (uptr->fileref, uptr->pos, SEEK_SET); /* resync */
                }
            else if (*bc != MTR_GAP)
                uptr->pos = uptr->pos + sizeof (t_mtrlnt) +     /* spc over record */
                    ((f == MTUF_F_STD)? ((sbc + 1) & ~1): sbc);
            }
        while ((*bc == MTR_GAP) || (*bc == MTR_FHGAP));
        break;

    case MTUF_F_TPC:
        sim_fread (&tpcbc, sizeof (t_tpclnt), 1, uptr->fileref);
        *bc = tpcbc;                                    /* save rec lnt */
        if (ferror (uptr->fileref)) {                   /* error? */
            MT_SET_PNU (uptr);                          /* pos not upd */
            return sim_tape_ioerr (uptr);
            }
        if (feof (uptr->fileref)) {                     /* eof? */
            MT_SET_PNU (uptr);                          /* pos not upd */
            return MTSE_EOM;
            }
        uptr->pos = uptr->pos + sizeof (t_tpclnt);      /* spc over reclnt */
        if (tpcbc == TPC_TMK)                           /* tape mark? */
            return MTSE_TMK;
        uptr->pos = uptr->pos + ((tpcbc + 1) & ~1);     /* spc over record */
        break;

    case MTUF_F_P7B:
        for (sbc = 0, all_eof = 1; ; sbc++) {           /* loop thru record */
            sim_fread (&c, sizeof (uint8), 1, uptr->fileref);
            if (ferror (uptr->fileref)) {               /* error? */
                MT_SET_PNU (uptr);                      /* pos not upd */
                return sim_tape_ioerr (uptr);
                }
            if (feof (uptr->fileref)) {                 /* eof? */
                if (sbc == 0)                           /* no data? eom */
                    return MTSE_EOM;
                break;                                  /* treat like eor */
                }
            if ((sbc != 0) && (c & P7B_SOR))            /* next record? */
                break;
            if ((c & P7B_DPAR) != P7B_EOF)
                all_eof = 0;
            }
        *bc = sbc;                                      /* save rec lnt */
        sim_fseek (uptr->fileref, uptr->pos, SEEK_SET); /* for read */
        uptr->pos = uptr->pos + sbc;                    /* spc over record */
        if (all_eof)                                    /* tape mark? */
            return MTSE_TMK;
        break;

    default:
        return MTSE_FMT;
        }

return MTSE_OK;
}

/* Read record length reverse (internal routine)

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to returned record length
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   beginning of tape    unchanged
   read error           unchanged
   end of file          unchanged
   end of medium        updated
   tape mark            updated
   data record          updated, sim_fread will read record forward

   See notes at "sim_tape_wrgap" regarding erase gap implementation.
*/

t_stat sim_tape_rdlntr (UNIT *uptr, t_mtrlnt *bc)
{
uint8 c;
t_bool all_eof;
uint32 f = MT_GET_FMT (uptr);
t_addr ppos;
t_mtrlnt sbc;
t_tpclnt tpcbc;

MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return MTSE_UNATT;
if (sim_tape_bot (uptr))                                /* at BOT? */
    return MTSE_BOT;
switch (f) {                                            /* switch on fmt */

    case MTUF_F_STD: case MTUF_F_E11:
        do {
            sim_fseek (uptr->fileref, uptr->pos - sizeof (t_mtrlnt), SEEK_SET);
            sim_fread (bc, sizeof (t_mtrlnt), 1, uptr->fileref);    /* read rec lnt */
            sbc = MTR_L (*bc);
            if (ferror (uptr->fileref))                 /* error? */
                return sim_tape_ioerr (uptr);
            if (feof (uptr->fileref))                   /* eof? */
                return MTSE_EOM;
            uptr->pos = uptr->pos - sizeof (t_mtrlnt);  /* spc over rec lnt */
            if (*bc == MTR_EOM)                         /* eom? */
                return MTSE_EOM;
            if (*bc == MTR_TMK)                         /* tape mark? */
                return MTSE_TMK;
            if ((*bc & MTR_M_RHGAP) == MTR_RHGAP) {     /* half gap? */
                uptr->pos = uptr->pos + sizeof (t_mtrlnt) / 2;  /* half space rev */
                sim_fseek (uptr->fileref, uptr->pos, SEEK_SET); /* resync */
                }
            else if (*bc != MTR_GAP) {
                uptr->pos = uptr->pos - sizeof (t_mtrlnt) - /* spc over record */
                    ((f == MTUF_F_STD)? ((sbc + 1) & ~1): sbc);
                sim_fseek (uptr->fileref, uptr->pos + sizeof (t_mtrlnt), SEEK_SET);
                }
            else if (sim_tape_bot (uptr))               /* backed into BOT? */
                return MTSE_BOT;
        }
        while ((*bc == MTR_GAP) || (*bc == MTR_RHGAP));
        break;

    case MTUF_F_TPC:
        ppos = sim_tape_tpc_fnd (uptr, (t_addr *) uptr->filebuf); /* find prev rec */
        sim_fseek (uptr->fileref, ppos, SEEK_SET);      /* position */
        sim_fread (&tpcbc, sizeof (t_tpclnt), 1, uptr->fileref);
        *bc = tpcbc;                                    /* save rec lnt */
        if (ferror (uptr->fileref))                     /* error? */
            return sim_tape_ioerr (uptr);
        if (feof (uptr->fileref))                       /* eof? */
            return MTSE_EOM;
        uptr->pos = ppos;                               /* spc over record */
        if (*bc == MTR_TMK)                             /* tape mark? */
            return MTSE_TMK;
        sim_fseek (uptr->fileref, uptr->pos + sizeof (t_tpclnt), SEEK_SET);
        break;

    case MTUF_F_P7B:
        for (sbc = 1, all_eof = 1; (t_addr) sbc <= uptr->pos ; sbc++) {
            sim_fseek (uptr->fileref, uptr->pos - sbc, SEEK_SET);
            sim_fread (&c, sizeof (uint8), 1, uptr->fileref);
            if (ferror (uptr->fileref))                 /* error? */
                return sim_tape_ioerr (uptr);
            if (feof (uptr->fileref))                   /* eof? */
                return MTSE_EOM;
            if ((c & P7B_DPAR) != P7B_EOF)
                all_eof = 0;
            if (c & P7B_SOR)                            /* start of record? */
                break;
            }
        uptr->pos = uptr->pos - sbc;                    /* update position */
        *bc = sbc;                                      /* save rec lnt */
        sim_fseek (uptr->fileref, uptr->pos, SEEK_SET); /* for read */
        if (all_eof)                                    /* tape mark? */
            return MTSE_TMK;
        break;

    default:
        return MTSE_FMT;
        }

return MTSE_OK;
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
if (st = sim_tape_rdlntf (uptr, &tbc))                  /* read rec lnt */
    return st;
*bc = rbc = MTR_L (tbc);                                /* strip error flag */
if (rbc > max) {                                        /* rec out of range? */
    MT_SET_PNU (uptr);
    uptr->pos = opos;
    return MTSE_INVRL;
    }
i = sim_fread (buf, sizeof (uint8), rbc, uptr->fileref);/* read record */
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

if (st = sim_tape_rdlntr (uptr, &tbc))                  /* read rec lnt */
    return st;
*bc = rbc = MTR_L (tbc);                                /* strip error flag */
if (rbc > max)                                          /* rec out of range? */
    return MTSE_INVRL;
i = sim_fread (buf, sizeof (uint8), rbc, uptr->fileref);/* read record */
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

t_stat sim_tape_wrdata (UNIT *uptr, uint32 dat)
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
if (MT_GET_FMT (uptr) == MTUF_F_P7B)                    /* cant do P7B */
    return MTSE_FMT;
return sim_tape_wrdata (uptr, MTR_EOM);
}

/* Write erase gap

   Inputs:
        uptr    = pointer to tape unit
        gaplen  = length of gap in tenths of an inch
        bpi     = current recording density in bytes per inch

   Outputs:
        status  = operation status

   exit condition       position
   ------------------   ------------------
   unit unattached      unchanged
   unsupported format   unchanged
   write protected      unchanged
   read error           unchanged, PNU set
   write error          unchanged, PNU set
   gap written          updated


   An erase gap is represented in the tape image file by a special metadata
   value.  This value is chosen so that it is still recognizable even if it has
   been "cut in half" by a subsequent data overwrite that does not end on a
   metadatum-sized boundary.  In addition, a range of metadata values are
   reserved for detection in the reverse direction.  Erase gaps are supported
   only in SIMH tape format.

   This implementation supports erasing gaps in the middle of a populated tape
   image and will always produce a valid image.  It also produces valid images
   when overwriting gaps with data records, with one exception: a data write
   that leaves only two bytes of gap remaining will produce an invalid tape.
   This limitation is deemed acceptable, as it is analogous to the existing
   limitation that data records cannot overwrite other data records without
   producing an invalid tape.

   Because SIMH tape images do not carry physical parameters (e.g., recording
   density), overwriting a tape image file containing gap metadata is
   problematic if the density setting is not the same as that used during
   recording.  There is no way to establish a gap of a certain length
   unequivocally in an image file, so this implementation establishes a gap of a
   certain number of bytes that reflect the desired gap length at the bpi used
   during writing.

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
   truncated appropriately.

   When reading in either direction, gap metadata markers are ignored (skipped)
   until a record length header, EOF marker, EOM marker, or physical EOF is
   encountered.  Thus, tape images containing gap metadata are transparent to
   the calling simulator.

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
 */

t_stat sim_tape_wrgap (UNIT *uptr, uint32 gaplen, uint32 bpi)
{
t_stat st;
t_mtrlnt meta, sbc, new_len, rec_size;
t_addr gap_pos = uptr->pos;
uint32 file_size, marker_count;
uint32 format = MT_GET_FMT (uptr);
uint32 gap_alloc = 0;                                   /* gap allocated from tape */
int32 gap_needed = (gaplen * bpi) / 10;                 /* gap remainder still needed */
const uint32 meta_size = sizeof (t_mtrlnt);             /* bytes per metadatum */
const uint32 min_rec_size = 2 + sizeof (t_mtrlnt) * 2;  /* smallest data record size */

MT_CLR_PNU (uptr);

if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return MTSE_UNATT;
if (format != MTUF_F_STD)                               /* not SIMH fmt? */
    return MTSE_FMT;
if (sim_tape_wrp (uptr))                                /* write protected? */
    return MTSE_WRP;

file_size = sim_fsize (uptr->fileref);                  /* get file size */
sim_fseek (uptr->fileref, uptr->pos, SEEK_SET);         /* position tape */

/* Read tape records and allocate to gap until amount required is consumed.

   Read next metadatum from tape:
    - EOF or EOM: allocate remainder of bytes needed.
    - TMK or GAP: allocate sizeof(metadatum) bytes.
    - Reverse GAP: allocate sizeof(metadatum) / 2 bytes.
    - Data record: see below.

   Loop until bytes needed = 0.
*/

do {
    sim_fread (&meta, meta_size, 1, uptr->fileref);     /* read metadatum */

    if (ferror (uptr->fileref)) {                       /* read error? */
        uptr->pos = gap_pos;                            /* restore original position */
        MT_SET_PNU (uptr);                              /* position not updated */
        return sim_tape_ioerr (uptr);                   /* translate error */
        }
    else
        uptr->pos = uptr->pos + meta_size;              /* move tape over datum */

    if (feof (uptr->fileref) || (meta == MTR_EOM)) {    /* at eof or eom? */
        gap_alloc = gap_alloc + gap_needed;             /* allocate remainder */
        gap_needed = 0;
        }

    else if ((meta == MTR_GAP) || (meta == MTR_TMK)) {  /* gap or tape mark? */
        gap_alloc = gap_alloc + meta_size;              /* allocate marker space */
        gap_needed = gap_needed - meta_size;            /* reduce requirement */
        }

    else if (meta == MTR_FHGAP) {                       /* half gap? */
        uptr->pos = uptr->pos - meta_size / 2;          /* backup to resync */
        sim_fseek (uptr->fileref, uptr->pos, SEEK_SET); /* position tape */
        gap_alloc = gap_alloc + meta_size / 2;          /* allocate marker space */
        gap_needed = gap_needed - meta_size / 2;        /* reduce requirement */
        }

    else if (uptr->pos + 
             MTR_L (meta) + meta_size > file_size) {    /* rec len out of range? */
        gap_alloc = gap_alloc + gap_needed;             /* presume overwritten tape */
        gap_needed = 0;                                 /* allocate remainder */
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
            sim_fseek (uptr->fileref, uptr->pos, SEEK_SET); /* move tape */
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
while (gap_needed > 0);

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

/* Space record forward */

t_stat sim_tape_sprecf (UNIT *uptr, t_mtrlnt *bc)
{
t_stat st;

st = sim_tape_rdlntf (uptr, bc);                        /* get record length */
*bc = MTR_L (*bc);
return st;
}

/* Space record reverse */

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
return (uptr->flags & MTUF_WRP)? TRUE: FALSE;
}

/* Process I/O error */

t_stat sim_tape_ioerr (UNIT *uptr)
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

uint32 sim_tape_tpc_map (UNIT *uptr, t_addr *map)
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

t_addr sim_tape_tpc_fnd (UNIT *uptr, t_addr *map)
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
extern uint32 sim_taddr_64;
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
