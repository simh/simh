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

#define RZ23_DTYPE      0
#define RZ23_TYPE       SCSI_DISK
#define RZ23_PQUAL      0
#define RZ23_SCSI       1
#define RZ23_RM         FALSE
#define RZ23_BLK        512
#define RZ23_LBN        204863
#define RZ23_MANU       "DEC"
#define RZ23_DESC       "RZ23     (C) DEC"
#define RZ23_REV        "0A18"

#define RZ23L_DTYPE     1
#define RZ23L_TYPE      SCSI_DISK
#define RZ23L_PQUAL     0
#define RZ23L_SCSI      1
#define RZ23L_RM        FALSE
#define RZ23L_BLK       512
#define RZ23L_LBN       237587
#define RZ23L_MANU      "DEC"
#define RZ23L_DESC      "RZ23L    (C) DEC"
#define RZ23L_REV       "2528"

#define RZ24_DTYPE      2
#define RZ24_TYPE       SCSI_DISK
#define RZ24_PQUAL      0
#define RZ24_SCSI       1
#define RZ24_RM         FALSE
#define RZ24_BLK        512
#define RZ24_LBN        409791
#define RZ24_MANU       "DEC"
#define RZ24_DESC       "RZ24     (C) DEC"
#define RZ24_REV        "4041"

#define RZ24L_DTYPE     3
#define RZ24L_TYPE      SCSI_DISK
#define RZ24L_PQUAL     0
#define RZ24L_SCSI      2
#define RZ24L_RM        FALSE
#define RZ24L_BLK       512
#define RZ24L_LBN       479349
#define RZ24L_MANU      "DEC"
#define RZ24L_DESC      "RZ24L    (C) DEC"
#define RZ24L_REV       "4766"

#define RZ25_DTYPE      4
#define RZ25_TYPE       SCSI_DISK
#define RZ25_PQUAL      0
#define RZ25_SCSI       2
#define RZ25_RM         FALSE
#define RZ25_BLK        512
#define RZ25_LBN        832526
#define RZ25_MANU       "DEC"
#define RZ25_DESC       "RZ25     (C) DEC"
#define RZ25_REV        "0700"

#define RZ25L_DTYPE     5
#define RZ25L_TYPE      SCSI_DISK
#define RZ25L_PQUAL     0
#define RZ25L_SCSI      2
#define RZ25L_RM        FALSE
#define RZ25L_BLK       512
#define RZ25L_LBN       1046205
#define RZ25L_MANU      "DEC"
#define RZ25L_DESC      "RZ25L    (C) DEC"
#define RZ25L_REV       "0008"

#define RZ26_DTYPE      6
#define RZ26_TYPE       SCSI_DISK
#define RZ26_PQUAL      0
#define RZ26_SCSI       2
#define RZ26_RM         FALSE
#define RZ26_BLK        512
#define RZ26_LBN        2050859
#define RZ26_MANU       "DEC"
#define RZ26_DESC       "RZ26     (C) DEC"
#define RZ26_REV        "0700"

#define RZ26L_DTYPE     7
#define RZ26L_TYPE      SCSI_DISK
#define RZ26L_PQUAL     0
#define RZ26L_SCSI      2
#define RZ26L_RM        FALSE
#define RZ26L_BLK       512
#define RZ26L_LBN       2050859
#define RZ26L_MANU      "DEC"
#define RZ26L_DESC      "RZ26L    (C) DEC"
#define RZ26L_REV       "0008"

#define RZ55_DTYPE      8
#define RZ55_TYPE       SCSI_DISK
#define RZ55_PQUAL      0
#define RZ55_SCSI       1
#define RZ55_RM         FALSE
#define RZ55_BLK        512
#define RZ55_LBN        648437
#define RZ55_MANU       "DEC"
#define RZ55_DESC       "RZ55"
#define RZ55_REV        "0900"

#define RRD40_DTYPE     9
#define RRD40_TYPE      SCSI_CDROM
#define RRD40_PQUAL     0
#define RRD40_SCSI      1
#define RRD40_RM        TRUE
#define RRD40_BLK       512
#define RRD40_LBN       1160156
#define RRD40_MANU      "DEC"
#define RRD40_DESC      "RRD40"
#define RRD40_REV       "250D"

