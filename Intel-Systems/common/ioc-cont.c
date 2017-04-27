/*  ioc-cont.c: Intel IPC DBB adapter

    Copyright (c) 2010, William A. Beech

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

        27 Jun 16 - Original file.

    NOTES:


*/

#include "system_defs.h"                /* system header in system dir */

#define DEBUG   0

//dbb status flag bits
#define OBF     1
#define IBF     2
#define F0      4
#define CD      8

//dbb command codes
#define PACIFY  0x00    //Resets IOC and its devices
#define ERESET  0x01    //Resets device-generated error (not used by standard devices)
#define SYSTAT  0x02    //Returns subsystem status byte to master
#define DSTAT   0x03    //Returns device status byte to master
#define SRQDAK  0x04    //Enables input of device interrupt acknowledge mask from master
#define SRQACK  0x05    //Clears IOC subsystem interrupt request
#define SRQ     0x06    //Tests ability of IOC to forward an interrupt request to the master
#define DECHO   0x07    //Tests ability of IOC to echo data byte sent by master
#define CSMEM   0x08    //Requests IOC to checksum on-board ROM. Returns pass/fail
#define TRAM    0x09    //Requests IOC to test on-board RAM. Returns pass/fail
#define SINT    0x0A    //Enables specified device interrupt from IOC
#define CRTC    0x10    //Requests data byte output to the CRT monitor
#define CRTS    0x11    //Returns CRT status byte to master
#define KEYC    0x12    //Requests data byte input from the keyboard
#define KSTC    0x13    //Returns keyboard status byte to master
#define WPBC    0x15    //Enables input of first of five bytes that define current diskette operation
#define WPBCC   0x16    //Enables input of each of four bytes that follow WPBC
#define WDBC    0x17    //Enables input of diskette write bytes from master
#define RDBC    0x19    //Enables output of diskette read bytes to master
#define RRSTS   0x1B    //Returns diskette result byte to master
#define RDSTS   0x1C    //Returns diskette device status byte to master

/* external globals */

extern uint16   port;                   //port called in dev_table[port]
extern int32    PCX;

/* function prototypes */

uint8 ioc_cont0(t_bool io, uint8 data);    /* ioc_cont*/
uint8 ioc_cont1(t_bool io, uint8 data);    /* ioc_cont*/
t_stat ioc_cont_reset (DEVICE *dptr, uint16 baseport);

/* external function prototypes */

extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8), uint16, uint8);
extern uint32 saved_PC;                    /* program counter */

/* globals */

uint8   dbb_stat;
uint8   dbb_cmd;
uint8   dbb_in;
uint8   dbb_out;

UNIT ioc_cont_unit[] = {
    { UDATA (0, 0, 0) },                /* ioc_cont*/
};

REG ioc_cont_reg[] = {
    { HRDATA (CONTROL0, ioc_cont_unit[0].u3, 8) }, /* ioc_cont */
    { NULL }
};

DEBTAB ioc_cont_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE ioc_cont_dev = {
    "IOC-CONT",             //name
    ioc_cont_unit,         //units
    ioc_cont_reg,          //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &ioc_cont_reset,       //reset
    NULL,       //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    ioc_cont_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/* Reset routine */

t_stat ioc_cont_reset(DEVICE *dptr, uint16 baseport)
{
    sim_printf("      ioc_cont[%d]: Reset\n", 0);
    sim_printf("      ioc_cont[%d]: Registered at %04X\n", 0, baseport);
    reg_dev(ioc_cont0, baseport, 0); 
    reg_dev(ioc_cont1, baseport + 1, 0); 
    dbb_stat = 0x00;                /* clear DBB status */
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* IOC control port functions */

uint8 ioc_cont0(t_bool io, uint8 data)
{
    if (io == 0) {                      /* read data port */
        if (DEBUG)
            sim_printf("\n   ioc_cont0: read data returned %02X PCX=%04X", dbb_out, PCX);
        return dbb_out;
    } else {                            /* write data port */
        dbb_in = data;
        dbb_stat |= IBF;
        if (DEBUG)
            sim_printf("\n   ioc_cont0: write data=%02X port=%02X PCX=%04X", dbb_in, port, PCX);
        return 0;
    }
}

uint8 ioc_cont1(t_bool io, uint8 data)
{
    int temp;

    if (io == 0) {                      /* read status port */
        if ((dbb_stat & F0) && (dbb_stat & IBF)) {
            temp = dbb_stat;
            if (DEBUG)
                sim_printf("\n   ioc_cont1: DBB status read 1 data=%02X PCX=%04X", dbb_stat, PCX);
            dbb_stat &= ~IBF;           //reset IBF flag
            return temp;
        } 
        if ((dbb_stat & F0) && (dbb_stat & OBF)) {
            temp = dbb_stat;
            if (DEBUG)
                sim_printf("\n   ioc_cont1: DBB status read 2 data=%02X PCX=%04X", dbb_stat, PCX);
            dbb_stat &= ~OBF;           //reset OBF flag
            return temp;
        } 
        if (dbb_stat & F0) {
            temp = dbb_stat;
            if (DEBUG)
                sim_printf("\n   ioc_cont1: DBB status read 3 data=%02X PCX=%04X", dbb_stat, PCX);
            dbb_stat &= ~F0;            //reset F0 flag
            return temp;
        }
//        if (DEBUG)
//            sim_printf("   ioc_cont1: DBB status read 4 data=%02X PCX=%04X\n", dbb_stat, PCX);
        return dbb_stat;
    } else {                            /* write command port */
        dbb_cmd = data;
        switch(dbb_cmd){
            case PACIFY:                //should delay 100 ms
                dbb_stat = 0;
                break;
            case SYSTAT:
                dbb_out = 0;
                dbb_stat |= OBF;
                dbb_stat &= ~CD;
                break;
            case CRTS:
                dbb_out = 0;
                dbb_stat |= F0;
                break;
            case KSTC:
                dbb_out = 0;
                dbb_stat |= F0;
                break;
            case RDSTS:
                dbb_out = 0x80;         //not ready
                dbb_stat |= (F0 | IBF);
                break;
            default:
                sim_printf("\n   ioc_cont1: Unknown command %02X PCX=%04X", dbb_cmd, PCX);
        }
        if (DEBUG)
            sim_printf("\n   ioc_cont1: DBB command write data=%02X PCX=%04X", dbb_cmd, PCX);
        return 0;
    }
}

/* end of ioc-cont.c */
