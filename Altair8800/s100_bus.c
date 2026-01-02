/* s100_bus.c - S100 Bus Simulator

   Copyright (c) 2025, Patrick A. Linstruth

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

   Except as contained in this notice, the name of Patrick Linstruth shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   History:
   07-Nov-2025   PAL   Initial version

*/

#include "sim_defs.h"
#include "altair8800_sys.h"
#include "s100_z80.h"
#include "s100_bus.h"

static t_stat bus_reset               (DEVICE *dptr);
static t_stat bus_dep                 (t_value val, t_addr addr, UNIT *uptr, int32 sw);
static t_stat bus_ex                  (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
static t_stat bus_cmd_memory          (int32 flag, const char *cptr);
static t_stat bus_show_config         (FILE *st, UNIT *uptr, int32 val, const void *desc);
static t_stat bus_show_console        (FILE *st, UNIT *uptr, int32 val, const void *desc);
static t_stat bus_show_help           (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static t_stat bus_hexload_command     (int32 flag, const char *cptr);
static t_stat bus_hexsave_command     (int32 flag, const char *cptr);
static t_stat hexload                 (const char *filename, t_addr bias);
static t_stat hexsave                 (FILE *outFile, t_addr start, t_addr end);

static ChipType chiptype = CHIP_TYPE_Z80;

static MDEV mdev_table[MAXPAGE];      /* Active memory table  */
static MDEV mdev_dflt;                /* Default memory table */

static uint32 bus_addr = 0x0000;

static int32 poc = TRUE; /* Power On Clear */

/* Interrupts */
uint32 nmiInterrupt = 0x00;      /* NMI                     */
uint32 vectorInterrupt = 0x00;   /* Vector Interrupt bits   */
uint8 dataBus[MAX_INT_VECTORS];  /* Data bus value          */

/*  This is the I/O configuration table. There are 255 possible
    device addresses, if a device is plugged to a port it's routine
    address is here, 'nulldev' means no device is available
*/
IDEV idev_in[MAXPAGE];
IDEV idev_out[MAXPAGE];

int32 nulldev(const int32 addr, const int32 io, const int32 data) { return 0xff; }

/* Which UNIT is the CONSOLE */
UNIT *bus_console = NULL;

static const char* bus_description(DEVICE *dptr) {
    return "S100 Bus";
}

static UNIT bus_unit = {
    UDATA (NULL, 0, 0)
};

static REG bus_reg[] = {
    { HRDATAD (WRU,     sim_int_char,       8, "Interrupt character pseudo register"), },
    { FLDATAD (POC,     poc,       0x01,         "Power on Clear flag"), },
    { HRDATAD(VECINT,vectorInterrupt,       8, "Vector Interrupt pseudo register"), },
    { BRDATAD (DATABUS, dataBus, 16, 8,     MAX_INT_VECTORS, "Data bus pseudo register"), REG_RO + REG_CIRC },
    { HRDATAD(NMI,       nmiInterrupt,       1, "NMI Interrupt pseudo register"), },
    { NULL }
};

static MTAB bus_mod[] = {
    { UNIT_BUS_VERBOSE,     UNIT_BUS_VERBOSE,   "VERBOSE",      "VERBOSE",      NULL, NULL,
        NULL, "Enable verbose messages"     },
    { UNIT_BUS_VERBOSE,     0,                  "QUIET",        "QUIET",        NULL, NULL,
        NULL, "Disable verbose messages"                },

    { MTAB_XTD | MTAB_VDV | MTAB_NMO,  0, "CONFIG",   NULL, NULL, &bus_show_config,  NULL, "Show BUS configuration" },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO,  0, "CONSOLE",  NULL, NULL, &bus_show_console, NULL, "Show CONSOLE unit" },

    { 0 }
};

static DEBTAB bus_dt[] = {
    { NULL,     0 }
};

DEVICE bus_dev = {
    "BUS", &bus_unit, bus_reg, bus_mod,
    1, ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    &bus_ex, &bus_dep, &bus_reset,
    NULL, NULL, NULL,
    NULL, 0, 0,
    bus_dt, NULL, NULL, &bus_show_help, NULL, NULL, &bus_description
};

/* Simulator-specific commands */
static CTAB bus_cmd_tbl[] = {
    { "REG",     &z80_cmd_reg,         0, "REG                            Display registers\n" },
    { "MEM",     &bus_cmd_memory,      0, "MEM <address>                  Dump a block of memory\n" },
    { "HEXLOAD", &bus_hexload_command, 0, "HEXLOAD [fname] <bias>         Load Intel hex file\n" },
    { "HEXSAVE", &bus_hexsave_command, 0, "HEXSAVE [fname] [start-end]    Save Intel hex file\n" },
    { NULL, NULL, 0, NULL }
};

/* bus reset */
static t_stat bus_reset(DEVICE *dptr) {
    int i;

    if (poc) {
        sim_vm_cmd = bus_cmd_tbl;

    /* Clear MEM and IO table */
        for (i = 0; i < MAXPAGE; i++) {
            mdev_table[i].routine = &nulldev;
            mdev_table[i].name = "nulldev";

            mdev_dflt.routine = &nulldev;
            mdev_dflt.name = "nulldev";

            idev_in[i].routine = &nulldev;
            idev_in[i].name = "nulldev";
            idev_out[i].routine = &nulldev;
            idev_out[i].name = "nulldev";
        }

        poc = FALSE;
    }

    return SCPE_OK;
}

/* memory examine */
static t_stat bus_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
    *vptr = s100_bus_memr(addr & ADDRMASK);

    return SCPE_OK;
}

