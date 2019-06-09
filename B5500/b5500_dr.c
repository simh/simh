/* b5500_dr.c: Burrioughs 5500 Drum controller

   Copyright (c) 2015, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "b5500_defs.h"

#if (NUM_DEVS_DR > 0) 

#define UNIT_DR         UNIT_ATTABLE | UNIT_DISABLE | UNIT_FIX | \
                        UNIT_BUFABLE | UNIT_MUSTBUF


/* in u4 is current address */
/* in u5       Bits 30-16 of W */
#define ADDR     u4
#define CMD      u5

#define DR_CHAN         000003  /* Channel number */
#define DR_RD           000004  /* Executing a read command */
#define DR_WR           000010  /* Executing a write command */
#define DR_RDY          000040  /* Device Ready */

#define AUXMEM          (1 << UNIT_V_UF)

t_stat              drm_srv(UNIT *);
t_stat              drm_boot(int32, DEVICE *);
t_stat              drm_attach(UNIT *, CONST char *);
t_stat              drm_detach(UNIT *);
t_stat              set_drum(UNIT * uptr, int32 val, CONST char *cptr,
                             void *desc);
t_stat              set_auxmem(UNIT * uptr, int32 val, CONST char *cptr,
                             void *desc);
t_stat              drm_help (FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *drm_description (DEVICE *);


MTAB                drm_mod[] = {
    {AUXMEM, 0, "DRUM", "DRUM", &set_drum, NULL, NULL, "Device is drum"},
    {AUXMEM, AUXMEM, "AUXMEM", "AUXMEM", &set_auxmem, NULL, NULL, "Device is memory unit"},
    {0}
};


UNIT                drm_unit[] = {
    {UDATA(&drm_srv, UNIT_DR, 32*1024)}, /* DRA */
    {UDATA(&drm_srv, UNIT_DR, 32*1024)}, /* DRB */
};

DEVICE              drm_dev = {
    "DR", drm_unit, NULL, drm_mod,
    NUM_DEVS_DR, 8, 15, 1, 8, 64,
    NULL, NULL, NULL, &drm_boot, &drm_attach, &drm_detach,
    NULL, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &drm_help, NULL, NULL,
    &drm_description
};




/* Start off a disk command */
t_stat drm_cmd(uint16 cmd, uint16 dev, uint8 chan, uint16 *wc, uint8 rd_flg)
{
    UNIT        *uptr;
    int         u = (dev==DRUM1_DEV)? 0: 1;

    uptr = &drm_unit[u];

    /* If unit disabled return error */
    if (uptr->flags & UNIT_DIS) {
        return SCPE_NODEV;
    }

    if ((uptr->flags & (UNIT_BUF)) == 0) {
        sim_debug(DEBUG_CMD, &drm_dev, "Drum not buffered\n\r");
        return SCPE_UNATT;
    }

    /* Check if drive is ready to recieve a command */
    if ((uptr->CMD & DR_RDY) == 0) 
        return SCPE_BUSY;

    uptr->CMD = chan;
    if (rd_flg) 
        uptr->CMD |= DR_RD;
    else
        uptr->CMD |= DR_WR;
    uptr->ADDR = cmd << 3;
    sim_debug(DEBUG_CMD, &drm_dev, "Drum access %s %06o\n\r",
                (uptr->CMD & DR_RD) ? "read" : "write", uptr->ADDR);
    sim_activate(uptr, 100);
    return SCPE_OK;
}
        

/* Handle processing disk controller commands */
t_stat drm_srv(UNIT * uptr)
{
    int                 chan = uptr->CMD & DR_CHAN;
    uint8               *ch = &(((uint8 *)uptr->filebuf)[uptr->ADDR]);
    
 
    /* Process for each unit */
    if (uptr->CMD & DR_RD) {
        /* Transfer one Character */
        if (chan_write_drum(chan, ch, 0)) {
                uptr->CMD = DR_RDY;
                chan_set_end(chan);
                return SCPE_OK;
        }
        uptr->ADDR++;
        if (uptr->ADDR > ((int32)uptr->capac << 3)) {
                sim_debug(DEBUG_CMD, &drm_dev, "Drum overrun\n\r");
                uptr->CMD = DR_RDY;
                chan_set_error(chan);
                chan_set_end(chan);
                return SCPE_OK;
        }
        sim_activate(uptr, 40);
    }

    /* Process for each unit */
    if (uptr->CMD & DR_WR) {
        /* Transfer one Character */
        if (chan_read_drum(chan, ch, 0)) {
                uptr->CMD = DR_RDY;
                chan_set_end(chan);
                return SCPE_OK;
        }
        uptr->ADDR++;
        if (uptr->ADDR > ((int32)uptr->capac << 3)) {
                sim_debug(DEBUG_CMD, &drm_dev, "Drum overrun\n\r");
                uptr->CMD = DR_RDY;
                chan_set_error(chan);
                chan_set_end(chan);
                return SCPE_OK;
        }
        sim_activate(uptr, 40);
    }

    return SCPE_OK;
}

/* Boot from given device */
t_stat
drm_boot(int32 unit_num, DEVICE * dptr)
{
    int         dev = (unit_num)? DRUM2_DEV:DRUM1_DEV;
    t_uint64    desc;

    desc = (((t_uint64)dev)<<DEV_V)|DEV_IORD|DEV_OPT|020LL;
    return chan_boot(desc);
}


t_stat
drm_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;
    int                 u = uptr - drm_unit;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;
    if ((sim_switches & SIM_SW_REST) == 0)
        uptr->CMD |= DR_RDY; 
    uptr->hwmark = uptr->capac;
    if (u) 
        iostatus |= DRUM2_FLAG;
    else 
        iostatus |= DRUM1_FLAG;
    return SCPE_OK;
}

