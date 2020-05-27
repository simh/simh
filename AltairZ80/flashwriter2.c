/*************************************************************************
 *                                                                       *
 * $Id: flashwriter2.c 1941 2008-06-13 05:31:03Z hharte $                *
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
 *     Vector Graphic, Inc. FlashWriter II module for SIMH               *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/*#define DBG_MSG*/

#include "altairz80_defs.h"

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

extern int32 sio0s(const int32 port, const int32 io, const int32 data);
extern int32 sio0d(const int32 port, const int32 io, const int32 data);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);

static char ansibuf[32];

#define FW2_MAX_BOARDS          4
#define UNIT_V_FW2_VERBOSE      (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_FW2_VERBOSE        (1 << UNIT_V_FW2_VERBOSE)
#define FW2_CAPACITY            (2048)          /* FlashWriter II Memory Size               */

typedef struct {
    UNIT  *uptr;            /* UNIT pointer */
    uint8 cur_FL_Row;       /* Current Flashwriter Row */
    uint8 cur_FL_Col;       /* Current Flashwriter Column */
    uint8 FL_Row;
    uint8 FL_Col;
    uint8 reversevideo;     /* Flag set if reverse video is currently on */
    uint8 M[FW2_CAPACITY];  /* FlashWriter 2K Video Memory */
} FW2_INFO;

static FW2_INFO *fw2_info[FW2_MAX_BOARDS];
static uint8 port_map[FW2_MAX_BOARDS] = { 0x11, 0x15, 0x17, 0x19 };

static int32 fw2dev(const int32 Addr, const int32 rw, const int32 data);
static t_stat fw2_attach(UNIT *uptr, CONST char *cptr);
static t_stat fw2_detach(UNIT *uptr);
static uint8 FW2_Read(const uint32 Addr);
static uint8 FW2_Write(const uint32 Addr, uint8 cData);
static t_stat get_base_address(const char *cptr, uint32 *baseaddr);
static const char* fw2_description(DEVICE *dptr);

static UNIT fw2_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, FW2_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, FW2_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, FW2_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, FW2_CAPACITY) }
};

#define FWII_NAME   "Vector Graphic Flashwriter 2"

static const char* fw2_description(DEVICE *dptr) {
    return FWII_NAME;
}

static MTAB fw2_mod[] = {
    /* quiet, no warning messages       */
    { UNIT_FW2_VERBOSE, 0,                  "QUIET",    "QUIET", NULL, NULL, NULL,
        "No verbose messages for unit " FWII_NAME "n"   },
    /* verbose, show warning messages   */
    { UNIT_FW2_VERBOSE, UNIT_FW2_VERBOSE,   "VERBOSE",  "VERBOSE", NULL, NULL, NULL,
        "Verbose messages for unit " FWII_NAME "n" },
    { 0 }
};

DEVICE fw2_dev = {
    "FWII", fw2_unit, NULL, fw2_mod,
    FW2_MAX_BOARDS, 10, 31, 1, FW2_MAX_BOARDS, FW2_MAX_BOARDS,
    NULL, NULL, NULL,
    NULL, &fw2_attach, &fw2_detach,
    NULL, (DEV_DISABLE | DEV_DIS), 0,
    NULL, NULL, NULL, NULL, NULL, NULL, &fw2_description
};

/* Attach routine */
static t_stat fw2_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    unsigned int i = 0;
    uint32 baseaddr;
    char *tptr;

    r = get_base_address(cptr, &baseaddr);
    if(r != SCPE_OK)    /* error? */
        return r;

    DBG_PRINT(("%s\n", __FUNCTION__));

    for(i = 0; i < FW2_MAX_BOARDS; i++) {
        if(&fw2_dev.units[i] == uptr) {
            if(uptr->flags & UNIT_FW2_VERBOSE) {
                sim_printf("Attaching unit %d at %04x\n", i, baseaddr);
            }
            break;
        }
    }

    if (i == FW2_MAX_BOARDS) {
        return (SCPE_IERR);
    }

    fw2_info[i] = (FW2_INFO *)calloc(1, sizeof(FW2_INFO));
    fw2_info[i]->uptr = uptr;
    fw2_info[i]->uptr->u3 = baseaddr;

    if(sim_map_resource(baseaddr, FW2_CAPACITY, RESOURCE_TYPE_MEMORY, &fw2dev, "fw2dev", FALSE) != 0) {
        sim_printf("%s: error mapping MEM resource at 0x%04x\n", __FUNCTION__, baseaddr);
        return SCPE_ARG;
    }

    if(sim_map_resource(0x00, 1, RESOURCE_TYPE_IO, &sio0s, "sio0s", FALSE) != 0) {
        sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, 0x00);
        return SCPE_ARG;
    }

    if(sim_map_resource(0x01, 1, RESOURCE_TYPE_IO, &sio0d, "sio0d", FALSE) != 0) {
        sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, 0x01);
        return SCPE_ARG;
    }

    tptr = (char *) malloc (strlen (cptr) + 3); /* get string buf */
    if (tptr == NULL)
        return SCPE_MEM;          /* no more mem? */
    sprintf(tptr, "0x%04x", baseaddr);          /* copy base address */
    uptr->filename = tptr;                      /* save */
    uptr->flags = uptr->flags | UNIT_ATT;
    return SCPE_OK;
}


