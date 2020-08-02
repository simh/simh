/*  isbc208.c: Intel iSBC208 Floppy Disk adapter
    05-03-15 version 
 
    Copyright (c) 2011, William A. Beech
 
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
 
        ?? ??? 11 - Original file.
        16 Dec 12 - Modified to use isbc_80_10.cfg file to set base and size.
        24 Apr 15 -- Modified to use simh_debug
 
    NOTES:
 
        These functions support a simulated iSBC208 interface to 4 each 8-, 5 1/4-, or 
        3 1/2-inch floppy disk drives.  Commands are setup with programmed I/O to the 
        simulated ports of an i8237 DMA controller and an i8272 FDC.  Data transfer 
        to/from the simulated disks is performed directly with the multibus memory.
 
        The iSBC-208 can be configured for 8- or 16-bit addresses.  It defaults to 8-bit 
        addresses for the 8080/8085 processors.  It can be configured for I/O port
        addresses with 3-bits (8-bit address) or 11-bits (16-bit address).  Default is 
        3-bits set to 0. This defines the port offset to be used to determine the actual
        port address. Bus priority can be configured for parallel or serial mode.  Default is 
        serial. The multibus interface interrupt can be configured for interrupt 0-7.  
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
        OA      Write       Set/Reset DMAC Mask Register
        OB      Write       Load DMAC Mode Register
        OC      Write       Clear DMAC First/Last Flip-Flop
        0D      Write       DMAC Master Clear
        OF      Write       Load DMAC Mask Register
        10      Read        Read FDC Status Register
        11      Write       Load FDC Data Register
                Read        Read FDC Data Register
        12      Write       Load Controller Auxiliary Port
                Read        Poll Interrupt Status
        13      Write       Controller Reset
        14      Write       Load Controller Low-Byte Segment Address Register
        15      Write       Load Controller High-Byte Segment Address Register
        20-2F   Read/Write  Reserved for iSBX Multimodule Board 
 
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
 
        Read FDC Status Register
 
            The Read FDC Status Register command accesses the FDC's main 
            status register. The individual status register bits are as 
            follows:
 
              7   6   5   4   3   2   1   0
            +---+---+---+---+---+---+---+---+
            |                               |
            +---+---+---+---+---+---+---+---+
              |   |   |   |   |   |   |   |
              |   |   |   |   |   |   |   +-- FDD 0 BUSY
              |   |   |   |   |   |   +------ FDD 1 BUSY
              |   |   |   |   |   +---------- FDD 2 BUSY 
              |   |   |   |   +-------------- FDD 3 BUSY
              |   |   |   +------------------ FDC BUSY
              |   |   +---------------------- NON-DMA MODE
              |   +-------------------------- DATA INPUT/OUTPUT
              +------------------------------ REQUEST FOR MASTER
 
        Read/Write FDC Data Register
 
            The Read and Write FDC Data Register commands are used to write 
            command and parameter bytes to the FDC in order to specify the 
            operation to be performed (referred to as the "command phase") 
            and to read status bytes from the FDC following the operation 
            (referred to as the "result phase"). During the command and 
            result phases, the 8-bit data register is actually a series of 
            8-bit registers in a stack. Each register is accessed in 
            sequence; the number of registers accessed and the individual 
            register contents are defined by the specific disk command.
 
        Write Controller Auxiliary Port
 
            The Write Controller Auxiliary Port command is used to set or 
            clear individual bits within the controller's auxiliary port. 
            The four low-order port bits are dedicated to auxiliary drive 
            control functions (jumper links are required to connect the 
            desired port bit to an available pin on the drive interface 
            connectors). The most common application for these bits is 
            the "Motor-On" control function for mini-sized drives.
 
              7   6   5   4   3   2   1   0
            +---+---+---+---+---+---+---+---+
            |                               |
            +---+---+---+---+---+---+---+---+
              |   |   |   |   |   |   |   |
              |   |   |   |   +---+---+---+-- DRIVE CONTROL
              |   |   |   +------------------ ADDR 20
              |   |   +---------------------- ADDR 21
              |   +-------------------------- ADDR 22
              +------------------------------ ADDR 23
 
        Poll Interrupt Status
 
            The Poll Interrupt Status command presents the interrupt 
            status of the controller and the two interrupt status 
            lines dedicated to the iSBX Multimodule board.
              7   6   5   4   3   2   1   0
            +---+---+---+---+---+---+---+---+
            | X   X   X   X   X             |
            +---+---+---+---+---+---+---+---+
                                  |   |   | 
                                  |   |   +-- CONTROLLER INTERRUPT
                                  |   +------ MULTIMODULE BOARD INTERRUPT 0
                                  +---------- MULTIMODULE BOARD INTERRUPT 1
 
        Controller Reset
 
            The Controller Reset command is the software reset for the 
            controller. This command clears the controller's auxiliary 
            port and segment address register, provides a reset signal 
            to the iSBX Multimodule board and initializes the bus 
            controller (releases the bus), the DMAC (clears the internal 
            registers and masks the DREQ inputs), and the FDC (places 
            the FDC in an idle state and disables the output control 
            lines to the diskette drive).
 
        Write Controller Low- And High-Byte Segment Address Registers
 
            The Write Controller Low- and High-Byte Address Registers 
            commands are required when the controller uses 20-bit 
            addressing (memory address range from 0 to OFFFFFH). These 
            commands are issued prior to initiating a diskette read or 
            write operation to specify the 16-bit segment address.
 
        FDC Commands
 
            The 8272/D765 is capable of performing 15 different 
            commands. Each command is initiated by a multibyte transfer 
            from the processor, and the result after execution of the 
            command may also be a multibyte transfer back to the processor. 
            Because of this multibyte interchange of information between 
            the FDC and the processor, it is convenient to consider each 
            command as consisting of three phases:
 
                Command Phase: The FDC receives all information required to 
                    perform a particular operation from the processor.
 
                Execution Phase: The FDC performs the operation it was 
                    instructed to do.
 
                Result Phase: After completion of the operation, status 
                    and other housekeeping information are made available 
                    to the processor.
 
            Not all the FDC commands are supported by this emulation.  Only the subset
            of commands required to build an operable CP/M BIOS are supported.  They are:
 
                Read - Read specified data from the selected FDD.
 
                Write - Write specified data to the selected FDD.
 
                Seek - Move the R/W head to the specified cylinder on the specified FDD.
 
                Specify - Set the characteristics for all the FDDs.
 
                Sense Interrupt - Sense change in FDD Ready line or end of Seek/Recalibrate
                    command.
 
                Sense Drive - Returns status of all the FDDs.
 
                Recalibrate - Move the R/W head to cylinder 0 on the specified FDD.
 
                Format Track - Format the current track on the specified FDD.
 
                Read ID - Reads the first address mark it finds.
 
        Simulated Floppy Disk Drives
 
            The units in this device simulate an 8- or 5 1/4- or 3 1/2 inch drives.  The 
            drives can emulate SSSD, SSDD, and DSDD.  Drives can be attached to files up 
            to 1.44MB in size.  Drive configuration is selected when a disk is logged onto 
            the system.  An identity sector or identity byte contains information to 
            configure the OS drivers for the type of drive to emulate.
 
        uptr->u3 - 
        uptr->u4 - 
        uptr->u5 - 
        uptr->u6 - unit number (0-FDD_NUM)
*/
 
