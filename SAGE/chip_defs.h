/* chip_defs.h: definitions for several chips

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

   22-Jan-10    HV      Initial version
*/
#ifndef CHIP_DEFS_H_
#define CHIP_DEFS_H_

#include "sim_imd.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

/*****************************************************************************************
 * General implementation note:
 * 
 * Each chip device is implemented through a specific data structure, e.g. struct i8251
 * The address of this data structure MUST be passed to the device->ctxt variable.
 * The data structure MUST contain a PNP_INFO attribute at the beginning.
 * 
 * In case each unit of a complex device has an own chip, device->ctxt points to an array
 * of as much elements as there are units.
 * The device reset routine MUST call add_iohandler and del_iohandler depending on
 * enable or disable of the device. add_iohandler and del_iohandler will be passed
 * the corresponding address of the data structure for the chip (device->ctxt).
 *
 *****************************************************************************************/

/* set this to 0 to remove debug messages */ 
#ifndef DBG_MSG
#define DBG_MSG 1
#endif

/* generic debug tracing support */
#if DBG_MSG==1

#define ADDRESS_FORMAT		"[0x%08x]"	
#if UNIX_PLATFORM
#define NLP "\r\n"
#else
#define NLP "\n"
#endif

#define TRACE_PRINT(level,args)\
	if(sim_deb && chip->dev->dctrl & level) { \
		fprintf(sim_deb,"%-4s: " ADDRESS_FORMAT " ", chip->dev->name, PCX); \
		fprintf args; fputs(NLP,sim_deb); }
#define TRACE_PRINT0(level,fmt)\
	if(sim_deb && chip->dev->dctrl & level) { \
		fprintf(sim_deb,"%-4s: " ADDRESS_FORMAT " ", chip->dev->name, PCX); \
		fprintf(sim_deb,fmt NLP); }
#define TRACE_PRINT1(level,fmt,arg1)\
	if(sim_deb && chip->dev->dctrl & level) { \
		fprintf(sim_deb,"%-4s: " ADDRESS_FORMAT " ", chip->dev->name, PCX); \
		fprintf(sim_deb,fmt NLP,arg1); }
#define TRACE_PRINT2(level,fmt,arg1,arg2)\
	if(sim_deb && chip->dev->dctrl & level) { \
		fprintf(sim_deb,"%-4s: " ADDRESS_FORMAT " ", chip->dev->name, PCX); \
		fprintf(sim_deb,fmt NLP,arg1,arg2); }
#else
#define TRACE_PRINT(level,args)
#define TRACE_PRINT0(level,fmt)
#define TRACE_PRINT1(level,fmt,arg1)
#define TRACE_PRINT2(level,fmt,arg1,arg2)
#endif

/*****************************************************************************************
 *  general terminal multiplexer/socket support
 *****************************************************************************************/

typedef struct {
	int pfirst;
	int prate;
	TMLN ldsc;
	TMXR desc;
	UNIT* term;
	UNIT* poll;
} SERMUX;
t_stat mux_attach(UNIT*,char*,SERMUX*);
t_stat mux_detach(UNIT*,SERMUX*);

/*****************************************************************************************
 *  8259 PIC
 *****************************************************************************************/
#define I8259_ICW1			0x10
#define I8259_ICW1_A765		0xe0
#define I8259_ICW1_LTIM		0x08
#define	I8259_ICW1_ADI		0x04
#define I8259_ICW1_SNGL		0x02
#define I8259_ICW1_IC4		0x01
#define I8259_ICW4_SFNM		0x10
#define I8259_ICW4_BUF		0x08
#define I8259_ICW4_MS		0x04
#define I8259_ICW4_AEOI		0x02
#define I8259_ICW4_UPM		0x01
#define I8259_OCW2_MODE		0xe0
#define I8259_OCW2_LEVEL	0x07
#define I8259_OCW3_ESMM		0x40
#define I8259_OCW3_SMM		0x20
#define I8259_OCW3			0x08
#define I8259_OCW3_POLL		0x04
#define I8259_OCW3_RR		0x02
#define I8259_OCW3_RIS		0x01

typedef struct i8259 {
	PNP_INFO	pnp;
	DEVICE* dev;         /* backlink to device */
	t_stat (*write)(struct i8259* chip,int port,uint32 value);
	t_stat (*read)(struct i8259* chip,int port,uint32* value);
	t_stat (*reset)(struct i8259* chip);
	int   state;
	int   rmode;
	int32 imr;
	int32 isr;
	int32 irr;
	int32 icw1;
	int32 icw2;
	int32 icw4;
	int32 prio; /* which IR* has prio 7? */
	t_bool autoint;
	int intlevel;
	int intvector;
} I8259;

