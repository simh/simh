/*************************************************************************
 *                                                                       *
 * $Id: n8vem.c 1995 2008-07-15 03:59:13Z hharte $                       *
 *                                                                       *
 * Copyright (c) 2007-2008 Howard M. Harte.                              *
 * http://www.hartetec.com                                               *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * Howard M. Harte.                                                      *
 *                                                                       *
 * SIMH Interface based on altairz80_hdsk.c, by Peter Schorn.            *
 *                                                                       *
 * Module Description:                                                   *
 *     N8VEM Single-Board Computer I/O module for SIMH.                  *
 * http://groups.google.com/group/n8vem/web/n8vem-single-board-computer-home-page *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/* #define DBG_MSG */

#include "altairz80_defs.h"

#if defined (_WIN32)
#include <windows.h>
#endif

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

/* Debug flags */
#define PIO_MSG     (1 << 0)
#define UART_MSG    (1 << 1)
#define MPCL_MSG    (1 << 2)
#define ROM_MSG     (1 << 3)
#define VERBOSE_MSG (1 << 4)

#define N8VEM_MAX_DRIVES    2

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint8 *ram;
    uint8 *rom;
    uint8 rom_attached;
    uint8 uart_scr;
    uint8 uart_lcr;
    uint8 mpcl_ram;
    uint8 mpcl_rom;
} N8VEM_INFO;

static N8VEM_INFO n8vem_info_data = { { 0x0, 0x8000, 0x60, 32 } };
static N8VEM_INFO *n8vem_info = &n8vem_info_data;

extern t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern uint32 PCX;
extern int32 find_unit_index (UNIT *uptr);

static t_stat n8vem_reset(DEVICE *n8vem_dev);
static t_stat n8vem_boot(int32 unitno, DEVICE *dptr);
static t_stat n8vem_attach(UNIT *uptr, CONST char *cptr);
static t_stat n8vem_detach(UNIT *uptr);

static uint8 N8VEM_Read(const uint32 Addr);
static uint8 N8VEM_Write(const uint32 Addr, uint8 cData);

static int32 n8vemdev(const int32 port, const int32 io, const int32 data);
static int32 n8vem_mem(const int32 port, const int32 io, const int32 data);

static int32 save_rom       = 0x00;     /* When set to 1, saves ROM back to file on disk at detach time */
static int32 save_ram       = 0x00;     /* When set to 1, saves RAM back to file on disk at detach time */
static int32 n8vem_pio1a    = 0x00;     /* 8255 PIO1A IN Port */
static int32 n8vem_pio1b    = 0x00;     /* 8255 PIO1B OUT Port */
static int32 n8vem_pio1c    = 0x00;     /* 8255 PIO1C IN Port */
static int32 n8vem_pio1ctrl = 0x00;     /* 8255 PIO1 Control Port */
static const char* n8vem_description(DEVICE *dptr);

#define N8VEM_ROM_SIZE  (1024 * 1024)
#define N8VEM_RAM_SIZE  (512 * 1024)

#define N8VEM_RAM_SELECT    (1 << 7)
#define N8VEM_RAM_MASK      0x0F
#define N8VEM_ROM_MASK      0x1F
#define N8VEM_ADDR_MASK     0x7FFF

static UNIT n8vem_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, N8VEM_ROM_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, N8VEM_RAM_SIZE) }
};

static REG n8vem_reg[] = {
    { HRDATAD (SAVEROM,      save_rom,               1,
               "When 1, saves the ROM back to file on disk at detach time"),    },
    { HRDATAD (SAVERAM,      save_ram,               1,
               "When 1 save the RAM back to file on disk at detach time"),      },
    { HRDATAD (PIO1A,        n8vem_pio1a,            8,
               "8255 PIO1A IN Port"),                                           },
    { HRDATAD (PIO1B,        n8vem_pio1b,            8,
               "8255 PIO1B OUT Port"),                                          },
    { HRDATAD (PIO1C,        n8vem_pio1c,            8,
               "8255 PIO1C IN Port"),                                           },
    { HRDATAD (PIO1CTRL,     n8vem_pio1ctrl,         8,
               "8255 PIO1 Control Port"),                                       },
    { NULL }
};

