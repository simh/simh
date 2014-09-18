/* sage_stddev.c: Standard devices for sage-II system

   Copyright (c) 2009-2010 Holger Veit

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

#include "sim_defs.h"
#include "m68k_cpu.h"
#include "sage_defs.h"

/***********************************************************************************
 * 8259-5 interrupt controller
 * 
 * IRQ output hardwired to Interrupt Priority Level 1 in the Sage
 * Level 2: from external bus (wired to HDC board, AUX devices)
 * Level 3: from external bus
 * Level 4: IEEE 488 Interrupt U6
 * Level 5: Console Uart U67 Receiver Interrupt
 * Level 6: FDI floppy controller
 * Level 7: nonmaskable RAM parity error (not possible in simh)
 * 
 * hardwired inputs:
 * IR0 = Output 2 of U74 real time clock
 * IR1 = Modem Uart U58 Receiver Interrupt
 * IR2 = Console Uart U67 Transmitter Interrupt
 * IR3 = Modem Uart U58 Receiver Interrupt
 * IR4 = Modem Carrier Detect Interrupt U38
 * IR5 = LP Port Acknowledge U39/U38
 * IR6 = Output 0 of U74 real time clock
 * IR7 = Output C2 of U39
 *  
 * Notes: 
 * 	INTA- is hardwired to VCC, so vectoring is not possible
 * 	SP- is hardwired to VCC, so buffered mode is not possible, and device is a master.
 *  CAS0-2 lines are open, no need to handle
 *  UCSD bios and boot prom do not program the PIC for rotating priorities,
 *  so effectively prio is always 7.
 * 
 **********************************************************************************/
extern DEVICE sagepic_dev;
static t_stat sagepic_reset(DEVICE* dptr);
static I8259 u73 = { {0,0,U73_ADDR,4,2},
			&sagepic_dev,NULL,NULL,i8259_reset
};

UNIT sagepic_unit = {
	UDATA (NULL, UNIT_IDLE, 0)
};

REG sagepic_reg[] = {
	{ DRDATA(STATE, u73.state, 8) },
	{ HRDATA(IRR, 	u73.irr, 8) },
	{ HRDATA(IMR, 	u73.imr, 8) },
	{ HRDATA(ISR, 	u73.isr, 8) },
	{ HRDATA(ICW1,  u73.icw1, 8) },
	{ HRDATA(ICW2,  u73.icw2, 8) },
	{ HRDATA(ICW4,  u73.icw4, 8) },
	{ HRDATA(OCW2,  u73.prio, 3) },
	{ NULL }
};

static MTAB sagepic_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "IO", "IO", &set_iobase, &show_iobase, NULL },
	{ 0 }
};

DEVICE sagepic_dev = {
	"PIC", &sagepic_unit, sagepic_reg, sagepic_mod,
	1, 16, 32, 2, 16, 16,
	NULL, NULL, &sagepic_reset,
	NULL, NULL, NULL,
	&u73, DEV_DEBUG, 0,
	i8259_dt, NULL, NULL
};

static t_stat sagepic_reset(DEVICE* dptr) 
{
	t_stat rc;
	if ((rc = (dptr->flags & DEV_DIS) ?
		del_iohandler(dptr->ctxt) :
		add_iohandler(&sagepic_unit,dptr->ctxt,i8259_io)) != SCPE_OK) return rc;
	return u73.reset(&u73);
}

t_stat sage_raiseint(int level)
{
	return i8259_raiseint(&u73,level);
}

