/* s3_disk.c: IBM 5444 Disk Drives

   Copyright (c) 2001-2005, Charles E. Owen

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.

   r1   Removeable disk 1
   f1   Fixed disk 1
   r2   Removeable disk 2
   f2   Fixed disk 2

   25-Apr-03    RMS     Revised for extended file support
   08-Oct-02    RMS     Added impossible function catcher
*/

#include "s3_defs.h"
#include <ctype.h>

extern uint8 M[];
extern int32 IAR[], level;
extern FILE *trace;
extern int32 debug_reg;
char dbuf[DSK_SECTSIZE];                                /* Disk buffer */
int32 dsk (int32 disk, int32 op, int32 m, int32 n, int32 data);
int32 read_sector(UNIT *uptr, char *dbuf, int32 sect);
int32 write_sector(UNIT *uptr, char *dbuf, int32 sect);
t_stat r1_svc (UNIT *uptr);
t_stat r1_boot (int32 unitno, DEVICE *dptr);
t_stat r1_attach (UNIT *uptr, CONST char *cptr);
t_stat r1_reset (DEVICE *dptr);
t_stat f1_svc (UNIT *uptr);
t_stat f1_boot (int32 unitno, DEVICE *dptr);
t_stat f1_attach (UNIT *uptr, CONST char *cptr);
t_stat f1_reset (DEVICE *dptr);
t_stat r2_svc (UNIT *uptr);
t_stat r2_boot (int32 unitno, DEVICE *dptr);
t_stat r2_attach (UNIT *uptr, CONST char *cptr);
t_stat r2_reset (DEVICE *dptr);
t_stat f2_svc (UNIT *uptr);
t_stat f2_boot (int32 unitno, DEVICE *dptr);
t_stat f2_attach (UNIT *uptr, CONST char *cptr);
t_stat f2_reset (DEVICE *dptr);
extern int32 GetMem(int32 addr);
extern int32 PutMem(int32 addr, int32 data);

char opstr[5][5] = { "SIO", "LIO", "TIO", "SNS", "APL" };

int32 DDAR[2];                                          /* Data address register */
int32 DCAR[2];                                          /* Disk Control Address Register */
int32 diskerr[2] = { 0, 0 };                            /* Error status */
int32 notrdy[2] = { 0, 0 };                             /* Not ready error */
int32 seekbusy[2] = { 0, 0 };                           /* Drive busy flags */
int32 seekhead[2] = { 0, 0 };                           /* Disk head 0,1 */
int32 found[2] = { 0, 0 };                              /* Scan found bit */
int32 RIDsect[2] = { 0, 0 };                            /* for Read ID */

/* Disk data structures

   xy_dev       CDR descriptor
   xy_unit      CDR unit descriptor
   xy_reg       CDR register list

   x = F or R
   y = 1 or 2
*/

UNIT r1_unit = { UDATA (&r1_svc, UNIT_FIX+UNIT_ATTABLE, 0), 100 };

REG r1_reg[] = {
    { FLDATA (NOTRDY, notrdy[0], 0) },
    { FLDATA (SEEK, seekbusy[0], 0) },
    { HRDATA (DAR, DDAR[0], 16) },
    { HRDATA (CAR, DCAR[0], 16) },
    { HRDATA (ERR, diskerr[0], 16) },
    { DRDATA (CYL, r1_unit.u3, 8) },
    { DRDATA (HEAD, seekhead[0], 8) },
    { DRDATA (POS, r1_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, r1_unit.wait, 24), PV_LEFT },
    { BRDATA (BUF, dbuf, 8, 8, 256) },
    { NULL }
};

DEVICE r1_dev = {
    "R1", &r1_unit, r1_reg, NULL,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &r1_reset,
    &r1_boot, &r1_attach, NULL
};

UNIT f1_unit = { UDATA (&f1_svc, UNIT_FIX+UNIT_ATTABLE, 0), 100 };

REG f1_reg[] = {
    { FLDATA (NOTRDY, notrdy[0], 0) },
    { FLDATA (SEEK, seekbusy[0], 0) },
    { HRDATA (DAR, DDAR[0], 16) },
    { HRDATA (CAR, DCAR[0], 16) },
    { HRDATA (ERR, diskerr[0], 16) },
    { DRDATA (CYL, f1_unit.u3, 8) },
    { DRDATA (HEAD, seekhead[0], 8) },
    { DRDATA (POS, f1_unit.pos, 32), PV_LEFT },
    { DRDATA (TIME, f1_unit.wait, 24), PV_LEFT },
    { BRDATA (BUF, dbuf, 8, 8, 256) },
    { NULL }
};