#define N8VEM_NAME  "Single-Board Computer"

static const char* n8vem_description(DEVICE *dptr) {
    return N8VEM_NAME;
}

static MTAB n8vem_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,  "MEMBASE",  "MEMBASE",  &set_membase, &show_membase,
        NULL, "Sets device base address"   },
    { MTAB_XTD|MTAB_VDV,    0,  "IOBASE",   "IOBASE",   &set_iobase, &show_iobase,
        NULL, "Sets device I/O address"    },
    { 0 }
};

/* Debug Flags */
static DEBTAB n8vem_dt[] = {
    { "PIO",        PIO_MSG,        "PIP activity"}     ,
    { "UART",       UART_MSG,       "UART activity"     },
    { "ROM",        ROM_MSG,        "ROM activity"      },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { NULL,         0                                   }
};

DEVICE n8vem_dev = {
    "N8VEM", n8vem_unit, n8vem_reg, n8vem_mod,
    N8VEM_MAX_DRIVES, 10, 31, 1, N8VEM_MAX_DRIVES, N8VEM_MAX_DRIVES,
    NULL, NULL, &n8vem_reset,
    &n8vem_boot, &n8vem_attach, &n8vem_detach,
    &n8vem_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    n8vem_dt, NULL, NULL, NULL, NULL, NULL, &n8vem_description
};

/* Reset routine */
static t_stat n8vem_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: Reset.\n");

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &n8vemdev, TRUE);
        sim_map_resource(pnp->mem_base, pnp->mem_size, RESOURCE_TYPE_MEMORY, &n8vem_mem, TRUE);
        free(n8vem_info->ram);
        free(n8vem_info->rom);
    } else {
        /* Connect N8VEM at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &n8vemdev, FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
        /* Connect N8VEM Memory (512K RAM, 1MB FLASH) */
        if(sim_map_resource(pnp->mem_base, pnp->mem_size, RESOURCE_TYPE_MEMORY, &n8vem_mem, FALSE) != 0) {
            sim_printf("%s: error mapping MEM resource at 0x%04x\n", __FUNCTION__, pnp->mem_base);
            return SCPE_ARG;
        }

        n8vem_info->ram = (uint8 *)calloc(1, (N8VEM_RAM_SIZE));
        n8vem_info->rom = (uint8 *)calloc(1, (N8VEM_ROM_SIZE));

        /* Clear the RAM and ROM mapping registers */
        n8vem_info->mpcl_ram = 0;
        n8vem_info->mpcl_rom = 0;
    }
    return SCPE_OK;
}

static t_stat n8vem_boot(int32 unitno, DEVICE *dptr)
{
    sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: Boot.\n");

    /* Clear the RAM and ROM mapping registers */
    n8vem_info->mpcl_ram = 0;
    n8vem_info->mpcl_rom = 0;

    /* Set the PC to 0, and go. */
    *((int32 *) sim_PC->loc) = 0;
    return SCPE_OK;
}

/* Attach routine */
static t_stat n8vem_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    int32 i = 0, rtn;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if ( r != SCPE_OK)              /* error?       */
        return r;

    /* Determine length of this disk */
    uptr->capac = sim_fsize(uptr->fileref);

    sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: Attach %s.\n", i == 0 ? "ROM" : "RAM");

    if(i == 0) { /* Attaching ROM */
        n8vem_info->rom_attached = TRUE;

        /* Erase ROM */
        memset(n8vem_info->rom, 0xFF, N8VEM_ROM_SIZE);

        if(uptr->capac > 0) {
            /* Only read in enough of the file to fill the ROM. */
            if (uptr->capac > N8VEM_ROM_SIZE)
                uptr->capac = N8VEM_ROM_SIZE;

            rtn = fread((void *)(n8vem_info->rom), uptr->capac, 1, uptr->fileref);
            sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: Reading %d bytes into ROM." " Result = %ssuccessful.\n", uptr->capac, rtn == 1 ? "" : "not ");
        }
    } else { /* attaching RAM */
        /* Erase RAM */
        memset(n8vem_info->ram, 0x00, N8VEM_RAM_SIZE);

        if(uptr->capac > 0) {
            /* Only read in enough of the file to fill the RAM. */
            if(uptr->capac > N8VEM_RAM_SIZE)
                uptr->capac = N8VEM_RAM_SIZE;

            rtn = fread((void *)(n8vem_info->ram), uptr->capac, 1, uptr->fileref);
            sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: Reading %d bytes into RAM." " Result = %ssuccessful.\n", uptr->capac, rtn == 1 ? "" : "not ");
        }
    }
    return r;
}

