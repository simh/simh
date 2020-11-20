/* kx10_rh.c: RH10/RH20 interace routines.

   Copyright (c) 2019-2020, Richard Cornwell

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



/* CONI Flags */
#define IADR_ATTN       0000000000040LL   /* Interrupt on attention */
#define IARD_RAE        0000000000100LL   /* Interrupt on register access error */
#define DIB_CBOV        0000000000200LL   /* Control bus overrun */
#define CXR_PS_FAIL     0000000002000LL   /* Power supply fail (not implemented) */
#define CXR_ILC         0000000004000LL   /* Illegal function code */
#define CR_DRE          0000000010000LL   /* Or Data and Control Timeout */
#define DTC_OVER        0000000020000LL   /* DF10 did not supply word on time (not implemented) */
#define CCW_COMP_1      0000000040000LL   /* Control word written. */
#define CXR_CHAN_ER     0000000100000LL   /* Channel Error */
#define CXR_EXC         0000000200000LL   /* Error in drive transfer */
#define CXR_DBPE        0000000400000LL   /* Device Parity error (not implemented) */
#define CXR_NXM         0000001000000LL   /* Channel non-existent memory (not implemented) */
#define CXR_CWPE        0000002000000LL   /* Channel Control word parity error (not implemented) */
#define CXR_CDPE        0000004000000LL   /* Channel Data Parity Error (not implemented) */
#define CXR_SD_RAE      0000200000000LL   /* Register access error */
#define CXR_ILFC        0000400000000LL   /* Illegal CXR function code */
#define B22_FLAG        0004000000000LL   /* 22 bit channel */
#define CC_CHAN_PLS     0010000000000LL   /* Channel transfer pulse (not implemented) */
#define CC_CHAN_ACT     0020000000000LL   /* Channel in use */
#define CC_INH          0040000000000LL   /* Disconnect channel */
#define CB_FULL         0200000000000LL   /* Set when channel buffer is full (not implemented) */
#define AR_FULL         0400000000000LL   /* Set when AR is full (not implemented) */

/* RH20 CONI Flags */
#define RH20_PCR_FULL   0000000000020LL   /* Primary command file full */
#define RH20_ATTN_ENA   0000000000040LL   /* Attention enable */
#define RH20_SCR_FULL   0000000000100LL   /* Secondary command full */
#define RH20_ATTN       0000000000200LL   /* Attention */
#define RH20_MASS_ENA   0000000000400LL   /* Mass bus enable */
#define RH20_DATA_OVR   0000000001000LL   /* Data overrun */
#define RH20_CHAN_RDY   0000000002000LL   /* Channel ready to start */
#define RH20_RAE        0000000004000LL   /* Register access error */
#define RH20_DR_RESP    0000000010000LL   /* Drive no response */
#define RH20_CHAN_ERR   0000000020000LL   /* Channel error */
#define RH20_SHRT_WC    0000000040000LL   /* Short word count */
#define RH20_LONG_WC    0000000100000LL   /* Long word count */
#define RH20_DR_EXC     0000000200000LL   /* Exception */
#define RH20_DATA_PRI   0000000400000LL   /* Data parity error */
#define RH20_SBAR       0000001000000LL   /* SBAR set */
#define RH20_XEND       0000002000000LL   /* Transfer ended */

/* CONO Flags */
#define ATTN_EN         0000000000040LL   /* enable attention interrupt. */
#define REA_EN          0000000000100LL   /* enable register error interrupt */
#define CBOV_CLR        0000000000200LL   /* Clear CBOV */
#define CONT_RESET      0000000002000LL   /* Clear All error bits */
#define ILC_CLR         0000000004000LL   /* Clear ILC and SD RAE */
#define DRE_CLR         0000000010000LL   /* Clear CR_CBTO and CR_DBTO */
#define OVER_CLR        0000000020000LL   /* Clear DTC overrun */
#define WRT_CW          0000000040000LL   /* Write control word */
#define CHN_CLR         0000000100000LL   /* Clear Channel Error */
#define DR_EXC_CLR      0000000200000LL   /* Clear DR_EXC */
#define DBPE_CLR        0000000400000LL   /* Clear CXR_DBPE */

