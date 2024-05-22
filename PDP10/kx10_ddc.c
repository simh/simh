/* kx10_ddc.c: Drum RES-10 Disk Controller.

   Copyright (c) 2013-2022, Richard Cornwell

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MEDDCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"

#ifndef NUM_DEVS_DDC
#define NUM_DEVS_DDC 0
#endif

#if (NUM_DEVS_DDC > 0)

#define DDC_DEVNUM       0440                    /* 0174 */
#define NUM_UNITS_DDC    4

/* Flags in the unit flags word */

#define UNIT_V_DTYPE    (UNIT_V_UF + 0)                 /* disk type */
#define UNIT_M_DTYPE    1
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

/* Parameters in the unit descriptor */

#define STATUS          u3              /* Current status */
#define POS             u4              /* Position in sector buffer */
#define UFLAGS          u5              /* Function */
#define SEC             us9             /* Sector counter */


/* CONI bits */
#define QUEUE_PAR       0400000000000LL
#define DDC_BSY         0000001000000LL
#define DDC_DON         0000000400000LL
#define DDC_CSE         0000000001000LL
#define DDC_QF          0000000000400LL
#define DDC_RDY         0000000000200LL    /* Drum Ready */
#define DDC_SPA         0000000000100LL    /* Drum Silo Parity Error */
#define DDC_NXM         0000000000040LL    /* NXM */
#define DDC_EXC         0000000000020LL    /* Exceed Capacity */
#define DDC_HUD         0000000000010LL    /* Drum Hung */
#define DDC_MPE         0000000000004LL    /* MPE */
#define DDC_OVR         0000000000002LL    /* Data overrun */
#define DDC_CKR         0000000000001LL    /* Checksum error */

/* CONO bits */
#define DDC_RST         0000000600000LL    /* Drum Reset */
#define DDC_CLR         0000000400000LL    /* Clear Int */
#define DDC_ERR         0000000200000LL    /* Clear Errors */
#define DDC_EXF         0000000100000LL    /* Execute FR */
#define DDC_EXQ         0000000040000LL    /* Execute Queue */

/* Command words */
#define DDC_CMD         0700000000000LL    /* Drum command */
#define DDC_SEQ         0003700000000LL    /* Sequence number */
#define DDC_PIA         0000070000000LL    /* PIA */
#define DDC_FUNC        0000006000000LL    /* Function */
#define DDC_READ        0000002000000LL
#define DDC_WRITE       0000004000000LL
#define DDC_DISK        0000001400000LL    /* Logical Disc */
#define DDC_TRK         0000000377600LL    /* Track */
#define DDC_SEC         0000000000177LL    /* Sector */

/* DataI bits */
  /*    DDC_SEC         0000000000177LL    Sector */
#define DDC_DONE        0400000000000LL    /* Done flag */

/* Drum Status */
#define DDC_PWB         0700000000000LL
#define DDC_SECCNT      0017700000000LL    /* Sequence counter */
#define DDC_ADDR        0000000777777LL

/* DataI */
/* 177 Sector number */

/* Number of sectors = 13 */
/* Sector size = 0200 words */



#define DDC10_WDS       0200

#define DDC_SIZE        (7000 * DDC10_WDS)

uint64          ddc_buf[DDC10_WDS];
uint64          ddc_cmd[16];
int             ddc_cmdptr;
int             ddc_putptr;

t_stat          ddc_devio(uint32 dev, uint64 *data);
t_stat          ddc_svc(UNIT *);
void            ddc_ini(UNIT *, t_bool);
t_stat          ddc_reset(DEVICE *);
t_stat          ddc_attach(UNIT *, CONST char *);
t_stat          ddc_detach(UNIT *);
t_stat          ddc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                     const char *cptr);
const char      *ddc_description (DEVICE *dptr);


UNIT                ddc_unit[] = {
/* Controller 1 */
    { UDATA (&ddc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, DDC_SIZE) },
    { UDATA (&ddc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, DDC_SIZE) },
    { UDATA (&ddc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, DDC_SIZE) },
    { UDATA (&ddc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, DDC_SIZE) },

};

DIB ddc_dib = {DDC_DEVNUM, 1, &ddc_devio, NULL};

MTAB                ddc_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED",
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED",
        &set_writelock, NULL,   NULL, "Write lock drive" },
    {0}
};

REG                 ddc_reg[] = {
    {0}
};

DEVICE              ddc_dev = {
    "DDC", ddc_unit, ddc_reg, ddc_mod,
    NUM_UNITS_DDC, 8, 18, 1, 8, 36,
    NULL, NULL, &ddc_reset, NULL, &ddc_attach, &ddc_detach,
    &ddc_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &ddc_help, NULL, NULL, &ddc_description
};