/* Detach routine */
static t_stat n8vem_detach(UNIT *uptr)
{
    t_stat r;
    int32 i = 0;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: Detach %s.\n", i == 0 ? "ROM" : "RAM");

    /* rewind to the beginning of the file. */
    if(sim_fseek(uptr->fileref, 0, SEEK_SET)) {
        sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: Cannot write into %s image.\n", i == 0 ? "ROM" : "RAM");
    } else if(i == 0) { /* ROM */
        /* Save the ROM back to disk if SAVEROM is set. */
        if(save_rom == 1) {
            sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: Writing %d bytes into ROM image.\n", N8VEM_ROM_SIZE);
            fwrite((void *)(n8vem_info->rom), N8VEM_ROM_SIZE, 1, uptr->fileref);
        }
    } else { /* RAM */
        /* Save the RAM back to disk if SAVERAM is set. */
        if(save_ram == 1) {
            sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: Writing %d bytes into RAM image.\n", N8VEM_RAM_SIZE);
            fwrite((void *)(n8vem_info->ram), N8VEM_RAM_SIZE, 1, uptr->fileref);
        }
    }
    r = detach_unit(uptr);  /* detach unit */

    return r;
}

/* RAM MEMORY PAGE CONFIGURATION LATCH CONTROL PORT ( IO_Y3 ) INFORMATION
 *
 *  7 6 5 4  3 2 1 0      ONLY APPLICABLE TO THE LOWER MEMORY PAGE $0000-$7FFF
 *  ^ ^ ^ ^  ^ ^ ^ ^
 *  : : : :  : : : :--0 = A15 RAM ADDRESS LINE DEFAULT IS 0
 *  : : : :  : : :----0 = A16 RAM ADDRESS LINE DEFAULT IS 0
 *  : : : :  : :------0 = A17 RAM ADDRESS LINE DEFAULT IS 0
 *  : : : :  :--------0 = A18 RAM ADDRESS LINE DEFAULT IS 0
 *  : : : :-----------0 =
 *  : : :-------------0 =
 *  : :---------------0 =
 *  :-----------------0 =
 *
 * ROM MEMORY PAGE CONFIGURATION LATCH CONTROL PORT ( IO_Y3+$04 ) INFORMATION
 *
 *  7 6 5 4  3 2 1 0      ONLY APPLICABLE TO THE LOWER MEMORY PAGE $0000-$7FFF
 *  ^ ^ ^ ^  ^ ^ ^ ^
 *  : : : :  : : : :--0 = A15 ROM ADDRESS LINE DEFAULT IS 0
 *  : : : :  : : :----0 = A16 ROM ADDRESS LINE DEFAULT IS 0
 *  : : : :  : :------0 = A17 ROM ADDRESS LINE DEFAULT IS 0
 *  : : : :  :--------0 = A18 ROM ADDRESS LINE DEFAULT IS 0
 *  : : : :-----------0 = A19 ROM ONLY ADDRESS LINE DEFAULT IS 0
 *  : : :-------------0 =
 *  : :---------------0 =
 *  :-----------------0 = ROM SELECT (0=ROM, 1=RAM) DEFAULT IS 0
 */
 static int32 n8vem_mem(const int32 Addr, const int32 write, const int32 data)
{
/*  DBG_PRINT(("N8VEM: ROM %s, Addr %04x" NLP, write ? "WR" : "RD", Addr)); */
    if(write) {
        if(n8vem_info->mpcl_rom & N8VEM_RAM_SELECT)
        {
            n8vem_info->ram[((n8vem_info->mpcl_ram & N8VEM_RAM_MASK) << 15) | (Addr & N8VEM_ADDR_MASK)] = data;
        } else {
            if(save_rom == 1) {
                n8vem_info->rom[((n8vem_info->mpcl_rom & N8VEM_ROM_MASK) << 15) | (Addr & N8VEM_ADDR_MASK)] = data;
            } else {
                sim_debug(ROM_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " WR ROM[0x%05x]: Cannot write to ROM.\n", PCX, ((n8vem_info->mpcl_rom & N8VEM_ROM_MASK) << 15) | (Addr & N8VEM_ADDR_MASK));
            }
        }
        return 0;
    } else {
        if(n8vem_info->mpcl_rom & N8VEM_RAM_SELECT)
        {
            return n8vem_info->ram[((n8vem_info->mpcl_ram & N8VEM_RAM_MASK) << 15) | (Addr & N8VEM_ADDR_MASK)];
        } else {
            return n8vem_info->rom[((n8vem_info->mpcl_rom & N8VEM_ROM_MASK) << 15) | (Addr & N8VEM_ADDR_MASK)];
        }
    }
}

