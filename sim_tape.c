/* sim_tape.c: simulator tape support library

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

   25-Apr-03	RMS	Added extended file support
   28-Mar-03	RMS	Added E11 and TPC format support

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
   sim_tape_set_fmt	set tape format
   sim_tape_show_fmt	show tape format
*/

#include "sim_defs.h"
#include "sim_tape.h"

struct sim_tape_fmt {
	char		*name;				/* name */
	int32		uflags;				/* unit flags */
	t_addr		bot;				/* bot test */
};

static struct sim_tape_fmt fmts[MTUF_N_FMT] = {
	{ "SIMH", 0,       sizeof (t_mtrlnt) - 1 },
	{ "E11",  0,       sizeof (t_mtrlnt) - 1 },
	{ "TPC",  UNIT_RO, sizeof (t_tpclnt) - 1 },
/*	{ "TPF",  UNIT_RO, 0 }, */
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

if (sim_switches & SWMASK ('F')) {			/* format spec? */
	cptr = get_glyph (cptr, gbuf, 0);		/* get spec */
	if (*cptr == 0) return SCPE_2FARG;		/* must be more */
	if (sim_tape_set_fmt (uptr, 0, gbuf, NULL) != SCPE_OK)
	    return SCPE_ARG;  }
r = attach_unit (uptr, cptr);				/* attach unit */
if (r != SCPE_OK) return r;				/* error? */

switch (MT_GET_FMT (uptr)) {				/* case on format */
case MTUF_F_TPC:					/* TPC */
	objc = sim_tape_tpc_map (uptr, NULL);		/* get # objects */
	if (objc == 0) {				/* tape empty? */
		sim_tape_detach (uptr);
		return SCPE_FMT;  }			/* yes, complain */
	uptr->filebuf = calloc (objc + 1, sizeof (t_mtrlnt));
	if (uptr->filebuf == NULL) {			/* map allocated? */
		sim_tape_detach (uptr);
		return SCPE_MEM;  }			/* no, complain */
	uptr->hwmark = objc + 1;			/* save map size */
	sim_tape_tpc_map (uptr, uptr->filebuf);		/* fill map */
	break;
default:
	break;  }

sim_tape_rewind (uptr);
return SCPE_OK;
}

/* Detach tape unit */