extern t_stat i8259_io(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask);
extern t_stat i8259_read(I8259* pic,int addr,uint32* value);
extern t_stat i8259_write(I8259* pic,int addr, uint32 value);
extern t_stat i8259_reset(I8259* chip);
extern t_stat i8259_raiseint(I8259* chip,int level);

/* Debug flags */
#define DBG_PIC_RD (1 << 0)
#define DBG_PIC_WR (1 << 1)
#define DBG_PIC_II (1 << 2)
#define DBG_PIC_IO (1 << 3)
extern DEBTAB i8259_dt[];

/*****************************************************************************************
 *  8251 USART
 *****************************************************************************************/
#define I8251_AMODE_STOP	0xc0
#define  I8251_AMODE_S1		0x40
#define  I8251_AMODE_S15	0x80
#define  I8251_AMODE_S2		0xc0
#define I8251_MODE_EP		0x20
#define I8251_MODE_PEN		0x10
#define I8251_AMODE_BITS	0x0c
#define  I8251_AMODE_BITS5	0x00
#define  I8251_AMODE_BITS6	0x04
#define  I8251_AMODE_BITS7	0x08
#define  I8251_AMODE_BITS8	0x0c
#define I8251_MODE_BAUD		0x03
#define  I8251_MODE_SYNC	0x00
#define  I8251_AMODE_BAUD1	0x01
#define  I8251_AMODE_BAUD16	0x02
#define  I8251_AMODE_BAUD64	0x03
#define I8251_SMODE_ESD		0x40
#define I8251_SMODE_SCS		0x80
#define I8251_CMD_EH		0x80
#define I8251_CMD_IR		0x40
#define I8251_CMD_RTS		0x20
#define I8251_CMD_ER		0x10
#define I8251_CMD_SBRK		0x08
#define I8251_CMD_RXE		0x04
#define I8251_CMD_DTR		0x02
#define I8251_CMD_TXEN		0x01
#define I8251_ST_DSR		0x80
#define I8251_ST_SYNBRK		0x40
#define I8251_ST_FE			0x20
#define I8251_ST_OE			0x10
#define I8251_ST_PE			0x08
#define I8251_ST_TXEMPTY	0x04
#define I8251_ST_RXRDY		0x02
#define I8251_ST_TXRDY		0x01

typedef struct i8251 {
	PNP_INFO pnp;
	DEVICE* dev;         /* backlink to device */
	t_stat (*write)(struct i8251* chip,int port,uint32 value);
	t_stat (*read)(struct i8251* chip,int port,uint32* value);
	t_stat (*reset)(struct i8251* chip);
	t_stat (*txint)(struct i8251* chip);
	t_stat (*rxint)(struct i8251* chip);
	UNIT* in;
	UNIT* out;
	SERMUX* mux;
	int init;
	int mode;
	int sync1;
	int sync2;
	int cmd;
	int ibuf;
	int obuf;
	int status;
	int bitmask;
	t_bool oob; /* out-of-band=1 will allow a console to receive CTRL-E even when receiver is disabled */
	int crlf;  /* CRLF state machine to suppress NUL bytes */
} I8251;

/* default handlers */
extern t_stat i8251_io(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask);
extern t_stat i8251_write(I8251* chip,int port,uint32 value);
extern t_stat i8251_read(I8251* chip,int port,uint32* value);
extern t_stat i8251_reset(I8251* chip);

/* Debug flags */
#define DBG_UART_RD  (1 << 0)
#define DBG_UART_WR  (1 << 1)
#define DBG_UART_IRQ (1 << 2)
extern DEBTAB i8251_dt[];


/*****************************************************************************************
 *  8253 TIMER
 *****************************************************************************************/
/*forward*/ struct i8253;
typedef struct {
	t_stat (*call)(struct i8253* chip,int rw,uint32* src);
	int state;			/* the current output state (latching, MSB/LSB out */
	int mode;			/* programmed mode */
	int32 latch;		/* the latched value of count */
	int32 divider;		/* programmed divider value */
	int32 count;		/* the real count value as calculated by rcall callback */
} I8253CNTR;

typedef struct i8253 {
	PNP_INFO pnp;
	DEVICE* dev;         /* backlink to device */
	UNIT* unit;          /* backlink to unit */
	t_stat (*reset)(struct i8253* chip);
	t_stat (*ckmode)(struct i8253* chip, uint32 value);
	I8253CNTR cntr[3];
	int init;
} I8253;

