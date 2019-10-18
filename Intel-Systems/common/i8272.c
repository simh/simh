/*  i8272.c: Intel 8272 FDC adapter

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

        ?? ??? 10 - Original file.
        16 Dec 12 - Modified to use isbc_80_10.cfg file to set base and size.
        24 Apr 15 -- Modified to use simh_debug

    NOTES:

        u6 - unit number/device number
*/

#include "system_defs.h"

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

/* status register 3/fddst72 definitions */

#define FT              0x80            /* Fault */
#define WP              0x40            /* Write protect */
#define RDY             0x20            /* Ready */
#define T0              0x10            /* Track 0 */
#define TS              0x08            /* Two sided */
// dups in register 0 definitions
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

int32 i8272_devnum = 0;                 //actual number of 8272 instances + 1
uint16 i8272_port[4];                   //base port registered to each instance

//disk geometry values
#define MDSSD           256256          //single density FDD size
#define MDSDD           512512          //double density FDD size
#define MAXSECSD        26              //single density last sector
#define MAXSECDD        52              //double density last sector
#define MAXTRK          76              //last track

/* external globals */

extern uint16   port;                   //port called in dev_table[port]
extern uint16    PCX;

/* internal function prototypes */

t_stat i8272_svc (UNIT *uptr);
t_stat i8272_reset (DEVICE *dptr, uint16 base);
void i8272_reset1(uint8 devnum);
uint8 i8272_get_dn(void);
t_stat i8272_attach (UNIT *uptr, CONST char *cptr);
t_stat i8272_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
uint8 i8272_r00(t_bool io, uint8 data);
uint8 i8272_r01(t_bool io, uint8 data);

/* external function prototypes */

extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8), uint16);
extern void multibus_put_mbyte(uint16 addr, uint8 val);
extern uint8 multibus_get_mbyte(uint16 addr);

/* 8272 physical register definitions */ 
/* 8272 command register stack*/

uint8 i8272_w0[4];                      // MT+MFM+SK+command 
uint8 i8272_w1[4];                      // HDS [HDS=H << 2] + DS1 + DS0
uint8 i8272_w2[4];                      // cylinder # (0-XX)
uint8 i8272_w3[4];                      // head # (0 or 1)
uint8 i8272_w4[4];                      // sector # (1-XX)                         
uint8 i8272_w5[4];                      // number of bytes (128 << N)
uint8 i8272_w6[4];                      // End of track (last sector # on cylinder)
uint8 i8272_w7[4];                      // Gap length
uint8 i8272_w8[4];                      // Data length (when N=0, size to read or write)

/* 8272 status register stack */

uint8 i8272_msr[4];                     // main status                         
uint8 i8272_r0[4];                      // ST 0                       
uint8 i8272_r1[4];                      // ST 1
uint8 i8272_r2[4];                      // ST 2
uint8 i8272_r3[4];                      // ST 3

/* data obtained from analyzing command registers/attached file length */

int32 wsp72[4] = {0, 0, 0, 0};            // indexes to write stacks (8272 data)
int32 rsp72[4] = {0, 0, 0, 0};            // indexes to read stacks (8272 data)
int32 cyl[4];                           // current cylinder
int32 hed[4];                           // current head [ h << 2]
int32 h[4];                             // current head
int32 sec[4];                           // current sector
int32 drv[4];                           // current drive
uint8 cmd[4], pcmd[4];                  // current command
int32 secn[4];                          // N 0-128, 1-256, etc
int32 spt[4];                           // sectors per track
int32 ssize[4];                         // sector size (128 << N)
int32 nd[4];                            //non-DMA mode

uint8 *i8272_buf[4][FDD_NUM] = {        /* FDD buffer pointers */
    NULL,
    NULL,
    NULL,
    NULL
};

int32 fddst72[4][FDD_NUM] = {             // in ST3 format
    0,                                  // status of FDD 0
    0,                                  // status of FDD 1
    0,                                  // status of FDD 2
    0                                   // status of FDD 3
};

int8 maxcyl72[4][FDD_NUM] = { 
    0,                                  // last cylinder + 1 of FDD 0
    0,                                  // last cylinder + 1 of FDD 1
    0,                                  // last cylinder + 1 of FDD 2
    0                                   // last cylinder + 1 of FDD 3
};

/* i8272 Standard I/O Data Structures */
/* up to 4 i8272 devices */

UNIT i8272_unit[] = { 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, MDSDD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, MDSDD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, MDSDD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, MDSDD), 20 } 
};