/* RH20 CONO Flags */
#define RH20_DELETE_SCR 0000000000100LL   /* Clear SCR */
#define RH20_RCLP       0000000000200LL   /* Reset command list */
#define RH20_MASS_EN    0000000000400LL   /* Mass bus enable */
#define RH20_XFER_CLR   0000000001000LL   /* Clear XFER error */
#define RH20_CLR_MBC    0000000002000LL   /* Clear MBC */
#define RH20_CLR_RAE    0000000004000LL   /* Clear RAE error */

/* DATAO/DATAI */
#define CR_REG          0770000000000LL   /* Register number */
#define LOAD_REG        0004000000000LL   /* Load register */
#define CR_MAINT_MODE   0000100000000LL   /* Maint mode... not implemented */
#define CR_DRIVE        0000007000000LL
#define CR_GEN_EVD      0000000400000LL   /* Enable Parity */
#define CR_DXES         0000000200000LL   /* Disable DXES errors  */
#define CR_INAD         0000000077600LL
#define CR_WTEVM        0000000000100LL   /* Verify Parity */
#define CR_FUNC         0000000000076LL
#define CR_GO           0000000000001LL

#define IRQ_VECT        0000000000777LL   /* Interupt vector */
#define IRQ_KI10        0000002000000LL
#define IRQ_KA10        0000001000000LL
#define FNC_XFER        024             /* >=? data xfr */

/* Status register settings */
#define DS_OFF          0000001         /* offset mode */
#define DS_VV           0000100         /* volume valid */
#define DS_DRY          0000200         /* drive ready */
#define DS_DPR          0000400         /* drive present */
#define DS_PGM          0001000         /* programable NI */
#define DS_LST          0002000         /* last sector */
#define DS_WRL          0004000         /* write locked */
#define DS_MOL          0010000         /* medium online */
#define DS_PIP          0020000         /* pos in progress */
#define DS_ERR          0040000         /* error */
#define DS_ATA          0100000         /* attention active */

/* RH20 channel status flags */
#define RH20_MEM_PAR    00200000000000LL  /* Memory parity error */
#define RH20_NADR_PAR   00100000000000LL  /* Address parity error */
#define RH20_NOT_WC0    00040000000000LL  /* Word count not zero */
#define RH20_NXM_ERR    00020000000000LL  /* Non existent memory */
#define RH20_LAST_ERR   00000400000000LL  /* Last transfer error */
#define RH20_ERROR      00000200000000LL  /* RH20 error */
#define RH20_LONG_STS   00000100000000LL  /* Did not reach wc */
#define RH20_SHRT_STS   00000040000000LL  /* WC reached zero */
#define RH20_OVER       00000020000000LL  /* Overrun error */

/* 0-37 mass bus register.
   70   SBAR, block address.
   71   STCR, neg block count, func
   72   PBAR
   73   PTCR
   74   IVIR Interrupt vector address.
   75   Diags read.
   76   Diags write.
   77   Status (tra,cb test, bar test, ev par, r/w, exc,ebl, 0, attn, sclk
*/

/*
 * CCW 000..... New channel comand list pointer  HALT.
       010..... Next CCW Address JUMP
       1xycount-address.  x=halt last xfer, y=reverse
*/

extern uint32  eb_ptr;

t_stat
rh_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr;
    DIB    *dibp;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
       return SCPE_IERR;
    dibp = (DIB *) dptr->ctxt;
    dptr->flags &= ~DEV_M_RH;
    dptr->flags |= val;
    dibp->dev_num &= ~(RH10_DEV|RH20_DEV);
    dibp->dev_num |= (val) ? RH20_DEV: RH10_DEV;
    return SCPE_OK;
}

t_stat rh_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   DEVICE *dptr;

   if (uptr == NULL)
      return SCPE_IERR;

   dptr = find_dev_from_unit(uptr);
   if (dptr == NULL)
      return SCPE_IERR;
   fprintf (st, "%s", (dptr->flags & TYPE_RH20) ? "RH20" : "RH10");
   return SCPE_OK;
}