/******************************************************************************************************
 * DIP switches at the back panel.
 * 
 * In the technical manual, switches are layed out 12345678 left to right,
 * but here seen as two HEX digits 8765 4321, i.e. 0xc0 is bit 8 and bit 7 set on
 * 
 * a "d" (down) means switch is off or "0", and a "u" (up) means switch is on or "1"
 * 
 * Note that programatically dip switches are port a and b of the onboard 8255 U22
 * which also through port c serves part of the FDC signals
 * 
 * group-a:
 * 8 7 6 5 4 3 2 1
 * | | | | | d d d--- 19,2K baud
 * | | | | | d d u--- 9600 baud
 * | | | | | d u d--- 4800 baud
 * | | | | | d u u--- 2400 baud
 * | | | | | u d d--- 1200 baud
 * | | | | | u d u--- 600 baud
 * | | | | | u u d--- 300 baud
 * | | | | | u u u--- reserved
 * | | | | d--------- even parity
 * | | | | u--------- parity disabled
 * | | d d----------- boot to debugger
 * | | d u----------- boot to floppy 0
 * | | u d----------- boot to harddisk 0 partition 0
 * | | u u----------- reserved
 * | d--------------- 96 tpi drive
 * | u--------------- 48 tpi drive
 * x----------------- reserved
 * 
 * group-b:
 * 8 7 6 5 4 3 2 1
 * | | | +-+-+-+-+--- device talk and listen address
 * | | u------------- enable talk
 * | | d------------- disable talk
 * | u--------------- enable listen
 * | d--------------- disable listen
 * u----------------- 2 consecutive addresses
 * d----------------- 1 address
 */

#if defined(SAGE_IV)
       uint32 groupa = 0xd7;	/* used by cons device, 19k2, no parity, boot floppy 0 */
       uint32 groupb = 0xf8;	/* used by ieee device */
#else
       uint32 groupa = 0xe7;	/* used by cons device, 19k2, no parity, boot winchester 0 */
       uint32 groupb = 0xf8;	/* used by ieee device */
#endif
       
static t_stat sagedip_reset(DEVICE* dptr);
static t_stat set_groupa(UNIT *uptr, int32 val, char *cptr, void *desc);
static t_stat show_groupa(FILE *st, UNIT *uptr, int32 val, void *desc);
static t_stat set_groupb(UNIT *uptr, int32 val, char *cptr, void *desc);
static t_stat show_groupb(FILE *st, UNIT *uptr, int32 val, void *desc);
static t_stat u22_reset(I8255* chip);
static t_stat u22_calla(I8255* chip,int rw);
static t_stat u22_callb(I8255* chip,int rw);
static t_stat u22_callc(I8255* chip,int rw);
static t_stat u22_ckmode(I8255* chip,uint32 data);

extern DEVICE sagedip_dev;
static I8255 u22 = { 
		{ 0,0,U22_ADDR,8,2 }, 
		&sagedip_dev,i8255_write,i8255_read,u22_reset,u22_calla,u22_callb,u22_callc,u22_ckmode
};
uint32* u22_portc = &u22.portc; /* this is used in the FD device as well, but whole 8255 is handled here */

UNIT sagedip_unit = {
	UDATA (NULL, UNIT_IDLE, 0)
};

REG sagedip_reg[] = {
	{ HRDATA(PORTA, u22.porta, 8) },
	{ HRDATA(PORTB, u22.portb, 8) },
	{ HRDATA(PORTC, u22.portc, 8) },
	{ HRDATA(CTRL,  u22.ctrl, 8) },
	{ NULL }
};

static MTAB sagedip_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,  "IO",       "IO",       &set_iobase, &show_iobase, NULL },
    { MTAB_XTD|MTAB_VDV,    0,  "GROUPA",   "GROUPA",   &set_groupa, &show_groupa, NULL },
    { MTAB_XTD|MTAB_VDV,    0,  "GROUPB",   "GROUPB",   &set_groupb, &show_groupb, NULL },
	{ 0 }
};

/* Debug Flags */
DEBTAB sagedip_dt[] = {
	{ "RDA", DBG_PP_RDA },
	{ "RDB", DBG_PP_RDB },
	{ "WRC", DBG_PP_WRC },
	{ "WRMODE", DBG_PP_MODE },
	{ NULL, 0 }
};

DEVICE sagedip_dev = {
	"DIP", &sagedip_unit, sagedip_reg, sagedip_mod,
	1, 16, 32, 2, 16, 16,
	NULL, NULL, &sagedip_reset,
	NULL, NULL, NULL,
	&u22, DEV_DEBUG, 0,
	sagedip_dt, NULL, NULL
};

static t_stat sagedip_reset(DEVICE* dptr) 
{
	t_stat rc;
	
	if ((rc = (dptr->flags & DEV_DIS) ? /* Disconnect I/O Ports */
    	del_iohandler(dptr->ctxt) :
   		add_iohandler(&sagedip_unit,dptr->ctxt,i8255_io)) != SCPE_OK) return rc;
	
	/* clear 8255 ctrl register */
	return u22.reset(&u22);
}