REG i8272_reg[] = {
    { HRDATA (STAT720, i8272_msr[0], 8) },
    { HRDATA (STAT7200, i8272_r0[0], 8) },
    { HRDATA (STAT7210, i8272_r1[0], 8) },
    { HRDATA (STAT7220, i8272_r2[0], 8) },
    { HRDATA (STAT7230, i8272_r3[0], 8) },
    { HRDATA (CMD7200, i8272_w0[0], 8) },
    { HRDATA (CMD7210, i8272_w1[0], 8) },
    { HRDATA (CMD7220, i8272_w2[0], 8) },
    { HRDATA (CMD7230, i8272_w3[0], 8) },
    { HRDATA (CMD7240, i8272_w4[0], 8) },
    { HRDATA (CMD7250, i8272_w5[0], 8) },
    { HRDATA (CMD7260, i8272_w6[0], 8) },
    { HRDATA (CMD7270, i8272_w7[0], 8) },
    { HRDATA (CMD7280, i8272_w8[0], 8) },
    { HRDATA (FDC0FDD0, fddst72[0][0], 8) },
    { HRDATA (FDC0FDD1, fddst72[0][1], 8) },
    { HRDATA (FDC0FDD2, fddst72[0][2], 8) },
    { HRDATA (FDC0FDD3, fddst72[0][3], 8) },
    { HRDATA (STAT721, i8272_msr[1], 8) },
    { HRDATA (STAT7201, i8272_r0[1], 8) },
    { HRDATA (STAT7211, i8272_r1[1], 8) },
    { HRDATA (STAT7221, i8272_r2[1], 8) },
    { HRDATA (STAT7231, i8272_r3[1], 8) },
    { HRDATA (CMD7201, i8272_w0[1], 8) },
    { HRDATA (CMD7211, i8272_w1[1], 8) },
    { HRDATA (CMD7221, i8272_w2[1], 8) },
    { HRDATA (CMD7231, i8272_w3[1], 8) },
    { HRDATA (CMD7241, i8272_w4[1], 8) },
    { HRDATA (CMD7251, i8272_w5[1], 8) },
    { HRDATA (CMD7261, i8272_w6[1], 8) },
    { HRDATA (CMD7271, i8272_w7[1], 8) },
    { HRDATA (CMD7281, i8272_w8[1], 8) },
    { HRDATA (FDC1FDD0, fddst72[1][0], 8) },
    { HRDATA (FDC1FDD1, fddst72[1][1], 8) },
    { HRDATA (FDC1FDD2, fddst72[1][2], 8) },
    { HRDATA (FDC1FDD3, fddst72[1][3], 8) },
    { HRDATA (STAT722, i8272_msr[2], 8) },
    { HRDATA (STAT7202, i8272_r0[2], 8) },
    { HRDATA (STAT7212, i8272_r1[2], 8) },
    { HRDATA (STAT7222, i8272_r2[2], 8) },
    { HRDATA (STAT7232, i8272_r3[2], 8) },
    { HRDATA (CMD7202, i8272_w0[2], 8) },
    { HRDATA (CMD7212, i8272_w1[2], 8) },
    { HRDATA (CMD7222, i8272_w2[2], 8) },
    { HRDATA (CMD7232, i8272_w3[2], 8) },
    { HRDATA (CMD7242, i8272_w4[2], 8) },
    { HRDATA (CMD7252, i8272_w5[2], 8) },
    { HRDATA (CMD7262, i8272_w6[2], 8) },
    { HRDATA (CMD7272, i8272_w7[2], 8) },
    { HRDATA (CMD7282, i8272_w8[2], 8) },
    { HRDATA (FDC2FDD0, fddst72[2][0], 8) },
    { HRDATA (FDC2FDD1, fddst72[2][1], 8) },
    { HRDATA (FDC2FDD2, fddst72[2][2], 8) },
    { HRDATA (FDC2FDD3, fddst72[2][3], 8) },
    { HRDATA (STAT72_0, i8272_msr[3], 8) },
    { HRDATA (STAT720_0, i8272_r0[3], 8) },
    { HRDATA (STAT721_0, i8272_r1[3], 8) },
    { HRDATA (STAT722_0, i8272_r2[3], 8) },
    { HRDATA (STAT723_0, i8272_r3[3], 8) },
    { HRDATA (CMD7203, i8272_w0[3], 8) },
    { HRDATA (CMD7213, i8272_w1[3], 8) },
    { HRDATA (CMD7223, i8272_w2[3], 8) },
    { HRDATA (CMD7233, i8272_w3[3], 8) },
    { HRDATA (CMD7243, i8272_w4[3], 8) },
    { HRDATA (CMD7253, i8272_w5[3], 8) },
    { HRDATA (CMD7263, i8272_w6[3], 8) },
    { HRDATA (CMD7273, i8272_w7[3], 8) },
    { HRDATA (CMD7283, i8272_w8[3], 8) },
    { HRDATA (FDC3FDD0, fddst72[3][0], 8) },
    { HRDATA (FDC3FDD1, fddst72[3][1], 8) },
    { HRDATA (FDC3FDD2, fddst72[3][2], 8) },
    { HRDATA (FDC3FDD3, fddst72[3][3], 8) },
    { NULL }
};

DEBTAB i8272_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

