/* b5500_dk.c: Burrioughs 5500 Disk controller

   Copyright (c) 2016, Richard Cornwell

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

#if (NUM_DEVS_DSK > 0) 


/* in u3 is device address */
/* in u4 is current buffer position */
/* in u5       Bits 30-16 of W */
#define URCSTA_SKIP     000017  /* Skip mask */
#define URCSTA_SINGLE   000020  /* Single space skip. */
#define URCSTA_DOUBLE   000040  /* Double space skip */
#define URCSTA_READ     000400  /* Read flag */
#define URCSTA_WC       001000  /* Use word count */
#define URCSTA_DIRECT   002000  /* Direction, Long line */
#define URCSTA_BINARY   004000  /* Binary transfer */
#define URCSTA_INHIBIT  040000  /* Inhibit transfer to memory */

#define DK_CHAN         0000003 /* Channel number */
#define DK_CTRL         0000004 /* Disk controller unit attached too */
#define DK_WC           0000010 /* Use word count */
#define DK_BSY          0000020 /* Drive is busy. */
#define DK_RD           0000040 /* Executing a read command */
#define DK_WR           0000100 /* Executing a write command */
#define DK_RDCK         0000200 /* Executing a read check command */
#define DK_ADDR         0000400 /* Drive has an address. */
#define DK_BIN          0001000 /* Binary mode */
#define DK_WCZERO       0002000 /* Word count Zero */
#define DK_SECMASK      0770000 /* Number of segments to transfer */
#define DK_SECT         0010000 /* One segment */
#define DK_SEC_SIZE     240     /* Sector size  */
#define DK_MAXSEGS      200000  /* Max segments for MOD I  ESU */
#define DK_MAXSEGS2     400000  /* Max segments for MOD IB ESU */
#define DFX_V           (UNIT_V_UF + 1)
#define MODIB_V         (UNIT_V_UF + 2)
#define DFX             (1 << DFX_V)
#define MODIB           (1 << MODIB_V)