t_stat rh_devio(uint32 dev, uint64 *data) {
     DEVICE        *dptr = NULL;
     struct rh_if  *rhc = NULL;
     int            drive;
     uint32         drdat = 0;

     for (drive = 0; rh[drive].dev_num != 0; drive++) {
        if (rh[drive].dev_num == (dev & 0774)) {
            rhc = rh[drive].rh;
            dptr = rh[drive].dev;
            break;
        }
     }
     if (rhc == NULL)
        return SCPE_OK;
#if KL
     if (dptr->flags & TYPE_RH20) {
         switch(dev & 3) {
         case CONI:
              *data = rhc->status & RMASK;
              if (rhc->attn != 0)
                 *data |= RH20_ATTN;
              if (rhc->rae != 0)
                 *data |= RH20_RAE;
              sim_debug(DEBUG_CONI, dptr, "%s %03o CONI %06o PC=%o %o\n",
                     dptr->name, dev, (uint32)*data, PC, rhc->attn);
              return SCPE_OK;

         case CONO:
              clr_interrupt(dev);
              /* Clear flags */
              if (*data & RH20_CLR_MBC) {
                 if (rhc->dev_reset != NULL)
                     rhc->dev_reset(dptr);
                 rhc->attn = 0;
                 rhc->imode = 2;
              }
              rhc->status &= ~(07LL|IADR_ATTN|RH20_MASS_EN);
              rhc->status |= *data & (07LL|IADR_ATTN|RH20_MASS_EN);
              if (*data & RH20_DELETE_SCR)
                 rhc->status &= ~(RH20_SBAR|RH20_SCR_FULL);
              if (*data & (RH20_RCLP|RH20_CLR_MBC))
                 rhc->cia = eb_ptr | (rhc->devnum - 0540);
              if (*data & (RH20_CLR_RAE|RH20_CLR_MBC)) {
                 rhc->rae = 0;
              }
              rhc->status &= ~RH20_DR_RESP;
              if (*data & PI_ENABLE)
                 rhc->status &= ~PI_ENABLE;
              if (((rhc->status & IADR_ATTN) != 0 && rhc->attn != 0)
                    || (rhc->status & PI_ENABLE))
                 set_interrupt(rhc->devnum, rhc->status);
              sim_debug(DEBUG_CONO, dptr, "%s %03o CONO %06o PC=%06o %06o\n",
                    dptr->name, dev, (uint32)*data, PC, rhc->status);
              return SCPE_OK;

         case DATAI:
              *data = 0;
              if (rhc->status & BUSY && rhc->reg != 04) {
                  rhc->status |= CC_CHAN_ACT;
                  return SCPE_OK;
              }
              if (rhc->reg < 040) {
                  int parity;
                  if (rhc->dev_read(dptr, rhc, rhc->reg, &drdat))
                      rhc->status |= RH20_DR_RESP;
                  *data = (uint64)(drdat & 0177777);
                  parity = (int)((*data >> 8) ^ *data);
                  parity = (parity >> 4) ^ parity;
                  parity = (parity >> 2) ^ parity;
                  parity = ((parity >> 1) ^ parity) & 1;
                  *data |= ((uint64)(!parity)) << 16;
                  *data |= ((uint64)(rhc->drive)) << 18;
                  *data |= BIT10;
              } else if ((rhc->reg & 070) != 070) {
                  rhc->rae = 1;
                  break;
              } else {
                  switch(rhc->reg & 07) {
                  case 0:   *data = rhc->sbar; break;
                  case 1:   *data = rhc->stcr; break;
                  case 2:   *data = rhc->pbar; break;
                  case 3:   *data = rhc->ptcr; break;
                  case 4:   *data = rhc->ivect; break;
                  case 5:
                  case 6:   break;
                  case 7:   *data = 0;  break;
                  }
              }
              *data |= ((uint64)(rhc->reg)) << 30;
              sim_debug(DEBUG_DATAIO, dptr, "%s %03o DATI %012llo %d PC=%06o\n",
                          dptr->name, dev, *data, rhc->drive, PC);
              return SCPE_OK;

         case DATAO:
              sim_debug(DEBUG_DATAIO, dptr, "%s %03o DATO %012llo  PC=%06o %06o\n",
                         dptr->name, dev, *data, PC, rhc->status);
              rhc->reg = ((int)(*data >> 30)) & 077;
              rhc->imode |= 2;
              if (rhc->reg < 040)
                  rhc->drive = (int)(*data >> 18) & 07;
              if (*data & LOAD_REG) {
                  if (rhc->reg < 040) {
                     clr_interrupt(dev);
                     /* Check if access error */
                     if (rhc->rae & (1 << rhc->drive) && (*data & BIT9) == 0) {
                         set_interrupt(rhc->devnum, rhc->status);
                         return SCPE_OK;
                     }
                     if (rhc->dev_write(dptr, rhc, rhc->reg & 037, (int)(*data & 0777777)))
                          rhc->status |= RH20_DR_RESP;
                     if (((rhc->status & IADR_ATTN) != 0 && rhc->attn != 0)
                             || (rhc->status & PI_ENABLE))
                         set_interrupt(rhc->devnum, rhc->status);
                     /* Check if access error */
                     if (rhc->rae & (1 << rhc->drive) && (*data & BIT9) == 0)
                         set_interrupt(rhc->devnum, rhc->status);
                     else
                         rhc->rae &= ~(1 << rhc->drive);
                  } else if ((rhc->reg & 070) != 070) {
                      if ((*data & BIT9) == 0) {
                          rhc->rae |= 1 << rhc->drive;
                          set_interrupt(rhc->devnum, rhc->status);
                      }
                  } else {
                      switch(rhc->reg & 07) {
                      case 0:
                               rhc->sbar = (*data) & (CR_DRIVE|RMASK);
                               rhc->status |= RH20_SBAR;
                               break;
                      case 1:
                               rhc->stcr = (*data) & (BIT10|BIT7|CR_DRIVE|RMASK);
                               rhc->status |= RH20_SCR_FULL;
                               break;
                      case 4:
                               rhc->ivect = (*data & IRQ_VECT);
                               break;
                      case 2:
                      case 3:
                      case 5:
                      case 6:
                      case 7:
                               break;
                      }
                  }
              }
         }
         if ((rhc->status & (RH20_SCR_FULL|RH20_PCR_FULL)) == (RH20_SCR_FULL))
             rh20_setup(rhc);
         return SCPE_OK;
     }
#endif
     switch(dev & 3) {
     case CONI:
        *data = rhc->status & ~(IADR_ATTN|IARD_RAE);
        if (rhc->attn != 0 && (rhc->status & IADR_ATTN))
           *data |= IADR_ATTN;
        if (rhc->rae != 0 && (rhc->status & IARD_RAE)) {
           *data |= IARD_RAE;
            if (rhc->rae & (1 << rhc->drive))
                *data |= CXR_SD_RAE;
        }
#if KI_22BIT
        *data |= B22_FLAG;
#endif
        sim_debug(DEBUG_CONI, dptr, "%s %03o CONI %06o PC=%o %o\n",
               dptr->name, dev, (uint32)*data, PC, rhc->attn);
        return SCPE_OK;

     case CONO:
         clr_interrupt(dev);
         rhc->status &= ~(07LL|IADR_ATTN|IARD_RAE);
         rhc->status |= *data & (07LL|IADR_ATTN|IARD_RAE);
         /* Clear flags */
         if (*data & CONT_RESET && rhc->dev_reset != NULL) {
            rhc->dev_reset(dptr);
            rhc->status &= (07LL|IADR_ATTN|IARD_RAE);
         }
         if (*data & (DBPE_CLR|DR_EXC_CLR|CHN_CLR))
            rhc->status &= ~(*data & (DBPE_CLR|DR_EXC_CLR|CHN_CLR));
         if (*data & OVER_CLR)
            rhc->status &= ~(DTC_OVER);
         if (*data & CBOV_CLR)
            rhc->status &= ~(DIB_CBOV);
         if (*data & CXR_ILC)
            rhc->status &= ~(CXR_ILFC|CXR_SD_RAE);
         if (*data & DRE_CLR)
            rhc->status &= ~(CR_DRE);
         if (*data & WRT_CW)
            rh_writecw(rhc, 0);
         if (*data & PI_ENABLE)
            rhc->status &= ~PI_ENABLE;
         if (rhc->status & PI_ENABLE)
            set_interrupt(dev, rhc->status);
         if ((rhc->status & IADR_ATTN) != 0 && rhc->attn != 0)
            set_interrupt(dev, rhc->status);
         sim_debug(DEBUG_CONO, dptr, "%s %03o CONO %06o PC=%06o %06o\n",
               dptr->name, dev, (uint32)*data, PC, rhc->status);
         return SCPE_OK;

     case DATAI:
        *data = 0;
        if (rhc->status & BUSY && rhc->reg != 04) {
            rhc->status |= CC_CHAN_ACT;
            return SCPE_OK;
        }
        if (rhc->reg == 040) {
              if (rhc->dev_read(dptr, rhc, 0, &drdat))
                  rhc->status |= CR_DRE;
              *data = (uint64)(drdat & 077);
              *data |= ((uint64)(rhc->cia)) << 6;
              *data |= ((uint64)(rhc->xfer_drive)) << 18;
        } else if (rhc->reg == 044) {
              *data = (uint64)rhc->ivect;
              if (rhc->imode)
                *data |= IRQ_KI10;
              else
                *data |= IRQ_KA10;
        } else if (rhc->reg == 054) {
                *data = (uint64)(rhc->rae);
        } else if ((rhc->reg & 040) == 0) {
              int parity;
              if (rhc->dev_read(dptr, rhc, rhc->reg, &drdat)) {
                  rhc->rae |= 1 << rhc->drive;
                  rhc->status |= CR_DRE;
              }
              *data = (uint64)(drdat & 0177777);
              parity = (int)((*data >> 8) ^ *data);
              parity = (parity >> 4) ^ parity;
              parity = (parity >> 2) ^ parity;
              parity = ((parity >> 1) ^ parity) & 1;
              *data |= ((uint64)(parity ^ 1)) << 17;
              *data |= ((uint64)(rhc->drive)) << 18;
        }
        *data |= ((uint64)(rhc->reg)) << 30;
        sim_debug(DEBUG_DATAIO, dptr, "%s %03o DATI %012llo %d PC=%06o\n",
                    dptr->name, dev, *data, rhc->drive, PC);
        return SCPE_OK;

     case DATAO:
         sim_debug(DEBUG_DATAIO, dptr, "%s %03o DATO %012llo  PC=%06o %06o\n",
                    dptr->name, dev, *data, PC, rhc->status);
         rhc->reg = ((int)(*data >> 30)) & 077;
         rhc->imode &= ~2;
         if (rhc->reg < 040 && rhc->reg != 04) {
            rhc->drive = (int)(*data >> 18) & 07;
         }
         if (*data & LOAD_REG) {
             if (rhc->reg == 040) {
                if ((*data & 1) == 0) {
                   return SCPE_OK;
                }

                if (rhc->status & BUSY) {
                    rhc->status |= CC_CHAN_ACT;
                    return SCPE_OK;
                }

                rhc->status &= ~(CCW_COMP_1|PI_ENABLE);
                if (((*data >> 1) & 037) < FNC_XFER) {
                   rhc->status |= CXR_ILC;
                   rh_setirq(rhc);
                   sim_debug(DEBUG_DATAIO, dptr,
                       "%s %03o command abort %012llo, %d PC=%06o %06o\n",
                       dptr->name, dev, *data, rhc->drive, PC, rhc->status);
                   return SCPE_OK;
                }
                /* Check if access error */
                if (rhc->rae & (1 << rhc->drive))
                    return SCPE_OK;
                /* Start command */
                rh_setup(rhc, (uint32)(*data >> 6));
                rhc->xfer_drive = (int)(*data >> 18) & 07;
                if (rhc->dev_write(dptr, rhc, 0, (uint32)(*data & 077))) {
                    rhc->status |= CR_DRE;
                }
                sim_debug(DEBUG_DATAIO, dptr,
                    "%s %03o command %012llo, %d PC=%06o %06o\n",
                    dptr->name, dev, *data, rhc->drive, PC, rhc->status);
             } else if (rhc->reg == 044) {
                /* Set KI10 Irq vector */
                rhc->ivect = (int)(*data & IRQ_VECT);
                rhc->imode = (*data & IRQ_KI10) != 0;
             } else if (rhc->reg == 050) {
                ;    /* Diagnostic access to mass bus. */
             } else if (rhc->reg == 054) {
                /* clear flags */
                rhc->rae &= ~(*data & 0377);
                if (rhc->rae == 0)
                    clr_interrupt(dev);
             } else if ((rhc->reg & 040) == 0) {
                rhc->drive = (int)(*data >> 18) & 07;
                /* Check if access error */
                if (rhc->rae & (1 << rhc->drive)) {
                    return SCPE_OK;
                }
                if (rhc->dev_write(dptr, rhc, rhc->reg & 037, (uint32)(*data & 0777777)))
                    rhc->status |= CR_DRE;
             }
         }
         clr_interrupt(dev);
         if (((rhc->status & (IADR_ATTN|BUSY)) == IADR_ATTN && rhc->attn != 0)
               || (rhc->status & PI_ENABLE))
             set_interrupt(rhc->devnum, rhc->status);
         return SCPE_OK;
    }
    return SCPE_OK; /* Unreached */
}

