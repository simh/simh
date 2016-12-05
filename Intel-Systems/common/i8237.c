/*  i8237.c: Intel 8237 DMA adapter

    Copyright (c) 2016, William A. Beech

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

    11 Jul 16 - Original file.

    NOTES:

  
        Default is none.  Since all channel registers in the i8237 are 16-bit, transfers 
        are done as two 8-bit operations, low- then high-byte.

        Port addressing is as follows (Port offset = 0):

        Port    Mode    Command Function

        00      Write       Load DMAC Channel 0 Base and Current Address Regsiters
                Read        Read DMAC Channel 0 Current Address Register
        01      Write       Load DMAC Channel 0 Base and Current Word Count Registers
                Read        Read DMAC Channel 0 Current Word Count Register
        02      Write       Load DMAC Channel 1 Base and Current Address Regsiters
                Read        Read DMAC Channel 1 Current Address Register
        03      Write       Load DMAC Channel 1 Base and Current Word Count Registers
                Read        Read DMAC Channel 1 Current Word Count Register
        04      Write       Load DMAC Channel 2 Base and Current Address Regsiters
                Read        Read DMAC Channel 2 Current Address Register
        05      Write       Load DMAC Channel 2 Base and Current Word Count Registers
                Read        Read DMAC Channel 2 Current Word Count Register
        06      Write       Load DMAC Channel 3 Base and Current Address Regsiters
                Read        Read DMAC Channel 3 Current Address Register
        07      Write       Load DMAC Channel 3 Base and Current Word Count Registers
                Read        Read DMAC Channel 3 Current Word Count Register
        08      Write       Load DMAC Command Register
                Read        Read DMAC Status Register
        09      Write       Load DMAC Request Register
        0A      Write       Set/Reset DMAC Mask Register
        0B      Write       Load DMAC Mode Register
        0C      Write       Clear DMAC First/Last Flip-Flop
        0D      Write       DMAC Master Clear
        0F      Write       Load DMAC Mask Register

        Register usage is defined in the following paragraphs.

        Read/Write DMAC Address Registers

            Used to simultaneously load a channel's current-address register and base-address 
            register with the memory address of the first byte to be transferred. (The Channel 
            0 current/base address register must be loaded prior to initiating a diskette read 
            or write operation.)  Since each channel's address registers are 16 bits in length
            (64K address range), two "write address register" commands must be executed in 
            order to load the complete current/base address registers for any channel.

        Read/Write DMAC Word Count Registers

            The Write DMAC Word Count Register command is used to simultaneously load a 
            channel's current and base word-count registers with the number of bytes 
            to be transferred during a subsequent DMA operation.  Since the word-count 
            registers are 16-bits in length, two commands must be executed to load both 
            halves of the registers.

        Write DMAC Command Register

            The Write DMAC Command Register command loads an 8-bit byte into the 
            DMAC's command register to define the operating characteristics of the 
            DMAC. The functions of the individual bits in the command register are 
            defined in the following diagram. Note that only two bits within the 
            register are applicable to the controller; the remaining bits select 
            functions that are not supported and, accordingly, must always be set 
            to zero.

              7   6   5   4   3   2   1   0
            +---+---+---+---+---+---+---+---+
            | 0   0   0       0       0   0 |
            +---+---+---+---+---+---+---+---+
                          |       |
                          |       +---------- 0 CONTROLLER ENABLE
                          |                   1 CONTROLLER DISABLE
                          |
                          +------------------ 0 FIXED PRIORITY
                                              1 ROTATING PRIORITY

        Read DMAC Status Register Command

            The Read DMAC Status Register command accesses an 8-bit status byte that 
            identifies the DMA channels that have reached terminal count or that 
            have a pending DMA request.

              7   6   5   4   3   2   1   0
            +---+---+---+---+---+---+---+---+
            |         0               0     |
            +---+---+---+---+---+---+---+---+
              |   |       |   |   |       |
              |   |       |   |   |       +-- CHANNEL 0 TC
              |   |       |   |   +---------- CHANNEL 2 TC
              |   |       |   +-------------- CHANNEL 3 TC
              |   |       +------------------ CHANNEL 0 DMA REQUEST
              |   +-------------------------- CHANNEL 2 DMA REQUEST
              +------------------------------ CHANNEL 3 DMA REQUEST

        Write DMAC Request Register
          
            The data byte associated with the Write DMAC Request Register command 
            sets or resets a channel's associated request bit within the DMAC's 
            internal 4-bit request register.

              7   6   5   4   3   2   1   0
            +---+---+---+---+---+---+---+---+
            | X   X   X   X   X             |
            +---+---+---+---+---+---+---+---+
                                  |   |   | 
                                  |   +---+-- 00 SELECT CHANNEL 0
                                  |           01 SELECT CHANNEL 1
                                  |           10 SELECT CHANNEL 2
                                  |           11 SELECT CHANNEL 3
                                  |
                                  +---------- 0 RESET REQUEST BIT
                                              1 SET REQUEST BIT

        Set/Reset DMAC Mask Register

            Prior to a DREQ-initiated DMA transfer, the channel's mask bit must 
            be reset to enable recognition of the DREQ input. When the transfer 
            is complete (terminal count reached or external EOP applied) and 
            the channel is not programmed to autoinitialize, the channel's 
            mask bit is automatically set (disabling DREQ) and must be reset 
            prior to a subsequent DMA transfer. All four bits of the mask 
            register are set (disabling the DREQ inputs) by a DMAC master 
            clear or controller reset. Additionally, all four bits can be 
            set/reset by a single Write DMAC Mask Register command.


              7   6   5   4   3   2   1   0
            +---+---+---+---+---+---+---+---+
            | X   X   X   X   X             |
            +---+---+---+---+---+---+---+---+
                                  |   |   | 
                                  |   +---+-- 00 SELECT CHANNEL 0
                                  |           01 SELECT CHANNEL 1
                                  |           10 SELECT CHANNEL 2
                                  |           11 SELECT CHANNEL 3
                                  |
                                  +---------- 0 RESET REQUEST BIT
                                              1 SET REQUEST BIT

        Write DMAC Mode Register

            The Write DMAC Mode Register command is used to define the 
            operating mode characteristics for each DMA channel. Each 
            channel has an internal 6-bit mode register; the high-order 
            six bits of the associated data byte are written into the 
            mode register addressed by the two low-order bits.


              7   6   5   4   3   2   1   0
            +---+---+---+---+---+---+---+---+
            |                               |
            +---+---+---+---+---+---+---+---+
              |   |   |   |   |   |   |   |
              |   |   |   |   |   |   +---+-- 00 SELECT CHANNEL 0
              |   |   |   |   |   |           01 SELECT CHANNEL 1
              |   |   |   |   |   |           10 SELECT CHANNEL 2
              |   |   |   |   |   |           11 SELECT CHANNEL 3
              |   |   |   |   |   |
              |   |   |   |   +---+---------- 00 VERIFY TRANSFER
              |   |   |   |                   01 WRITE TRANSFER
              |   |   |   |                   10 READ TRANSFER
              |   |   |   |   
              |   |   |   +------------------ 0 AUTOINITIALIZE DISABLE
              |   |   |                       1 AUTOINITIALIZE ENABLE
              |   |   |
              |   |   +---------------------- 0 ADDRESS INCREMENT
              |   |                           1 ADDRESS DECREMENT
              |   |
              +---+-------------------------- 00 DEMAND MODE
                                              01 SINGLE MODE
                                              10 BLOCK MODE

        Clear DMAC First/Last Flip-Flop

                The Clear DMAC First/Last Flip-Flop command initializes 
                the DMAC's internal first/last flip-flop so that the 
                next byte written to or re~d from the 16-bit address 
                or word-count registers is the low-order byte.  The 
                flip-flop is toggled with each register access so that 
                a second register read or write command accesses the 
                high-order byte.

        DMAC Master Clear

            The DMAC Master Clear command clears the DMAC's command, status, 
            request, and temporary registers to zero, initializes the 
            first/last flip-flop, and sets the four channel mask bits in 
            the mask register to disable all DMA requests (i.e., the DMAC 
            is placed in an idle state).

        Write DMAC Mask Register

            The Write DMAC Mask Register command allows all four bits of the 
            DMAC's mask register to be written with a single command.

              7   6   5   4   3   2   1   0
            +---+---+---+---+---+---+---+---+
            | X   X   X   X           X     |
            +---+---+---+---+---+---+---+---+
                              |   |       |
                              |   |       +-- 0 CLEAR CHANNEL 0 MASK BIT
                              |   |           1 SET CHANNEL 0 MASK BIT
                              |   |    
                              |   +---------- 0 CLEAR CHANNEL 2 MASK BIT
                              |               1 SET CHANNEL 2 MASK BIT
                              |
                              +-------------- 0 CLEAR CHANNEL 3 MASK BIT
                                              1 SET CHANNEL 3 MASK BIT

*/