#include "system_defs.h"
 
#if defined (SBC208_NUM) && (SBC208_NUM > 0)

#define UNIT_V_WPMODE   (UNIT_V_UF)     /* Write protect */
#define UNIT_WPMODE     (1 << UNIT_V_WPMODE)
 
/* master status register definitions */
#define RQM             0x80            /* Request for master */
#define DIO             0x40            /* Data I/O Direction 0=W, 1=R */
#define NDM             0x20            /* Non-DMA mode */
#define CB              0x10            /* FDC busy */
#define D3B             0x08            /* FDD 3 busy */`
#define D2B             0x04            /* FDD 2 busy */`
#define D1B             0x02            /* FDD 1 busy */`
#define D0B             0x01            /* FDD 0 busy */`
 
/* status register 0 definitions */
#define IC              0xC0            /* Interrupt code */
#define IC_NORM         0x00            /* normal completion */
#define IC_ABNORM       0x40            /* abnormal completion */
#define IC_INVC         0x80            /* invalid command */
#define IC_RC           0xC0            /* drive not ready */
#define SE              0x20            /* Seek end */
#define EC              0x10            /* Equipment check */
#define NR              0x08            /* Not ready */
#define HD              0x04            /* Head selected */
#define US              0x03            /* Unit selected */
#define US_0            0x00            /* Unit 0 */
#define US_1            0x01            /* Unit 1 */
#define US_2            0x02            /* Unit 2 */
#define US_3            0x03            /* Unit 3 */
 
/* status register 1 definitions */
#define EN              0x80            /* End of cylinder */
#define DE              0x20            /* Data error */
#define OR              0x10            /* Overrun */
#define ND              0x04            /* No data */
#define NW              0x02            /* Not writable */
#define MA              0x01            /* Missing address mark */
 
/* status register 2 definitions */
#define CM              0x40            /* Control mark */
#define DD              0x20            /* Data error in data field */
#define WC              0x10            /* Wrong cylinder */
#define BC              0x02            /* Bad cylinder */
#define MD              0x01            /* Missing address mark in data field */
 
/* status register 3/fddst definitions */
#define FT              0x80            /* Fault */
#define WP              0x40            /* Write protect */
#define RDY             0x20            /* Ready */
#define T0              0x10            /* Track 0 */
#define TS              0x08            /* Two sided */
//#define HD              0x04            /* Head selected */
//#define US              0x03            /* Unit selected */
 
/* FDC command definitions */
#define READTRK         0x02
#define SPEC            0x03
#define SENDRV          0x04
#define WRITE           0x05
#define READ            0x06
#define HOME            0x07
#define SENINT          0x08
#define WRITEDEL        0x09
#define READID          0x0A
#define READDEL         0x0C
#define FMTTRK          0x0D
#define SEEK            0x0F
#define SCANEQ          0x11
#define SCANLOEQ        0x19
#define SCANHIEQ        0x1D
 
#define FDD_NUM          4
 
/* internal function prototypes */
 