static int32 n8vemdev(const int32 port, const int32 io, const int32 data)
{
/*    DBG_PRINT(("N8VEM: IO %s, Port %02x\n", io ? "WR" : "RD", port)); */
    if(io) {
        N8VEM_Write(port, data);
        return 0;
    } else {
        return(N8VEM_Read(port));
    }
}

#define N8VEM_PIO1A       0x00 /* (INPUT)  IN 1-8 */
#define N8VEM_PIO1B       0x01 /* (OUTPUT) OUT TO LEDS */
#define N8VEM_PIO1C       0x02 /* (INPUT) */
#define N8VEM_PIO1CONT    0x03 /* CONTROL BYTE PIO 82C55 */

#define N8VEM_UART_DATA   0x08
#define N8VEM_UART_RSR    0x09
#define N8VEM_UART_INTR   0x0A
#define N8VEM_UART_LCR    0x0B
#define N8VEM_UART_MCR    0x0C
#define N8VEM_UART_LSR    0x0D
#define N8VEM_UART_MSR    0x0E
#define N8VEM_UART_SCR    0x0F

#define N8VEM_MPCL_RAM    0x18 /* RAM Address control port */
#define N8VEM_MPCL_RAM1   0x19 /* RAM Address control port */
#define N8VEM_MPCL_RAM2   0x1A /* RAM Address control port */
#define N8VEM_MPCL_RAM3   0x1B /* RAM Address control port */
#define N8VEM_MPCL_ROM    0x1C /* ROM Address control port */
#define N8VEM_MPCL_ROM1   0x1D /* ROM Address control port */
#define N8VEM_MPCL_ROM2   0x1E /* ROM Address control port */
#define N8VEM_MPCL_ROM3   0x1F /* ROM Address control port */

extern int32 sio0d(const int32 port, const int32 io, const int32 data);
extern int32 sio0s(const int32 port, const int32 io, const int32 data);

