/* ka10_dsk.c: 270 Disk Controller.

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
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MEDSKHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"

#ifndef NUM_DEVS_DSK
#define NUM_DEVS_DSK 0
#endif

#if (NUM_DEVS_DSK > 0)

#define DSK_DEVNUM       0270                    /* 0174 */
#define NUM_UNITS_DSK    4

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    1
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

/* Parameters in the unit descriptor */

#define CUR_CYL         u3              /* current cylinder */
#define DATAPTR         u4              /* data pointer */
#define UFLAGS          u5              /* Function */

/* CONO bits */
#define PIA             0000007
#define EIS             0000010    /* Enable Idle State */
#define EFR             0000020    /* Enable file ready */
#define EES             0000040    /* Enable end of sector */
#define EFE             0000100    /* Enable file error */
#define SCL             0000200    /* Clear error flags WO */
#define MRB             0000200    /* Meter read bad RO */
#define MRG             0000400    /* Meter read good RO */
#define CMD             0003000    /* Command */
#define WR_CMD          0002000    /* Command is write */
#define RD_CMD          0001000    /* Command is read */
#define END             0010000    /* End */
#define CLR             0020000    /* Clear */
#define MCL             0040000    /* Master clear RO */

/* CONI bits */
/* Upper 18 bits, same as CONO */
#define SECT_END        (1 << 18)  /* End of sector flag set */
#define DCE             0001000    /* Data clock error */
#define CME             0000400    /* Command error */
#define WLE             0000200    /* Write lock error */
#define ADE             0000100    /* Not operational */
#define ALM             0000040    /* ALARM */
#define DRL             0000020    /* Data request late */
#define RCE             0000010    /* Read Compare error */
#define PER             0000004    /* Parity error */
#define FER             0000002    /* File error */
#define OPR             0000001    /* Not operational */

/* Octoflop states */
#define IDS                0200    /* Selcted idle state */
#define SNA                0100    /* Selected new address */
#define ADT                0040    /* Address terminated */
#define DFR                0020    /* Disc file ready */
#define ALS                0010    /* Alert state */
#define CMS                0004    /* Command selected */
#define SCS                0002    /* Sector started */
#define SCE                0001    /* Sector end */

/* Start in IDS state.
 *
 * DATAO sets state to SNA.
 *   43 us later state to ADT.
 * State ADT:
 *   Seek cylinder/sector.
 *  DATAO resets to SNA.
 *   Find cylinder state to DFR.
 * State DFR:
 *   Remain in DFR for 1ms then set to ADT.
 *  CONO set state to ALS.
 *  DATAO nop.
 * State ALS:
 *   15us later advance to CMS if Read/Write command
 * State CMS:
 *   200us later advance to SCS, before first bit of sector.
 * State SCS:
 *   Transfer data.
 *   End of sector advance to SCE
 *   CONO +END or +CLR
 *     Read command, terminate read. Advance to SCE.
 *     Write/read cmp.
 * State SCE:
 *    Increase sector by 1.
 *    If END set CMD = NOP.
 *    2us, if CMD = NOP advance to IDS, else CMS.
 *         set SECT_END flag. (Possible IRQ).
 *
 * CONO +SCL clear error bits. ADE,CME,DCE,DRL,FER,PER,RCE,SECT_END,WLE.
 * CONO +CLR clears CMD=0.
 */

#define DSK_WDS        128
#define DSK_SECS       44
#define DSK_CYL        64 * 16
#define DSK_SIZE       (DSK_SECS * DSK_CYL * DSK_WDS)

uint64          dsk_buf[DSK_WDS];
uint8           dsk_octflp;
uint32          dsk_status;
uint32          dsk_cmd;
uint32          dsk_addr;
int             dsk_dct = 0;

t_stat          dsk_devio(uint32 dev, uint64 *data);
t_stat          dsk_svc(UNIT *);
t_stat          dsk_boot(int32, DEVICE *);
t_stat          dsk_set_dct (UNIT *, int32, CONST char *, void *);
t_stat          dsk_show_dct (FILE *, UNIT *, int32, CONST void *);
void            dsk_ini(UNIT *, t_bool);
t_stat          dsk_reset(DEVICE *);
t_stat          dsk_attach(UNIT *, CONST char *);
t_stat          dsk_detach(UNIT *);
t_stat          dsk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                     const char *cptr);
const char      *dsk_description (DEVICE *dptr);

