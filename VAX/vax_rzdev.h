/* vax_rzdev.h: DEC SCSI devices

   Copyright (c) 2019, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.
*/

#ifndef _VAX_RZDEV_H_
#define _VAX_RZDEV_H_

#include "sim_scsi.h"

#define RZ23_TYPE       SCSI_DISK
#define RZ23_PQUAL      0
#define RZ23_SCSI       1
#define RZ23_FLGS       0
#define RZ23_BLK        512
#define RZ23_LBN        204864
#define RZ23_SECTS      33
#define RZ23_SURFS      4
#define RZ23_CYLS       776
#define RZ23_MANU       "DEC"
#define RZ23_DESC       "RZ23     (C) DEC"
#define RZ23_REV        "0A18"

#define RZ23L_TYPE      SCSI_DISK
#define RZ23L_PQUAL     0
#define RZ23L_SCSI      1
#define RZ23L_FLGS      0
#define RZ23L_BLK       512
#define RZ23L_LBN       237588
#define RZ23L_SECTS     39
#define RZ23L_SURFS     4
#define RZ23L_CYLS      1524
#define RZ23L_MANU      "DEC"
#define RZ23L_DESC      "RZ23L    (C) DEC"
#define RZ23L_REV       "2528"

#define RZ24_TYPE       SCSI_DISK
#define RZ24_PQUAL      0
#define RZ24_SCSI       1
#define RZ24_FLGS       0
#define RZ24_BLK        512
#define RZ24_LBN        409792
#define RZ24_SECTS      38
#define RZ24_SURFS      8
#define RZ24_CYLS       1348
#define RZ24_MANU       "DEC"
#define RZ24_DESC       "RZ24     (C) DEC"
#define RZ24_REV        "4041"

#define RZ24L_TYPE      SCSI_DISK
#define RZ24L_PQUAL     0
#define RZ24L_SCSI      2
#define RZ24L_FLGS      0
#define RZ24L_BLK       512
#define RZ24L_LBN       479350
#define RZ24L_SECTS     66
#define RZ24L_SURFS     4
#define RZ24L_CYLS      1818
#define RZ24L_MANU      "DEC"
#define RZ24L_DESC      "RZ24L    (C) DEC"
#define RZ24L_REV       "4766"

#define RZ25_TYPE       SCSI_DISK
#define RZ25_PQUAL      0
#define RZ25_SCSI       2
#define RZ25_FLGS       0
#define RZ25_BLK        512
#define RZ25_LBN        832527
#define RZ25_SECTS      62
#define RZ25_SURFS      9
#define RZ25_CYLS       1492
#define RZ25_MANU       "DEC"
#define RZ25_DESC       "RZ25     (C) DEC"
#define RZ25_REV        "0700"

#define RZ25L_TYPE      SCSI_DISK
#define RZ25L_PQUAL     0
#define RZ25L_SCSI      2
#define RZ25L_FLGS      0
#define RZ25L_BLK       512
#define RZ25L_LBN       1046206
#define RZ25L_SECTS     79
#define RZ25L_SURFS     8
#define RZ25L_CYLS      1891
#define RZ25L_MANU      "DEC"
#define RZ25L_DESC      "RZ25L    (C) DEC"
#define RZ25L_REV       "0008"

#define RZ26_TYPE       SCSI_DISK
#define RZ26_PQUAL      0
#define RZ26_SCSI       2
#define RZ26_FLGS       0
#define RZ26_BLK        512
#define RZ26_LBN        2050860
#define RZ26_SECTS      57
#define RZ26_SURFS      14
#define RZ26_CYLS       2570
#define RZ26_MANU       "DEC"
#define RZ26_DESC       "RZ26     (C) DEC"
#define RZ26_REV        "0700"

#define RZ26L_TYPE      SCSI_DISK
#define RZ26L_PQUAL     0
#define RZ26L_SCSI      2
#define RZ26L_FLGS      0
#define RZ26L_BLK       512
#define RZ26L_LBN       2050860
#define RZ26L_SECTS     57
#define RZ26L_SURFS     14
#define RZ26L_CYLS      2570
#define RZ26L_MANU      "DEC"
#define RZ26L_DESC      "RZ26L    (C) DEC"
#define RZ26L_REV       "0008"

#define RZ55_TYPE       SCSI_DISK
#define RZ55_PQUAL      0
#define RZ55_SCSI       1
#define RZ55_FLGS       0
#define RZ55_BLK        512
#define RZ55_LBN        648437
#define RZ55_SECTS      36
#define RZ55_SURFS      15
#define RZ55_CYLS       1224
#define RZ55_MANU       "DEC"
#define RZ55_DESC       "RZ55"
#define RZ55_REV        "0900"

#define RRD40_TYPE      SCSI_CDROM
#define RRD40_PQUAL     0
#define RRD40_SCSI      1
#define RRD40_FLGS      DRVFL_RMV
#define RRD40_BLK       512
#define RRD40_LBN       1160156
#define RRD40_SECTS     150     /* ??? */
#define RRD40_SURFS     1
#define RRD40_CYLS      7800    /* ??? */
#define RRD40_MANU      "DEC"
#define RRD40_DESC      "RRD40"
#define RRD40_REV       "250D"

#define RRD42_TYPE      SCSI_CDROM
#define RRD42_PQUAL     0
#define RRD42_SCSI      1
#define RRD42_FLGS      DRVFL_RMV
#define RRD42_BLK       512
#define RRD42_LBN       1160156
#define RRD42_SECTS     150     /* ??? */
#define RRD42_SURFS     1
#define RRD42_CYLS      7800    /* ??? */
#define RRD42_MANU      "DEC"
#define RRD42_DESC      "RRD42"
#define RRD42_REV       "1.1A"

