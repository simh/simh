/* hp2100_di.h: HP 12821A HP-IB Disc Interface simulator definitions

   Copyright (c) 2010-2016, J. David Bryan

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   DI           12821A Disc Interface

   13-May-16    JDB     Modified for revised SCP API function parameter types
   14-Feb-12    JDB     First release
   16-Nov-10    JDB     Created DI common definitions file


   This file defines the interface between HP-IB device simulators and the
   12821A Disc Interface simulator.  It must be included by the device-specific
   modules (hp2100_di_da.c, etc.).


   Implementation notes:

    1. Three CARD_ID values are defined, corresponding to the Amigo disc (DA),
       CS/80 disc (DC), and Amigo mag tape (MA) simulators.  At first release,
       only the DA device is implemented.  However, as the 12821A diagnostic
       requires two cards to test I/O fully, a dummy DC device is provided by
       the DA simulator.  It is enabled only when the DA card is configured for
       diagnostic mode.  This dummy device should be removed when either the DC
       or MA device is implemented.
*/



/* Program constants */

#define FIFO_SIZE       16                              /* FIFO depth */

typedef enum {
    da, dc, ma,                                         /* card IDs */
    first_card = da,                                    /* first card ID */
    last_card  = ma,                                    /* last card ID */
    card_count                                          /* count of card IDs */
    } CARD_ID;


/* Device flags and accessors (bits 7-0 are reserved for disc/tape flags) */

#define DEV_V_BUSADR    (DEV_V_UF +  8)                 /* bits 10-8: interface HP-IB address */
#define DEV_V_DIAG      (DEV_V_UF + 11)                 /* bit 11: diagnostic mode */
#define DEV_V_W1        (DEV_V_UF + 12)                 /* bit 12: DCPC pacing jumper */

#define DEV_M_BUSADR    07                              /* bus address mask */

#define DEV_BUSADR      (DEV_M_BUSADR << DEV_V_BUSADR)
#define DEV_DIAG        (1 << DEV_V_DIAG)
#define DEV_W1          (1 << DEV_V_W1)

#define GET_DIADR(f)    (((f) >> DEV_V_BUSADR) & DEV_M_BUSADR)
#define SET_DIADR(f)    (((f) & DEV_M_BUSADR) << DEV_V_BUSADR)


/* Unit flags and accessors (bits 7-0 are reserved for disc/tape flags) */

#define UNIT_V_BUSADR   (UNIT_V_UF + 8)                 /* bits 10-8: unit HP-IB address */

#define UNIT_M_BUSADR   07                              /* bus address mask */

#define UNIT_BUSADR     (UNIT_M_BUSADR << UNIT_V_BUSADR)

#define GET_BUSADR(f)   (((f) >> UNIT_V_BUSADR) & UNIT_M_BUSADR)
#define SET_BUSADR(f)   (((f) & UNIT_M_BUSADR) << UNIT_V_BUSADR)


/* Debug flags */

#define DEB_CPU         (1 << 0)                        /* words received from and sent to the CPU */
#define DEB_CMDS        (1 << 1)                        /* interface commands received from the CPU */
#define DEB_BUF         (1 << 2)                        /* data read from and written to the card FIFO */
#define DEB_XFER        (1 << 3)                        /* data received and transmitted via HP-IB */
#define DEB_RWSC        (1 << 4)                        /* device read/write/status/control commands */
#define DEB_SERV        (1 << 5)                        /* unit service scheduling calls */


/* HP-IB control line state bit flags.

   NOTE that these flags align with the corresponding flags in the DI status
   register, so don't change the numerical values!
*/

#define BUS_ATN         0001                            /* attention */
#define BUS_EOI         0002                            /* end or identify */
#define BUS_DAV         0004                            /* data available */
#define BUS_NRFD        0010                            /* not ready for data */
#define BUS_NDAC        0020                            /* not data accepted */
#define BUS_REN         0040                            /* remote enable */
#define BUS_IFC         0100                            /* interface clear */
#define BUS_SRQ         0200                            /* service request */

#define BUS_PPOLL       (BUS_ATN | BUS_EOI)             /* parallel poll */

/* HP-IB data */