t_stat isbc208_cfg(uint8 base);
t_stat isbc208_reset (DEVICE *dptr);
void isbc208_reset1 (void);
t_stat isbc208_attach (UNIT *uptr, const char *cptr);
t_stat isbc208_set_mode (UNIT *uptr, int32 val, const char *cptr, void *desc);
t_stat isbc208_svc (UNIT *uptr);
uint8 isbc208_r0(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r1(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r2(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r3(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r4(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r5(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r6(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r7(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r8(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r9(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_rA(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_rB(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_rC(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_rD(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_rE(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_rF(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r10(t_bool io, uint8 dat, uint8 devnum);
uint8 isbc208_r11(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r12(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r13(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r14(t_bool io, uint8 data, uint8 devnum);
uint8 isbc208_r15(t_bool io, uint8 data, uint8 devnum);
 
/* external function prototypes */
 
extern void set_irq(int32 int_num);
extern void clr_irq(int32 int_num);
extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
extern void multibus_put_mbyte(uint16 addr, uint8 val);
extern int32 multibus_get_mbyte(uint16 addr);
 
/* external globals */

extern uint16   PCX;

/* 8237 physical register definitions */
uint16 i8237_r0;                        // 8237 ch 0 address register
uint16 i8237_r1;                        // 8237 ch 0 count register
uint16 i8237_r2;                        // 8237 ch 1 address register
uint16 i8237_r3;                        // 8237 ch 1 count register
uint16 i8237_r4;                        // 8237 ch 2 address register
uint16 i8237_r5;                        // 8237 ch 2 count register
uint16 i8237_r6;                        // 8237 ch 3 address register
uint16 i8237_r7;                        // 8237 ch 3 count register
uint8 i8237_r8;                         // 8237 status register
uint8 i8237_r9;                         // 8237 command register
uint8 i8237_rA;                         // 8237 mode register
uint8 i8237_rB;                         // 8237 mask register
uint8 i8237_rC;                         // 8237 request register
uint8 i8237_rD;                         // 8237 first/last ff
 
/* 8272 physical register definitions */ 
/* 8272 command register stack*/
uint8 i8272_w0;                         // MT+MFM+SK+command 
uint8 i8272_w1;                         // HDS [HDS=H << 2] + DS1 + DS0
uint8 i8272_w2;                         // cylinder # (0-XX)
uint8 i8272_w3;                         // head # (0 or 1)
uint8 i8272_w4;                         // sector # (1-XX)                         
uint8 i8272_w5;                         // number of bytes (128 << N)
uint8 i8272_w6;                         // End of track (last sector # on cylinder)
uint8 i8272_w7;                         // Gap length
uint8 i8272_w8;                         // Data length (when N=0, size to read or write)
 
/* 8272 status register stack */
uint8 i8272_msr;                        // main status                         
uint8 i8272_r0;                         // ST 0                       
uint8 i8272_r1;                         // ST 1
uint8 i8272_r2;                         // ST 2
uint8 i8272_r3;                         // ST 3
 
/* iSBC-208 physical register definitions */
uint16 isbc208_sr;                      // isbc-208 segment register
uint8 isbc208_i;                        // iSBC-208 interrupt register
uint8 isbc208_a;                        // iSBC-208 auxillary port register
 
/* data obtained from analyzing command registers/attached file length */
int32 wsp = 0, rsp = 0;                 // indexes to write and read stacks (8272 data)
int32 cyl;                              // current cylinder
int32 hed;                              // current head [ h << 2]
int32 h;                                // current head
int32 sec;                              // current sector
int32 drv;                              // current drive
uint8 cmd, pcmd;                        // current command
int32 secn;                             // N 0-128, 1-256, etc
int32 spt;                              // sectors per track
int32 ssize;                            // sector size (128 << N)
 
int32 fddst[FDD_NUM] = {                // in ST3 format
    0,                                  // status of FDD 0
    0,                                  // status of FDD 1
    0,                                  // status of FDD 2
    0                                   // status of FDD 3
};
 
int8 maxcyl[FDD_NUM] = { 
    0,                                  // last cylinder + 1 of FDD 0
    0,                                  // last cylinder + 1 of FDD 1
    0,                                  // last cylinder + 1 of FDD 2
    0                                   // last cylinder + 1 of FDD 3
};
 
/* isbc208 Standard SIMH Device Data Structures - 4 units */
UNIT isbc208_unit[] = { 
    { UDATA (&isbc208_svc, UNIT_ATTABLE|UNIT_DISABLE|UNIT_BUFABLE|UNIT_MUSTBUF|UNIT_FIX, 368640), 20 }, 
    { UDATA (&isbc208_svc, UNIT_ATTABLE|UNIT_DISABLE|UNIT_BUFABLE|UNIT_MUSTBUF|UNIT_FIX, 368640), 20 }, 
    { UDATA (&isbc208_svc, UNIT_ATTABLE|UNIT_DISABLE|UNIT_BUFABLE|UNIT_MUSTBUF|UNIT_FIX, 368640), 20 }, 
    { UDATA (&isbc208_svc, UNIT_ATTABLE|UNIT_DISABLE|UNIT_BUFABLE|UNIT_MUSTBUF|UNIT_FIX, 368640), 20 } 
};
 
REG isbc208_reg[] = {
    { HRDATA (CH0ADR, i8237_r0, 16) },
    { HRDATA (CH0CNT, i8237_r1, 16) },
    { HRDATA (CH1ADR, i8237_r2, 16) },
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
    { HRDATA (STAT72, i8272_msr, 8) },
    { HRDATA (STAT720, i8272_r0, 8) },
    { HRDATA (STAT721, i8272_r1, 8) },
    { HRDATA (STAT722, i8272_r2, 8) },
    { HRDATA (STAT723, i8272_r3, 8) },
    { HRDATA (CMD720, i8272_w0, 8) },
    { HRDATA (CMD721, i8272_w1, 8) },
    { HRDATA (CMD722, i8272_w2, 8) },
    { HRDATA (CMD723, i8272_w3, 8) },
    { HRDATA (CMD724, i8272_w4, 8) },
    { HRDATA (CMD725, i8272_w5, 8) },
    { HRDATA (CMD726, i8272_w6, 8) },
    { HRDATA (CMD727, i8272_w7, 8) },
    { HRDATA (CMD728, i8272_w8, 8) },
    { HRDATA (FDD0, fddst[0], 8) },
    { HRDATA (FDD1, fddst[1], 8) },
    { HRDATA (FDD2, fddst[2], 8) },
    { HRDATA (FDD3, fddst[3], 8) },
    { HRDATA (SEGREG, isbc208_sr, 8) },
    { HRDATA (AUX, isbc208_a, 8) },
    { HRDATA (INT, isbc208_i, 8) },
    { NULL }
};
 
MTAB isbc208_mod[] = {
    { UNIT_WPMODE, 0, "RW", "RW", &isbc208_set_mode },
    { UNIT_WPMODE, UNIT_WPMODE, "WP", "WP", &isbc208_set_mode },
    { 0 }
};
 
DEBTAB isbc208_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { "REG", DEBUG_reg },
    { NULL }
};
 
DEVICE isbc208_dev = {
    "SBC208",                   //name 
    isbc208_unit,               //units 
    isbc208_reg,                //registers 
    isbc208_mod,                //modifiers
    FDD_NUM,                    //numunits 
    16,                         //aradix  
    32,                         //awidth  
    1,                          //aincr  
    16,                         //dradix  
    8,                          //dwidth
    NULL,                       //examine  
    NULL,                       //deposit  
    &isbc208_reset,             //reset
    NULL,                       //boot
    &isbc208_attach,            //attach  
    NULL,                       //detach
    NULL,                       //ctxt     
    DEV_DEBUG|DEV_DISABLE|DEV_DIS, //flags 
    0,                          //dctrl 
    isbc208_debug,              //debflags
    NULL,                       //msize
    NULL                        //lname
};
 
/* Service routines to handle simulator functions */
 
// configuration routine

t_stat isbc208_cfg(uint8 base)
{
    int32 i;
    UNIT *uptr;

    sim_printf("    sbc208: at base 0%02XH\n",
        base);
    reg_dev(isbc208_r0, base + 0, 0); //8237 registers 
    reg_dev(isbc208_r1, base + 1, 0); 
    reg_dev(isbc208_r2, base + 2, 0); 
    reg_dev(isbc208_r3, base + 3, 0); 
    reg_dev(isbc208_r4, base + 4, 0); 
    reg_dev(isbc208_r5, base + 5, 0); 
    reg_dev(isbc208_r6, base + 6, 0); 
    reg_dev(isbc208_r7, base + 7, 0); 
    reg_dev(isbc208_r8, base + 8, 0); 
    reg_dev(isbc208_r9, base + 9, 0); 
    reg_dev(isbc208_rA, base + 10, 0); 
    reg_dev(isbc208_rB, base + 11, 0); 
    reg_dev(isbc208_rC, base + 12, 0); 
    reg_dev(isbc208_rD, base + 13, 0); 
    reg_dev(isbc208_rE, base + 14, 0); 
    reg_dev(isbc208_rF, base + 15, 0); 
    reg_dev(isbc208_r10, base + 16, 0); //8272 registers
    reg_dev(isbc208_r11, base + 17, 0); 
    reg_dev(isbc208_r12, base + 18, 0); //devices on iSBC 208 
    reg_dev(isbc208_r13, base + 19, 0); 
    reg_dev(isbc208_r14, base + 20, 0); 
    reg_dev(isbc208_r15, base + 21, 0); 
    // one-time initialization for all FDDs
    for (i = 0; i < FDD_NUM; i++) { 
        uptr = isbc208_dev.units + i;
        uptr->u3 = 0;
        uptr->u4 = 0;
        uptr->u5 = 0;
        uptr->u6 = i;               //fdd unit number
        fddst[i] = WP | T0 | i;     /* initial drive status */
        uptr->flags |= UNIT_WPMODE; //set WP in unit flags
    }
    return SCPE_OK;
}

/* Reset routine */
 
t_stat isbc208_reset (DEVICE *dptr)
{
    isbc208_reset1();
    return SCPE_OK;
}
 
void isbc208_reset1 (void)
{
    int32 i;
    UNIT *uptr;

    for (i = 0; i < FDD_NUM; i++) {     /* handle all units */
        uptr = isbc208_dev.units + i;
        if ((uptr->flags & UNIT_ATT) == 0) { // unattached
//            sim_printf("         SBC208: FDD %d - Configured, Status=%02X, No disk image attached\n", 
//                i, fddst[i]);
        } else {                        /* attached */
            fddst[i] |= RDY;            /* drive ready */
//            sim_printf("         SBC208: FDD %d - Configured, Status=%02X, Attached to disk image %s\n",
//                i, fddst[i], uptr->filename);
            sim_activate (&isbc208_unit[uptr->u6], isbc208_unit[uptr->u6].wait);
        }
    }
    i8237_r8 = 0;                       /* status */
    i8237_r9 = 0;                       /* command */
    i8237_rB = 0x0F;                    /* mask */
    i8237_rC = 0;                       /* request */
    i8237_rD = 0;                       /* first/last FF */
    rsp = wsp = 0;                      /* reset indexes */
    cmd = 0;                            /* clear command */
    i8272_msr = RQM;                    /* 8272 ready for start of command */
//    sim_printf("   8237 Reset\n");
//    sim_printf("   8272 Reset\n");
}
 
/* isbc208 attach - attach an .IMG file to a FDD */
 
t_stat isbc208_attach (UNIT *uptr, const char *cptr)
{
    t_stat r;
    int32 c = 0;
    long len;
    uint8 fddnum;

    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   isbc208_attach: Attach error %d\n", r);
        return r;
    }
    len = sim_fsize (uptr->fileref);
    fddnum = uptr->u6;
    fddst[fddnum] |= RDY;               /* set unit ready */
    if (len == 368640) {                /* 5" 360K DSDD */
        maxcyl[fddnum] = 40;
        fddst[fddnum] |= TS;            // two sided
    }
    else if (len == 512512) {           /* 8" 512K SSDD */
        maxcyl[fddnum] = 77;
    }
    else if (len == 737280) {           /* 5" 720K DSQD */
        maxcyl[fddnum] = 80;
        fddst[fddnum] |= TS;            // two sided
    }
    else if (len == 1228800) {          /* 5" 1.2M DSHD */
        maxcyl[fddnum] = 80;
        fddst[fddnum] |= TS;            // two sided
    }
    else if (len == 1474560) {          /* 3.5" 1.44M DSHD */
        maxcyl[fddnum] = 80;
        fddst[fddnum] |= TS;            // two sided
    }
    uptr->capac = len;
    detach_unit (uptr);
    attach_unit (uptr, cptr);
    sim_printf("   SBC208: FDD %d - %ld bytes of disk image %s loaded, fddst=%02X\n", 
        fddnum, len, uptr->filename, fddst[fddnum]);
    sim_activate (&isbc208_unit[fddnum], isbc208_unit[fddnum].wait);
//    sim_printf( "   iSBC208_attach: Done\n");
    return SCPE_OK;
}
 
/* isbc208 set mode = WP or RW */
 
t_stat isbc208_set_mode (UNIT *uptr, int32 val, const char *cptr, void *desc)
{
    if (uptr->flags & UNIT_ATT)
        return sim_messagef (SCPE_ALATT, "%s is already attached to %s\n", sim_uname(uptr), uptr->filename);
    if (val & UNIT_WPMODE) {            /* write protect */
        fddst[uptr->u6] |= WP;
        uptr->flags |= val;
    } else {                            /* read write */
        fddst[uptr->u6] &= ~WP;
        uptr->flags &= ~val;
    }
    return SCPE_OK;
}
 
/* service routine - actually does the simulated disk I/O */
 
t_stat isbc208_svc (UNIT *uptr)
{
    int32 i, imgadr, data;
    int32 bpt, bpc;
    uint8 *fbuf;
 
    if ((i8272_msr & CB) && cmd && (uptr->u6 == drv)) { /* execution phase */
        fbuf = (uint8 *) uptr->filebuf;
        switch (cmd) {
        case READ:                  /* 0x06 */
            h = i8272_w3;           // h = 0 or 1 
            hed = i8272_w3 << 2;    // hed = 0 or 4 [h << 2] 
            sec = i8272_w4;         // sector number (1-XX)
            secn = i8272_w5;        // N (0-5)
            spt = i8272_w6;         // sectors/track
            ssize = 128 << secn;    // size of sector (bytes)
            bpt = ssize * spt;      // bytes/track
            bpc = bpt * 2;          // bytes/cylinder
//            sim_printf("208 Read: FDD=%d h=%d s=%d t=%d n=%d secsiz=%d spt=%d PCX=%04X\n",
//                drv, h, sec, cyl, secn, ssize, spt, PCX);
            if ((fddst[uptr->u6] & RDY) == 0) { // drive not ready
                i8272_r0 = IC_ABNORM | NR | hed | drv; /* command done - Not ready error*/
                i8272_r3 = fddst[uptr->u6];
                i8272_msr |= (RQM | DIO | CB); /* enter result phase */
            } else {                // get image addr for this d, h, c, s    
                if (fddst[uptr->u6] & TS)
                    imgadr = (cyl * bpc) /*+ (h * bpt)*/ + ((sec - 1) * ssize);
                else {
                    imgadr = (cyl * bpt) + ((sec - 1) * ssize);
                    for (i=0; i<=i8237_r1; i++) { /* copy selected sector to memory */
                        data = *(fbuf + (imgadr + i));
                        multibus_put_mbyte(i8237_r0 + i, data);
                    }
//*** need to step return results IAW table 3-11 in 143078-001
                i8272_w4 = ++sec;   /* next sector */
                i8272_r0 = hed | drv; /* command done - no error */
                i8272_r3 = fddst[uptr->u6];
            }
            i8272_r1 = 0;
            i8272_r2 = 0;
            i8272_w2 = cyl;         /* generate a current address mark */
            i8272_w3 = h;
            if (i8272_w4 > i8272_w6) { // beyond last sector of track?
                i8272_w4 = 1;       // yes, set to sector 1;
                if (h) {            // on head one?
                    i8272_w2++;     // yes, step cylinder
                    h = 0;          // back to head 0
                    }
                }
            }
            i8272_w5 = secn;
            i8272_msr |= (RQM | DIO | CB); /* enter result phase */
            rsp = wsp = 0;          /* reset indexes */
            set_irq(SBC208_INT);    /* set interrupt */
            break;
        case WRITE:                 /* 0x05 */
            h = i8272_w3;           // h = 0 or 1 
            hed = i8272_w3 << 2;    // hed = 0 or 4 [h << 2] 
            sec = i8272_w4;         // sector number (1-XX)
            secn = i8272_w5;        // N (0-5)
            spt = i8272_w6;         // sectors/track
            ssize = 128 << secn;    // size of sector (bytes)
            bpt = ssize * spt;      // bytes/track
            bpc = bpt * 2;          // bytes/cylinder
            i8272_r1 = 0;           // clear ST1
            i8272_r2 = 0;           // clear ST2
//            sim_printf("208 Read: FDD=%d h=%d s=%d t=%d n=%d secsiz=%d spt=%d PCX=%04X\n",
//                drv, h, sec, cyl, secn, ssize, spt, PCX);
            if ((fddst[uptr->u6] & RDY) == 0) {
                i8272_r0 = IC_ABNORM | NR | hed | drv; /* Not ready error*/
                i8272_r3 = fddst[uptr->u6];
                i8272_msr |= (RQM | DIO | CB); /* enter result phase */
                } else if (fddst[uptr->u6] & WP) {
                    i8272_r0 = IC_ABNORM | hed | drv; /* write protect error*/
                    i8272_r1 = NW;      // set not writable in ST1
                    i8272_r3 = fddst[uptr->u6] | WP;
                    i8272_msr |= (RQM | DIO | CB); /* enter result phase */
                    sim_printf("\nWrite Protected fddst[%d]=%02X\n", uptr->u6, fddst[uptr->u6]); 
            } else {                // get image addr for this d, h, c, s    
                if (fddst[uptr->u6] == TS)
                    imgadr = (cyl * bpc) /*+ (h * bpt)*/ + ((sec - 1) * ssize);
                else {
                    imgadr = (cyl * bpt) + ((sec - 1) * ssize);
                    for (i=0; i<=i8237_r1; i++) { /* copy selected memory to image */
                        data = multibus_get_mbyte(i8237_r0 + i);
                        *(fbuf + (imgadr + i)) = data;
                    }
                //*** quick fix. Needs more thought!
                /*
                fp = fopen(uptr->filename, "wb"); // write out modified image
                for (i=0; i<uptr->capac; i++) {
                    c = *(isbc208_buf[uptr->u6] + i) & 0xFF;
                    fputc(c, fp);
                }
                fclose(fp);
                */
//*** need to step return results IAW table 3-11 in 143078-001
                }
                i8272_w2 = cyl;     /* generate a current address mark */
                i8272_w3 = hed >> 2;
                i8272_w4 = ++sec;   /* next sector */
                i8272_w5 = secn;
                i8272_r0 = hed | drv; /* command done - no error */
                i8272_r3 = fddst[uptr->u6];
                i8272_msr |= (RQM | DIO | CB); /* enter result phase */
            }
            rsp = wsp = 0;          /* reset indexes */
            set_irq(SBC208_INT);    /* set interrupt */
            break;
        case FMTTRK:                /* 0x0D */
            if ((fddst[uptr->u6] & RDY) == 0) {
                i8272_r0 = IC_ABNORM | NR | hed | drv; /* Not ready error*/
                i8272_msr |= (RQM | DIO | CB); /* enter result phase */
            } else if (fddst[uptr->u6] & WP) {
                i8272_r0 = IC_ABNORM | hed | drv; /* write protect error*/
                i8272_r3 = fddst[uptr->u6] | WP;
                i8272_msr |= (RQM | DIO | CB); /* enter result phase */
            } else {
                ;                   /* do nothing for now */
                i8272_msr |= (RQM | DIO | CB); /* enter result phase */
            }
            rsp = wsp = 0;          /* reset indexes */
            set_irq(SBC208_INT);    /* set interrupt */
            break;
        case SENINT:                /* 0x08 */
            i8272_msr |= (RQM | DIO | CB); /* enter result phase */
            i8272_r0 = hed | drv;   /* command done - no error */
            i8272_r1 = 0;
            i8272_r2 = 0;
            rsp = wsp = 0;          /* reset indexes */
            clr_irq(SBC208_INT);    /* clear interrupt */
            break;
        case SENDRV:                /* 0x04 */
            i8272_msr |= (RQM | DIO | CB); /* enter result phase */
            i8272_r0 = hed | drv;   /* command done - no error */
            i8272_r1 = 0;
            i8272_r2 = 0;
            i8272_r3 = fddst[drv];  /* drv status */
            rsp = wsp = 0;          /* reset indexes */
            break;
        case HOME:                  /* 0x07 */
            if ((fddst[uptr->u6] & RDY) == 0) {
                i8272_r0 = IC_ABNORM | NR | hed | drv; /* Not ready error*/
                i8272_r3 = fddst[uptr->u6];
            } else {
                cyl = 0;            /* now on cylinder 0 */
                fddst[drv] |= T0;   /* set status flag */
                i8272_r0 = SE | hed | drv; /* seek end - no error */
            }
            i8272_r1 = 0;
            i8272_r2 = 0;
            i8272_msr &= ~(RQM | DIO | CB | hed | drv); /* execution phase done*/
            i8272_msr |= RQM;       /* enter COMMAND phase */
            rsp = wsp = 0;          /* reset indexes */
            set_irq(SBC208_INT);    /* set interrupt */
            break;
        case SPEC:                  /* 0x03 */
            fddst[0] |= TS;         //*** bad, bad, bad!
            fddst[1] |= TS;
            fddst[2] |= TS;
            fddst[3] |= TS;
            i8272_r0 = hed | drv;   /* command done - no error */
            i8272_r1 = 0;
            i8272_r2 = 0;
            i8272_msr &= ~(RQM | DIO | CB); /* execution phase done*/
//            i8272_msr = 0;          // force 0 for now, where does 0x07 come from?
            i8272_msr |= RQM;       /* enter command phase */
            rsp = wsp = 0;          /* reset indexes */
            break;
        case READID:                /* 0x0A */
            if ((fddst[uptr->u6] & RDY) == 0) {
                i8272_r0 = IC_RC | NR | hed | drv; /* Not ready error*/
                i8272_r3 = fddst[uptr->u6];
            } else {
                i8272_w2 = cyl;     /* generate a valid address mark */
                i8272_w3 = hed >> 2;
                i8272_w4 = 1;       /* always sector 1 */
                i8272_w5 = secn;
                i8272_r0 = hed | drv; /* command done - no error */
                i8272_msr &= ~(RQM | DIO | CB); /* execution phase done*/
                i8272_msr |= RQM;   /* enter command phase */
            }
            i8272_r1 = 0;
            i8272_r2 = 0;
            rsp = wsp = 0;          /* reset indexes */
            break;
        case SEEK:                  /* 0x0F */
            if ((fddst[uptr->u6] & RDY) == 0) { /* Not ready? */
                i8272_r0 = IC_ABNORM | NR | hed | drv; /* error*/
                i8272_r3 = fddst[uptr->u6];
            } else if (i8272_w2 >= maxcyl[uptr->u6]) { // too many steps
                i8272_r0 = IC_ABNORM | RDY | hed | drv; /* seek error*/
            } else {
//                i8272_r0 |= SE | hed | drv; /* command done - no error */
                i8272_r0 = SE | hed | drv; /* command done - no error */
                cyl = i8272_w2;     /* new cylinder number */
                if (cyl == 0) {     /* if cyl 0, set flag */
                    fddst[drv] |= T0; /* set T0 status flag */
                    i8272_r3 |= T0;
                } else {
                    fddst[drv] &= ~T0; /* clear T0 status flag */
                    i8272_r3 &= ~T0;
                }
            }
            i8272_r1 = 0;
            i8272_r2 = 0;
            i8272_msr &= ~(RQM | DIO | CB | hed | drv); /* execution phase done*/
            i8272_msr |= RQM;       /* enter command phase */
            rsp = wsp = 0;          /* reset indexes */
            set_irq(SBC208_INT);    /* set interrupt */
            break;
        default:
            i8272_msr &= ~(RQM | DIO | CB); /* execution phase done*/
            i8272_msr |= RQM;       /* enter command phase */
            i8272_r0 = IC_INVC | hed | drv; /* set bad command error */
            i8272_r1 = 0;
            i8272_r2 = 0;
            rsp = wsp = 0;          /* reset indexes */
            break;
        }
        pcmd = cmd;                     /* save for result phase */
        cmd = 0;                        /* reset command */
    }
    sim_activate (&isbc208_unit[uptr->u6], isbc208_unit[uptr->u6].wait);
    return SCPE_OK;
}
 
// read/write FDC data register stack
uint8 isbc208_r11(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read FDC data register */
//        sim_printf("208 R11: Read data=%02X pcmd=%02X rsp=%d PCX=%04X\nA", data, pcmd, rsp, PCX);
        wsp = 0;                        /* clear write stack index */
        switch (rsp) {                  /* read from next stack register */
        case 0:
            rsp++;                  /* step read stack index */
            clr_irq(SBC208_INT);    /* clear interrupt */
            if (pcmd == SENDRV) {
                i8272_msr = RQM;    /* result phase SENDRV done */
                return i8272_r1;    // SENDRV return ST1
            }
            return i8272_r0;        /* ST0 */
        case 1:
            rsp++;                  /* step read stack index */
            if (pcmd == SENINT) {
                i8272_msr = RQM;    /* result phase SENINT done */
                return cyl;         // SENINT return current cylinder
            }
            return i8272_r1;        /* ST1 */
        case 2:
            rsp++;                  /* step read stack index */
            return i8272_r2;        /* ST2 */
        case 3:
            rsp++;                  /* step read stack index */
            return i8272_w2;        /* C - cylinder */
        case 4:
            rsp++;                  /* step read stack index */
            return i8272_w3;        /* H - head */
        case 5:
            rsp++;                  /* step read stack index */
            return i8272_w4;        /* R - sector */
        case 6:
            i8272_msr = RQM;        /* result phase ALL OTHERS done */
            return i8272_w5;        /* N  - sector size*/
        }
    } else {                            /* write FDC data register */ 
//        sim_printf("208 R11: Write data=%02X cmd=%02X wsp=%d PCX=%04X\nA", data, cmd, wsp, PCX);
        rsp = 0;                        /* clear read stack index */
        switch (wsp) {                  /* write to next stack register */
        case 0:
            i8272_w0 = data;        /* rws = MT + MFM + SK + cmd */
            cmd = data & 0x1F;      /* save the current command */
            if (cmd == SENINT) {
                i8272_msr = CB;     /* command phase SENINT done */
                return 0;
            }
            wsp++;                  /* step write stack index */
            break;
        case 1:
            i8272_w1 = data;        /* rws = hed + drv */
            if (cmd != SPEC)
                drv = data & 0x03;
            if (cmd == HOME || cmd == SENDRV || cmd == READID) {
                i8272_msr = CB | hed | drv; /* command phase HOME, READID and SENDRV done */
                return 0;
            }
            wsp++;                  /* step write stack index */
            break;
        case 2:
            i8272_w2 = data;        /* rws = C */
            if (cmd == SPEC || cmd == SEEK) {
                i8272_msr = CB | hed | drv; /* command phase SPECIFY and SEEK done */
                return 0;
            }
            wsp++;                  /* step write stack index */
            break;
        case 3:
            i8272_w3 = data;        /* rw = H */
            hed = data;
            wsp++;                  /* step write stack index */
            break;
        case 4:
            i8272_w4 = data;        /* rw = R */
            sec = data;
            wsp++;                  /* step write stack index */
            break;
        case 5:
            i8272_w5 = data;        /* rw = N */
            if (cmd == FMTTRK) {
                i8272_msr = CB | hed | drv; /* command phase FMTTRK done */
                return 0;
            }
            wsp++;                  /* step write stack index */
            break;
        case 6:
            i8272_w6 = data;        /* rw = last sector number */
            wsp++;                  /* step write stack index */
            break;
        case 7:
            i8272_w7 = data;        /* rw = gap length */
            wsp++;                  /* step write stack index */
            break;
        case 8:
            i8272_w8 = data;        /* rw = bytes to transfer */
            if (cmd == READ || cmd == WRITE)
                i8272_msr = CB | hed | drv; /* command phase all others done */
            break;
        }
    }
    return 0;
}
 

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
 
    Each function is passed an 'io' flag, where 0 means a read from
    the port, and 1 means a write to the port.  On input, the actual
    input is passed as the return value, on output, 'data' is written
    to the device.
*/
 
uint8 isbc208_r0(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current address CH 0 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            return (i8237_r0 >> 8);
        } else {                        /* low byte */
            i8237_rD++;
            return (i8237_r0 & 0xFF);
        }
    } else {                            /* write base & current address CH 0 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            i8237_r0 |= (data << 8);
        } else {                        /* low byte */
            i8237_rD++;
            i8237_r0 = data & 0xFF;
        }
        return 0;
    }
}
 
uint8 isbc208_r1(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current word count CH 0 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            return (i8237_r1 >> 8);
        } else {                        /* low byte */
            i8237_rD++;
            return (i8237_r1 & 0xFF);
        }
    } else {                            /* write base & current address CH 0 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            i8237_r1 |= (data << 8);
        } else {                        /* low byte */
            i8237_rD++;
            i8237_r1 = data & 0xFF;
        }
        return 0;
    }
}
 
uint8 isbc208_r2(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current address CH 1 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            return (i8237_r2 >> 8);
        } else {                        /* low byte */
            i8237_rD++;
            return (i8237_r2 & 0xFF);
        }
    } else {                            /* write base & current address CH 1 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            i8237_r2 |= (data << 8);
        } else {                        /* low byte */
            i8237_rD++;
            i8237_r2 = data & 0xFF;
        }
        return 0;
    }
}
 
uint8 isbc208_r3(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current word count CH 1 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            return (i8237_r3 >> 8);
        } else {                        /* low byte */
            i8237_rD++;
            return (i8237_r3 & 0xFF);
        }
    } else {                            /* write base & current address CH 1 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            i8237_r3 |= (data << 8);
        } else {                        /* low byte */
            i8237_rD++;
            i8237_r3 = data & 0xFF;
        }
        return 0;
    }
}
 
uint8 isbc208_r4(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current address CH 2 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            return (i8237_r4 >> 8);
        } else {                        /* low byte */
            i8237_rD++;
            return (i8237_r4 & 0xFF);
        }
    } else {                            /* write base & current address CH 2 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            i8237_r4 |= (data << 8);
        } else {                        /* low byte */
            i8237_rD++;
            i8237_r4 = data & 0xFF;
        }
        return 0;
    }
}
 
uint8 isbc208_r5(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current word count CH 2 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            return (i8237_r5 >> 8);
        } else {                        /* low byte */
            i8237_rD++;
            return (i8237_r5 & 0xFF);
        }
    } else {                            /* write base & current address CH 2 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            i8237_r5 |= (data << 8);
        } else {                        /* low byte */
            i8237_rD++;
            i8237_r5 = data & 0xFF;
        }
        return 0;
    }
}
 