t_stat ddc_devio(uint32 dev, uint64 *data) {
     UNIT        *uptr;
     DEVICE      *dptr;

     dptr = &ddc_dev;
     uptr = &dptr->units[0];
     switch(dev & 3) {
     case CONI:
        sim_debug(DEBUG_CONI, dptr, "DDC %03o CONI %06o PC=%o\n", dev,
                          (uint32)*data, PC);
        *data = uptr->STATUS;
        if (ddc_cmdptr != ((ddc_putptr + 2) & 0xf)) {
            *data |= DDC_RDY;
        }
        if (ddc_cmdptr == ddc_putptr) {
            *data |= DDC_BSY;
        }
        *data |= ((t_uint64)uptr->UFLAGS) << 25;
        break;
     case CONO:
        if (*data & DDC_CLR) {  /* Clear irq */
            uptr->STATUS &= ~(DDC_DON);
            clr_interrupt(ddc_dib.dev_num);
        }
        if (*data & DDC_ERR) { /* Clear error */
            uptr->STATUS &= ~(DDC_SPA|DDC_NXM|DDC_EXC|DDC_HUD|DDC_MPE|
                             DDC_OVR|DDC_CKR|DDC_QF);
        }
        if (*data & DDC_EXF) {  /* Execute FR */
        }
        if (*data & DDC_EXQ) {  /* Execute Queue */
           if (!sim_is_active(uptr)) {
               sim_activate(uptr, 100);
               uptr->POS = 0;
           }
        }

         sim_debug(DEBUG_CONO, dptr, "DDC %03o CONO %06o PC=%o\n", dev,
                   (uint32)*data, PC);
         break;
     case DATAI:
         *data = (t_uint64)(uptr->SEC++);
         uptr->SEC &= 0177;
         if (uptr->SEC > (13 << 2))
            uptr->SEC = 0;
         if ((uptr->STATUS & DDC_DON) != 0)
            *data |= DDC_DONE;
         sim_debug(DEBUG_DATAIO, dptr, "DDC %03o DATI %012llo PC=%o\n",
                  dev, *data, PC);
         break;
     case DATAO:
         sim_debug(DEBUG_DATAIO, dptr, "DDC %03o DATO %012llo, PC=%o\n",
                  dev, *data, PC);
        /* Insert the command into the queue */
        if (((ddc_putptr + 1) & 0xf) != ddc_cmdptr) {
            int           func;
            int           pia;
            int           dsk;
            int           trk;
            int           sec;
            int           seq;
            ddc_cmd[ddc_putptr] = *data;
            sec = ddc_cmd[ddc_putptr] & DDC_SEC;
            trk = (ddc_cmd[ddc_putptr] & DDC_TRK) >> 7;
            dsk = (ddc_cmd[ddc_putptr] & DDC_DISK) >> 17;
            func = (ddc_cmd[ddc_putptr] & DDC_FUNC) >> 19;
            pia = (ddc_cmd[ddc_putptr] & DDC_PIA) >> 21;
            seq = (ddc_cmd[ddc_putptr] & DDC_SEQ) >> 24;
       sim_debug(DEBUG_DETAIL, dptr, "DDC %d cmd %d %d %d %d %o\n",
            dsk, trk, sec, func, pia, seq);

            ddc_putptr = (ddc_putptr + 1) & 0xf;
        } else {
            uptr->STATUS |= DDC_QF;
        }
        break;
    }
    return SCPE_OK;
}