/* memory deposit */
static t_stat bus_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
    s100_bus_memw(addr & ADDRMASK, val);
 
    return SCPE_OK;
}

static t_stat bus_show_config(FILE *st, UNIT *uptr, int32 val, const void *desc)
{
    const char *last = NULL;
    int i, spage, epage;

    /* show memory */
    fprintf(st, "\nMEMORY:\n");

    for (i = 0; i < MAXPAGE; i++) {
        if (mdev_table[i].name != last) {
            if (last != NULL) {
                fprintf(st, "%04X-%04X: %s\n", spage << LOG2PAGESIZE, (epage << LOG2PAGESIZE) | 0xff, mdev_table[epage].routine != &nulldev ? sys_strupr(last) : "");
            }

            last = mdev_table[i].name;
            spage = i;
        }

        epage = i;
    }

    fprintf(st, "%04X-%04X: %s\n", spage << LOG2PAGESIZE, (epage << LOG2PAGESIZE) | 0xff, mdev_table[epage].routine != &nulldev ? sys_strupr(last) : "");

    fprintf(st, "\nDefault Memory Device: %s\n", sys_strupr(mdev_dflt.name));

    /* show which ports are assigned */
    fprintf(st, "\nIO:\n");
    fprintf(st, "PORT %-8.8s  %-8.8s\n", "IN", "OUT");
    for (i = 0; i < MAXPAGE; i++) {
        if (idev_in[i].routine != &nulldev || idev_out[i].routine != &nulldev) {
            fprintf(st, "%02X:  ", i);
            fprintf(st, "%-8.8s  ", sys_strupr(idev_in[i].name));  /* strupr must be output before called again */
            fprintf(st, "%-8.8s\n", sys_strupr(idev_out[i].name));
        }
    }

    fprintf(st, "\n");

    bus_show_console(st, NULL, 0, NULL);

    return SCPE_OK;
}

static t_stat bus_show_console(FILE *st, UNIT *uptr, int32 val, const void *desc)
{
    /* show current CONSOLE unit */
    fprintf(st, "CONSOLE Unit: %s\n", (bus_console == NULL) ? "NONE" : sim_uname(bus_console));

    return SCPE_OK;
}

void s100_bus_get_idev(int32 port, IDEV *idev_in, IDEV *idev_out)
{
    if (idev_in != NULL) {
        idev_in->routine = idev_in[port & 0xff].routine;
        idev_in->name = idev_in[port & 0xff].name;
    }
    if (idev_out != NULL) {
        idev_out->routine = idev_out[port & 0xff].routine;
        idev_out->name = idev_out[port & 0xff].name;
    }
}

t_stat s100_bus_addio(int32 port, int32 size, int32 (*routine)(const int32, const int32, const int32), const char *name)
{
    s100_bus_addio_in(port, size, routine, name);
    s100_bus_addio_out(port, size, routine, name);

    return SCPE_OK;
}

