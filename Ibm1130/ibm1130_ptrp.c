/* ibm1130_ptrp.c: IBM 1130 paper tape reader/punch emulation

   Based on the SIMH simulator package written by Robert M Supnik

   Brian Knittel
   Revision History

   2004.10.22 - Written.

 * (C) Copyright 2004, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to simh@ibm1130.org
 */

#include "ibm1130_defs.h"

/***************************************************************************************
 *  1134 Paper Tape Reader	device PTR
 *  1055 Paper Tape Punch	device PTP  (shares DSW with PTR)
 ***************************************************************************************/

#define PTR1134_DSW_READER_RESPONSE				0x4000
#define PTR1134_DSW_PUNCH_RESPONSE				0x1000
#define PTR1134_DSW_READER_BUSY					0x0800
#define PTR1134_DSW_READER_NOT_READY			0x0400
#define PTR1134_DSW_PUNCH_BUSY					0x0200
#define PTR1134_DSW_PUNCH_NOT_READY				0x0100

#define IS_ONLINE(u) (((u)->flags & (UNIT_ATT|UNIT_DIS)) == UNIT_ATT)

static t_stat ptr_svc    (UNIT *uptr);
static t_stat ptr_reset  (DEVICE *dptr);
static t_stat ptr_attach (UNIT *uptr, CONST char *cptr);
static t_stat ptr_detach (UNIT *uptr);
static t_stat ptr_boot	 (int32 unitno, DEVICE *dptr);
static t_stat ptp_svc    (UNIT *uptr);
static t_stat ptp_reset  (DEVICE *dptr);
static t_stat ptp_attach (UNIT *uptr, CONST char *cptr);
static t_stat ptp_detach (UNIT *uptr);

static int16 ptr_dsw   = 0;								/* device status word */
static int32 ptr_wait  = 1000;							/* character read wait */
static uint8 ptr_char  = 0;								/* last character read */
static int32 ptp_wait  = 1000;							/* character punch wait */

UNIT ptr_unit[1] = {
	{ UDATA (&ptr_svc, UNIT_ATTABLE, 0) },
};

REG ptr_reg[] = {
	{ HRDATA (DSW, 	    ptr_dsw,  16) },				/* device status word */
	{ DRDATA (WTIME,    ptr_wait, 24), PV_LEFT },		/* character read wait */
	{ DRDATA (LASTCHAR, ptr_char,  8), PV_LEFT },		/* last character read */
	{ NULL }  };

DEVICE ptr_dev = {
	"PTR", ptr_unit, ptr_reg, NULL,
	1, 16, 16, 1, 16, 16,
	NULL, NULL, ptr_reset,
	ptr_boot, ptr_attach, ptr_detach};

UNIT ptp_unit[1] = {
	{ UDATA (&ptp_svc, UNIT_ATTABLE, 0) },
};

REG ptp_reg[] = {
	{ HRDATA (DSW, 	  ptr_dsw,  16) },					/* device status word (this is the same as the reader's!) */
	{ DRDATA (WTIME,  ptp_wait, 24), PV_LEFT },			/* character punch wait */
	{ NULL }  };

DEVICE ptp_dev = {
	"PTP", ptp_unit, ptp_reg, NULL,
	1, 16, 16, 1, 16, 16,
	NULL, NULL, ptp_reset,
	NULL, ptp_attach, ptp_detach};

/* xio_1134_papertape - XIO command interpreter for the 1134 paper tape reader and 1055 paper tape punch */