MTAB i8272_mod[] = {
    { UNIT_WPMODE, 0, "RW", "RW", &i8272_set_mode },
    { UNIT_WPMODE, UNIT_WPMODE, "WP", "WP", &i8272_set_mode },
    { 0 }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE i8272_dev = {
    "I8272",            //name
    i8272_unit,         //units
    i8272_reg,          //registers
    i8272_mod,          //modifiers
    FDD_NUM,            //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    NULL,               //reset
    NULL,               //boot
    &i8272_attach,      //attach  
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    i8272_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/* Service routines to handle simulator functions */

/* i8272_svc - actually does FDD read and write */

t_stat i8272_svc(UNIT *uptr)
{
    uint32 i;
    int32 imgadr, data;
    int c;
    int32 bpt, bpc;
    FILE *fp;
    uint8 devnum;

    devnum = uptr->u5;              //get FDC instance
    if ((i8272_msr[devnum] & CB) && cmd[devnum] && (uptr->u6 == drv[devnum])) { /* execution phase */
        sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: Entered execution phase\n");
        switch (cmd[devnum]) {
        case READ:                  /* 0x06 */
//            sim_printf("READ-e: fddst72=%02X", fddst72[devnum][uptr->u6]);
            h[devnum] = i8272_w3[devnum];           // h = 0 or 1 
            hed[devnum] = i8272_w3[devnum] << 2;    // hed = 0 or 4 [h << 2] 
            sec[devnum] = i8272_w4[devnum];         // sector number (1-XX)
            secn[devnum] = i8272_w5[devnum];        // N (0-5)
            spt[devnum] = i8272_w6[devnum];         // sectors/track
            ssize[devnum] = 128 << secn[devnum];    // size of sector (bytes)
            bpt = ssize[devnum] * spt[devnum];      // bytes/track
            bpc = bpt * 2;          // bytes/cylinder
//            sim_printf(" d=%d h=%d c=%d s=%d\n", drv[devnum], h[devnum], cyl[devnum], sec[devnum]);
            sim_debug (DEBUG_flow, &i8272_dev, 
                "8272_svc: FDC read: h=%d, hed=%d, sec=%d, secn=%d, spt=%d, ssize=%04X, bpt=%04X, bpc=%04X\n",
                    h[devnum], hed[devnum], sec[devnum], secn[devnum], spt[devnum], ssize[devnum], bpt, bpc);
            sim_debug (DEBUG_flow, &i8272_dev, 
                "8272_svc: FDC read: d=%d h=%d c=%d s=%d N=%d spt=%d fddst72=%02X\n",
                    drv[devnum], h[devnum], cyl[devnum], sec[devnum], secn[devnum], spt[devnum], fddst72[devnum][uptr->u6]);
            sim_debug (DEBUG_read, &i8272_dev, "8272_svc: FDC read of d=%d h=%d c=%d s=%d\n",
                    drv[devnum], h[devnum], cyl[devnum], sec[devnum]);
            if ((fddst72[devnum][uptr->u6] & RDY) == 0) { // drive not ready
                i8272_r0[devnum] = IC_ABNORM + NR + hed[devnum] + drv[devnum]; /* command done - Not ready error*/
                i8272_r3[devnum] = fddst72[devnum][uptr->u6];
                i8272_msr[devnum] |= (RQM + DIO + CB); /* enter result phase */
                sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: FDC read: Not Ready\n"); 
            } else {                // get image addr for this d, h, c, s    
                imgadr = (cyl[devnum] * bpc) + (h[devnum] * bpt) + ((sec[devnum] - 1) * ssize[devnum]);
//                sim_debug (DEBUG_read, &i8272_dev, 
//                    "8272_svc: FDC read: DMA addr=%04X cnt=%04X imgadr=%04X\n", 
//                        i8237_r0, i8237_r1, imgadr);
                for (i=0; i<=ssize[devnum]; i++) { /* copy selected sector to memory */
                    data = *(i8272_buf[devnum][uptr->u6] + (imgadr + i));
//                    multibus_put_mbyte(i8237_r0 + i, data);
                }
//*** need to step return results IAW table 3-11 in 143078-001
                i8272_w4[devnum] = ++sec[devnum];   /* next sector */
                i8272_r0[devnum] = hed[devnum] + drv[devnum]; /* command done - no error */
                i8272_r3[devnum] = fddst72[devnum][uptr->u6];
            }
            i8272_r1[devnum] = 0;
            i8272_r2[devnum] = 0;
            i8272_w2[devnum] = cyl[devnum];         /* generate a current address mark */
            i8272_w3[devnum] = h[devnum];
            if (i8272_w4[devnum] > i8272_w6[devnum]) { // beyond last sector of track?
                i8272_w4[devnum] = 1;       // yes, set to sector 1;
                if (h[devnum]) {            // on head one?
                    i8272_w2[devnum]++;     // yes, step cylinder
                    h[devnum] = 0;          // back to head 0
                }
            }
            i8272_w5[devnum] = secn[devnum];
            i8272_msr[devnum] |= (RQM + DIO + CB); /* enter result phase */
            rsp72[devnum] = wsp72[devnum] = 0;          /* reset indexes */
//            set_irq(I8272_INT);    /* set interrupt */
//                sim_printf("READ-x: fddst72=%02X\n", fddst72[devnum][uptr->u6]);
            break;
        case WRITE:                 /* 0x05 */
//                sim_printf("WRITE-e: fddst72=%02X\n", fddst72[devnum][uptr->u6]);
            h[devnum] = i8272_w3[devnum];           // h = 0 or 1 
            hed[devnum] = i8272_w3[devnum] << 2;    // hed = 0 or 4 [h << 2] 
            sec[devnum] = i8272_w4[devnum];         // sector number (1-XX)
            secn[devnum] = i8272_w5[devnum];        // N (0-5)
            spt[devnum] = i8272_w6[devnum];         // sectors/track
            ssize[devnum] = 128 << secn[devnum];    // size of sector (bytes)
            bpt = ssize[devnum] * spt[devnum];      // bytes/track
            bpc = bpt * 2;          // bytes/cylinder
            sim_debug (DEBUG_flow, &i8272_dev, 
                "8272_svc: FDC write: hed=%d, sec=%d, secn=%d, spt=%d, ssize=%04X, bpt=%04X, bpc=%04X\n",
                    hed[devnum], sec[devnum], secn[devnum], spt[devnum], ssize[devnum], bpt, bpc);
            sim_debug (DEBUG_flow, &i8272_dev, 
                "8272_svc: FDC write: d=%d h=%d c=%d s=%d N=%d spt=%d fddst72=%02X\n",
                    drv[devnum], h[devnum], cyl[devnum], sec[devnum], secn[devnum], spt[devnum], fddst72[devnum][uptr->u6]);
            sim_debug (DEBUG_write, &i8272_dev, "8272_svc: FDC write of d=%d h=%d c=%d s=%d\n",
                    drv[devnum], h[devnum], cyl[devnum], sec[devnum]);
            i8272_r1[devnum] = 0;           // clear ST1
            i8272_r2[devnum] = 0;           // clear ST2
            if ((fddst72[devnum][uptr->u6] & RDY) == 0) {
                i8272_r0[devnum] = IC_ABNORM + NR + hed[devnum] + drv[devnum]; /* Not ready error*/
                i8272_r3[devnum] = fddst72[devnum][uptr->u6];
                i8272_msr[devnum] |= (RQM + DIO + CB); /* enter result phase */
                sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: FDC write: Not Ready\n"); 
//                } else if (fddst72[devnum][uptr->u6] & WP) {
//                    i8272_r0[devnum] = IC_ABNORM + hed[devnum] + drv[devnum]; /* write protect error */
//                    i8272_r1[devnum] = NW;      // set not writable in ST1
//                    i8272_r3[devnum] = fddst72[devnum][uptr->u6] + WP;
//                    i8272_msr[devnum] |= (RQM + DIO + CB); /* enter result phase */
//                    sim_printf("\nWrite Protected fddst72[%d]=%02X\n", uptr->u6, fddst72[devnum][uptr->u6]); 
//                    if (i8272_dev.dctrl & DEBUG_flow)
//                        sim_printf("8272_svc: FDC write: Write Protected\n"); 
            } else {                // get image addr for this d, h, c, s    
                imgadr = (cyl[devnum] * bpc) + (h[devnum] * bpt) + ((sec[devnum] - 1) * ssize[devnum]);
//                sim_debug (DEBUG_write, &i8272_dev, 
//                    "8272_svc: FDC write: DMA adr=%04X cnt=%04X imgadr=%04X\n", 
//                        i8237_r0, i8237_r1, imgadr);
                for (i=0; i<=ssize[devnum]; i++) { /* copy selected memory to image */
//                    data = multibus_get_mbyte(i8237_r0 + i);
                    *(i8272_buf[devnum][uptr->u6] + (imgadr + i)) = data;
                }
                //*** quick fix. Needs more thought!
//                fp = fopen(uptr->filename, "wb"); // write out modified image
//                for (i=0; i<uptr->capac; i++) {
//                    c = *(i8272_buf[devnum][uptr->u6] + i) & 0xFF;
//                    fputc(c, fp);
//                }
//                fclose(fp);
//*** need to step return results IAW table 3-11 in 143078-001
                i8272_w2[devnum] = cyl[devnum];     /* generate a current address mark */
                i8272_w3[devnum] = hed[devnum] >> 2;
                i8272_w4[devnum] = ++sec[devnum];   /* next sector */
                i8272_w5[devnum] = secn[devnum];
                i8272_r0[devnum] = hed[devnum] + drv[devnum]; /* command done - no error */
                i8272_r3[devnum] = fddst72[devnum][uptr->u6];
                i8272_msr[devnum] |= (RQM + DIO + CB); /* enter result phase */
            }
            rsp72[devnum] = wsp72[devnum] = 0;          /* reset indexes */
//            set_irq(I8272_INT);    /* set interrupt */
//                sim_printf("WRITE-x: fddst72=%02X\n", fddst72[devnum][uptr->u6]);
            break;
        case FMTTRK:                /* 0x0D */
            if ((fddst72[devnum][uptr->u6] & RDY) == 0) {
                i8272_r0[devnum] = IC_ABNORM + NR + hed[devnum] + drv[devnum]; /* Not ready error*/
                i8272_msr[devnum] |= (RQM + DIO + CB); /* enter result phase */
                sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: Not Ready\n"); 
            } else if (fddst72[devnum][uptr->u6] & WP) {
                i8272_r0[devnum] = IC_ABNORM + hed[devnum] + drv[devnum]; /* write protect error*/
                i8272_r3[devnum] = fddst72[devnum][uptr->u6] + WP;
                i8272_msr[devnum] |= (RQM + DIO + CB); /* enter result phase */
                sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: Write Protected\n"); 
            } else {
                ;                   /* do nothing for now */
                i8272_msr[devnum] |= (RQM + DIO + CB); /* enter result phase */
            }
            rsp72[devnum] = wsp72[devnum] = 0;          /* reset indexes */
//            set_irq(I8272_INT);    /* set interrupt */
            break;
        case SENINT:                /* 0x08 */
            i8272_msr[devnum] |= (RQM + DIO + CB); /* enter result phase */
            i8272_r0[devnum] = hed[devnum] + drv[devnum];   /* command done - no error */
            i8272_r1[devnum] = 0;
            i8272_r2[devnum] = 0;
            rsp72[devnum] = wsp72[devnum] = 0;          /* reset indexes */
//            clr_irq(I8272_INT);    /* clear interrupt */
            break;
        case SENDRV:                /* 0x04 */
            sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: FDC sense drive: d=%d fddst72=%02X\n",
            drv[devnum], fddst72[devnum][uptr->u6]);
            i8272_msr[devnum] |= (RQM + DIO + CB); /* enter result phase */
            i8272_r0[devnum] = hed[devnum] + drv[devnum];   /* command done - no error */
            i8272_r1[devnum] = 0;
            i8272_r2[devnum] = 0;
            i8272_r3[devnum] = fddst72[devnum][drv[devnum]];  /* drv status */
            rsp72[devnum] = wsp72[devnum] = 0;          /* reset indexes */
            break;
        case HOME:                  /* 0x07 */
//                sim_printf("HOME-e: fddst72=%02X\n", fddst72[devnum][uptr->u6]);
            sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: FDC home: d=%d fddst72=%02X\n",
                drv[devnum], fddst72[devnum][uptr->u6]);
            if ((fddst72[devnum][uptr->u6] & RDY) == 0) {
                i8272_r0[devnum] = IC_ABNORM + NR + hed[devnum] + drv[devnum]; /* Not ready error*/
                i8272_r3[devnum] = fddst72[devnum][uptr->u6];
                sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: Not Ready\n"); 
            } else {
                cyl[devnum] = 0;            /* now on cylinder 0 */
                fddst72[devnum][drv[devnum]] |= T0;   /* set status flag */
                i8272_r0[devnum] = SE + hed[devnum] + drv[devnum]; /* seek end - no error */
            }
            i8272_r1[devnum] = 0;
            i8272_r2[devnum] = 0;
            i8272_msr[devnum] &= ~(RQM + DIO + CB + hed[devnum] + drv[devnum]); /* execution phase done*/
            i8272_msr[devnum] |= RQM;       /* enter COMMAND phase */
            rsp72[devnum] = wsp72[devnum] = 0;          /* reset indexes */
//            set_irq(I8272_INT);    /* set interrupt */
//                sim_printf("HOME-x: fddst72=%02X\n", fddst72[devnum][uptr->u6]);
            break;
        case SPEC:                  /* 0x03 */
            fddst72[devnum][0] |= TS;         //*** bad, bad, bad!
            fddst72[devnum][1] |= TS;
            fddst72[devnum][2] |= TS;
            fddst72[devnum][3] |= TS;
//                sim_printf("SPEC-e: fddst72[%d]=%02X\n", uptr->u6, fddst72[devnum][uptr->u6]);
            sim_debug (DEBUG_flow, &i8272_dev, 
                "8272_svc: FDC specify: SRT=%d ms HUT=%d ms HLT=%d ms ND=%d\n", 
                    16 - (drv[devnum] >> 4), 16 * (drv[devnum] & 0x0f), i8272_w2[devnum] & 0xfe, nd[devnum]);
            i8272_r0[devnum] = hed[devnum] + drv[devnum];   /* command done - no error */
            i8272_r1[devnum] = 0;
            i8272_r2[devnum] = 0;
//            i8272_msr[devnum] &= ~(RQM + DIO + CB); /* execution phase done*/
            i8272_msr[devnum] = 0;          // force 0 for now, where does 0x07 come from?
            i8272_msr[devnum] |= RQM + (nd[devnum] ? ND : 0);       /* enter command phase */
            rsp72[devnum] = wsp72[devnum] = 0;          /* reset indexes */
//                sim_printf("SPEC-x: fddst72[%d]=%02X\n", uptr->u6, fddst72[devnum][uptr->u6]);
            break;
        case READID:                /* 0x0A */
            if ((fddst72[devnum][uptr->u6] & RDY) == 0) {
                i8272_r0[devnum] = IC_RC + NR + hed[devnum] + drv[devnum]; /* Not ready error*/
                i8272_r3[devnum] = fddst72[devnum][uptr->u6];
                sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: Not Ready\n"); 
            } else {
                i8272_w2[devnum] = cyl[devnum];     /* generate a valid address mark */
                i8272_w3[devnum] = hed[devnum] >> 2;
                i8272_w4[devnum] = 1;       /* always sector 1 */
                i8272_w5[devnum] = secn[devnum];
                i8272_r0[devnum] = hed[devnum] + drv[devnum]; /* command done - no error */
                i8272_msr[devnum] &= ~(RQM + DIO + CB); /* execution phase done*/
                i8272_msr[devnum] |= RQM;   /* enter command phase */
            }
            i8272_r1[devnum] = 0;
            i8272_r2[devnum] = 0;
            rsp72[devnum] = wsp72[devnum] = 0;          /* reset indexes */
            break;
        case SEEK:                  /* 0x0F */
//                sim_printf("SEEK-e: fddst72=%02X\n", fddst72[devnum][uptr->u6]);
            sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: FDC seek: d=%d c=%d fddst72=%02X\n",
                drv[devnum], i8272_w2, fddst72[devnum][uptr->u6]);
            if ((fddst72[devnum][uptr->u6] & RDY) == 0) { /* Not ready? */
                i8272_r0[devnum] = IC_ABNORM + NR + hed[devnum] + drv[devnum]; /* error*/
                i8272_r3[devnum] = fddst72[devnum][uptr->u6];
                sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: FDC seek: Not Ready\n"); 
            } else if (i8272_w2[devnum] >= maxcyl72[devnum][uptr->u6]) {
                i8272_r0[devnum] = IC_ABNORM + RDY + hed[devnum] + drv[devnum]; /* seek error*/
                sim_debug (DEBUG_flow, &i8272_dev, "8272_svc: FDC seek: Invalid Cylinder %d\n", i8272_w2); 
            } else {
                i8272_r0[devnum] |= SE + hed[devnum] + drv[devnum]; /* command done - no error */
                cyl[devnum] = i8272_w2[devnum];         /* new cylinder number */
                if (cyl[devnum] == 0) {         /* if cyl 0, set flag */
                    fddst72[devnum][drv[devnum]] |= T0;   /* set T0 status flag */
                    i8272_r3[devnum] |= T0;
                } else {
                    fddst72[devnum][drv[devnum]] &= ~T0;   /* clear T0 status flag */
                    i8272_r3[devnum] &= ~T0;
                }
            }
            i8272_r1[devnum] = 0;
            i8272_r2[devnum] = 0;
            i8272_msr[devnum] &= ~(RQM + DIO + CB + hed[devnum] + drv[devnum]); /* execution phase done*/
            i8272_msr[devnum] |= RQM;       /* enter command phase */
            rsp72[devnum] = wsp72[devnum] = 0;          /* reset indexes */
//                set_irq(I8272_INT);    /* set interrupt */
//                sim_printf("SEEK-x: fddst72=%02X\n", fddst72[devnum][uptr->u6]);
            break;
        default:
            i8272_msr[devnum] &= ~(RQM + DIO + CB); /* execution phase done*/
            i8272_msr[devnum] |= RQM;       /* enter command phase */
            i8272_r0[devnum] = IC_INVC + hed[devnum] + drv[devnum]; /* set bad command error */
            i8272_r1[devnum] = 0;
            i8272_r2[devnum] = 0;
            rsp72[devnum] = wsp72[devnum] = 0;          /* reset indexes */
            break;
        }
        pcmd[devnum] = cmd[devnum];                     /* save for result phase */
        cmd[devnum] = 0;                        /* reset command */
        sim_debug (DEBUG_flow, &i8272_dev, 
            "8272_svc: Exit: msr=%02X ST0=%02X ST1=%02X ST2=%02X ST3=%02X\n",
                i8272_msr[devnum], i8272_r0[devnum], i8272_r1[devnum], i8272_r2[devnum], i8272_r3); 
    }
    sim_activate (&i8272_unit[uptr->u6], i8272_unit[uptr->u6].wait);
    return SCPE_OK;
}