#include "system_defs.h"

/* external globals */

extern uint16 port;                     //port called in dev_table[port]

/* globals */

int32 i8237_devnum = 0;                 //actual number of 8253 instances + 1
uint16 i8237_port[4];                   //base port registered to each instance

/* function prototypes */

t_stat i8237_svc(UNIT *uptr);
t_stat i8237_reset(DEVICE *dptr, uint16 base);
void i8237_reset1(int32 devnum);
t_stat i8237_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
uint8 i8237_r0x(t_bool io, uint8 data);
uint8 i8237_r1x(t_bool io, uint8 data);
uint8 i8237_r2x(t_bool io, uint8 data);
uint8 i8237_r3x(t_bool io, uint8 data);
uint8 i8237_r4x(t_bool io, uint8 data);
uint8 i8237_r5x(t_bool io, uint8 data);
uint8 i8237_r6x(t_bool io, uint8 data);
uint8 i8237_r7x(t_bool io, uint8 data);
uint8 i8237_r8x(t_bool io, uint8 data);
uint8 i8237_r9x(t_bool io, uint8 data);
uint8 i8237_rAx(t_bool io, uint8 data);
uint8 i8237_rBx(t_bool io, uint8 data);
uint8 i8237_rCx(t_bool io, uint8 data);
uint8 i8237_rDx(t_bool io, uint8 data);
uint8 i8237_rEx(t_bool io, uint8 data);
uint8 i8237_rFx(t_bool io, uint8 data);

