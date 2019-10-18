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

//dbb status flag bits
#define OBF     1
#define IBF     2
#define F0      4
#define CD      8

//system status bits
#define IIM     16                      //illegal interrupt mask
#define IDT     32                      //illegal data transfer
#define IC      64                      //illegal command
#define DE      128                     //device error

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

extern uint16    PCX;

/* function prototypes */

t_stat ioc_cont_cfg(uint8 base, uint8 devnum);
t_stat ioc_cont_reset (DEVICE *dptr);
uint8 ioc_cont0(t_bool io, uint8 data, uint8 devnum);    /* ioc_cont*/
uint8 ioc_cont1(t_bool io, uint8 data, uint8 devnum);    /* ioc_cont*/

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);

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
    ioc_cont_reset,     //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    ioc_cont_debug,     //debflags
    NULL,               //msize
    NULL                //lname
};

// ioc_cont configuration

t_stat ioc_cont_cfg(uint8 base, uint8 devnum)
{
    sim_printf("    ioc-cont[%d]: at base 0%02XH\n",
        devnum, base & 0xFF);
    reg_dev(ioc_cont0, base, devnum); 
    reg_dev(ioc_cont1, base + 1, devnum); 
    return SCPE_OK;
}

/* Reset routine */

t_stat ioc_cont_reset(DEVICE *dptr)
{
    dbb_stat = 0x00;                /* clear DBB status */
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* IOC data port functions */

uint8 ioc_cont0(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read data port */
        dbb_stat &= ~OBF;               //reset OBF flag
        return dbb_out;
    } else {                            /* write data port */
        dbb_in = data;
        dbb_stat |= IBF;
        return 0;
    }
}

/* IOC control port functions */

uint8 ioc_cont1(t_bool io, uint8 data, uint8 devnum)
{
    int temp;

    if (io == 0) {                      /* read status port */
        if ((dbb_stat & F0) && (dbb_stat & IBF)) {
            return dbb_stat;
        } 
        if ((dbb_stat & F0) && (dbb_stat & OBF)) {
            temp = dbb_stat;
            dbb_stat &= ~OBF;           //reset OBF flag
            return temp;
        } 
        if (dbb_stat & F0) {
            return dbb_stat;
        }
        return dbb_stat;
    } else {                            /* write command port */
        dbb_stat |= F0;
        dbb_cmd = data;
        switch(dbb_cmd){
            case PACIFY:                //reset IOC and its devices
                dbb_stat = 0;
                break;
            case ERESET:                //reset device-generated error(not used by std devices)
                break;
            case SYSTAT:                //returns subsystem status byte to master
                dbb_out = 0;
                dbb_stat |= OBF;
                dbb_stat &= ~CD;
                break;
            case DSTAT:                 //returns device status to master
                break;
            case SRQDAK:                //enables input of device int ack mask from master
                break;
            case SRQACK:                //clears IOC subsystem int req
                break;
            case SRQ:                   //tests ability of IOC to forward an int req to master
                break;
            case DECHO:                 //tests ability of IOC to echo data byte sent by master
                break;
            case CSMEM:                 //requests IOC to checksum onboard ROM
                break;
            case TRAM:                  //requests IOC to test onboard RAM
                break;
            case SINT:                  //enables specified device int from IOC
                break;
            case CRTC:                  //requests data byte output to CRT
                break;
            case CRTS:                  //return CRT status byte to master
                dbb_out = 0;
                dbb_stat |= F0;
                break;
            case KEYC:                  //request data byte from KBD
                break;
            case KSTC:                  //return KBD status byte to master
                dbb_out = 0;
                break;
            case WPBC:                  //enables input of first 5 bytes of IOPB
                break;
            case WPBCC:                 //enables input of 4 bytes that follow WPDC
                break;
            case WDBC:                  //enables input of diskette write bytes from master
                break;
            case RDBC:                  //enables output of diskette read bytes to master.
                break;
            case RRSTS:                 //returns diskette result byte to master
                break;
            case RDSTS:                 //returns diskette device status byte to master
                dbb_out = 0x80;         //not ready
                dbb_stat |= IBF;
                break;
            default:
                sim_printf("   IOC-CONT: Unknown command %02X PCX=%04X\n", dbb_cmd, PCX);
        }
        return 0;
    }
}

/* end of ioc-cont.c */