#if !PDP6
#define D DEV_DIS
#else
#define D 0
#endif

UNIT                dsk_unit[] = {
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },

};

DIB dsk_dib[] = {
    {DSK_DEVNUM, 1, &dsk_devio, NULL},
    };

MTAB                dsk_mod[] = {
    {UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL},
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "DCT", "DCT",
       &dsk_set_dct, &dsk_show_dct, NULL},
    {0}
};

REG                 dsk_reg[] = {
    {BRDATA(BUFF, dsk_buf, 16, 64, DSK_WDS), REG_HRO},
    {0}
};

DEVICE              dsk_dev = {
    "DSK", dsk_unit, dsk_reg, dsk_mod,
    NUM_UNITS_DSK, 8, 18, 1, 8, 36,
    NULL, NULL, &dsk_reset, &dsk_boot, &dsk_attach, &dsk_detach,
    &dsk_dib[0], DEV_DISABLE | DEV_DEBUG | D, 0, dev_debug,
    NULL, NULL, &dsk_help, NULL, NULL, &dsk_description
};


t_stat
dsk_devio(uint32 dev, uint64 *data) {
     UNIT        *uptr = &dsk_unit[(dsk_addr >> 16) & 03];
     uint64       res;

     switch(dev & 3) {
     case CONI:
          res = ((uint64)(dsk_cmd) << 18);
          res |= ((uint64)(dsk_octflp)) << 10;
          res |= ((uint64)(dsk_status & RMASK));
          if ((uptr->flags & UNIT_ATT) == 0)
              res |= OPR;
          if (uptr->flags & UNIT_WLK)
              res |= WLE;
          *data = res;
          sim_debug(DEBUG_CONI, &dsk_dev, "DSK %03o CONI %012llo PC=%o\n", dev,
                            *data, PC);
          break;
     case CONO:
          clr_interrupt(dev);
          if (*data & SCL)
              dsk_status &= ADE|CME|DCE|DRL|FER|PER|RCE|SECT_END;
          /* If disk controller is busy */
          if (dsk_octflp & (ALS|CMS|SCS|SCE)) {
              /* Only update IRQ flags and stop flags */
              dsk_cmd &= END|CLR|CMD;
              dsk_cmd |= *data & ~(CMD|SCL);
          } else {
              dsk_cmd &= END|CLR;
              dsk_cmd |= *data & ~(SCL);
          }
          if ((dsk_cmd & EIS) != 0 && dsk_octflp == IDS)
              set_interrupt(dev, dsk_cmd);
          if ((dsk_cmd & EFE) != 0 && (dsk_status & (FER|PER|WLE|RCE|DRL)) != 0)
              set_interrupt(dev, dsk_cmd);
          if ((dsk_cmd & EFR) != 0 && dsk_octflp == DFR)
              set_interrupt(dev, dsk_cmd);
          if ((dsk_cmd & EFR) != 0 && dsk_status & SECT_END)
              set_interrupt(dev, dsk_cmd);
          sim_debug(DEBUG_CONO, &dsk_dev, "DSK %03o CONO %06o PC=%o %06o\n", dev,
                    (uint32)*data, PC, dsk_status);
          break;
     case DATAI:
          sim_debug(DEBUG_DATAIO, &dsk_dev, "DSK %03o DATI %012llo PC=%o\n",
                   dev, *data, PC);
          break;
     case DATAO:
          sim_debug(DEBUG_DATAIO, &dsk_dev, "DSK %03o DATO %012llo, PC=%o %03o\n",
                   dev, *data, PC, dsk_octflp);
          /* If not in right state we can't change it */
          if (dsk_octflp & (SCE|SCS|CMS|ALS))
              break;
          /* Zero lower 3 bits of sector if read next sector set */
          if (*data & 01000000LL)
              *data &= ~07LL;

          dsk_addr = (*data & RMASK);
          uptr = &dsk_unit[(dsk_addr >> 16) & 03];
          /* If we are idle, start controller */
          if (dsk_octflp == IDS) {
              sim_activate(uptr, 100);
              clr_interrupt(dev);
          }
          dsk_octflp = SNA;
          break;
    }
    return SCPE_OK;
}