void xio_1134_papertape (int32 iocc_addr, int32 iocc_func, int32 iocc_mod)
{
	char msg[80];

	switch (iocc_func) {
		case XIO_READ:											/* read: return last character read */
			M[iocc_addr & mem_mask] = (uint16) (ptr_char << 8);
			break;

		case XIO_WRITE:											/* write: initiate punch operation */
			if ((ptr_dsw & PTR1134_DSW_PUNCH_NOT_READY) == 0 && IS_ONLINE(ptp_unit)) {
				putc((M[iocc_addr & mem_mask] >> 8) & 0xFF, ptp_unit->fileref);
				ptp_unit->pos++;
			}
			sim_activate(ptp_unit, ptp_wait);					/* schedule interrupt */
			SETBIT(ptr_dsw, PTR1134_DSW_PUNCH_NOT_READY | PTR1134_DSW_PUNCH_BUSY);
			break;

		case XIO_SENSE_DEV:										/* sense device status */
			ACC = ptr_dsw;
			if (iocc_mod & 0x01) {								/* reset interrupts */
				CLRBIT(ptr_dsw, PTR1134_DSW_READER_RESPONSE | PTR1134_DSW_PUNCH_RESPONSE);
				CLRBIT(ILSW[4], ILSW_4_1134_TAPE);
			}
			break;

		case XIO_CONTROL:										/* control: initiate character read */
			sim_activate(ptr_unit, ptr_wait);					/* schedule interrupt */
			SETBIT(ptr_dsw, PTR1134_DSW_READER_BUSY | PTR1134_DSW_READER_NOT_READY);
			break;

		default:
			sprintf(msg, "Invalid 1134 reader/1055 punch XIO function %x", iocc_func);
			xio_error(msg);
	}
}

/* ptr_svc - emulated timeout - 1134 read operation complete */

static t_stat ptr_svc (UNIT *uptr)
{
	CLRBIT(ptr_dsw, PTR1134_DSW_READER_BUSY);				/* clear reader busy flag */
	SETBIT(ptr_dsw, PTR1134_DSW_READER_NOT_READY);			/* assume at end of file */

	if (IS_ONLINE(uptr)) {									/* fetch character from file */
		ptr_char = getc(uptr->fileref);
		uptr->pos++;

		if (! feof(uptr->fileref))							/* there's more left */
			CLRBIT(ptr_dsw, PTR1134_DSW_READER_NOT_READY);
	}

	SETBIT(ptr_dsw, PTR1134_DSW_READER_RESPONSE);			/* indicate read complete */

	SETBIT(ILSW[4], ILSW_4_1134_TAPE);						/* initiate interrupt */
	calc_ints();

	return SCPE_OK;
}

/* ptp_svc - emulated timeout -- 1055 punch operation complete */

static t_stat ptp_svc (UNIT *uptr)
{
	CLRBIT(ptr_dsw, PTR1134_DSW_PUNCH_BUSY);				/* clear punch busy flag */

	if (IS_ONLINE(uptr))									/* update punch ready status */
		CLRBIT(ptr_dsw, PTR1134_DSW_PUNCH_NOT_READY);
	else
		SETBIT(ptr_dsw, PTR1134_DSW_PUNCH_NOT_READY);
	
	SETBIT(ptr_dsw, PTR1134_DSW_PUNCH_RESPONSE);			/* indicate punch complete */

	SETBIT(ILSW[4], ILSW_4_1134_TAPE);						/* initiate interrupt */
	calc_ints();

	return SCPE_OK;
}

/* ptr_reset - reset emulated paper tape reader */

static t_stat ptr_reset (DEVICE *dptr)
{
	sim_cancel(ptr_unit);

	CLRBIT(ptr_dsw, PTR1134_DSW_READER_BUSY | PTR1134_DSW_READER_RESPONSE);
	SETBIT(ptr_dsw, PTR1134_DSW_READER_NOT_READY);

	if (IS_ONLINE(ptr_unit) && ! feof(ptr_unit->fileref))
		CLRBIT(ptr_dsw, PTR1134_DSW_READER_NOT_READY);

	if ((ptr_dsw & PTR1134_DSW_PUNCH_RESPONSE) == 0) {		/* punch isn't interrupting either */
		CLRBIT(ILSW[4], ILSW_4_1134_TAPE);
		calc_ints();
	}

	return SCPE_OK;
}

/* ptp_reset - reset emulated paper tape punch */

