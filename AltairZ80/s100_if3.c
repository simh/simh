/*************************************************************************
 *                                                                       *
 * $Id: s100_if3.c 1991 2008-07-10 16:06:12Z hharte $                    *
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
 *     CompuPro System Support 1 module for SIMH.                        *
 * Note this does not include the Boot ROM on the System Support 1 Card  *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/*#define DBG_MSG */

#include "altairz80_defs.h"
#include <time.h>

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

/* Debug flags */
#define ERROR_MSG   (1 << 0)
#define RXIRQ_MSG   (1 << 1)
#define TXIRQ_MSG   (1 << 2)
#define UART_MSG    (1 << 3)
#define USER_MSG    (1 << 4)

#define IF3_MAX_BOARDS    4

#define UNIT_V_IF3_CONNECT     (UNIT_V_UF + 1) /* Connect/Disconnect IF3 unit */
#define UNIT_IF3_CONNECT       (1 << UNIT_V_IF3_CONNECT)

#define IF3_PORT_BASE   0x300

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
} IF3_INFO;

static IF3_INFO if3_info_data = { { 0x0, 0, 0x10, 8 } };

extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern uint32 PCX;

extern int32 sio0d(const int32 port, const int32 io, const int32 data);
extern int32 sio0s(const int32 port, const int32 io, const int32 data);

static t_stat set_if3_connect(UNIT *uptr, int32 val, CONST char *cptr, void *desc);

static t_stat if3_reset(DEVICE *if3_dev);
static t_stat if3_svc (UNIT *uptr);
static uint8 IF3_Read(const uint32 Addr);
static uint8 IF3_Write(const uint32 Addr, uint8 cData);
static int32 if3dev(const int32 port, const int32 io, const int32 data);
static t_stat update_rx_tx_isr (UNIT *uptr);
static const char* if3_description(DEVICE *dptr);

static UNIT if3_unit[] = {
    { UDATA (&if3_svc, UNIT_FIX | UNIT_DISABLE | UNIT_ROABLE | UNIT_IF3_CONNECT, 0) },
    { UDATA (&if3_svc, UNIT_FIX | UNIT_DISABLE | UNIT_ROABLE, 0) },
    { UDATA (&if3_svc, UNIT_FIX | UNIT_DISABLE | UNIT_ROABLE, 0) },
    { UDATA (&if3_svc, UNIT_FIX | UNIT_DISABLE | UNIT_ROABLE, 0) }
};

static uint8 if3_user = 0;
static uint8 if3_board = 0;
static uint8 if3_rimr[IF3_MAX_BOARDS] = { 0, 0, 0, 0 };
static uint8 if3_timr[IF3_MAX_BOARDS] = { 0, 0, 0, 0 };
static uint8 if3_risr[IF3_MAX_BOARDS] = { 0, 0, 0, 0 };
static uint8 if3_tisr[IF3_MAX_BOARDS] = { 0, 0, 0, 0 };

static REG if3_reg[] = {
    { HRDATAD (USER,     if3_user,       3, "IF3 user register"), },
    { HRDATAD (BOARD,    if3_board,      2, "IF3 board register"), },
    { BRDATAD (RIMR,     &if3_rimr[0],   16, 8, 4, "IF3 RIMR register array"), },
    { BRDATAD (RISR,     &if3_risr[0],   16, 8, 4, "IF3 RISR register array"), },
    { BRDATAD (TIMR,     &if3_timr[0],   16, 8, 4, "IF3 TIMR register array"), },
    { BRDATAD (TISR,     &if3_tisr[0],   16, 8, 4, "IF3 TISR register array"), },
    { NULL }
};

#define IF3_NAME    "Compupro Interfacer 3"

static const char* if3_description(DEVICE *dptr) {
    return IF3_NAME;
}

static MTAB if3_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,               "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    { UNIT_IF3_CONNECT,     UNIT_IF3_CONNECT,"INSTALLED",  "INSTALLED",
        &set_if3_connect, NULL, NULL, "Installs board for unit " IF3_NAME "n"       },
    { UNIT_IF3_CONNECT,     0,               "UNINSTALLED","UNINSTALLED",
        &set_if3_connect, NULL, NULL, "Uninstalls board for unit " IF3_NAME "n"     },
    { 0 }
};

/* Debug Flags */
static DEBTAB if3_dt[] = {
    { "ERROR",  ERROR_MSG,      "Error messages"    },
    { "RXIRQ",  RXIRQ_MSG,      "RX IRQ messages"   },
    { "TXIRQ",  TXIRQ_MSG,      "TX IRQ messages"   },
    { "UART",   UART_MSG,       "UART messages"     },
    { "USER",   USER_MSG,       "User messages"     },
    { NULL,     0                                   }
};

