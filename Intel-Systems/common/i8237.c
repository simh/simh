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

/* internal function prototypes */

t_stat i8237_svc (UNIT *uptr);
t_stat i8237_reset (DEVICE *dptr);
void i8237_reset_dev (uint8 devnum);
t_stat i8237_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
uint8 i8237_r0x(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_r1x(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_r2x(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_r3x(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_r4x(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_r5x(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_r6x(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_r7x(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_r8x(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_r9x(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_rAx(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_rBx(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_rCx(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_rDx(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_rEx(t_bool io, uint8 data, uint8 devnum);
uint8 i8237_rFx(t_bool io, uint8 data, uint8 devnum);

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);

/* globals */

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

uint16 i8237_sr[4];                     // 8237 segment register
uint8 i8237_i[4];                       // 8237 interrupt register
uint8 i8237_a[4];                       // 8237 auxillary port register

/* i8237 Standard SIMH Device Data Structures - 4 units */

UNIT i8237_unit[] = {
    { UDATA (0, 0, 0) ,20 },            /* i8237 0 */
    { UDATA (0, 0, 0) ,20 },            /* i8237 1 */
    { UDATA (0, 0, 0) ,20 },            /* i8237 2 */
    { UDATA (0, 0, 0) ,20 }             /* i8237 3 */
};

REG i8237_reg[] = {
    { HRDATA (CH0ADR0, i8237_r0[0], 16) }, /* i8237 0 */
    { HRDATA (CH0CNT0, i8237_r1[0], 16) },
    { HRDATA (CH1ADR0, i8237_r2[0], 16) },
    { HRDATA (CH1CNT0, i8237_r3[0], 16) },
    { HRDATA (CH2ADR0, i8237_r4[0], 16) },
    { HRDATA (CH2CNT0, i8237_r5[0], 16) },
    { HRDATA (CH3ADR0, i8237_r6[0], 16) },
    { HRDATA (CH3CNT0, i8237_r7[0], 16) },
    { HRDATA (STAT370, i8237_r8[0], 8) },
    { HRDATA (CMD370, i8237_r9[0], 8) },
    { HRDATA (MODE0, i8237_rA[0], 8) },
    { HRDATA (MASK0, i8237_rB[0], 8) },
    { HRDATA (REQ0, i8237_rC[0], 8) },
    { HRDATA (FF0, i8237_rD[0], 8) },
    { HRDATA (SEGREG0, i8237_sr[0], 8) },
    { HRDATA (AUX0, i8237_a[0], 8) },
    { HRDATA (INT0, i8237_i[0], 8) },
    { HRDATA (CH0ADR1, i8237_r0[1], 16) }, /* i8237 1 */
    { HRDATA (CH0CNT1, i8237_r1[1], 16) },
    { HRDATA (CH1ADR1, i8237_r2[1], 16) },
    { HRDATA (CH1CNT1, i8237_r3[1], 16) },
    { HRDATA (CH2ADR1, i8237_r4[1], 16) },
    { HRDATA (CH2CNT1, i8237_r5[1], 16) },
    { HRDATA (CH3ADR1, i8237_r6[1], 16) },
    { HRDATA (CH3CNT1, i8237_r7[1], 16) },
    { HRDATA (STAT371, i8237_r8[1], 8) },
    { HRDATA (CMD371, i8237_r9[1], 8) },
    { HRDATA (MODE1, i8237_rA[1], 8) },
    { HRDATA (MASK1, i8237_rB[1], 8) },
    { HRDATA (REQ1, i8237_rC[1], 8) },
    { HRDATA (FF1, i8237_rD[1], 8) },
    { HRDATA (SEGREG1, i8237_sr[1], 8) },
    { HRDATA (AUX1, i8237_a[1], 8) },
    { HRDATA (INT1, i8237_i[1], 8) },
    { HRDATA (CH0ADR2, i8237_r0[2], 16) }, /* i8237 2 */
    { HRDATA (CH0CNT2, i8237_r1[2], 16) },
    { HRDATA (CH1ADR2, i8237_r2[2], 16) },
    { HRDATA (CH1CNT2, i8237_r3[2], 16) },
    { HRDATA (CH2ADR2, i8237_r4[2], 16) },
    { HRDATA (CH2CNT2, i8237_r5[2], 16) },
    { HRDATA (CH3ADR2, i8237_r6[2], 16) },
    { HRDATA (CH3CNT2, i8237_r7[2], 16) },
    { HRDATA (STAT372, i8237_r8[2], 8) },
    { HRDATA (CMD372, i8237_r9[2], 8) },
    { HRDATA (MODE2, i8237_rA[2], 8) },
    { HRDATA (MASK2, i8237_rB[2], 8) },
    { HRDATA (REQ2, i8237_rC[2], 8) },
    { HRDATA (FF2, i8237_rD[2], 8) },
    { HRDATA (SEGREG2, i8237_sr[2], 8) },
    { HRDATA (AUX2, i8237_a[2], 8) },
    { HRDATA (INT2, i8237_i[2], 8) },
    { HRDATA (CH0ADR3, i8237_r0[3], 16) }, /* i8237 3 */
    { HRDATA (CH0CNT3, i8237_r1[3], 16) },
    { HRDATA (CH1ADR3, i8237_r2[3], 16) },
    { HRDATA (CH1CNT3, i8237_r3[3], 16) },
    { HRDATA (CH2ADR3, i8237_r4[3], 16) },
    { HRDATA (CH2CNT3, i8237_r5[3], 16) },
    { HRDATA (CH3ADR3, i8237_r6[3], 16) },
    { HRDATA (CH3CNT3, i8237_r7[3], 16) },
    { HRDATA (STAT373, i8237_r8[3], 8) },
    { HRDATA (CMD373, i8237_r9[3], 8) },
    { HRDATA (MODE3, i8237_rA[3], 8) },
    { HRDATA (MASK3, i8237_rB[3], 8) },
    { HRDATA (REQ3, i8237_rC[3], 8) },
    { HRDATA (FF3, i8237_rD[3], 8) },
    { HRDATA (SEGREG3, i8237_sr[3], 8) },
    { HRDATA (AUX3, i8237_a[3], 8) },
    { HRDATA (INT3, i8237_i[3], 8) },
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
    "8237",                     //name 
    i8237_unit,                 //units 
    i8237_reg,                  //registers 
    i8237_mod,                  //modifiers
    I8237_NUM,                  //numunits 
    16,                         //aradix  
    32,                         //awidth  
    1,                          //aincr  
    16,                         //dradix  
    8,                          //dwidth
    NULL,                       //examine  
    NULL,                       //deposit  
    i8237_reset,                //reset
    NULL,                       //boot
    NULL,                       //attach
    NULL,                       //detach
    NULL,                       //ctxt     
    0,                          //flags 
    0,                          //dctrl 
    i8237_debug,                //debflags
    NULL,                       //msize
    NULL                        //lname
};

/* Service routines to handle simulator functions */

// i8251 configuration

t_stat i8237_cfg(uint8 base, uint8 devnum)
{
    sim_printf("    i8237[%d]: at base port 0%02XH\n",
        devnum, base & 0xFF);
    reg_dev(i8237_r1x, base + 1, devnum); 
    reg_dev(i8237_r2x, base + 2, devnum); 
    reg_dev(i8237_r3x, base + 3, devnum); 
    reg_dev(i8237_r4x, base + 4, devnum); 
    reg_dev(i8237_r5x, base + 5, devnum); 
    reg_dev(i8237_r6x, base + 6, devnum); 
    reg_dev(i8237_r7x, base + 7, devnum); 
    reg_dev(i8237_r8x, base + 8, devnum); 
    reg_dev(i8237_r9x, base + 9, devnum); 
    reg_dev(i8237_rAx, base + 10, devnum); 
    reg_dev(i8237_rBx, base + 11, devnum); 
    reg_dev(i8237_rCx, base + 12, devnum); 
    reg_dev(i8237_rDx, base + 13, devnum); 
    reg_dev(i8237_rEx, base + 14, devnum); 
    reg_dev(i8237_rFx, base + 15, devnum); 
    return SCPE_OK;
}

/* service routine - actually does the simulated DMA */

t_stat i8237_svc(UNIT *uptr)
{
    sim_activate (&i8237_unit[uptr->u6], i8237_unit[uptr->u6].wait);
    return SCPE_OK;
}

/* Reset routine */

t_stat i8237_reset(DEVICE *dptr)
{
    uint8 devnum;
    
    for (devnum=0; devnum<I8237_NUM; devnum++) {
        i8237_reset_dev(devnum);
        sim_activate (&i8237_unit[devnum], i8237_unit[devnum].wait); /* activate unit */
    }
    return SCPE_OK;
}

void i8237_reset_dev(uint8 devnum)
{
    int32 i;
    UNIT *uptr;
    static int flag = 1;

    for (i = 0; i < 1; i++) {     /* handle all units */
        uptr = i8237_dev.units + i;
        if (uptr->capac == 0) {         /* if not configured */
//            sim_printf("   SBC208%d: Not configured\n", i);
//            if (flag) {
//                sim_printf("      ALL: \"set isbc208 en\"\n");
//                sim_printf("      EPROM: \"att isbc2080 <filename>\"\n");
//                flag = 0;
//            }
            uptr->capac = 0;            /* initialize unit */
            uptr->u3 = 0; 
            uptr->u4 = 0;
            uptr->u5 = 0;
            uptr->u6 = i;               /* unit number - only set here! */
            sim_activate (&i8237_unit[uptr->u6], i8237_unit[uptr->u6].wait);
        } else {
//            sim_printf("   SBC208%d: Configured, Attached to %s\n", i, uptr->filename);
        }
    }
    devnum = uptr->u6;
    i8237_r8[devnum] = 0;                       /* status */
    i8237_r9[devnum] = 0;                       /* command */
    i8237_rB[devnum] = 0x0F;                    /* mask */
    i8237_rC[devnum] = 0;                       /* request */
    i8237_rD[devnum] = 0;                       /* first/last FF */
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

uint8 i8237_r0x(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current address CH 0 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r0[devnum](H) read as %04X\n", i8237_r0[devnum]);
            return (i8237_r0[devnum] >> 8);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r0[devnum](L) read as %04X\n", i8237_r0[devnum]);
            return (i8237_r0[devnum] & 0xFF);
        }
    } else {                            /* write base & current address CH 0 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            i8237_r0[devnum] |= (data << 8);
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r0[devnum](H) set to %04X\n", i8237_r0[devnum]);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            i8237_r0[devnum] = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r0[devnum](L) set to %04X\n", i8237_r0[devnum]);
        }
    }
    return 0;
}

uint8 i8237_r1x(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current word count CH 0 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r1[devnum](H) read as %04X\n", i8237_r1[devnum]);
            return (i8237_r1[devnum] >> 8);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r1[devnum](L) read as %04X\n", i8237_r1[devnum]);
            return (i8237_r1[devnum] & 0xFF);
        }
    } else {                            /* write base & current address CH 0 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            i8237_r1[devnum] |= (data << 8);
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r1[devnum](H) set to %04X\n", i8237_r1[devnum]);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            i8237_r1[devnum] = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r1[devnum](L) set to %04X\n", i8237_r1[devnum]);
        }
    }
    return 0;
}

uint8 i8237_r2x(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current address CH 1 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r2[devnum](H) read as %04X\n", i8237_r2[devnum]);
            return (i8237_r2[devnum] >> 8);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r2[devnum](L) read as %04X\n", i8237_r2[devnum]);
            return (i8237_r2[devnum] & 0xFF);
        }
    } else {                            /* write base & current address CH 1 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            i8237_r2[devnum] |= (data << 8);
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r2[devnum](H) set to %04X\n", i8237_r2[devnum]);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            i8237_r2[devnum] = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r2[devnum](L) set to %04X\n", i8237_r2[devnum]);
        }
    }
    return 0;
}

uint8 i8237_r3x(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current word count CH 1 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r3[devnum](H) read as %04X\n", i8237_r3[devnum]);
            return (i8237_r3[devnum] >> 8);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r3[devnum](L) read as %04X\n", i8237_r3[devnum]);
            return (i8237_r3[devnum] & 0xFF);
        }
    } else {                            /* write base & current address CH 1 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            i8237_r3[devnum] |= (data << 8);
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r3[devnum](H) set to %04X\n", i8237_r3[devnum]);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            i8237_r3[devnum] = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r3[devnum](L) set to %04X\n", i8237_r3[devnum]);
        }
    }
    return 0;
}

uint8 i8237_r4x(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current address CH 2 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r4[devnum](H) read as %04X\n", i8237_r4[devnum]);
            return (i8237_r4[devnum] >> 8);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r4[devnum](L) read as %04X\n", i8237_r4[devnum]);
            return (i8237_r4[devnum] & 0xFF);
        }
    } else {                            /* write base & current address CH 2 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            i8237_r4[devnum] |= (data << 8);
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r4[devnum](H) set to %04X\n", i8237_r4[devnum]);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            i8237_r4[devnum] = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r4[devnum](L) set to %04X\n", i8237_r4[devnum]);
        }
    }
    return 0;
}

uint8 i8237_r5x(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current word count CH 2 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r5[devnum](H) read as %04X\n", i8237_r5[devnum]);
            return (i8237_r5[devnum] >> 8);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r5[devnum](L) read as %04X\n", i8237_r5[devnum]);
            return (i8237_r5[devnum] & 0xFF);
        }
    } else {                            /* write base & current address CH 2 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            i8237_r5[devnum] |= (data << 8);
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r5[devnum](H) set to %04X\n", i8237_r5[devnum]);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            i8237_r5[devnum] = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r5[devnum](L) set to %04X\n", i8237_r5[devnum]);
        }
    }
    return 0;
}

uint8 i8237_r6x(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current address CH 3 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r6[devnum](H) read as %04X\n", i8237_r6[devnum]);
            return (i8237_r6[devnum] >> 8);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r6[devnum](L) read as %04X\n", i8237_r6[devnum]);
            return (i8237_r6[devnum] & 0xFF);
        }
    } else {                            /* write base & current address CH 3 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            i8237_r6[devnum] |= (data << 8);
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r6[devnum](H) set to %04X\n", i8237_r6[devnum]);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            i8237_r6[devnum] = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r6[devnum](L) set to %04X\n", i8237_r6[devnum]);
        }
    }
    return 0;
}

uint8 i8237_r7x(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current word count CH 3 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r7[devnum](H) read as %04X\n", i8237_r7[devnum]);
            return (i8237_r7[devnum] >> 8);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r7[devnum](L) read as %04X\n", i8237_r7[devnum]);
            return (i8237_r7[devnum] & 0xFF);
        }
    } else {                            /* write base & current address CH 3 */
        if (i8237_rD[devnum]) {                 /* high byte */
            i8237_rD[devnum] = 0;
            i8237_r7[devnum] |= (data << 8);
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r7[devnum](H) set to %04X\n", i8237_r7[devnum]);
        } else {                        /* low byte */
            i8237_rD[devnum]++;
            i8237_r7[devnum] = data & 0xFF;
            sim_debug (DEBUG_reg, &i8237_dev, "i8237_r7[devnum](L) set to %04X\n", i8237_r7[devnum]);
        }
    }
    return 0;
}

uint8 i8237_r8x(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read status register */
        sim_debug (DEBUG_reg, &i8237_dev, "i8237_r8[devnum] (status) read as %02X\n", i8237_r8[devnum]);
        return (i8237_r8[devnum]);
    } else {                            /* write command register */
        i8237_r9[devnum] = data & 0xFF;
        sim_debug (DEBUG_reg, &i8237_dev, "i8237_r9[devnum] (command) set to %02X\n", i8237_r9[devnum]);
    }
    return 0;
}

uint8 i8237_r9x(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_r9[devnum]\n");
        return 0;
    } else {                            /* write request register */
        i8237_rC[devnum] = data & 0xFF;
        sim_debug (DEBUG_reg, &i8237_dev, "i8237_rC[devnum] (request) set to %02X\n", i8237_rC[devnum]);
    }
    return 0;
}

uint8 i8237_rAx(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rA[devnum]\n");
        return 0;
    } else {                            /* write single mask register */
        switch(data & 0x03) {
        case 0:
            if (data & 0x04)
                i8237_rB[devnum] |= 1;
            else
                i8237_rB[devnum] &= ~1;
            break;
        case 1:
            if (data & 0x04)
                i8237_rB[devnum] |= 2;
            else
                i8237_rB[devnum] &= ~2;
            break;
        case 2:
            if (data & 0x04)
                i8237_rB[devnum] |= 4;
            else
                i8237_rB[devnum] &= ~4;
            break;
        case 3:
            if (data & 0x04)
                i8237_rB[devnum] |= 8;
            else
                i8237_rB[devnum] &= ~8;
            break;
        }
        sim_debug (DEBUG_reg, &i8237_dev, "i8237_rB[devnum] (mask) set to %02X\n", i8237_rB[devnum]);
    }
    return 0;
}

uint8 i8237_rBx(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rB[devnum]\n");
        return 0;
    } else {                            /* write mode register */
        i8237_rA[devnum] = data & 0xFF;
        sim_debug (DEBUG_reg, &i8237_dev, "i8237_rA[devnum] (mode) set to %02X\n", i8237_rA[devnum]);
    }
    return 0;
}

uint8 i8237_rCx(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rC[devnum]\n");
        return 0;
    } else {                            /* clear byte pointer FF */
        i8237_rD[devnum] = 0;
        sim_debug (DEBUG_reg, &i8237_dev, "i8237_rD[devnum] (FF) cleared\n");
    }
    return 0;
}

uint8 i8237_rDx(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read temporary register */
        sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rD[devnum]\n");
        return 0;
    } else {                            /* master clear */
        i8237_reset_dev(devnum);
        sim_debug (DEBUG_reg, &i8237_dev, "i8237 master clear\n");
    }
    return 0;
}

uint8 i8237_rEx(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rE[devnum]\n");
        return 0;
    } else {                            /* clear mask register */
        i8237_rB[devnum] = 0;
        sim_debug (DEBUG_reg, &i8237_dev, "i8237_rB[devnum] (mask) cleared\n");
    }
    return 0;
}

uint8 i8237_rFx(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        sim_debug (DEBUG_reg, &i8237_dev, "Illegal read of i8237_rF[devnum]\n");
        return 0;
    } else {                            /* write all mask register bits */
        i8237_rB[devnum] = data & 0x0F;
        sim_debug (DEBUG_reg, &i8237_dev, "i8237_rB[devnum] (mask) set to %02X\n", i8237_rB[devnum]);
    }
    return 0;
}

/* end of i8237.c */