t_stat s100_bus_addio_in(int32 port, int32 size, int32 (*routine)(const int32, const int32, const int32), const char *name)
{
    int i;

    for (i = port; i < port + size; i++) {
        if (bus_unit.flags & UNIT_BUS_VERBOSE) {
            sim_printf("Mapping IO %04x IN, handler=%s\n", i, name);
        }

        idev_in[i & 0xff].routine = routine;
        idev_in[i & 0xff].name = name;
    }

    return SCPE_OK;
}

t_stat s100_bus_addio_out(int32 port, int32 size, int32 (*routine)(const int32, const int32, const int32), const char *name)
{
    int i;

    for (i = port; i < port + size; i++) {
        if (bus_unit.flags & UNIT_BUS_VERBOSE) {
            sim_printf("Mapping IO %04x OUT, handler=%s\n", i, name);
        }

        idev_out[i & 0xff].routine = routine;
        idev_out[i & 0xff].name = name;
    }

    return SCPE_OK;
}


t_stat s100_bus_remio(int32 port, int32 size, int32 (*routine)(const int32, const int32, const int32))
{
    s100_bus_remio_in(port, size, routine);
    s100_bus_remio_out(port, size, routine);

    return SCPE_OK;
}

t_stat s100_bus_remio_in(int32 port, int32 size, int32 (*routine)(const int32, const int32, const int32))
{
    int i;

    for (i = port; i < port + size; i++) {
        if (idev_in[i & 0xff].routine == routine) {
            if (bus_unit.flags & UNIT_BUS_VERBOSE) {
                sim_printf("Unmapping IO %04x IN, handler=%s\n", i, idev_in[i & 0xff].name);
            }

            idev_in[i & 0xff].routine = &nulldev;
            idev_in[i & 0xff].name = "nulldev";
        }
    }

    return SCPE_OK;
}

t_stat s100_bus_remio_out(int32 port, int32 size, int32 (*routine)(const int32, const int32, const int32))
{
    int i;

    for (i = port; i < port + size; i++) {
        if (idev_out[i & 0xff].routine == routine) {
            if (bus_unit.flags & UNIT_BUS_VERBOSE) {
                sim_printf("Unmapping IO %04x OUT, handler=%s\n", i, idev_out[i & 0xff].name);
            }

            idev_out[i & 0xff].routine = &nulldev;
            idev_out[i & 0xff].name = "nulldev";
        }
    }

    return SCPE_OK;
}

void s100_bus_get_mdev(int32 addr, MDEV *mdev)
{
    int32 page;

    page = (addr & ADDRMASK) >> LOG2PAGESIZE;

    if (mdev != NULL) {
        mdev->routine = mdev_table[page].routine;
        mdev->name = mdev_table[page].name;
    }
}

t_stat s100_bus_addmem(int32 baseaddr, uint32 size, 
    int32 (*routine)(const int32 addr, const int32 rw, const int32 data), const char *name)
{
    int32 page;
    uint32 i;

    page = (baseaddr & ADDRMASK) >> LOG2PAGESIZE;

    if (size < PAGESIZE) {
        size = PAGESIZE;
    }

    if (bus_unit.flags & UNIT_BUS_VERBOSE) {
        sim_printf("addmem: baseaddr=%04X page=%02X size=%04X LOG2SIZE=%04X name=%s\n", baseaddr, page, size, size >> LOG2PAGESIZE, name);
    }

    for (i = 0; i < (size >> LOG2PAGESIZE); i++) {
            mdev_table[page + i].routine = routine;
            mdev_table[page + i].name = name;
    }

    return SCPE_OK;
}

t_stat s100_bus_setmem_dflt(int32 (*routine)(const int32 addr, const int32 rw, const int32 data), const char *name)
{
    mdev_dflt.routine = routine;
    mdev_dflt.name = name;

    return SCPE_OK;
}

t_stat s100_bus_remmem(int32 baseaddr, uint32 size, 
    int32 (*routine)(const int32 addr, const int32 rw, const int32 data))
{
    int32 page;
    uint32 i;

    page = (baseaddr & ADDRMASK) >> LOG2PAGESIZE;

    for (i = 0; i < (size >> LOG2PAGESIZE); i++) {
        if (mdev_table[page + i].routine == routine) {
            mdev_table[page + i].routine = mdev_dflt.routine;
            mdev_table[page + i].name = mdev_dflt.name;
        }
    }

    return SCPE_OK;
}