DEVICE if3_dev = {
    "IF3", if3_unit, if3_reg, if3_mod,
    IF3_MAX_BOARDS, 10, 31, 1, IF3_MAX_BOARDS, IF3_MAX_BOARDS,
    NULL, NULL, &if3_reset,
    NULL, NULL, NULL,
    &if3_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    if3_dt, NULL, NULL, NULL, NULL, NULL, &if3_description
};

static t_stat set_if3_connect(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if(uptr->flags & UNIT_DISABLE) {
        sim_debug(ERROR_MSG, &if3_dev, "IF3[%d]: not enabled.\n", uptr->u3);
        return SCPE_OK;
    }

    if(val & UNIT_IF3_CONNECT) {
        sim_debug((RXIRQ_MSG|TXIRQ_MSG), &if3_dev, "IF3[%d]: IRQ polling started...\n", uptr->u3);
        sim_activate(uptr, 100000);
    } else {
        sim_debug((RXIRQ_MSG|TXIRQ_MSG), &if3_dev, "IF3[%d]: IRQ polling stopped.\n", uptr->u3);
        sim_cancel(uptr);
    }
    return (SCPE_OK);
}

/* Reset routine */
static t_stat if3_reset(DEVICE *dptr)
{
    uint8 i;

    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        for(i=0;i<IF3_MAX_BOARDS;i++)
            sim_cancel(&if3_unit[i]);

        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &if3dev, TRUE);
    } else {
        /* Connect IF3 at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &if3dev, FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }

        for(i=0;i<IF3_MAX_BOARDS;i++) {
            if3_unit[i].u3 = i; /* Store unit board ID in u3. Also guarantees that u3 < IF3_MAX_BOARDS */
            if(if3_unit[i].flags & UNIT_IF3_CONNECT) {
                sim_debug((RXIRQ_MSG|TXIRQ_MSG), &if3_dev, "IF3[%d]: IRQ polling started...\n", i);
                sim_activate(&if3_unit[i], 200000); /* start Rx/Tx interrupt polling routine */

            }
        }
    }
    return SCPE_OK;
}

static int32 if3dev(const int32 port, const int32 io, const int32 data)
{
    DBG_PRINT(("IF3: IO %s, Port %02x\n", io ? "WR" : "RD", port));
    if(io) {
        IF3_Write(port, data);
        return 0;
    } else {
        return(IF3_Read(port));
    }
}

#define IF3_UART_DATA   0x00
#define IF3_UART_STAT   0x01
#define IF3_UART_MODE   0x02
#define IF3_UART_CMD    0x03
#define IF3_TISR        0x04
#define IF3_RISR        0x05
#define IF3_RESERVED    0x06
#define IF3_USER_SEL    0x07

static uint8 IF3_Read(const uint32 Addr)
{
    uint8 cData = 0xFF;

    /* Check if board is connected */
    if(!(if3_unit[if3_board].flags & UNIT_IF3_CONNECT)) {
        sim_debug(ERROR_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " RD UART[%d] Board not connected DATA=0x%02x\n", if3_board, PCX, if3_user, cData);
        return cData;
    }

    switch(Addr & 0x07) {
        case IF3_UART_DATA:
            cData = sio0d(IF3_PORT_BASE+(if3_board*0x10)+(if3_user*2), 0, 0);
            sim_debug(UART_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " RD UART[%d] DATA=0x%02x Port=0x%03x\n", if3_board, PCX, if3_user, cData, IF3_PORT_BASE+(if3_board*0x10)+(if3_user*2));
            break;
        case IF3_UART_STAT:
            cData = sio0s(IF3_PORT_BASE+(if3_board*0x10)+1+(if3_user*2), 0, 0);
            sim_debug(UART_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " RD UART[%d] STAT=0x%02x Port=0x%03x\n", if3_board, PCX, if3_user, cData, IF3_PORT_BASE+(if3_board*0x10)+1+(if3_user*2));
            break;
        case IF3_UART_MODE:
            sim_debug(ERROR_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " RD UART MODE cannot read 0x%02x\n", if3_board, PCX, Addr);
            break;
        case IF3_UART_CMD:
            sim_debug(ERROR_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " RD UART CMD cannot read 0x%02x\n", if3_board, PCX, Addr);
            break;
        case IF3_TISR:
            update_rx_tx_isr (&if3_unit[if3_board]);
            cData = if3_tisr[if3_board];
            sim_debug(TXIRQ_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " RD UART TISR=0x%02x\n", if3_board, PCX, cData);
            break;
        case IF3_RISR:
            update_rx_tx_isr (&if3_unit[if3_board]);
            cData = if3_risr[if3_board];
            sim_debug(RXIRQ_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " RD UART RISR=0x%02x\n", if3_board,  PCX, cData);
            break;
        case IF3_RESERVED:
            sim_debug(ERROR_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " RD UART RESERVED cannot read 0x%02x\n", if3_board, PCX, Addr);
            break;
        case IF3_USER_SEL:
            sim_debug(USER_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " Cannot read UART_SEL\n", if3_board, PCX);
            break;
    }

    return (cData);

}

