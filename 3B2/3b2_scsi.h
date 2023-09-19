/* 3b2_scsi.h: CM195W SCSI Controller CIO Card

   Copyright (c) 2020-2022, Seth J. Morabito

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
#define HA_WRITE_BLK   0x0c
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
#define HA_MODESEL     0x15
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

#define HA_STAT(tc,ha_stat,cio_stat)        {         \
        ha_state.ts[tc].rep.ssb = (ha_stat);        \
        ha_state.ts[tc].rep.status = (cio_stat);    \
    }

/* Hardware Notes
 * ==============
 *
 * Disk Drives
 * -----------
 *
 * There are two emulated SCSI disk drives available.
 *
 * 1. 155 MB CDC Wren III (CDC 94161-9)
 * 2. 327 MB CDC Wren IV  (CDC 94171-9)
 *
 * The CDC 94161-9 was also OEMed as the "AT&T KS23483,L3"
 * The CDC 94171-9 was also OEMed as the "AT&T KS23483,L25"
 *
 *
 * Tape Drive
 * ----------
 *
 * Wangtek 5125EN (AT&T Part number KS23417,L2)
 *
 * DC600A cartridge tape at 120MB (QIC-120 format)
 *
 */


/* Other SCSI hard disk types
 * ---------------------------
 *
 * These geometries are supported natively and automatically
 * by System V Release 3.2.3 UNIX.
 *
 *  1. CDC 94161-9   155 MB/148 MiB  512 B/s, 35 s/t, 9 head,  965 cyl
 *  2. AT&T KS23483  327 MB/312 MiB  512 B/s, 46 s/t, 9 head, 1547 cyl
 *  (a.k.a CDC 94171-9)
 *
 * Also supported was a SCSI-to-ESDI bridge controller that used the
 * Emulex MD23/S2 SCSI-to-ESDI bridge. It allowed up to four ESDI
 * drives to be mapped as LUNs 0-3.
 *
 */

/* AT&T 155 MB Hard Disk (35 sec/t, 9 hd, 964 cyl) */
#define SD155_PQUAL      0x00
#define SD155_SCSI       1
#define SD155_BLK        512
#define SD155_SECT       35
#define SD155_SURF       9
#define SD155_CYL        964
#define SD155_LBN        303660
#define SD155_TEXT       "Set 155MB Disk Type"
#define SD155_MANU       "AT&T"
#define SD155_DESC       "KS23483"
#define SD155_REV        "0000"

/* AT&T 300 MB Hard Disk (43 sec/t, 9 hd, 1514 cyl) */
#define SD300_PQUAL      0x00
#define SD300_SCSI       1
#define SD300_BLK        512
#define SD300_SECT       43
#define SD300_SURF       9
#define SD300_CYL        1515
#define SD300_LBN        585937
#define SD300_TEXT       "Set 300MB Disk Type"
#define SD300_MANU       "AT&T"
#define SD300_DESC       "KS23483"
#define SD300_REV        "0000"

/* AT&T 327 MB Hard Disk (46 sec/t, 9 hd, 1547 cyl) */
#define SD327_PQUAL      0x00
#define SD327_SCSI       1
#define SD327_BLK        512
#define SD327_SECT       46
#define SD327_SURF       9
#define SD327_CYL        1547
#define SD327_LBN        640458
#define SD327_TEXT       "Set 327MB Disk Type"
#define SD327_MANU       "AT&T"
#define SD327_DESC       "KS23483"
#define SD327_REV        "0000"

/* AT&T 630 MB Hard Disk (56 sec/t, 16 hd, 1447 cyl) */
#define SD630_PQUAL      0x00
#define SD630_SCSI       1
#define SD630_BLK        512
#define SD630_SECT       56
#define SD630_SURF       16
#define SD630_CYL        1447
#define SD630_LBN        1296512
#define SD630_TEXT       "Set 630MB Disk Type"
#define SD630_MANU       "AT&T"
#define SD630_DESC       "KS23483"
#define SD630_REV        "0000"

/* Wangtek 120MB cartridge tape */
#define ST120_PQUAL      0x00
#define ST120_SCSI       1
#define ST120_BLK        512
#define ST120_LBN        1
#define ST120_TEXT       "Set Wangtek 120MB Tape Type"
#define ST120_MANU       "WANGTEK"
#define ST120_DESC       "KS23465"
#define ST120_REV        "CX17"

#define HA_DISK(d)      {    DRV_SCSI(                       \
        SCSI_DISK, d##_PQUAL, d##_SCSI,  FALSE,     d##_BLK, \
        d##_SECT,  d##_SURF,  d##_CYL,                       \
        d##_LBN,   d##_MANU,  d##_DESC,  d##_REV,   #d,   0, \
        d##_TEXT)                                            \
    }

#define HA_TAPE(d)      {    DRV_SCSI(                       \
        SCSI_TAPE, d##_PQUAL, d##_SCSI,  TRUE,      d##_BLK, \
        0,         0,         0,                             \
        d##_LBN,   d##_MANU,  d##_DESC,  d##_REV,   #d,   0, \
        d##_TEXT)                                            \
    }

#define HA_SIZE(d)      d##_LBN

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
 * SCSI Target state
 */
typedef struct {
    t_bool pending;       /* Service pending */
    ha_req req;           /* SCSI job request */
    ha_resp rep;          /* SCSI job reply */
} ha_ts;

/*
 * General SCSI HA internal state.
 */
typedef struct {
    uint8   slot;             /* Card Backsplane Slot # */
    uint32  pump_state;
    t_bool  frq;              /* Fast Request Queue enabled */
    uint8   edt[HA_EDT_LEN];  /* Equipped Device Table */
    ha_ts   ts[8];            /* Target state */
} HA_STATE;

t_stat ha_show_type(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ha_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ha_reset(DEVICE *dptr);
t_stat ha_svc(UNIT *uptr);
t_stat ha_rq_svc(UNIT *uptr);
t_stat ha_attach(UNIT *uptr, CONST char *cptr);
t_stat ha_detach(UNIT *uptr);

void ha_fast_queue_check();
void ha_sysgen(uint8 slot);
void ha_express(uint8 slot);
void ha_full(uint8 slot);

/* Fast Completion */

void ha_fcm_express(uint8 target);

#endif /* _3B2_SCSI_H_ */
