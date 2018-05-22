/* 3b2_ctc.h: AT&T 3B2 Model 400 "CTC" feature card

   Copyright (c) 2018, Seth J. Morabito

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

/*
 * CTC is an intelligent feature card for the 3B2 that supports a
 * Cipher "FloppyTape(tm)" 525 drive that can read and write 23MB
 * DC600A cartridges.
 *
 * The CTC card is based on the Common I/O (CIO) platform.
 *
 * Notes:
 * ------
 *
 * The Cipher FloppyTape is an odd beast. Although it's a tape drive,
 * it is controlled by a floppy controller. It is divided into virtual
 * sectors that can be addressed by Cylinder / Track / Sector.
 * Stepping and head select pulses dictate where on the tape to read
 * from or write to. Moreover, System V maps a filesystem onto the
 * tape, and a properly formatted tape drive will have a VTOC on
 * partition 0.
 *
 */

#ifndef _3B2_CTC_H_
#define _3B2_CTC_H_

#include "3b2_defs.h"
#include "3b2_io.h"

#define UNIT_V_WLK    (DKUF_V_UF + 0)     /* Write-locked tape */
#define UNIT_WLK      (1 << UNIT_V_WLK)

#define CTC_ID        0x0005
#define CTC_IPL       12
#define CTC_VERSION   1

/* Request Opcodes */
#define CTC_CONFIG      30
#define CTC_CLOSE       31
#define CTC_FORMAT      32
#define CTC_OPEN        33
#define CTC_READ        34
#define CTC_WRITE       35
#define CTC_VWRITE      36

/* Completion Opcodes */
#define CTC_SUCCESS     0
#define CTC_HWERROR     32
#define CTC_RDONLY      33
#define CTC_NOTREADY    36
#define CTC_NOMEDIA     42

/* VTOC values */
#define VTOC_VERSION    1
#define VTOC_SECSZ      512
#define VTOC_PART       16          /* Number of "partitions" on tape */
#define VTOC_VALID      0x600DDEEE  /* Magic number for valid VTOC */

/* Physical Device Info (pdinfo) values */
#define PD_VALID        0xCA5E600D  /* Magic number for valid PDINFO */
#define PD_DRIVEID      5
#define PD_VERSION      0
#define PD_CYLS         6
#define PD_TRACKS       245
#define PD_SECTORS      31
#define PD_BYTES        512
#define PD_LOGICALST    29

#define CTC_CAPACITY    (PD_CYLS * PD_TRACKS * PD_SECTORS) /* In blocks */

struct partition {
    uint16 id;       /* Partition ID     */
    uint16 flag;     /* Permission Flags */
    uint32 sstart;   /* Starting Sector  */
    uint32 ssize;    /* Size in Sectors  */
};

struct vtoc {
    uint32 bootinfo[3];                /* n/a */
    uint32 sanity;                     /* magic number */
    uint32 version;                    /* layout version */
    uint8  volume[8];                  /* volume name */
    uint16 sectorsz;                   /* sector size in bytes */
    uint16 nparts;                     /* number of partitions */
    uint32 reserved[10];               /* free space */
    struct partition part[VTOC_PART];  /* partition headers */
    uint32 timestamp[VTOC_PART];       /* partition timestamp */
};

struct pdinfo {
    uint32 driveid;     /* identifies the device type */
    uint32 sanity;      /* verifies device sanity */
    uint32 version;     /* version number */
    uint8  serial[12];  /* serial number of the device */
    uint32 cyls;        /* number of cylinders per drive */
    uint32 tracks;      /* number tracks per cylinder */
    uint32 sectors;     /* number sectors per track */
    uint32 bytes;       /* number of bytes per sector */
    uint32 logicalst;   /* sector address of logical sector 0 */
    uint32 errlogst;    /* sector address of error log area */
    uint32 errlogsz;    /* size in bytes of error log area */
    uint32 mfgst;       /* sector address of mfg. defect info */
    uint32 mfgsz;       /* size in bytes of mfg. defect info */
    uint32 defectst;    /* sector address of the defect map */
    uint32 defectsz;    /* size in bytes of defect map */
    uint32 relno;       /* number of relocation areas */
    uint32 relst;       /* sector address of relocation area */
    uint32 relsz;       /* size in sectors of relocation area */
    uint32 relnext;     /* address of next avail reloc sector */
};

typedef struct {
    uint32 time;        /* Time used during a tape session (in 25ms chunks) */
} CTC_STATE;

extern DEVICE ctc_dev;

t_stat ctc_reset(DEVICE *dptr);
t_stat ctc_svc(UNIT *uptr);
t_stat ctc_attach(UNIT *uptr, CONST char *cptr);
t_stat ctc_detach(UNIT *uptr);
void ctc_sysgen(uint8 cid);
void ctc_express(uint8 cid);
void ctc_full(uint8 cid);

/* Largely here for debugging purposes */
static t_stat ctc_show_cqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat ctc_show_rqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat ctc_show_queue_common(FILE *st, UNIT *uptr, int32 val, CONST void *desc, t_bool rq);

#endif /* _3B2_CTC_H_ */