static uint8 IF3_Write(const uint32 Addr, uint8 cData)
{
    /* Check if board is connected for all ports except "user select" */
    if((Addr & 0x7) != IF3_USER_SEL) {
        if(!(if3_unit[if3_board].flags & UNIT_IF3_CONNECT)) {
            sim_debug(ERROR_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " WR UART[%d] Board not connected DATA=0x%02x\n", if3_board, PCX, if3_user, cData);
            return cData;
        }
    }

    switch(Addr & 0x07) {
        case IF3_UART_DATA:
            sio0d(IF3_PORT_BASE+(if3_board*0x10)+(if3_user*2), 1, cData);
            sim_debug(UART_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " WR UART[%d] DATA=0x%02x Port=0x%03x\n", if3_board, PCX, if3_user, cData, IF3_PORT_BASE+(if3_board*0x10)+(if3_user*2));
            break;
        case IF3_UART_STAT:
            sim_debug(UART_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " WR UART[%d] STAT=0x%02x\n", if3_board, PCX, if3_user, cData);
            break;
        case IF3_UART_MODE:
            sim_debug(UART_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " WR UART[%d] MODE=0x%02x\n", if3_board, PCX, if3_user, cData);
            break;
        case IF3_UART_CMD:
            sim_debug(UART_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " WR UART[%d] CMD=0x%02x\n", if3_board, PCX, if3_user, cData);
            break;
        case IF3_TISR:
            sim_debug(TXIRQ_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " WR UART TIMR=0x%02x\n", if3_board, PCX, cData);
            if3_timr[if3_board] = cData;
            break;
        case IF3_RISR:
            sim_debug(RXIRQ_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " WR UART RIMR=0x%02x\n", if3_board, PCX, cData);
            if3_rimr[if3_board] = cData;
            break;
        case IF3_RESERVED:
            sim_debug(UART_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " WR UART[%d] RESERVED=0x%02x\n", if3_board, PCX, if3_user, cData);
            break;
        case IF3_USER_SEL:
            if3_board = (cData & 0x18) >> 3; /* guarantees that if3_board < IF3_MAX_BOARDS */
            if3_user = cData & 0x7;
            sim_debug(USER_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " WR UART_SEL=0x%02x (Board=%d, Rel_User=%d, User=%d)\n", if3_board, PCX, cData, if3_board, if3_user, cData);
            break;
    }

    return(0);
}

#define SS1_VI2_INT         2   /* IF3 Rx interrupts tied to VI2 */
#define SS1_VI3_INT         3   /* IF3 Tx interrupts tied to VI3 */

#define IF3_NUM_PORTS       8   /* Number of ports per IF3 board */

extern void raise_ss1_interrupt(uint8 isr_index);

/* Unit service routine */
static t_stat if3_svc (UNIT *uptr)
{
    uint8 pending_rx_irqs;
    uint8 pending_tx_irqs;
    uint8 board = uptr->u3;

    update_rx_tx_isr(uptr);

    pending_rx_irqs = if3_risr[board] & if3_rimr[board];
    if(pending_rx_irqs) {
        sim_debug(RXIRQ_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " Rx IRQ Pending: 0x%02x\n", board, PCX, pending_rx_irqs);
        raise_ss1_interrupt(SS1_VI2_INT);
    }

    pending_tx_irqs = if3_tisr[board] & if3_timr[board];
    if(pending_tx_irqs) {
        sim_debug(TXIRQ_MSG, &if3_dev, "IF3[%d]: " ADDRESS_FORMAT " Tx IRQ Pending: 0x%02x\n", board, PCX, pending_tx_irqs);
        raise_ss1_interrupt(SS1_VI3_INT);
    }

    sim_activate(&if3_unit[board], 200000);
    return SCPE_OK;
}


static t_stat update_rx_tx_isr (UNIT *uptr)
{
    uint8 i;
    uint8 cData;
    uint8 board = uptr->u3;

    if3_risr[board] = 0;
    if3_tisr[board] = 0;

    for(i=0;i<IF3_NUM_PORTS;i++) {
        cData = sio0s(IF3_PORT_BASE+1+(board*0x10)+(i*2), 0, 0);
        if(cData & 2) { /* RX char available? */
            if3_risr[board] |= (1 << i);
        }
        if(cData & 1) { /* TX Buffer empty? */
            if3_tisr[board] |= (1 << i);
        }
    }
    return (SCPE_OK);
}