static t_stat ptp_reset (DEVICE *dptr)
{
	sim_cancel(ptp_unit);

	CLRBIT(ptr_dsw, PTR1134_DSW_PUNCH_BUSY | PTR1134_DSW_PUNCH_RESPONSE);
	SETBIT(ptr_dsw, PTR1134_DSW_PUNCH_NOT_READY);

	if (IS_ONLINE(ptp_unit))
		CLRBIT(ptr_dsw, PTR1134_DSW_PUNCH_NOT_READY);

	if ((ptr_dsw & PTR1134_DSW_READER_RESPONSE) == 0) {		/* reader isn't interrupting either */
		CLRBIT(ILSW[4], ILSW_4_1134_TAPE);
		calc_ints();
	}

	return SCPE_OK;
}

/* ptr_attach - attach file to simulated paper tape reader */

static t_stat ptr_attach (UNIT *uptr, CONST char *cptr)
{
	t_stat rval;

	SETBIT(ptr_dsw, PTR1134_DSW_READER_NOT_READY);			/* assume failure */

	if ((rval = attach_unit(uptr, cptr)) != SCPE_OK)		/* use standard attach */
		return rval;

	if ((ptr_dsw & PTR1134_DSW_READER_BUSY) == 0 && ! feof(uptr->fileref))
		CLRBIT(ptr_dsw, PTR1134_DSW_READER_NOT_READY);		/* we're in business */

	return SCPE_OK;
}

/* ptr_attach - detach file from simulated paper tape reader */

static t_stat ptr_detach (UNIT *uptr)
{
	SETBIT(ptr_dsw, PTR1134_DSW_READER_NOT_READY);

	return detach_unit(uptr);
}

/* ptr_attach - perform paper tape initial program load */

static t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
	int ch, nch, val, addr;
	t_bool leader = TRUE, start = FALSE;
	t_stat rval;

	addr = 0;
	nch  = 0;
	val  = 0;

	for (;;) {
		if ((ch = getc(ptr_unit->fileref)) == EOF) {
			printf("EOF on paper tape without finding Channel 5 end-of-load mark\n");
			break;
		}

		if (leader) {
			if ((ch & 0x7F) == 0x7F)			/* ignore leading rubouts or "delete" characters */
				continue;

			leader = FALSE;						/* after first nonrubout, any punch in channel 5 terminates load */
		}

		/* this is untested -- not sure of actual byte ordering  */

		val = (val << 4) | (ch & 0x0F);			/* get next nybble */

		if (++nch == 4) {						/* if we now have four nybbles, store the word */
			M[addr & mem_mask] = (uint16) val;

			addr++;								/* prepare for next word */
			nch = 0;
			val = 0;
		}

		if (ch & 0x10) {						/* channel 5 punch terminates load */
			start = TRUE;
			break;
		}
	}

	if (! start)								/* if we didn't get a valid load, report EOF error */
		return SCPE_EOF;

	if ((rval = reset_all(0)) != SCPE_OK)		/* force a reset */
		return rval;

	IAR = 0;									/* start running at address 0 */
	return SCPE_OK;
}

/* ptp_attach - attach file to simulated paper tape punch */

static t_stat ptp_attach (UNIT *uptr, CONST char *cptr)
{
	t_stat rval;

	SETBIT(ptr_dsw, PTR1134_DSW_PUNCH_NOT_READY);			/* assume failure */

	if ((rval = attach_unit(uptr, cptr)) != SCPE_OK)		/* use standard attach */
		return rval;

	fseek(uptr->fileref, 0, SEEK_END);						/* if we opened an existing file, append to it */
	uptr->pos = ftell(uptr->fileref);

	if ((ptr_dsw & PTR1134_DSW_PUNCH_BUSY) == 0)
		CLRBIT(ptr_dsw, PTR1134_DSW_PUNCH_NOT_READY);		/* we're in business */

	return SCPE_OK;
}

/* ptp_detach - detach file from simulated paper tape punch */

static t_stat ptp_detach (UNIT *uptr)
{
	SETBIT(ptr_dsw, PTR1134_DSW_PUNCH_NOT_READY);

	return detach_unit(uptr);
}