DEVICE f1_dev = {
    "F1", &f1_unit, f1_reg, NULL,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &f1_reset,
    &f1_boot, &f1_attach, NULL
};

UNIT r2_unit = { UDATA (&r2_svc, UNIT_FIX+UNIT_ATTABLE, 0), 100 };

REG r2_reg[] = {
    { FLDATA (NOTRDY, notrdy[1], 0) },
    { FLDATA (SEEK, seekbusy[1], 0) },
    { HRDATA (DAR, DDAR[1], 16) },
    { HRDATA (CAR, DCAR[1], 16) },
    { HRDATA (ERR, diskerr[1], 16) },
    { DRDATA (CYL, r2_unit.u3, 8) },
    { DRDATA (HEAD, seekhead[1], 8) },
    { DRDATA (POS, r2_unit.pos, 32), PV_LEFT },
    { DRDATA (TIME, r2_unit.wait, 24), PV_LEFT },
    { BRDATA (BUF, dbuf, 8, 8, 256) },
    { NULL }
};

DEVICE r2_dev = {
    "R2", &r2_unit, r2_reg, NULL,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &r2_reset,
    &r2_boot, &r2_attach, NULL
};

UNIT f2_unit = { UDATA (&f2_svc, UNIT_FIX+UNIT_ATTABLE, 0), 100 };

REG f2_reg[] = {
    { FLDATA (NOTRDY, notrdy[1], 0) },
    { FLDATA (SEEK, seekbusy[1], 0) },
    { HRDATA (DAR, DDAR[1], 16) },
    { HRDATA (CAR, DCAR[1], 16) },
    { HRDATA (ERR, diskerr[1], 16) },
    { DRDATA (CYL, f2_unit.u3, 8) },
    { DRDATA (HEAD, seekhead[1], 8) },
    { DRDATA (POS, f2_unit.pos, 32), PV_LEFT },
    { DRDATA (TIME, f2_unit.wait, 24), PV_LEFT },
    { BRDATA (BUF, dbuf, 8, 8, 256) },
    { NULL }
};

DEVICE f2_dev = {
    "F2", &f2_unit, f2_reg, NULL,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &f2_reset,
    &f2_boot, &f2_attach, NULL
};

/* -------------------------------------------------------------------- */

/* 5444: master routines */

int32 dsk1 (int32 op, int32 m, int32 n, int32 data)
{
    int32 r;
    
    r = dsk(0, op, m, n, data);
    return (r);
}

int32 dsk2 (int32 op, int32 m, int32 n, int32 data)
{
    int32 r;
    
    r = dsk(1, op, m, n, data);
    return (r);
}

/* 5444: operational routine */