/* i8272 hardware reset routine */ 

t_stat i8272_reset(DEVICE *dptr, uint16 base)
{
    if (i8272_devnum >= I8272_NUM) {
        sim_printf("8272_reset: too many devices!\n");
        return 0;
    }
    if (I8272_NUM) {
        sim_printf("   I8272-%d: Hardware Reset\n", i8272_devnum);
        sim_printf("   I8272-%d: Registered at %04X\n", i8272_devnum, base);
        i8272_port[i8272_devnum] = reg_dev(i8272_r00, I8272_BASE + 0); 
        reg_dev(i8272_r01, I8272_BASE + 1); 
        if ((i8272_dev.flags & DEV_DIS) == 0) 
            i8272_reset1(i8272_devnum);
        i8272_devnum++;
    } else {
        sim_printf("   No isbc208 installed\n");
    }
    return SCPE_OK;
}

/* i8272 software reset routine */ 

void i8272_reset1(uint8 devnum)
{
    int32 i;
    UNIT *uptr;
    static int flag = 1;

    if (flag) sim_printf("I8272: Initializing\n");
    for (i = 0; i < FDD_NUM; i++) {     /* handle all units */
        uptr = i8272_dev.units + i;
        if (uptr->capac == 0) {         /* if not configured */
//            sim_printf("   i8272%d: Not configured\n", i);
//            if (flag) {
//                sim_printf("      ALL: \"set isbc208 en\"\n");
//                sim_printf("      EPROM: \"att isbc2080 <filename>\"\n");
//                flag = 0;
//            }
            uptr->capac = 0;            /* initialize unit */
            uptr->u3 = 0; 
            uptr->u4 = 0;
            uptr->u5 = devnum;          // i8272 device instance - only set here! 
            uptr->u6 = i;               /* FDD number - only set here! */
            fddst72[devnum][i] = WP + T0 + i; /* initial drive status */
            uptr->flags |= UNIT_WPMODE; /* set WP in unit flags */
            sim_activate (&i8272_unit[uptr->u6], i8272_unit[uptr->u6].wait);
        } else {
            fddst72[devnum][i] = RDY + WP + T0 + i; /* initial attach drive status */
//            sim_printf("   i8272%d: Configured, Attached to %s\n", i, uptr->filename);
        }
    }
    i8272_msr[devnum] = RQM;                    /* 8272 ready for start of command */
    rsp72[devnum] = wsp72[devnum] = 0;                      /* reset indexes */
    cmd[devnum] = 0;                            /* clear command */
    sim_printf("   i8272-%d: Software Reset\n", i8272_devnum);
    if (flag) {
        sim_printf("   8272 Reset\n");
    }
    flag = 0;
}

