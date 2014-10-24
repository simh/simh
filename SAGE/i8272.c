/* i8272.c: Generic i8272/upd765 fdc chip

   Copyright (c) 2009,2010 Holger Veit
   Copyright (c) 2007-2008 Howard M. Harte http://www.hartetec.com
   
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

   Change log:
    - 22-Jul-2008, Howard M. Harte, original code in AltairZ80/i8272.c
    - 19-Apr-2008, Tony Nicholson, added other .IMD formats
    - 06-Aug-2008, Tony Nicholson, READID should use HDS bit and add support
             for logical Head and Cylinder maps in the .IMD image file (AGN)
    - 15-Feb-2010, Holger Veit, Support for M68K emulation, UPD765A/B commands
    - 05-Apr-2010, Holger Veit, use sim_deb for trace and dbg
    - 11-Apr-2010, Holger Veit, ReadID fixed, non-DMA RW fixed
    - 12-Apr-2010, Holger Veit, The whole mess rewritten for readability, and functionality
    - 17-Jul-2010, Holger Veit, Incorrect ST0 return from Recalibrate, should not set SEEK_END
    - 18-Jul-2010, Holger Veit, Adjust Interrupt delays, too fast for ReadID and Seek
    - 18-Jul-2010, Holger Veit, Lost last byte of a track, because ST0 was delivered too fast
    - 23-Jul-2010, Holger Veit, Fix error handling case of unattached drive
    - 25-Jul-2010, Holger Veit, One-Off error in i8251_datawrite
*/

#include "m68k_cpu.h"
#include "chip_defs.h"

#if defined (_WIN32)
#include <windows.h>
#endif

#include "sim_imd.h"

/* internal state machine:
 * 
 * fromstate   condition for transition  tostate       comment
 * ----------- ------------------------- ------------- ---------------------------------------------------
 * any         reset                     S_CMD         expect a command byte
 * S_CMD       commandcode               S_CMDREAD     has cmd, expect further arguments
 * S_CMDREAD   !cmdread                  S_CMDREAD     has some args, but need more
 * S_CMDREAD   cmdread                   S_EXEC        has all args, gather info, get data for read cmds
 *
 * S_EXEC      readsector                S_SECREAD     read sector
 * S_EXEC      writesector               S_DATAWRITE   expect data from system for write
 * S_EXEC      !(readsector|writesector) S_RESULT      process commands not requiring read/write (e.g. SPECIFY)
 *
 * S_SECREAD   IMMEDIATE                 S_DATAREAD    deliver read data back to system
 * S_DATAREAD  !dataread                 S_DATAREAD    did not deliver all data back yet
 * S_DATAREAD  dataread&moresectors      S_SECREAD     has return all data, but still more sectors to read
 * S_DATAREAD  dataread&!moresectors     S_RESULT      has return all data, reading finished
 *
 * S_DATAWRITE !datawritten              S_DATAWRITE   expect more data to write
 * S_DATAWRITE datawritten               S_SECWRITE    has all data, write data to disk
 * S_SECWRITE  moresectors               S_DATAWRITE   data written to disk, more data to write
 *
 * S_RESULT    !resultdone               S_RESULT      has not yet delivered all result codes
 * S_RESULT    resultdone                S_CMD         finished with result output, wait for next cmd
 */
#if DBG_MSG==1
#include <ctype.h>
#define NEXTSTATE(s) TRACE_PRINT2(DBG_FD_STATE,"TRANSITION from=%s to=%s",states[chip->fdc_state],states[s]); chip->fdc_state = s
#else
#define NEXTSTATE(s) chip->fdc_state = s
#endif

extern uint32 PCX;

int32 find_unit_index (UNIT *uptr);
static void i8272_interrupt(I8272* chip,int delay);

/* need to be implemented elsewhere */
extern void PutByteDMA(const uint32 Addr, const uint32 Value);
extern uint8 GetByteDMA(const uint32 Addr);

#define IMAGE_TYPE_DSK          1               /* Flat binary "DSK" image file.            */
#define IMAGE_TYPE_IMD          2               /* ImageDisk "IMD" image file.              */
#define IMAGE_TYPE_CPT          3               /* CP/M Transfer "CPT" image file.          */

/* Intel 8272 Commands */
#define I8272_READ_TRACK            0x02
#define I8272_SPECIFY               0x03
#define I8272_SENSE_DRIVE_STATUS    0x04
#define I8272_WRITE_DATA            0x05
#define I8272_READ_DATA             0x06
#define I8272_RECALIBRATE           0x07
#define I8272_SENSE_INTR_STATUS     0x08
#define I8272_WRITE_DELETED_DATA    0x09
#define I8272_READ_ID               0x0A
#define I8272_READ_DELETED_DATA     0x0C
#define I8272_FORMAT_TRACK          0x0D
#define I8272_SEEK                  0x0F
#define UPD765_VERSION				0x10
#define I8272_SCAN_EQUAL            0x11
#define I8272_SCAN_LOW_EQUAL        0x19
#define I8272_SCAN_HIGH_EQUAL       0x1D

/* SENSE DRIVE STATUS bit definitions */
#define DRIVE_STATUS_TWO_SIDED  0x08
#define DRIVE_STATUS_TRACK0     0x10
#define DRIVE_STATUS_READY      0x20
#define DRIVE_STATUS_WP         0x40
#define DRIVE_STATUS_FAULT      0x80

#define I8272_MSR_RQM           (1 << 7)
#define I8272_MSR_DATA_OUT      (1 << 6)
#define I8272_MSR_NON_DMA       (1 << 5)
#define I8272_MSR_FDC_BUSY      (1 << 4)

/* convert coded i8272 sector size to real byte length */
#define I8272_SEC2SZ(s) (128 << (s))

/* pointer to system specific FD device, to be set by implementation */
DEVICE* i8272_dev = NULL;

