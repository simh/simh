/* pdp11_kg.c - Communications Arithmetic Option KG11-A

   Copyright (c) 2007-2008, John A. Dundas III

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

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   kg           KG11-A Communications Arithmetic Option (M7251)

   08-Jan-08    JAD     First public release integrated with SIMH V3.7-3.
   09-Dec-07    JAD     SIMH-style debugging.
                        Finished validating against real hardware.
                        Support for up to 8 units, the maximum.
                        Keep all module data in the UNIT structure.
                        Clean up bit and mask definitions.
   01-Dec-07    JAD     Now work on the corner cases that the
                        diagnostic does not check.
                        CLR does not clear the QUO bit.
                        Correct SR write logic.
   29-Nov-07    JAD     Original implementation and testing based on
                        an idea from 07-Jul-03.  Passes the ZKGAB0
                        diagnostic.

   Information necessary to create this simulation was gathered from
   a number of sources including:

   KG11-A Exclusive-OR and CRC block check manual, DEC-11-HKGAA-B-D
    <http://www.computer.museum.uq.edu.au/pdf/DEC-11-HKGAA-B-D%20KG11-A%20Exclusive-OR%20and%20CRC%20Block%20Check%20Manual.pdf>
   Maintenance print set
    <http://bitsavers.org/pdf/dec/unibus/KG11A_EngrDrws.pdf>
   A Painless Guide to CRC Error Detection Algorithms, Ross N. Williams
    <http://www.ross.net/crc/download/crc_v3.txt">

   The original PDP-11 instruction set, as implemented in the /20,
   /15, /10, and /5, did not include XOR.  [One of the differences
   tables incorrectly indicates the /04 does not implement this
   instruction.]  This device implements XOR, XORB, and a variety of
   CRCs.

   The maintenance prints indicate that the device was probably available
   starting in late 1971.  May need to check further.  The first edition
   of the manual was May 1972.

   The device was still sold at least as late as mid-1982 according
   to the PDP-11 Systems and Options Summary.  RSTS/E included support
   for up to 8 units in support of the 2780 emulation or for use with
   DP11, DU11, or DUP11.  The device appears to have been retired by
   1983-03, and possibly earlier.

   I/O Page Registers

   SR      7707x0  (read-write)    status
   BCC     7707x2  (read-only)     BCC (block check character)
   DR      7707x4  (write-only)    data

   Vector: none

   Priority: none

   The KG11-A is a programmed-I/O, non-interrupting device.  Therefore
   no vector or bus request level are necessary.  It is a Unibus device
   but since it does not address memory itself (it only presents
   registers in the I/O page) it is compatible with extended Unibus
   machines (22-bit) as well as traditional Unibus.

   Implements 5 error detection codes:
        LRC-8
        LRC-16
        CRC-12
        CRC-16
        CRC-CCITT
*/

#if !defined (VM_PDP11)
#error "KG11 is not supported!"
#endif
#include "pdp11_defs.h"

extern FILE *sim_deb;
extern REG cpu_reg[];
extern int32 R[];

#ifndef KG_UNITS
#define KG_UNITS        (8)
#endif

/* Control and Status Register */

#define KGSR_V_QUO      (8)                             /* RO */
#define KGSR_V_DONE     (7)                             /* RO */
#define KGSR_V_SEN      (6)                             /* R/W shift enable */
#define KGSR_V_STEP     (5)                             /* W */
#define KGSR_V_CLR      (4)                             /* W */
#define KGSR_V_DDB      (3)                             /* R/W double data byte */
#define KGSR_V_CRCIC    (2)                             /* R/W */
#define KGSR_V_LRC      (1)                             /* R/W */
#define KGSR_V_16       (0)                             /* R/W */

#define KGSR_M_QUO      (1u << KGSR_V_QUO)
#define KGSR_M_DONE     (1u << KGSR_V_DONE)
#define KGSR_M_SEN      (1u << KGSR_V_SEN)
#define KGSR_M_STEP     (1u << KGSR_V_STEP)
#define KGSR_M_CLR      (1u << KGSR_V_CLR)
#define KGSR_M_DDB      (1u << KGSR_V_DDB)
#define KGSR_M_CRCIC    (1u << KGSR_V_CRCIC)
#define KGSR_M_LRC      (1u << KGSR_V_LRC)
#define KGSR_M_16       (1u << KGSR_V_16)

#define KG_SR_RDMASK    (KGSR_M_QUO | KGSR_M_DONE | KGSR_M_SEN | KGSR_M_DDB | \
                         KGSR_M_CRCIC | KGSR_M_LRC | KGSR_M_16)
#define KG_SR_WRMASK    (KGSR_M_SEN | KGSR_M_DDB | KGSR_M_CRCIC | \
                         KGSR_M_LRC | KGSR_M_16)