#define BUS_ADDRESS     0037                            /* bus address mask */
#define BUS_GROUP       0140                            /* bus group mask */
#define BUS_COMMAND     0160                            /* bus command type mask */
#define BUS_DATA        0177                            /* bus data mask */
#define BUS_PARITY      0200                            /* bus parity mask */

#define BUS_PCG         0000                            /* primary command group */
#define BUS_LAG         0040                            /* listen address group */
#define BUS_TAG         0100                            /* talk address group */
#define BUS_SCG         0140                            /* secondary command group */

#define BUS_UCG         0020                            /* universal command group */
#define BUS_ACG         0000                            /* addressed command group */

#define BUS_UNADDRESS   0037                            /* unlisten and untalk addresses */

#define PPR(a)          (uint8) (1 << (7 - (a)))        /* parallel poll response */


/* Byte accessors */

#define BYTE_SHIFT      8                               /* byte shift count */
#define UPPER_BYTE      0177400                         /* high-order byte mask */
#define LOWER_BYTE      0000377                         /* low-order byte mask */

#define GET_UPPER(w)    (uint8) (((w) & UPPER_BYTE) >> BYTE_SHIFT)
#define GET_LOWER(w)    (uint8) ((w) & LOWER_BYTE)

#define SET_UPPER(b)    (uint16) ((b) << BYTE_SHIFT)
#define SET_LOWER(b)    (uint16) (b)
#define SET_BOTH(b)     (SET_UPPER (b) | SET_LOWER (b))

typedef enum {
    upper,                                              /* upper byte selected */
    lower                                               /* lower byte selected */
    } SELECTOR;


/* Per-card state variables */

typedef struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    FLIP_FLOP srq;                                      /* SRQ flip-flop */
    FLIP_FLOP edt;                                      /* EDT flip-flop */
    FLIP_FLOP eor;                                      /* EOR flip-flop */
    SELECTOR  ibp;                                      /* input byte pointer selector */
    SELECTOR  obp;                                      /* output byte pointer selector */

    uint16    cntl_register;                            /* control word register */
    uint16    status_register;                          /* status word register */
    uint16    input_data_register;                      /* input data register */

    uint32    fifo [FIFO_SIZE];                         /* FIFO buffer */
    uint32    fifo_count;                               /* FIFO occupancy counter */
    REG      *fifo_reg;                                 /* FIFO register pointer */

    uint32    acceptors;                                /* unit bitmap of the bus acceptors */
    uint32    listeners;                                /* unit bitmap of the bus listeners */
    uint32    talker;                                   /* unit bitmap of the bus talker */

    uint8     bus_cntl;                                 /* HP-IB bus control state (ATN, EOI, etc.) */
    uint8     poll_response;                            /* address bitmap of parallel poll responses */

    double    ifc_timer;                                /* 100 microsecond IFC timer */
    } DI_STATE;


/* Disc interface VM global register definitions.

   These definitions should be included before any device-specific registers.


   Implementation notes:

    1. The TMR register is included to ensure that the IFC timer is saved by a
       SAVE command.  It is declared as a hidden, read-only byte array of a size
       compatible with a double-precision floating-point value, as there is no
       appropriate macro for the double type.
*/