t_stat s100_bus_remmem_dflt(int32 (*routine)(const int32 addr, const int32 rw, const int32 data))
{
    if (mdev_dflt.routine == routine) {
        mdev_dflt.routine = &nulldev;
        mdev_dflt.name = "nulldev";
    }

    return SCPE_OK;
}

int32 s100_bus_in(int32 port)
{
    return idev_in[port].routine(port, S100_IO_READ, 0);
}

void s100_bus_out(int32 port, int32 data)
{
    idev_out[port].routine(port, S100_IO_WRITE, data);
}

int32 s100_bus_memr(t_addr addr)
{
    int32 page;

    page = (addr & ADDRMASK) >> LOG2PAGESIZE;

    return mdev_table[page].routine(addr, S100_IO_READ, 0);
}

void s100_bus_memw(t_addr addr, int32 data)
{
    int32 page;

    page = (addr & ADDRMASK) >> LOG2PAGESIZE;

    mdev_table[page].routine(addr, S100_IO_WRITE, data);
}

ChipType s100_bus_set_chiptype(ChipType new)
{
    chiptype = new;

    return chiptype;
}

ChipType s100_bus_get_chiptype(void)
{
    return chiptype;
}

uint32 s100_bus_set_addr(uint32 new)
{
    bus_addr = new;

    return bus_addr;
}

uint32 s100_bus_get_addr(void)
{
    return bus_addr;
}

uint32 s100_bus_int(int32 vector, int32 data)
{
    vectorInterrupt |= vector;
    dataBus[vector] = data;

    return vectorInterrupt;
}

uint32 s100_bus_get_int(void)
{
    return vectorInterrupt;
}

uint32 s100_bus_get_int_data(int32 vector)
{
    return dataBus[vector];
}

uint32 s100_bus_clr_int(int32 vector)
{
    vectorInterrupt &= ~(1 << vector);

    return vectorInterrupt;
}

void s100_bus_nmi()
{
    nmiInterrupt = TRUE;
}

int32 s100_bus_get_nmi()
{
    return nmiInterrupt;
}

void s100_bus_clr_nmi()
{
    nmiInterrupt = FALSE;
}

static t_stat bus_cmd_memory(int32 flag, const char *cptr)
{
    char abuf[16];
    t_addr lo, hi, last;
    t_value byte;
    static t_addr disp_addr = 0;

    if (get_range(NULL, cptr, &lo, &hi, 16, ADDRMASK, 0) == NULL) {
        lo = hi = disp_addr;
    }
    else {
        disp_addr = lo & ~(0x0f);
    }

    if (hi == lo) {
        hi = (lo & ~(0x0f)) + 0xff;
    }

    last = hi | 0x00000f;

    while (disp_addr <= last && disp_addr <= ADDRMASK) {

        if (!(disp_addr & 0x0f)) {
            if (ADDRMASK+1 <= 0x10000) {
                sim_printf("%04X ", disp_addr);
            }
            else {
                sim_printf("%02X:%04X ", disp_addr >> 16, disp_addr & 0xffff);
            }
        }

        if (disp_addr < lo || disp_addr > hi) {
            sim_printf("   ");
            abuf[disp_addr & 0x0f] = ' ';
        }
        else {
            byte = s100_bus_memr(disp_addr);
            sim_printf("%02X ", byte);
            abuf[disp_addr & 0x0f] = sim_isprint(byte) ? byte : '.';
        }

        if ((disp_addr & 0x000f) == 0x000f) {
            sim_printf("%16.16s\n", abuf);
        }

        disp_addr++;
    }

    if (disp_addr > ADDRMASK) {
        disp_addr = 0;
    }

    return SCPE_OK | SCPE_NOMESSAGE;
}

/*  This is the binary loader. The input file is considered to be a string of
    literal bytes with no special format. The load starts at the current value
    of the PC if no start address is given. If the input string ends with ROM
    (not case sensitive) the memory area is made read only.
    ALTAIRROM/NOALTAIRROM settings are ignored.
*/