/* Debug Flags */
DEBTAB i8272_dt[] = {
    { "ERROR",  DBG_FD_ERROR },
    { "SEEK",   DBG_FD_SEEK },
    { "CMD",    DBG_FD_CMD },
    { "RDDATA", DBG_FD_RDDATA },
    { "WRDATA", DBG_FD_WRDATA },
    { "STATUS", DBG_FD_STATUS },
    { "FMT",    DBG_FD_FMT },
    { "VERBOSE",DBG_FD_VERBOSE },
    { "IRQ",    DBG_FD_IRQ },
    { "STATE",  DBG_FD_STATE },
    { "IMD",    DBG_FD_IMD },
    { "DATA",   DBG_FD_DATA},
    { NULL,     0 }
};

static char* states[] = {
	"invalid", "S_CMD", "S_CMDREAD", "S_EXEC", "S_DATAWRITE", "S_SECWRITE", 
	"S_SECREAD", "S_DATAREAD", "S_RESULT"
};

static char* messages[] = {
    "Undefined Command 0x0", "Undefined Command 0x1", "Read Track",            "Specify",
    "Sense Drive Status",    "Write Data",            "Read Data",             "Recalibrate",
    "Sense Interrupt Status","Write Deleted Data",    "Read ID",               "Undefined Command 0xB",
    "Read Deleted Data",     "Format Track",          "Undefined Command 0xE", "Seek",
    "Undefined Command 0x10","Scan Equal",            "Undefined Command 0x12","Undefined Command 0x13",
    "Undefined Command 0x14","Undefined Command 0x15","Undefined Command 0x16","Undefined Command 0x17",
    "Undefined Command 0x18","Scan Low Equal",        "Undefined Command 0x1A","Undefined Command 0x1B",
    "Undefined Command 0x1C","Scan High Equal",       "Undefined Command 0x1E","Undefined Command 0x1F"
};

static int8 cmdsizes[] = {
	1, 1, 9, 3, 2, 9, 9, 2,
	1, 9, 2, 1, 9, 6, 1, 3,
	1, 9, 1, 1, 1, 1, 1, 1,
	1, 9, 1, 1, 1, 9, 1, 1
};

static int8 resultsizes[] = {
	1, 1, 7, 0, 1, 7, 7, 0,
	2, 7, 7, 1, 7, 7, 1, 0,
	1, 7, 1, 1, 1, 1, 1, 1,
	1, 7, 1, 1, 1, 7, 1, 1
};

/* default routine to select the drive.
 * In principle, it just passes the US0/US1 bits into fdc_curdrv,
 * but the Sage FD does not use US0/US1 bits of FDC, for whatever reason... 
 */
void i8272_seldrv(I8272* chip, int drvnum)
{
	chip->fdc_curdrv = drvnum & 0x03;
}

/*
 * find_unit_index   find index of a unit
 */
int32 find_unit_index (UNIT *uptr)
{
    DEVICE *dptr;
    uint32 i;

    if (uptr == NULL) return -1;
    dptr = find_dev_from_unit(uptr);

    for(i=0; i<dptr->numunits; i++)
        if(dptr->units + i == uptr) break;

    return i == dptr->numunits ? -1 : i;
}

/* Attach routine */
t_stat i8272_attach(UNIT *uptr, char *cptr)
{
    char header[4];
    t_stat rc;
    int32 i = 0;
    I8272* chip;
    DEVICE* dptr;

    if ((dptr = find_dev_from_unit(uptr))==NULL) return SCPE_IERR;
    if ((chip = (I8272*)dptr->ctxt)==NULL) return SCPE_IERR;
    if ((rc = attach_unit(uptr, cptr)) != SCPE_OK) return rc;

    /* Determine length of this disk */
    uptr->capac = sim_fsize(uptr->fileref);

    if ((i = find_unit_index(uptr)) == -1) return SCPE_IERR;

    TRACE_PRINT1(DBG_FD_VERBOSE,"Attach I8272 drive %d\n",i);
    chip->drive[i].uptr = uptr;

    /* Default to drive not ready */
    chip->drive[i].ready = 0;

    if(uptr->capac > 0) {
        fgets(header, 4, uptr->fileref);
        if(strncmp(header, "IMD", 3)) {
            sim_printf("I8272: Only IMD disk images are supported\n");
            chip->drive[i].uptr = NULL;
            return SCPE_OPENERR;
        }
    } else {
        /* create a disk image file in IMD format. */
        if (diskCreate(uptr->fileref, "$Id: i8272.c 1999 2008-07-22 04:25:28Z hharte $") != SCPE_OK) {
            sim_printf("I8272: Failed to create IMD disk.\n");
            chip->drive[i].uptr = NULL;
            return SCPE_OPENERR;
        }
        uptr->capac = sim_fsize(uptr->fileref);
    }

    uptr->u3 = IMAGE_TYPE_IMD;

    if (uptr->flags & UNIT_I8272_VERBOSE) {
        sim_printf("I8272%d: attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);
    }

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if (uptr->flags & UNIT_I8272_VERBOSE)
            sim_printf("--------------------------------------------------------\n");
        chip->drive[i].imd = diskOpenEx(uptr->fileref, uptr->flags & UNIT_I8272_VERBOSE, dptr, DBG_FD_IMD, 0);
        if (uptr->flags & UNIT_I8272_VERBOSE)
            sim_printf("\n");
        if (chip->drive[i].imd == NULL) {
            sim_printf("I8272: IMD disk corrupt.\n");
            chip->drive[i].uptr = NULL;
            return SCPE_OPENERR;
        }
        chip->drive[i].ready = 1;
    } else
        chip->drive[i].imd = NULL;

    return SCPE_OK;
}