int32 dsk (int32 disk, int32 op, int32 m, int32 n, int32 data)
{
    int32 iodata, i, j, u, sect, nsects, addr, r, c, res;
    int32 F, C, S, N, usave;
    UNIT *uptr;
    
    u = m;
    if (disk == 1) u += 2;
    F = GetMem(DCAR[disk]+0);                           /* Flag bits */
    C = GetMem(DCAR[disk]+1);                           /* Cylinder */
    S = GetMem(DCAR[disk]+2);                           /* Sector */
    N = GetMem(DCAR[disk]+3);                           /* Number of sectors */
    switch (u) {
        case 0:
            uptr = r1_dev.units;
            break;
        case 1:
            uptr = f1_dev.units;
            break;
        case 2:
            uptr = r2_dev.units;
            break;
        case 3:
            uptr = f2_dev.units;
            break;
        default:
            break;
    }
    if (debug_reg & 0x02)
        fprintf(trace, "==> %04X %s %01X,%d,%04X DAR=%04X CAR=%04X C=%02X, S=%02X, N=%02X\n",
            IAR[level],
            opstr[op],
            m, n, data,
            DDAR[disk],
            DCAR[disk],
            C, S, N);

    switch (op) {

        /* SIO 5444 */
        case 0:
            if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_UNATT;
            diskerr[disk] = 0;                          /* SIO resets errors */
            found[disk] = 0;                            /* ... and found bit */
            iodata = 0;
            switch (n) {
                case 0x00:                              /* Seek */
                    if (S & 0x80)
                        seekhead[disk] = 1;
                        else
                        seekhead[disk] = 0;
                    if (S & 1) {
                        uptr -> u3 += N;
                    } else {
                        uptr -> u3 -= N;
                    }
                    if (uptr -> u3 < 0)
                        uptr -> u3 = 0;
                    if (uptr -> u3 > 203) {
                        uptr -> u3 = 0;
                        diskerr[disk] |= 0x0100;
                        if (debug_reg & 0x02)
                            fprintf(trace, "==> Seek Past End of Disk\n");
                    }   

                    /*sim_activate(uptr, uptr -> wait);*/               
                    sim_activate(uptr, 1);              
                    
                    /* Seek arms are the same for both disks on a drive:
                       update the other arm */
                            
                    usave = uptr -> u3;     
                    if (u == 0) uptr = f1_dev.units;
                    if (u == 1) uptr = r1_dev.units;
                    if (u == 2) uptr = f2_dev.units;
                    if (u == 3) uptr = r2_dev.units;
                    uptr -> u3 = usave;     
                            
                    seekbusy[disk] = 1; 
                    iodata = SCPE_OK;
                    break;

                case 0x01:                              /* Read */
                    switch (data) {
                        case 0:                         /* Read data */
                            sect = (S >> 2) & 0x3F;
                            nsects = N + 1;
                            addr = DDAR[disk];

                            for (i = 0; i < nsects; i++) {
                                r = read_sector(uptr, dbuf, sect);
                                if (r != 1 || uptr->u3 != C) {
                                    diskerr[disk] |= 0x0800;
                                    break;
                                }   
                                for (j = 0; j < DSK_SECTSIZE; j++) {
                                    PutMem(addr, dbuf[j]);
                                    addr++;
                                }

                                if (sect == 55) {       /* HJS MODS */
                                    S = sect;
                                    N = nsects - i - 2;
                                    if (N > -1) diskerr[disk] |= 0x0020; /* end of cyl. */
                                    DDAR[disk] = addr & 0xFFFF;  /* HJS mod */
                                    PutMem(DCAR[disk]+2, S << 2);
                                    PutMem(DCAR[disk]+3, N);
                                    sim_activate(uptr, 1);
                                    iodata = SCPE_OK;
                                    break;
                                }

                                sect++;
                                S = sect - 1;
                                N = nsects - i - 2;
                                if (sect == 24)
                                    sect = 32;
                            }
                            DDAR[disk] = addr & 0xFFFF;   /* HJS mod */
                            PutMem(DCAR[disk]+2, S << 2);
                            PutMem(DCAR[disk]+3, N);
                            /*sim_activate(uptr, uptr -> wait);*/
                            sim_activate(uptr, 1);
                            iodata = SCPE_OK;
                            break;
                        case 1:                         /* Read ID */
                            if (uptr -> u3 > 0 && uptr -> u3 < 4)
                                PutMem(DCAR[disk], 1);
                                else
                                PutMem(DCAR[disk], 0);
                            PutMem(DCAR[disk]+1, uptr -> u3);
                            PutMem(DCAR[disk]+2, RIDsect[disk]);
                            RIDsect[disk]++;
                            if (RIDsect[disk] > 23)
                                RIDsect[disk] = 32;
                            if (RIDsect[disk] > 55)
                                RIDsect[disk] = 0;  
                            break;
                        case 2:                         /* Read Diagnostic */
                            iodata = STOP_INVDEV;
                            break;
                        case 3:                         /* Verify */
                            sect = (S >> 2) & 0x3F;
                            nsects = N + 1;
                            addr = DDAR[disk];
                            for (i = 0; i < nsects; i++) {
                                r = read_sector(uptr, dbuf, sect);
                                if (r != 1 || uptr->u3 != C) {
                                    diskerr[disk] |= 0x0800;
                                    break;
                                }   
                                if (sect == 55) {       /* HJS MODS */
                                    S = sect;
                                    N = nsects - i - 2;
                                    if (N > -1) diskerr[disk] |= 0x0020; /* end of cyl. */
                                    DDAR[disk] = addr & 0xFFFF;
                                    PutMem(DCAR[disk]+2, S << 2);
                                    PutMem(DCAR[disk]+3, N);
                                    sim_activate(uptr, 1);
                                    iodata = SCPE_OK;
                                    break;
                                }
                                sect++;
                                S = sect - 1;
                                N = nsects - i - 2;
                                if (sect == 24)
                                    sect = 32;
                            }
                            DDAR[disk] = addr & 0xFFFF;
                            PutMem(DCAR[disk]+2, S << 2);
                            PutMem(DCAR[disk]+3, N);
                            /*sim_activate(uptr, uptr -> wait);*/
                            sim_activate(uptr, 1);
                            break;      
                        default:
                            return STOP_INVDEV;
                    }                               
                    break;
                case 0x02:                              /* Write */
                    switch (data) {
                        case 0:                         /* Write Data */
                            sect = (S >> 2) & 0x3F;
                            nsects = N + 1;
                            addr = DDAR[disk];
                            for (i = 0; i < nsects; i++) {
                                for (j = 0; j < DSK_SECTSIZE; j++) {
                                    dbuf[j] = GetMem(addr);
                                    addr++;
                                }
                                r = write_sector(uptr, dbuf, sect);
                                if (r != 1 || uptr->u3 != C) {
                                    diskerr[disk] |= 0x0400;
                                    break;
                                }   
                                if (sect == 55) {   /* HJS MODS */
                                    S = sect;
                                    N = nsects - i - 2;
                                    if (N > -1) diskerr[disk] |= 0x0020; /* end of cyl. */
                                    DDAR[disk] = addr & 0xFFFF;
                                    PutMem(DCAR[disk]+2, S << 2);
                                    PutMem(DCAR[disk]+3, N);
                                    sim_activate(uptr, 1);
                                    iodata = SCPE_OK;
                                    break;
                                }
                                sect++;
                                S = sect - 1;
                                N = nsects - i - 2;
                                if (sect == 24)
                                    sect = 32;
                            }
                            DDAR[disk] = addr & 0xFFFF;  /* HJS mod */
                            PutMem(DCAR[disk]+2, S << 2);
                            PutMem(DCAR[disk]+3, N);
                            /*sim_activate(uptr, uptr -> wait);*/
                            sim_activate(uptr, 1);
                            break;
                        case 1:                         /* Write identifier */
                            if (seekhead[disk] == 0)
                                S = 0;
                                else
                                S = 0x80;
                            N = 23;

                            sect = (S >> 2) & 0x3F;
                            nsects = N + 1;
                            addr = DDAR[disk];
                            for (i = 0; i < nsects; i++) {
                                for (j = 0; j < DSK_SECTSIZE; j++) {
                                    dbuf[j] = GetMem(addr);
                                }
                                r = write_sector(uptr, dbuf, sect);
                                if (r != 1) {
                                    diskerr[disk] |= 0x0400;
                                    break;
                                }   
                                if (sect == 55) {
                                    S = sect;
                                    N = nsects - i - 2;
                                    if (N > 0) diskerr[disk] |= 0x0020;
                                        DDAR[disk] = addr & 0xFFFF;
                                    PutMem(DCAR[disk]+2, S << 2);
                                    PutMem(DCAR[disk]+3, N);
                                    sim_activate(uptr, 1);
                                    iodata = SCPE_OK;
                                    break;
                                }
                                sect++;
                                S = sect - 1;
                                N = nsects - i - 2;
                                if (sect == 24)
                                    sect = 32;
                            }
                            DDAR[disk] = addr & 0xFFFF;
                            PutMem(DCAR[disk]+2, S << 2);
                            PutMem(DCAR[disk]+3, N);
                            /*sim_activate(uptr, uptr -> wait);*/
                            sim_activate(uptr, 1);
                            break;
                        default:
                            return STOP_INVDEV;
                    }                               
                    break;
                case 0x03:                              /* Scan */
                    sect = (S >> 2) & 0x3F;
                    nsects = N + 1;
                    addr = DDAR[disk];
                    for (i = 0; i < nsects; i++) {
                        r = read_sector(uptr, dbuf, sect);
                        if (r != 1 || uptr->u3 != C) {
                            diskerr[disk] |= 0x0800;
                            break;
                        }
                        res = 0;    
                        for (j = 0; j < DSK_SECTSIZE; j++) {
                            c = GetMem(addr);
                            if (j != 0xff) {
                                if (dbuf[i] < c)
                                    res = 1;
                                if (dbuf[i] > c)
                                    res = 3;    
                            }
                            addr++;
                        }
                        if (res == 0)
                            found[disk] = 1;
                        if (res == data)
                            break;
                        if (sect == 55) {               /* HJS MODS */
                            S = sect;
                            N = nsects - i - 2;
                            if (N > -1) diskerr[disk] |= 0x0020; /* end of cyl. */
                            DDAR[disk] = addr & 0xFFFF;
                            PutMem(DCAR[disk]+2, S << 2);
                            PutMem(DCAR[disk]+3, N);
                            sim_activate(uptr, 1);
                            iodata = SCPE_OK;
                            break;
                        }
                        sect++;
                        S = sect - 1;
                        N = nsects - i - 2;
                        if (sect == 24)
                            sect = 32;
                    }
                    PutMem(DCAR[disk]+2, S << 2);
                    PutMem(DCAR[disk]+3, N);
                    /*sim_activate(uptr, uptr -> wait);*/
                    sim_activate(uptr, 1);
                    break;
                default:
                    return STOP_INVDEV;
            }
            return iodata;

        /* LIO 5444 */
        case 1:     
            if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_UNATT;
            switch (n) {
                case 0x04:                              /* Data Addr  */
                    DDAR[disk] = data;
                    break;
                case 0x06:                              /* Control Addr */
                    DCAR[disk] = data;
                    break;
                default:
                    return STOP_INVDEV;
            }
            return SCPE_OK;
        /* TIO 5444 */
        case 2:
            if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_UNATT << 16;
            iodata = 0;
            switch (n) {
                case 0x00:                              /* Error */
                    if (diskerr[disk] || notrdy[disk])
                        iodata = 1;
                    if ((uptr -> flags & UNIT_ATT) == 0)
                        iodata = 1;
                    break;
                case 0x02:                              /* Busy */
                    if (sim_is_active (uptr))
                        iodata = 1;
                    break;
                case 0x04:
                    if (found[disk])
                        iodata = 1;
                    break;          
                default:
                    return (STOP_INVDEV << 16);
            }                       
            return ((SCPE_OK << 16) | iodata);

        /* SNS 5444 */
        case 3: 
            if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_UNATT << 16;
            iodata = 0;
            switch (n) {
                case 0x01:
                    break;
                case 0x02:
                    iodata = diskerr[disk];
                    if (notrdy[disk])
                        iodata |= 0x4000;
                    if ((uptr -> flags & UNIT_ATT) == 0)
                        iodata |= 0x4000;
                    if (seekbusy[disk])
                        iodata |= 0x0010;
                    if (uptr -> u3 == 0)
                        iodata |= 0x0040;       
                    break;
                case 0x03:
                    iodata = 0;
                    break;  
                case 0x04:
                    iodata = DDAR[disk];
                    break;
                case 0x06:
                    iodata = DCAR[disk];
                    break;  
                default:
                    return (STOP_INVDEV << 16);
            }
            iodata |= ((SCPE_OK << 16) & 0xffff0000);       
            return (iodata);

        /* APL 5444 */
        case 4: 
            if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_UNATT << 16;
            iodata = 0;
            switch (n) {
                case 0x00:                              /* Error */
                    if (diskerr[disk] || notrdy[disk])
                        iodata = 1;
                    if ((uptr -> flags & UNIT_ATT) == 0)
                        iodata = 1;
                    break;
                case 0x02:                              /* Busy */
                    if (sim_is_active (uptr))
                        iodata = 1;
                    break;  
                default:
                    return (STOP_INVDEV << 16);
            }                       
            return ((SCPE_OK << 16) | iodata);
        default:
            break;
    }
    sim_printf (">>DSK%d non-existent function %d\n", disk, op);
    return SCPE_OK;                     
}

