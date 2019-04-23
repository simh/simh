/* sim_scsi.c: SCSI bus simulation

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

#include "sim_scsi.h"
#include "sim_disk.h"
#include "sim_tape.h"

/* SCSI commands */

#define CMD_TESTRDY     0x00                            /* test unit ready */
#define CMD_INQUIRY     0x12                            /* inquiry */
#define CMD_REQSENSE    0x03                            /* request sense */
#define CMD_RDBLKLIM    0x05                            /* read block limits */
#define CMD_MODESEL6    0x15                            /* mode select (6 bytes) */
#define CMD_MODESEL10   0x55                            /* mode select (10 bytes) */
#define CMD_MODESENSE6  0x1A                            /* mode sense (6 bytes) */
#define CMD_MODESENSE10 0x5A                            /* mode sense (10 bytes) */
#define CMD_STARTSTOP   0x1B                            /* start/stop unit */
#define CMD_LOADUNLOAD  0x1B                            /* load/unload unit */
#define CMD_PREVALLOW   0x1E                            /* prevent/allow medium removal */
#define CMD_RDCAP       0x25                            /* read capacity */
#define CMD_READ6       0x08                            /* read (6 bytes) */
#define CMD_READ10      0x28                            /* read (10 bytes) */
#define CMD_RDLONG      0x3E                            /* read long */
#define CMD_WRITE6      0x0A                            /* write (6 bytes) */
#define CMD_WRITE10     0x2A                            /* write (10 bytes) */
#define CMD_ERASE       0x19                            /* erase */
#define CMD_RESERVE     0x16                            /* reserve unit */
#define CMD_RELEASE     0x17                            /* release unit */
#define CMD_REWIND      0x01                            /* rewind */
#define CMD_SNDDIAG     0x1D                            /* send diagnostic */
#define CMD_SPACE       0x11                            /* space */
#define CMD_WRFMARK     0x10                            /* write filemarks */

/* SCSI status codes */

#define STS_OK          0                               /* good */
#define STS_CHK         2                               /* check condition */

/* SCSI sense keys */

#define KEY_OK          0                               /* no sense */
#define KEY_NOTRDY      2                               /* not ready */
#define KEY_ILLREQ      5                               /* illegal request */
#define KEY_PROT        7                               /* data protect */
#define KEY_BLANK       8                               /* blank check */
#define KEY_M_ILI       0x20                            /* incorrent length indicator */

/* Additional sense codes */

#define ASC_OK          0                               /* no additional sense information */
#define ASC_INVCOM      0x20                            /* invalid command operation code */
#define ASC_INVCDB      0x24                            /* invalid field in cdb */
#define ASC_NOMEDIA     0x3A                            /* media not present */

#define PUTL(b,x,v)     b[x] = (v >> 24) & 0xFF; \
                        b[x+1] = (v >> 16) & 0xFF; \
                        b[x+2] = (v >> 8) & 0xFF; \
                        b[x+3] = v & 0xFF
#define PUTW(b,x,v)     b[x] = (v >> 8) & 0xFF; \
                        b[x+1] = v & 0xFF
#define GETL(b,x)       ((b[x] << 24) | \
                         (b[x+1] << 16) | \
                         (b[x+2] << 8) | \
                          b[x+3])
#define GETW(b,x)       ((b[x] << 8)|b[x+1])

static const char *scsi_phases[] = {
    "DATO",                                             /* data out */
    "DATI",                                             /* data in */
    "CMD",                                              /* command */
    "STS",                                              /* status */
    "",                                                 /* invalid*/
    "",                                                 /* invalid*/
    "MSGO",                                             /* message out */
    "MSGI"                                              /* message in */
    };

/* Arbitrate for control of the bus */

t_bool scsi_arbitrate (SCSI_BUS *bus, uint32 initiator)
{
if (bus->initiator < 0) {                               /* bus free? */
    sim_debug (SCSI_DBG_BUS, bus->dptr,
       "Initiator %d won arbitration\n", initiator);
    bus->initiator = initiator;                         /* won arbitration */
    return TRUE;
    }
sim_debug (SCSI_DBG_BUS, bus->dptr,
   "Initiator %d lost arbitration\n", initiator);
return FALSE;                                           /* lost arbitration */
}

/* Release control of the bus */

void scsi_release (SCSI_BUS *bus)
{
if (bus->initiator < 0)                                 /* already free? */
    return;
sim_debug (SCSI_DBG_BUS, bus->dptr,
   "Initiator %d released bus\n", bus->initiator);
bus->phase = SCSI_DATO;                                 /* bus free state */
bus->initiator = -1;
bus->target = -1;
bus->buf_t = bus->buf_b = 0;
}

/* Assert the attention signal */

void scsi_set_atn (SCSI_BUS *bus)
{
sim_debug (SCSI_DBG_BUS, bus->dptr,
   "Attention signal asserted\n");
bus->atn = TRUE;                                        /* assert ATN */
if (bus->target != -1)                                  /* target selected? */
    bus->phase = SCSI_MSGO;                             /* go to msg out phase */
}

/* Clear the attention signal */

void scsi_release_atn (SCSI_BUS *bus)
{
sim_debug (SCSI_DBG_BUS, bus->dptr,
   "Attention signal cleared\n");
bus->atn = FALSE;                                       /* release ATN */
}

/* Assert the request signal */

void scsi_set_req (SCSI_BUS *bus)
{
if (bus->req == FALSE) {
    sim_debug (SCSI_DBG_BUS, bus->dptr,
       "Request signal asserted\n");
    bus->req = TRUE;                                    /* assert REQ */
    }
}

/* Clear the attention signal */

void scsi_release_req (SCSI_BUS *bus)
{
if (bus->req == TRUE) {
    sim_debug (SCSI_DBG_BUS, bus->dptr,
       "Request signal cleared\n");
    bus->req = FALSE;                                   /* release REQ */
    }
}

void scsi_set_phase (SCSI_BUS *bus, uint32 phase)
{
if (bus->phase != phase) {
    sim_debug (SCSI_DBG_BUS, bus->dptr,
       "Phase changed to %s\n", scsi_phases[phase]);
    bus->phase = phase;
    }
}

/* Attempt to select a target device */

t_bool scsi_select (SCSI_BUS *bus, uint32 target)
{
UNIT *uptr = bus->dev[target];

if (bus->initiator < 0) {
    sim_debug (SCSI_DBG_BUS, bus->dptr,
        "SCSI: Attempted to select a target without arbitration\n");
    return FALSE;
    }
if (bus->target >= 0) {
    sim_debug (SCSI_DBG_BUS, bus->dptr,
        "SCSI: Attempted to select a target when a target is already selected\n");
    return FALSE;
    }
if ((uptr->flags & UNIT_DIS) == 0) {                    /* unit enabled? */
    sim_debug (SCSI_DBG_BUS, bus->dptr,
       "Select target %d%s\n", target, (bus->atn ? " with attention" : ""));
    if (bus->atn)
        scsi_set_phase (bus, SCSI_MSGO);                /* message out */
    else
        scsi_set_phase (bus, SCSI_CMD);                 /* command */
    bus->target = target;
    scsi_set_req (bus);                                 /* request data */
    return TRUE;
    }
sim_debug (SCSI_DBG_BUS, bus->dptr,
   "Select timeout for target %d\n", target);
scsi_release (bus);
return FALSE;
}

/* Process a SCSI message */