t_stat
dsk_svc (UNIT *uptr)
{
   int           ctlr  = (dsk_addr >> 16) & 03;
   int           cyl;
   int           sec;
   int           wc;
   uint64        data;
   DEVICE       *dptr;
   t_stat        err;

   dptr = &dsk_dev;

   /* Check if we need to seek */
   if (dsk_octflp == SCE) {
       if ((dsk_cmd & CMD) == WR_CMD && (uptr->flags & UNIT_WLK) == 0) {
           /* Write the block */
           int da;
           for (; uptr->DATAPTR < DSK_WDS; uptr->DATAPTR++)
                dsk_buf[uptr->DATAPTR] = 0;
           cyl = (dsk_addr >> 6) & 01777;
           sec = dsk_addr & 077;
           if (sec > DSK_SECS)
              sec -= DSK_SECS;
           da = (sec + (cyl * DSK_SECS)) * DSK_WDS;
           err = sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
           (void)sim_fwrite (&dsk_buf[0], sizeof(uint64),
                        DSK_WDS, uptr->fileref);
           sim_debug(DEBUG_DETAIL, dptr, "DSK %d Write %d %d\n", ctlr, da, cyl);
       }
       uptr->DATAPTR = 0;
       sec = (dsk_addr + 1) & 077;
       if (sec >= DSK_SECS)
           sec = 0;
       dsk_addr = (dsk_addr & ~077) | sec;
       if (dsk_cmd & CLR)
          dsk_cmd &= ~(CMD|CLR);
       dsk_octflp = CMS;
       if (dsk_cmd & END || (dsk_cmd & CMD) == 0 || dct_is_connect(dsk_dct) == 0) {
          dsk_cmd &= ~(CMD|CLR|END);
          dsk_octflp = IDS;
       }
    } else

   /* Do transfer */
   if (dsk_octflp == SCS) {
       if (dsk_cmd & END) {
           dsk_octflp = SCE;
       } else if ((dsk_status & DRL) == 0) {
           if (dsk_cmd & WR_CMD) {
               if (dct_read(dsk_dct, &data, 2) == 0) {
                   dsk_status |= DRL;
               } else if (dsk_cmd & RD_CMD) {
                   if (dsk_buf[uptr->DATAPTR] != data)
                       dsk_status |= RCE;
               } else {
                   sim_debug(DEBUG_DETAIL, dptr, "DSK %d Write %012llo %d\n",
                               ctlr, data, uptr->DATAPTR);
                   if ((uptr->flags & UNIT_WLK) != 0)
                       dsk_status |= DCE|PER|FER;
                   dsk_buf[uptr->DATAPTR] = data;
               }
           } else if (dsk_cmd & RD_CMD) {
               data = dsk_buf[uptr->DATAPTR];
               if (dct_write(dsk_dct, &data, 2) == 0)
                   dsk_status |= DRL;
           }
      }
      uptr->DATAPTR++;
      if (uptr->DATAPTR == DSK_WDS)
          dsk_octflp = SCE;
   }

   if (dsk_octflp == CMS) {
       sim_debug(DEBUG_DETAIL, dptr, "DSK %d CMS\n", ctlr);
       if (dsk_cmd & RD_CMD) {
           /* Read the block */
           int da;
           cyl = (dsk_addr >> 6) & 01777;
           sec = dsk_addr & 077;
           if (sec > DSK_SECS)
              sec -= DSK_SECS;
           da = (sec + (cyl * DSK_SECS)) * DSK_WDS;
           err = sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
           wc = sim_fread (&dsk_buf[0], sizeof(uint64),
                        DSK_WDS, uptr->fileref);
           sim_debug(DEBUG_DETAIL, dptr, "DSK %d Read %d %d\n", ctlr, da, cyl);
           for (; wc < DSK_WDS; wc++)
                dsk_buf[wc] = 0;
       } else if (dsk_cmd & WR_CMD) {
           /* Check if we can write disk */
           if (uptr->flags & UNIT_WLK) {
              dsk_status |= CME|FER;
           }
       }
       uptr->DATAPTR = 0;
       dsk_octflp = SCS;
   }

   /* Ready for data transfer */
   if (dsk_octflp == DFR) {
       if (dsk_cmd & CMD) {
          dsk_octflp = ALS;
       } else {
          dsk_octflp = ADT;
       }
       sim_activate(uptr, 100);
       return SCPE_OK;
   }

   /* If at ADT then seek to correct cylinder */
   if (dsk_octflp == ADT) {
       if ((uptr->flags & UNIT_ATT) == 0) {
           dsk_status |= ADE|FER;
       } else {
           cyl = (dsk_addr >> 6) & 077;
           if (cyl != uptr->CUR_CYL) {
               cyl -= uptr->CUR_CYL;
               if (cyl < 0)
                   cyl = -cyl;
               uptr->CUR_CYL = (dsk_addr >> 6) & 077;
               sim_activate(uptr, 10000 * cyl);
               return SCPE_OK;
           }
       }
       dsk_octflp = DFR;
   }

   /* Address is correct and we have a command */
   if (dsk_octflp == ALS) {
       sim_debug(DEBUG_DETAIL, dptr, "DSK %d Alarm\n", ctlr);
       dsk_octflp = CMS;
   }

   /* If at SNA the switch to ADT */
   if (dsk_octflp == SNA) {
       sim_debug(DEBUG_DETAIL, dptr, "DSK %d Sna\n", ctlr);
       dsk_octflp = ADT;
       if (uptr->flags & UNIT_WLK)
           dsk_status |= WLE|FER;
   }

   /* If we are in idle state, just return */
   if (dsk_octflp == IDS) {
       sim_debug(DEBUG_DETAIL, dptr, "DSK %d Idle\n", ctlr);
       if ((dsk_cmd & EIS) != 0)
          set_interrupt(DSK_DEVNUM, dsk_cmd);
       return SCPE_OK;
   }

    sim_activate(uptr, 100);
    if ((dsk_cmd & EIS) != 0 && dsk_octflp == IDS)
        set_interrupt(DSK_DEVNUM, dsk_cmd);
    if ((dsk_cmd & EFE) != 0 && (dsk_status & (FER|PER|WLE|RCE|DRL)) != 0)
        set_interrupt(DSK_DEVNUM, dsk_cmd);
    if ((dsk_cmd & EFR) != 0 && dsk_octflp == DFR)
        set_interrupt(DSK_DEVNUM, dsk_cmd);
    if ((dsk_cmd & EFR) != 0 && dsk_status & SECT_END)
        set_interrupt(DSK_DEVNUM, dsk_cmd);
    return SCPE_OK;
}