/* Detach routine */
t_stat i8272_detach(UNIT *uptr)
{
    t_stat rc;
    int8 i;
    DEVICE* dptr;
    I8272* chip;

    if ((dptr = find_dev_from_unit(uptr))==NULL) return SCPE_IERR;
    if ((chip = (I8272*)dptr->ctxt)==NULL) return SCPE_IERR;
    if ((i = find_unit_index(uptr)) == -1) return SCPE_IERR;

    TRACE_PRINT1(DBG_FD_VERBOSE,"Detach I8272 drive %d\n",i);
    rc = diskClose(&chip->drive[i].imd);
    chip->drive[i].ready = 0;
    if (rc != SCPE_OK) return rc;

    return detach_unit(uptr);  /* detach unit */
}

t_stat i8272_setDMA(I8272* chip, uint32 dma_addr)
{
    chip->fdc_dma_addr = dma_addr & 0xFFFFFF;
    return SCPE_OK;
}

t_stat i8272_io(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask)
{
	int port = ioh->offset;
	I8272* chip = (I8272*)ioh->ctxt;
	if (rw==MEM_WRITE)
		return chip->write ? chip->write(chip,port,*value) : i8272_write(chip,port,*value);
	else
		return chip->read ? chip->read(chip,port,value) : i8272_read(chip,port,value);
}

t_stat i8272_reset(I8272* chip)
{
	NEXTSTATE(S_CMD);
	chip->idcount = 0;
	chip->fdc_fault = 0;

	return SCPE_OK;
}

static uint8 floorlog2(unsigned int n)
{
    /* Compute log2(n) */
    uint8 r = 0;
    if (n >= 1<<16) { n >>=16; r += 16; }
    if (n >= 1<< 8) { n >>= 8; r +=  8; }
    if (n >= 1<< 4) { n >>= 4; r +=  4; }
    if (n >= 1<< 2) { n >>= 2; r +=  2; }
    if (n >= 1<< 1) { r +=  1; }
    return ((n == 0) ? (0xFF) : r); /* 0xFF is error return value */
}

static t_stat i8272_resultphase(I8272* chip,int delay) 
{
	uint8 cmd = chip->cmd[0] & 0x1f;
	chip->fdc_msr &= ~I8272_MSR_NON_DMA;
	chip->result_len = resultsizes[cmd];
    chip->result_cnt = 0;
	NEXTSTATE(S_RESULT);
    if (delay) i8272_interrupt(chip,delay);
	return SCPE_OK;
}

/*
 * this routine effectively sets the TC input of the FDC; this results in
 * terminating a current read or write operation and switches state to RESULT delivery
 * Sage-II needs this because during boot it tries to read sector 1...EOT (=9), but actually
 * stops polling after 2 sectors by asserting TC
 */
t_stat i8272_finish(I8272* chip)
{
	switch (chip->fdc_state) {
	case S_DATAREAD:
	case S_DATAWRITE:
	case S_SECREAD:
	case S_SECWRITE:
	case S_RESULT:
		TRACE_PRINT0(DBG_FD_VERBOSE,"Finish I/O, returning result");
		chip->irqflag = 0;
		chip->result[0] &= 0x3f; /* IC=normal termination */
		return i8272_resultphase(chip,0);
	default: /* @TODO is this correct? */
		TRACE_PRINT0(DBG_FD_VERBOSE,"Finish I/O, reset to S_CMD state");
		NEXTSTATE(S_CMD);
		return SCPE_OK;
	}
}

/* this routine is called when RDY pin goes to zero, effectively
 * terminating I/O immediately and going to S_RESULT state.
 */
t_stat i8272_abortio(I8272* chip)
{
	switch (chip->fdc_state) {
	case S_DATAREAD:
	case S_DATAWRITE:
	case S_SECREAD:
	case S_SECWRITE:
		TRACE_PRINT0(DBG_FD_VERBOSE,"RDY=0 during I/O, aborting and returning result");
		chip->irqflag = 0;
		chip->result[0] |= 0xc0; /* notify RDY change condition */
		return i8272_resultphase(chip,0);

	case S_RESULT:
		TRACE_PRINT0(DBG_FD_VERBOSE,"RDY=0, returning result");
		chip->irqflag = 0;
		return i8272_resultphase(chip,0);
	
	default: /* @TODO is this correct? */
		TRACE_PRINT0(DBG_FD_VERBOSE,"Abort I/O, reset to S_CMD state");
		NEXTSTATE(S_CMD);
		return SCPE_OK;
	}
}


static t_stat i8272_dataread(I8272* chip,uint32* value)
{
	if (chip->fdc_nd_cnt < chip->fdc_secsz) {
		/* return a single byte */
		chip->irqflag = 0;
		*value = chip->fdc_sdata[chip->fdc_nd_cnt];
		TRACE_PRINT2(DBG_FD_RDDATA,"read buffer #%d value=%x", chip->fdc_nd_cnt, *value);
		chip->fdc_nd_cnt++;
		if (chip->fdc_nd_cnt != chip->fdc_secsz) {
			i8272_interrupt(chip,1); /* notify one more byte is ready */
			return SCPE_OK;
		}
	}
	/* more sectors to read? */
	if (chip->fdc_sector <= chip->fdc_eot) {
		NEXTSTATE(S_SECREAD);
		return SCPE_OK;
	}
		
	/* finished data read */
	TRACE_PRINT0(DBG_FD_RDDATA,"read buffer complete.");
	chip->result[0] &= 0x3f; /* clear bits 7,6: terminated correctly */
	return i8272_resultphase(chip,0);
}

static I8272_DRIVE_INFO* i8272_select_drive(I8272* chip, uint8 drive)
{
	I8272_DRIVE_INFO* dip;
	
    (*chip->seldrv)(chip,drive);
    dip = &chip->drive[chip->fdc_curdrv];
    return dip->uptr == NULL ? NULL : dip;
}

