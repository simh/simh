/* i7000_chan.c: IBM 7000 Channel simulator

   Copyright (c) 2005-2016, Richard Cornwell

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

   channel

   Common routines for handling channel functions.

*/

#include "i7000_defs.h"

extern DEVICE      *sim_devices[];



int     num_devs[NUM_CHAN];


t_stat
chan_set_devs(DEVICE * dptr)
{
    int                 i;

    for(i = 0; i < NUM_CHAN; i++) {
        num_devs[i] = 0;
    }
    /* Build channel array */
    for (i = 0; sim_devices[i] != NULL; i++) {
        UNIT               *uptr = sim_devices[i]->units;
        DIB                *dibp = (DIB *) sim_devices[i]->ctxt;
        int                 ctype;
        int                 num;

        /* If no DIB, not channel device */
        if (dibp == NULL)
            continue;
        /* Skip channel devices */
        if (sim_devices[i] == &chan_dev)
            continue;
        /* Skip disabled devices */
        if (sim_devices[i]->flags & DEV_DIS)
            continue;
        ctype = dibp->ctype;
        if (dibp->upc > 1) {
            int            chan = UNIT_G_CHAN(uptr->flags);
            int            type = CHAN_G_TYPE(chan_unit[chan].flags);
            if (((1 << type) & ctype) == 0) {
                if ((chan_unit[chan].flags & CHAN_SET) ||
                    ((chan_unit[chan].flags & CHAN_AUTO)
                         && num_devs[chan] != 0)) {
                    for (num = sim_devices[i]->numunits; num > 0; num--)
                        (uptr++)->flags |= UNIT_DIS;
                    goto nextdev;
                }
            }
            /* Set channel to highest type */
            if ((chan_unit[chan].flags & CHAN_SET) == 0) {
                /* Set type to highest found */
                for(type = 7; type >=0; type--)
                   if (ctype & (1 << type))
                     break;
                chan_unit[chan].flags &= ~(CHAN_MODEL);
                chan_unit[chan].flags |= CHAN_S_TYPE(type)|CHAN_SET;
            }
            num_devs[chan] += sim_devices[i]->numunits;
            if (dibp->ini != NULL) {
                for (num = sim_devices[i]->numunits; num > 0; num--) {
                    uptr->flags &= ~UNIT_CHAN;
                    uptr->flags |= UNIT_S_CHAN(chan);
                    dibp->ini(uptr++, 1);
                }
            }
            goto nextdev;
        }

        for (num = sim_devices[i]->numunits; num > 0; num--) {
            int                 chan = UNIT_G_CHAN(uptr->flags);
            int                 type = CHAN_G_TYPE(chan_unit[chan].flags);

            if ((uptr->flags & UNIT_DIS) == 0) {
                if (((1 << type) & ctype) == 0) {
                    if ((chan_unit[chan].flags & CHAN_SET) ||
                        ((chan_unit[chan].flags & CHAN_AUTO)
                         && num_devs[chan] != 0)) {
                        uptr->flags |= UNIT_DIS;
                        goto next;
                    }
                }
                /* Set channel to highest type */
                if ((chan_unit[chan].flags & CHAN_SET) == 0) {
                     /* Set type to highest found */
                     for(type = 7; type >=0; type--)
                        if (ctype & (1 << type))
                          break;
                     chan_unit[chan].flags &= ~(CHAN_MODEL);
                     chan_unit[chan].flags |= CHAN_S_TYPE(type)|CHAN_SET;
                }
                num_devs[chan]++;
                if (dibp->ini != NULL)
                    dibp->ini(uptr, 1);
            }
          next:
            uptr++;
        }
    nextdev:
        ;
    }
    return SCPE_OK;
}

/* Print help for "SET dev CHAN" based on allowed types */
void help_set_chan_type(FILE *st, DEVICE *dptr, const char *name)
{
#if NUM_CHAN > 1
   DIB        *dibp = (DIB *) dptr->ctxt;
   int        ctype = dibp->ctype;
   int        i;
   int        m;

   fprintf (st, "Devices can be moved to any channel via the command\n\n");
   fprintf (st, "   sim> SET %s CHAN=x     where x is", dptr->name);
   if (ctype & 3) {
       if (ctype == 1 || ctype == 2)
          fprintf(st, " only");
       fprintf (st, " %s", chname[0]);
       if ((ctype & ~3) != 0)
          fprintf(st, " or");
   }
   if ((ctype & ~3) != 0)
      fprintf(st, " %s to %s", chname[1], chname[NUM_CHAN-1]);
   fprintf (st, "\n\n%s can be attached to ", name);
   m = 1;
   for(i = 0; ctype != 0; i++) {
      if (ctype & m)  {
         fprintf(st, "%s", chan_type_name[i]);
         ctype &= ~m;
         if (ctype != 0)
            fprintf(st, ", or ");
      }
      m <<= 1;
   }
   fprintf(st, " channel\n");
#endif
}