/* Disk unit service.  If a stacker select is active, copy to the
   selected stacker.  Otherwise, copy to the normal stacker.  If the
   unit is unattached, simply exit.
*/

t_stat r1_svc (UNIT *uptr)
{
seekbusy[0] = 0;
return SCPE_OK;
}
t_stat f1_svc (UNIT *uptr)
{
seekbusy[0] = 0;
return SCPE_OK;
}
t_stat r2_svc (UNIT *uptr)
{
seekbusy[1] = 0;
return SCPE_OK;
}
t_stat f2_svc (UNIT *uptr)
{
seekbusy[1] = 0;
return SCPE_OK;
}


/* Disk reset */

t_stat r1_reset (DEVICE *dptr)
{
diskerr[0] = notrdy[0] = seekbusy[0] = 0;               /* clear indicators */
found[0] = 0;
sim_cancel (&r1_unit);                                  /* clear event */
r1_unit.u3 = 0;                                         /* cylinder 0 */
return SCPE_OK;
}
t_stat f1_reset (DEVICE *dptr)
{
diskerr[0] = notrdy[0] = seekbusy[0] = 0;               /* clear indicators */
found[0] = 0;
sim_cancel (&f1_unit);                                  /* clear event */
f1_unit.u3 = 0;                                         /* cylinder 0 */
return SCPE_OK;
}
t_stat r2_reset (DEVICE *dptr)
{
diskerr[1] = notrdy[1] = seekbusy[1] = 0;               /* clear indicators */
found[1] = 0;
sim_cancel (&r2_unit);                                  /* clear event */
r2_unit.u3 = 0;                                         /* cylinder 0 */
return SCPE_OK;
}
t_stat f2_reset (DEVICE *dptr)
{
diskerr[1] = notrdy[1] = seekbusy[1] = 0;               /* clear indicators */
found[1] = 0;
sim_cancel (&f2_unit);                                  /* clear event */
f2_unit.u3 = 0;                                         /* cylinder 0 */
return SCPE_OK;
}