static t_stat i8272_secread(I8272* chip)
{
	int i;
	unsigned int flags = 0;
	unsigned int readlen;
	I8272_DRIVE_INFO* dip = &chip->drive[chip->fdc_curdrv];

	/* finished with sector read? */
	if (chip->fdc_sector > chip->fdc_eot) {
		TRACE_PRINT2(DBG_FD_RDDATA,"No more sectors: sec=%d EOT=%d",chip->fdc_sector,chip->fdc_eot);
		return i8272_resultphase(chip,10);
	}
	
    /* no, read a buffer */
	TRACE_PRINT(DBG_FD_RDDATA,(sim_deb,"RD Data, C/H/S=%d/%d/%d sector len=%d",
		dip->track, chip->fdc_head, chip->fdc_sector, chip->fdc_secsz));

	if (dip->imd == NULL) {
		sim_printf(".imd is NULL!" NLP);
		return SCPE_STOP;
	}
	
	sectRead(dip->imd, dip->track, chip->fdc_head, chip->fdc_sector,
                  chip->fdc_sdata, chip->fdc_secsz, &flags, &readlen);

	chip->result[5] = chip->fdc_sector;
	chip->result[1] = 0x80;
	chip->fdc_sector++; /* prepare next sector */
	
	/* DMA mode? */
	if (chip->fdc_nd==0) { /* DMA mode */
		for (i=0; i < chip->fdc_secsz; i++) {
			PutByteDMA(chip->fdc_dma_addr, chip->fdc_sdata[i]);
			chip->fdc_dma_addr++;
		}
		TRACE_PRINT(DBG_FD_RDDATA, (sim_deb,"C:%d/H:%d/S:%d/L:%4d: Data transferred to RAM at 0x%06x",
			dip->track, chip->fdc_head, chip->fdc_sector,
			chip->fdc_secsz, chip->fdc_dma_addr - i));
	} else {
		chip->fdc_nd_cnt = 0; /* start buffer transfer */
        TRACE_PRINT0(DBG_FD_RDDATA,"read buffer started.");

		/* go to data transfer state */
		NEXTSTATE(S_DATAREAD);
		i8272_interrupt(chip,100);
	}
	return SCPE_OK;
}

t_stat i8272_read(I8272* chip,int addr,uint32* value)
{
	t_stat rc;
	I8272_DRIVE_INFO* dip;
	if ((dip = &chip->drive[chip->fdc_curdrv]) == NULL) {
		sim_printf("i8272_read: chip->drive returns NULL, fdc_curdrv=%d\n",chip->fdc_curdrv); 
		return SCPE_IERR;
	}

	switch(addr & 0x1) {
	case I8272_FDC_MSR:
		*value  = chip->fdc_msr | I8272_MSR_RQM;
		switch (chip->fdc_state) {
		case S_CMD:
		case S_CMDREAD:
			*value &= ~(I8272_MSR_DATA_OUT|I8272_MSR_FDC_BUSY);
			return SCPE_OK;
		case S_SECREAD:
		case S_DATAWRITE:
		case S_DATAREAD:
		case S_SECWRITE:
		case S_EXEC:
			*value |= (I8272_MSR_DATA_OUT|I8272_MSR_FDC_BUSY);
			break;
			
		case S_RESULT:
			*value |= I8272_MSR_DATA_OUT;
			*value &= ~I8272_MSR_FDC_BUSY;
			break;
		default:
			sim_printf("Default case in i8272_read(FDC_MSR): state=%d\n",chip->fdc_state);
			return SCPE_IERR;
		}            
		TRACE_PRINT1(DBG_FD_STATUS,"RD FDC MSR = 0x%02x",*value);
		return SCPE_OK;

	case I8272_FDC_DATA:
		for (;;) {
			switch (chip->fdc_state) {
			case S_DATAREAD: /* only comes here in non-DMA mode */
				if ((rc=i8272_dataread(chip,value)) != SCPE_OK) return rc;
				if (chip->fdc_state == S_RESULT ||
				    chip->fdc_state == S_DATAREAD) return SCPE_OK;
				/* otherwise will immediately move to state S_SECREAD */
				break;
			
			case S_SECREAD:
				if ((rc=i8272_secread(chip)) != SCPE_OK) return rc;
				if (chip->fdc_state ==S_DATAREAD) return SCPE_OK;
				/* will immediately move to state S_RESULT */
			case S_RESULT:
	            *value = chip->result[chip->result_cnt];
	            TRACE_PRINT2(DBG_FD_STATUS, "Result [%d]=0x%02x",chip->result_cnt, *value);
	            chip->irqflag = 0;
	            chip->result_cnt ++;
	            if(chip->result_cnt == chip->result_len) {
	                TRACE_PRINT0(DBG_FD_STATUS,"Result phase complete.\n");
	                NEXTSTATE(S_CMD);
	            }
	            
#if 0
            	else {
	        	    i8272_interrupt(chip,5);
            	}
#endif
            	return SCPE_OK;

			case S_CMD:
			case S_CMDREAD:
			case S_EXEC:
			case S_DATAWRITE:
			case S_SECWRITE:
	            *value = chip->result[0]; /* hack, in theory any value should be ok but this makes "format" work */
	            TRACE_PRINT1(DBG_FD_VERBOSE,"error, reading data register when not in data phase. Returning 0x%02x",*value);
				return SCPE_OK;
			
			default:
				sim_printf("Default case in i8272_read(FDC_DATA): state=%d\n",chip->fdc_state);
				return SCPE_IERR;
			}
		}
		return SCPE_OK;
	default:
		TRACE_PRINT1(DBG_FD_VERBOSE,"Cannot read register %x",addr);
		*value = 0xFF;
    }

    return SCPE_OK;
}

