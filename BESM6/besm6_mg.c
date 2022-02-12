/*
 * BESM-6 magnetic tape device (formatted)
 *
 * Copyright (c) 2009, Serge Vakulenko
 * Copyright (c) 2009-2020, Leonid Broukhis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * SERGE VAKULENKO OR LEONID BROUKHIS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.

 * Except as contained in this notice, the name of Leonid Broukhis or
 * Serge Vakulenko shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from Leonid Broukhis and Serge Vakulenko.
 */
#include "besm6_defs.h"
#include <ctype.h>
#include "sim_tape.h"
/*
 * I/O command bits
 */
#define MG_BLOCK         0740000000   /* RAM block number - 27-24 рр */
#define MG_READ_SYSDATA   004000000   /* control words only */
#define MG_READ           000400000   /* reading to RAM flag */
#define MG_PAGE           000370000   /* номер страницы памяти */
#define MG_UNIT           000001600   /* номер устройства */
/*
 * Tape movement bits
 */
#define MG_CLEARINTR      040000000
#define MG_BACK           000000002   /* 0 - forward, 1 - backward */
#define MG_MOVE           000000001   /* start moving the tape */

#define MG_OFFLINE        (1<<8)      /* 0 - online, 1 - offline */
#define MG_READONLY       (1<<16)     /* 0 - r/w, 1 - r/o */
#define MG_MOVING         1           /* 0 - stopped, 1 - moving */

/*
 * Параметры обмена с внешним устройством.
 */
typedef struct {
    int op;                         /* Условное слово обмена */
    int dev;                        /* Номер устройства, 0..7 */
    int memory;                     /* Начальный адрес памяти */
    int format;                     /* Флаг разметки */
    int last_moving;                /* Last unit on which movement started */
    int status;                     /* Регистр состояния */
    t_value mask_done, mask_free;   /* Маска готовности для ГРП */
    int mask_fail;                  /* Маска ошибки обмена */
    t_value *sysdata;               /* Буфер системных данных */
} KMT;

static KMT controller [4];          /* Две стойки КМД */
int mg_fail;                      /* Маска ошибок по направлениям */

t_stat mg_event (UNIT *u);

#define MG_SIZE 0
#define MG_TOTBLK 02010

#define MG_IO_DELAY (200*MSEC)
#define MG_MOVE_DELAY (100*MSEC)
#define MG_GAP_DELAY (10*MSEC)

/*
 * MG data structures
 *
 * mg_dev     DISK device descriptor
 * mg_unit    DISK unit descriptor
 * mg_reg     DISK register list
 */
UNIT mg_unit [32] = {
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_DIS, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
    { UDATA (mg_event, UNIT_ATTABLE+UNIT_ROABLE, MG_SIZE) },
};

#define in_io u3
#define cmd u4

REG mg_reg[] = {
    { ORDATA   (КУС_0,      controller[0].op,       24) },
    { ORDATA   (УСТР_0,     controller[0].dev,       3) },
    { ORDATA   (МОЗУ_0,     controller[0].memory,   20) },
    { ORDATA   (РС_0,       controller[0].status,   24) },
    { ORDATA   (КУС_1,      controller[1].op,       24) },
    { ORDATA   (УСТР_1,     controller[1].dev,       3) },
    { ORDATA   (МОЗУ_1,     controller[1].memory,   20) },
    { ORDATA   (РС_1,       controller[1].status,   24) },
    { ORDATA   (КУС_2,      controller[2].op,       24) },
    { ORDATA   (УСТР_2,     controller[2].dev,       3) },
    { ORDATA   (МОЗУ_2,     controller[2].memory,   20) },
    { ORDATA   (РС_2,       controller[2].status,   24) },
    { ORDATA   (КУС_3,      controller[3].op,       24) },
    { ORDATA   (УСТР_3,     controller[3].dev,       3) },
    { ORDATA   (МОЗУ_3,     controller[3].memory,   20) },
    { ORDATA   (РС_3,       controller[3].status,   24) },
    { ORDATA   (ОШ,         mg_fail,               6) },
    { 0 }
};

MTAB mg_mod[] = {
    { 0 }
};