/* Disk unit attach */

t_stat r1_attach (UNIT *uptr, CONST char *cptr)
{
diskerr[0] = notrdy[0] = seekbusy[0] = 0;               /* clear status */
found[0] = 0;
uptr -> u3 = 0;                                         /* cylinder 0 */
return attach_unit (uptr, cptr);
}
t_stat f1_attach (UNIT *uptr, CONST char *cptr)
{
diskerr[0] = notrdy[0] = seekbusy[0] = 0;               /* clear status */
found[0] = 0;
uptr -> u3 = 0;                                         /* cylinder 0 */
return attach_unit (uptr, cptr);
}
t_stat r2_attach (UNIT *uptr, CONST char *cptr)
{
diskerr[1] = notrdy[1] = seekbusy[1] = 0;               /* clear status */
found[1] = 0;
uptr -> u3 = 0;                                         /* cylinder 0 */
return attach_unit (uptr, cptr);
}
t_stat f2_attach (UNIT *uptr, CONST char *cptr)
{
diskerr[1] = notrdy[1] = seekbusy[1] = 0;               /* clear status */
found[1] = 0;
uptr -> u3 = 0;                                         /* cylinder 0 */
return attach_unit (uptr, cptr);
}

/* Bootstrap routine */

t_stat r1_boot (int32 unitno, DEVICE *dptr)
{
int i;
r1_unit.u3 = 0;
read_sector(r1_dev.units, dbuf, 0);
for (i = 0; i < 256; i++) {
    M[i] = dbuf[i];
}
return SCPE_OK;
}
t_stat f1_boot (int32 unitno, DEVICE *dptr)
{
int i;
f1_unit.u3 = 0;
read_sector(f1_dev.units, dbuf, 0);
for (i = 0; i < 256; i++) {
    M[i] = dbuf[i];
}
return SCPE_OK;
}
t_stat r2_boot (int32 unitno, DEVICE *dptr)
{
int i;
r2_unit.u3 = 0;
read_sector(r2_dev.units, dbuf, 0);
for (i = 0; i < 256; i++) {
    M[i] = dbuf[i];
}
return SCPE_OK;
}
t_stat f2_boot (int32 unitno, DEVICE *dptr)
{
int i;
f2_unit.u3 = 0;
read_sector(f2_dev.units, dbuf, 0);
for (i = 0; i < 256; i++) {
    M[i] = dbuf[i];
}
return SCPE_OK;
}