static t_stat i8272_makeresult(I8272* chip, uint8 s0, uint8 s1, uint8 s2, uint8 s3,uint8 s4, uint8 s5, uint8 s6)
{
	chip->result[0] = s0;
	chip->result[1] = s1;
	chip->result[2] = s2;
	chip->result[3] = s3;
	chip->result[4] = s4;
	chip->result[5] = s5;
	chip->result[6] = s6;
	chip->result_cnt = 0;
	chip->fdc_fault = 0;
	return SCPE_OK;
}

static I8272_DRIVE_INFO* i8272_decodecmdbits(I8272* chip)
{
	/* note this routine is also used in places where MT or SK bits are irrelevant.
	 * chip docs imply these bits to be set to 0 
	 */
	chip->fdc_mt = (chip->cmd[0] & 0x80) >> 7;
    chip->fdc_mfm = (chip->cmd[0] & 0x40) >> 6;
    chip->fdc_sk = (chip->cmd[0] & 0x20) >> 5;
    chip->fdc_hds = (chip->cmd[1] & 0x04) ? 1 : 0;
    return i8272_select_drive(chip,chip->cmd[1]);
}

#define msgMFM (chip->fdc_mfm ? "MFM" : "FM")
#define msgMT  (chip->fdc_mt ? "Multi" : "Single")
#define msgSK  (chip->fdc_sk ? "True" : "False")
#define msgHDS (chip->fdc_hds ? "True" : "False")
#define msgND  (chip->fdc_nd ? "NON-DMA" : "DMA")
#define msgCMD messages[cmd]

static t_stat i8272_nodriveerror(I8272* chip,const char* command,int delay)
{
	uint8 st0;
	
	TRACE_PRINT1(DBG_FD_ERROR,"%s: no drive or disk\n",command);
	st0 = 0x40 | 0x10 | chip->fdc_curdrv;
    i8272_makeresult(chip, st0, 0, 0, 0, 0, 0, 0);
    return i8272_resultphase(chip,delay);
}


static t_stat i8272_format(I8272* chip)
{
	uint8 track, fillbyte, sc, cnt;
    uint8 sectormap[I8272_MAX_SECTOR]; /* Physical to logical sector map for FORMAT TRACK */
	unsigned int flags = 0;
    int i;
	I8272_DRIVE_INFO* dip;

	/* get MFM bit, others are irrelevant */
    if ((dip = i8272_decodecmdbits(chip)) == NULL)
    	return i8272_nodriveerror(chip,"Format",10);

    track = dip->track;   
    chip->fdc_seek_end = track != chip->cmd[2] ? 1 : 0;
    chip->fdc_sec_len = chip->cmd[2];
    chip->fdc_secsz = I8272_SEC2SZ(chip->fdc_sec_len);
    if(chip->fdc_sec_len > I8272_MAX_N) {
        TRACE_PRINT(DBG_FD_ERROR, (sim_deb,"Illegal sector size %d [N=%d]. Reset to %d [N=%d].",
                                chip->fdc_secsz, chip->fdc_sec_len,
                                I8272_MAX_SECTOR_SZ, I8272_MAX_N));
        chip->fdc_sec_len = I8272_MAX_N;
    }
    chip->fdc_secsz = I8272_SEC2SZ(chip->fdc_sec_len);

    sc = chip->cmd[3];
    chip->fdc_gap = chip->cmd[4];
    fillbyte = chip->cmd[5];

    TRACE_PRINT(DBG_FD_FMT,(sim_deb,"Format Drive: %d, %s, C=%d. H=%d. N=%d, SC=%d, GPL=%02x, FILL=%02x",
        chip->fdc_curdrv, msgMFM, track, chip->fdc_head, chip->fdc_sec_len, sc, chip->fdc_gap, fillbyte));

    cnt = 0;

    i8272_makeresult(chip,
    		((chip->fdc_hds & 1) << 2) | chip->fdc_curdrv, 
			0, 0, track,
			chip->fdc_head,   /* AGN for now we cannot format with logicalHead */
    		chip->fdc_sector, /* AGN ditto for logicalCyl */
    		chip->fdc_sec_len);

    for(i = 1; i <= sc; i++) {
		TRACE_PRINT(DBG_FD_CMD, (sim_deb,"Format Track %d, Sector=%d, len=%d",
			track, i, chip->fdc_secsz));

        if(cnt >= I8272_MAX_SECTOR) {
            TRACE_PRINT0(DBG_FD_ERROR,"Illegal sector count");
            cnt = 0;
        }
        sectormap[cnt] = i;
        cnt++;
        if(cnt == sc) {
            trackWrite(dip->imd, track, chip->fdc_head, sc,
                chip->fdc_secsz, sectormap, chip->fdc_mfm ? 3 : 0,
                fillbyte, &flags);

            /* Recalculate disk size */
            dip->uptr->capac = sim_fsize(dip->uptr->fileref);
        }
    }
    chip->fdc_sector = sc;
    return i8272_resultphase(chip,1000);
}