#define RRW11_TYPE      SCSI_WORM
#define RRW11_PQUAL     0
#define RRW11_SCSI      1
#define RRW11_FLGS      DRVFL_RMV
#define RRW11_BLK       512
#define RRW11_LBN       1160156
#define RRW11_SECTS     150     /* ??? */
#define RRW11_SURFS     1
#define RRW11_CYLS      7800    /* ??? */
#define RRW11_MANU      "DEC"
#define RRW11_DESC      "RRW11"
#define RRW11_REV       "1.1A"

#define CDW900_TYPE     SCSI_WORM
#define CDW900_PQUAL    0
#define CDW900_SCSI     1
#define CDW900_FLGS     DRVFL_RMV
#define CDW900_BLK      512
#define CDW900_LBN      1160156
#define CDW900_SECTS    150     /* ??? */
#define CDW900_SURFS    1
#define CDW900_CYLS     7800    /* ??? */
#define CDW900_MANU     "SONY"
#define CDW900_DESC     "CDW-900E"
#define CDW900_REV      "1.13"

#define XR1001_TYPE     SCSI_WORM
#define XR1001_PQUAL    0
#define XR1001_SCSI     1
#define XR1001_FLGS     DRVFL_RMV
#define XR1001_BLK      512
#define XR1001_LBN      1160156
#define XR1001_SECTS    150     /* ??? */
#define XR1001_SURFS    1
#define XR1001_CYLS     7800    /* ??? */
#define XR1001_MANU     "JVC"
#define XR1001_DESC     "XR-W1001"
#define XR1001_REV      "1.1A"

#define TZK50_TYPE      SCSI_TAPE
#define TZK50_PQUAL     0x50
#define TZK50_SCSI      1
#define TZK50_FLGS      DRVFL_RMV
#define TZK50_BLK       512
#define TZK50_LBN       1160156
#define TZK50_SECTS     0     /* ??? */
#define TZK50_SURFS     0
#define TZK50_CYLS      0    /* ??? */
#define TZK50_MANU      "DEC"
#define TZK50_DESC      "TZK50"
#define TZK50_REV       "1.1A"

#define TZ30_TYPE       SCSI_TAPE
#define TZ30_PQUAL      0x30
#define TZ30_SCSI       1
#define TZ30_FLGS       DRVFL_RMV
#define TZ30_BLK        512
#define TZ30_LBN        1160156
#define TZ30_SECTS      0     /* ??? */
#define TZ30_SURFS      0
#define TZ30_CYLS       0    /* ??? */
#define TZ30_MANU       "DEC"
#define TZ30_DESC       "TZK50"
#define TZ30_REV        "1.1A"

#define RZU_TYPE     SCSI_DISK
#define RZU_PQUAL    0
#define RZU_SCSI     2
#define RZU_FLGS     DRVFL_RMV | DRVFL_SETSIZE
#define RZU_BLK      512
#define RZU_LBN      2500860
#define RZU_SECTS    150     /* ??? */
#define RZU_SURFS    15
#define RZU_CYLS     1200    /* ??? */
#define RZU_MANU     "SIMH"
#define RZU_DESC     "RZUSER"
#define RZU_REV      "0001"

#define RZ_DEV(d)                                   \
    { d##_SECTS, d##_SURFS, d##_CYLS,               \
      d##_LBN, #d, d##_BLK,                         \
      (DRVFL_TYPE_SCSI | d##_FLGS |                 \
       ((d##_TYPE == SCSI_CDROM) ? DRVFL_RO : 0)),  \
      ((d##_TYPE == SCSI_TAPE) ? "MK" : "DK"), 0, 0,\
      NULL, NULL, 0,0,0,0,0,0,0,0,0,0,              \
      d##_TYPE, d##_PQUAL, d##_SCSI,                \
      d##_MANU,  d##_DESC, d##_REV }
#define RZ_DEV_A(d,a)                               \
    { d##_SECTS, d##_SURFS, d##_CYLS,               \
      d##_LBN, #d, d##_BLK,                         \
      (DRVFL_TYPE_SCSI | d##_FLGS |                 \
       ((d##_TYPE == SCSI_CDROM) ? DRVFL_RO : 0)),  \
      ((d##_TYPE == SCSI_TAPE) ? "MK" : "DK"), 0, 0,\
      #a, NULL, 0,0,0,0,0,0,0,0,0,0,                \
      d##_TYPE, d##_PQUAL, d##_SCSI,                \
      d##_MANU,  d##_DESC, d##_REV }

static DRVTYP drv_tab[] = {
    RZ_DEV (RZ23),
    RZ_DEV (RZ23L),
    RZ_DEV (RZ24),
    RZ_DEV (RZ24L),
    RZ_DEV (RZ25),
    RZ_DEV (RZ25L),
    RZ_DEV (RZ26),
    RZ_DEV (RZ26L),
    RZ_DEV (RZ55),
    RZ_DEV_A (RRD40,CDROM),
    RZ_DEV (RRD42),
    RZ_DEV (RRW11),
    RZ_DEV (CDW900),
    RZ_DEV (XR1001),
    RZ_DEV (TZK50),
    RZ_DEV (TZ30),
    RZ_DEV_A (RZU,RZUSER),
    { 0 }
    };

#endif
