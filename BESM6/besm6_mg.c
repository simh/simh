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

static KMT controller [4];          /* 4 channels, 8 tape devices on each */
int mg_fail;                        /* Маска ошибок по направлениям */

t_stat mg_event (UNIT *u);

#define MG_SIZE 0
#define MG_TOTBLK 02010

#define MG_IO_DELAY (200*MSEC)
#define MG_MOVE_DELAY (100*MSEC)
#define MG_GAP_DELAY (10*MSEC)

// Formatting is allowed only on channel 6 (controller 3)
#define FMT_CTLR 3

/*
 * MG data structures
 *
 * mg_dev     DISK device descriptor
 * mg_unit    DISK unit descriptor
 * mg_reg     DISK register list
 */
UNIT mg_unit [32] = {
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
    { 0 },
    { ORDATA   (КУС_1,      controller[1].op,       24) },
    { ORDATA   (УСТР_1,     controller[1].dev,       3) },
    { ORDATA   (МОЗУ_1,     controller[1].memory,   20) },
    { ORDATA   (РС_1,       controller[1].status,   24) },
    { 0 },
    { ORDATA   (КУС_2,      controller[2].op,       24) },
    { ORDATA   (УСТР_2,     controller[2].dev,       3) },
    { ORDATA   (МОЗУ_2,     controller[2].memory,   20) },
    { ORDATA   (РС_2,       controller[2].status,   24) },
    { 0 },
    { ORDATA   (КУС_3,      controller[3].op,       24) },
    { ORDATA   (УСТР_3,     controller[3].dev,       3) },
    { ORDATA   (МОЗУ_3,     controller[3].memory,   20) },
    { ORDATA   (РС_3,       controller[3].status,   24) },
    { ORDATA   (ОШ,         mg_fail,                 6) },
    { 0 }
};

MTAB mg_mod[] = {
    { 0 }
};

t_stat mg_reset (DEVICE *dptr);
t_stat mg_attach (UNIT *uptr, CONST char *cptr);
t_stat mg_detach (UNIT *uptr);