/* Sets the device onto a given channel */
t_stat
set_chan(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE             *dptr;
    DIB                *dibp;
    int                 newch;
    int                 chan;
    int                 num;
    int                 type;
    int                 ctype;
    int                 compat;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;

    chan = UNIT_G_CHAN(uptr->flags);
    if (chan >= NUM_CHAN)
       chan = 0;
    dibp = (DIB *) dptr->ctxt;

    if (dibp == NULL)
        return SCPE_IERR;
    for(newch = 0; newch < NUM_CHAN; newch++)
        if (strcmp(cptr, chname[newch]) == 0)
            break;
    if (newch == NUM_CHAN)
        return SCPE_ARG;
    if (newch == chan)
        return SCPE_OK;

    ctype = dibp->ctype;
    compat = ctype;

    /* Update the number of devices on this channel */
    num_devs[newch] = 0;
    for (num = 0; sim_devices[num] != NULL; num++) {
        UNIT               *u = sim_devices[num]->units;
        DIB                *dibp = (DIB *) sim_devices[num]->ctxt;
        int                 units = sim_devices[num]->numunits;

        /* If no DIB, not channel device */
        if (dibp == NULL)
            continue;
        /* Skip channel devices */
        if (sim_devices[num] == &chan_dev)
            continue;
        /* Skip disabled devices */
        if (sim_devices[num]->flags & DEV_DIS)
            continue;
        if (dibp->upc > 1) {
            if ((u->flags & UNIT_DIS) == 0 &&
                 UNIT_G_CHAN(u->flags) == chan) {
                num_devs[newch] += units;
                compat &= dibp->ctype;
            }
        } else {
             int i;
             for (i = 0; i < units; i++) {
                  if ((u->flags & UNIT_DIS) == 0 &&
                        UNIT_G_CHAN(u->flags) == chan) {
                      num_devs[newch]++;
                      compat &= dibp->ctype;
                  }
                  u++;
            }
        }
    }

    /* If nothing left on channel, drop set bit */
    if (num_devs[newch] == 0 && chan_unit[newch].flags & CHAN_AUTO) {
        chan_unit[newch].flags &= ~CHAN_SET;
        compat = ctype;
    }

    /* Check if same type or everyone can handle new type */
    type = CHAN_G_TYPE(chan_unit[newch].flags);
    if (((1 << type) & ctype) == 0) {
        /* If set or no common types */
        if (chan_unit[newch].flags & CHAN_SET && compat == 0)
            return SCPE_IERR;
        if ((chan_unit[newch].flags & CHAN_AUTO) &&
            (compat == 0 && num_devs[newch] != 0))
            return SCPE_IERR;
        else {
            /* Set type to highest compatable type */
            for(type = 7; type >=0; type--)
                if (compat >> type)
                   break;
            chan_unit[newch].flags &= ~(CHAN_MODEL);
            chan_unit[newch].flags |= CHAN_S_TYPE(type)|CHAN_SET;
        }
    }

    /* Set channel to highest type */
    if ((chan_unit[chan].flags & CHAN_SET) == 0) {
        /* Set type to highest found */
        for(type = 7; type >=0; type--)
            if (ctype >> type)
                break;
        chan_unit[chan].flags &= ~(CHAN_MODEL);
        chan_unit[chan].flags |= CHAN_S_TYPE(type)|CHAN_SET;
    }

   /* Detach unit from orignal channel */
    if (dibp->upc > 1)
        num_devs[chan] -= dptr->numunits;
    else
        num_devs[chan]--;
    if (num_devs[chan] == 0 && (chan_unit[chan].flags & CHAN_AUTO))
        chan_unit[chan].flags &= ~CHAN_SET;

   /* Hook up to new channel */
    if (dibp->upc > 1) {
        uint32  unit;
        for (unit = 0; unit < dptr->numunits; unit++) {
            /* Set the new channel */
            dptr->units[unit].flags &= ~UNIT_CHAN;
            dptr->units[unit].flags |= UNIT_S_CHAN(newch);
        }
        num_devs[newch] += dptr->numunits;
    } else {
        /* Set the new channel */
        uptr->flags &= ~UNIT_CHAN;
        uptr->flags |= UNIT_S_CHAN(newch);
        num_devs[newch]++;
    }
    return SCPE_OK;
}