/* Handle KI and KL style interrupt vectors */
t_addr
rh_devirq(uint32 dev, t_addr addr) {
    struct rh_if  *rhc = NULL;
    int            drive;

    for (drive = 0; rh[drive].dev_num != 0; drive++) {
       if (rh[drive].dev_num == (dev & 0774)) {
           rhc = rh[drive].rh;
           break;
       }
    }
    if (rhc != NULL) {
        if (rhc->imode == 1) /* KI10 Style */
           addr = RSIGN | rhc->ivect;
        else if (rhc->imode == 2) /* RH20 style */
           addr = rhc->ivect;
    } else {
       sim_printf("Unable to find device %03o\n\r", dev);
    }
    return  addr;
}

/* Set the attention flag for a unit */
void rh_setattn(struct rh_if *rhc, int unit)
{
    rhc->attn |= 1<<unit;
    if ((rhc->status & BUSY) == 0 && (rhc->status & IADR_ATTN) != 0) 
        set_interrupt(rhc->devnum, rhc->status);
}

void rh_error(struct rh_if *rhc)
{
    if (rhc->imode == 2)
       rhc->status |= RH20_DR_EXC;
}

/* Decrement block count for RH20, nop for RH10 */
int rh_blkend(struct rh_if *rhc)
{
#if KL
     if (rhc->imode == 2) {
         rhc->cia = (rhc->cia + 1) & 01777;
         if (rhc->cia == 0) {
            rhc->status |= RH20_XEND;
            return 1;
         }
     }
#endif
     return 0;
}

