/*  i0.c: Intel intellec imm8-60

    Copyright (c) 2020, William A. Beech

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
        WILLIAM A. BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
        IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
        CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

        Except as contained in this notice, the name of William A. Beech shall not be
        used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from William A. Beech.

    MODIFICATIONS:

        18 July 20 - Original file.

    NOTES:

        These functions support a simulated imm8-60 interface board attached to a
        Teletype Model 33 ASR.
*/

#include "system_defs.h"

#if defined (IO_NUM) && (IO_NUM > 0)

// imm8-60 status bits
// I/O COMMAND CONSTANTS

#define RBIT    1               //TTY READER GO/NO GO
#define PCMD    2               //PTP GO/NO GO
#define RCMD    4               //PTR GO/NO GO
#define DSB     8               //PROM ENABLE/DISABLE. DSB=1
#define XXX     0x10            //DATA IN T/C
#define XXY     0x20            //DATA OUT T/C
#define PBIT    0x40            //1702 PROM PROG. GO/NO GO
#define PBITA   0x80            //1702A PROM PROG. GO/NO GO

// TTY I/O CONSTANTS

#define TTI     0               //TTY INPUT DATA PORT
#define TTO     0               //TTY OUTPUT DATA PORT
#define TTS     1               //TTY INPUT STATUS PORT
#define TTC     1               //TTY OUTPUT COMMAND PORT
#define TTYGO   RBIT OR DSB     //START TTY READER
#define TTYNO   DSB             //STOP TTY READER
#define TTYDA   1               //DATA AVAILABLE
#define TTYBE   4               //TRANSMIT BUFFER EMPTY

// CRT I/O CONSTANTS

#define CRTI    4               //CRT INPUT DATA PORT
#define CRTS    5               //CRT INPUT STATUS PORT
#define CRTO    4               //CRT OUTPUT DATA PORT
#define CRTDA   1               //DATA AVAILABLE
#define CRTBE   4               //TRANSMIT BUFFER EMPTY

// PTR I/O CONSTANTS

#define PTRI    3               //PTR INPUT DATA PORT (NOT INVERTED)
#define PTRS    TTS             //PTR INPUT STATUS PORT
#define PTRC    TTC             //PTR OUTPUT COMMAND PORT
#define PTRGO   RCMD OR DSB     //START PTR
#define PTRNO   TTYNO           //STOP PTR
#define PTRDA   0x20             //PTR DATA AVAILABLE

// PTP I/O CONSTANTS

#define PTPO    3               //PTP OUTPUT DATA PORT
#define PTPS    TTS             //PTP INPUT STATUS PORT
#define PTPC    TTC             //PTP OUTPUT COMMAND PORT
#define PRDY    0x40             //PUNCH READY STATUS
#define PTPGO   PCMD OR DSB     //PTP START PUNCH
#define PTPNO   TTYNO           //STOP PUNCH

// PROM PROGRAMMER I/O CONSTANTS

#define PAD     2               //PROM ADDRES OUTPUT PORT
#define PDO     PTPO            //PROM DATA OUTPUT PORT
#define PDI     2               //PROM DATA INPUT PORT
#define PROMC   TTC             //PROGRAMMING PULSE OUTPUT PORT
#define PROGO   PBITA           //START PROGRAMMING
#define PRONO   0               //STOP PROGRAMMING
#define ENB     0               //ENABLE PROGRAMMER

/* external globals */

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);

/* globals */

uint8   status = 0;
uint8   command = 0;

/* function prototypes */

t_stat IO_cfg(uint8 base, uint8 devnum);
t_stat IO_svc (UNIT *uptr);
t_stat IO_reset (DEVICE *dptr);
t_stat IO_attach (UNIT *uptr, CONST char *cptr);
t_stat PTR_reset(DEVICE *dptr);
t_stat PTR_attach (UNIT *uptr, CONST char *cptr);
uint8 IO_is(t_bool io, uint8 data, uint8 devnum);
uint8 IO_id(t_bool io, uint8 data, uint8 devnum);
uint8 IO_oc(t_bool io, uint8 data, uint8 devnum);
uint8 IO_od(t_bool io, uint8 data, uint8 devnum);
void IO_reset_dev(uint8 devnum);

/* imm-60 Standard I/O Data Structures */

UNIT IO_unit[4] = { 
    { UDATA (&IO_svc, 0, 0), 10 }, //TTY input/output
    { UDATA (&IO_svc, 0, 0), 10 }, //TTY status/command
    { UDATA (&IO_svc, 0, 0), KBD_POLL_WAIT }, //PROM data input/output
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, 0x1000) } //TTY reader/punch
};

REG IO_reg[] = {
    { HRDATA (DATA0, IO_unit[0].buf, 8) },
    { HRDATA (STAT0, status, 8) },
    { HRDATA (MODE0, IO_unit[0].u4, 8) },
    { HRDATA (CMD0, IO_unit[0].u5, 8) },
    { HRDATA (DATA1, IO_unit[1].buf, 8) },
    { HRDATA (STAT1, status, 8) },
    { HRDATA (MODE1, IO_unit[1].u4, 8) },
    { HRDATA (CMD1, IO_unit[1].u5, 8) },
    { HRDATA (DATA2, IO_unit[2].buf, 8) },
    { HRDATA (STAT2, status, 8) },
    { HRDATA (MODE2, IO_unit[2].u4, 8) },
    { HRDATA (CMD2, IO_unit[2].u5, 8) },
    { HRDATA (DATA3, IO_unit[3].buf, 8) },
    { HRDATA (STAT3, status, 8) },
    { HRDATA (MODE3, IO_unit[3].u4, 8) },
    { HRDATA (CMD3, IO_unit[3].u5, 8) },
    { NULL }
};