static t_stat i8272_readid(I8272* chip)
{
	TRACK_INFO* curtrk;
	I8272_DRIVE_INFO* dip;
	uint8 hds = chip->fdc_hds;
	
	if ((dip = i8272_decodecmdbits(chip)) == NULL)
    	return i8272_nodriveerror(chip,"Readid",10);

    curtrk = &dip->imd->track[dip->track][hds];
    
    /* Compute the i8272 "N" value from the sectorsize of this              */
    /* disk's current track - i.e. N = log2(sectsize) - log2(128)           */
    /* The calculation also works for non-standard format disk images with  */
    /* sectorsizes of 2048, 4096 and 8192 bytes                             */
    chip->fdc_sec_len = floorlog2(curtrk->sectsize) - 7; /* AGN fix to use fdc_hds (was fdc_head)*/
	chip->fdc_secsz = I8272_SEC2SZ(chip->fdc_sec_len);
    
    /* HV we cycle the read sectors on each call of READID to emulator disk spinning */
    /* Sage BIOS need this to find the highest sector number. */
    /* This could be improved by adding some delay based */
    /* on elapsed time for a more "realistic" simulation. */
    /* This would allow disk analysis programs that use   */
    /* READID to detect non-standard disk formats.        */
    if (chip->idcount == 0 || chip->idcount >= curtrk->nsects) {
    	chip->fdc_sector = curtrk->start_sector;
    	chip->idcount = 1;
    } else {
    	chip->fdc_sector++;
        chip->idcount++;
    }
    if((chip->fdc_sec_len == 0xF8) || (chip->fdc_sec_len > I8272_MAX_N)) { /* Error calculating N or N too large */
        TRACE_PRINT1(DBG_FD_ERROR,"Illegal sector size N=%d. Reset to 0.",chip->fdc_sec_len);
        chip->fdc_sec_len = 0;
        chip->fdc_secsz = 0;
        return SCPE_OK;
    }

    /* build result */
	i8272_makeresult(chip,
			((hds & 1) << 2) | chip->fdc_curdrv,
    		0, 0,
			curtrk->logicalCyl[chip->fdc_sector],  /* AGN logicalCyl */
			curtrk->logicalHead[chip->fdc_sector], /* AGN logicalHead */
			chip->fdc_sector,
			chip->fdc_sec_len);

	TRACE_PRINT(DBG_FD_CMD, (sim_deb,
        "READ ID Drive %d result ST0=%02x ST1=%02x ST2=%02x C=%d H=%d R=%02x N=%d",
        chip->fdc_curdrv, chip->result[0],
        chip->result[1],chip->result[2],chip->result[3],
        chip->result[4],chip->result[5],chip->result[6]));
    return i8272_resultphase(chip,20);
}

static t_stat i8272_seek(I8272* chip)
{
	I8272_DRIVE_INFO* dip;
	if ((dip = i8272_decodecmdbits(chip)) == NULL)
    	return i8272_nodriveerror(chip,"Seek",10);

    dip->track = chip->cmd[2];
    chip->fdc_head = chip->fdc_hds; /*AGN seek should save the head */
    chip->fdc_seek_end = 1;
    TRACE_PRINT(DBG_FD_SEEK, (sim_deb,"Seek Drive: %d, %s %s, C=%d. Skip Deleted Data=%s Head Select=%s",
                chip->fdc_curdrv, msgMT, msgMFM, chip->cmd[2], msgSK, msgHDS));

    NEXTSTATE(S_CMD); /* no result phase */
	i8272_interrupt(chip,100);
    return SCPE_OK;
}

static t_stat i8272_senseint(I8272* chip)
{
	I8272_DRIVE_INFO* dip = &chip->drive[chip->fdc_curdrv];
	uint8 st0 = (chip->fdc_seek_end ? 0x20 : 0x00) | chip->fdc_curdrv;
	if (chip->fdc_fault)
		st0 |= (0x40 | chip->fdc_fault);

    TRACE_PRINT2(DBG_FD_CMD,"Sense Interrupt Status ST0=0x%x PCN=%d",st0,dip->track);
	i8272_makeresult(chip, st0, dip->track, 0,0,0,0,0);
    chip->irqflag = 0; /* clear interrupt flag, don't raise a new one */
	return i8272_resultphase(chip,0);
}

static t_stat i8272_sensedrive(I8272* chip)
{
	uint8 st3;
	I8272_DRIVE_INFO* dip;
	t_bool track0;
	
    if ((dip = i8272_select_drive(chip,chip->cmd[1])) == NULL) {
    	sim_printf("i8272_sensedrive: i8272_select_drive returns 0\n");
    	st3 = DRIVE_STATUS_FAULT;
    	track0 = FALSE;
    } else {
    	track0 = dip->track == 0;
	    st3 = dip->ready ? DRIVE_STATUS_READY : 0; /* Drive Ready */
	    if(imdGetSides(dip->imd) == 2) {
	        st3 |= DRIVE_STATUS_TWO_SIDED;    /* Two-sided?       */
	    }
	    if(imdIsWriteLocked(dip->imd) || (dip->uptr->flags & UNIT_I8272_WLK)) {
	        st3 |= DRIVE_STATUS_WP;           /* Write Protected? */
	    }
    }
    st3 |= (chip->fdc_hds & 1) << 2;
    st3 |= chip->fdc_curdrv;
    st3 |= track0 ? DRIVE_STATUS_TRACK0 : 0x00; /* Track 0 */
	i8272_makeresult(chip, st3, 0, 0, 0, 0, 0, 0);
    
    TRACE_PRINT1(DBG_FD_CMD,"Sense Drive Status = 0x%02x", st3);
	return i8272_resultphase(chip,5);
}

static t_stat i8272_recalibrate(I8272* chip)
{
	I8272_DRIVE_INFO* dip;
    if ((dip = i8272_select_drive(chip,chip->cmd[1])) == NULL) {
    	TRACE_PRINT1(DBG_FD_ERROR,"Recalibrate: no drive or disk drive=%x\n",chip->cmd[1]);
    	chip->fdc_fault = 0x10; /* EC error */
    } else {
	    dip->track = 0;
	    chip->idcount = 0; /* initialize the ID cycler (used by READID) */
//    chip->fdc_seek_end = 1;
	    chip->fdc_seek_end = 0;
    }
    TRACE_PRINT2(DBG_FD_SEEK,"Recalibrate: Drive 0x%02x, EC=%d",chip->fdc_curdrv,chip->fdc_fault?1:0);

    NEXTSTATE(S_CMD);  /* No result phase */
	i8272_interrupt(chip,20);
    return SCPE_OK;
}