static t_stat set_gr(char* cptr, uint32* sw)
{
	int i;
	char c;
	
	if (!cptr) return SCPE_ARG;
	
	*sw = 0;
	for (i=0; *cptr && i<8; i++) {
		c = *cptr++;
		*sw <<= 1;
		if (c=='1') *sw |= 1;
		else if (c=='0') continue;
		else if (c==0) break;
		else return SCPE_ARG;
	}
	return SCPE_OK;
}

static t_stat set_groupa(UNIT *uptr, int32 val, char *cptr, void *desc)
{
	return set_gr(cptr,&groupa);
}

static t_stat set_groupb(UNIT *uptr, int32 val, char *cptr, void *desc)
{
	return set_gr(cptr,&groupb);
}

static t_stat show_gr(FILE* st, char* prefix, uint32 gr)
{
	int i;
	fputs(prefix, st);
	for (i = 0x80; i > 0; i = i >> 1)
		fprintf(st,"%c", gr&i ? '1' : '0');
	return SCPE_OK;
}

static t_stat show_groupa(FILE *st, UNIT *uptr, int32 val, void *desc)
{
	return show_gr(st, "GROUPA=", groupa);
}

static t_stat show_groupb(FILE *st, UNIT *uptr, int32 val, void *desc)
{
	return show_gr(st, "GROUPB=", groupb);
}

static t_stat u22_reset(I8255* chip)
{
	chip->ctrl = 0;
	chip->portc = 0;
	return SCPE_OK;
}

extern I8272 u21;

static t_stat u22_calla(I8255* chip,int rw)
{
	if (rw==0) {
		chip->porta = groupa & 0xff;
		TRACE_PRINT1(DBG_PP_RDA,"WR PortA: 0x%x",groupa);
	}
	return SCPE_OK;
}

static t_stat u22_callb(I8255* chip,int rw)
{
	if (rw==0) {
		chip->portb = groupb & 0xff;
		TRACE_PRINT1(DBG_PP_RDA,"WR PortB: 0x%x",groupb);
	}
	return SCPE_OK;
}

/* callback handler for FDC bits */
static t_stat u22_callc(I8255* chip,int rw)
{
	/* bit0: TC+ positive enforce that internal data counter of FDC is reset
	 * bit1: RDY+ positive enable the FDC
	 * bit2: FDIE+ positive enable FDC interrupt (handled directly by reading portc in sage_fd.c)
	 * bit3: SL0- negative select of drive 0
	 * bit4: SL1- negative select of drive 1
	 * bit5: MOT- negative switch on drive motor (ignored)
	 * bit6: PCRMP- negative precompensation (ignored)
	 * bit7: FRES+ positive FDC reset
	 */

	if (I8255_ISSET(portc,U22C_TC)) { /* TC+ */
		i8272_finish(&u21); /* terminate a read/write in progress */
	}
	if (I8255_ISCLR(portc,U22C_RDY)) { /* RDY+ */
		i8272_abortio(&u21); /* abort current op */
	}
	if (I8255_ISCLR(portc,U22C_SL0)) { /* SL0- */
		u21.fdc_curdrv = 0;
	} else if (I8255_ISCLR(portc,U22C_SL1)) { /* SL1- */
		u21.fdc_curdrv = 1;
	} else if (I8255_ISSET(portc,U22C_SL0|U22C_SL1)) { /* deselect drives */
		u21.fdc_curdrv = 0;
	}
	if (I8255_ISSET(portc,U22C_FRES)) { /* FRES+ */
		i8272_reset(&u21);
	}
	TRACE_PRINT(DBG_PP_WRC,(sim_deb,"PORTC Flags: %s%s%s%s%s%s%s%s",
			I8255_ISSET(portc,U22C_TC)?"TC ":"",
			I8255_ISSET(portc,U22C_RDY)?"RDY ":"",
			I8255_ISSET(portc,U22C_FDIE)?"FDIE ":"",
			I8255_ISSET(portc,U22C_SL0)?"":"SL0 ",
			I8255_ISSET(portc,U22C_SL1)?"":"SL1 ",
			I8255_ISSET(portc,U22C_MOT)?"":"MOT ",
			I8255_ISSET(portc,U22C_PCRMP)?"":"PCRMP ",
			I8255_ISSET(portc,U22C_FRES)?"FRES ":""));
	return SCPE_OK;
}