uint8 i8272_get_dn(void)
{
    int i;

    for (i=0; i<I8272_NUM; i++)
        if (port >= i8272_port[i] && port <= i8272_port[i] + 1)
            return i;
    sim_printf("i8272_get_dn: port %03X not in 8272 device table\n", port);
    return 0xFF;
}

/* i8272 attach - attach an .IMG file to an FDD on a FDC */

t_stat i8272_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    uint8 fdcnum, fddnum, devnum;
    int i;
    long flen;

    sim_debug (DEBUG_flow, &i8272_dev, "   i8272_attach: Entered with cptr=%s\n", cptr);
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   i8272_attach: Attach error\n");
        return r;
    }
    devnum = fdcnum = uptr->u5;
    fddnum = uptr->u6;
    flen = uptr->capac;
    fddst72[devnum][uptr->u6] |= RDY;             /* set unit ready */
    for (i=0; i<fddnum; i++) {               //for each disk drive
        if (flen == 368640) {               /* 5" 360K DSDD */
            maxcyl72[devnum][uptr->u6] = 40;
            fddst72[devnum][uptr->u6] |= TS;          // two sided
        }
        else if (flen == 737280) {          /* 5" 720K DSQD / 3.5" 720K DSDD */
            maxcyl72[devnum][uptr->u6] = 80;
            fddst72[devnum][uptr->u6] |= TS;          // two sided
        }
        else if (flen == 1228800) {         /* 5" 1.2M DSHD */
            maxcyl72[devnum][uptr->u6] = 80;
            fddst72[devnum][uptr->u6] |= TS;          // two sided
        }
        else if (flen == 1474560) {         /* 3.5" 1.44M DSHD */
            maxcyl72[devnum][uptr->u6] = 80;
            fddst72[devnum][uptr->u6] |= TS;          // two sided
        }
        sim_printf("   Drive-%d: %d bytes of disk image %s loaded, fddst72=%02X\n", 
            uptr->u6, i, uptr->filename, fddst72[devnum][uptr->u6]);
    }   
    sim_debug (DEBUG_flow, &i8272_dev, "   i8272_attach: Done\n");
    return SCPE_OK;
}

