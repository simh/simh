/* sim_tape.h: simulator tape support library

   Copyright (c) 1993-2003, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   Ultimately, this will be a place to hide processing of various tape formats,
   as well as OS-specific direct hardware access.

   Public routines:

   sim_tape_attach	attach tape unit
   sim_tape_detach	detach tape unit
   sim_tape_rdrecf	read tape record forward
   sim_tape_rdrecr	read tape record reverse
   sim_tape_wrrecf	write tape record forward
   sim_tape_sprecf	space tape record forward
   sim_tape_sprecr	space tape record reverse
   sim_tape_wrtmk	write tape mark
   sim_tape_wreom	erase remainder of tape
   sim_tape_rewind	rewind
   sim_tape_reset	reset unit
   sim_tape_bot		TRUE if at beginning of tape
   sim_tape_eot		TRUE if at or beyond end of tape
   sim_tape_wrp		TRUE if write protected
*/

#include "sim_defs.h"
#include "sim_tape.h"

t_stat sim_tape_ioerr (UNIT *uptr);
t_stat sim_tape_wrdata (UNIT *uptr, uint32 dat);

/* Attach tape unit */

t_stat sim_tape_attach (UNIT *uptr, char *cptr)
{
t_stat r = attach_unit (uptr, cptr);
if (r != SCPE_OK) return r;
return sim_tape_rewind (uptr);
}

/* Detach tape unit */

t_stat sim_tape_detach (UNIT *uptr)
{
t_stat r = detach_unit (uptr);

if (r != SCPE_OK) return r;
return sim_tape_rewind (uptr);
}

/* Read record length forward (internal routine)

   Inputs:
	uptr	=	pointer to tape unit
	bc	=	pointer to returned record length
   Outputs:
	status	=	operation status

   exit condition	position

   unit unattached	unchanged
   read error		unchanged, PNU set
   end of file/medium	unchanged, PNU set
   tape mark		updated
   data record		unchanged
*/

t_stat sim_tape_rdlntf (UNIT *uptr, t_mtrlnt *bc)
{
MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0) return MTSE_UNATT;	/* not attached? */
fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* set tape pos */
fxread (bc, sizeof (t_mtrlnt), 1, uptr->fileref);	/* read rec lnt */
if (ferror (uptr->fileref)) {				/* error? */
	MT_SET_PNU (uptr);				/* pos not upd */
	return sim_tape_ioerr (uptr);  }
if (feof (uptr->fileref) || (*bc == MTR_EOM)) {		/* eof or eom? */
	MT_SET_PNU (uptr);				/* pos not upd */
	return MTSE_EOM;  }
if (*bc == MTR_TMK) {					/* tape mark? */
	uptr->pos = uptr->pos + sizeof (t_mtrlnt);	/* spc over tmk */
	return MTSE_TMK;  }
return MTSE_OK;
}

/* Read record length reverse (internal routine)

   Inputs:
	uptr	=	pointer to tape unit
	bc	=	pointer to returned record length
   Outputs:
	status	=	operation status

   exit condition	position

   unit unattached	unchanged
   beginning of tape	unchanged
   read error		unchanged
   end of file		unchanged
   end of medium	updated
   tape mark		updated
   data record		unchanged
*/

t_stat sim_tape_rdlntr (UNIT *uptr, t_mtrlnt *bc)
{
MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0) return MTSE_UNATT;	/* not attached? */
if (uptr->pos < sizeof (t_mtrlnt)) return MTSE_BOT;	/* at BOT? */
fseek (uptr->fileref, uptr->pos - sizeof (t_mtrlnt), SEEK_SET);
fxread (bc, sizeof (t_mtrlnt), 1, uptr->fileref);	/* read rec lnt */
if (ferror (uptr->fileref))				/* error? */
	return sim_tape_ioerr (uptr);
if (feof (uptr->fileref)) return MTSE_EOM;		/* eof? */
if (*bc == MTR_EOM) {					/* eom? */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* spc over eom */
	return MTSE_EOM;  }
if (*bc == MTR_TMK) {					/* tape mark? */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* spc over tmk */
	return MTSE_TMK;  }
return SCPE_OK;
}

/* Read record forward

   Inputs:
	uptr	=	pointer to tape unit
	buf	=	pointer to buffer
	bc	=	pointer to returned record length
	max	=	maximum record size
   Outputs:
	status	=	operation status

   exit condition	position

   unit unattached	unchanged
   read error		unchanged, PNU set
   end of file/medium	unchanged, PNU set
   invalid record	unchanged, PNU set
   tape mark		updated
   data record		updated
   data record error	updated
*/

t_stat sim_tape_rdrecf (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max)
{
t_mtrlnt i, tbc, rbc, ebc;
t_stat st;

if (st = sim_tape_rdlntf (uptr, &tbc)) return st;	/* read rec lnt */
*bc = rbc = MTR_L (tbc);				/* strip error flag */
ebc = (rbc + 1) & ~1;
if (rbc > max) {					/* rec out of range? */
	MT_SET_PNU (uptr);
	return MTSE_INVRL;  }
i = fxread (buf, sizeof (uint8), rbc, uptr->fileref);	/* read record */
if (ferror (uptr->fileref)) {				/* error? */
	MT_SET_PNU (uptr);
	return sim_tape_ioerr (uptr);  }
for ( ; i < rbc; i++) buf[i] = 0;			/* fill with 0's */
uptr->pos = uptr->pos + ebc + (2 * sizeof (t_mtrlnt));	/* move tape */
return (MTR_F (tbc)? MTSE_RECE: MTSE_OK);
}