t_stat mg_reset (DEVICE *dptr);
t_stat mg_attach (UNIT *uptr, CONST char *cptr);
t_stat mg_detach (UNIT *uptr);

DEVICE mg_dev = {
    "MG", mg_unit, mg_reg, mg_mod,
    32, 8, 21, 1, 8, 50,
    NULL, NULL, &mg_reset, NULL, &mg_attach, &mg_detach,
    NULL, DEV_DISABLE | DEV_DEBUG | DEV_TAPE
};

/*
 * Определение контроллера по устройству.
 */
static KMT *unit_to_ctlr (UNIT *u)
{
    return &controller[(u - mg_unit) >> 3];
}

/*
 * Reset routine
 */
t_stat mg_reset (DEVICE *dptr)
{
    int i;

    memset (&controller, 0, sizeof (controller));
    controller[2].sysdata = &memory [050];
    controller[3].sysdata = &memory [060];
    controller[2].mask_done = GRP_CHAN5_DONE;
    controller[3].mask_done = GRP_CHAN6_DONE;
    controller[2].mask_free = GRP_CHAN5_FREE;
    controller[3].mask_free = GRP_CHAN6_FREE;
    controller[2].mask_fail = 04;
    controller[3].mask_fail = 02;
    controller[2].status = BITS(8) << 8;  /* r/w, offline, not moving */
    controller[3].status = BITS(8) << 8;
    // Formatting is allowed only on controller 3
    controller[3].last_moving = -1;
    controller[3].format = 0;
    for (i=16; i<32; ++i) {
        if (mg_unit[i].flags & UNIT_ATT) {
            controller[i/8].status &= ~(MG_OFFLINE << (i%8));
            if (mg_unit[i].flags & UNIT_RO) {
                controller[i/8].status |= MG_READONLY << (i%8);
            }
        }
        mg_unit[i].in_io = 0;
        sim_cancel (&mg_unit[i]);
    }
    return SCPE_OK;
}

t_stat mg_attach (UNIT *u, CONST char *cptr)
{
    t_stat s;
    int32 saved_switches = sim_switches;
    int num = (u - mg_unit) & 7;
    int ctrl = (u - mg_unit) / 8;
    sim_switches |= SWMASK ('E');
    while (1) {
        s = sim_tape_attach (u, cptr);

        if ((s == SCPE_OK) && (sim_switches & SWMASK ('N'))) {
            t_value fullzone[8+1024];
            t_value * control = fullzone; /* block (zone) number, key, userid, checksum */
            t_value * zone = fullzone + 8;
            t_value funit = u - mg_unit + 030;
            int tapeno, blkno, word;
            char *filenamepart = NULL;
            char *pos;
            /* Using the rightmost sequence of digits within the filename
             * provided in the command line as a volume number,
             * e.g. "/var/tmp/besm6/2052.bin" -> 2052
             */
            filenamepart = sim_filepath_parts (u->filename, "n");
            pos = filenamepart + strlen(filenamepart);
            while (pos > filenamepart && !isdigit(*--pos));
            while (pos > filenamepart && isdigit(*pos)) --pos;
            if (!isdigit(*pos)) ++pos;
            tapeno = strtoul (pos, NULL, 10);
            free (filenamepart);
            if (tapeno == 0 || tapeno >= 2048) {
                if (tapeno == 0)
                    s = sim_messagef (SCPE_ARG,
                                  "%s: filename must contain volume number 1..2047\n",
                                      sim_uname(u));
                else
                    s = sim_messagef (SCPE_ARG,
                                      "%s: tape volume %d from filename %s invalid (must be 1..2047)\n",
                                      sim_uname (u), tapeno, cptr);
                filenamepart = strdup (u->filename);
                sim_tape_detach (u);
                remove (filenamepart);
                free (filenamepart);
                return s;          /* not formatting */
            }
            sim_messagef (SCPE_OK, "%s: formatting tape volume %d\n", sim_uname (u), tapeno);

            control[0] = SET_PARITY(funit << 42 | (memory[0221] & 0377774000000LL), PARITY_NUMBER);
            control[1] = SET_PARITY(0x987654321000LL, PARITY_NUMBER); /* task ID */
            control[2] = SET_PARITY((t_value)tapeno << 30 | tapeno, PARITY_NUMBER);
            control[4] = SET_PARITY(12345, PARITY_NUMBER); /* time */
            control[5] = SET_PARITY(0, PARITY_NUMBER); /* last word */
            control[7] = SET_PARITY(0, PARITY_NUMBER); /* checksum */
            for (word = 0; word < 02000; ++word) {
                zone[word] = SET_PARITY(0, PARITY_NUMBER);
            }
            for (blkno = 0; blkno < MG_TOTBLK; ++blkno) {
                int zno = blkno / 2;
                control[3] = SET_PARITY(070707LL << 24 | zno << 13 | blkno, PARITY_NUMBER);
                control[6] = control[3];
                sim_tape_wrrecf(u, (uint8*)fullzone, sizeof(fullzone));
                // sim_tape_wrgap(u, 20);
            }
            sim_tape_wrtmk(u);
            sim_tape_wrtmk(u);
            sim_tape_rewind(u);
            break;
        }
        if (s == SCPE_OK ||
            (saved_switches & SWMASK ('E')) ||
            (sim_switches & SWMASK('N')))
            break;
        sim_switches |= SWMASK ('N');
    }
    if (sim_switches & SWMASK ('R'))
        controller[ctrl].status |= MG_READONLY << num;
    else
        controller[ctrl].status &= ~(MG_READONLY << num);
    
    /* ready */
    controller[ctrl].status &= ~(MG_OFFLINE << num);
    GRP |= controller[ctrl].mask_free;
    return SCPE_OK;
}

