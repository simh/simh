/* sim_tape.h: simulator tape support library definitions

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
*/

#ifndef _SIM_TAPE_H_
#define _SIM_TAPE_H_	0

/* Temporary for use with old sim_defs.h */

#undef MTR_TMK
#undef MTR_EOM
#undef MTR_ERF
#undef MT_SET_PNU
#undef MT_CLR_PNU
#undef MT_TST_PNU

/* Default tape format */

/* typedef uint32		t_mtrlnt;		/* magtape rec lnt */

#define MTR_TMK		0x00000000			/* tape mark */
#define MTR_EOM		0xFFFFFFFF			/* end of medium */
#define MTR_ERF		0x80000000			/* error flag */
#define MTR_F(x)	((x) & MTR_ERF)			/* record error flg */
#define MTR_L(x)	((x) & ~MTR_ERF)		/* record length */

/* Unit flags */

#define MTUF_V_PNU	(UNIT_V_UF + 0)			/* position not upd */
#define MTUF_V_WLK	(UNIT_V_UF + 1)			/* write locked */
#define MTUF_V_FMT	(UNIT_V_UF + 2)			/* tape file format */
#define MTUF_W_FMT	3				/* 3b of formats */
#define MTUF_N_FMT	(1u << MTUF_W_FMT)		/* number of formats */
#define MTUF_M_FMT	((1u << MTUF_W_FMT) - 1)
#define MTUF_F_STD	 0				/* SIMH format */
#define MTUF_F_E11	 1				/* E11 format */
#define MTUF_F_TPC	 2				/* TPC format */
#define MUTF_F_TDF	 3				/* TDF format */
#define MTUF_V_UF	(MTUF_V_FMT + MTUF_W_FMT)
#define MTUF_PNU	(1u << MTUF_V_PNU)
#define MTUF_WLK	(1u << MTUF_V_WLK)
#define MTUF_WRP	(MTUF_WLK | UNIT_RO)

#define MT_SET_PNU(u)	(u)->flags = (u)->flags | MTUF_PNU
#define MT_CLR_PNU(u)	(u)->flags = (u)->flags & ~MTUF_PNU
#define MT_TST_PNU(u)	((u)->flags & MTUF_PNU)
#define MT_GET_FMT(u)	(((u)->flags >> MTUF_V_FMT) & MTUF_M_FMT)

/* Return status codes */

#define MTSE_OK		0				/* no error */
#define MTSE_TMK	1				/* tape mark */
#define MTSE_UNATT	2				/* unattached */
#define MTSE_IOERR	3				/* IO error */
#define MTSE_INVRL	4				/* invalid rec lnt */
#define MTSE_FMT	5				/* invalid format */
#define MTSE_BOT	6				/* beginning of tape */
#define MTSE_EOM	7				/* end of medium */
#define MTSE_RECE	8				/* error in record */
#define MTSE_WRP	9				/* write protected */

/* Prototypes */

t_stat sim_tape_attach (UNIT *uptr, char *cptr);
t_stat sim_tape_detach (UNIT *uptr);
t_stat sim_tape_rdrecf (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max);
t_stat sim_tape_rdrecr (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max);
t_stat sim_tape_wrrecf (UNIT *uptr, uint8 *buf, t_mtrlnt bc);
t_stat sim_tape_wrtmk (UNIT *uptr);
t_stat sim_tape_wreom (UNIT *uptr);
t_stat sim_tape_sprecf (UNIT *uptr, t_mtrlnt *bc);
t_stat sim_tape_sprecr (UNIT *uptr, t_mtrlnt *bc);
t_stat sim_tape_rewind (UNIT *uptr);
t_stat sim_tape_reset (UNIT *uptr);
t_bool sim_tape_bot (UNIT *uptr);
t_bool sim_tape_wrp (UNIT *uptr);
t_bool sim_tape_eot (UNIT *uptr, t_addr cap);

#endif