static t_stat i8272_specify(I8272* chip)
{
	chip->fdc_fault = 0;
    chip->fdc_nd  = chip->cmd[2] & 0x01;		/* DMA/non-DMA mode */
    TRACE_PRINT(DBG_FD_CMD, (sim_deb,"Specify: SRT=%d, HUT=%d, HLT=%d, ND=%s",
    		16 - ((chip->cmd[1] & 0xF0) >> 4),	/*SRT*/
    		(chip->cmd[1] & 0x0F) * 16,			/*HUT*/
    		((chip->cmd[2] & 0xFE) >> 1) * 2,	/*HLT*/
    		msgND));

    NEXTSTATE(S_CMD); 							/* no result phase */
    i8272_interrupt(chip,1);
    return SCPE_OK;
}

static t_bool i8272_secrw(I8272* chip,uint8 cmd)
{
	TRACK_INFO* curtrk;
	I8272_DRIVE_INFO* dip;
	if ((dip = i8272_decodecmdbits(chip)) == NULL) return FALSE;

	chip->fdc_seek_end = dip->track != chip->cmd[2] ? 1 : 0;
    if (dip->track != chip->cmd[2]) {
        TRACE_PRINT(DBG_FD_CMD, (sim_deb,
            "ERROR: CMD=0x%02x[%s]: Drive: %d, Command wants track %d, but positioner is on track %d.",
            cmd, msgCMD, chip->fdc_curdrv, chip->cmd[2], dip->track));
    }

    dip->track = chip->cmd[2];
    chip->fdc_head = chip->cmd[3] & 1; /* AGN mask to head 0 or 1 */
    curtrk = &dip->imd->track[dip->track][chip->fdc_head];
    
    chip->fdc_sector = chip->cmd[4];
    chip->fdc_sec_len = chip->cmd[5];
    if(chip->fdc_sec_len > I8272_MAX_N) {
        TRACE_PRINT(DBG_FD_ERROR, (sim_deb,"Illegal sector size %d [N=%d]. Reset to %d [N=%d].",
                                I8272_SEC2SZ(chip->fdc_sec_len), chip->fdc_sec_len,
                                I8272_MAX_SECTOR_SZ, I8272_MAX_N));
        chip->fdc_sec_len = I8272_MAX_N;
    }
    chip->fdc_secsz = I8272_SEC2SZ(chip->fdc_sec_len);
    chip->fdc_eot = chip->cmd[6];
    chip->fdc_gap = chip->cmd[7];
    chip->fdc_dtl = chip->cmd[8];

    TRACE_PRINT(DBG_FD_CMD, (sim_deb,
        "CMD=0x%02x[%s]: Drive: %d, %s %s, C=%d. H=%d. S=%d, N=%d, EOT=%02x, GPL=%02x, DTL=%02x",
        cmd, msgCMD, chip->fdc_curdrv, msgMT, msgMFM, 
        dip->track, chip->fdc_head, chip->fdc_sector,
        chip->fdc_sec_len, chip->fdc_eot, chip->fdc_gap, chip->fdc_dtl));

    i8272_makeresult(chip,
    		((chip->fdc_hds & 1) << 2) | chip->fdc_curdrv | 0x40, 
    		0, 0,
    		curtrk->logicalCyl[chip->fdc_sector],  /* AGN logicalCyl */
    		curtrk->logicalHead[chip->fdc_sector], /* AGN logicalHead */
    		chip->fdc_sector,
			chip->fdc_sec_len);
	chip->result_cnt = 0;
	chip->fdc_nd_cnt = 0; /* start buffer transfer */
	return TRUE;
}

static t_bool i8272_secwrite(I8272* chip)
{
	unsigned int readlen;
	unsigned int flags = 0;
	I8272_DRIVE_INFO* dip = &chip->drive[chip->fdc_curdrv];
	
	TRACE_PRINT(DBG_FD_WRDATA, (sim_deb,"SecWrite: C:%d/H:%d/S:%d/L:%4d",
			dip->track, chip->fdc_head, chip->fdc_sector,
			chip->fdc_secsz));
	sectWrite(dip->imd, dip->track, chip->fdc_head, chip->fdc_sector,
            chip->fdc_sdata, chip->fdc_secsz, &flags, &readlen);
	chip->fdc_sector++;
	if (chip->fdc_sector > chip->fdc_eot)
		return i8272_resultphase(chip,200);

	NEXTSTATE(S_DATAWRITE);
	if (chip->fdc_nd) { /* non-DMA */
		chip->fdc_nd_cnt = 0;
		i8272_interrupt(chip,10); /* non-DMA: initiate next sector write */
		return TRUE;
	}				
	return FALSE;
}

static t_bool i8272_datawrite(I8272* chip,uint32 value,I8272_DRIVE_INFO* dip)
{
	int i;
	
	/* finished with sector write? */
	if (chip->fdc_sector > chip->fdc_eot) {
		TRACE_PRINT0(DBG_FD_WRDATA,"Finished sector write");
		return i8272_resultphase(chip,200);
	}
	if (chip->fdc_nd == 0) { /* DMA */
		for (i=0; i< chip->fdc_secsz; i++) {
			chip->fdc_sdata[i] = GetByteDMA(chip->fdc_dma_addr);
			chip->fdc_dma_addr++;
		}
		TRACE_PRINT(DBG_FD_WRDATA, (sim_deb,"C:%d/H:%d/S:%d/L:%4d: Data transferred from RAM at 0x%06x",
				dip->track, chip->fdc_head, chip->fdc_sector,
				chip->fdc_secsz, chip->fdc_dma_addr - i));
	} else { /* non-DMA */
		chip->fdc_msr |= I8272_MSR_NON_DMA;
		if ((chip->fdc_nd_cnt+1) < chip->fdc_secsz) {
			chip->fdc_sdata[chip->fdc_nd_cnt] = value;
			TRACE_PRINT(DBG_FD_WRDATA,(sim_deb,"write buffer #%d value=%x (%c)", chip->fdc_nd_cnt,value,isprint(value)?value:'?'));
			chip->fdc_nd_cnt++;
			/* not yet finished buffering data, leave writer routine */
			i8272_interrupt(chip,10);
			TRACE_PRINT0(DBG_FD_WRDATA,"Expect more data");
			return TRUE;
		}
	}
	TRACE_PRINT0(DBG_FD_WRDATA,"Finished with data write");
	return FALSE;
}