t_stat mg_detach (UNIT *u)
{
    /* TODO: сброс бита ГРП готовности направления при отключении последнего устройства. */
    int num = (u - mg_unit) & 7;
    int ctrl = (u - mg_unit) / 8;
    /* Set RO, not ready */
    controller[ctrl].status |= (1 << (16 + num));
    controller[ctrl].status |= (1 << (8 + num));
    
    return sim_tape_detach (u);
}

/*
 * Отладочная печать массива данных обмена.
 */
static void log_data (t_value *data, int nwords)
{
    int i;
    t_value val;

    for (i=0; i<nwords; ++i) {
        val = data[i];
        fprintf (sim_log, " %04o-%04o-%04o-%04o",
                 (int) (val >> 36) & 07777,
                 (int) (val >> 24) & 07777,
                 (int) (val >> 12) & 07777,
                 (int) val & 07777);
        if ((i & 3) == 3)
            fprintf (sim_log, "\n");
    }
    if ((i & 3) != 0)
        fprintf (sim_log, "\n");
}

/*
 * Writing to a tape.
 */
void mg_write (UNIT *u)
{
    KMT *c = unit_to_ctlr (u);
    int unit = u - mg_unit;
    int ret;
    t_value fullzone[8+1024];
    int page = (u->cmd & MG_PAGE) >> 2 | (u->cmd & MG_BLOCK) >> 8;
    if (mg_dev.dctrl)
        sim_printf ("::: writing MG%d mem %05o\n",
                     unit, page);
    memcpy(fullzone, c->sysdata, 8*sizeof(t_value));
    memcpy(fullzone+8, &memory[page], 1024*sizeof(t_value));
    ret = sim_tape_wrrecf (u, (uint8*) fullzone, sizeof(fullzone));
    if (ret != MTSE_OK) {
        mg_fail |= c->mask_fail;
    }
}

/*
 * Controlling formatting mode:
 * 0 - disable, 2 - create gap, 3 - create synchrotrack
 */