t_stat sim_tape_detach (UNIT *uptr)
{
uint32 f = MT_GET_FMT (uptr);
t_stat r;

r = detach_unit (uptr);					/* detach unit */
if (r != SCPE_OK) return r;

switch (f) {						/* case on format */
case MTUF_F_TPC:					/* TPC */
	if (uptr->filebuf) free (uptr->filebuf);	/* free map */
	uptr->filebuf = NULL;
	uptr->hwmark = 0;
	break;
default:
	break;  }

sim_tape_rewind (uptr);
return SCPE_OK;
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
   data record		updated, fxread will read record forward
*/

t_stat sim_tape_rdlntf (UNIT *uptr, t_mtrlnt *bc)
{
uint32 f = MT_GET_FMT (uptr);
t_mtrlnt sbc;
t_tpclnt tpcbc;

MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0) return MTSE_UNATT;	/* not attached? */
fseek_ext (uptr->fileref, uptr->pos, SEEK_SET);		/* set tape pos */

switch (f) {						/* switch on fmt */
case MTUF_F_STD: case MTUF_F_E11:
	fxread (bc, sizeof (t_mtrlnt), 1, uptr->fileref); /* read rec lnt */
	sbc = MTR_L (*bc);				/* save rec lnt */
	if (ferror (uptr->fileref)) {			/* error? */
	    MT_SET_PNU (uptr);				/* pos not upd */
	    return sim_tape_ioerr (uptr);  }
	if (feof (uptr->fileref) || (*bc == MTR_EOM)) {	/* eof or eom? */
	    MT_SET_PNU (uptr);				/* pos not upd */
	    return MTSE_EOM;  }
	uptr->pos = uptr->pos + sizeof (t_mtrlnt);	/* spc over rec lnt */
	if (*bc == MTR_TMK) return MTSE_TMK;		/* tape mark? */
	uptr->pos = uptr->pos + sizeof (t_mtrlnt) +	/* spc over record */
	    ((f == MTUF_F_STD)? ((sbc + 1) & ~1): sbc);
	break;

case MTUF_F_TPC:
	fxread (&tpcbc, sizeof (t_tpclnt), 1, uptr->fileref);
	*bc = tpcbc;					/* save rec lnt */
	if (ferror (uptr->fileref)) {			/* error? */
	    MT_SET_PNU (uptr);				/* pos not upd */
	    return sim_tape_ioerr (uptr);  }
	if (feof (uptr->fileref)) {			/* eof? */
	    MT_SET_PNU (uptr);				/* pos not upd */
	    return MTSE_EOM;  }
	uptr->pos = uptr->pos + sizeof (t_tpclnt);	/* spc over reclnt */
	if (tpcbc == TPC_TMK) return MTSE_TMK;		/* tape mark? */
	uptr->pos = uptr->pos + ((tpcbc + 1) & ~1);	/* spc over record */
	break;

default:
	return MTSE_FMT;  }

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
   data record		updated, fxread will read record forward
*/

t_stat sim_tape_rdlntr (UNIT *uptr, t_mtrlnt *bc)
{
uint32 f = MT_GET_FMT (uptr);
t_addr ppos;
t_mtrlnt sbc;
t_tpclnt tpcbc;

MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0) return MTSE_UNATT;	/* not attached? */
if (sim_tape_bot (uptr)) return MTSE_BOT;		/* at BOT? */

switch (f) {						/* switch on fmt */
case MTUF_F_STD: case MTUF_F_E11:
	fseek_ext (uptr->fileref, uptr->pos - sizeof (t_mtrlnt), SEEK_SET);
	fxread (bc, sizeof (t_mtrlnt), 1, uptr->fileref); /* read rec lnt */
	sbc = MTR_L (*bc);
	if (ferror (uptr->fileref))			/* error? */
	    return sim_tape_ioerr (uptr);
	if (feof (uptr->fileref)) return MTSE_EOM;	/* eof? */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* spc over rec lnt */
	if (*bc == MTR_EOM) return MTSE_EOM;		/* eom? */
	if (*bc == MTR_TMK) return MTSE_TMK;		/* tape mark? */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt) -	/* spc over record */
	    ((f == MTUF_F_STD)? ((sbc + 1) & ~1): sbc);
	fseek_ext (uptr->fileref, uptr->pos + sizeof (t_mtrlnt), SEEK_SET);
	break;

case MTUF_F_TPC:
	ppos = sim_tape_tpc_fnd (uptr, uptr->filebuf);	/* find prev rec */
	fseek_ext (uptr->fileref, ppos, SEEK_SET);	/* position */
	fxread (&tpcbc, sizeof (t_tpclnt), 1, uptr->fileref);
	*bc = tpcbc;					/* save rec lnt */
	if (ferror (uptr->fileref))			/* error? */
	    return sim_tape_ioerr (uptr);
	if (feof (uptr->fileref)) return MTSE_EOM;	/* eof? */
	uptr->pos = ppos;				/* spc over record */
	if (*bc == MTR_TMK) return MTSE_TMK;		/* tape mark? */
	fseek_ext (uptr->fileref, uptr->pos + sizeof (t_tpclnt), SEEK_SET);
	break;
default:
	return MTSE_FMT;  }
return MTSE_OK;
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
t_mtrlnt i, tbc, rbc;
t_addr opos;
t_stat st;

opos = uptr->pos;					/* old position */
if (st = sim_tape_rdlntf (uptr, &tbc)) return st;	/* read rec lnt */
*bc = rbc = MTR_L (tbc);				/* strip error flag */
if (rbc > max) {					/* rec out of range? */
	MT_SET_PNU (uptr);
	uptr->pos = opos;
	return MTSE_INVRL;  }
i = fxread (buf, sizeof (uint8), rbc, uptr->fileref);	/* read record */
if (ferror (uptr->fileref)) {				/* error? */
	MT_SET_PNU (uptr);
	uptr->pos = opos;
	return sim_tape_ioerr (uptr);  }
for ( ; i < rbc; i++) buf[i] = 0;			/* fill with 0's */
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
t_mtrlnt i, rbc, tbc;
t_stat st;