static uint8 N8VEM_Read(const uint32 Addr)
{
    uint8 cData = 0xFF;

    switch(Addr & 0x1F) {
        case N8VEM_PIO1A:
            sim_debug(PIO_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " RD: PIO1A\n", PCX);
            cData = n8vem_pio1a;
            break;
        case N8VEM_PIO1B:
            sim_debug(PIO_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " RD: PIO1B\n", PCX);
            cData = n8vem_pio1b;
            break;
        case N8VEM_PIO1C:
            sim_debug(PIO_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " RD: PIO1C\n", PCX);
            cData = n8vem_pio1c;
            break;
        case N8VEM_PIO1CONT:
            sim_debug(PIO_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " RD: PIO1CTRL\n", PCX);
            cData = n8vem_pio1ctrl;
            break;
        case N8VEM_UART_LCR:
            cData = n8vem_info->uart_lcr;
            break;
        case N8VEM_UART_DATA:
        case N8VEM_UART_RSR:
        case N8VEM_UART_LSR:
        case N8VEM_UART_INTR:
        case N8VEM_UART_MCR:
        case N8VEM_UART_MSR:
            sim_debug(UART_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " RD[%02x]: UART not Implemented.\n", PCX, Addr);
            break;
        case N8VEM_UART_SCR:        /* 16550 Scratchpad, implemented so software can detect UART is present */
            cData = n8vem_info->uart_scr;
            break;
        case N8VEM_MPCL_RAM:
        case N8VEM_MPCL_RAM1:
        case N8VEM_MPCL_RAM2:
        case N8VEM_MPCL_RAM3:
            sim_debug(MPCL_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " RD: MPCL_RAM not Implemented.\n", PCX);
            break;
        case N8VEM_MPCL_ROM:
        case N8VEM_MPCL_ROM1:
        case N8VEM_MPCL_ROM2:
        case N8VEM_MPCL_ROM3:
            sim_debug(MPCL_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " RD: MPCL_ROM not Implemented.\n", PCX);
            break;
        default:
            sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " RD[%02x]: not Implemented.\n", PCX, Addr);
            break;
    }

    return (cData);

}

static uint8 N8VEM_Write(const uint32 Addr, uint8 cData)
{

    switch(Addr & 0x1F) {
        case N8VEM_PIO1A:
            sim_debug(PIO_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " WR: PIO1A=0x%02x\n", PCX, cData);
            n8vem_pio1a = cData;
            break;
        case N8VEM_PIO1B:
            sim_debug(PIO_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " WR: PIO1B=0x%02x\n", PCX, cData);
            n8vem_pio1b = cData;
            break;
        case N8VEM_PIO1C:
            sim_debug(PIO_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " WR: PIO1C=0x%02x\n", PCX, cData);
            n8vem_pio1c = cData;
            break;
        case N8VEM_PIO1CONT:
            sim_debug(PIO_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " WR: PIO1_CTRL=0x%02x\n", PCX, cData);
            n8vem_pio1ctrl = cData;
            break;
        case N8VEM_UART_LCR:
            sim_debug(UART_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " WR: UART LCR=%02x.\n", PCX, cData);
            n8vem_info->uart_lcr = cData;
            break;
        case N8VEM_UART_DATA:
        case N8VEM_UART_RSR:
        case N8VEM_UART_INTR:
        case N8VEM_UART_MCR:
        case N8VEM_UART_LSR:
        case N8VEM_UART_MSR:
            sim_debug(UART_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " WR[%02x]: UART not Implemented.\n", PCX, Addr);
            break;
        case N8VEM_UART_SCR:        /* 16550 Scratchpad, implemented so software can detect UART is present */
            n8vem_info->uart_scr = cData;
            break;
        case N8VEM_MPCL_RAM:
        case N8VEM_MPCL_RAM1:
        case N8VEM_MPCL_RAM2:
        case N8VEM_MPCL_RAM3:
            sim_debug(MPCL_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " WR: MPCL_RAM=0x%02x\n", PCX, cData);
            n8vem_info->mpcl_ram = cData;
            break;
        case N8VEM_MPCL_ROM:
        case N8VEM_MPCL_ROM1:
        case N8VEM_MPCL_ROM2:
        case N8VEM_MPCL_ROM3:
            sim_debug(MPCL_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " WR: MPCL_ROM=0x%02x\n", PCX, cData);
            n8vem_info->mpcl_rom = cData;
            break;
        default:
            sim_debug(VERBOSE_MSG, &n8vem_dev, "N8VEM: " ADDRESS_FORMAT " WR[0x%02x]=0x%02x: not Implemented.\n", PCX, Addr, cData);
            break;
    }

    return(0);
}