void mg_format (uint32 op)
{
    KMT *c = &controller[3];
    c->format = op & 3;
    if (mg_dev.dctrl)
        sim_printf("Format mode %d\n", op);
    switch (op & 3) {
    case 0:
        if (c->last_moving != -1) {
            sim_printf("Formatting off on MG%d\n", c->last_moving);
        }
        // c->last_moving = -1;
        break;
    case 1:
        sim_printf("Formatting mode 1 does not exist\n");
        break;
    case 2:
        // When mode 2 (erasure) is enabled, if the tape is not yet moving,
        // nothing happens; if the tape is already moving, the movement ceases
        // to be self-sustaining; the runoff is 50 ms
        if (c->last_moving != -1) {
            int num = c->last_moving & 7;
            UNIT * u = mg_unit + c->last_moving;
            if (c->status & (MG_MOVING << num)) {
                sim_cancel(u);
                sim_activate(u, MG_GAP_DELAY);
                sim_printf("Block runoff on MG%d\n", c->last_moving);
            }
        }
        break;
    case 3:
        // A tape must already be moving
        if (c->last_moving == -1) {
            sim_printf("Enabling synchrotrack on a stationary tape?\n");
        } else {
            UNIT * u = mg_unit + c->last_moving;
            int num = c->last_moving & 7;
            if (c->status & (MG_MOVING << num)) {
                t_value fullzone[8+1024];
                sim_cancel(u);
                u->in_io = 0;
                sim_printf("(in_io = 0) Extending block on MG%d\n", c->last_moving);
                // Writing the synchrotrack for a zone is like writing a zone of arbitrary values
                sim_tape_wrrecf(u, (uint8*) fullzone, sizeof(fullzone));
                // Writing the synchrotrack is self-sustaining, no end event requested.
                sim_printf("Formatting block on MG%d\n", c->last_moving);
            }
        }
    }
}

/*
 * Reading from a tape.
 */
void mg_read (UNIT *u)
{
    KMT *c = unit_to_ctlr (u);
    t_mtrlnt len;
    t_value fullzone[8+1024];
    int ret;
    int unit = u - mg_unit;
    int page = (u->cmd & MG_PAGE) >> 2 | (u->cmd & MG_BLOCK) >> 8;

    if (mg_dev.dctrl)
        sim_printf ((u->cmd & MG_READ_SYSDATA) ?
                     "::: reading MG%d control words\n" :
                     "::: reading MG%d mem %05o\n",
                     unit, page);
    ret = sim_tape_rdrecf (u, (uint8*) fullzone, &len, sizeof(t_value)*(8+1024));
    if (ret != MTSE_OK || len != sizeof(t_value)*(8+1024)) {
        /* Bad tape format */
            if (mg_dev.dctrl)
                sim_printf("MG%d: Bad read: ret %d len %d\n", unit, ret, len);
            mg_fail |= c->mask_fail;
            return;
    }
    memcpy(c->sysdata, fullzone, 8*sizeof(t_value));
    if (! (u->cmd & MG_READ_SYSDATA)) {
        memcpy(&memory [page], fullzone+8, 8*1024);
    }
}


/*
 * Specifying the operation (read/write) and the memory location.
 * The actial I/O is initiated by a move command.
 */
void mg_io (int ctlr, uint32 op)
{
    KMT *c = &controller [ctlr];

    c->op = op;
    c->dev = (op >> 7) & 7;
    c->memory = (op & MG_PAGE) >> 2 | (op & MG_BLOCK) >> 8;

    if (mg_dev.dctrl)
        sim_printf ("::: MG%d: %s %s %08o\n",
                    ctlr*8 + c->dev,
                    (c->op & MG_READ) ? "read" : "write",
                    (c->op & MG_READ_SYSDATA) ? "sysdata" : "",
                    op);

    mg_fail &= ~c->mask_fail;

    /* Гасим главный регистр прерываний. */
    GRP &= ~c->mask_free;
}

/*
 * Moving the tape.
 */