#define I8253_SCMASK	0xc0
#define  I8253_SC0		0x00
#define  I8253_SC1		0x40
#define  I8253_SC2		0x80
#define I8253_RLMASK	0x30
#define  I8253_LATCH	0x00
#define  I8253_LSB		0x10
#define  I8253_MSB		0x20
#define  I8253_BOTH		0x30
#define I8253_MODEMASK	0xe0
#define  I8253_MODE0	0x00
#define  I8253_MODE1	0x02
#define  I8253_MODE2	0x04
#define  I8253_MODE2a	0x0c
#define  I8253_MODE3	0x06
#define  I8253_MODE3a	0x0e
#define  I8253_MODE4	0x08
#define  I8253_MODE5	0x0a
#define I8253_MODEBIN	0x00
#define I8253_MODEBCD	0x01

#define I8253_ST_LSBNEXT	0x01
#define I8253_ST_MSBNEXT	0x02
#define I8253_ST_LATCH		0x08

/* default handlers */
extern t_stat i8253_io(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask);
extern t_stat i8253_reset(I8253* chip);

/* Debug flags */
#define DBG_TMR_RD (1 << 0)
#define DBG_TMR_WR (1 << 1)
extern DEBTAB i8253_dt[];

/****************************************************************************************
 * upd765 FDC chip
 ***************************************************************************************/
#define I8272_MAX_DRIVES    4
#define I8272_MAX_SECTOR    26
#define I8272_MAX_SECTOR_SZ    8192
/* 2^(7 + I8272_MAX_N) == I8272_MAX_SECTOR_SZ */
#define I8272_MAX_N         6

#define I8272_FDC_MSR       0   /* R=FDC Main Status Register, W=Drive Select Register */
#define I8272_FDC_DATA      1   /* R/W FDC Data Register */

typedef struct {
    UNIT *uptr;
    DISK_INFO *imd;
    uint8 ntracks;   /* number of tracks */
    uint8 nheads;    /* number of heads */
    uint32 sectsize; /* sector size, not including pre/postamble */
    uint8 track;     /* Current Track */
    uint8 ready;     /* Is drive ready? */
} I8272_DRIVE_INFO;

typedef enum i8272state {
	S_CMD=1, S_CMDREAD, S_EXEC, S_DATAWRITE, S_SECWRITE, S_SECREAD, S_DATAREAD, S_RESULT
} I8272_STATE;

typedef struct i8272 {
	PNP_INFO pnp;       /* Plug-n-Play Information */
	DEVICE* dev;         /* backlink to device */
	t_stat (*write)(struct i8272* chip,int port,uint32 data);
	t_stat (*read)(struct i8272* chip,int port,uint32* data);
	t_stat (*reset)(struct i8272* chip);
	void   (*seldrv)(struct i8272* chip,int seldrv);
	void   (*irq)(struct i8272* chip,int delay);
	
	I8272_STATE fdc_state;	/* internal state machine */
	uint32 fdc_dma_addr;/* DMA Transfer Address */
    uint8 fdc_msr;      /* 8272 Main Status Register */
    uint8 fdc_nd;       /* Non-DMA Mode 1=Non-DMA, 0=DMA */
    uint8 fdc_head;     /* H Head Number */
    uint8 fdc_sector;   /* R Record (Sector) */
    uint8 fdc_sec_len;  /* N Sector Length in controller units (2^(7+fdc_sec_len)) */
    uint8 fdc_eot;      /* EOT End of Track (Final sector number of cyl) */
    uint8 fdc_gap;      /* GAP Length */
    uint8 fdc_dtl;      /* DTL Data Length */
    uint8 fdc_mt;       /* Multiple sectors */
    uint8 fdc_mfm;      /* MFM mode */
    uint8 fdc_sk;       /* Skip Deleted Data */
    uint8 fdc_hds;      /* Head Select */
    uint8 fdc_seek_end; /* Seek was executed successfully */
    int   fdc_secsz;    /* N Sector Length in bytes: 2^(7 + fdc_sec_len),  fdc_sec_len <= I8272_MAX_N */
    int   fdc_nd_cnt;   /* read/write count in non-DMA mode, -1 if start read */
	uint8 fdc_sdata[I8272_MAX_SECTOR_SZ]; /* sector buffer */
	uint8 fdc_fault;	/* error code passed from some commands to sense_int */
	
    uint8 cmd_cnt;      /* command read count */
    uint8 cmd[10];      /* Storage for current command */
    uint8 cmd_len;      /* FDC Command Length */

    uint8 result_cnt;   /* result emit count */
    uint8 result[10];   /* Result data */
    uint8 result_len;   /* FDC Result Length */

    uint8 idcount;      /* used to cycle sector numbers during ReadID */
    uint8 irqflag;      /* set by interrupt, cleared by I8272_SENSE_INTERRUPT */

    uint8 fdc_curdrv;   /* Currently selected drive */
    I8272_DRIVE_INFO drive[I8272_MAX_DRIVES];
} I8272;