t_stat
drm_detach(UNIT * uptr)
{
    t_stat              r;
    int                 u = uptr - drm_unit;
    if ((r = detach_unit(uptr)) != SCPE_OK)
        return r;
    uptr->CMD = 0;
    if (u) 
        iostatus &= ~DRUM2_FLAG;
    else 
        iostatus &= ~DRUM1_FLAG;
    return SCPE_OK;
}

t_stat
set_drum(UNIT * uptr, int32 val, CONST char *cptr, void *desc) {
    if ((uptr->flags & AUXMEM) == 0)
        return SCPE_OK;
    if (uptr->flags & UNIT_ATT) 
        drm_detach(uptr);
    uptr->flags |= UNIT_ATTABLE;
    return SCPE_OK;
}

t_stat
set_auxmem(UNIT * uptr, int32 val, CONST char *cptr, void *desc) {
    int                 u = uptr - drm_unit;

    if (uptr->flags & AUXMEM)
        return SCPE_OK;
    if (uptr->flags & UNIT_ATT) 
        detach_unit(uptr);
    uptr->flags &= ~UNIT_ATTABLE;
    if (uptr->filebuf == 0) {
        uptr->filebuf = calloc(uptr->capac, 8);
        uptr->flags |= UNIT_BUF;
    }
    uptr->CMD = DR_RDY; 
    if (u) 
        iostatus |= DRUM2_FLAG;
    else 
        iostatus |= DRUM1_FLAG;
    
    return SCPE_OK;
}

t_stat
drm_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  fprintf (st, "B430 Magnetic Drum or B6500 memory module\n\n");
  fprintf (st, "There are up to two drum units DR0 and DR1. These can either\n");
  fprintf (st, "be attached to a file or set to AUXMEM. Setting to AUXMEM causes\n");
  fprintf (st, "them to exist only during the given sim run. Setting back to DRUM\n");
  fprintf (st, "will clear whatever was stored on the drum. If the device is set\n");
  fprintf (st, "to DRUM it must be attached to a file which it will buffer until\n");
  fprintf (st, "the unit is detached, or the sim exits. MCP must be configured to\n");
  fprintf (st, "the drum\n\n");
  fprint_set_help (st, dptr) ;
  fprint_show_help (st, dptr) ;
  return SCPE_OK;
} 

const char *
drm_description (DEVICE *dptr)
{ 
   return "B430 Magnetic Drum or B6500 memory module";
}
#endif
