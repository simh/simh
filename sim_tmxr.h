/* sim_tmxr.h: terminal multiplexor definitions

   Copyright (c) 2001, Robert M Supnik

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

   Based on the original DZ11 simulator by Thord Nilson, as updated by
   Arthur Krewat.

   20-Oct-01	RMS	Removed tmxr_getchar, formalized buffer guard,
			added tmxr_rqln, tmxr_tqln
*/

#define TMXR_V_VALID	15
#define TMXR_VALID	(1 << TMXR_V_VALID)
#define TMXR_MAXBUF	128				/* buffer size */
#define TMXR_GUARD	8				/* buffer guard */
#define TMXR_MAXLIN	64				/* max lines */

struct tmln {
	SOCKET		conn;				/* line conn */
	uint32		ipad;				/* IP address */
	uint32		cnms;				/* conn time */
	int32		tsta;				/* Telnet state */
	int32		rcve;				/* rcv enable */
	int32		xmte;				/* xmt enable */
	int32		dstb;				/* disable Tlnt bin */
	int32		rxbpr;				/* rcv buf remove */
	int32		rxbpi;				/* rcv buf insert */
	int32		rxcnt;				/* rcv count */
	int32		txbpr;				/* xmt buf remove */
	int32		txbpi;				/* xmt buf insert */
	int32		txcnt;				/* xmt count */
	char		rxb[TMXR_MAXBUF];		/* rcv buffer */
	char		txb[TMXR_MAXBUF];		/* xmt buffer */
	};

typedef struct tmln TMLN;

struct tmxr {
	int32		lines;				/* # lines */
	SOCKET		master;				/* master socket */
	TMLN		*ldsc[TMXR_MAXLIN];		/* line descriptors */
	};

typedef struct tmxr TMXR;

int32 tmxr_poll_conn (TMXR *mp, UNIT *uptr);
void tmxr_reset_ln (TMLN *lp);
int32 tmxr_getc_ln (TMLN *lp);
void tmxr_poll_rx (TMXR *mp);
void tmxr_putc_ln (TMLN *lp, int32 chr);
void tmxr_poll_tx (TMXR *mp);
t_stat tmxr_attach (TMXR *mp, UNIT *uptr, char *cptr);
t_stat tmxr_detach (TMXR *mp, UNIT *uptr);
t_stat tmxr_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat tmxr_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
void tmxr_msg (SOCKET sock, char *msg);
void tmxr_fstatus (FILE *st, TMLN *lp, int32 ln);
int32 tmxr_rqln (TMLN *lp);
int32 tmxr_tqln (TMLN *lp);