static t_stat u22_ckmode(I8255* chip, uint32 data)
{
	/* hardwired:
	 * d7=1 -- mode set flag
	 * d6=0 -+ group a mode 0: basic I/O
	 * d5=0 -+
	 * d4=1 -- porta = input
	 * d3=0 -- portc upper = output
	 * d2=0 -- group b mode 0: basic I/O
	 * d1=1 -- portb = input
	 * d0=0 -- portc lower = output
	 */ 
	TRACE_PRINT1(DBG_PP_MODE,"WR Mode: 0x%x",data);
	if (data != 0x92) {
		printf("u22_ckmode: unsupported ctrl=0x%02x\n",data);
		return STOP_IMPL;
	}
	return SCPE_OK;
}

/***********************************************************************************
 * Two 8553 timers U75 (TIMER1) and U74 (TIMER2)
 * Each contain three 8/16 bit timers
 * In the sage hardwired in the following way:
 *
 *            +---------+
 * 615kHz--+->|Timer1 C1|---> Baud ser0
 *         |  +---------+
 *         +->|Timer1 C2|---> Baud ser1
 *            +---------+
 *            +---------+    +---------+
 * 64kHz---+->|Timer1 C0|--->|Timer2 C0|--> PIC IR6   
 *         |  |div 64000|    |mode0    |
 *         |  +---------+    +---------+
 *         |  +---------+    +---------+
 *         +->|Timer2 C1|--->|Timer2 C2|--> PIC IR0  
 *            |         |    |         |
 *            +---------+    +---------+
 * 
 * NOT UNITS: Timer1 C1 and C2 are programmed in mode 2 as clock dividers for the USARTs
 * In this emulation we allow programming them, but since they don't produce interrupts,
 * their work is ignored.
 * 
 * Timer1 C0 and timer2 C0 form a clock divider which produces an interrupt at PIC level 6
 * Likewise, timer2 C1 and timer2 C2 form a clock divider which produces an interrupt at PIC level 0
 * 
 * Typically, the first one in cascade is programmed in mode 2, the second one in mode 0.
 * Timer1 C0 is explicitly programmed as a divider by 64k, so that it feeds timer2 C0 
 * with a 1Hz clock.
 * 
 * The way the timers are hardwired makes certain mode settings impossible: all GATE
 * inputs are set to VCC, so MODE1 and MODE5 are impossible, and MODE4 becomes a
 * variant of MODE0. MODE3 is used by the baudrate generators. The timers may run in 
 * 8 bit mode, but analysis of existing BIOS code (BOOT PROM and UCSD BIOS) uncovered
 * they are used in 16 bit mode only.
 * So, this implementation only contains the most likely usages, and the other ones have
 * to be added when there is a necessity.
 *
 * Notes on actual implementation:
 * Since we know the input clocks, we have just to take care about the division factors
 * stored in T1C0 and T2C1. Whenever one of these timers are read out, the actual count
 * has to be calculated on the fly. The actual cnt registers only hold the count factors
 * programmed, but are never counted down, as, in the case of the 64kHz clock this would
 * mean to trigger sim_* events 64000 times a second.
 *
 ***********************************************************************************/

/************************************************************************************
 *  timer 1
 ***********************************************************************************/
static t_stat sagetimer1_reset(DEVICE* dptr);
static t_stat timer1_svc(UNIT* uptr);
static t_stat u75_ckmode(I8253* chip, uint32 data);
static t_stat u75_call0(I8253* chip,int addr, uint32* value);

static t_stat sagetimer2_reset(DEVICE* dptr);
static t_stat timer2_svc(UNIT* uptr);
static t_stat u74_ckmode(I8253* chip, uint32 data);
static t_stat u74_call1(I8253* chip,int addr, uint32* value);

extern DEVICE sagetimer1_dev;
extern DEVICE sagetimer2_dev;

/* forward timer 2 */
UNIT sagetimer2_unit = {
	UDATA (&timer2_svc, UNIT_IDLE, 0)
};

static I8253 u74 = { {0,0,U74_ADDR,8,2},
	&sagetimer2_dev,&sagetimer2_unit,i8253_reset,u74_ckmode,
	{ { 0, }, { u74_call1, }, { 0, } } 
};