DEBTAB IO_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

MTAB IO_mod[] = {
    { 0 }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE IO_dev = {
    "IO",               //name
    IO_unit,            //units
    IO_reg,             //registers
    IO_mod,             //modifiers
    IO_NUM,             //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    &IO_reset,          //reset
    NULL,               //boot
    &IO_attach,         //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    IO_debug,           //debflags
    NULL,               //msize
    NULL                //lname
};

UNIT PTR_unit[1] = { 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, 0x1000) } //TTY reader/punch
};

REG PTR_reg[] = {
    { HRDATA (DATA0, IO_unit[0].buf, 8) },
    { HRDATA (STAT0, status, 8) },
    { HRDATA (MODE0, IO_unit[0].u4, 8) },
    { HRDATA (CMD0, IO_unit[0].u5, 8) },
    { NULL }
};

DEBTAB PTR_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

MTAB PTR_mod[] = {
    { 0 }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE PTR_dev = {
    "PTR",            //name
    PTR_unit,         //units
    PTR_reg,          //registers
    PTR_mod,          //modifiers
    PTR_NUM,          //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    &PTR_reset,       //reset
    NULL,               //boot
    &PTR_attach,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    PTR_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

// imm-60 configuration

t_stat IO_cfg(uint8 base, uint8 devnum)
{
    sim_printf("    io[%d]: at base port 0%02XH\n",
        devnum, base & 0xFF);
    reg_dev(IO_id, base, devnum); 
    reg_dev(IO_is, base + 1, devnum); 
    reg_dev(IO_oc, base + 2, devnum); 
    reg_dev(IO_od, base + 3, devnum); 
    return SCPE_OK;
}

/* Service routines to handle simulator functions */

/* IO_svc - actually gets char & places in buffer */

t_stat IO_svc (UNIT *uptr)
{
    int32 temp;

    sim_activate (uptr, uptr->wait); /* continue poll */
    if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) {
        status |= TTYDA;            //clear data avail
        return temp;                /* no char or error? */
    }
//    if (command & RBIT) {           //read from tty rdr
//        printf("%c", (int)(uptr+3)->filebuf);
//    }
    uptr->buf = toupper(temp & 0x7F); /* Save char */
    status &= ~TTYDA;               /* Set data available status */
    return SCPE_OK;
}

/* Reset routine */

t_stat IO_reset (DEVICE *dptr)
{
    uint8 devnum;
    
    for (devnum=0; devnum < IO_NUM; devnum++) {
        IO_reset_dev(devnum);
        sim_activate (&IO_unit[devnum], IO_unit[devnum].wait); /* activate unit */
    }
    return SCPE_OK;
}

void IO_reset_dev(uint8 devnum)
{
    status = TTYDA | PTRDA | DSB;         /* set data not avail status */
    IO_unit[devnum].u4 = 0;
    IO_unit[devnum].u5 = 0;
    IO_unit[devnum].u6 = 0;
    IO_unit[devnum].buf = 0;
    IO_unit[devnum].pos = 0;
}

t_stat IO_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   IO_attach: Attach error %d\n", r);
        return r;
    }
    return SCPE_OK;
}

t_stat PTR_reset(DEVICE *dptr)
{
    return SCPE_OK;
}

t_stat PTR_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   PTR_attach: Attach error %d\n", r);
        return r;
    }
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

// status/command

uint8 IO_is(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read status port - works*/
        return status;
    } else {                        /* write command port */
        command = data;
        if (command & RBIT) {
            status &= ~TTYDA;       /* Set data available status */
            data = data;
        }
    }
    return 0;
}

// TTY in/out

uint8 IO_id(t_bool io, uint8 data, uint8 devnum)
{
    char val;
    
    if (io == 0) {                  /* read data port */
        if (command & RBIT) {       //read from tty rdr
            status |= TTYDA;        //set TTYDA off
            return 'Z';
        } else {
            status |= TTYDA;        //set TTYDA off
            val = IO_unit[devnum].buf;
            val = (~val) & 0x7f;
            return (val);
        }
    } else {                        /* write data port - works*/
//        IO_unit[devnum].u3 |= TTYBE;    //set TTYBE off   
        val = ~data;
        sim_putchar(val & 0x7f);
    }
    return 0;
}

uint8 IO_oc(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read status port */
        data = data;
    } else {                        /* write status port */
        data = data;
    }
    return 0;
}

// TTY RDR in/PCH out
uint8 IO_od(t_bool io, uint8 data, uint8 devnum)
{
    char val;
    
    if (io == 0) {                  /* read data port */
        status |= PTRDA;            //set PTRDA off
        val = IO_unit[devnum].buf;
        val = (~val) & 0x7f;
        return (val);
    } else {                        /* write data port - works*/
        data = data;
//        IO_unit[devnum].u3 |= TTYBE;    //set TTYBE off   
//        val = ~data;
//        sim_putchar(val & 0x7f);
    }
    return 0;
}

#endif /* IO_NUM > 0 */

/* end of imm8-60.c */