t_stat sim_load(FILE *fileref, const char *cptr, const char *fnam, int flag)
{
    int32 i;
    uint32 addr, cnt = 0, org;
    t_addr j, lo, hi;
    const char *result;
    char gbuf[CBUFSIZE];

    if (flag) { /* dump ram to file */
        result = get_range(NULL, cptr, &lo, &hi, 16, ADDRMASK, 0);

        if (result == NULL) {
            return SCPE_ARG;
        }

        for (j = lo; j <= hi; j++) {
            if (putc(s100_bus_memr(j & ADDRMASK), fileref) == EOF) {
                return SCPE_IOERR;
            }
        }
        sim_printf("%d byte%s dumped [%x - %x] to %s.\n", PLURAL(hi + 1 - lo), lo, hi, fnam);

        return SCPE_OK;
    }

    if (*cptr == 0) {
        addr = s100_bus_get_addr();
    }

    else {
        get_glyph(cptr, gbuf, 0);

        addr = strtotv(cptr, &result, 16) & ADDRMASK;

        if (cptr == result) {
            return SCPE_ARG;
        }

        while (isspace(*result)) {
            result++;
        }
    }

    /* addr is start address to load to, makeROM == TRUE iff memory should become ROM */
    org = addr;

    while ((addr < MAXBANKSIZE) && ((i = getc(fileref)) != EOF)) {
        s100_bus_memw(addr & ADDRMASK, i);
        addr++;
        cnt++;
    }

    sim_printf("%d (%04X) byte%s [%d page%s] loaded at %04X.\n", cnt, PLURAL(cnt), PLURAL((cnt + 0xff) >> 8), org);

    return SCPE_OK;
}

static t_stat bus_hexload_command(int32 flag, const char *cptr)
{
    char filename[4*CBUFSIZE];
    t_addr lo = 0, hi = 0;

    GET_SWITCHES(cptr);        /* get switches */

    if (*cptr == 0) {          /* must be more */
        return SCPE_2FARG;
    }

    cptr = get_glyph_quoted(cptr, filename, 0);    /* get filename */
    sim_trim_endspc(filename);

    if (*cptr != 0) {          /* bias available */
        get_range(NULL, cptr, &lo, &hi, ADDRRADIX, 0, 0);
    }

    lo &= ADDRMASK;

    hexload(filename, lo);

    return SCPE_OK;
}

static t_stat bus_hexsave_command(int32 flag, const char *cptr)
{
    char filename[4*CBUFSIZE];
    FILE *sfile;
    t_addr lo = 0, hi = 0;

    GET_SWITCHES(cptr);        /* get switches */

    if (*cptr == 0) {          /* must be more */
        return SCPE_2FARG;
    }

    cptr = get_glyph_quoted(cptr, filename, 0);    /* get filename */
    sim_trim_endspc(filename);

    if (*cptr == 0) {          /* must be more */
        return SCPE_2FARG;
    }

    get_range(NULL, cptr, &lo, &hi, ADDRRADIX, 0, 0);

    lo &= ADDRMASK;
    hi &= ADDRMASK;

    if (hi < lo) {  /* bad addresses */
        return SCPE_ARG;
    }

    if ((sfile = sim_fopen(filename, "w")) == NULL) {    /* try existing file */
        return SCPE_OPENERR;
    }

    hexsave(sfile, lo, hi);

    sim_printf("Output file: %s\n", filename);

    fclose (sfile);

    return SCPE_OK;
}

/* hexload will load an Intel hex file into RAM.
   Based on HEX2BIN by Mike Douglas
   https://deramp.com/downloads/misc_software/hex-binary utilities for the PC/
*/

#define INBUF_LEN 600