uint8 isbc208_r6(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current address CH 3 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            return (i8237_r6 >> 8);
        } else {                        /* low byte */
            i8237_rD++;
            return (i8237_r6 & 0xFF);
        }
    } else {                            /* write base & current address CH 3 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            i8237_r6 |= (data << 8);
        } else {                        /* low byte */
            i8237_rD++;
            i8237_r6 = data & 0xFF;
        }
        return 0;
    }
}
 
uint8 isbc208_r7(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read current word count CH 3 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            return (i8237_r7 >> 8);
        } else {                        /* low byte */
            i8237_rD++;
            return (i8237_r7 & 0xFF);
        }
    } else {                            /* write base & current address CH 3 */
        if (i8237_rD) {                 /* high byte */
            i8237_rD = 0;
            i8237_r7 |= (data << 8);
        } else {                        /* low byte */
            i8237_rD++;
            i8237_r7 = data & 0xFF;
        }
        return 0;
    }
}
 
uint8 isbc208_r8(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read status register */
        return (i8237_r8);
    } else {                            /* write command register */
        i8237_r9 = data & 0xFF;
        return 0;
    }
}
 
uint8 isbc208_r9(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        return 0;
    } else {                            /* write request register */
        i8237_rC = data & 0xFF;
        return 0;
    }
}
 
