/* sage_lp.c: Printer device for sage-II system

   Copyright (c) 2009, Holger Veit

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
   Holger Veit BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Holger Veit et al shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Holger Veit et al.

   04-Oct-09    HV      Initial version
*/

#include "sage_defs.h"

#define UNIT_V_OFFLINE  (UNIT_V_UF + 0)                 /* unit offline */
#define UNIT_OFFLINE    (1 << UNIT_V_OFFLINE)

static t_stat sagelp_reset(DEVICE* dptr);
static t_stat sagelp_attach(UNIT *uptr, char *cptr);
static t_stat sagelp_detach(UNIT *uptr);
static t_stat sagelp_output(UNIT *uptr);
static t_stat u39_reset(I8255* chip);
static t_stat u39_calla(I8255* chip,int rw);
static t_stat u39_callb(I8255* chip,int rw);
static t_stat u39_callc(I8255* chip,int rw);
static t_stat u39_ckmode(I8255* chip,uint32 data);
extern DEVICE sagelp_dev;

/* The LP Centronics device in sage is implemented by a 8255 with the following settings:
 * port A output data
 * port B input status from printer, and from misc devices
 *  B0 Floppy interrupt flag
 *  B1 Floppy write protect flag
 *  B2 Modem ringing indicator
 *  B3 Modem carrier detect
 *  B4 Printer BUSY flag
 *  B5 Printer PAPER flag
 *  B6 Printer SELECT flag (on/offline)
 *  B7 Printer FAULT flag 
 * port C lower half output control for misc devices
 *  C0 Parity error reset
 *  C1 IEEE enable
 *  C2 Interrupt level 7
 *  C3 activity LED
 * port C upper half input status from printers
 *  C4 printer STROBE flag
 *  C5 printer PRIME flag
 *  C6 printer ACK INT clear
 *  C7 modem Ringing/Carrier INT clear (MI)
 */

static I8255 u39 = { 
		{ 0,0,U39_ADDR,8,2},
		&sagelp_dev,
		i8255_write,i8255_read,u39_reset,u39_calla,u39_callb,u39_callc,u39_ckmode
};

UNIT sagelp_unit = {
	UDATA (NULL, UNIT_SEQ|UNIT_ATTABLE|UNIT_TEXT, 0), SERIAL_OUT_WAIT
};

REG sagelp_reg[] = {
	{ HRDATA(PORTA, u39.porta, 8) },
	{ HRDATA(PORTB, u39.portb, 8) },
	{ HRDATA(PORTC, u39.portc, 8) },
	{ HRDATA(CTRL,  u39.ctrl, 8) },
    { GRDATA (BUF, sagelp_unit.buf, 16, 8, 0) },
    { DRDATA (POS, sagelp_unit.pos, T_ADDR_W), PV_LEFT },
	{ NULL }
};

static MTAB sagelp_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "IO", "IO", &set_iobase, &show_iobase, NULL },
    { UNIT_OFFLINE, UNIT_OFFLINE, "offline", "OFFLINE", NULL },
    { UNIT_OFFLINE, 0, "online", "ONLINE", NULL },
	{ 0 }
};

DEBTAB sagelp_dt[] = {
	{ "WRA", DBG_PP_WRA },
	{ "RDB", DBG_PP_RDB },
	{ "RDC", DBG_PP_RDC },
	{ "WRC", DBG_PP_WRC },
	{ "WRMODE", DBG_PP_MODE },
	{ NULL, 0 }
};

DEVICE sagelp_dev = {
	"LP", &sagelp_unit, sagelp_reg, sagelp_mod,
	1, 16, 32, 2, 16, 16,
	NULL, NULL, &sagelp_reset,
	NULL, &sagelp_attach, &sagelp_detach,
	&u39, (DEV_DISABLE|DEV_DEBUG), 0,
	sagelp_dt, NULL, NULL
};

t_stat sagelp_reset(DEVICE* dptr) 
{
	t_stat rc;
    if ((rc = (dptr->flags & DEV_DIS) ? /* Disconnect I/O Ports */
    	del_iohandler(dptr->ctxt) :
   		add_iohandler(&sagelp_unit,dptr->ctxt,i8255_io)) != SCPE_OK) return rc;
    	
	return u39.reset(&u39);
}

/* we don't accept any mode and combination that a 8255 can do, because
 * u39 is hardwired to porta=output, portb=input and portc=output
 */

static t_stat u39_calla(I8255* chip, int rw)
{
	if (rw) {
		sagelp_unit.buf = chip->porta;
	    TRACE_PRINT1(DBG_PP_WRA,"WR PortA = 0x%x",chip->porta);
	}
	return SCPE_OK;
}