/* Set an IRQ for a DF10 device */
void rh_setirq(struct rh_if *rhc) {
      rhc->status |= PI_ENABLE;
      set_interrupt(rhc->devnum, rhc->status);
}

/* Generate the DF10 complete word */
void rh_writecw(struct rh_if *rhc, int nxm) {
     uint64       wrd1;
#if KL
     if (rhc->imode == 2) {
         uint32   chan = (rhc->devnum - 0540);
         int      wc = ((rhc->wcr ^ RH20_WMASK) + 1) & RH20_WMASK;
         rhc->status |= RH20_CHAN_RDY;
         rhc->status &= ~(RH20_PCR_FULL);
         if (wc != 0 || (rhc->status & RH20_XEND) == 0 ||
             (rhc->ptcr & BIT10) != 0 || nxm) {
             uint64 wrd2;
             wrd1 = SMASK|(uint64)(rhc->ccw);
             if ((rhc->ptcr & BIT10) == 0 || (rhc->status & RH20_DR_EXC) != 0)
                  return;
             if (nxm) {
                 wrd1 |= RH20_NXM_ERR;
                 rhc->status |= RH20_CHAN_ERR;
             }  
             if (wc != 0) {
                 wrd1 |= RH20_NOT_WC0;
                 if (rhc->status & RH20_XEND) {
                     wrd1 |= RH20_LONG_STS;
                     if ((rhc->ptcr & 070) == 060) { /* Write command */
                         rhc->status |= RH20_LONG_WC|RH20_CHAN_ERR;
                     }
                 }
             } else if ((rhc->status & RH20_XEND) == 0) {
                 wrd1 |= RH20_SHRT_STS;
                 if ((rhc->ptcr & 070) == 060) { /* Write command */
                     rhc->status |= RH20_SHRT_WC|RH20_CHAN_ERR;
                 }
             }
             /* No error and not storing */
             if ((rhc->status & RH20_CHAN_ERR) == 0 && (rhc->ptcr & BIT10) == 0)
                 return;
             wrd1 |= RH20_NADR_PAR;
             wrd2 = ((uint64)rhc->cop << 33) | (((uint64)wc) << CSHIFT) |
                                ((uint64)(rhc->cda) & AMASK);
             (void)Mem_write_word(chan+1, &wrd1, 1);
             (void)Mem_write_word(chan+2, &wrd2, 1);
         }
         return;
     }
#endif
     if (nxm)
        rhc->status |= CXR_NXM;
     rhc->status |= CCW_COMP_1;
     if (rhc->wcr != 0)
         rhc->cda++;
     wrd1 = ((uint64)(rhc->ccw & WMASK) << CSHIFT) | ((uint64)(rhc->cda) & AMASK);
     (void)Mem_write_word(rhc->cia|1, &wrd1, 0);
}