/* Raw Disk Data In/Out */

int32 read_sector(UNIT *uptr, char *dbuf, int32 sect)
{
    static int32 rtn, realsect;
    static long pos;

                                                        /* calculate real sector no */
    if (sect > 23)
        realsect = sect - 8;
        else
        realsect = sect;
                                                        /* physically read the sector */
        pos = DSK_CYLSIZE * uptr -> u3;
        pos += DSK_SECTSIZE * realsect;
        rtn = fseek(uptr -> fileref, pos, 0);
        rtn = fread(dbuf, DSK_SECTSIZE, 1, uptr -> fileref);
        return (rtn);
}

int32 write_sector(UNIT *uptr, char *dbuf, int32 sect)
{
    static int32 rtn, realsect;
    static long pos;

                                                        /* calculate real sector no */
    if (sect > 23)
        realsect = sect - 8;
        else
        realsect = sect;
        if (uptr -> u3 == 0 && realsect == 32)
            rtn = 0;
                                                        /* physically write the sector */
        pos = DSK_CYLSIZE * uptr -> u3;
        pos += DSK_SECTSIZE * realsect;
        rtn = fseek(uptr -> fileref, pos, 0);
        rtn = fwrite(dbuf, DSK_SECTSIZE, 1, uptr -> fileref);
        return (rtn);
}