t_stat i8272_write(I8272* chip, int addr, uint32 value)
{
	uint8 cmd;
	I8272_DRIVE_INFO* dip;
	if ((dip = &chip->drive[chip->fdc_curdrv]) == NULL) {
    	sim_printf("i8272_write: chip->drive returns 0 fdc_curdrv=%d\n",chip->fdc_curdrv);
		return SCPE_IERR;
	}

	switch(addr & 0x1) {
	case I8272_FDC_MSR:
		TRACE_PRINT1(DBG_FD_VERBOSE,"WR Drive Select Reg=%02x", value);
		return SCPE_OK;

	case I8272_FDC_DATA:
		chip->fdc_msr &= 0xF0;
		TRACE_PRINT2(DBG_FD_VERBOSE,"WR Data, index=%d value=%x", chip->cmd_cnt,value);
		
		for (;;) {
			switch (chip->fdc_state) {
			case S_CMD:
				/* first cmd byte */
				cmd = value & 0x1f;
				chip->cmd_cnt = 0;
	            TRACE_PRINT2(DBG_FD_CMD,"CMD=0x%02x[%s]", cmd, msgCMD);
	           	chip->cmd_len = cmdsizes[cmd];
	           	NEXTSTATE(S_CMDREAD);
	           	/*fallthru*/
			case S_CMDREAD:
				/* following cmd bytes */
				chip->cmd[chip->cmd_cnt] = value;
				chip->cmd_cnt++;
			
				if (chip->cmd_cnt == chip->cmd_len) {
					chip->fdc_nd_cnt = 0; /* initialize counter for Non-DMA mode */
					chip->cmd_cnt = 0; /* reset index for next CMD */
					NEXTSTATE(S_EXEC); /* continue immediately with S_EXEC code */
					break;
				}
				return SCPE_OK;
			case S_DATAREAD: /* data reading happens in i8272_read */
				return SCPE_OK;
			case S_RESULT: /* result polling happens in i8272_read */
				return SCPE_OK;
			case S_DATAWRITE:
				if (i8272_datawrite(chip,value,dip)) return SCPE_OK;
				TRACE_PRINT0(DBG_FD_WRDATA,"Go Sector Write");
				NEXTSTATE(S_SECWRITE);
				/*fallthru*/
			case S_SECWRITE: /* write buffer */
				if (i8272_secwrite(chip)) return SCPE_OK;
				break;
			case S_SECREAD:
				return i8272_secread(chip);
			case S_EXEC:
				cmd = chip->cmd[0] & 0x1f;
				switch (cmd) {
				case I8272_SPECIFY:
					return i8272_specify(chip);

				case I8272_SENSE_INTR_STATUS:
					return i8272_senseint(chip);

				case I8272_SENSE_DRIVE_STATUS:  /* Setup Status3 Byte */
					return i8272_sensedrive(chip);

                case I8272_RECALIBRATE: /* RECALIBRATE */
                	return i8272_recalibrate(chip);

                case UPD765_VERSION:
					i8272_makeresult(chip, 0x80, 0, 0, 0, 0, 0, 0);
					/* signal UPD765A, don't know whether B version (0x90) is relevant */
					return i8272_resultphase(chip,5);

				case I8272_SEEK:    /* SEEK */
					return i8272_seek(chip);

               case I8272_READ_ID:
					return i8272_readid(chip);

                case I8272_FORMAT_TRACK:    /* FORMAT A TRACK */
                	return i8272_format(chip);

                case I8272_READ_TRACK:
                    sim_printf("I8272: " ADDRESS_FORMAT " Read a track (untested.)" NLP, PCX);
                    chip->fdc_sector = 1; /* Read entire track from sector 1...eot */
                case I8272_READ_DATA:
                case I8272_READ_DELETED_DATA:
                	if (!i8272_secrw(chip,cmd))
                    	return i8272_nodriveerror(chip,"I8272_READ_*_DATA",10);

                	/* go directly to secread state */
                	NEXTSTATE(S_SECREAD);
					break;
                	
                case I8272_WRITE_DATA:
                case I8272_WRITE_DELETED_DATA:
                	if (!i8272_secrw(chip,cmd))
                    	return i8272_nodriveerror(chip,"I8272_WRITE_*_DATA",10);

                	NEXTSTATE(S_DATAWRITE); /* fill buffer */
					if (chip->fdc_nd != 0) { /* non-DMA */
                		i8272_interrupt(chip,100); /* request the first data byte */
                		return SCPE_OK;
					}
                	break;

                case I8272_SCAN_LOW_EQUAL:
                case I8272_SCAN_HIGH_EQUAL:
                case I8272_SCAN_EQUAL:
                	if (!i8272_secrw(chip,cmd))
                    	return i8272_nodriveerror(chip,"I8272_SCAN_*",10);

                	TRACE_PRINT0(DBG_FD_CMD,"Scan Data");
                    TRACE_PRINT0(DBG_FD_ERROR,"ERROR: Scan not implemented.");
                    return i8272_resultphase(chip,200);
				}
			}
		}
		/*NOTREACHED*/
	
	default:
		return SCPE_OK;
	}
}

static void i8272_interrupt(I8272* chip,int delay)
{
    TRACE_PRINT0(DBG_FD_IRQ,"FDC Interrupt");
    chip->irqflag = 1;
	(*chip->irq)(chip,delay);
}