/* Finish off a DF10 transfer */
void rh_finish_op(struct rh_if *rhc, int nxm) {
#if KL
     if (rhc->imode != 2)
#endif
     rhc->status &= ~BUSY;
     rh_writecw(rhc, nxm);
     rh_setirq(rhc);
#if KL
     if (rhc->imode == 2 &&
          (rhc->status & (RH20_SCR_FULL|RH20_PCR_FULL)) == (RH20_SCR_FULL) &&
          (rhc->status & (RH20_DR_EXC|RH20_CHAN_ERR)) == 0)
        rh20_setup(rhc);
#endif
}

#if KL
/* Set up for a RH20 transfer */
void rh20_setup(struct rh_if *rhc)
{
     DEVICE        *dptr = NULL;
     int           reg;

     for (reg = 0; rh[reg].dev_num != 0; reg++) {
        if (rh[reg].rh == rhc) {
            dptr = rh[reg].dev;
            break;
        }
     }
     if (dptr == 0)
         return;
     rhc->pbar = rhc->sbar;
     rhc->ptcr = rhc->stcr;
     /* Read drive status */
     rhc->drive = (rhc->ptcr >> 18) & 07;
     rhc->status &= ~(RH20_DATA_OVR|RH20_CHAN_RDY|RH20_DR_RESP|RH20_CHAN_ERR|RH20_SHRT_WC|\
                      RH20_LONG_WC|RH20_DR_EXC|RH20_SCR_FULL|PI_ENABLE|RH20_XEND);
     rhc->status |= RH20_PCR_FULL;
     if (rhc->status & RH20_SBAR) {
         rhc->drive = (rhc->pbar >> 18) & 07;
         if (rhc->dev_write != NULL)
             (void)rhc->dev_write(dptr, rhc, 5, (rhc->pbar & 0177777));
         rhc->status &= ~RH20_SBAR;
     }
     if (rhc->ptcr & BIT7) {  /* If RCPL reset I/O pointers */
         rhc->ccw = eb_ptr + (rhc->devnum - 0540);
         rhc->wcr = 0;
     }
     /* Hold block count in cia */
     rhc->drive = (rhc->ptcr >> 18) & 07;
     rhc->cia = (rhc->ptcr >> 6) & 01777;
     if (rhc->dev_write != NULL)
         (void)rhc->dev_write(dptr, rhc, 0, (rhc->ptcr & 077));
     rhc->cop = 0;
     rhc->wcr = 0;
     rhc->status &= ~RH20_CHAN_RDY;
}
#endif

