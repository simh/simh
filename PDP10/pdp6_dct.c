/* ka10_dct.c: Type 136 Data Control

   Copyright (c) 2013-2019, Richard Cornwell

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

#include "kx10_defs.h"

#ifndef NUM_DEVS_DCT
#define NUM_DEVS_DCT 0
#endif

#if (NUM_DEVS_DCT > 0)

#define DCT_DEVNUM       0200    /* First device number */

#define STATUS u3

/* CONI/CONO Flags */
#define PIA             0000000000007LL
#define DEV             0000000000070LL
#define PACK            0000000000300LL
#define IN_OUT          0000000000400LL
#define DB_RQ           0000000001000LL     /* DCT has data for 10 or needs data */
#define DB_AC           0000000002000LL     /* DCT has completed a word. */
#define DB_MV           0000000004000LL     /* Data needs to be moved between buffers */
#define MISS            0000000010000LL
#define NUM_CHARS       0000000160000LL

uint64        dct_buf[NUM_DEVS_DCT];
uint64        dct_acc[NUM_DEVS_DCT];

t_stat        dct_devio(uint32 dev, uint64 *data);
t_stat        dct_svc(UNIT *);
t_stat        dct_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                 const char *cptr);
const char    *dct_description (DEVICE *dptr);


#if !PDP6
#define D DEV_DIS
#else
#define D 0
#endif

UNIT                dct_unit[] = {
/* Controller 1 */
    { UDATA (&dct_svc, UNIT_DISABLE, 0) },
    { UDATA (&dct_svc, UNIT_DISABLE, 0) },
};

DIB                 dct_dib[] = {
    {DCT_DEVNUM, NUM_DEVS_DCT, &dct_devio, NULL},
};

REG                 dct_reg[] = {
    {BRDATA(BUFF, dct_buf, 16, 36, NUM_DEVS_DCT), REG_HRO},
    {BRDATA(BUFF, dct_acc, 16, 36, NUM_DEVS_DCT), REG_HRO},
    {0}
};

DEVICE              dct_dev = {
    "DCT", dct_unit, dct_reg, NULL,
    NUM_DEVS_DCT, 8, 18, 1, 8, 36,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &dct_dib[0], DEV_DISABLE | DEV_DEBUG | D, 0, dev_debug,
    NULL, NULL, &dct_help, NULL, NULL, &dct_description
};



t_stat
dct_devio(uint32 dev, uint64 *data) {
     int            u;
     UNIT          *uptr = NULL;

     u = (dev - dct_dib[0].dev_num) >> 2;

     if (u >= NUM_DEVS_DCT)
        return SCPE_OK;

     uptr = &dct_unit[u];
     switch(dev & 3) {
     case CONI:
          *data = (uint64)(uptr->STATUS);
          sim_debug(DEBUG_CONI, &dct_dev, "DCT %03o CONI %012llo %d PC=%o\n", dev,
                             *data, u, PC);
          break;

     case CONO:
          clr_interrupt(dev);
          /* Clear flags */
          uptr->STATUS = *data & 017777;
          if (uptr->STATUS & DB_RQ)
              set_interrupt(dev, uptr->STATUS);
          sim_debug(DEBUG_CONO, &dct_dev, "DCT %03o CONO %06o %d PC=%o %06o\n", dev,
                  (uint32)*data, u, PC, uptr->STATUS);
          break;

     case DATAI:
          clr_interrupt(dev);
          if (uptr->STATUS & DB_RQ) {
              *data = dct_buf[u];
              uptr->STATUS &= ~(DB_RQ);
              uptr->STATUS |= DB_MV;
              sim_activate(uptr, 10);
          }
          sim_debug(DEBUG_DATAIO, &dct_dev, "DCT %03o DATI %012llo %d  PC=%o\n",
                  dev, *data, u, PC);
          break;

     case DATAO:
          clr_interrupt(dev);
          sim_debug(DEBUG_DATAIO, &dct_dev, "DCT %03o DATO %012llo, %d PC=%o\n",
                  dev, *data, u, PC);
          if (uptr->STATUS & DB_RQ) {
              dct_buf[u] = *data;
              uptr->STATUS &= ~(DB_RQ);
              uptr->STATUS |= DB_MV;
              sim_activate(uptr, 10);
          }
    }

    return SCPE_OK;
}

/* OUT = 0, dev-> 10, OUT=1 10 -> dev */