/* external function prototypes */

extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8), uint16);

/* 8237 physical register definitions */

uint16 i8237_r0[4];                     // 8237 ch 0 address register
uint16 i8237_r1[4];                     // 8237 ch 0 count register
uint16 i8237_r2[4];                     // 8237 ch 1 address register
uint16 i8237_r3[4];                     // 8237 ch 1 count register
uint16 i8237_r4[4];                     // 8237 ch 2 address register
uint16 i8237_r5[4];                     // 8237 ch 2 count register
uint16 i8237_r6[4];                     // 8237 ch 3 address register
uint16 i8237_r7[4];                     // 8237 ch 3 count register
uint8 i8237_r8[4];                      // 8237 status register
uint8 i8237_r9[4];                      // 8237 command register
uint8 i8237_rA[4];                      // 8237 mode register
uint8 i8237_rB[4];                      // 8237 mask register
uint8 i8237_rC[4];                      // 8237 request register
uint8 i8237_rD[4];                      // 8237 first/last ff
uint8 i8237_rE[4];                      // 8237 
uint8 i8237_rF[4];                      // 8237 

/* i8237 physical register definitions */

uint16 i8237_sr[4];                      // isbc-208 segment register
uint8 i8237_i[4];                        // iSBC-208 interrupt register
uint8 i8237_a[4];                        // iSBC-208 auxillary port register

/* i8237 Standard SIMH Device Data Structures - 1 unit */

UNIT i8237_unit[] = {
    { UDATA (0, 0, 0) ,20 },            /* i8237 0 */
    { UDATA (0, 0, 0) ,20 },            /* i8237 1 */
    { UDATA (0, 0, 0) ,20 },            /* i8237 2 */
    { UDATA (0, 0, 0) ,20 }             /* i8237 3 */
};


REG i8237_reg[] = {
    { HRDATA (CH0ADR, i8237_r0[devnum], 16) },
    { HRDATA (CH0CNT, i8237_r1[devnum], 16) },
    { HRDATA (CH1ADR, i8237_r2[devnum], 16) },
    { HRDATA (CH1CNT, i8237_r3, 16) },
    { HRDATA (CH2ADR, i8237_r4, 16) },
    { HRDATA (CH2CNT, i8237_r5, 16) },
    { HRDATA (CH3ADR, i8237_r6, 16) },
    { HRDATA (CH3CNT, i8237_r7, 16) },
    { HRDATA (STAT37, i8237_r8, 8) },
    { HRDATA (CMD37, i8237_r9, 8) },
    { HRDATA (MODE, i8237_rA, 8) },
    { HRDATA (MASK, i8237_rB, 8) },
    { HRDATA (REQ, i8237_rC, 8) },
    { HRDATA (FF, i8237_rD, 8) },
    { HRDATA (SEGREG, i8237_sr, 8) },
    { HRDATA (AUX, i8237_a, 8) },
    { HRDATA (INT, i8237_i, 8) },
    { NULL }
};