/* set DCT channel and unit. */
t_stat
dsk_set_dct (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 dct;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    dct = (int32) get_uint (cptr, 8, 20, &r);
    if (r != SCPE_OK)
        return r;
    dsk_dct = dct;
    return SCPE_OK;
}

t_stat
dsk_show_dct (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   if (uptr == NULL)
      return SCPE_IERR;

   fprintf (st, "DCT=%02o", dsk_dct);
   return SCPE_OK;
}



t_stat
dsk_reset(DEVICE * dptr)
{
    int unit;
    UNIT *uptr = dptr->units;
    for(unit = 0; unit < NUM_UNITS_DSK; unit++) {
         uptr->UFLAGS  = 0;
         uptr->CUR_CYL = 0;
         uptr++;
    }
    dsk_octflp = IDS;
    return SCPE_OK;
}

/* Boot from given device */
t_stat
dsk_boot(int32 unit_num, DEVICE * dptr)
{
    return SCPE_OK;
}

/* Device attach */
t_stat
dsk_attach (UNIT *uptr, CONST char *cptr)
{
     t_stat r;

     r = attach_unit (uptr, cptr);
     if (r != SCPE_OK)
         return r;
     uptr->CUR_CYL = 0;
     uptr->UFLAGS = 0;
     return SCPE_OK;
}

/* Device detach */
t_stat
dsk_detach (UNIT *uptr)
{
    if (!(uptr->flags & UNIT_ATT))                          /* attached? */
        return SCPE_OK;
    if (sim_is_active (uptr))                              /* unit active? */
        sim_cancel (uptr);                                  /* cancel operation */
    return detach_unit (uptr);
}

t_stat
dsk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "The DSK controller implements the 270 disk controller for the PDP6\n");
fprintf (st, "Options include the ability to set units write enabled or write locked\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "The DSK device supports the BOOT command.\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *dsk_description (DEVICE *dptr)
{
return "270 disk controller";
}

#endif