/* i8272 set mode */
/* Handle write protect */

t_stat i8272_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint8 devnum;

    sim_debug (DEBUG_flow, &i8272_dev, "   i8272_set_mode: Entered with val=%08XH uptr->flags=%08X\n", 
        val, uptr->flags);
    devnum = uptr->u5;
    if (val & UNIT_WPMODE) {            /* write protect */
        fddst72[devnum][uptr->u6] |= WP;
        uptr->flags |= val;
    } else {                            /* read write */
        fddst72[devnum][uptr->u6] &= ~WP;
        uptr->flags &= ~val;
    }
//    sim_printf("fddst72[%d]=%02XH uptr->flags=%08X\n", uptr->u6, fddst72[devnum][uptr->u6], uptr->flags);
    sim_debug (DEBUG_flow, &i8272_dev, "   i8272_set_mode: Done\n");
    return SCPE_OK;
}
/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

uint8 i8272_r00(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8272_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read FDC status register */
            sim_debug (DEBUG_reg, &i8272_dev, "i8272_msr read as %02X\n", i8272_msr[devnum]);
            return i8272_msr[devnum];
        } else { 
            sim_debug (DEBUG_reg, &i8272_dev, "Illegal write to i8272_r0\n");
            return 0;
        }
    }
    return 0;
}