/* Setup for a DF10 transfer */
void rh_setup(struct rh_if *rhc, uint32 addr)
{
     rhc->cia = addr & ICWA;
     rhc->ccw = rhc->cia;
     rhc->wcr = 0;
     rhc->status |= BUSY;
}


/* Fetch the next IO control word */
int rh_fetch(struct rh_if *rhc) {
     uint64      data;
     int         reg;
     DEVICE      *dptr = NULL;

     for (reg = 0; rh[reg].dev_num != 0; reg++) {
        if (rh[reg].rh == rhc) {
            dptr = rh[reg].dev;
            break;
        }
     }
#if KL
     if (rhc->imode == 2 && (rhc->cop & 2) != 0) {
         return 0;
     }
#endif
     if (Mem_read_word(rhc->ccw, &data, 0)) {
         rh_finish_op(rhc, 1);
         return 0;
     }
     sim_debug(DEBUG_EXP, dptr, "%s fetch %06o %012llo\n\r", dptr->name, rhc->ccw, data);
#if KL
     if (rhc->imode == 2) {
         while((data & RH20_XFER) == 0) {
             rhc->ccw = (uint32)(data & AMASK);
             if ((data & (BIT1|BIT2)) == 0) {
                 return 0;
             }
             if (Mem_read_word(rhc->ccw, &data, 0)) {
                 rh_finish_op(rhc, 1);
                 return 0;
             }
             sim_debug(DEBUG_EXP, dptr, "%s fetch2 %06o %012llo\n\r", dptr->name, rhc->ccw, data);
//fprintf(stderr, "RH20 fetch2 %06o %012llo\n\r", rhc->ccw, data);
         }
         rhc->wcr = (((data >> CSHIFT) & RH20_WMASK) ^ WMASK) + 1;
         rhc->cda = (data & AMASK);
         rhc->cop = (data >> 33) & 07;
         rhc->ccw = (uint32)((rhc->ccw + 1) & AMASK);
         return 1;
     }
#endif
     while((data & (WMASK << CSHIFT)) == 0) {
         if ((data & AMASK) == 0 || (uint32)(data & AMASK) == rhc->ccw) {
             rh_finish_op(rhc, 0);
             return 0;
         }
         rhc->ccw = (uint32)(data & AMASK);
         if (Mem_read_word(rhc->ccw, &data, 0)) {
             rh_finish_op(rhc, 1);
             return 0;
         }
         sim_debug(DEBUG_EXP, dptr, "%s fetch2 %06o %012llo\n\r", dptr->name, rhc->ccw, data);
     }
     rhc->wcr = (uint32)((data >> CSHIFT) & WMASK);
     rhc->cda = (uint32)(data & AMASK);
     rhc->ccw = (uint32)((rhc->ccw + 1) & AMASK);
     return 1;
}