static t_stat hexload(const char *filename, t_addr bias)
{
    FILE *sFile;
    char inBuf[INBUF_LEN];
    char dataStr[INBUF_LEN];
    int sRecords = 0;
    int byteCount, dataAddr, recType, dataByte, checkSum;
    int lowAddr = ADDRMASK;
    int highAddr = 0;
    char *bufPtr;

    if ((sFile = sim_fopen(filename, "r")) == NULL) {    /* try existing file */
        return SCPE_OPENERR;
    }

    /* Read the hex or s-record file and put data into a memory image array */
    while (fgets(inBuf, INBUF_LEN, sFile)) {
        inBuf[strcspn(inBuf, "\n")] = '\0';  /* end string at new line if present */

        if (sRecords) {
            sscanf(inBuf, "S%1X%2x%4x%s", &recType, &byteCount, &dataAddr, dataStr);
            checkSum = byteCount + (dataAddr >> 8) + (dataAddr & 0xff) + 1;
            byteCount -= 3;    /* make byteCount = data bytes only */
            recType--;         /* make S1 match .hex record type 0 */
        }
        else {
            sscanf(inBuf, ":%2x%4x%2x%s", &byteCount, &dataAddr, &recType, dataStr);
            checkSum = byteCount + (dataAddr >> 8) + (dataAddr & 0xff) + recType;
        }

        bufPtr = dataStr;

        if ((recType == 0) && (byteCount > 0) && (dataAddr+byteCount <= MAXADDR)) {
            if (dataAddr+byteCount > highAddr) {
                highAddr = dataAddr + byteCount;
            }

            if (dataAddr < lowAddr) {
                lowAddr = dataAddr;
            }

            do {
                sscanf(bufPtr, "%2x", &dataByte);
                bufPtr += 2;
                s100_bus_memw((dataAddr + bias) & ADDRMASK, dataByte);   /* Write to memory */
                dataAddr++;
                checkSum += dataByte;
            } while (--byteCount != 0);

            sscanf(bufPtr, "%2x", &dataByte);          /* checksum byte */

            if (0 != ((checkSum+dataByte) & 0xff)) {
                fprintf(stderr,"Checksum error\n  %s\n", inBuf);
                fclose (sFile);

                return SCPE_IERR;
            }
        }
    }

    /* Display results */
    if (bias) {
        sim_printf("%s: %04X (%04X+%04X)-%04X (%04X+%04X)\n", filename,
            (lowAddr + bias) & ADDRMASK, lowAddr, bias,
            (highAddr + bias - 1) & ADDRMASK, highAddr, bias);
    }
    else {
        sim_printf("%s %04X-%04X\n", filename, lowAddr, highAddr-1);
    }

    fclose (sFile);

    return SCPE_OK;
}

/* hexsave will load an Intel hex file into RAM.
   Based on HEX2BIN by Mike Douglas
   https://deramp.com/downloads/misc_software/hex-binary utilities for the PC/
*/

#define LINE_LEN 32

static t_stat hexsave(FILE *outFile, t_addr start, t_addr end)
{
    uint8 inBuf[INBUF_LEN];
    int sRecords = 0;
    int checkSum;
    uint32 i;
    t_addr dataAddr;
    uint32 byteCount;

    dataAddr = start;

    do {
        for (byteCount = 0; byteCount < LINE_LEN && dataAddr + byteCount <= end; byteCount++) {
            inBuf[byteCount] = s100_bus_memr(dataAddr + byteCount);
        }


        if (byteCount > 0) {
            if (sRecords) {
                fprintf(outFile, "S1%02X%04X", byteCount+3, dataAddr);
                checkSum = byteCount + (dataAddr >> 8) + (dataAddr & 0xff) + 4;
            }
            else {
                fprintf(outFile, ":%02X%04X00", byteCount, dataAddr);
                checkSum = byteCount + (dataAddr >> 8) + (dataAddr & 0xff);
            }

            for (i=0; i<byteCount; i++) {
                fprintf(outFile,"%02X", inBuf[i]);
                checkSum += inBuf[i];
            }

            fprintf(outFile, "%02X\n", -checkSum & 0xff);
            dataAddr += byteCount;
        }
    } while (dataAddr <= end);

    /* Finish output file and display results */

    if (sRecords) {
        fprintf(outFile, "S9\n");
    }
    else {
        fprintf(outFile, ":00000001FF\n");
    }

    sim_printf("Start address  = %04X\n", start);
    sim_printf("High address = %04X\n", dataAddr-1);

    return SCPE_OK;
}

/*
 * set_membase, show_membase, set_iobase, and show_iobase
 *
 * Generic functions for change a device's base memory and io
 * addresses.
 *
 * DEVICE *ctxt = must point to the address of a RES resource structure.
 */