t_stat              dsk_cmd(uint16, uint16, uint8, uint16 *);
t_stat              dsk_srv(UNIT *);
t_stat              dsk_boot(int32, DEVICE *);
t_stat              dsk_help (FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *dsk_description (DEVICE *);
t_stat              esu_srv(UNIT *);
t_stat              esu_attach(UNIT *, CONST char *);
t_stat              esu_detach(UNIT *);
t_stat              esu_help (FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *esu_description (DEVICE *);

uint8               dsk_buffer[NUM_DEVS_DSK][DK_SEC_SIZE];
t_stat              set_mod(UNIT *uptr, int32 val, CONST char *cptr, 
                        void *desc);

#define ESU_TYPE        UDATA(&esu_srv, UNIT_ATTABLE+UNIT_DISABLE+ \
                                        UNIT_FIX, DK_SEC_SIZE * DK_MAXSEGS)

UNIT                esu_unit[] = {
    {ESU_TYPE, DK_MAXSEGS},     /* 0 */
    {ESU_TYPE, DK_MAXSEGS},     /* 1 */
    {ESU_TYPE, DK_MAXSEGS},     /* 2 */
    {ESU_TYPE, DK_MAXSEGS},     /* 3 */
    {ESU_TYPE, DK_MAXSEGS},     /* 4 */
    {ESU_TYPE, DK_MAXSEGS},     /* 5 */
    {ESU_TYPE, DK_MAXSEGS},     /* 6 */
    {ESU_TYPE, DK_MAXSEGS},     /* 7 */
    {ESU_TYPE, DK_MAXSEGS},     /* 8 */
    {ESU_TYPE, DK_MAXSEGS},     /* 9 */
    {ESU_TYPE, DK_MAXSEGS},     /* 10 */
    {ESU_TYPE, DK_MAXSEGS},     /* 11 */
    {ESU_TYPE, DK_MAXSEGS},     /* 12 */
    {ESU_TYPE, DK_MAXSEGS},     /* 13 */
    {ESU_TYPE, DK_MAXSEGS},     /* 14 */
    {ESU_TYPE, DK_MAXSEGS},     /* 15 */
    {ESU_TYPE, DK_MAXSEGS},     /* 16 */
    {ESU_TYPE, DK_MAXSEGS},     /* 17 */
    {ESU_TYPE, DK_MAXSEGS},     /* 18 */
    {ESU_TYPE, DK_MAXSEGS},     /* 19 */
};

MTAB                esu_mod[] = {
    {MODIB, 0, "MODI", "MODI", &set_mod, NULL, NULL,
           "Sets ESU to Fast Mod I drive"},
    {MODIB, MODIB, "MODIB", "MODIB", &set_mod, NULL, NULL,
           "Sets ESU to Slow Mod IB drive"},
    {0}
};


DEVICE              esu_dev = {
    "ESU", esu_unit, NULL, esu_mod,
    20, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &esu_attach, &esu_detach,
    NULL, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &esu_help, NULL, NULL, &esu_description
};

MTAB                dsk_mod[] = {
    {DFX, 0, NULL, "NODFX", NULL, NULL, NULL,
        "Disables drive sharing, use only on DK1"},
    {DFX, DFX, "DFX", "DFX", NULL, NULL, NULL,
        "Enables drive sharing, use only on DK1"},
    {0}
};


UNIT                dsk_unit[] = {
    {UDATA(&dsk_srv, UNIT_DISABLE, 0)}, /* DKA */
    {UDATA(&dsk_srv, UNIT_DIS | UNIT_DISABLE, 0)}, /* DKB */
};

DEVICE              dsk_dev = {
    "DK", dsk_unit, NULL, dsk_mod,
    NUM_DEVS_DSK, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, &dsk_boot, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dsk_help, NULL, NULL, &dsk_description
};



/* Start off a disk command */
t_stat dsk_cmd(uint16 cmd, uint16 dev, uint8 chan, uint16 *wc)
{
    UNIT        *uptr;
    int         u = (dev==DSK1_DEV)? 0: 1;

    uptr = &dsk_unit[u];

    /* If unit disabled return error */
    if (uptr->flags & UNIT_DIS) 
        return SCPE_NODEV;

    /* Check if drive is ready to recieve a command */
    if ((uptr->u5 & DK_BSY)) 
        return SCPE_BUSY;

    uptr->u5 = chan|DK_BSY;
    if (dev == DSK2_DEV)
        uptr->u5 |= DK_CTRL;
    uptr->u5 |= (cmd & 077) << 12;
    if (cmd & URCSTA_INHIBIT) 
        uptr->u5 |= DK_RDCK;
    else if (cmd & URCSTA_READ) 
        uptr->u5 |= DK_RD;
    else
        uptr->u5 |= DK_WR;
    if (cmd & URCSTA_WC) {
        uptr->u5 |= DK_WC;
        if (*wc == 0)
           uptr->u5 |= DK_WCZERO;
    }
    if (cmd & URCSTA_BINARY)
        uptr->u5 |= DK_BIN;
    if (loading) {
        uptr->u4 = 1;
        uptr->u3 = 0;
    } else {
        uptr->u5 |= DK_ADDR;
    }
    sim_activate(uptr, 100);
    return SCPE_OK;
}
        

/* Handle processing disk controller commands */
t_stat dsk_srv(UNIT * uptr)
{
    int                 chan = uptr->u5 & DK_CHAN;
    DEVICE              *dptr = find_dev_from_unit(uptr);
    int                 i;
    int                 addr;
    uint8               abuf[8];        /* x-esu-disk-track-segment */
    int                 u = uptr - dsk_unit;
    int                 esu;
    UNIT                *eptr;

    if ((uptr->u5 & DK_BSY) == 0)
        return SCPE_OK;

    /* Read in first word, which a address. */
    /* Note special read routine since address is not included
        in the word count */
    if (uptr->u5 & DK_ADDR) {
        /* Read in 8 characters which are the address */
        for (i = 0; i < 8; i++) {
            if (chan_read_disk(chan, &abuf[i], 0)) 
                break;
            abuf[i] &= 017;     /* Mask zone out */
            if (abuf[i] == 012) /* Zero to zero */
                abuf[i] = 0;
        }

        /* extract ESU and Address */
        esu = abuf[1];
        addr = 0;
        for (i = 2; i < 8; i++) {
           addr = (addr * 10) + abuf[i];
        }
        uptr->u5 &= ~DK_ADDR;
        uptr->u4 = addr;

        /* Map to ESU */
        if (u && (dsk_unit[u].flags & DFX) == 0) 
           esu += 10;
        sim_debug(DEBUG_DETAIL, dptr, "Disk access %d %s %02o %d,%d\n\r", u,
                (uptr->u5 & DK_RDCK) ? "rcheck" : 
                (uptr->u5 & DK_RD) ? "read" :
                (uptr->u5 & DK_WR)? "write" : "nop", (uptr->u5 >> 9) & 077,
                esu, addr);
        
        uptr->u3 = esu;
        eptr = &esu_unit[uptr->u3];
        /* Check if valid */
        if ((eptr->flags & UNIT_DIS) || (eptr->flags & UNIT_ATT) == 0) {
           /* Set not ready and end channel */
           chan_set_notrdy(chan);
           uptr->u5 = 0;
           return SCPE_OK;
        }

        /* Check if Read Check or Write Check */
        if ((uptr->u5 & (DK_WCZERO|DK_WC|DK_SECMASK)) == (DK_WCZERO|DK_WC)) {
            if (uptr->u4 >= eptr->wait) 
                chan_set_eof(chan);
            
            if (uptr->u5 & DK_WR) {
                sim_debug(DEBUG_DETAIL, dptr, "Disk write int %d %d %o\n\r", 
                      uptr->u3, uptr->u4, uptr->u5);
            }
            if (uptr->u5 & DK_RD) {
                sim_debug(DEBUG_DETAIL, dptr, "Disk read int %d %d %o\n\r", 
                      uptr->u3, uptr->u4, uptr->u5);
                if (eptr->flags & MODIB) 
                   chan_set_error(chan);
            }
            chan_set_end(chan);
            uptr->u5 = 0;
            return SCPE_OK;
        }

        sim_activate(uptr, 5000);
        return SCPE_OK;
    } 

    /* Kick off actual transfer to ESU */
    if (((uptr->u5 & DK_ADDR) == 0) && 
                ((uptr->u5 & (DK_RDCK|DK_RD|DK_WR)) != 0)) {
        eptr = &esu_unit[uptr->u3];

        /* Wait until unit is ready for new access */
        if ((eptr->u5 & DK_BSY) == 0) {

            eptr->u3 = (uptr->u5 & DK_WR) ? 0 : DK_SEC_SIZE;
            eptr->u4 = uptr->u4;        /* Disk address */
            eptr->u5 = uptr->u5;        /* Command */
            if (uptr->u5 & DK_RDCK) {
                uptr->u5 = 0;
                chan_set_end(chan);
            } else 
                uptr->u5 &= ~(DK_RDCK|DK_RD|DK_WR);
            sim_activate(eptr, 500);
            return SCPE_OK;
        }
        sim_activate(uptr, 100);
    }
    return SCPE_OK;
}

void esu_set_end(UNIT *uptr, int err) {
        int             chan = uptr->u5 & DK_CHAN;
        int             dsk = ((uptr->u5 & DK_CTRL) != 0);
        DEVICE          *dptr = find_dev_from_unit(uptr);

        sim_debug(DEBUG_DETAIL, dptr, "Disk done %d %d %o\n\r", uptr->u3,
                 uptr->u4, uptr->u5);
        if (err)
            chan_set_error(chan);
        uptr->u5 = 0;
        dsk_unit[dsk].u5 = 0;
        chan_set_end(chan);
}
        
/* Handle processing esu controller commands */
t_stat esu_srv(UNIT * uptr)
{
    int                 chan = uptr->u5 & DK_CHAN;
    DEVICE              *dptr = find_dev_from_unit(uptr);
    int                 u = uptr - esu_unit;
    int                 dsk = ((uptr->u5 & DK_CTRL) != 0);
    int                 wc;
    
 
    /* Process for each unit */
    if (uptr->u5 & DK_RD) {
        /* Check if at start of segment */
        if (uptr->u3 >= DK_SEC_SIZE) {
            int         da = (uptr->u4 * DK_SEC_SIZE);

            /* Check if end of operation */
            if ((uptr->u5 & (DK_SECMASK)) == 0) {
                esu_set_end(uptr, 0);
                return SCPE_OK;
            }

            /* Check if over end of disk */
            if (uptr->u4 >= uptr->wait) {
                sim_debug(DEBUG_DETAIL, dptr, "Disk read over %d %d %o\n\r",
                                 uptr->u3, uptr->u4, uptr->u5);
                chan_set_eof(chan);
                esu_set_end(uptr, 0);
                return SCPE_OK;
            }
            sim_debug(DEBUG_DETAIL, dptr, "Disk read %d %d %d %o %d\n\r",
                                 u,uptr->u3, uptr->u4, uptr->u5, da);
        
            if (sim_fseek(uptr->fileref, da, SEEK_SET) < 0) {
                esu_set_end(uptr, 1);
                return SCPE_OK;
            }
            wc = sim_fread(&dsk_buffer[dsk][0], 1, DK_SEC_SIZE,
                                 uptr->fileref);
            for (; wc < DK_SEC_SIZE; wc++) 
                dsk_buffer[dsk][wc] = (uptr->u5 & DK_BIN) ? 0 :020;
            uptr->u3 = 0;
            uptr->u4++; /* Advance disk address */
            uptr->u5 -= DK_SECT;
        }
        /* Transfer one Character */
        if (chan_write_char(chan, &dsk_buffer[dsk][uptr->u3], 0)) {
                esu_set_end(uptr, 0);
                return SCPE_OK;
        }
        uptr->u3++;
    }

    if (uptr->u5 & DK_RDCK) {
        if (uptr->u3 >= DK_SEC_SIZE) {

            /* Check if over end of disk */
            if (uptr->u4 >= uptr->wait) {
                sim_debug(DEBUG_DETAIL, dptr, "Disk rdchk over %d %d %o\n\r",
                         uptr->u3, uptr->u4, uptr->u5);
                uptr->u5 = 0;
                IAR |= IRQ_14 << dsk;
                return SCPE_OK;
            }
            sim_debug(DEBUG_DETAIL, dptr, "Disk rdchk %d %d %d %o\n\r", u,
                         uptr->u3, uptr->u4, uptr->u5);
        
            uptr->u4++; /* Advance disk address */
            uptr->u5 -= DK_SECT;
            uptr->u3 = 0;

            /* Check if end of operation */
            if ((uptr->u5 & (DK_SECMASK)) == 0) {
                uptr->u5 = 0;
                IAR |= IRQ_14 << dsk;
                return SCPE_OK;
            }
        }
        /* Check if at end of segment */
        uptr->u3++;
    }

    /* Process for each unit */
    if (uptr->u5 & DK_WR) {
        /* Check if end of operation */
        if ((uptr->u5 & (DK_SECMASK)) == 0) {
            esu_set_end(uptr, 0);
            return SCPE_OK;
        }

        /* Transfer one Character */
        if (chan_read_char(chan, &dsk_buffer[dsk][uptr->u3], 0)) {
            if (uptr->u3 != 0) {
                while (uptr->u3 < DK_SEC_SIZE) 
                    dsk_buffer[dsk][uptr->u3++] = (uptr->u5 & DK_BIN) ? 0 :020;
            }
        } 
        uptr->u3++;

        /* Check if at end of segment */
        if (uptr->u3 >= DK_SEC_SIZE) {
           int  da = (uptr->u4 * DK_SEC_SIZE);

           /* Check if over end of disk */
           if (uptr->u4 >= uptr->wait) {
               sim_debug(DEBUG_DETAIL, dptr, "Disk write over %d %d %o\n\r", 
                           uptr->u3, uptr->u4, uptr->u5);
               chan_set_eof(chan);
               esu_set_end(uptr, 0);
               return SCPE_OK;
           }
        
           sim_debug(DEBUG_DETAIL, dptr, "Disk write %d %d %d %o %d\n\r",
                            u, uptr->u3, uptr->u4, uptr->u5, da);
           if (sim_fseek(uptr->fileref, da, SEEK_SET) < 0) {
               esu_set_end(uptr, 1);
               return SCPE_OK;
           }
        
           wc = sim_fwrite(&dsk_buffer[dsk][0], 1, DK_SEC_SIZE, 
                                   uptr->fileref);
           if (wc != DK_SEC_SIZE) {
               esu_set_end(uptr, 1);
               return SCPE_OK;
           }
           uptr->u3 = 0;
           uptr->u4++;  /* Advance disk address */
           uptr->u5 -= DK_SECT;
        } 
    }
    sim_activate(uptr, (uptr->flags & MODIB) ? 200 :100);
    return SCPE_OK;
}
                
t_stat
set_mod(UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
    if (uptr == NULL)   return SCPE_IERR;
    if (val == MODIB) 
        uptr->wait = DK_MAXSEGS2;
    else 
        uptr->wait = DK_MAXSEGS;
    uptr->capac = DK_SEC_SIZE * uptr->wait;
    return SCPE_OK;
}


/* Boot from given device */
t_stat
dsk_boot(int32 unit_num, DEVICE * dptr)
{
    int         dev = (unit_num)? DSK2_DEV:DSK1_DEV;
    t_uint64    desc;
    int         i;

    for(i = 0; i < 20; i++) 
        esu_unit[i].u5 = 0;

    desc = (((t_uint64)dev)<<DEV_V)|DEV_IORD|DEV_OPT|020LL;
    return chan_boot(desc);
}


t_stat
esu_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;
    int                 u = uptr-esu_unit;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;
    if (u < 10) {
        iostatus |= DSK1_FLAG;
    }
    if (u >= 10 || (dsk_unit[1].flags & DFX) != 0) {
        iostatus |= DSK2_FLAG;
    }
    return SCPE_OK;
}

