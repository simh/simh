/*  $Id: s100_fif.c 1995 2008-07-15 03:59:13Z hharte $

    IMSAI FIF Disk Controller by Ernie Price

    Based on altairz80_dsk.c, Copyright (c) 2002-2014, Peter Schorn

    Plug-n-Play added by Howard M. Harte

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
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of Peter Schorn shall not
    be used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from Peter Schorn.

*/

#include "altairz80_defs.h"

#define UNIT_V_DSK_VERBOSE  (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_DSK_VERBOSE    (1 << UNIT_V_DSK_VERBOSE)
#define DSK_SECTSIZE        137 /* size of sector                                       */
#define DSK_SECT            32  /* sectors per track                                    */
#define MAX_TRACKS          254 /* number of tracks, original Altair has 77 tracks only */
#define DSK_TRACSIZE        (DSK_SECTSIZE * DSK_SECT)
#define MAX_DSK_SIZE        (DSK_TRACSIZE * MAX_TRACKS)

static t_stat fif_reset(DEVICE *dptr);
static t_stat fif_set_verbose(UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static int32 fif_io(const int32 port, const int32 io, const int32 data);
static const char* fif_description(DEVICE *dptr);

extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern uint8 GetBYTEWrapper(const uint32 Addr);
extern void  PutBYTEWrapper(const uint32 Addr, const uint32 Value);

extern uint32 PCX;

/* global data on status */

/* currently selected drive (values are 0 .. NUM_OF_DSK)
   current_disk < NUM_OF_DSK implies that the corresponding disk is attached to a file */
static int32 current_disk                   = NUM_OF_DSK;
static int32 warnLevelDSK                   = 3;
static int32 warnAttached   [NUM_OF_DSK]    = {0, 0, 0, 0, 0, 0, 0, 0};
static int32 warnDSK11                      = 0;

/* 88DSK Standard I/O Data Structures */

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
} FIF_INFO;

FIF_INFO fif_info_data = { { 0x0000, 0, 0xFD, 1 } };
FIF_INFO *fif_info = &fif_info_data;

static UNIT fif_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) }
};

#define FIF_NAME    "IMSAI"

static const char* fif_description(DEVICE *dptr) {
    return FIF_NAME;
}

static REG fif_reg[] = {
    { DRDATAD (DISK,         current_disk,   4,
               "Current selected disk")                                                     },
    { DRDATAD (DSKWL,        warnLevelDSK, 32,
               "Warn level register")                                                       },
    { BRDATAD (WARNATTACHED, warnAttached,   10, 32, NUM_OF_DSK,
               "Count for selection of unattached disk register array"), REG_CIRC + REG_RO  },
    { DRDATAD (WARNDSK11,    warnDSK11, 4,
               "Count of IN/OUT(9) on unattached disk register"), REG_RO                    },
    { NULL }
};

static MTAB fif_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0,                 "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    /* quiet, no warning messages       */
    { UNIT_DSK_VERBOSE, 0,                  "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " FIF_NAME "n"              },
    /* verbose, show warning messages   */
    { UNIT_DSK_VERBOSE, UNIT_DSK_VERBOSE,   "VERBOSE",  "VERBOSE",
        &fif_set_verbose, NULL, NULL, "Verbose messages for unit " FIF_NAME "n"     },
    { 0 }
};

DEVICE fif_dev = {
    "FIF", fif_unit, fif_reg, fif_mod,
    8, 10, 31, 1, 8, 8,
    NULL, NULL, &fif_reset,
    NULL, NULL, NULL,
    &fif_info_data, (DEV_DISABLE | DEV_DIS), 0,
    NULL, NULL, NULL, NULL, NULL, NULL, &fif_description
};

static void resetDSKWarningFlags(void) {
    int32 i;
    for (i = 0; i < NUM_OF_DSK; i++)
        warnAttached[i] = 0;
    warnDSK11 = 0;
}

static t_stat fif_set_verbose(UNIT *uptr, int32 value, CONST char *cptr, void *desc) {
    resetDSKWarningFlags();
    return SCPE_OK;
}

/* returns TRUE iff there exists a disk with VERBOSE */
static int32 hasVerbose(void) {
    int32 i;
    for (i = 0; i < NUM_OF_DSK; i++) {
        if (((fif_dev.units + i) -> flags) & UNIT_DSK_VERBOSE) {
            return TRUE;
        }
    }
    return FALSE;
}

/* service routines to handle simulator functions */

/* Reset routine */
static t_stat fif_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    resetDSKWarningFlags();
    current_disk = NUM_OF_DSK;

    if(dptr->flags & DEV_DIS) {
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &fif_io, TRUE);
    } else {
        /* Connect HDSK at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &fif_io, FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->mem_base);
            dptr->flags |= DEV_DIS;
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}

typedef struct desc_t
{
    uint8
        cmd_unit,   /* (cmd << 4) | unit : 1 = A: */
        result,     /* result: 0 == busy, 1 = normal completion, */
        nn,         /* number of secs ? */
        track,      /* track */
        sector,     /* sector */
        addr_l,     /* low  (transfer address) */
        addr_h;     /* high (transfer address) */
} desc_t;