if (st = sim_tape_rdlntr (uptr, &tbc)) return st;	/* read rec lnt */
*bc = rbc = MTR_L (tbc);				/* strip error flag */
if (rbc > max) return MTSE_INVRL;			/* rec out of range? */
i = fxread (buf, sizeof (uint8), rbc, uptr->fileref);	/* read record */
if (ferror (uptr->fileref))				/* error? */
	return sim_tape_ioerr (uptr);
for ( ; i < rbc; i++) buf[i] = 0;			/* fill with 0's */
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
uint32 f = MT_GET_FMT (uptr);
t_mtrlnt sbc;

MT_CLR_PNU (uptr);
if (f == MTUF_F_STD) sbc = (bc + 1) & ~1;
else sbc = bc;
if ((uptr->flags & UNIT_ATT) == 0) return MTSE_UNATT;	/* not attached? */
if (uptr->flags & MTUF_WRP) return MTSE_WRP;		/* write prot? */
fseek_ext (uptr->fileref, uptr->pos, SEEK_SET);		/* set pos */
fxwrite (&bc, sizeof (t_mtrlnt), 1, uptr->fileref);
fxwrite (buf, sizeof (uint8), sbc, uptr->fileref);
fxwrite (&bc, sizeof (t_mtrlnt), 1, uptr->fileref);
if (ferror (uptr->fileref)) {				/* error? */
	MT_SET_PNU (uptr);
	return sim_tape_ioerr (uptr);  }
uptr->pos = uptr->pos + sbc + (2 * sizeof (t_mtrlnt));	/* move tape */
return MTSE_OK;
}

/* Write metadata forward (internal routine) */

t_stat sim_tape_wrdata (UNIT *uptr, uint32 dat)
{
MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0) return MTSE_UNATT;	/* not attached? */
if (uptr-> flags & MTUF_WRP) return MTSE_WRP;		/* write prot? */
fseek_ext (uptr->fileref, uptr->pos, SEEK_SET);		/* set pos */
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
t_stat st;

st = sim_tape_rdlntf (uptr, bc);			/* get record length */
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
	return SCPE_OK;  }
st = sim_tape_rdlntr (uptr, bc);			/* get record length */
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

/* Set tape format */

t_stat sim_tape_set_fmt (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 f;

if (uptr == NULL) return SCPE_IERR;
if (cptr == NULL) return SCPE_ARG;
for (f = 0; f < MTUF_N_FMT; f++) {
	if (fmts[f].name && (strcmp (cptr, fmts[f].name) == 0)) {
	    uptr->flags = (uptr->flags & ~MTUF_FMT) |
		(f << MTUF_V_FMT) | fmts[f].uflags;
	    return SCPE_OK;  }  }
return SCPE_ARG;
}

/* Show tape format */

t_stat sim_tape_show_fmt (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 f = MT_GET_FMT (uptr);

if (fmts[f].name) fprintf (st, "%s format", fmts[f].name);
else fprintf (st, "invalid format");
return SCPE_OK;
}

/* Map a TPC format tape image */

uint32 sim_tape_tpc_map (UNIT *uptr, t_addr *map)
{
t_addr tpos;
t_tpclnt bc;
uint32 i, objc;

if ((uptr == NULL) || (uptr->fileref == NULL)) return 0;
for (objc = 0, tpos = 0;; ) {
	fseek_ext (uptr->fileref, tpos, SEEK_SET);
	i = fxread (&bc, sizeof (t_tpclnt), 1, uptr->fileref);
	if (i == 0) break;
	if (map) map[objc] = tpos;
	objc++;
	tpos = tpos + ((bc + 1) & ~1) + sizeof (t_tpclnt);  }
if (map) map[objc] = tpos;
return objc;
}

/* Find the preceding record in a TPC file */

t_addr sim_tape_tpc_fnd (UNIT *uptr, t_addr *map)
{
uint32 lo, hi, p;


if (map == NULL) return 0;
lo = 0;
hi = uptr->hwmark - 1;
do {	p = (lo + hi) >> 1;
	if (uptr->pos == map[p])
	    return ((p == 0)? map[p]: map[p - 1]);
	else if (uptr->pos < map[p]) hi = p - 1;
	else lo = p + 1;  }
while (lo <= hi);
return ((p == 0)? map[p]: map[p - 1]);
}