static t_stat u39_callb(I8255* chip, int rw)
{
	if (rw==0) { /* only when reading port */
		/* propagate FDC Write Protect */
		int portb = 0;
		I8272_DRIVE_INFO* dip = &u21.drive[u21.fdc_curdrv];
		if (dip->uptr && (dip->uptr->flags & UNIT_I8272_WLK)) {
			portb |= U39B_WP;
		    TRACE_PRINT1(DBG_PP_RDB,"RD PortB: WP+=%d",(portb&U39B_WP)?1:0);
		}
			
		/* propagate FDC interrupt */
		if (u21.irqflag) {
			portb |= U39B_FDI;
		    TRACE_PRINT0(DBG_PP_RDB,"RD PortB: FDI+=1");
		} else {
		    TRACE_PRINT0(DBG_PP_RDB,"RD PortB: FDI+=0");
		}
		chip->portb = portb;
	}
	return SCPE_OK;
}

static t_stat u39_callc(I8255* chip,int rw)
{
	if (rw==1) {
		if (I8255_FALLEDGE(portc,U39C_STROBE)) {
			sagelp_output(&sagelp_unit);
			TRACE_PRINT1(DBG_PP_RDC,"RD PortC: STROBE-=%d",chip->portc&U39C_STROBE?1:0);
		}
		if (I8255_RISEEDGE(portc,U39C_SI)) {
/*			printf("rising edge on SI: PC=%x!\n",PCX);*/
			TRACE_PRINT1(DBG_PP_RDC,"RD PortC: SI+=%d",chip->portc&U39C_SI?1:0);
			sage_raiseint(SI_PICINT);
		}
	}
	return SCPE_OK;
}

static t_stat u39_ckmode(I8255* chip,uint32 data)
{
	TRACE_PRINT1(DBG_PP_MODE,"WR Mode: 0x%x",data);
	
	/* BIOS initializes port A as input, later LP is initialized to output */
	if (!(data==0x82 || data==0x92)) {
		/* hardwired:
		 * d7=1 -- mode set flag
		 * d6=0 -+ group a mode 0: basic I/O
		 * d5=0 -+
		 * d4=0 -- port a = output / input
		 * d3=0 -- port c upper = output
		 * d2=0 -- group b mode 0: basic I/O
		 * d1=1 -- port b = input
		 * d0=0 -- port c lower = output
		 */
		printf("u39_ckmode: unsupported ctrl=0x%02x\n",data);
		return STOP_IMPL;
	}
	chip->portc = 0; /* reset port */
	return SCPE_OK;
}

static t_stat u39_reset(I8255* chip)
{
	sagelp_unit.buf = 0;
   	sim_cancel (&sagelp_unit);
   	return SCPE_OK;
}

static t_stat sagelp_attach (UNIT *uptr, char *cptr)
{
	t_stat rc;
	rc = attach_unit(uptr, cptr);
	if ((sagelp_unit.flags & UNIT_ATT) == 0)
		u39.portb |= U39B_PAPER;	/* no paper */
		
	return rc;
}

static t_stat sagelp_detach (UNIT *uptr)
{
	u39.portb |= U39B_PAPER;	/* no paper */
	return detach_unit (uptr);
}

static t_stat sagelp_output(UNIT *uptr)
{
	if ((uptr->flags & UNIT_ATT)==0) {
		u39.portb |= U39B_PAPER;	/* unattached means: no paper */
		return SCPE_UNATT;
	} else if (uptr->flags & UNIT_OFFLINE) {
		u39.portb &= ~U39B_SEL;		/* offline means: SEL = 0 */ 
		return STOP_OFFLINE;
	}
	u39.portb &= ~U39B_PAPER;		/* has paper */
	u39.portb |= U39B_SEL;			/* is online */
	u39.portb |= U39B_FAULT;		/* no fault */
	u39.portb &= ~U39B_BUSY;			/* not busy */
	if ((u39.portc & U39C_STROBE)==0) {	/* strobe presented */
		fputc (uptr->buf & 0177, uptr->fileref);	/* put out char */
		if (ferror (uptr->fileref)) {
		    perror ("LP I/O error");
		    clearerr (uptr->fileref);
		    return SCPE_IOERR;
	    }
		sagelp_unit.pos = ftell(uptr->fileref);	/* update pos */
		u39.portc |= U39C_STROBE;	/* XXX reset strobe directly */
		sage_raiseint(LP_PICINT);
		return SCPE_OK;
	}
	return SCPE_OK;
}