#define RRD42_DTYPE     10
#define RRD42_TYPE      SCSI_CDROM
#define RRD42_PQUAL     0
#define RRD42_SCSI      1
#define RRD42_RM        TRUE
#define RRD42_BLK       512
#define RRD42_LBN       1160156
#define RRD42_MANU      "DEC"
#define RRD42_DESC      "RRD42"
#define RRD42_REV       "1.1A"

#define RRW11_DTYPE     11
#define RRW11_TYPE      SCSI_WORM
#define RRW11_PQUAL     0
#define RRW11_SCSI      1
#define RRW11_RM        TRUE
#define RRW11_BLK       512
#define RRW11_LBN       1160156
#define RRW11_MANU      "DEC"
#define RRW11_DESC      "RRW11"
#define RRW11_REV       "1.1A"

#define CDW900_DTYPE    12
#define CDW900_TYPE     SCSI_WORM
#define CDW900_PQUAL    0
#define CDW900_SCSI     1
#define CDW900_RM       TRUE
#define CDW900_BLK      512
#define CDW900_LBN      1160156
#define CDW900_MANU     "SONY"
#define CDW900_DESC     "CDW-900E"
#define CDW900_REV      "1.13"

#define XR1001_DTYPE    13
#define XR1001_TYPE     SCSI_WORM
#define XR1001_PQUAL    0
#define XR1001_SCSI     1
#define XR1001_RM       TRUE
#define XR1001_BLK      512
#define XR1001_LBN      1160156
#define XR1001_MANU     "JVC"
#define XR1001_DESC     "XR-W1001"
#define XR1001_REV      "1.1A"

#define TZK50_DTYPE     14
#define TZK50_TYPE      SCSI_TAPE
#define TZK50_PQUAL     0x50
#define TZK50_SCSI      1
#define TZK50_RM        TRUE
#define TZK50_BLK       512
#define TZK50_LBN       1160156
#define TZK50_MANU      "DEC"
#define TZK50_DESC      "TZK50"
#define TZK50_REV       "1.1A"

#define TZ30_DTYPE      15
#define TZ30_TYPE       SCSI_TAPE
#define TZ30_PQUAL      0x30
#define TZ30_SCSI       1
#define TZ30_RM         TRUE
#define TZ30_BLK        512
#define TZ30_LBN        1160156
#define TZ30_MANU       "DEC"
#define TZ30_DESC       "TZK50"
#define TZ30_REV        "1.1A"

#define RZU_DTYPE       16                              /* user defined */
#define RZU_TYPE        SCSI_DISK
#define RZU_PQUAL       0
#define RZU_SCSI        2
#define RZU_RM          TRUE
#define RZU_BLK         512
#define RZU_LBN         236328                          /* from RZ23 */
#define RZU_MANU        "SIMH"
#define RZU_DESC        "RZUSER"
#define RZU_REV         "0001"
#define RZU_MINC        10000                           /* min cap LBNs */
#define RZU_MAXC        4194303                         /* max cap LBNs */
#define RZU_EMAXC       2147483647                      /* ext max cap */

#define RZ_DEV(d) \
    { d##_TYPE, d##_PQUAL, d##_SCSI, d##_RM,  d##_BLK, \
      d##_LBN,  d##_MANU,  d##_DESC, d##_REV, #d }
#define RZ_SIZE(d)      d##_LBN

static struct scsi_dev_t rzdev_tab[] = {
    RZ_DEV (RZ23),
    RZ_DEV (RZ23L),
    RZ_DEV (RZ24),
    RZ_DEV (RZ24L),
    RZ_DEV (RZ25),
    RZ_DEV (RZ25L),
    RZ_DEV (RZ26),
    RZ_DEV (RZ26L),
    RZ_DEV (RZ55),
    RZ_DEV (RRD40),
    RZ_DEV (RRD42),
    RZ_DEV (RRW11),
    RZ_DEV (CDW900),
    RZ_DEV (XR1001),
    RZ_DEV (TZK50),
    RZ_DEV (TZ30),
    RZ_DEV (RZU),
    { 0 }
    };

#endif