// read/write FDC data register stack

uint8 i8272_r01(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8272_get_dn()) != 0xFF) {
        if (io == 0) {                          /* read FDC data register */
            wsp72[devnum] = 0;                    /* clear write stack index */
            switch (rsp72[devnum]) {              /* read from next stack register */
            case 0:
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_r1 read as %02X\n", i8272_r1[devnum]);
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_r3 read as %02X\n", i8272_r3[devnum]);
                rsp72[devnum]++;                  /* step read stack index */
//                clr_irq(I8272_INT);             /* clear interrupt */
                if (pcmd[devnum] == SENDRV) {
                    i8272_msr[devnum] = RQM;    /* result phase SENDRV done */
                    return i8272_r1[devnum];    // SENDRV return ST1
                }
                return i8272_r0[devnum];        /* ST0 */
            case 1:
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_r2 read as %02X\n", i8272_r2[devnum]);
                rsp72[devnum]++;                  /* step read stack index */
                if (pcmd[devnum] == SENINT) {
                    i8272_msr[devnum] = RQM;    /* result phase SENINT done */
                    return cyl[devnum];         // SENINT return current cylinder
                }
                return i8272_r1[devnum];        /* ST1 */
            case 2:
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_r3 read as %02X\n", i8272_r3[devnum]);
                rsp72[devnum]++;                  /* step read stack index */
                return i8272_r2[devnum];        /* ST2 */
            case 3:
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w2 read as %02X\n", i8272_w2);
                rsp72[devnum]++;                  /* step read stack index */
                return i8272_w2[devnum];        /* C - cylinder */
            case 4:
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w3 read as %02X\n", i8272_w3[devnum]);
                rsp72[devnum]++;                  /* step read stack index */
                return i8272_w3[devnum];        /* H - head */
            case 5:
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w4 read as %02X\n", i8272_w4[devnum]);
                rsp72[devnum]++;                  /* step read stack index */
                return i8272_w4[devnum];        /* R - sector */
            case 6:
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w5 read as %02X\n", i8272_w5[devnum]);
                i8272_msr[devnum] = RQM;        /* result phase ALL OTHERS done */
                return i8272_w5[devnum];        /* N  - sector size*/
            }
        } else {                            /* write FDC data register */ 
            rsp72[devnum] = 0;                        /* clear read stack index */
            switch (wsp72[devnum]) {                  /* write to next stack register */
            case 0:
                i8272_w0[devnum] = data;        /* rws = MT + MFM + SK + cmd */
                cmd[devnum] = data & 0x1F;      /* save the current command */
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w0 set to %02X\n", data);
                if (cmd[devnum] == SENINT) {
                    i8272_msr[devnum] = CB;     /* command phase SENINT done */
                    return 0;
                }
                wsp72[devnum]++;                  /* step write stack index */
                break;
            case 1:
                i8272_w1[devnum] = data;        /* rws = hed + drv */
                if (cmd[devnum] != SPEC)
                    drv[devnum] = data & 0x03;
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w1 set to %02X\n", data);
                if (cmd[devnum] == HOME || cmd[devnum] == SENDRV || cmd[devnum] == READID) {
                    i8272_msr[devnum] = CB + hed[devnum] + drv[devnum]; /* command phase HOME, READID and SENDRV done */
                    return 0;
                }
                wsp72[devnum]++;                  /* step write stack index */
                break;
            case 2:
                i8272_w2[devnum] = data;        /* rws = C */
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w2 set to %02X\n", data);
                if (cmd[devnum] == SPEC || cmd[devnum] == SEEK) {
                    i8272_msr[devnum] = CB + hed[devnum] + drv[devnum]; /* command phase SPECIFY and SEEK done */
                    return 0;
                }
                wsp72[devnum]++;                  /* step write stack index */
                break;
            case 3:
                i8272_w3[devnum] = data;        /* rw = H */
                hed[devnum] = data;
                wsp72[devnum]++;                  /* step write stack index */
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w3 set to %02X\n", data);
                break;
            case 4:
                i8272_w4[devnum] = data;        /* rw = R */
                sec[devnum] = data;
                wsp72[devnum]++;                  /* step write stack index */
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w4 set to %02X\n", data);
                break;
            case 5:
                i8272_w5[devnum] = data;        /* rw = N */
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w5 set to %02X\n", data);
                if (cmd[devnum] == FMTTRK) {
                    i8272_msr[devnum] = CB + hed[devnum] + drv[devnum]; /* command phase FMTTRK done */
                    return 0;
                }
                wsp72[devnum]++;                  /* step write stack index */
                break;
            case 6:
                i8272_w6[devnum] = data;        /* rw = last sector number */
                wsp72[devnum]++;                  /* step write stack index */
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w6 set to %02X\n", data);
                break;
            case 7:
                i8272_w7[devnum] = data;        /* rw = gap length */
                wsp72[devnum]++;                  /* step write stack index */
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w7 set to %02X\n", data);
                break;
            case 8:
                i8272_w8[devnum] = data;        /* rw = bytes to transfer */
                sim_debug (DEBUG_reg, &i8272_dev, "i8272_w8 set to %02X\n", data);
                if (cmd[devnum] == READ || cmd[devnum] == WRITE)
                    i8272_msr[devnum] = CB + hed[devnum] + drv[devnum]; /* command phase all others done */
                break;
            }
        }
    }
    return 0;
}

/* end of i8272.c */