/* Read next word */
int rh_read(struct rh_if *rhc) {
     uint64 data;
     if (rhc->wcr == 0) {
         if (!rh_fetch(rhc))
             return 0;
     }
     rhc->wcr = (uint32)((rhc->wcr + 1) & WMASK);
     if (rhc->cda != 0) {
        if (rhc->cda > MEMSIZE) {
            rh_finish_op(rhc, 1);
            return 0;
        }
#if KL
        if (rhc->imode == 2) {
            if (Mem_read_word(rhc->cda, &data, 0)) {
                rh_finish_op(rhc, 1);
                return 0;
            }
            if (rhc->cop & 01)
                rhc->cda = (uint32)((rhc->cda - 1) & AMASK);
            else
                rhc->cda = (uint32)((rhc->cda + 1) & AMASK);
        } else {
            rhc->cda = (uint32)((rhc->cda + 1) & AMASK);
            if (Mem_read_word(rhc->cda, &data, 0)) {
                rh_finish_op(rhc, 1);
                return 0;
            }
        }
#else
        rhc->cda = (uint32)((rhc->cda + 1) & AMASK);
        if (Mem_read_word(rhc->cda, &data, 0)) {
            rh_finish_op(rhc, 1);
            return 0;
        }
#endif
     } else {
        data = 0;
     }
     rhc->buf = data;
     if (rhc->wcr == 0) {
        return rh_fetch(rhc);
     }
     return 1;
}

/* Write next word */
int rh_write(struct rh_if *rhc) {
     if (rhc->wcr == 0) {
         if (!rh_fetch(rhc))
             return 0;
     }
     rhc->wcr = (uint32)((rhc->wcr + 1) & WMASK);
     if (rhc->cda != 0) {
        if (rhc->cda > MEMSIZE) {
           rh_finish_op(rhc, 1);
           return 0;
        }
#if KL
        if (rhc->imode == 2) {
            if (Mem_write_word(rhc->cda, &rhc->buf, 0)) {
                rh_finish_op(rhc, 1);
                return 0;
            }
            if (rhc->cop & 01)
                rhc->cda = (uint32)((rhc->cda - 1) & AMASK);
            else
                rhc->cda = (uint32)((rhc->cda + 1) & AMASK);
        } else {
            rhc->cda = (uint32)((rhc->cda + 1) & AMASK);
            if (Mem_write_word(rhc->cda, &rhc->buf, 0)) {
                rh_finish_op(rhc, 1);
                return 0;
            }
        }
#else
        rhc->cda = (uint32)((rhc->cda + 1) & AMASK);
        if (Mem_write_word(rhc->cda, &rhc->buf, 0)) {
            rh_finish_op(rhc, 1);
            return 0;
        }
#endif
     }
     if (rhc->wcr == 0) {
        return rh_fetch(rhc);
     }
     return 1;
}