#define DI_REGS(dev)    \
    { ORDATA (CWR, di [dev].cntl_register,       16), REG_FIT },                   \
    { ORDATA (SWR, di [dev].status_register,     16), REG_FIT },                   \
    { ORDATA (IDR, di [dev].input_data_register, 16), REG_FIT },                   \
                                                                                   \
    { DRDATA (FCNT, di [dev].fifo_count, 5) },                                     \
    { BRDATA (FIFO, di [dev].fifo, 8, 20, FIFO_SIZE), REG_CIRC },                  \
                                                                                   \
    { GRDATA (ACPT,   di [dev].acceptors,     2, 4, 0) },                          \
    { GRDATA (LSTN,   di [dev].listeners,     2, 4, 0) },                          \
    { GRDATA (TALK,   di [dev].talker,        2, 4, 0) },                          \
    { GRDATA (PPR,    di [dev].poll_response, 2, 8, 0), REG_FIT },                 \
    { GRDATA (BUSCTL, di [dev].bus_cntl,      2, 8, 0), REG_FIT },                 \
                                                                                   \
    { FLDATA (CTL, di [dev].control, 0) },                                         \
    { FLDATA (FLG, di [dev].flag,    0) },                                         \
    { FLDATA (FBF, di [dev].flagbuf, 0) },                                         \
    { FLDATA (SRQ, di [dev].srq,     0) },                                         \
    { FLDATA (EDT, di [dev].edt,     0) },                                         \
    { FLDATA (EOR, di [dev].eor,     0) },                                         \
                                                                                   \
    { BRDATA (TMR, &di [dev].ifc_timer, 10, CHAR_BIT, sizeof (double)), REG_HRO }, \
                                                                                   \
    { ORDATA (SC, dev##_dib.select_code, 6), REG_HRO }


/* Disc interface VM global modifier definitions.

   These definitions should be included before any device-specific modifiers.
*/

#define DI_MODS(dev)    \
    { MTAB_XTD | MTAB_VDV,             1, "ADDRESS", "ADDRESS", &di_set_address, &di_show_address, &dev }, \
                                                                                                           \
    { MTAB_XTD | MTAB_VDV,             1, NULL,      "DIAG",    &di_set_cable,   NULL,             &dev }, \
    { MTAB_XTD | MTAB_VDV,             0, NULL,      "HPIB",    &di_set_cable,   NULL,             &dev }, \
    { MTAB_XTD | MTAB_VDV,             0, "CABLE",   NULL,      NULL,            &di_show_cable,   &dev }, \
                                                                                                           \
    { MTAB_XTD | MTAB_VDV,             0, "SC",      "SC",      &hp_setsc,       &hp_showsc,       &dev }, \
    { MTAB_XTD | MTAB_VDV | MTAB_NMO,  0, "DEVNO",   "DEVNO",   &hp_setdev,      &hp_showdev,      &dev }, \
                                                                                                           \
    { MTAB_XTD | MTAB_VUN,             0, "BUS",     "BUS",     &di_set_address, &di_show_address, &dev }


/* Disc interface global bus routine definitions */

typedef t_bool ACCEPTOR  (uint32  unit, uint8  data);
typedef void   RESPONDER (CARD_ID card, uint32 unit, uint8 new_cntl);


/* Disc interface global variables */

extern DI_STATE di [];
extern DEBTAB   di_deb [];


/* Disc interface global VM routines */

extern IOHANDLER di_io;
extern t_stat    di_reset (DEVICE *dptr);

/* Disc interface global SCP routines */

extern t_stat di_set_address (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
extern t_stat di_set_cable   (UNIT *uptr, int32 value, CONST char *cptr, void *desc);

extern t_stat di_show_address (FILE *st, UNIT *uptr, int32 value, CONST void *desc);
extern t_stat di_show_cable   (FILE *st, UNIT *uptr, int32 value, CONST void *desc);

/* Disc interface global bus routines */

extern t_bool di_bus_source    (CARD_ID card, uint8  data);
extern void   di_bus_control   (CARD_ID card, uint32 unit, uint8 assert, uint8 deny);
extern void   di_poll_response (CARD_ID card, uint32 unit, FLIP_FLOP response);


/* Amigo disc global VM routines */

extern t_stat da_service (UNIT  *uptr);
extern t_stat da_boot    (int32 unitno, DEVICE *dptr);

/* Amigo disc global bus routines */

extern ACCEPTOR  da_bus_accept;
extern RESPONDER da_bus_respond;


/* Amigo mag tape global VM routines */

extern t_stat ma_service (UNIT  *uptr);
extern t_stat ma_boot    (int32 unitno, DEVICE *dptr);

/* Amigo mag tape global SCP routines */

extern t_stat ma_set_timing  (UNIT *uptr, int32 val,   CONST char *cptr, void *desc);
extern t_stat ma_show_timing (FILE *st,   UNIT  *uptr, int32 val,        CONST void *desc);

/* Amigo mag tape global bus routines */

extern ACCEPTOR  ma_bus_accept;
extern RESPONDER ma_bus_respond;