/* Detach routine */
static t_stat fw2_detach(UNIT *uptr)
{
    uint8 i;

    DBG_PRINT(("%s\n", __FUNCTION__));

    for(i = 0; i < FW2_MAX_BOARDS; i++) {
        if(&fw2_dev.units[i] == uptr) {
            break;
        }
    }

    if (i >= FW2_MAX_BOARDS)
        return SCPE_ARG;

    /* Disconnect FlashWriter2: unmap memory and I/O resources */
    sim_map_resource(fw2_info[i]->uptr->u3, FW2_CAPACITY, RESOURCE_TYPE_MEMORY, &fw2dev, "fw2dev", TRUE);
    sim_map_resource(0x00, 1, RESOURCE_TYPE_IO, &sio0s, "sio0s", TRUE);
    sim_map_resource(0x01, 1, RESOURCE_TYPE_IO, &sio0d, "sio0d", TRUE);

    if(fw2_info[i]) {
        free(fw2_info[i]);
    }

    free (uptr->filename);                  /* free base address string */
    uptr->filename = NULL;
    uptr->flags = uptr->flags & ~UNIT_ATT;  /* not attached */
    return SCPE_OK;
}

static t_stat get_base_address(const char *cptr, uint32 *baseaddr)
{
    uint32 b;
    sscanf(cptr, "%x", &b);
    if(b & (FW2_CAPACITY-1)) {
        sim_printf("FWII must be on a %d-byte boundary.\n", FW2_CAPACITY);
        return SCPE_ARG;
    }
    *baseaddr = b & ~(FW2_CAPACITY-1);
    return SCPE_OK;
}

extern int32 getBankSelect(void);

/* This is the main entry point into the Flashwriter2 emulation. */
static int32 fw2dev(const int32 Addr, const int32 rw, const int32 data)
{
    int32 bank = getBankSelect();
    if(bank == 0) {
        if(rw == 0) { /* Read */
            return(FW2_Read(Addr));
        } else {    /* Write */
            return(FW2_Write(Addr, data));
        }
    } else
        return 0xff;
}


static uint8 FW2_Write(const uint32 Addr, uint8 Value)
{
    FW2_INFO *fw2 = NULL;
    uint8 FL_Row;
    uint8 FL_Col;
    uint32 baseaddr = 0;
    uint8 i;
    uint8 outchar;
    uint8 port;

    for(i = 0; i < FW2_MAX_BOARDS; i++) {
        if(fw2_info[i] != NULL) {
            baseaddr = fw2_info[i]->uptr->u3;
            if((Addr >= baseaddr) && (Addr < (baseaddr + FW2_CAPACITY))) {
                break;
            }
        }
    }

    if(i == FW2_MAX_BOARDS) {
        return 0;
    }

    fw2 = fw2_info[i];
    port = port_map[i];

    fw2->M[Addr - baseaddr] = Value;

    /* Only print if it is in the visible part of the Flashwriter memory */
    if((Addr >= baseaddr) && (Addr < (baseaddr + (80 * 24)))) {
        FL_Col = ((Addr-baseaddr) % 80) + 1;
        FL_Row = ((Addr-baseaddr) / 80) + 1;

        if(Value & 0x80) { /* reverse video */
            if(fw2->reversevideo == 0) {
                fw2->reversevideo = 1;
                sprintf(ansibuf, "\x1b[07m");
                for(i=0;i<strlen(ansibuf);i++) {
                    sio0d(port, 1, ansibuf[i]);
                }
            }
        } else {
            if(fw2->reversevideo == 1) {
                fw2->reversevideo = 0;
                sprintf(ansibuf, "\x1b[00m");
                for(i=0;i<strlen(ansibuf);i++) {
                    sio0d(port, 1, ansibuf[i]);
                }
            }
        }

        outchar = Value & 0x7F;
        if(outchar < ' ') {
            outchar = 'O';
        }
        if(outchar == 0x7F) { /* this is supposed to be a square Block character on FW2 */
            outchar = 'X';
        }

        if((fw2->cur_FL_Row == FL_Row) && (FL_Col == fw2->cur_FL_Col + 1)) {
            sio0d(port, 1, outchar);
        } else {
            /* ESC[#;#H */
            sprintf(ansibuf, "\x1b[%d;%dH%c", FL_Row, FL_Col, outchar);
            for(i=0;i<strlen(ansibuf);i++) {
                sio0d(port, 1, ansibuf[i]);
            }
        }
        fw2->cur_FL_Col = FL_Col;
        fw2->cur_FL_Row = FL_Row;
    }

    return(1);
}


static uint8 FW2_Read(const uint32 Addr)
{
    uint32 baseaddr = 0;
    uint8 i;

    for(i = 0; i < FW2_MAX_BOARDS; i++) {
        if(fw2_info[i] != NULL) {
            baseaddr = fw2_info[i]->uptr->u3;
            if((Addr >= baseaddr) && (Addr < (baseaddr + FW2_CAPACITY))) {
                break;
            }
        }
    }

    if(i == FW2_MAX_BOARDS) {
        return 0xFF;
    }

    return(fw2_info[i]->M[Addr - baseaddr]);
}