t_stat ddc_svc (UNIT *uptr)
{
   int           wc;
   int           func;
   int           pia;
   int           dsk;
   int           trk;
   int           sec;
   int           seq;
   t_addr        adr;
   uint64        word;
   DEVICE       *dptr;
   UNIT         *duptr;

   dptr = &ddc_dev;
   sec = (ddc_cmd[ddc_cmdptr] & DDC_SEC) >> 2;
   trk = (ddc_cmd[ddc_cmdptr] & DDC_TRK) >> 7;
   dsk = (ddc_cmd[ddc_cmdptr] & DDC_DISK) >> 17;
   func = (ddc_cmd[ddc_cmdptr] & DDC_FUNC) >> 19;
   pia = (ddc_cmd[ddc_cmdptr] & DDC_PIA) >> 21;
   seq = (ddc_cmd[ddc_cmdptr] & DDC_SEQ) >> 24;
   word = ddc_cmd[ddc_cmdptr+1];
   adr = word & RMASK;
   duptr = &ddc_dev.units[dsk];

   if ((duptr->flags & UNIT_ATT) == 0) {
       uptr->STATUS |= DDC_DON|DDC_HUD;
       uptr->UFLAGS = seq;
       sim_debug(DEBUG_DETAIL, dptr, "DDC %d Set done %d %d\n", dsk, pia, seq);

       set_interrupt(ddc_dib.dev_num, pia);
       uptr->POS = 0;

       ddc_cmdptr += 2;
       ddc_cmdptr &= 0xf;
       if (ddc_cmdptr != ddc_putptr)
           sim_activate(uptr, 100);
   }

   if (uptr->POS == 0) {
       int da;
       da = ((trk * 13) + sec) * DDC10_WDS;
       (void)sim_fseek(duptr->fileref, da * sizeof(uint64), SEEK_SET);
       wc = sim_fread (&ddc_buf[0], sizeof(uint64),
                    DDC10_WDS, duptr->fileref);
       sim_debug(DEBUG_DETAIL, dptr, "DDC %d Read %d %d %d %d %d %o\n",
            dsk, da, trk, sec, func, pia, seq);
       for (; wc < DDC10_WDS; wc++)
            ddc_buf[wc] = 0;
   }

   if (func == 2) {
       if (Mem_write_word(adr, &ddc_buf[uptr->POS], 0)) {
           uptr->STATUS |= DDC_NXM;
           goto done;
       }
   } else if (func == 1) {
       if (Mem_read_word(adr, &ddc_buf[uptr->POS], 0)) {
          uptr->STATUS |= DDC_NXM;
          goto done;
       }
   }
   sim_debug(DEBUG_DATA, dptr, "DDC %d xfer %06o %012llo\n",
               dsk, adr, ddc_buf[uptr->POS]);
   uptr->POS++;
   word = (word & LMASK) | ((adr + 1) & RMASK);

   if (uptr->POS == DDC10_WDS) {
done:
       if (func == 2) {
          int da;
          da = ((trk * 13) + sec) * DDC10_WDS;
          (void)sim_fseek(duptr->fileref, da * sizeof(uint64), SEEK_SET);
          wc = sim_fwrite (&ddc_buf[0], sizeof(uint64),
                       DDC10_WDS, duptr->fileref);
          sim_debug(DEBUG_DETAIL, dptr, "DDC %d Write %d %d %d %d %d %o\n",
               dsk, da, trk, sec, func, pia, seq);
       }
       sec ++;
       ddc_cmd[ddc_cmdptr] &= ~DDC_SEC;
       ddc_cmd[ddc_cmdptr] |= (DDC_SEC & (sec << 2));
       word += 0000100000000LL;
       sim_debug(DEBUG_DETAIL, dptr, "DDC %d next sect %012llo %012llo\n", dsk, word, ddc_cmd[ddc_cmdptr]);
       if ((word & DDC_SECCNT) == 0) {
           ddc_cmd[ddc_cmdptr+1] = (word & (DDC_SECCNT|DDC_PWB)) | (adr & RMASK);
           uptr->STATUS |= DDC_DON;
           uptr->UFLAGS = seq;
           uptr->SEC = sec << 2;
           sim_debug(DEBUG_DETAIL, dptr, "DDC %d Set done %d %d\n", dsk, pia, seq);

           set_interrupt(ddc_dib.dev_num, pia);
           uptr->POS = 0;

           ddc_cmdptr += 2;
           ddc_cmdptr &= 0xf;
           if (ddc_cmdptr != ddc_putptr)
               sim_activate(uptr, 100);
           return SCPE_OK;
       }
       uptr->POS = 0;
   }
   ddc_cmd[ddc_cmdptr+1] = word;
   sim_activate(uptr, 100);
   return SCPE_OK;
}




t_stat
ddc_reset(DEVICE * dptr)
{
    UNIT   *uptr;
    int    i;
    ddc_cmdptr = ddc_putptr = 0;
    for (i = 0; i < NUM_UNITS_DDC; i++) {
        uptr = &dptr->units[i];
        uptr->SEC = 0;
        uptr->UFLAGS = 0;
        uptr->STATUS = 0;
    }
    return SCPE_OK;
}

/* Device attach */

t_stat ddc_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    r = attach_unit (uptr, cptr);
    if (r != SCPE_OK || (sim_switches & SIM_SW_REST) != 0)
        return r;
    return SCPE_OK;
}

/* Device detach */

t_stat ddc_detach (UNIT *uptr)
{
    if (!(uptr->flags & UNIT_ATT))             /* attached? */
        return SCPE_OK;
    if (sim_is_active (uptr))                  /* unit active? */
        sim_cancel (uptr);                     /* cancel operation */
    return detach_unit (uptr);
}

t_stat ddc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "DDC-10  Drum  Drives (DDC)\n\n");
fprintf (st, "The DDC controller implements the RES-10 disk controller that talked\n");
fprintf (st, "to drum drives.\n");
fprintf (st, "Options include the ability to set units write enabled or write locked, to\n");
fprintf (st, "set the drive type to one of two disk types\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *ddc_description (DEVICE *dptr)
{
return "DDC-10 disk controller";
}

#endif