MTAB i8237_mod[] = {
    { 0 }
};

DEBTAB i8237_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { "REG", DEBUG_reg },
    { NULL }
};

DEVICE i8237_dev = {
    "I8237",                   //name 
    i8237_unit,               //units 
    i8237_reg,                //registers 
    i8237_mod,                //modifiers
    1,                    //numunits 
    16,                         //aradix  
    32,                         //awidth  
    1,                          //aincr  
    16,                         //dradix  
    8,                          //dwidth
    NULL,                       //examine  
    NULL,                       //deposit  
//    &i8237_reset,             //deposit 
    NULL,       //reset
    NULL,                       //boot
    NULL,               //attach
    NULL,                       //detach
    NULL,                       //ctxt     
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    0,                          //dctrl 
//    DEBUG_flow + DEBUG_read + DEBUG_write,              //dctrl 
    i8237_debug,              //debflags
    NULL,                       //msize
    NULL                        //lname
};

/* Service routines to handle simulator functions */

/* service routine - actually does the simulated DMA */

t_stat i8237_svc(UNIT *uptr)
{
    sim_printf("uptr=%08X\n", uptr);
    sim_activate (&i8237_unit[uptr->u6], i8237_unit[uptr->u6].wait);
    return SCPE_OK;
}

/* Reset routine */

t_stat i8237_reset(DEVICE *dptr, uint16 base)
{
     if (i8237_devnum > I8237_NUM) {
        sim_printf("i8237_reset: too many devices!\n");
        return SCPE_MEM;
    }
    sim_printf("   8237 Reset\n");
    sim_printf("   8237: Registered at %03X\n", base);
    i8237_port[i8237_devnum] = reg_dev(i8237_r0x, base); 
    reg_dev(i8237_r1x, base + 1); 
    reg_dev(i8237_r2x, base + 2); 
    reg_dev(i8237_r3x, base + 3); 
    reg_dev(i8237_r4x, base + 4); 
    reg_dev(i8237_r5x, base + 5); 
    reg_dev(i8237_r6x, base + 6); 
    reg_dev(i8237_r7x, base + 7); 
    reg_dev(i8237_r8x, base + 8); 
    reg_dev(i8237_r9x, base + 9); 
    reg_dev(i8237_rAx, base + 10); 
    reg_dev(i8237_rBx, base + 11); 
    reg_dev(i8237_rCx, base + 12); 
    reg_dev(i8237_rDx, base + 13); 
    reg_dev(i8237_rEx, base + 14); 
    reg_dev(i8237_rFx, base + 15); 
    sim_printf("   8237 Reset\n");
    sim_printf("   8237: Registered at %03X\n", base);
    sim_activate (&i8237_unit[i8237_devnum], i8237_unit[i8237_devnum].wait); /* activate unit */
    if ((i8237_dev.flags & DEV_DIS) == 0) 
        i8237_reset1(i8237_devnum);
    i8237_devnum++;
    return SCPE_OK;
}

uint8 i8237_get_dn(void)
{
    int i;

    for (i=0; i<I8237_NUM; i++)
        if (port >=i8237_port[i] && port <= i8237_port[i] + 16)
            return i;
    sim_printf("i8237_get_dn: port %03X not in 8237 device table\n", port);
    return 0xFF;
}

void i8237_reset1(int32 devnum)
{
    int32 i;
    UNIT *uptr;

    uptr = i8237_dev[devnum].units;
    if (uptr->capac == 0) {         /* if not configured */
        uptr->capac = 0;            /* initialize unit */
        uptr->u3 = 0; 
        uptr->u4 = 0;
        uptr->u5 = 0;
        uptr->u6 = i;               /* unit number - only set here! */
        sim_activate (&i8237_unit[devnum], i8237_unit[devnum].wait);
    }
    i8237_r8[devnum] = 0;           /* status */
    i8237_r9[devnum] = 0;           /* command */
    i8237_rB[devnum] = 0x0F;        /* mask */
    i8237_rC[devnum] = 0;           /* request */
    i8237_rD[devnum] = 0;           /* first/last FF */
}


/* i8237 set mode = 8- or 16-bit data bus */
/* always 8-bit mode for current simulators */