#define KG_SR_POLYMASK  (KGSR_M_CRCIC|KGSR_M_LRC|KGSR_M_16)

/* Unit structure redefinitions */
#define SR              u3
#define BCC             u4
#define DR              u5
#define PULSCNT         u6

#define POLY_LRC8       (0x0008)
#define POLY_LRC16      (0x0080)
#define POLY_CRC12      (0x0f01)
#define POLY_CRC16      (0xa001)
#define POLY_CCITT      (0x8408)

static const struct {
    uint16              poly;
    uint16              pulses;
    const char * const  name;
} config[] = {
                                                        /* DDB=0 */
        { POLY_CRC12,   6, "CRC-12" },
        { POLY_CRC16,   8, "CRC-16" },
        { POLY_LRC8,    8, "LRC-8" },
        { POLY_LRC16,   8, "LRC-16" },
        { 0,            0, "undefined" },
        { POLY_CCITT,   8, "CRC-CCITT" },
        { 0,            0, "undefined" },
        { 0,            0, "undefined" },
                                                        /* DDB=1 */
        { POLY_CRC12,   12, "CRC-12" },
        { POLY_CRC16,   16, "CRC-16" },
        { POLY_LRC8,    16, "LRC-8" },
        { POLY_LRC16,   16, "LRC-16" },
        { 0,            0, "undefined" },
        { POLY_CCITT,   16, "CRC-CCITT" },
        { 0,            0, "undefined" },
        { 0,            0, "undefined" }
};

/* Forward declarations */

static t_stat kg_rd (int32 *, int32, int32);
static t_stat kg_wr (int32, int32, int32);
static t_stat kg_reset (DEVICE *);
static void do_poly (int, t_bool);
static t_stat set_units (UNIT *, int32, char *, void *);

/* 16-bit rotate right */

#define ROR(n,v)        (((v >> n) & DMASK) | (v << (16 - n)) & DMASK)

/* 8-bit rotate right */

#define RORB(n,v)       (((v & 0377) >> n) | ((v << (8 - n)) & 0377))

/* KG data structures

   kg_dib       KG PDP-11 device information block
   kg_unit      KG unit descriptor
   kg_reg       KG register list
   kg_mod       KG modifiers table
   kg_debug     KG debug names table
   kg_dev       KG device descriptor
*/

static DIB kg_dib = {
    IOBA_KG,
    (IOLN_KG + 2) * KG_UNITS,
    &kg_rd,
    &kg_wr,
    0, 0, 0, { NULL }
};

static UNIT kg_unit[] = {
    { UDATA (NULL, 0, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_DIS, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_DIS, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_DIS, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_DIS, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_DIS, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_DIS, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_DIS, 0) },
};

static const REG kg_reg[] = {
    { ORDATA (SR0, kg_unit[0].SR, 16) },
    { ORDATA (SR1, kg_unit[1].SR, 16) },
    { ORDATA (SR2, kg_unit[2].SR, 16) },
    { ORDATA (SR3, kg_unit[3].SR, 16) },
    { ORDATA (SR4, kg_unit[4].SR, 16) },
    { ORDATA (SR5, kg_unit[5].SR, 16) },
    { ORDATA (SR6, kg_unit[6].SR, 16) },
    { ORDATA (SR7, kg_unit[7].SR, 16) },
    { ORDATA (BCC0, kg_unit[0].BCC, 16) },
    { ORDATA (BCC1, kg_unit[1].BCC, 16) },
    { ORDATA (BCC2, kg_unit[2].BCC, 16) },
    { ORDATA (BCC3, kg_unit[3].BCC, 16) },
    { ORDATA (BCC4, kg_unit[4].BCC, 16) },
    { ORDATA (BCC5, kg_unit[5].BCC, 16) },
    { ORDATA (BCC6, kg_unit[6].BCC, 16) },
    { ORDATA (BCC7, kg_unit[7].BCC, 16) },
    { ORDATA (DR0, kg_unit[0].DR, 16) },
    { ORDATA (DR1, kg_unit[1].DR, 16) },
    { ORDATA (DR2, kg_unit[2].DR, 16) },
    { ORDATA (DR3, kg_unit[3].DR, 16) },
    { ORDATA (DR4, kg_unit[4].DR, 16) },
    { ORDATA (DR5, kg_unit[5].DR, 16) },
    { ORDATA (DR6, kg_unit[6].DR, 16) },
    { ORDATA (DR7, kg_unit[7].DR, 16) },
    { ORDATA (PULSCNT0, kg_unit[0].PULSCNT, 16) },
    { ORDATA (PULSCNT1, kg_unit[1].PULSCNT, 16) },
    { ORDATA (PULSCNT2, kg_unit[2].PULSCNT, 16) },
    { ORDATA (PULSCNT3, kg_unit[3].PULSCNT, 16) },
    { ORDATA (PULSCNT4, kg_unit[4].PULSCNT, 16) },
    { ORDATA (PULSCNT5, kg_unit[5].PULSCNT, 16) },
    { ORDATA (PULSCNT6, kg_unit[6].PULSCNT, 16) },
    { ORDATA (PULSCNT7, kg_unit[7].PULSCNT, 16) },
    { ORDATA (DEVADDR, kg_dib.ba, 32), REG_HRO },
    { NULL }
};