static desc_t mydesc;

enum {NONE, WRITE_SEC, READ_SEC, FMT_TRACK};

#define SEC_SZ      128
#define SPT         26
#define UMASK       0xf

static uint8 blanksec[SEC_SZ];
/*                      0 1 2 3 4 5 6 7 8 9 a b c d e f */
static const uint8 utrans[] = {0,1,2,0,3,0,0,0,4,0,0,0,0,0,0,0};

/**************************************************

    Translate an IMSAI FIF disk request into an access into the harddrive file

*/
static int DoDiskOperation(desc_t *dsc, uint8 val)
{
    int32   current_disk_flags;
    int     kt,
            addr;
    FILE    *cpx;
    UNIT    *uptr;
    int32   rtn;

#if 0
    sim_printf("%02x %02x %02x %02x %02x %02x %02x %02x \n",
        val,
        dsc->cmd_unit,
        dsc->result,
        dsc->nn,
        dsc->track,
        dsc->sector,
        dsc->addr_l,
        dsc->addr_h);
#endif

    current_disk = (utrans[dsc->cmd_unit & UMASK]) - 1; /* 0 <= current_disk < NUM_OF_DSK */
    if (current_disk >= NUM_OF_DSK) {
        if (hasVerbose() && (warnDSK11 < warnLevelDSK)) {
            warnDSK11++;
/*03*/      sim_printf("FIF%i: " ADDRESS_FORMAT " Attempt disk io on illegal disk %d - ignored." NLP, current_disk, PCX, current_disk);
        }
        return 0;               /* no drive selected - can do nothing */
    }
    current_disk_flags = (fif_dev.units + current_disk) -> flags;
    if ((current_disk_flags & UNIT_ATT) == 0) { /* nothing attached? */
        if ((current_disk_flags & UNIT_DSK_VERBOSE) && (warnAttached[current_disk] < warnLevelDSK)) {
            warnAttached[current_disk]++;
/*02*/sim_printf("FIF%i: " ADDRESS_FORMAT " Attempt to select unattached FIF%d - ignored." NLP, current_disk, PCX, current_disk);
        }
        current_disk = NUM_OF_DSK;
        return 2;
    }

    uptr = fif_dev.units + current_disk;
    cpx = uptr->fileref;

    /* decode request: */
    switch (dsc->cmd_unit >> 4) {
        case FMT_TRACK:
            /*sim_printf("%c", dsc->track % 10 ? '*' : '0' + + dsc->track / 10); */
            /*Sleep(250); */
            memset(blanksec, 0, SEC_SZ);
            addr = dsc->track * SPT;
            if (sim_fseek(cpx, addr * SEC_SZ, SEEK_SET) == 0) {
                /* write a track worth of sectors */
                for (kt=0; kt < SPT; kt++) {
                    sim_fwrite(blanksec, 1, sizeof(blanksec), cpx);
                }
            } else {
                if ((current_disk_flags & UNIT_DSK_VERBOSE) &&
                    (warnAttached[current_disk] < warnLevelDSK)) {
                    warnAttached[current_disk]++;
                    sim_printf("FIF%i: " ADDRESS_FORMAT " sim_fseek error." NLP, current_disk, PCX);
                }
            }
            break;

        case READ_SEC:
            addr = (dsc->track * SPT) + dsc->sector - 1;
            if (sim_fseek(cpx, addr * SEC_SZ, SEEK_SET) == 0) {
                rtn = sim_fread(blanksec, 1, SEC_SZ, cpx);
                if ((rtn != SEC_SZ) && (current_disk_flags & UNIT_DSK_VERBOSE) &&
                    (warnAttached[current_disk] < warnLevelDSK)) {
                warnAttached[current_disk]++;
                sim_printf("FIF%i: " ADDRESS_FORMAT " sim_fread error." NLP, current_disk, PCX);
            }
            addr = dsc->addr_l + (dsc->addr_h << 8); /* no assumption on endianness */
            for (kt = 0; kt < SEC_SZ; kt++) {
                PutBYTEWrapper(addr++, blanksec[kt]);
            }
            } else {
                if ((current_disk_flags & UNIT_DSK_VERBOSE) &&
                    (warnAttached[current_disk] < warnLevelDSK)) {
                    warnAttached[current_disk]++;
                    sim_printf("FIF%i: " ADDRESS_FORMAT " sim_fseek error." NLP, current_disk, PCX);
                }
            }
            break;

        case WRITE_SEC:
            addr = (dsc->track * SPT) + dsc->sector - 1;
            if (sim_fseek(cpx, addr * SEC_SZ, SEEK_SET) == 0) {
                addr = dsc->addr_l + (dsc->addr_h << 8); /* no assumption on endianness */
                for (kt = 0; kt < SEC_SZ; kt++) {
                    blanksec[kt] = GetBYTEWrapper(addr++);
                }
                sim_fwrite(blanksec, 1, SEC_SZ, cpx);
            } else {
                if ((current_disk_flags & UNIT_DSK_VERBOSE) &&
                    (warnAttached[current_disk] < warnLevelDSK)) {
                    warnAttached[current_disk]++;
                    sim_printf("FIF%i: " ADDRESS_FORMAT " sim_fseek error." NLP, current_disk, PCX);
                }
            }
            break;

        default:
            ;
    }
    return 1;
}