t_stat i8237_set_mode(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    sim_debug (DEBUG_flow, &i8237_dev, "   i8237_set_mode: Entered with val=%08XH uptr->flags=%08X\n", val, uptr->flags);
    sim_debug (DEBUG_flow, &i8237_dev, "   i8237_set_mode: Done\n");
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.

    Each function is passed an 'io' flag, where 0 means a read from
    the port, and 1 means a write to the port.  On input, the actual
    input is passed as the return value, on output, 'data' is written
    to the device.
*/

uint8 i8237_r0x(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read current address CH 0 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r0[%d](H) read as %04X\n", devnum, i8237_r0[devnum]);
                return (i8237_r0[devnum] >> 8);
            } else {                        /* low byte */
                i8237_rD++;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r0[%d](L) read as %04X\n", devnum, i8237_r0[devnum]);
                return (i8237_r0[devnum] & 0xFF);
            }
        } else {                            /* write base & current address CH 0 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                i8237_r0[devnum] |= (data << 8);
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r0[%d](H) set to %04X\n", devnum, i8237_r0[devnum]);
            } else {                        /* low byte */
                i8237_rD++;
                i8237_r0[devnum] = data & 0xFF;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r0[%d](L) set to %04X\n"devnum, , i8237_r0[devnum]);
            }
            return 0;
        }
    }
}

uint8 i8237_r1x(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read current word count CH 0 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r1[%d](H) read as %04X\n", devnum, i8237_r1[devnum]);
                return (i8237_r1[devnum][devnum] >> 8);
            } else {                        /* low byte */
                i8237_rD++;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r1[%d](L) read as %04X\n", devnum, i8237_r1[devnum]);
                return (i8237_r1[devnum] & 0xFF);
            }
        } else {                            /* write base & current address CH 0 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                i8237_r1[devnum] |= (data << 8);
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r1[%d](H) set to %04X\n", devnum, i8237_r1[devnum]);
            } else {                        /* low byte */
                i8237_rD++;
                i8237_r1[devnum] = data & 0xFF;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r1[%d](L) set to %04X\n", devnum, i8237_r1[devnum]);
            }
            return 0;
        }
    }
}

uint8 i8237_r2x(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read current address CH 1 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r2[%d](H) read as %04X\n", devnum, i8237_r2[devnum]);
                return (i8237_r2[devnum] >> 8);
            } else {                        /* low byte */
                i8237_rD++;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r2[%d](L) read as %04X\n", devnum, i8237_r2[devnum]);
                return (i8237_r2[devnum] & 0xFF);
            }
        } else {                            /* write base & current address CH 1 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                i8237_r2[devnum] |= (data << 8);
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r2[%d](H) set to %04X\n", devnum, i8237_r2[devnum]);
            } else {                        /* low byte */
                i8237_rD++;
                i8237_r2[devnum] = data & 0xFF;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r2[%d](L) set to %04X\n", devnum, i8237_r2[devnum]);
            }
            return 0;
        }
    }
}

uint8 i8237_r3x(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read current word count CH 1 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r3(H) read as %04X\n", i8237_r3);
                return (i8237_r3 >> 8);
            } else {                        /* low byte */
                i8237_rD++;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r3(L) read as %04X\n", i8237_r3);
                return (i8237_r3 & 0xFF);
            }
        } else {                            /* write base & current address CH 1 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                i8237_r3 |= (data << 8);
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r3(H) set to %04X\n", i8237_r3);
            } else {                        /* low byte */
                i8237_rD++;
                i8237_r3 = data & 0xFF;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r3(L) set to %04X\n", i8237_r3);
            }
            return 0;
        }
    }
}

uint8 i8237_r4x(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read current address CH 2 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r4(H) read as %04X\n", i8237_r4);
                return (i8237_r4 >> 8);
            } else {                        /* low byte */
                i8237_rD++;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r4(L) read as %04X\n", i8237_r4);
                return (i8237_r4 & 0xFF);
            }
        } else {                            /* write base & current address CH 2 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                i8237_r4 |= (data << 8);
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r4(H) set to %04X\n", i8237_r4);
            } else {                        /* low byte */
                i8237_rD++;
                i8237_r4 = data & 0xFF;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r4(L) set to %04X\n", i8237_r4);
            }
            return 0;
        }
    }
}