/* Set Memory Base Address routine */
t_stat set_membase(UNIT *uptr, int32 val, const char *cptr, void *desc)
{
    DEVICE *dptr;
    RES *res;
    uint32 newba;
    t_stat r;

    if (cptr == NULL) {
        return SCPE_ARG;
    }

    if (uptr == NULL) {
        return SCPE_IERR;
    }

    if ((dptr = find_dev_from_unit(uptr)) == NULL) {
        return SCPE_IERR;
    }

    res = (RES *) dptr->ctxt;

    if (res == NULL) {
        return SCPE_IERR;
    }

    newba = get_uint (cptr, 16, 0xFFFF, &r);

    if (r != SCPE_OK) {
        return r;
    }

    if ((newba > 0xFFFF) || (newba % res->mem_size)) {
        return SCPE_ARG;
    }

    if (dptr->flags & DEV_DIS) {
        sim_printf("device not enabled yet.\n");
        res->mem_base = newba & ~(res->mem_size-1);
    }
    else {
        dptr->flags |= DEV_DIS;
        dptr->reset(dptr);
        res->mem_base = newba & ~(res->mem_size-1);
        dptr->flags &= ~DEV_DIS;
        dptr->reset(dptr);
    }

    return SCPE_OK;
}

/* Show Base Address routine */
t_stat show_membase(FILE *st, UNIT *uptr, int32 val, const void *desc)
{
    DEVICE *dptr;
    RES *res;

    if (uptr == NULL) {
        return SCPE_IERR;
    }

    if ((dptr = find_dev_from_unit(uptr)) == NULL) {
        return SCPE_IERR;
    }

    res = (RES *) dptr->ctxt;

    if (res == NULL) {
        return SCPE_IERR;
    }

    fprintf(st, "MEM=0x%04X-0x%04X", res->mem_base, res->mem_base + res->mem_size-1);

    return SCPE_OK;
}

/* Set Memory Base Address routine */
t_stat set_iobase(UNIT *uptr, int32 val, const char *cptr, void *desc)
{
    DEVICE *dptr;
    RES *res;
    uint32 newba;
    t_stat r;

    if (cptr == NULL) {
        return SCPE_ARG;
    }

    if (uptr == NULL) {
        return SCPE_IERR;
    }

    if ((dptr = find_dev_from_unit(uptr)) == NULL) {
        return SCPE_IERR;
    }

    res = (RES *) dptr->ctxt;

    if (res == NULL) {
        return SCPE_IERR;
    }

    newba = get_uint (cptr, 16, 0xFF, &r);

    if (r != SCPE_OK) {
        return r;
    }

    if ((newba > 0xFF) || (newba % res->io_size)) {
        return SCPE_ARG;
    }

    if (dptr->flags & DEV_DIS) {
        sim_printf("device not enabled yet.\n");
        res->io_base = newba & ~(res->io_size-1);
    } else {
        dptr->flags |= DEV_DIS;
        dptr->reset(dptr);
        res->io_base = newba & ~(res->io_size-1);
        dptr->flags &= ~DEV_DIS;
        dptr->reset(dptr);
    }

    return SCPE_OK;
}

/* Show I/O Base Address routine */
t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, const void *desc)
{
    DEVICE *dptr;
    RES *res;

    if (uptr == NULL) {
        return SCPE_IERR;
    }

    if ((dptr = find_dev_from_unit(uptr)) == NULL) {
        return SCPE_IERR;
    }

    res = (RES *) dptr->ctxt;

    if (res == NULL) {
        return SCPE_IERR;
    }

    fprintf(st, "I/O=0x%02X-0x%02X", res->io_base, res->io_base + res->io_size-1);

    return SCPE_OK;
}

/* Set new CONSOLE unit */
t_stat s100_bus_console(UNIT *uptr)
{
    bus_console = uptr;

    return SCPE_ARG;
}

/* Set new CONSOLE unit */
UNIT *s100_bus_get_console()
{
    return bus_console;
}

t_stat s100_bus_noconsole(UNIT *uptr)
{
    if (bus_console == uptr) {
        bus_console = NULL;

        return SCPE_OK;
    }

    return SCPE_ARG;
}

t_stat s100_bus_poll_kbd(UNIT *uptr)
{
    if (bus_console == uptr) {
        return sim_poll_kbd();
    }

    return SCPE_OK;
}

static t_stat bus_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nAltair 8800 Bus (%s)\n", dptr->name);

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);

    return SCPE_OK;
}