/* timer 1 */
UNIT sagetimer1_unit = {
	UDATA (&timer1_svc, UNIT_IDLE, 1)
};

static I8253 u75 = { {0,0,U75_ADDR,8,2},
	&sagetimer1_dev,&sagetimer1_unit,i8253_reset,u75_ckmode,
	{ { u75_call0, }, { 0, }, { 0, } } 
};

REG sagetimer1_reg[] = {
	{ HRDATA(INIT,  u75.init, 8), REG_HRO },
	{ HRDATA(STATE0,u75.cntr[0].state, 8),REG_HRO },
	{ HRDATA(STATE1,u75.cntr[1].state, 8),REG_HRO },
	{ HRDATA(STATE2,u75.cntr[2].state, 8),REG_HRO },
	{ HRDATA(MODE0, u75.cntr[0].mode, 8) },
	{ HRDATA(MODE1, u75.cntr[1].mode, 8) },
	{ HRDATA(MODE2, u75.cntr[2].mode, 8) },
	{ HRDATA(CNT0,  u75.cntr[0].count, 16) },
	{ HRDATA(CNT1,  u75.cntr[1].count, 16) },
	{ HRDATA(CNT2,  u75.cntr[2].count, 16) },
	{ HRDATA(LATCH0,u75.cntr[0].latch, 16) },
	{ HRDATA(LATCH1,u75.cntr[1].latch, 16) },
	{ HRDATA(LATCH2,u75.cntr[2].latch, 16) },
	{ HRDATA(DIV0,  u75.cntr[0].divider, 16),REG_HRO },
	{ HRDATA(DIV1,  u75.cntr[1].divider, 16),REG_HRO },
	{ HRDATA(DIV2,  u75.cntr[2].divider, 16),REG_HRO },
	{ NULL }
};

static MTAB sagetimer1_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,  "IO", "IO", &set_iobase, &show_iobase, NULL },
	{ 0 }
};

DEVICE sagetimer1_dev = {
	"TIMER1", &sagetimer1_unit, sagetimer1_reg, sagetimer1_mod,
	1, 16, 32, 2, 16, 16,
	NULL, NULL, &sagetimer1_reset,
	NULL, NULL, NULL,
	&u75, DEV_DEBUG, 0,
	i8253_dt, NULL, NULL
};

static t_stat sagetimer1_reset(DEVICE* dptr) 
{
	t_stat rc;
	if (!(rc = (dptr->flags & DEV_DIS) ?
    	del_iohandler(dptr->ctxt) :
		add_iohandler(&sagetimer1_unit,dptr->ctxt,i8253_io)) != SCPE_OK) return rc;
	return u75.reset(&u75);
}

static t_stat timer1_svc(UNIT* uptr)
{
	int32 wait;
	I8253CNTR* t1c0 = &u75.cntr[0];
	I8253CNTR* t2c0 = &u74.cntr[0];
	
//	fprintf(sim_deb,"TIMER1: timer1_svc called T1C0=%d T2C0=%d\n",t1c0->count,t2c0->count);
	/* we call this service 64000 times a second to decrement counter T1C0.
	 * When T1C0 reaches 0, it will decrement T2C0 */
	t1c0->count--;
	if (t1c0->count <= 0) {
		/* reload from divider */
		t1c0->count = t1c0->divider;
		/* decrement T2C0 counter and raise interrupt 6 if counter is zero */
		if (t2c0->count == 0) {
			sage_raiseint(TIMER2C0_PICINT);
//			printf("timer1 heartbeat\n");
			t2c0->count = 65536;
		}
		t2c0->count--;
	}

	/* adjust timing */
	wait = sim_rtcn_calb(64000,TMR_RTC1);
	sim_activate(u75.unit,wait); /* 64000 ticks per second */
	return SCPE_OK;
}

static t_stat u75_ckmode(I8253* chip,uint32 mode)
{
	/* @TODO check valid modes */
	return SCPE_OK;
}

static t_stat u75_call0(I8253* chip,int rw,uint32* value)
{
	if (rw==1) {
		I8253CNTR* cntr = &chip->cntr[0];
		if ((cntr->mode & I8253_BOTH) && (cntr->state & I8253_ST_MSBNEXT)) {
			sim_cancel(chip->unit);
			return SCPE_OK; /* not fully loaded yet */
		} else {
			/* start the CK0 clock at 64000Hz */
			sim_activate(chip->unit,sim_rtcn_init(64000,TMR_RTC1)); /* use timer1 C0 for this clock */
		}
	}
	return SCPE_OK;
}