uint8 i8237_r5x(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read current word count CH 2 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r5(H) read as %04X\n", i8237_r5);
                return (i8237_r5 >> 8);
            } else {                        /* low byte */
                i8237_rD++;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r5(L) read as %04X\n", i8237_r5);
                return (i8237_r5 & 0xFF);
            }
        } else {                            /* write base & current address CH 2 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                i8237_r5 |= (data << 8);
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r5(H) set to %04X\n", i8237_r5);
            } else {                        /* low byte */
                i8237_rD++;
                i8237_r5 = data & 0xFF;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r5(L) set to %04X\n", i8237_r5);
            }
            return 0;
        }
    }
}

uint8 i8237_r6x(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read current address CH 3 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r6(H) read as %04X\n", i8237_r6);
                return (i8237_r6 >> 8);
            } else {                        /* low byte */
                i8237_rD++;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r6(L) read as %04X\n", i8237_r6);
                return (i8237_r6 & 0xFF);
            }
        } else {                            /* write base & current address CH 3 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                i8237_r6 |= (data << 8);
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r6(H) set to %04X\n", i8237_r6);
            } else {                        /* low byte */
                i8237_rD++;
                i8237_r6 = data & 0xFF;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r6(L) set to %04X\n", i8237_r6);
            }
            return 0;
        }
    }
}

uint8 i8237_r7x(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read current word count CH 3 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r7(H) read as %04X\n", i8237_r7);
                return (i8237_r7 >> 8);
            } else {                        /* low byte */
                i8237_rD++;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r7(L) read as %04X\n", i8237_r7);
                return (i8237_r7 & 0xFF);
            }
        } else {                            /* write base & current address CH 3 */
            if (i8237_rD) {                 /* high byte */
                i8237_rD = 0;
                i8237_r7 |= (data << 8);
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r7(H) set to %04X\n", i8237_r7);
            } else {                        /* low byte */
                i8237_rD++;
                i8237_r7 = data & 0xFF;
                sim_debug (DEBUG_reg, &i8237_dev, "i8237_r7(L) set to %04X\n", i8237_r7);
            }
            return 0;
        }
    }
}

uint8 i8237_r8x(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read status register */
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r8 (status) read as %02X\n", i8237_r8);
            return (i8237_r8);
        } else {                            /* write command register */
            i8237_r9 = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r9 (command) set to %02X\n", i8237_r9);
            return 0;
        }
    }
}

uint8 i8237_r9x(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {
            sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_r9\n");
            return 0;
        } else {                            /* write request register */
            i8237_rC = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_rC (request) set to %02X\n", i8237_rC);
            return 0;
        }
    }
}

uint8 i8237_rAx(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {
            sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rA\n");
            return 0;
        } else {                            /* write single mask register */
            switch(data & 0x03) {
            case 0:
                if (data & 0x04)
                    i8237_rB |= 1;
                else
                    i8237_rB &= ~1;
                break;
            case 1:
                if (data & 0x04)
                    i8237_rB |= 2;
                else
                    i8237_rB &= ~2;
                break;
            case 2:
                if (data & 0x04)
                    i8237_rB |= 4;
                else
                    i8237_rB &= ~4;
                break;
            case 3:
                if (data & 0x04)
                    i8237_rB |= 8;
                else
                    i8237_rB &= ~8;
                break;
            }
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_rB (mask) set to %02X\n", i8237_rB);
            return 0;
        }
    }
}

uint8 i8237_rBx(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {
            sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rB\n");
            return 0;
        } else {                            /* write mode register */
            i8237_rA = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_rA (mode) set to %02X\n", i8237_rA);
            return 0;
        }
    }
}

uint8 i8237_rCx(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {
            sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rC\n");
            return 0;
        } else {                            /* clear byte pointer FF */
            i8237_rD = 0;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_rD (FF) cleared\n");
            return 0;
        }
    }
}

uint8 i8237_rDx(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read temporary register */
            sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rD\n");
            return 0;
        } else {                            /* master clear */
            i8237_reset1();
            sim_debug (DEBUG_reg, &i8237_dev, "i8237 master clear\n");
            return 0;
        }
    }
}

uint8 i8237_rEx(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {
            sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rE\n");
            return 0;
        } else {                            /* clear mask register */
            i8237_rB = 0;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_rB (mask) cleared\n");
            return 0;
        }
    }
}

uint8 i8237_rFx(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8237_get_dn()) != 0xFF) {
        if (io == 0) {
            sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rF\n");
            return 0;
        } else {                            /* write all mask register bits */
            i8237_rB = data & 0x0F;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_rB (mask) set to %02X\n", i8237_rB);
            return 0;
        }
    }
}

/* end of i8237.c */