/**********************************************************************

    Copy the disk descriptor from target RAM

*/
static void getdesc(uint16 addr) {
    uint32 x;
    uint8 *p1 = (uint8*)&mydesc;

    for (x = 0; x < sizeof(mydesc); x++) {
        *p1++ = GetBYTEWrapper(addr++);
    }
}

/**********************************************************************

    handle the IMSAI FIF floppy controller

*/
static int32 fif_io(const int32 port, const int32 io, const int32 data) {

    static int32    fdstate = 0;    /* chan 0xfd state */
    static int32    desc;
    static uint16   fdAdr[16];      /* disk descriptor address in 8080/z80 RAM */

    /* cmd | desc# */
    /*    cmd == 0x00 do operation */
    /*    cmd == 0x10 next 2 transfers are desc address */
    /* desc# is one of 16 0x0 - 0xf */

    if (!io) {
        return 0;
    }

    switch (fdstate) {
        case 0:
            desc = data & 0xf;
            if ((data & 0x10) != 0) { /* prefix 0x10 */
                fdstate++;          /* means desc address is next 2 out (fd),a */
            }
            else { /* do what descriptor says */
                getdesc(fdAdr[desc]);
                PutBYTEWrapper(fdAdr[desc] + 1,
                    (uint8)DoDiskOperation(&mydesc, (uint8)data));
            }
            break;

        case 1:
            /*sim_printf("D1 %02x %02x\n", desc, data); */
            fdAdr[desc] = data;        /* LSB of descriptor address */
            fdstate++;
            break;

        case 2:
            /*sim_printf("D2 %02x %02x\n", desc, data); */
            fdAdr[desc] |= data << 8;  /* MSB of descriptor address */
            fdstate = 0;
            break;
    }
    return 0;
}

#define ERNIES_FTP 0
#if ERNIES_FTP

#define WRK_BUF_SZ  150
#define FCB_SIZE    32
#define NAME_LTH    8
#define EXT_LTH     3


/**************************************************
*/
static void xfero(int32 addr, char *src, int32 lth)
{
    while (lth--) {
        PutBYTEWrapper(addr++, *src++);
    }
}

/**************************************************
*/
static void xferi(int32 addr, char *dst, int32 lth)
{
    while (lth--) {
        *dst++ = GetBYTEWrapper(addr++);
    }
}

#if !defined (_WIN32)
static void strupr(char *fn) {
    while (*fn) {
        if (('a' <= *fn) && (*fn <= 'z'))
            *fn -= 'a' - 'A';
        fn++;
    }
}
#endif

/**************************************************
*/
static void initfcb(char *fcb, char *fn, int32 flg)
{
    char *p1 = fcb;

    if (flg)
    {
        strupr(fn);
    }
    memset (fcb, 0 , FCB_SIZE);
    memset (fcb + 1, ' ', NAME_LTH + EXT_LTH);
    p1++;
    while (*fn && (*fn != '.'))
    {
        *p1++ = *fn++;
    }
    if (*fn == '.')
    {
        fn++;
    }
    p1 = fcb + NAME_LTH + 1;
    while (*fn && (*fn != '.'))
    {
        *p1++ = *fn++;
    }
}

/**************************************************

    FTP interface - most of the work is done here
    The IMDOS/CPM application only does minimal work

*/

char message[WRK_BUF_SZ];
char temp   [WRK_BUF_SZ];
FILE * myfile;

uint8 FTP(int32 BC, int32 DE)
{
    char   *p1, *p2;
    int32   retval;

    xferi(DE, temp, SEC_SZ);
    p1 = temp;
    switch (BC & 0x7f)
    {
        case 0:
            memcpy(message, p1 + 2, *(p1 + 1));
            *(message + *(p1 + 1)) = 0;
            p2 = strtok(message, " \t");
            if (!strcmp(p2, "get"))
            {
                p2 = strtok(NULL, " \t");
                if (myfile = fopen(p2, "rb"))
                {
                    initfcb(temp, p2, 1);
                    xfero(DE + 2, temp, 32);
                    retval = 0;
                    break;
                }
            }
            if (!strcmp(p2, "era"))
            {
                p2 = strtok(NULL, " \t");
                initfcb(temp, p2, 0);
                xfero(DE + 2, temp, 32);
                retval = 1;
                break;
            }
            retval = 0xff;
            break;

        case 20:
            memset(temp, 0x1a, SEC_SZ);
            retval = sim_fread(temp, 1, SEC_SZ, myfile) ? 0 : 1;
            xfero( DE, temp, SEC_SZ);
            if (retval)
            {
                fclose(myfile);
            }
            break;
    }
    return retval;
}

#endif /* ERNIES_FTP */

/* end of the source */