/* Read record reverse

   Inputs:
	uptr	=	pointer to tape unit
	buf	=	pointer to buffer
	bc	=	pointer to returned record length
	max	=	maximum record size
   Outputs:
	status	=	operation status

   exit condition	position

   unit unattached	unchanged
   read error		unchanged
   end of file		unchanged
   end of medium	updated
   invalid record	unchanged
   tape mark		updated
   data record		updated
   data record error	updated
*/

t_stat sim_tape_rdrecr (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max)
{
t_mtrlnt i, rbc, tbc, ebc;
t_stat st;

if (st = sim_tape_rdlntr (uptr, &tbc)) return st;	/* read rec lnt */
*bc = rbc = MTR_L (tbc);				/* strip error flag */
ebc = (rbc + 1) & ~1;
if (rbc > max) return MTSE_INVRL;			/* rec out of range? */
fseek (uptr->fileref, uptr->pos - sizeof (t_mtrlnt) - ebc, SEEK_SET);
i = fxread (buf, sizeof (uint8), rbc, uptr->fileref);	/* read record */
if (ferror (uptr->fileref))				/* error? */
	return sim_tape_ioerr (uptr);
for ( ; i < rbc; i++) buf[i] = 0;			/* fill with 0's */
uptr->pos = uptr->pos - ebc - (2 * sizeof (t_mtrlnt));	/* move tape */
return (MTR_F (tbc)? MTSE_RECE: MTSE_OK);
}

/* Write record forward

   Inputs:
	uptr	=	pointer to tape unit
	buf	=	pointer to buffer
	bc	=	record length
   Outputs:
	status	=	operation status

   exit condition	position

   unit unattached	unchanged
   write protect	unchanged
   write error		unchanged, PNU set
   data record		updated
*/

t_stat sim_tape_wrrecf (UNIT *uptr, uint8 *buf, t_mtrlnt bc)
{
t_mtrlnt ebc = (MTR_L (bc) + 1) & ~1;

MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0) return MTSE_UNATT;	/* not attached? */
if (uptr->flags & MTUF_WRP) return MTSE_WRP;		/* write prot? */
fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* set pos */
fxwrite (&bc, sizeof (t_mtrlnt), 1, uptr->fileref);
fxwrite (buf, sizeof (uint8), ebc, uptr->fileref);
fxwrite (&bc, sizeof (t_mtrlnt), 1, uptr->fileref);
if (ferror (uptr->fileref)) {				/* error? */
	MT_SET_PNU (uptr);
	return sim_tape_ioerr (uptr);  }
uptr->pos = uptr->pos + ebc + (2 * sizeof (t_mtrlnt));	/* move tape */
return MTSE_OK;
}

/* Write metadata forward (internal routine) */

t_stat sim_tape_wrdata (UNIT *uptr, uint32 dat)
{
MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0) return MTSE_UNATT;	/* not attached? */
if (uptr-> flags & MTUF_WRP) return MTSE_WRP;		/* write prot? */
fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* set pos */
fxwrite (&dat, sizeof (t_mtrlnt), 1, uptr->fileref);
if (ferror (uptr->fileref)) {				/* error? */
	MT_SET_PNU (uptr);
	return sim_tape_ioerr (uptr);  }
uptr->pos = uptr->pos + sizeof (t_mtrlnt);		/* move tape */
return MTSE_OK;
}

/* Write tape mark */

t_stat sim_tape_wrtmk (UNIT *uptr)
{
return sim_tape_wrdata (uptr, MTR_TMK);
}

/* Write end of medium */

t_stat sim_tape_wreom (UNIT *uptr)
{
return sim_tape_wrdata (uptr, MTR_EOM);
}

/* Space record forward */

t_stat sim_tape_sprecf (UNIT *uptr, t_mtrlnt *bc)
{
t_mtrlnt ebc;
t_stat st;

if (st = sim_tape_rdlntf (uptr, bc)) return st;		/* get record length */
*bc = MTR_L (*bc);
ebc = (*bc + 1) & ~1;
uptr->pos = uptr->pos + ebc + (2 * sizeof (t_mtrlnt));	/* update position */
return MTSE_OK;
}

/* Space record reverse */

t_stat sim_tape_sprecr (UNIT *uptr, t_mtrlnt *bc)
{
t_mtrlnt ebc;
t_stat st;

if (MT_TST_PNU (uptr)) {
	MT_CLR_PNU (uptr);
	return SCPE_OK;  }
if (st = sim_tape_rdlntr (uptr, bc)) return st;		/* get record length */
*bc = MTR_L (*bc);
ebc = (*bc + 1) & ~1;
uptr->pos = uptr->pos - ebc - (2 * sizeof (t_mtrlnt));	/* update position */
return MTSE_OK;
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
return (uptr->pos < sizeof (t_mtrlnt))? TRUE: FALSE;
}

/* Test for end of tape */

t_bool sim_tape_eot (UNIT *uptr, t_addr cap)
{
return (uptr->pos > cap)? TRUE: FALSE;
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
return SCPE_IOERR;
}