static const MTAB kg_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL, NULL, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "UNITS=0..8", &set_units, NULL, NULL },
    { 0 }
};

#define DBG_REG         (01)
#define DBG_POLY        (02)
#define DBG_CYCLE       (04)

static const DEBTAB kg_debug[] = {
    {"REG",   DBG_REG},
    {"POLY",  DBG_POLY},
    {"CYCLE", DBG_CYCLE},
    {0},
};

DEVICE kg_dev = {
    "KG", (UNIT *) &kg_unit, (REG *) kg_reg, (MTAB *) kg_mod,
    KG_UNITS, 8, 16, 2, 8, 16,
    NULL,                                               /* examine */
    NULL,                                               /* deposit */
    &kg_reset,                                          /* reset */
    NULL,                                               /* boot */
    NULL,                                               /* attach */
    NULL,                                               /* detach */
    &kg_dib,
    DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG,
    0,                                                  /* debug control */
    (DEBTAB *) &kg_debug,                               /* debug flags */
    NULL,                                               /* memory size chage */
    NULL                                                /* logical name */
};
                                                       /* KG I/O address routines */

static t_stat kg_rd (int32 *data, int32 PA, int32 access)
{
    int unit = (PA >> 3) & 07;

    if ((unit >= KG_UNITS) || (kg_unit[unit].flags & UNIT_DIS))
        return (SCPE_NXM);
    switch ((PA >> 1) & 03) {

        case 00:                                        /* SR */
            if (DEBUG_PRI(kg_dev, DBG_REG))
                fprintf (sim_deb, ">>KG%d: rd SR %06o, PC %06o\n",
                         unit, kg_unit[unit].SR, PC);
            *data = kg_unit[unit].SR & KG_SR_RDMASK;
            break;

        case 01:                                        /* BCC */
            if (DEBUG_PRI(kg_dev, DBG_REG))
                fprintf (sim_deb, ">>KG%d rd BCC %06o, PC %06o\n",
                         unit, kg_unit[unit].BCC, PC);
            *data = kg_unit[unit].BCC & DMASK;
            break;

        case 02:                                        /* DR */
            break;

        default:
            break;
    }
    return (SCPE_OK);
}

static t_stat kg_wr (int32 data, int32 PA, int32 access)
{
    int setup;
    int unit = (PA >> 3) & 07;

    if ((unit >= KG_UNITS) || (kg_unit[unit].flags & UNIT_DIS))
        return (SCPE_NXM);
    switch ((PA >> 1) & 03) {

        case 00:                                        /* SR */
            if (access == WRITEB)
                data = (PA & 1) ?
                    (kg_unit[unit].SR & 0377) | (data << 8) :
                    (kg_unit[unit].SR & ~0377) | data;
            if (DEBUG_PRI(kg_dev, DBG_REG))
                fprintf (sim_deb, ">>KG%d: wr SR %06o, PC %06o\n",
                         unit, data, PC);
            if (data & KGSR_M_CLR) {
                kg_unit[unit].PULSCNT = 0;              /* not sure about this */
                kg_unit[unit].BCC = 0;
                kg_unit[unit].SR |= KGSR_M_DONE;
            }
            setup = (kg_unit[unit].SR & 017) ^ (data & 017);
            kg_unit[unit].SR = (kg_unit[unit].SR & ~KG_SR_WRMASK) |
                (data & KG_SR_WRMASK);
                                                        /* if  low 4b changed, reset C1 & C2 */
            if (setup) {
                kg_unit[unit].PULSCNT = 0;
                if (DEBUG_PRI(kg_dev, DBG_POLY))
                    fprintf (sim_deb, ">>KG%d poly %s %d\n",
                             unit, config[data & 017].name, config[data & 017].pulses);
            }
            if (data & KGSR_M_SEN)
                break;
            if (data & KGSR_M_STEP) {
                do_poly (unit, TRUE);
                break;
            }
            break;

        case 01:                                        /* BCC */
            break;                                      /* ignored */

        case 02:                                        /* DR */
            if (access == WRITEB)
                data = (PA & 1) ?
                    (kg_unit[unit].DR & 0377) | (data << 8) :
                    (kg_unit[unit].DR & ~0377) | data;
            kg_unit[unit].DR = data & DMASK;
            if (DEBUG_PRI(kg_dev, DBG_REG))
                fprintf (sim_deb, ">>KG%d: wr DR %06o, data %06o, PC %06o\n",
                         unit, kg_unit[unit].DR, data, PC);
            kg_unit[unit].SR &= ~KGSR_M_DONE;

/* In a typical device, this is normally where we would use sim_activate()
   to initiate an I/O to be completed later.  The KG is a little
   different.  When it was first introduced, it's computation operation
   completed before another instruction could execute (on early PDP-11s),
   and software often took "advantage" of this fact by not bothering
   to check the status of the DONE bit.  In reality, the execution
   time of the polynomial is dependent upon the width of the poly; if
   8 bits 1us, if 16 bits, 2us.  Since known existing software will
   break if we actually defer the computation, it is performed immediately
   instead.  However this could easily be made into a run-time option,
   if there were software to validate correct operation. */

            if (kg_unit[unit].SR & KGSR_M_SEN)
                do_poly (unit, FALSE);
            break;

        default:
            break;
    }
    return (SCPE_OK);
}