void mg_ctl (int unit, uint32 op)
{
    UNIT *u = &mg_unit [unit];
    KMT *c = unit_to_ctlr (u);
    int move, back;
    if (op == MG_CLEARINTR) {
        // Only the controller number matters, unit is not used.
        GRP &= ~c->mask_done;
        return;
    }
    if (op & MG_CLEARINTR) {
        sim_printf("Clearing interrupts AND attempting to do something else (%08o)?\n", op);
        longjmp (cpu_halt, SCPE_IOERR);
    }
    if ((mg_dev.flags & DEV_DIS) || ! (u->flags & UNIT_ATT)) {
        /* Device not attached. */
        if (op != 0 && mg_dev.dctrl)
            sim_printf("::: MG%d: unattached, but control %08o issued\n", unit, op);
        mg_fail |= c->mask_fail;
        return;
    }
    mg_fail &= ~c->mask_fail;
    c->last_moving = unit;
    if (c->format) {
        c->status |= MG_MOVING << (unit & 7);
        if (c->format == 3) {
            // Must not be happening: starting from the stationary position
            // while writing the synchrotrack is bad.
            sim_printf("Accelerating while writing the synchrotrack is a bad idea.\n");
            // Moving with synchrotrack is self-sustaining, no activation needed.
        } else if (c->format == 2) {
            // Erasing, will sustain for about 50 ms
            sim_printf("Erasing MG%d\n", unit);
            sim_activate (u, MG_GAP_DELAY);
        } else if (c->format == 1) {
            if (mg_dev.dctrl)
                sim_printf("WHAT IS FORMAT 1?\n");
        }
        return;
    }
    move = op & MG_MOVE;
    back = op & MG_BACK;

    if ((unit & 7) == c->dev && move && !back) {
        /* Reading or writing */

        if (!(c->op & MG_READ) && u->flags & UNIT_RO) {
            /* Read only. */
            mg_fail |= c->mask_fail;
            return;
        }
        u->cmd = c->op;
        u->in_io = 1;
        sim_printf("MG%d: in_io = 1\n", unit);
        c->status |= MG_MOVING << (unit & 7);
        sim_activate (u, MG_IO_DELAY);
    } else if (move) {
        t_mtrlnt len;
        if (back) {
            if (sim_tape_bot (u)) {
                if (mg_dev.dctrl)
                    sim_printf("MG%d: at BOT, nowhere to step back\n", unit);
                sim_activate (u, MG_GAP_DELAY);
            } else {
                if (mg_dev.dctrl)
                    sim_printf("MG%d: Step back\n", unit);
                sim_tape_sprecr (u, &len);
                sim_activate (u, MG_MOVE_DELAY);
            }
        } else {
            if (mg_dev.dctrl)
                sim_printf("MG%d: Step forward\n", unit);
            sim_tape_sprecf (u, &len);
            sim_activate (u, MG_MOVE_DELAY);
        }
        c->status |= MG_MOVING << (unit & 7);
    } else {
        if (mg_dev.dctrl)
            sim_printf("Invalid command combination for MG%d: %08o\n", unit, op);
    }
}

/*
 * Запрос состояния контроллера.
 */
int mg_state (int ctlr)
{
    KMT *c = &controller [ctlr];
    static uint32 prev[4];
    if (mg_dev.dctrl && c->status != prev[ctlr]) {
        char status[24];
        int i;
        // Some tapes are online
        sim_printf("::: MG%02d-%02d: READONLY-ONLINE--MOVING-\n", ctlr*8+7, ctlr*8);
        for (i = 0; i < 8; ++i) {
            status[23-i] = c->status & (MG_MOVING << i) ? '0'+i : ' ';
            status[15-i] = c->status & (MG_OFFLINE << i) ? ' ': '0'+i;
            status[7-i] = c->status & (MG_READONLY << i) ? '0'+i : ' ';
        }
        sim_printf("::: MG%02d-%02d: %.24s\n", ctlr*8+7, ctlr*8, status);
        prev[ctlr] = c->status;
    }
    return c->status;
}

/*
 * End of I/O, sending an interrupt.
 */
t_stat mg_event (UNIT *u)
{
    KMT *c = unit_to_ctlr (u);
    int unit = u - mg_unit;
    int num = unit & 7;
    if (u->in_io) {
        if (u->cmd & MG_READ) {
            mg_read(u);
        } else {
            mg_write(u);
        }
        GRP |= c->mask_free;
        u->in_io = 0;
        sim_activate (u, MG_GAP_DELAY);
        if (mg_dev.dctrl)
            sim_printf("::: MG%d: (in_io = 0) end of I/O event\n", unit);
    } else {
        c->status &= ~(MG_MOVING << num);
        c->status &= ~(MG_OFFLINE << num);
        GRP |= c->mask_done;
        // if (mg_dev.dctrl)
            sim_printf("::: MG%d: stopping event\n", unit);
    }
    return SCPE_OK;
}

/*
 * Опрос ошибок обмена командой 033 4035.
 */
int mg_errors ()
{
#if 0
    if (mg_dev.dctrl)
        sim_printf ("::: КМД: опрос шкалы ошибок = %04o\n", mg_fail);
#endif
    return mg_fail;
}
