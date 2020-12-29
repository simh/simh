/* sim_scsi.h: SCSI bus simulation

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

#ifndef _SIM_SCSI_H_
#define _SIM_SCSI_H_     0

#include "sim_defs.h"
#include "sim_disk.h"
#include "sim_tape.h"

/* SCSI device states */

#define SCSI_DISC       0                               /* disconnected */
#define SCSI_TARG       1                               /* target mode */
#define SCSI_INIT       2                               /* initiator mode */

/* SCSI device types */

#define SCSI_DISK       0                               /* direct access device */
#define SCSI_TAPE       1                               /* sequential access device */
#define SCSI_PRINT      2                               /* printer */
#define SCSI_PROC       3                               /* processor */
#define SCSI_WORM       4                               /* write-once device */
#define SCSI_CDROM      5                               /* CD-ROM */
#define SCSI_SCAN       6                               /* scanner */
#define SCSI_OPTI       7                               /* optical */
#define SCSI_JUKE       8                               /* jukebox */
#define SCSI_COMM       9                               /* communications device */

/* SCSI bus phases */

#define SCSI_DATO       0                               /* data out */
#define SCSI_DATI       1                               /* data in */
#define SCSI_CMD        2                               /* command */
#define SCSI_STS        3                               /* status */
#define SCSI_MSGO       6                               /* message out */
#define SCSI_MSGI       7                               /* message in */

/* Debugging bitmaps */

#define SCSI_DBG_CMD    0x01000000                      /* SCSI commands */
#define SCSI_DBG_MSG    0x02000000                      /* SCSI messages */
#define SCSI_DBG_BUS    0x04000000                      /* bus activity */
#define SCSI_DBG_DSK    0x08000000                      /* disk activity */

#define SCSI_V_WLK      DKUF_V_WLK                      /* hwre write lock */
#define SCSI_V_NOAUTO   ((DKUF_V_UF > MTUF_V_UF) ? DKUF_V_UF : MTUF_V_UF)/* noautosize */
#define SCSI_V_UF       (SCSI_V_NOAUTO + 1)
#define SCSI_WLK        (1 << SCSI_V_WLK)
#define SCSI_NOAUTO     (1 << SCSI_V_NOAUTO)


struct scsi_dev_t {
    uint8 devtype;                                      /* device type */
    uint8 pqual;                                        /* peripheral qualifier */
    uint32 scsiver;                                     /* SCSI version */
    t_bool removeable;                                  /* removable flag */
    uint32 block_size;                                  /* device block size */
    uint32 lbn;                                         /* device size (blocks) */
    const char *manufacturer;                           /* manufacturer string */
    const char *product;                                /* product string */
    const char *rev;                                    /* revision string */
    const char *name;                                   /* gap length for tapes */
    uint32 gaplen;
    };

struct scsi_bus_t {
    DEVICE *dptr;                                       /* SCSI device */
    UNIT *dev[8];                                       /* target units */
    int32 initiator;                                    /* current initiator */
    int32 target;                                       /* current target */
    t_bool atn;                                         /* attention flag */
    t_bool req;                                         /* request flag */
    uint8 *buf;                                         /* transfer buffer */
    uint8 cmd[10];                                      /* command buffer */
    uint32 buf_b;                                       /* buffer bottom ptr */
    uint32 buf_t;                                       /* buffer top ptr */
    uint32 phase;                                       /* current bus phase */
    uint32 lun;                                         /* selected lun */
    uint32 status;
    uint32 sense_key;
    uint32 sense_code;
    uint32 sense_qual;
    uint32 sense_info;
};

typedef struct scsi_bus_t SCSI_BUS;
typedef struct scsi_dev_t SCSI_DEV;

t_bool scsi_arbitrate (SCSI_BUS *bus, uint32 initiator);
void scsi_release (SCSI_BUS *bus);
void scsi_set_atn (SCSI_BUS *bus);
void scsi_release_atn (SCSI_BUS *bus);
t_bool scsi_select (SCSI_BUS *bus, uint32 target);
uint32 scsi_write (SCSI_BUS *bus, uint8 *data, uint32 len);
uint32 scsi_read (SCSI_BUS *bus, uint8 *data, uint32 len);
uint32 scsi_state (SCSI_BUS *bus, uint32 id);
void scsi_add_unit (SCSI_BUS *bus, uint32 id, UNIT *uptr);
void scsi_set_unit (SCSI_BUS *bus, UNIT *uptr, SCSI_DEV *dev);
void scsi_reset_unit (UNIT *uptr);
void scsi_reset (SCSI_BUS *bus);
t_stat scsi_init (SCSI_BUS *bus, uint32 maxfr);

t_stat scsi_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat scsi_set_wlk (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat scsi_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat scsi_show_wlk (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat scsi_attach (UNIT *uptr, CONST char *cptr);
t_stat scsi_attach_ex (UNIT *uptr, CONST char *cptr, const char **drivetypes);
t_stat scsi_detach (UNIT *uptr);
t_stat scsi_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);

#endif