uint32 scsi_message (SCSI_BUS *bus, uint8 *data, uint32 len)
{
uint32 used;

if (data[0] & 0x80) {                                   /* identify */
    bus->lun = (data[0] & 0xF);
    sim_debug (SCSI_DBG_MSG, bus->dptr,
        "Identify, LUN = %d\n", bus->lun);
    scsi_set_req (bus);                                 /* request data */
    used = 1;                                           /* message length */
    }
else if (data[0] == 0x1) {                              /* extended message */
    if (len < 2)
        return 0;                                       /* need more */
    if (len < (data[1] + 2u))
        return 0;                                       /* need more */
    sim_debug (SCSI_DBG_MSG, bus->dptr,
        "Extended message\n");
    scsi_set_req (bus);                                 /* request data */
    used = data[1] + 2;                                 /* extended message length */
    }
else if (data[0] == 0x6) {                              /* abort */
    sim_debug (SCSI_DBG_MSG, bus->dptr,
        "Abort\n");
    scsi_release (bus);                                 /* disconnect */
    used = 1;
    }
else if (data[0] == 0xc) {
    sim_debug (SCSI_DBG_MSG, bus->dptr,
        "Bus device reset\n");
    scsi_release (bus);                                 /* disconnect */
    used = 1;
    }
else {
    sim_printf ("SCSI: Unknown Message %02X\n", data[0]);
    used = len;                                         /* discard all bytes */
    }
scsi_set_phase (bus, SCSI_CMD);                         /* command phase next */
return used;
}

/* Send status to the initiator immediately */

void scsi_status (SCSI_BUS *bus, uint32 sts, uint32 key, uint32 asc)
{
bus->sense_key = key;
bus->sense_code = asc;
bus->buf[0] = sts;                                      /* status code */
bus->buf_b = 1;
scsi_set_phase (bus, SCSI_STS);                         /* status phase next */
scsi_set_req (bus);                                     /* request to send data */
}

/* Send status to the initiator at the end of transaction */

void scsi_status_deferred (SCSI_BUS *bus, uint32 sts, uint32 key, uint32 asc)
{
bus->status = sts;
bus->sense_key = key;
bus->sense_code = asc;
}

/* Decode the command group to get the command length */

uint32 scsi_decode_group (uint8 data)
{
uint32 group = (data >> 5) & 0x7;

switch (group) {
    case 0:                                             /* 6 byte commands */
        return 6;

    case 1:                                             /* 10 byte commands */
    case 2:
        return 10;

    case 3:                                             /* 12 byte commands */
        return 12;

    default:                                            /* vendor specific or reserved */
        return 0;
        }
}

/* Translate sim_tape status to SCSI status */

void scsi_tape_status (SCSI_BUS *bus, t_stat st)
{
switch (st) {

    case MTSE_OK:
        scsi_status_deferred (bus, STS_OK, KEY_OK, ASC_OK);
        break;

    case MTSE_TMK:
        scsi_status_deferred (bus, STS_CHK, (KEY_OK | 0x80), ASC_OK);
        bus->sense_qual = 1;                            /* filemark detected */
        break;

    case MTSE_RECE:                                     /* record in error */
    case MTSE_INVRL:                                    /* invalid rec lnt */
    case MTSE_IOERR:                                    /* IO error */
        scsi_status_deferred (bus, STS_CHK, KEY_OK, ASC_OK);
        break;

    case MTSE_FMT:
    case MTSE_UNATT:
    case MTSE_EOM:                                      /* end of medium */
        scsi_status_deferred (bus, STS_CHK, (KEY_BLANK | 0x40), ASC_OK);
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        scsi_status_deferred (bus, STS_CHK, (KEY_OK | 0x40), ASC_OK);
        break;

    case MTSE_WRP:                                      /* write protect */
        scsi_status_deferred (bus, STS_CHK, KEY_PROT, ASC_OK);
        break;
        }

return;
}

/* Limit the transfer count to the allocation specified
   by the SCSI command */

void scsi_check_alloc (SCSI_BUS *bus, uint32 alloc)
{
if (bus->buf_b > alloc)                                 /* check allocation */
    bus->buf_b = alloc;
}

/* Command - Test Unit Ready */

void scsi_test_ready (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];

sim_debug (SCSI_DBG_CMD, bus->dptr, "Test Unit Ready\n");

if (uptr->flags & UNIT_ATT)                             /* attached? */
    scsi_status (bus, STS_OK, KEY_OK, ASC_OK);          /* unit is ready */
else  
    scsi_status (bus, STS_CHK, KEY_NOTRDY, ASC_NOMEDIA); /* no media present */
}

/* Command - Inquiry */