t_stat
esu_detach(UNIT * uptr)
{
    t_stat              r;
    int                 u = uptr-esu_unit;
    int                 i, mask, lim;

    if ((r = detach_unit(uptr)) != SCPE_OK)
        return r;
    /* Determine which controller */
    if (u < 10) {
        mask = DSK1_FLAG;
        /* If DFX, then both controllers */
        if ((dsk_unit[1].flags & DFX) != 0) 
           mask |= DSK2_FLAG;
        lim = 10;
        i = 0;
    } else {
        /* If DFX, then drive not attached */
        if ((dsk_unit[1].flags & DFX) != 0) 
           return r;
        mask = DSK2_FLAG;
        lim = 20;
        i = 10;
    }
    /* Scan to see if any disks still attached */
    while (i < lim) {
        if (esu_unit[i].flags & UNIT_ATT) 
           return r;    /* Something still there */
        i++;
    }
    /* There are no longer any drives attached to 
        this controller */
    iostatus &= ~mask;
    return r;
}

t_stat
dsk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  fprintf (st, "B5470 Disk Controller\n\n");
  fprintf (st, "By default the second disk controller is not enabled.\n\n");
  fprintf (st, "     sim> SET DK1 ENABLE     to enable second disk controller for use\n");
  fprintf (st, "The B5500 could have up to two disk controllers that could talk\n");
  fprintf (st, "to up to 10 ESU. Each ESU held up to 5 storage units. By uses of\n");
  fprintf (st, "a exchange unit (DFX), the second controller could talk to the\n");
  fprintf (st, "same drives as the first controller. To use the second disk controller\n");
  fprintf (st, "to share the same drives as the first (after enabling DK1):\n\n");
  fprintf (st, "    sim> SET DK1 DFX       enable disk exchange\n\n");
  fprintf (st, "If you want to support more then 10 ESU units you will first\n");
  fprintf (st, "need to generate a new version of MCP without the DFX option\n");
  fprintf (st, "for MCP XV you also need to SET DKBNODFX TRUE when building the\n");
  fprintf (st, "system file.\n");
  fprintf (st, "ESU units 0-9 attach to DK0, or DK1 if DFX\n");
  fprintf (st, "ESU units 10-19 attach to DK1 only\n\n");
  fprintf (st, "The DK unit supports the BOOT command.\n\n");
  fprint_set_help (st, dptr) ;
  fprint_show_help (st, dptr) ;
  return SCPE_OK;
}