DEVICE mg_dev[4] = {
    {
        "MG3", mg_unit, mg_reg, mg_mod,
        8, 8, 21, 1, 8, 50,
        NULL, NULL, &mg_reset, NULL, &mg_attach, &mg_detach,
        NULL, DEV_DISABLE | DEV_DEBUG | DEV_TAPE
    },
    {
        "MG4", mg_unit + 8, mg_reg + 5, mg_mod,
        8, 8, 21, 1, 8, 50,
        NULL, NULL, &mg_reset, NULL, &mg_attach, &mg_detach,
        NULL, DEV_DISABLE | DEV_DEBUG | DEV_TAPE
    },
    {
        "MG5", mg_unit + 16, mg_reg + 10, mg_mod,
        8, 8, 21, 1, 8, 50,
        NULL, NULL, &mg_reset, NULL, &mg_attach, &mg_detach,
        NULL, DEV_DISABLE | DEV_DEBUG | DEV_TAPE
    },
    {
        "MG6", mg_unit + 24, mg_reg + 15, mg_mod,
        8, 8, 21, 1, 8, 50,
        NULL, NULL, &mg_reset, NULL, &mg_attach, &mg_detach,
        NULL, DEV_DISABLE | DEV_DEBUG | DEV_TAPE
    },
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
    int ctlr = dptr - mg_dev;
    KMT *c = &controller[ctlr];
    memset (c, 0, sizeof (*c));
    /*
     * The areas starting from words 030 and 040 are used for
     * disks; the remaining locations are shared by two channels each.
     */
    c->sysdata = &memory [ctlr <= 1 ? 050 : 060];

    /*
     * The "end of tape movement" interrupts are not used by the disks
     * and remain as per the initial spec.
     */
    c->mask_done = GRP_CHAN3_DONE >> ctlr;

    /*
     * The "end of I/O" interrupts go to channel 5 for all
     * channels except the 6th, which is the only channel used for
     * formatting tapes, requiring better responsiveness.
     */
    c->mask_free = ctlr == FMT_CTLR ? GRP_CHAN6_FREE : GRP_CHAN5_FREE;

    /*
     * Error masks follow the I/O interrupt scheme.
     */
    c->mask_fail = ctlr == FMT_CTLR ? 02 : 04;

    c->status = BITS(8) << 8;  /* r/w, offline, not moving */
    c->last_moving = -1;       /* used only by the FMT_CTLR */
    c->format = 0;

    for (i=0; i<8; ++i) {
        if (mg_unit[ctlr*8+i].flags & UNIT_ATT) {
            c->status &= ~(MG_OFFLINE << i);
            if (mg_unit[ctlr*8+i].flags & UNIT_RO) {
                c->status |= MG_READONLY << i;
            }
        }
        mg_unit[ctlr*8+i].dptr = dptr;
        mg_unit[ctlr*8+i].in_io = 0;
        sim_cancel (&mg_unit[ctlr*8+i]);
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
    if (u->dptr->dctrl)
        sim_printf ("::: writing %s mem %05o\n", sim_uname(u), page);
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
    KMT *c = &controller[FMT_CTLR];
    int prev = c->format;
    c->format = op & 3;
    switch (op & 3) {
    case 0:
        if (prev != 0 && c->last_moving != -1) {
            sim_printf("Formatting off on MG6%d\n", c->last_moving);
        }
        break;
    case 1:
        sim_printf("Formatting mode 1 does not exist\n");
        break;
    case 2:
        // When mode 2 (erasure) is enabled, if the tape is not yet moving,
        // nothing happens; if the tape is already moving, the movement ceases
        // to be self-sustaining; the runoff is 50 ms
        if (c->last_moving != -1) {
            int num = c->last_moving;
            UNIT * u = mg_unit + 8*FMT_CTLR + num;
            sim_printf("Formatting mode 2\n");
            if (c->status & (MG_MOVING << num)) {
                sim_cancel(u);
                sim_activate(u, MG_GAP_DELAY);
                sim_printf("Block runoff on MG6%d\n", c->last_moving);
            }
        }
        break;
    case 3:
        sim_printf("Formatting mode 3\n");
        // A tape must already be moving
        if (c->last_moving == -1) {
            sim_printf("Enabling synchrotrack on a stationary tape?\n");
        } else {
            int num = c->last_moving;
            UNIT * u = mg_unit + 8*FMT_CTLR + num;
            if (c->status & (MG_MOVING << num)) {
                t_value fullzone[8+1024];
                sim_cancel(u);
                u->in_io = 0;
                sim_printf("(in_io = 0) Extending block on %s\n", sim_uname(u));
                // Writing the synchrotrack for a zone is like writing a zone of arbitrary values
                sim_tape_wrrecf(u, (uint8*) fullzone, sizeof(fullzone));
                // Writing the synchrotrack is self-sustaining, no end event requested.
                sim_printf("Formatting block on %s\n", sim_uname(u));
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
    int unit = (u - mg_unit) & 7;
    int page = (u->cmd & MG_PAGE) >> 2 | (u->cmd & MG_BLOCK) >> 8;

    if (u->dptr->dctrl)
        sim_printf ((u->cmd & MG_READ_SYSDATA) ?
                     "::: reading %s control words\n" :
                     "::: reading %s mem %05o\n",
                    sim_uname(u), page);
    ret = sim_tape_rdrecf (u, (uint8*) fullzone, &len, sizeof(t_value)*(8+1024));
    if (ret != MTSE_OK || len != sizeof(t_value)*(8+1024)) {
        /* Bad tape format */
            if (u->dptr->dctrl)
                sim_printf("%s: Bad read: ret %d len %d\n", sim_uname(u), ret, len);
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
 * The actual I/O is initiated by a move command.
 * The I/O setting is taken by two controllers.
 * Given 2 affects 0 and 1.
 * Given 3 affects 2 and 3.
 */
void mg_io (int ctlr, uint32 op)
{
    int i;
    int dev = (op >> 7) & 7;
    for (i = (ctlr & 1) * 2; i <= (ctlr & 1) * 2 + 1; ++i) {
        KMT *c = &controller [i];
        c->op = op;
        c->dev = dev;
        c->memory = (op & MG_PAGE) >> 2 | (op & MG_BLOCK) >> 8;
    }

    if (mg_dev[ctlr].dctrl)
        sim_printf ("::: MG%o/%o: %s %s %08o\n",
                    (ctlr&1)*16 + 030 + dev, (ctlr&1)*16 + 040 + dev,
                    (op & MG_READ) ? "read" : "write",
                    (op & MG_READ_SYSDATA) ? "sysdata" : "",
                    op);

    /*
     * Error flags and interrupts, however, use the given controller number.
     */
    mg_fail &= ~controller[ctlr].mask_fail;

    /* Clearing the main interrupt register */
    GRP &= ~controller[ctlr].mask_free;
}

/*
 * Moving the tape.
 */
void mg_ctl (int unit, uint32 op)
{
    UNIT *u = &mg_unit [unit];
    KMT *c = unit_to_ctlr (u);
    int num = unit & 7;
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
    if ((u->dptr->flags & DEV_DIS) || ! (u->flags & UNIT_ATT)) {
        /* Device not attached. */
        if (op != 0 && u->dptr->dctrl)
            sim_printf("::: %s: unattached, but control %08o issued\n",
                       sim_uname(u), op);
        mg_fail |= c->mask_fail;
        return;
    }
    mg_fail &= ~c->mask_fail;
    c->last_moving = num;
    if (c->format) {
        c->status |= MG_MOVING << num;
        if (c->format == 3) {
            // Must not be happening: starting from the stationary position
            // while writing the synchrotrack is bad.
            sim_printf("Accelerating while writing the synchrotrack is a bad idea.\n");
            // Moving with synchrotrack is self-sustaining, no activation needed.
        } else if (c->format == 2) {
            // Erasing, will sustain for about 50 ms
            sim_printf("Erasing %s\n", sim_uname(u));
            sim_activate (u, MG_GAP_DELAY);
        } else if (c->format == 1) {
            if (u->dptr->dctrl)
                sim_printf("WHAT IS FORMAT 1?\n");
        }
        return;
    }
    move = op & MG_MOVE;
    back = op & MG_BACK;

    if (num == c->dev && move && !back) {
        /* Reading or writing */

        if (!(c->op & MG_READ) && u->flags & UNIT_RO) {
            /* Read only. */
            mg_fail |= c->mask_fail;
            return;
        }
        u->cmd = c->op;
        u->in_io = 1;
        if (u->dptr->dctrl)
            sim_printf("::: %s: in_io = 1\n", sim_uname(u));
        c->status |= MG_MOVING << num;
        sim_activate (u, MG_IO_DELAY);
    } else if (move) {
        t_mtrlnt len;
        if (back) {
            if (sim_tape_bot (u)) {
                if (u->dptr->dctrl)
                    sim_printf("%s: at BOT, nowhere to step back\n",
                               sim_uname(u));
                sim_activate (u, MG_GAP_DELAY);
            } else {
                if (u->dptr->dctrl)
                    sim_printf("%s: Step back\n", sim_uname(u));
                sim_tape_sprecr (u, &len);
                sim_activate (u, MG_MOVE_DELAY);
            }
        } else {
            if (u->dptr->dctrl)
                sim_printf("%s: Step forward\n", sim_uname(u));
            sim_tape_sprecf (u, &len);
            sim_activate (u, MG_MOVE_DELAY);
        }
        c->status |= MG_MOVING << num;
    } else {
        if (u->dptr->dctrl)
            sim_printf("Invalid command combination for %s: %08o\n",
                       sim_uname(u), op);
    }
}

/*
 * Запрос состояния контроллера.
 */
int mg_state (int ctlr)
{
    KMT *c = &controller [ctlr];
    static uint32 prev[4];
    if (mg_dev[ctlr].dctrl && c->status != prev[ctlr]) {
        char status[24];
        int i;
        // Some tapes are online
        sim_printf("::: MG%02o-%02o: READONLY-ONLINE--MOVING-\n",
                   ctlr*8+31, ctlr*8+24);
        for (i = 0; i < 8; ++i) {
            status[23-i] = c->status & (MG_MOVING << i) ? '0'+i : ' ';
            status[15-i] = c->status & (MG_OFFLINE << i) ? ' ': '0'+i;
            status[7-i] = c->status & (MG_READONLY << i) ? '0'+i : ' ';
        }
        sim_printf("::: MG%02o-%02o: %.24s\n",
                   ctlr*8+31, ctlr*8+24, status);
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
    if (u->dptr->dctrl)
        sim_printf("::: %s: event\n", sim_uname(u));
    if (u->in_io) {
        if (u->cmd & MG_READ) {
            mg_read(u);
        } else {
            mg_write(u);
        }
        GRP |= c->mask_free;
        u->in_io = 0;
        sim_activate (u, MG_GAP_DELAY);
        if (u->dptr->dctrl)
            sim_printf("::: %s: (in_io = 0) end of I/O event\n", sim_uname(u));
    } else {
        c->status &= ~(MG_MOVING << num);
        c->status &= ~(MG_OFFLINE << num);
        GRP |= c->mask_done;
        if (u->dptr->dctrl)
            sim_printf("::: %s: stopping event\n", sim_uname(u));
    }
    return SCPE_OK;
}

/*
 * Опрос ошибок обмена командой 033 4035.
 */
int mg_errors ()
{
#if 0
    if (mg_dev[0].dctrl)
        sim_printf ("::: КМД: опрос шкалы ошибок = %04o\n", mg_fail);
#endif
    return mg_fail;
}
