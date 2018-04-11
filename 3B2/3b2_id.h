/* 3b2_cpu.h: AT&T 3B2 Model 400 Hard Disk (uPD7261) Header

   Copyright (c) 2017, Seth J. Morabito

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

#ifndef __3B2_ID_H__
#define __3B2_ID_H__

#include "3b2_defs.h"
#include "3b2_sysdev.h"
#include "sim_disk.h"

#define ID0             0
#define ID1             1
#define ID_CTLR         2

#define ID_DATA_REG     0
#define ID_CMD_STAT_REG 1

/* Command Codes (bits 3-7 of command byte) */

#define ID_CMD_AUX      0x00  /* Auxiliary Command */
#define ID_CMD_SIS      0x01  /* Sense int. status */
#define ID_CMD_SPEC     0x02  /* Specify           */
#define ID_CMD_SUS      0x03  /* Sense unit status */
#define ID_CMD_DERR     0x04  /* Detect Error      */
#define ID_CMD_RECAL    0x05  /* Recalibrate       */
#define ID_CMD_SEEK     0x06  /* Seek              */
#define ID_CMD_FMT      0x07  /* Format            */
#define ID_CMD_VID      0x08  /* Verify ID         */
#define ID_CMD_RID      0x09  /* Read ID           */
#define ID_CMD_RDIAG    0x0A  /* Read Diagnostic   */
#define ID_CMD_RDATA    0x0B  /* Read Data         */
#define ID_CMD_CHECK    0x0C  /* Check             */
#define ID_CMD_SCAN     0x0D  /* Scan              */
#define ID_CMD_VDATA    0x0E  /* Verify Data       */
#define ID_CMD_WDATA    0x0F  /* Write Data        */

#define ID_AUX_RST      0x01
#define ID_AUX_CLB      0x02
#define ID_AUX_HSRQ     0x04
#define ID_AUX_CLCE     0x08

#define ID_STAT_DRQ     0x01
#define ID_STAT_NCI     0x02
#define ID_STAT_IER     0x04
#define ID_STAT_RRQ     0x08
#define ID_STAT_SRQ     0x10
#define ID_STAT_CEL     0x20
#define ID_STAT_CEH     0x40
#define ID_STAT_CB      0x80

#define ID_IST_SEN      0x80    /* Seek End        */
#define ID_IST_RC       0x40    /* Ready Change    */
#define ID_IST_SER      0x20    /* Seek Error      */
#define ID_IST_EQC      0x10    /* Equipment Check */
#define ID_IST_NR       0x08    /* Not Ready       */

#define ID_UST_DSEL     0x10    /* Drive Selected  */
#define ID_UST_SCL      0x08    /* Seek Complete   */
#define ID_UST_TK0      0x04    /* Track 0         */
#define ID_UST_RDY      0x02    /* Ready           */
#define ID_UST_WFL      0x01    /* Write Fault     */

#define ID_EST_ENC      0x80
#define ID_EST_OVR      0x40
#define ID_EST_DER      0x20
#define ID_EST_EQC      0x10
#define ID_EST_NR       0x08
#define ID_EST_ND       0x04
#define ID_EST_NWR      0x02
#define ID_EST_MAM      0x01

#define ID_DTLH_POLL    0x10

#define ID_SEEK_NONE    -1
#define ID_SEEK_0       0
#define ID_SEEK_1       1

/* Drive Geometries */

/* Common across all drive types */
#define ID_SEC_SIZE        512
#define ID_SEC_CNT         18
#define ID_CYL_SIZE        ID_SEC_SIZE * ID_SEC_CNT

/* Specific to each drive type */
#define ID_MAX_DTYPE       3

#define ID_HD30_DTYPE      0
#define ID_HD30_CYL        697
#define ID_HD30_HEADS      5
#define ID_HD30_LBN        62730

#define ID_HD72_DTYPE      1
#define ID_HD72_CYL        925
#define ID_HD72_HEADS      9
#define ID_HD72_LBN        149850

#define ID_HD72C_DTYPE     2
#define ID_HD72C_CYL       754
#define ID_HD72C_HEADS     11
#define ID_HD72C_LBN       149292

/* The HD135 is actually just an HD161 with only 1024 cylinders
 * formatted. This is a software limitation, not hardware. */

#define ID_HD135_DTYPE     3
#define ID_HD135_CYL       1224
#define ID_HD135_HEADS     15
#define ID_HD135_LBN       330480

#define ID_HD161_DTYPE     3
#define ID_HD161_CYL       1224
#define ID_HD161_HEADS     15
#define ID_HD161_LBN       330480

#define ID_V_DTYPE         (DKUF_V_UF + 0)
#define ID_M_DTYPE         3
#define ID_DTYPE           (ID_M_DTYPE << ID_V_DTYPE)
#define ID_GET_DTYPE(x)    (((x) >> ID_V_DTYPE) & ID_M_DTYPE)
#define ID_DRV(d)          { ID_##d##_HEADS, ID_##d##_LBN }

#define ID_DSK_SIZE(d)     ID_##d##_LBN

/* Unit, Register, Device descriptions */

#define ID_FIFO_LEN    8
#define ID_IDFIELD_LEN 4

#define ID_NUM_UNITS   2

#define DMA_ID_SVC     IDBASE+ID_DATA_REG

extern DEVICE id_dev;
extern DEBTAB sys_deb_tab[];
extern t_bool id_drq;
extern t_bool id_int();

#define IDBASE 0x4a000
#define IDSIZE 0x2

#define CMD_NUM      ((id_cmd >> 4) & 0xf)

/* Function prototypes */

t_bool id_int();
t_stat id_ctlr_svc(UNIT *uptr);
t_stat id_unit_svc(UNIT *uptr);
t_stat id_reset(DEVICE *dptr);
t_stat id_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat id_attach(UNIT *uptr, CONST char *cptr);
t_stat id_detach(UNIT *uptr);
uint32 id_read(uint32 pa, size_t size);
void id_write(uint32 pa, uint32 val, size_t size);
CONST char *id_description(DEVICE *dptr);
t_stat id_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
void id_handle_data(uint8 val);
void id_handle_command(uint8 val);
void id_after_dma();

static SIM_INLINE t_lba id_lba(uint16 cyl, uint8 head, uint8 sec);

#endif