/* Print devices on channel */
t_stat
print_chan(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    int                 chan = uptr - chan_unit;
    int                 i;

    /* Check all devices */
    fprintf(st, "units=");
    for (i = 0; sim_devices[i] != NULL; i++) {
        UNIT               *u = sim_devices[i]->units;
        DIB                *dibp = (DIB *) sim_devices[i]->ctxt;
        uint32              num;

        /* If no DIB, not channel device */
        if (dibp == NULL)
            continue;
        /* Skip channel devices */
        if (sim_devices[i] == &chan_dev)
            continue;
        /* Skip disabled devices */
        if (sim_devices[i]->flags & DEV_DIS)
            continue;
        if (dibp->upc > 1) {
            if ((u->flags & UNIT_DIS) == 0 &&
                 UNIT_G_CHAN(u->flags) == chan)
                 fprintf(st, "%s, ", sim_devices[i]->name);
        } else {
             for (num = 0; num < sim_devices[i]->numunits; num++) {
                  if ((u->flags & UNIT_DIS) == 0 &&
                        UNIT_G_CHAN(u->flags) == chan)
                      fprintf(st, "%s%d, ", sim_devices[i]->name, num);
                  u++;
            }
        }
    }
    return SCPE_OK;
}

t_stat
get_chan(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    DEVICE             *dptr;
    DIB                *dibp;
    int                 chan;

    if (uptr == NULL)
        return SCPE_IERR;
    chan = UNIT_G_CHAN(uptr->flags);
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;
    fprintf(st, "Chan=%s", chname[chan]);
    return SCPE_OK;
}

t_stat
chan9_set_select(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int                 newsel;
    DEVICE             *dptr;
    DIB                *dibp;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    if (*cptr == '\0' || cptr[1] != '\0')
        return SCPE_ARG;
    if (*cptr == '0')
        newsel = 0;
    else if (*cptr == '1')
        newsel = 1;
    else
        return SCPE_ARG;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;

    dibp = (DIB *) dptr->ctxt;

    if (dibp == NULL)
        return SCPE_IERR;

   /* Change to new selection. */
    if (dibp->upc > 1) {
        uint32  unit;
        for (unit = 0; unit < dptr->numunits; unit++) {
            if (newsel)
                dptr->units[unit].flags |= UNIT_SELECT;
            else
                dptr->units[unit].flags &= ~UNIT_SELECT;
        }
    } else {
        if (newsel)
            uptr->flags |= UNIT_SELECT;
        else
            uptr->flags &= ~UNIT_SELECT;
    }
    return SCPE_OK;
}

t_stat
chan9_get_select(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    if (uptr->flags & UNIT_SELECT)
        fputs("Select=1", st);
    else
        fputs("Select=0", st);
    return SCPE_OK;
}

/* Check channel for error */
int chan_error(int chan)
{
    return chan_flags[chan] & CHS_ATTN;
}

/* Check channel for flag, clear it if it was set */
int chan_stat(int chan, uint32 flag)
{
    if (chan_flags[chan] & flag) {
        chan_flags[chan] &= ~flag;
        return 1;
    }
    return 0;
}

/* Check channel for flag */
int chan_test(int chan, uint32 flag)
{
    if (chan_flags[chan] & flag)
        return 1;
    return 0;
}

/* Check channel is selected */
int chan_select(int chan)
{
    return chan_flags[chan] & DEV_SEL;
}

/* Check channel is active */
int chan_active(int chan)
{
    return (chan_flags[chan] &
                (DEV_DISCO |DEV_SEL | STA_ACTIVE | STA_WAIT | STA_TWAIT)) != 0;
}

void
chan_set_attn(int chan)
{
    chan_flags[chan] |= CHS_ATTN;
}

void
chan_set_eof(int chan)
{
    chan_flags[chan] |= CHS_EOF;
}

void
chan_set_error(int chan)
{
    chan_flags[chan] |= CHS_ERR;
}

void
chan_set_sel(int chan, int need)
{
    chan_flags[chan] &=
        ~(DEV_WEOR | DEV_REOR | DEV_FULL | DEV_WRITE | DEV_DISCO);
    chan_flags[chan] |= DEV_SEL;
    if (need)
        chan_flags[chan] |= DEV_WRITE;
}

void
chan_clear_status(int chan)
{
    chan_flags[chan] &=
        ~(CHS_ATTN | CHS_EOT | CHS_BOT | DEV_REOR | DEV_WEOR);
}

void
chan_set(int chan, uint32 flag)
{
    chan_flags[chan] |= flag;
}

void
chan_clear(int chan, uint32 flag)
{
    chan_flags[chan] &= ~flag;
}

void
chan9_clear_error(int chan, int sel) {
    chan_flags[chan] &= ~(SNS_UEND | (SNS_ATTN1 >> sel));
}

void
chan9_set_attn(int chan, int sel)
{
    uint16              mask = SNS_ATTN1 >> sel;

    chan9_set_error(chan, mask);
}