extern t_stat i8272_io(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask);
extern t_stat i8272_write(I8272* chip, int addr, uint32 value);
extern t_stat i8272_read(I8272* chip,int addr,uint32* value);
extern t_stat i8272_reset(I8272* chip);
extern void   i8272_seldrv(I8272* chip,int drvnum);
extern t_stat i8272_abortio(I8272* chip);
extern t_stat i8272_finish(I8272* chip);
extern t_stat i8272_attach(UNIT *uptr, char *cptr);
extern t_stat i8272_detach(UNIT *uptr);
extern t_stat i8272_setDMA(I8272* chip, uint32 dma_addr);

/* Debug flags */
#define DBG_FD_ERROR   (1 << 0)
#define DBG_FD_SEEK    (1 << 1)
#define DBG_FD_CMD     (1 << 2)
#define DBG_FD_RDDATA  (1 << 3)
#define DBG_FD_WRDATA  (1 << 4)
#define DBG_FD_STATUS  (1 << 5)
#define DBG_FD_FMT     (1 << 6)
#define DBG_FD_VERBOSE (1 << 7)
#define DBG_FD_IRQ     (1 << 8)
#define DBG_FD_STATE   (1 << 9)
#define DBG_FD_IMD     (1 << 10)
#define DBG_FD_DATA    (1 << 11)
extern DEBTAB i8272_dt[];
extern DEVICE* i8272_dev;

/* moved from i8272.c */
#define UNIT_V_I8272_WLK        (UNIT_V_UF + 0) /* write locked                             */
#define UNIT_I8272_WLK          (1 << UNIT_V_I8272_WLK)
#define UNIT_V_I8272_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_I8272_VERBOSE      (1 << UNIT_V_I8272_VERBOSE)
#define I8272_CAPACITY          (77*2*16*256)   /* Default Micropolis Disk Capacity         */
#define I8272_CAPACITY_SSSD     (77*1*26*128)   /* Single-sided Single Density IBM Diskette1 */

/*****************************************************************************************
 *  8255 PARPORT
 *****************************************************************************************/
typedef struct i8255 {
	PNP_INFO pnp;
	DEVICE* dev;         /* backlink to device */
	t_stat (*write)(struct i8255* chip,int port,uint32 data);
	t_stat (*read)(struct i8255* chip,int port,uint32* data);
	t_stat (*reset)(struct i8255* chip);
	t_stat (*calla)(struct i8255* chip,int rw);
	t_stat (*callb)(struct i8255* chip,int rw);
	t_stat (*callc)(struct i8255* chip,int rw);
	t_stat (*ckmode)(struct i8255* chip,uint32 data);
	uint32 porta;
	uint32 last_porta; /* for edge detection */
	uint32 portb;
	uint32 last_portb; /* for edge detection */
	uint32 portc;
	uint32 last_portc; /* for edge detection */
	uint32 ctrl;
} I8255;
extern t_stat i8255_io(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask);
extern t_stat i8255_read(I8255* chip,int port,uint32* data);
extern t_stat i8255_write(I8255* chip,int port,uint32 data);
#define I8255_RISEEDGE(port,bit) ((chip->last_##port & bit)==0 && (chip->port & bit))
#define I8255_FALLEDGE(port,bit) ((chip->last_##port & bit) && (chip->port & bit)==0)
#define I8255_ISSET(port,bit) ((chip->port & (bit))==(bit))
#define I8255_ISCLR(port,bit) ((chip->port & (bit))==0)

/* debug flags */
#define DBG_PP_WRA (1<<0)
#define DBG_PP_WRB (1<<1)
#define DBG_PP_WRC (1<<2)
#define DBG_PP_RDA (1<<3)
#define DBG_PP_RDB (1<<4)
#define DBG_PP_RDC (1<<5)
#define DBG_PP_MODE (1<<6)

#endif /*CHIP_DEFS_H_*/