const char *
dsk_description (DEVICE *dptr) 
{
   return "B5470 disk controller module";
}

t_stat
esu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  fprintf (st, "B471 ESU with 5 B457 storage units\n\n");
  fprintf (st, "Each ESU unit represents the electronics unit and 5 storage units\n");
  fprintf (st, "MOD I units could handle about 48 million characters.\n");
  fprintf (st, "MOD IB units could handle about 96 million characters.\n");
  fprintf (st, "MOD IB units operated at half the speed of MOD I units.\n");
  fprintf (st, "ESU units can be added to a system after it has been booted,\n");
  fprintf (st, "however they can't be removed. The configuration of disks must\n");
  fprintf (st, "be the same each time the same system is booted.\n");
  fprintf (st, "To use larger slower drives do:\n");
  fprintf (st, "     sim> SET ESUn MODIB       before the unit is attached\n");
  fprintf (st, "To use smaller faster drives do (default):\n");
  fprintf (st, "     sim> SET ESUn MODI        before the unit is attached\n\n");
  fprint_set_help (st, dptr) ;
  fprint_show_help (st, dptr) ;
  return SCPE_OK;
}

const char *
esu_description (DEVICE *dptr) 
{
    return  "B471 electrontics unit and 5 B457 storage units.";
}
#endif