/* KG reset */

static t_stat kg_reset (DEVICE *dptr)
{
    int i;

    if (DEBUG_PRI(kg_dev, DBG_REG))
        fprintf (sim_deb, ">>KG: reset PC %06o\n", PC);
    for (i = 0; i < KG_UNITS; i++) {
        kg_unit[i].SR = KGSR_M_DONE;
        kg_unit[i].BCC = 0;
        kg_unit[i].PULSCNT = 0;
    }
    return (SCPE_OK);
}

static void cycleOneBit (int unit)
{
    int quo;

    if (DEBUG_PRI(kg_dev, DBG_CYCLE))
        fprintf (sim_deb, ">>KG%d: cycle s BCC %06o DR %06o\n",
           unit, kg_unit[unit].BCC, kg_unit[unit].DR);
    if (kg_unit[unit].SR & KGSR_M_DONE)
        return;
    if ((kg_unit[unit].SR & KG_SR_POLYMASK) == 0)
        kg_unit[unit].BCC = (kg_unit[unit].BCC & 077) |
                            ((kg_unit[unit].BCC >> 2) & 07700);
    kg_unit[unit].SR &= ~KGSR_M_QUO;
    quo = (kg_unit[unit].BCC & 01) ^ (kg_unit[unit].DR & 01);
    kg_unit[unit].BCC = (kg_unit[unit].BCC & ~01) | quo;
    if (kg_unit[unit].SR & KGSR_M_LRC)
        kg_unit[unit].BCC = (kg_unit[unit].SR & KGSR_M_16) ?
            ROR(1, kg_unit[unit].BCC) :
            RORB(1, kg_unit[unit].BCC);
    else
        kg_unit[unit].BCC = (kg_unit[unit].BCC & 01) ?
            (kg_unit[unit].BCC >> 1) ^ config[kg_unit[unit].SR & 07].poly :
            kg_unit[unit].BCC >> 1;
    kg_unit[unit].DR >>= 1;
    kg_unit[unit].SR |= quo << KGSR_V_QUO;
    if ((kg_unit[unit].SR & KG_SR_POLYMASK) == 0)
        kg_unit[unit].BCC = (kg_unit[unit].BCC & 077) |
                            ((kg_unit[unit].BCC & 07700) << 2);
    kg_unit[unit].PULSCNT++;
    if (kg_unit[unit].PULSCNT >= config[kg_unit[unit].SR & 017].pulses)
        kg_unit[unit].SR |= KGSR_M_DONE;
    if (DEBUG_PRI(kg_dev, DBG_CYCLE))
        fprintf (sim_deb, ">>KG%d: cycle e BCC %06o DR %06o\n",
            unit, kg_unit[unit].BCC, kg_unit[unit].DR);
}

static void do_poly (int unit, t_bool step)
{
    if (kg_unit[unit].SR & KGSR_M_DONE)
        return;
    if (step)
        cycleOneBit (unit);
    else {
        while (!(kg_unit[unit].SR & KGSR_M_DONE))
            cycleOneBit (unit);
    }
}

static t_stat set_units (UNIT *u, int32 val, char *s, void *desc)
{
    int32       i, units;
    t_stat      stat;

    if (s == NULL)
        return (SCPE_ARG);
    units = get_uint (s, 10, KG_UNITS, &stat);
    if (stat != SCPE_OK)
        return (stat);
    for (i = 0; i < KG_UNITS; i++) {
        if (i < units)
            kg_unit[i].flags &= ~UNIT_DIS;
        else
            kg_unit[i].flags |= UNIT_DIS;
    }
    kg_dev.numunits = units;
    return (SCPE_OK);
}