/* OUT starts with RQ & AC with MV =0 */
/* IN starts with MV = 1 RQ & AC = 0 */
t_stat
dct_svc (UNIT *uptr)
{
    int   u  = uptr - dct_unit;
    int   dev = dct_dib[0].dev_num + (u << 2);

    /* Transfer from 10 to device */
    if ((uptr->STATUS & (DB_MV|IN_OUT|DB_AC|DB_RQ)) == (DB_AC|DB_MV|IN_OUT)) {
        dct_acc[u] = dct_buf[u];
        uptr->STATUS &= ~(DB_MV|DB_AC);
        uptr->STATUS |= DB_RQ;
    }

    /* Tranfer from device to 10 */
    if ((uptr->STATUS & (DB_MV|IN_OUT|DB_AC|DB_RQ)) == (DB_AC|DB_MV)) {
        dct_buf[u] = dct_acc[u];
        uptr->STATUS &= ~(DB_MV|DB_AC);
        uptr->STATUS |= DB_RQ;
    }

    if (uptr->STATUS & DB_RQ)
       set_interrupt(dev, uptr->STATUS);
    return SCPE_OK;
}



/* Check if the dct is still connected to this device. */
int
dct_is_connect (int dev)
{
    int   d = dev & 07;
    int   u = (dev >> 3) & 07;
    UNIT  *uptr;

    /* Valid device? */
    if (u >= NUM_DEVS_DCT)
        return 0;
    uptr = &dct_unit[u];
    /* Is DCT pointed at this device? */
    if (((uptr->STATUS & DEV) >> 3) != d)
        return 0;
    /* If sending processor to device, and no data, terminate */
    if ((uptr->STATUS & IN_OUT) != 0 && (uptr->STATUS & DB_AC) != 0)
        return 0;
    /* Everything ok, still connected */
    return 1;
}

/* Read data from memory */
int
dct_read (int dev, uint64 *data, int cnt)
{
    int      d = dev & 07;
    int      u = (dev >> 3) & 07;
    DEVICE   *dptr = &dct_dev;
    UNIT     *uptr;

    /* Valid device? */
    if (u >= NUM_DEVS_DCT)
        return 0;

    uptr = &dct_unit[u];
    /* Is DCT pointed at this device? */
    if (((uptr->STATUS & DEV) >> 3) != d)
        return 0;

    /* Check if correct direction */
    if ((uptr->STATUS & IN_OUT) == 0)
        return 0;

    /* If we have data return it */
    if ((uptr->STATUS & DB_AC) == 0) {
        *data = dct_acc[u];
         sim_debug(DEBUG_DATA, dptr, "DCT Read %012llo, %d \n",
                 *data, u);
        uptr->STATUS &= ~(NUM_CHARS);
        uptr->STATUS |= DB_AC | DB_MV | ((cnt & 7) << 13);
        sim_activate(uptr, 20);
        return 1;
    }
    return 0;
}

/* Write data to memory */
int
dct_write (int dev, uint64 *data, int cnt)
{
    int      d = dev & 07;
    int      u = (dev >> 3) & 07;
    DEVICE   *dptr = &dct_dev;
    UNIT     *uptr;

    /* Valid device? */
    if (u >= NUM_DEVS_DCT)
        return 0;

    uptr = &dct_unit[u];
    /* Is DCT pointed at this device? */
    if (((uptr->STATUS & DEV) >> 3) != d)
        return 0;

    /* Check if correct direction */
    if ((uptr->STATUS & IN_OUT) != 0)
        return 0;

    /* If buffer is empty put data in it. */
    if ((uptr->STATUS & DB_AC) == 0) {
        dct_acc[u] = *data;
         sim_debug(DEBUG_DATA, dptr, "DCT Write %012llo, %d %06o\n", *data, u, uptr->STATUS);
        uptr->STATUS &= ~(NUM_CHARS);
        uptr->STATUS |= DB_AC | DB_MV | ((cnt & 7) << 13);
        sim_activate(uptr, 20);
        return 1;
    }
    return 0;
}


t_stat
dct_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Data Controller Type 136 is a data buffer between fast ");
fprintf (st, "devices and the PDP6. Individual devices are hooked up to ports ");
fprintf (st, "on each DCT.\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
return SCPE_OK;
}

const char *
dct_description (DEVICE *dptr)
{
return "Data Controller Type 136";
}

#endif