void scsi_inquiry (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;

sim_debug (SCSI_DBG_CMD, bus->dptr, "Inquiry\n");

if ((bus->lun != 0) || (uptr->flags & UNIT_DIS)) {
    memset (&bus->buf[0], 0, 36);                       /* no such device or lun */
    bus->buf[0] = 0x7f;
    bus->buf_b += 36;
//    scsi_status (bus, STS_CHK, KEY_ILLREQ, ASC_INVCOM);
    }
else {
    bus->buf[bus->buf_b++] = (dev->pqual << 5) | dev->devtype;   /* device class */
#if 0
    if (data[0] & 0x01) {                               /* vital product data */

        switch (data[2]) {                              /* page code */

            case 0x00:                                  /* list of supported pages */
                bus->buf[bus->buf_b++] = 0x00;          /* page code */
                bus->buf[bus->buf_b++] = 0x00;          /* reserved */
                bus->buf[bus->buf_b++] = 0x02;          /* page length */
                bus->buf[bus->buf_b++] = 0x00;          /* page 0 is supported. */
                bus->buf[bus->buf_b++] = 0x80;          /* page 0x80 is supported. */
                break;

            case 0x80:                                  /* unit serial # page */
                bus->buf[bus->buf_b++] = 0x80;          /* page code */
                bus->buf[bus->buf_b++] = 0x00;          /* reserved */
                bus->buf[bus->buf_b++] = 4;             /* serial number length */
                sprintf (&bus->buf[l], "%-4s", "1234");
                l += 4;
                break;

            default:
                sim_printf ("SCSI: Vital product data page %02x not implemented\n", data[2]);
                }
        }
#endif
    if (dev->removeable)
        bus->buf[bus->buf_b++] = 0x80;                  /* removeable */
    else
        bus->buf[bus->buf_b++] = 0;                     /* fixed */
    bus->buf[bus->buf_b++] = dev->scsiver;              /* versions */
    bus->buf[bus->buf_b++] = dev->scsiver;              /* respose data format */
    bus->buf[bus->buf_b++] = 31;                        /* additional length */
    bus->buf[bus->buf_b++] = 0;                         /* reserved */
    bus->buf[bus->buf_b++] = 0;                         /* reserved */
    bus->buf[bus->buf_b++] = 0;

    sprintf ((char *)&bus->buf[bus->buf_b], "%-8s", dev->manufacturer);
    bus->buf_b += 8;
    sprintf ((char *)&bus->buf[bus->buf_b], "%-16s", dev->product);
    bus->buf_b += 16;
    sprintf ((char *)&bus->buf[bus->buf_b], "%-4s", dev->rev);
    bus->buf_b += 4;
    }

scsi_check_alloc (bus, data[4]);                        /* check allocation */
scsi_set_phase (bus, SCSI_DATI);                        /* data in phase next */
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Request Sense */

void scsi_req_sense (SCSI_BUS *bus, uint8 *data, uint32 len)
{
sim_debug (SCSI_DBG_CMD, bus->dptr, "Request Sense\n");

bus->buf[bus->buf_b++] = (0x70 | 0x80);                 /* current error, valid */
bus->buf[bus->buf_b++] = 0;                             /* segment # */
bus->buf[bus->buf_b++] = bus->sense_key;                /* sense key */
bus->buf[bus->buf_b++] = (bus->sense_info >> 24) & 0xFF; /* information */
bus->buf[bus->buf_b++] = (bus->sense_info >> 16) & 0xFF;
bus->buf[bus->buf_b++] = (bus->sense_info >> 8) & 0xFF;
bus->buf[bus->buf_b++] = bus->sense_info & 0xFF;
bus->buf[bus->buf_b++] = 10;                            /* additional length */
bus->buf[bus->buf_b++] = 0;                             /* cmd specific info */
bus->buf[bus->buf_b++] = 0;
bus->buf[bus->buf_b++] = 0;
bus->buf[bus->buf_b++] = 0;
bus->buf[bus->buf_b++] = bus->sense_code;               /* ASC */
bus->buf[bus->buf_b++] = bus->sense_qual;               /* ASCQ */
bus->buf[bus->buf_b++] = 0;                             /* FRU code */
bus->buf[bus->buf_b++] = 0;                             /* sense key specific */
bus->buf[bus->buf_b++] = 0;
bus->buf[bus->buf_b++] = 0;

bus->sense_key = 0;                                     /* no sense */
bus->sense_code = 0;                                    /* no additional sense information */
bus->sense_qual = 0;
bus->sense_info = 0;

scsi_check_alloc (bus, data[4]);                        /* check allocation */
scsi_set_phase (bus, SCSI_DATI);                        /* data in phase next */
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Mode Select (6 byte command) */

void scsi_mode_sel6 (SCSI_BUS *bus, uint8 *data, uint32 len)
{
if (bus->phase == SCSI_CMD) {
    sim_debug (SCSI_DBG_CMD, bus->dptr, "Mode Select(6) - CMD\n");
    memcpy (&bus->cmd[0], &data[0], 6);
    bus->buf_b = bus->cmd[4];
    scsi_set_phase (bus, SCSI_DATO);                    /* data out phase next */
    scsi_set_req (bus);                                 /* request data */
    }
else if (bus->phase == SCSI_DATO) {
    sim_debug (SCSI_DBG_CMD, bus->dptr, "Mode Select(6) - DATO\n");
    /* Not currently implemented so just return
       good status for now */
    scsi_status (bus, STS_OK, KEY_OK, ASC_OK);
    }
}

/* Command - Mode Select (10 byte command) */

void scsi_mode_sel10 (SCSI_BUS *bus, uint8 *data, uint32 len)
{
if (bus->phase == SCSI_CMD) {
    sim_debug (SCSI_DBG_CMD, bus->dptr, "Mode Select(10) - CMD\n");
    memcpy (&bus->cmd[0], &data[0], 10);
    bus->buf_b = GETW (data, 7);
    scsi_set_phase (bus, SCSI_DATO);                    /* data out phase next */
    scsi_set_req (bus);                                 /* request data */
    }
else if (bus->phase == SCSI_DATO) {
    sim_debug (SCSI_DBG_CMD, bus->dptr, "Mode Select(6) - DATO\n");
    /* Not currently implemented so just return
       good status for now */
    scsi_status (bus, STS_OK, KEY_OK, ASC_OK);
    }
}

/* Mode Sense common fields */

void scsi_mode_sense (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;
uint32 pc, pctl;

pc = data[2] & 0x3F;                                    /* page code */
pctl = (data[2] >> 6) & 0x3F;                           /* page control */

bus->buf[bus->buf_b++] = 0x00;                          /* density code */
bus->buf[bus->buf_b++] = ((uptr->capac - 1) >> 16) & 0xFF; /* # blocks (23:16) */
bus->buf[bus->buf_b++] = ((uptr->capac - 1) >> 8) & 0xFF; /* # blocks (15:8) */
bus->buf[bus->buf_b++] = (uptr->capac - 1) & 0xFF;      /* # blocks (7:0) */
bus->buf[bus->buf_b++] = 0x00;                          /* reserved */
bus->buf[bus->buf_b++] = (dev->block_size >> 16) & 0xFF;
bus->buf[bus->buf_b++] = (dev->block_size >> 8) & 0xFF;
bus->buf[bus->buf_b++] = (dev->block_size >> 0) & 0xFF;

if ((pc == 0x1) || (pc == 0x3F)) {
    bus->buf[bus->buf_b++] = 0x1;                       /* R/W error recovery page */
    bus->buf[bus->buf_b++] = 0xA;                       /* page length */
    bus->buf[bus->buf_b++] = 0x26;                      /* TB, PER, DTE */
    bus->buf[bus->buf_b++] = 0x8;                       /* read retry count */
    bus->buf[bus->buf_b++] = 0x78;                      /* correction span */
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0x8;                       /* write retry count */
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    }
if ((pc == 0x2) || (pc == 0x3F)) {
    bus->buf[bus->buf_b++] = 0x2;                       /* disconnect-reconnect page */
    bus->buf[bus->buf_b++] = 0xE;                       /* page length */
    bus->buf[bus->buf_b++] = 0x10;                      /* buffer full ratio */
    bus->buf[bus->buf_b++] = 0x10;                      /* buffer empty ratio */
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    }
if ((pc == 0x3) || (pc == 0x3F)) {
    bus->buf[bus->buf_b++] = 0x3;                       /* format device page */
    bus->buf[bus->buf_b++] = 0x16;                      /* page length */
    bus->buf[bus->buf_b++] = 0;                         /* tracks per zone (15:8) */
    bus->buf[bus->buf_b++] = 1;                         /* tracks per zone (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* alt sectors per zone (15:8) */
    bus->buf[bus->buf_b++] = 1;                         /* alt sectors per zone (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* alt tracks per zone (15:8) */
    bus->buf[bus->buf_b++] = 0;                         /* alt tracks per zone (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* alt tracks per unit (15:8) */
    bus->buf[bus->buf_b++] = 0;                         /* alt tracks per unit (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* sectors per track (15:8) */
    bus->buf[bus->buf_b++] = 0x21;                      /* sectors per track (7:0) */
    bus->buf[bus->buf_b++] = 0x2;                       /* bytes per sector (15:8) */
    bus->buf[bus->buf_b++] = 0;                         /* bytes per sector (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* interleave (15:8) */
    bus->buf[bus->buf_b++] = 0;                         /* interleave (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* track skew factor (15:8) */
    bus->buf[bus->buf_b++] = 0;                         /* track skew factor (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* cyl skew factor (15:8) */
    bus->buf[bus->buf_b++] = 0;                         /* cyl skew factor (7:0) */
    bus->buf[bus->buf_b++] = 0x40;                      /* flags */
    bus->buf[bus->buf_b++] = 0;                         /* reserved */
    bus->buf[bus->buf_b++] = 0;                         /* reserved */
    bus->buf[bus->buf_b++] = 0;                         /* reserved */
    }
if ((pc == 0x4) || (pc == 0x3F)) {
    bus->buf[bus->buf_b++] = 0x4;                       /* rigid disk geometry page */
    bus->buf[bus->buf_b++] = 0x16;                      /* page length */
    bus->buf[bus->buf_b++] = 0;                         /* # cyls (23:16) */
    bus->buf[bus->buf_b++] = 0x4;                       /* # cyls (15:8) */
    bus->buf[bus->buf_b++] = 0;                         /* # cyls (7:0) */
    bus->buf[bus->buf_b++] = 0x2;                       /* # heads */
    bus->buf[bus->buf_b++] = 0;                         /* start cyl for write precomp (23:16) */
    bus->buf[bus->buf_b++] = 0x4;                       /* start cyl for write precomp (15:8) */
    bus->buf[bus->buf_b++] = 0;                         /* start cyl for write precomp (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* start cyl for reduced write current (23:16) */
    bus->buf[bus->buf_b++] = 0x4;                       /* start cyl for reduced write current (15:8) */
    bus->buf[bus->buf_b++] = 0;                         /* start cyl for reduced write current (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* drive step rate (15:8) */
    bus->buf[bus->buf_b++] = 0x1;                       /* drive step rate (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* landing zone cyl (23:16) */
    bus->buf[bus->buf_b++] = 0x4;                       /* landing zone cyl (15:8) */
    bus->buf[bus->buf_b++] = 0;                         /* landing zone cyl (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* reserved, RPL */
    bus->buf[bus->buf_b++] = 0;                         /* rotational offet */
    bus->buf[bus->buf_b++] = 0;                         /* reserved */
    bus->buf[bus->buf_b++] = 0x1C;                      /* medium rotation rate (15:8) */
    bus->buf[bus->buf_b++] = 0x20;                      /* medium rotation rate (7:0) */
    bus->buf[bus->buf_b++] = 0;                         /* reserved */
    bus->buf[bus->buf_b++] = 0;                         /* reserved */
    }
if ((pc == 0xA) || (pc == 0x3F)) {
    bus->buf[bus->buf_b++] = 0xA;                       /* control mode page */
    bus->buf[bus->buf_b++] = 0x6;                       /* page length */
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    bus->buf[bus->buf_b++] = 0;
    }
}

/* Command - Mode Sense (6 byte command) */

void scsi_mode_sense6 (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;
uint32 pc, pctl;

sim_debug (SCSI_DBG_CMD, bus->dptr, "Mode Sense(6)\n");

pc = data[2] & 0x3F;                                    /* page code */
pctl = (data[2] >> 6) & 0x3F;                           /* page control */

if (pc == 0x8) {
    scsi_status (bus, STS_CHK, KEY_ILLREQ, ASC_INVCDB);
    return;
    }

memset (&bus->buf[0], 0, data[4]);                      /* allocation len */
bus->buf[bus->buf_b++] = 0x0;                           /* mode data length */
bus->buf[bus->buf_b++] = 0x0;                           /* medium type */
if (dev->devtype == SCSI_CDROM)
    bus->buf[bus->buf_b++] = 0x80;                      /* dev specific param */
else
    bus->buf[bus->buf_b++] = 0x0;                       /* dev specific param */
bus->buf[bus->buf_b++] = 0x8;                           /* block descriptor len */

scsi_mode_sense (bus, data, len);                        /* get common data */

bus->buf[0] = bus->buf_b - 1;                           /* mode data length */

scsi_check_alloc (bus, data[4]);                        /* check allocation */
scsi_set_phase (bus, SCSI_DATI);
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Mode Sense (10 byte command) */

void scsi_mode_sense10 (SCSI_BUS *bus, uint8 *data, uint32 len)
{
uint32 pc, pctl;

sim_debug (SCSI_DBG_CMD, bus->dptr, "Mode Sense(10)\n");

pc = data[2] & 0x3F;                                    /* page code */
pctl = (data[2] >> 6) & 0x3F;                           /* page control */

if (pc == 0x8) {
    scsi_status (bus, STS_CHK, KEY_ILLREQ, ASC_INVCDB);
    return;
    }

memset (&bus->buf[0], 0, GETW (data, 7));               /* allocation len */
bus->buf[bus->buf_b++] = 0x0;                           /* mode data length (15:8) */
bus->buf[bus->buf_b++] = 0x0;                           /* mode data length (7:0) */
bus->buf[bus->buf_b++] = 0x0;                           /* medium type */
bus->buf[bus->buf_b++] = 0x0;                           /* dev specific param */
bus->buf[bus->buf_b++] = 0x0;                           /* reserved */
bus->buf[bus->buf_b++] = 0x0;                           /* reserved */
bus->buf[bus->buf_b++] = 0x0;                           /* block descriptor len (15:8) */
bus->buf[bus->buf_b++] = 0x8;                           /* block descriptor len (7:0) */

scsi_mode_sense (bus, data, len);                        /* get common data */

PUTW (bus->buf, 0, (bus->buf_b - 1));                   /* mode data length */

scsi_check_alloc (bus, GETW (data, 7));                 /* check allocation */
scsi_set_phase (bus, SCSI_DATI);
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Start/Stop Unit */

void scsi_start_stop (SCSI_BUS *bus, uint8 *data, uint32 len)
{
sim_debug (SCSI_DBG_CMD, bus->dptr, "Start/Stop Unit\n");
scsi_status (bus, STS_OK, KEY_OK, ASC_OK);
}

/* Command - Prevent/Allow Medium Removal */

void scsi_prev_allow (SCSI_BUS *bus, uint8 *data, uint32 len)
{
sim_debug (SCSI_DBG_CMD, bus->dptr, "Prevent/Allow Medium Removal\n");
scsi_status (bus, STS_OK, KEY_OK, ASC_OK);
}

/* Command - Read Capacity */

void scsi_read_capacity (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;

sim_debug (SCSI_DBG_CMD, bus->dptr, "Read Capacity, pmi = %d\n", (data[8] & 0x1));

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    scsi_status (bus, STS_CHK, KEY_NOTRDY, ASC_NOMEDIA);
    return;
    }

PUTL (bus->buf, 0, (uptr->capac - 1));                  /* # blocks */
PUTL (bus->buf, 4, dev->block_size);                    /* block size */

bus->buf_b = 8;
scsi_set_phase (bus, SCSI_DATI);                        /* data in phase next */
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Read (6 byte command), disk version */

void scsi_read6_disk (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;
t_lba lba;
t_seccnt sects, sectsread;
t_stat r;

lba = GETW (data, 2) | ((data[1] & 0x1F) << 16);
sects = data[4];
if (sects == 0)
    sects = 256;

sim_debug (SCSI_DBG_CMD, bus->dptr, "Read(6) lba %d blks %d\n", lba, sects);

if (uptr->flags & UNIT_ATT)
    r = sim_disk_rdsect (uptr, lba, &bus->buf[0], &sectsread, sects);
else {
    memset (&bus->buf[0], 0, (sects * dev->block_size));
    sectsread = sects;
    }

bus->buf_b = (sectsread * dev->block_size);
scsi_set_phase (bus, SCSI_DATI);                        /* data in phase next */
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Read (6 byte command), tape version */

void scsi_read6_tape (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;
t_seccnt sects, sectsread;
t_stat r;

if ((data[1] & 0x3) == 0x3) {                           /* SILI and FIXED? */
    scsi_status (bus, STS_CHK, KEY_ILLREQ, ASC_INVCDB);
    return;
    }

sects = GETW (data, 3) | (data[2] << 16);
sectsread = 0;

if (sects == 0) {                                       /* no data to read */
    scsi_status (bus, STS_OK, KEY_OK, ASC_OK);
    return;
    }

sim_debug (SCSI_DBG_CMD, bus->dptr,
    "Read(6) blks %d fixed %d\n", sects, (data[1] & 0x1));

if (uptr->flags & UNIT_ATT) {
    if (data[1] & 0x1) {
        r = sim_tape_rdrecf (uptr, &bus->buf[0], &sectsread, (sects * dev->block_size));
        sim_debug (SCSI_DBG_CMD, bus->dptr,
            "Read tape blk %d, read %d, r = %d\n", sects, sectsread, r);
        }
    else {
        r = sim_tape_rdrecf (uptr, &bus->buf[0], &sectsread, sects);
        sim_debug (SCSI_DBG_CMD, bus->dptr,
            "Read tape max %d, read %d, r = %d\n", sects, sectsread, r);
        if (r == MTSE_INVRL) {                          /* overlength condition */
            sim_debug (SCSI_DBG_CMD, bus->dptr,
                "Overlength\n");
            if ((data[1] & 0x2) && (dev->block_size == 0)) { /* SILI set */
                sim_debug (SCSI_DBG_CMD, bus->dptr,
                    "SILI set\n");
                }
            else {
                sim_debug (SCSI_DBG_CMD, bus->dptr,
                    "SILI not set - check condition\n");
                scsi_status (bus, STS_CHK, (KEY_OK | KEY_M_ILI), ASC_OK);
                return;
                }
            }
        else if ((r == MTSE_OK) && (sectsread < sects)) {  /* underlength condition */
            sim_debug (SCSI_DBG_CMD, bus->dptr,
                "Underlength\n");
            if (data[1] & 0x2) {                        /* SILI set */
                sim_debug (SCSI_DBG_CMD, bus->dptr,
                    "SILI set\n");
                }
            else {
                sim_debug (SCSI_DBG_CMD, bus->dptr,
                    "SILI not set - check condition\n");
                scsi_status_deferred (bus, STS_CHK, (KEY_OK | KEY_M_ILI), ASC_OK);
                bus->sense_info = (sects - sectsread);
                }
            }
        }

    if (r != MTSE_OK) {
        sim_debug (SCSI_DBG_CMD, bus->dptr,
            "Read error, r = %d\n", r);
        }
        scsi_tape_status (bus, r);
    }
else {
    memset (&bus->buf[0], 0, (sects * dev->block_size));
    sectsread = (sects * dev->block_size);
    }

if (sectsread > 0) {
    bus->buf_b = sectsread;
    scsi_set_phase (bus, SCSI_DATI);                    /* data in phase next */
    }
else {
    bus->buf[bus->buf_b++] = bus->status;               /* status code */
    scsi_set_phase (bus, SCSI_STS);                     /* status phase next */
    }
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Read (10 byte command), disk version */

void scsi_read10_disk (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;
t_lba lba;
t_seccnt sects, sectsread;
t_stat r;

lba = GETL (data, 2);
sects = GETW (data, 7);

sim_debug (SCSI_DBG_CMD, bus->dptr, "Read(10) lba %d blks %d\n", lba, sects);

if (sects == 0) {                                       /* no data to read */
    scsi_status (bus, STS_OK, KEY_OK, ASC_OK);
    return;
    }

if (uptr->flags & UNIT_ATT)
    r = sim_disk_rdsect (uptr, lba, &bus->buf[0], &sectsread, sects);
else {
    memset (&bus->buf[0], 0, (sects * dev->block_size));
    sectsread = sects;
    }

bus->buf_b = (sectsread * dev->block_size);
scsi_set_phase (bus, SCSI_DATI);                        /* data in phase next */
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Read Long */
/* This command is needed by VMS for host-based volume shadowing */
/* See DKDRIVER */

void scsi_read_long (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
t_lba lba;
t_seccnt sects, sectsread;
t_stat r;

lba = GETL (data, 2);
sects = GETW (data, 7);

sim_debug (SCSI_DBG_CMD, bus->dptr, "Read Long lba %d bytes %d\n", lba, sects);

if (uptr->flags & UNIT_ATT)
    r = sim_disk_rdsect (uptr, lba, &bus->buf[0], &sectsread, ((sects >> 9) + 1));
else {
    memset (&bus->buf[0], 0, sects);
    }

bus->buf_b = sects;
scsi_set_phase (bus, SCSI_DATI);                        /* data in phase next */
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Write (6 byte command), disk version */

void scsi_write6_disk (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;
t_lba lba;
t_seccnt sects, sectswritten;
t_stat r;

if (bus->phase == SCSI_CMD) {
    sim_debug (SCSI_DBG_CMD, bus->dptr, "Write(6) - CMD\n");
    memcpy (&bus->cmd[0], &data[0], 6);
    sects = bus->cmd[4];
    if (sects == 0) sects = 256;
    bus->buf_b = (sects * dev->block_size);
    scsi_set_phase (bus, SCSI_DATO);                    /* data out phase next */
    scsi_set_req (bus);                                 /* request data */
    }
else if (bus->phase == SCSI_DATO) {
    sects = bus->cmd[4];
    if (sects == 0) sects = 256;
    lba = GETW (bus->cmd, 2) | ((bus->cmd[1] & 0x1F) << 16);
    sim_debug (SCSI_DBG_CMD, bus->dptr, "Write(6) - DATO, lba %d bytes %d\n", lba, sects);

    if (uptr->flags & UNIT_ATT)
        r = sim_disk_wrsect (uptr, lba, &bus->buf[0], &sectswritten, sects);

    memset (&bus->cmd[0], 0, 10);
    scsi_status (bus, STS_OK, KEY_OK, ASC_OK);
    }
}

/* Command - Write (6 byte command), tape version */

void scsi_write6_tape (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;
t_seccnt sects;
t_stat r;

if (bus->phase == SCSI_CMD) {
    sim_debug (SCSI_DBG_CMD, bus->dptr,
        "Write(6) - CMD\n");
    memcpy (&bus->cmd[0], &data[0], 6);                 /* save current cmd */
    sects = GETW (bus->cmd, 3) | (bus->cmd[2] << 16);
    if (data[1] & 0x1)                                  /* FIXED */
        sects = sects * dev->block_size;
    bus->buf_b = sects;
    scsi_set_phase (bus, SCSI_DATO);                    /* data out phase next */
    scsi_set_req (bus);                                 /* request data */
    }
else if (bus->phase == SCSI_DATO) {
    sects = GETW (bus->cmd, 3) | (bus->cmd[2] << 16);
    if (data[1] & 0x1)                                  /* FIXED */
        sects = sects * dev->block_size;
    sim_debug (SCSI_DBG_CMD, bus->dptr,
        "Write(6) - DATO, bytes %d\n", sects);

    if (uptr->flags & UNIT_ATT) {
        r = sim_tape_wrrecf (uptr, &bus->buf[0], sects);
        sim_debug (SCSI_DBG_CMD, bus->dptr,
            "Write(6) - DATO, r = %d\n", r);
        scsi_tape_status (bus, r);                      /* translate status */
        }
    else
        scsi_status_deferred (bus, STS_OK, KEY_OK, ASC_OK);

    memset (&bus->cmd[0], 0, 10);                       /* clear current cmd */
    scsi_status (bus, bus->status, bus->sense_key, bus->sense_code);
    }
}

/* Command - Write (10 byte command), disk version */

void scsi_write10_disk (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;
t_lba lba;
t_seccnt sects, sectswritten;
t_stat r;

if (bus->phase == SCSI_CMD) {
    sim_debug (SCSI_DBG_CMD, bus->dptr, "Write(10) - CMD\n");
    memcpy (&bus->cmd[0], &data[0], 10);
    sects = GETW (bus->cmd, 7);
    if (sects == 0)                                     /* no data to write */
        scsi_status (bus, STS_OK, KEY_OK, ASC_OK);
    else {
        bus->buf_b = (sects * dev->block_size);
        scsi_set_phase (bus, SCSI_DATO);                /* data out phase next */
        scsi_set_req (bus);                             /* request data */
        }
    }
else if (bus->phase == SCSI_DATO) {
    sects = GETW (bus->cmd, 7);
    lba = GETL (bus->cmd, 2);
    sim_debug (SCSI_DBG_CMD, bus->dptr, "Write(10) - DATO, lba %d bytes %d\n", lba, sects);

    if (uptr->flags & UNIT_ATT)
        r = sim_disk_wrsect (uptr, lba, &bus->buf[0], &sectswritten, sects);

    memset (&bus->cmd[0], 0, 10);
    scsi_status (bus, STS_OK, KEY_OK, ASC_OK);
    }
}

/* Command - Erase */

void scsi_erase (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;
t_stat r;

sim_debug (SCSI_DBG_CMD, bus->dptr, "Erase\n");

if (data[1] & 0x1)                                      /* LONG bit set? */
    r = sim_tape_wreom (uptr);                          /* erase to EOT */
else
    r = sim_tape_wrgap (uptr, dev->gaplen);             /* write gap */

scsi_tape_status (bus, r);
scsi_status (bus, bus->status, bus->sense_key, bus->sense_code);
}

/* Command - Reserve Unit */

void scsi_reserve_unit (SCSI_BUS *bus, uint8 *data, uint32 len)
{
sim_debug (SCSI_DBG_CMD, bus->dptr, "Reserve Unit\n");
scsi_status (bus, STS_OK, KEY_OK, ASC_OK);              /* GOOD status */
}

/* Command - Release Unit */

void scsi_release_unit (SCSI_BUS *bus, uint8 *data, uint32 len)
{
sim_debug (SCSI_DBG_CMD, bus->dptr, "Release Unit\n");
scsi_status (bus, STS_OK, KEY_OK, ASC_OK);              /* GOOD status */
}

/* Command - Rewind */

void scsi_rewind (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
t_stat r;

sim_debug (SCSI_DBG_CMD, bus->dptr, "Rewind\n");

r = sim_tape_rewind (uptr);

scsi_tape_status (bus, r);
scsi_status (bus, bus->status, bus->sense_key, bus->sense_code);
}

/* Command - Send Diagnostic */

void scsi_send_diag (SCSI_BUS *bus, uint8 *data, uint32 len)
{
sim_debug (SCSI_DBG_CMD, bus->dptr, "Send Diagnostic\n");

if (data[1] & 0x4)                                      /* selftest */
    scsi_status (bus, STS_OK, KEY_OK, ASC_OK);          /* GOOD status */
else
    scsi_status (bus, STS_CHK, KEY_ILLREQ, ASC_INVCDB);
}

/* Command - Space */

void scsi_space (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
uint32 code, skipped;
t_seccnt sects;
t_stat r;

code = data[1] & 0x7;
sects = GETW (data, 3) | (data[2] << 16);

sim_debug (SCSI_DBG_CMD, bus->dptr, "Space %d %s\n", sects, ((code == 0) ? "records" : "files"));

switch (code) {

    case 0:                                             /* blocks */
        if (sects & 0x800000) {                         /* reverse */
            sects = 0x1000000 - sects;
            r = sim_tape_sprecsr (uptr, sects, &skipped);
            }
        else                                            /* forwards */
            r = sim_tape_sprecsf (uptr, sects, &skipped);
        break;

    case 1:                                             /* filemarks */
        if (sects & 0x800000) {                         /* reverse */
            sects = 0x1000000 - sects;
            r = sim_tape_spfiler (uptr, sects, &skipped);
            }
        else                                            /* forwards */
            r = sim_tape_spfilef (uptr, sects, &skipped);
        break;
        }

scsi_tape_status (bus, r);
bus->buf[bus->buf_b++] = bus->status;                   /* status code */
scsi_set_phase (bus, SCSI_STS);                         /* status phase next */
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Write Filemarks */

void scsi_wrfmark (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
uint32 i;
t_seccnt sects;
t_stat r;

sim_debug (SCSI_DBG_CMD, bus->dptr, "Write Filemarks\n");

sects = GETW (data, 3) | (data[2] << 16);

for (i = 0; i < sects; i++) {
    r = sim_tape_wrtmk (uptr);
    if (r != MTSE_OK) break;
    }
    
scsi_tape_status (bus, r);
bus->buf[bus->buf_b++] = bus->status;                   /* status code */
scsi_set_phase (bus, SCSI_STS);                         /* status phase next */
scsi_set_req (bus);                                     /* request to send data */
}

/* Command - Read Block Limits */

void scsi_read_blklim (SCSI_BUS *bus, uint8 *data, uint32 len)
{
sim_debug (SCSI_DBG_CMD, bus->dptr, "Read Block Limits\n");

bus->buf[bus->buf_b++] = 0x00;                          /* reserved */
bus->buf[bus->buf_b++] = (MTR_MAXLEN >> 16) & 0xFF;     /* max block length (23:16) */
bus->buf[bus->buf_b++] = (MTR_MAXLEN >> 8) & 0xFF;      /* max block length (15:8) */
bus->buf[bus->buf_b++] = MTR_MAXLEN & 0xFF;             /* max block length (7:0) */
bus->buf[bus->buf_b++] = 0x00;                          /* min block length (15:8) */
bus->buf[bus->buf_b++] = 0x01;                          /* min block length (7:0) */
scsi_set_phase (bus, SCSI_DATI);                        /* data in phase next */
scsi_set_req (bus);                                     /* request to send data */
}

/* Command Load/Unload Unit */

void scsi_load_unload (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];

sim_debug (SCSI_DBG_CMD, bus->dptr, "Load/Unload\n");

if ((data[4] & 0x5) == 0x5) {                           /* EOT & Load? */
    scsi_status (bus, STS_CHK, KEY_ILLREQ, ASC_INVCDB); /* invalid combination */
    return;
    }
if ((data[4] & 0x1) == 0)
    sim_tape_detach (uptr);                             /* unload */
scsi_status (bus, STS_OK, KEY_OK, ASC_OK);              /* GOOD status */
}

/* Process a SCSI command for a direct-access device */

void scsi_disk_command (SCSI_BUS *bus, uint8 *data, uint32 len)
{
switch (data[0]) {

    /* FIXME: FORMAT UNIT */

    case CMD_INQUIRY:                                   /* mandatory */
        scsi_inquiry (bus, data, len);
        break;

    case CMD_MODESEL6:                                  /* optional */
        scsi_mode_sel6 (bus, data, len);
        break;

    case CMD_MODESEL10:                                 /* optional */
        scsi_mode_sel10 (bus, data, len);
        break;

    case CMD_MODESENSE6:                                /* optional */
        scsi_mode_sense6 (bus, data, len);
        break;

    case CMD_MODESENSE10:                               /* optional */
        scsi_mode_sense10 (bus, data, len);
        break;

    case CMD_PREVALLOW:                                 /* optional */
        scsi_prev_allow (bus, data, len);
        break;

    case CMD_READ6:                                     /* mandatory */
        scsi_read6_disk (bus, data, len);
        break;

    case CMD_READ10:                                    /* mandatory */
        scsi_read10_disk (bus, data, len);
        break;

    case CMD_RDCAP:                                     /* mandatory */
        scsi_read_capacity (bus, data, len);
        break;

    case CMD_RDLONG:                                    /* optional - needed by VMS volume shadowing */
        scsi_read_long (bus, data, len);
        break;

    case CMD_RELEASE:                                   /* mandatory */
        scsi_release_unit (bus, data, len);
        break;

    case CMD_REQSENSE:                                  /* mandatory */
        scsi_req_sense (bus, data, len);
        break;

    case CMD_RESERVE:                                   /* mandatory */
        scsi_reserve_unit (bus, data, len);
        break;

    case CMD_SNDDIAG:                                   /* mandatory */
        scsi_send_diag (bus, data, len);
        break;

    case CMD_STARTSTOP:                                 /* optional */
        scsi_start_stop (bus, data, len);
        break;

    case CMD_TESTRDY:                                   /* mandatory */
        scsi_test_ready (bus, data, len);
        break;

    case CMD_WRITE6:                                    /* optional */
        scsi_write6_disk (bus, data, len);
        break;

    case CMD_WRITE10:                                   /* optional */
        scsi_write10_disk (bus, data, len);
        break;

    default:
        sim_printf ("SCSI: unknown disk command %02X\n", data[0]);
        scsi_status (bus, STS_CHK, KEY_ILLREQ, ASC_INVCOM);
        break;
        }
}

/* Process a SCSI command for a sequential-access device */

void scsi_tape_command (SCSI_BUS *bus, uint8 *data, uint32 len)
{
switch (data[0]) {

    case CMD_ERASE:                                     /* mandatory */
        scsi_erase (bus, data, len);
        break;

    case CMD_INQUIRY:                                   /* mandatory */
        scsi_inquiry (bus, data, len);
        break;

    case CMD_MODESEL6:                                  /* mandatory */
        scsi_mode_sel6 (bus, data, len);
        break;

    case CMD_MODESEL10:                                 /* optional */
        scsi_mode_sel10 (bus, data, len);
        break;

    case CMD_MODESENSE6:                                /* mandatory */
        scsi_mode_sense6 (bus, data, len);
        break;

    case CMD_MODESENSE10:                               /* optional */
        scsi_mode_sense10 (bus, data, len);
        break;

    case CMD_PREVALLOW:                                 /* optional */
        scsi_prev_allow (bus, data, len);
        break;

    case CMD_READ6:                                     /* mandatory */
        scsi_read6_tape (bus, data, len);
        break;

    case CMD_RDBLKLIM:                                  /* mandatory */
        scsi_read_blklim (bus, data, len);
        break;

    case CMD_RELEASE:                                   /* mandatory */
        scsi_release_unit (bus, data, len);
        break;

    case CMD_REQSENSE:                                  /* mandatory */
        scsi_req_sense (bus, data, len);
        break;

    case CMD_RESERVE:                                   /* mandatory */
        scsi_reserve_unit (bus, data, len);
        break;

    case CMD_REWIND:                                    /* mandatory */
        scsi_rewind (bus, data, len);
        break;

    case CMD_SNDDIAG:                                   /* mandatory */
        scsi_send_diag (bus, data, len);
        break;

    case CMD_SPACE:                                     /* mandatory */
        scsi_space (bus, data, len);
        break;

    case CMD_LOADUNLOAD:                                /* optional */
        scsi_load_unload (bus, data, len);
        break;

    case CMD_TESTRDY:                                   /* mandatory */
        scsi_test_ready (bus, data, len);
        break;

    case CMD_WRITE6:                                    /* mandatory */
        scsi_write6_tape (bus, data, len);
        break;

    case CMD_WRFMARK:                                   /* mandatory */
        scsi_wrfmark (bus, data, len);
        break;

    default:
        sim_printf ("SCSI: unknown tape command %02X\n", data[0]);
        scsi_status (bus, STS_CHK, KEY_ILLREQ, ASC_INVCOM);
        break;
        }
}

/* Process a SCSI command for a CD-ROM device */

void scsi_cdrom_command (SCSI_BUS *bus, uint8 *data, uint32 len)
{
switch (data[0]) {

    case CMD_INQUIRY:                                   /* mandatory */
        scsi_inquiry (bus, data, len);
        break;

    case CMD_MODESEL6:                                  /* optional */
        scsi_mode_sel6 (bus, data, len);
        break;

    case CMD_MODESEL10:                                 /* optional */
        scsi_mode_sel10 (bus, data, len);
        break;

    case CMD_MODESENSE6:                                /* optional */
        scsi_mode_sense6 (bus, data, len);
        break;

    case CMD_MODESENSE10:                               /* optional */
        scsi_mode_sense10 (bus, data, len);
        break;

    case CMD_PREVALLOW:                                 /* optional */
        scsi_prev_allow (bus, data, len);
        break;

    case CMD_READ6:                                     /* optional */
        scsi_read6_disk (bus, data, len);
        break;

    case CMD_READ10:                                    /* mandatory */
        scsi_read10_disk (bus, data, len);
        break;

    case CMD_RDCAP:                                     /* mandatory */
        scsi_read_capacity (bus, data, len);
        break;

    case CMD_RDLONG:                                    /* optional */
        scsi_read_long (bus, data, len);
        break;

    case CMD_RELEASE:                                   /* mandatory */
        scsi_release_unit (bus, data, len);
        break;

    case CMD_REQSENSE:                                  /* mandatory */
        scsi_req_sense (bus, data, len);
        break;

    case CMD_RESERVE:                                   /* mandatory */
        scsi_reserve_unit (bus, data, len);
        break;

    case CMD_SNDDIAG:                                   /* mandatory */
        scsi_send_diag (bus, data, len);
        break;

    case CMD_STARTSTOP:                                 /* optional */
        scsi_start_stop (bus, data, len);
        break;

    case CMD_TESTRDY:                                   /* mandatory */
        scsi_test_ready (bus, data, len);
        break;

    default:
        sim_printf ("SCSI: unknown CD-ROM command %02X\n", data[0]);
        scsi_status (bus, STS_CHK, KEY_ILLREQ, ASC_INVCOM);
        break;
        }
}

/* Process data for CMD phase */

uint32 scsi_command (SCSI_BUS *bus, uint8 *data, uint32 len)
{
UNIT *uptr = bus->dev[bus->target];
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;
uint32 cmd_len;

cmd_len = scsi_decode_group (data[0]);

if (len < cmd_len)                                      /* all command bytes received? */
    return 0;                                           /* no, need more */
bus->status = STS_OK;

switch (dev->devtype) {

    case SCSI_DISK:
    case SCSI_WORM:                                     /* same as disk for now */
        scsi_disk_command (bus, data, len);
        break;

    case SCSI_TAPE:
        scsi_tape_command (bus, data, len);
        break;

    case SCSI_CDROM:
        scsi_cdrom_command (bus, data, len);
        break;

    default:
        sim_printf ("SCSI: commands unimplemented for device type %d\n", dev->devtype);
        break;
        }

return cmd_len;
}

/* Process data for DATO phase */

uint32 scsi_data (SCSI_BUS *bus, uint8 *data, uint32 len)
{
uint32 i;

for (i = 0; ((i < len) && (bus->buf_t != bus->buf_b)); i++, bus->buf_t++)
    bus->buf[bus->buf_t] = data[i];
if (bus->buf_t == bus->buf_b) {
    bus->buf_t = 0;
    scsi_command (bus, &bus->cmd[0], bus->buf_b);
    }
else
    scsi_set_req (bus);                                 /* request data */
return i;
}

/* Write data to the SCSI bus */

uint32 scsi_write (SCSI_BUS *bus, uint8 *data, uint32 len)
{
uint32 left = len;
uint32 bc;
uint8 *buf;

scsi_release_req (bus);                                 /* assume done */
for (buf = data; left > 0; buf += bc, left -= bc) {
    switch (bus->phase) {
        case SCSI_DATO:
            bc = scsi_data (bus, buf, left);
            break;
        case SCSI_MSGO:
            bc = scsi_message (bus, buf, left);
            break;
        case SCSI_CMD:
            bc = scsi_command (bus, buf, left);
            break;
        default:
            return (len - left);
            }
    if (bc == 0) {                                      /* no data processed? */
        scsi_set_req (bus);                             /* request more */
        return (len - left);
        }
    }
switch (bus->phase) {                                   /* new phase */
    case SCSI_DATI:                                     /* data in */
    case SCSI_STS:                                      /* status */
    case SCSI_MSGI:                                     /* message in */
        if (bus->buf_t != bus->buf_b)                   /* data to return? */
            scsi_set_req (bus);                         /* let initiator know */
        break;
    default:
        break;
        }
return (len - left);
}

/* Read data from the SCSI bus */

uint32 scsi_read (SCSI_BUS *bus, uint8 *data, uint32 len)
{
uint32 i;

if (len == 0) {
    *data = bus->buf[bus->buf_t];
    return 0;
    }
scsi_release_req (bus);                                 /* assume done */
for (i = 0; ((i < len) && (bus->buf_t != bus->buf_b)); i++, bus->buf_t++)
    data[i] = bus->buf[bus->buf_t];
if (bus->buf_t == bus->buf_b) {
    bus->buf_t = bus->buf_b = 0;
    switch (bus->phase) {
        case SCSI_DATI:                                 /* data in */
            scsi_set_phase (bus, SCSI_STS);
            bus->buf[bus->buf_b++] = bus->status;       /* status code */
            scsi_set_req (bus);
            break;
        case SCSI_STS:                                  /* status */
            scsi_set_phase (bus, SCSI_MSGI);
            bus->buf[bus->buf_b++] = 0;                 /* command complete */
            scsi_set_req (bus);
            break;
//        case SCSI_MSGI:                                 /* message in */
//            scsi_release (bus);
//            break;
        default:
            break;
            }
    }
else
    scsi_set_req (bus);
return i;
}

/* Get the state of the given SCSI device */

uint32 scsi_state (SCSI_BUS *bus, uint32 id)
{
if (bus->initiator == id)
    return SCSI_INIT;                                   /* device is initiator */
if (bus->target == id)
    return SCSI_TARG;                                   /* device is target */
return SCSI_DISC;                                       /* device is disconnected */
}

/* Add a unit to the SCSI bus */

void scsi_add_unit (SCSI_BUS *bus, uint32 id, UNIT *uptr)
{
bus->dev[id] = uptr;
}

/* Set the SCSI device parameters for a unit */

void scsi_set_unit (SCSI_BUS *bus, UNIT *uptr, SCSI_DEV *dev)
{
uptr->up7 = (void *)dev;
}

/* Reset a unit */

void scsi_reset_unit (UNIT *uptr)
{
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;

if (dev == NULL)
    return;

switch (dev->devtype) {
    case SCSI_DISK:
    case SCSI_WORM:
    case SCSI_CDROM:
        sim_disk_reset (uptr);
        break;
    case SCSI_TAPE:
        sim_tape_rewind (uptr);
        break;
        }
}

/* Reset the SCSI bus */

void scsi_reset (SCSI_BUS *bus)
{
sim_debug (SCSI_DBG_BUS, bus->dptr, "Bus reset\n");
bus->phase = SCSI_DATO;
bus->buf_t = bus->buf_b = 0;
bus->atn = FALSE;
bus->initiator = -1;
bus->target = -1;
bus->lun = 0;
//bus->sense_key = 6;                                     /* UNIT ATTENTION */
//bus->sense_code = 0x29;                                 /* POWER ON, RESET, OR BUS DEVICE RESET OCCURRED */
bus->sense_key = 0;
bus->sense_code = 0;
bus->sense_qual = 0;
bus->sense_info = 0;
}

/* Initial setup of SCSI bus */

t_stat scsi_init (SCSI_BUS *bus, uint32 maxfr)
{
if (bus->buf == NULL)
    bus->buf = (uint8 *)calloc (maxfr, sizeof(uint8));
if (bus->buf == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

/* Set device file format */

t_stat scsi_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;

if (dev == NULL)
    return SCPE_NOFNC;

switch (dev->devtype) {
    case SCSI_DISK:
    case SCSI_WORM:
    case SCSI_CDROM:
        return sim_disk_set_fmt (uptr, val, cptr, desc);
    case SCSI_TAPE:
        return sim_tape_set_fmt (uptr, val, cptr, desc);
    default:
        return SCPE_NOFNC;
        }
}

/* Show device file format */

t_stat scsi_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;

if (dev == NULL)
    return SCPE_NOFNC;

switch (dev->devtype) {
    case SCSI_DISK:
    case SCSI_WORM:
    case SCSI_CDROM:
        return sim_disk_show_fmt (st, uptr, val, desc);
    case SCSI_TAPE:
        return sim_tape_show_fmt (st, uptr, val, desc);
    default:
        return SCPE_OK;
        }
}

/* Set/clear hardware write lock */

t_stat scsi_set_wlk (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
return SCPE_OK;
}

/* Show write lock status */

t_stat scsi_show_wlk (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
return SCPE_OK;
}

/* Attach device */

t_stat scsi_attach (UNIT *uptr, CONST char *cptr)
{
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;

if (dev == NULL)
    return SCPE_NOFNC;

switch (dev->devtype) {
    case SCSI_DISK:
    case SCSI_WORM:
    case SCSI_CDROM:
        return sim_disk_attach (uptr, cptr, dev->block_size, sizeof (uint8), (uptr->flags & SCSI_NOAUTO), SCSI_DBG_DSK, dev->name, 0, 0);
    case SCSI_TAPE:
        return sim_tape_attach (uptr, cptr);
    default:
        return SCPE_NOFNC;
        }
}

/* Dettach device */

t_stat scsi_detach (UNIT *uptr)
{
SCSI_DEV *dev = (SCSI_DEV *)uptr->up7;

if (dev == NULL)
    return SCPE_NOFNC;

switch (dev->devtype) {
    case SCSI_DISK:
    case SCSI_WORM:
    case SCSI_CDROM:
        return sim_disk_detach (uptr);                  /* detach unit */
    case SCSI_TAPE:
        return sim_tape_detach (uptr);                  /* detach unit */
    default:
        return SCPE_NOFNC;
        }
}

/* Show common SCSI help */

t_stat scsi_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "\nDisk drives on the SCSI bus can be attached to simulated storage in the\n");
fprintf (st, "following ways:\n\n");
sim_disk_attach_help (st, dptr, uptr, flag, cptr);
fprintf (st, "\nTape drives on the SCSI bus can be attached to simulated storage in the\n");
fprintf (st, "following ways:\n\n");
sim_tape_attach_help (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}