uint8 isbc208_rA(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
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
        return 0;
    }
}
 
uint8 isbc208_rB(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        return 0;
    } else {                            /* write mode register */
        i8237_rA = data & 0xFF;
        return 0;
    }
}
 
uint8 isbc208_rC(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        return 0;
    } else {                            /* clear byte pointer FF */
        i8237_rD = 0;
        return 0;
    }
}
 
uint8 isbc208_rD(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read temporary register */
        return 0;
    } else {                            /* master clear */
        isbc208_reset1();
        return 0;
    }
}
 
uint8 isbc208_rE(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        return 0;
    } else {                            /* clear mask register */
        i8237_rB = 0;
        return 0;
    }
}
 
uint8 isbc208_rF(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        return 0;
    } else {                            /* write all mask register bits */
        i8237_rB = data & 0x0F;
        return 0;
    }
}
 
uint8 isbc208_r10(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read FDC status register */
//        sim_printf("FDC Status=%02X PCX=%04X\n", i8272_msr, PCX);
        return i8272_msr;
    } else { 
        return 0;
    }
}
 
uint8 isbc208_r12(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read interrupt status */
        return (isbc208_i);
    } else {                            /* write controller auxillary port */ 
        isbc208_a = data & 0xFF;
        return 0;
    }
}
 
uint8 isbc208_r13(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        return 0;
    } else {                            /* reset controller */ 
        isbc208_reset1();
        return 0;
    }
}
 
uint8 isbc208_r14(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        return 0;
    } else {                            /* Low-Byte Segment Address Register */ 
        isbc208_sr = data & 0xFF;
        return 0;
    }
}
 
uint8 isbc208_r15(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {
        return 0;
    } else {                            /* High-Byte Segment Address Register */ 
        isbc208_sr |= data << 8;
        return 0;
    }
}
 
#endif /* SBC208_NUM > 0 */

/* end of isbc208.c */