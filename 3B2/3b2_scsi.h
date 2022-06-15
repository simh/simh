/* 3b2_scsi.h: AT&T 3B2 SCSI (CM195W) Host Adapter

   Copyright (c) 2020, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
 */

#ifndef _3B2_SCSI_H_
#define _3B2_SCSI_H_

#include "3b2_defs.h"

/* CIO Opcodes */
#define HA_BOOT        0x0a
#define HA_READ_BLK    0x0b
#define HA_CNTRL       0x20
#define HA_VERS        0x40
#define HA_DL_EEDT     0x42
#define HA_UL_EEDT     0x43
#define HA_EDSD        0x44
#define HA_RESET       0x45

#define HA_TESTRDY     0x00
#define HA_FORMAT      0x04
#define HA_WRITE       0x0a
#define HA_INQUIRY     0x12
#define HA_MODESNS     0x1a
#define HA_RDCPCTY     0x25
#define HA_READ        0x08
#define HA_READEXT     0x28
#define HA_WRTEXT      0x2a
#define HA_VERIFY      0x2f

#define HA_PDLS_OFF    0x28

/* CIO Status */
#define CIO_TIMEOUT    0x65

#define HA_BOOT_ADDR   0x2004000
#define HA_PDINFO_ADDR 0x2004400

#define HA_ID          0x0100
#define HA_IPL         12

#define HA_GOOD        0x00
#define HA_CKCON       0x02

#define HA_DSD_DISK    0x100
#define HA_DSD_TAPE    0x101

#define HA_VERSION     0x01

#define SCQRESIZE      24
#define RAPP_LEN       (SCQRESIZE - 8)
#define SCQCESIZE      16
#define CAPP_LEN       (SCQCESIZE - 8)

#define FC_TC(x)       (((x) >> 3) & 7)
#define FC_LU(x)       ((x) & 7)

#define HA_EDT_LEN     1024

#define HA_BLKSZ       512

#define HA_MAX_CMD     12
#define INQUIRY_MAX    36

#define HA_STAT(ha_stat,cio_stat)        {     \
        ha_state.reply.ssb = (ha_stat);        \
        ha_state.reply.status = (cio_stat);    \
    }


/* CDC Wren IV 327 MB Hard Disk (AT&T KS-23483,L3) */
#define SD327_PQUAL     0x00
#define SD327_SCSI      1
#define SD327_BLK       512
#define SD327_LBN       640396
#define SD327_TEXT      "Set CDC 327MB Disk Type"
#define SD327_MANU      "AT&T"
#define SD327_DESC      "KS23483"
#define SD327_REV       "0001"           /* TODO: Find real rev */
#define SD327_TPZ       9
#define SD327_ASEC      3
#define SD327_ATPZ      0
#define SD327_ATPU      0
#define SD327_SPT       46
#define SD327_CYL       1549
#define SD327_HEADS     9
#define SD327_PREC      1200
#define SD327_RWC       1200
#define SD327_STEP      15
#define SD327_LZ        1549
#define SD327_RPM       3600

/* Wangtek 120MB cartridge tape (AT&T KS-23465) */
#define ST120_PQUAL      0x00
#define ST120_SCSI       1
#define ST120_BLK        512
#define ST120_LBN        266004
#define ST120_TEXT       "Set Wangtek 120MB Tape Type"
#define ST120_MANU       "WANGTEK"
#define ST120_DESC       "KS23465"
#define ST120_REV        "CX17"
#define ST120_DENS       5

#define HA_DISK(d)      {    DRV_SCSI(                       \
        SCSI_DISK, d##_PQUAL, d##_SCSI,  FALSE,     d##_BLK, \
        d##_LBN,   d##_MANU,  d##_DESC,  d##_REV,   #d,   0, \
        d##_TEXT)                                            \
    }
#define HA_TAPE(d)      {    DRV_SCSI(                       \
        SCSI_TAPE, d##_PQUAL, d##_SCSI,  TRUE,      d##_BLK, \
        d##_LBN,   d##_MANU,  d##_DESC,  d##_REV,   #d,   0, \
        d##_TEXT)                                            \
    }
#define HA_SIZE(d)      d##_LBN


/* Hardware Notes
 * ==============
 *
 * Disk Drive
 * ----------
 *
 * We emulate a 300-Megabyte Hard Disk, AT&T part number KS23483,L3.
 *
 * This is the same as a CDC/Imprimis Wren IV 94171-327
 *
 *      512 bytes per block
 *    1,520 cylinders
 *        2 alternate cylinders (1518 available)
 *       46 Sectors per Track
 *        3 Alternate Sectors per Track (43 available)
 *        9 tracks per cylinder (9 heads)
 *
 * Formatted Size: 587,466 blocks
 *
 *
 * Tape Drive
 * ----------
 *
 * Wangtek 5099EN (AT&T Part number KS23417,L2)
 *
 * DC600A cartridge tape
 *
 *      512 bytes per block
 *        9 tracks
 *   13,956 blocks per track
 *
 * Formatted Size: 125,604 blocks
 *
 */

#define HA_JOB_QUICK     0
#define HA_JOB_EXPRESS   1
#define HA_JOB_FULL      2

typedef uint8 ha_jobtype;

typedef struct {
    uint32 addr;
    uint32 len;
} haddr;

/*
 * SCSI Command Request
 */
typedef struct {
    uint8  op;              /* Destructured from the cmd byte array */
    uint8  tc;
    uint8  lu;
    uint32 timeout;

    uint8  dlen;
    haddr  daddr[48];       /* Support up to 48 transfer addresses */

    uint32 dma_lst;
    uint16 cmd_len;
    uint8  cmd[HA_MAX_CMD];
} ha_req;

/*
 * SCSI Command Response
 */
typedef struct {
    ha_jobtype type;         /* Job type */
    t_bool pending;          /* Pending or completed? */
    uint8 status;            /* Result Status */
    uint8 op;                /* Command Opcode */
    uint8 subdev;            /* XXTTTLLL; T=Target, L=LUN */
    uint8 ssb;               /* SCSI Status Byte */
    uint32 addr;             /* Response address */
    uint32 len;              /* Response length */
} ha_resp;

#define    PUMP_NONE      0
#define    PUMP_SYSGEN    1
#define    PUMP_COMPLETE  2

/*
 * General SCSI HA internal state.
 */
typedef struct {
    uint8   cid;              /* Card Backsplane Slot #        */
    uint32  pump_state;
    uint32  haddr;            /* Host address for read/write   */
    uint32  hlen;             /* Length for read or write      */
    t_bool  initialized;      /* Card has been initialized     */
    t_bool  frq;              /* Fast Request Queue enabled    */
    uint8   edt[HA_EDT_LEN];  /* Equipped Device Table         */
    ha_req  request;          /* Current job request           */
    ha_resp reply;            /* Current job reply             */
} HA_STATE;

t_stat ha_show_type(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ha_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ha_reset(DEVICE *dptr);
t_stat ha_svc(UNIT *uptr);
t_stat ha_rq_svc(UNIT *uptr);
t_stat ha_attach(UNIT *uptr, CONST char *cptr);
t_stat ha_detach(UNIT *uptr);

void ha_fast_queue_check();
void ha_sysgen(uint8 cid);
void ha_express(uint8 cid);
void ha_full(uint8 cid);

/* Fast Completion */

void ha_fcm_express();

#endif /* _3B2_SCSI_H_ */