/************************************************************************************
 *  timer 2
 ***********************************************************************************/

REG sagetimer2_reg[] = {
	{ HRDATA(INIT,  u74.init, 8), REG_HRO },
	{ HRDATA(STATE0,u74.cntr[0].state, 8),REG_HRO },
	{ HRDATA(STATE1,u74.cntr[1].state, 8),REG_HRO },
	{ HRDATA(STATE2,u74.cntr[2].state, 8),REG_HRO },
	{ HRDATA(MODE0, u74.cntr[0].mode, 8) },
	{ HRDATA(MODE1, u74.cntr[1].mode, 8) },
	{ HRDATA(MODE2, u74.cntr[2].mode, 8) },
	{ HRDATA(CNT0,  u74.cntr[0].count, 16) },
	{ HRDATA(CNT1,  u74.cntr[1].count, 16) },
	{ HRDATA(CNT2,  u74.cntr[2].count, 16) },
	{ HRDATA(LATCH0,u74.cntr[0].latch, 16) },
	{ HRDATA(LATCH1,u74.cntr[1].latch, 16) },
	{ HRDATA(LATCH2,u74.cntr[2].latch, 16) },
	{ HRDATA(DIV0,  u74.cntr[0].divider, 16),REG_HRO },
	{ HRDATA(DIV1,  u74.cntr[1].divider, 16),REG_HRO },
	{ HRDATA(DIV2,  u74.cntr[2].divider, 16),REG_HRO },
	{ NULL }
};

static MTAB sagetimer2_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,  "IO", "IO", &set_iobase, &show_iobase, NULL },
	{ 0 }
};

DEVICE sagetimer2_dev = {
	"TIMER2", &sagetimer2_unit, sagetimer2_reg, sagetimer2_mod,
	1, 16, 32, 2, 16, 16,
	NULL, NULL, &sagetimer2_reset,
	NULL, NULL, NULL,
	&u74, DEV_DEBUG, 0,
	i8253_dt, NULL, NULL
};

static t_stat sagetimer2_reset(DEVICE* dptr) 
{
	t_stat rc;
	if ((rc = (dptr->flags & DEV_DIS) ?
    	del_iohandler(dptr->ctxt) :
		add_iohandler(&sagetimer2_unit,dptr->ctxt,i8253_io)) != SCPE_OK) return rc;
	return u74.reset(&u74);
}

static t_stat u74_ckmode(I8253* chip,uint32 mode)
{
	/* @TODO check valid modes */
	return SCPE_OK;
}

static t_stat u74_call1(I8253* chip,int rw,uint32* value)
{
	if (rw==1) {
		I8253CNTR* cntr = &chip->cntr[1];
		if ((cntr->mode & I8253_BOTH) && (cntr->state & I8253_ST_MSBNEXT)) {
			sim_cancel(chip->unit);
			return SCPE_OK; /* not fully loaded yet */
		} else {
			/* start the CK0 clock at 64000Hz */
			sim_activate(chip->unit,sim_rtcn_init(64000,TMR_RTC1)); /* use timer1 C0 for this clock */
		}
	}
	return SCPE_OK;
}

static t_stat timer2_svc(UNIT* uptr)
{
	int32 wait;
	I8253CNTR* t2c1 = &u74.cntr[1];
	I8253CNTR* t2c2 = &u74.cntr[2];
	
	/* we call this service 64000 times a second to decrement counter T2C1.
	 * When T2C1 reaches 0, it will decrement T2C2 */
	t2c1->count--;
	if (t2c1->count <= 0) {
		/* reload from divider */
		t2c1->count = t2c1->divider;
		/* decrement T2C2 counter and raise interrupt 0 if counter is zero */
		if (t2c2->count == 0) {
//			printf("timer2 heartbeat\n");
			sage_raiseint(TIMER2C2_PICINT);
		}
		t2c2->count--;
	}

	/* adjust timing */
	wait = sim_rtcn_calb(64000,TMR_RTC1);
	sim_activate(u74.unit,wait); /* 64000 ticks per second */
	return SCPE_OK;
}
