/*
 * BESM-6 magnetic disk device
 *
 * Copyright (c) 2009, Serge Vakulenko
 * Copyright (c) 2009, Leonid Broukhis
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
#include <sys/types.h>
#include <sys/stat.h>

/*
 * Управляющее слово обмена с магнитным диском.
 */
#define DISK_BLOCK         0740000000   /* номер блока памяти - 27-24 рр */
#define DISK_READ_SYSDATA   004000000   /* считывание только служебных слов */
#define DISK_PAGE_MODE      001000000   /* обмен целой страницей */
#define DISK_READ           000400000   /* чтение с диска в память */
#define DISK_PAGE           000370000   /* номер страницы памяти */
#define DISK_HALFPAGE       000004000   /* выбор половины листа */
#define DISK_UNIT           000001600   /* номер устройства */
#define DISK_HALFZONE       000000001   /* выбор половины зоны */

/*
 * Status register bits (most are unused: error conditions are not simulated)
 */
#define STATUS_SEEK         000000377   /* "Seek done" mask, per unit */
#define STATUS_READY        000000400   /* Selected unit is ready */
#define STATUS_SEEK_FAIL    000001000   /* Head location unknown, unit not ready */
#define STATUS_CHECKSUM     000002000   /* Bad checksum on read */
#define STATUS_FAILURE      000004000   /* Failure, OR of some upper bits */
#define STATUS_MAYDAY       000010000   /* Unspecified failure */
#define STATUS_NO_AMRK      000020000   /* Address marker not found after a revolution */
#define STATUS_WRONG_CYL    000040000   /* Wrong address marker */
#define STATUS_WRONG_ID     000100000   /* Bad track ID */
#define STATUS_BAD_ACSUM    000200000   /* Bad checksum of the address marker */
#define STATUS_UNFINISHED   000400000   /* IO not finished after a revolution */
#define STATUS_TRK_PARITY   001000000   /* Track parity in two-track IO */
#define STATUS_READONLY     002000000   /* The selected unit is read-only */
#define STATUS_POWERUP      004000000   /* The unit is powered up */
#define STATUS_ABSENT       010000000   /* The unit is not connected */
#define STATUS_BUF_ERR      020000000   /* Transfer buffer not ready */
 
/*
 * Total size of a "7.25 Mb" disk is 1000 (decimal) blocks;
 * of a "29 Mb" disk - 4000 blocks, out of which 4 are so called
 * pre-blocks. Logical blocks are mapped to physical by adding 4.
 * Physical blocks 0 to 2 are accesible only by standalone programs,
 * block 3 has the logical number "minus one".
 */
#define SYSTEM_VOLUME_ID 2053
/*
 * Параметры обмена с внешним устройством.
 */
typedef struct {
    int op;                         /* Условное слово обмена */
    int group;                      /* Unit group number */
    int dev;                        /* Номер устройства, 0..7 */
    int zone;                       /* Номер зоны на диске */
    int track;                      /* Выбор половины зоны на диске */
    int memory;                     /* Начальный адрес памяти */
    int format;                     /* Флаг разметки */
    int status;                     /* Регистр состояния */
    t_value mask_grp;               /* Маска готовности для ГРП */
    int mask_fail;                  /* Маска ошибки обмена */
    t_value *sysdata;               /* Буфер системных данных */
} KMD;

static KMD controller [2];          /* Две стойки КМД */
int disk_fail;                      /* Маска ошибок по направлениям */

t_stat disk_event (UNIT *u);

/*
 * DISK data structures
 *
 * md_dev     DISK device descriptor
 * md_unit    DISK unit descriptor
 * md_reg     DISK register list
 */
UNIT md_unit [64] = {
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
};

REG disk_reg[] = {
    { ORDATA   (КУС_0,      controller[0].op,       24) },
    { ORDATA   (ЛИНЕЙКА_0,  controller[0].group,     2) },
    { ORDATA   (УСТР_0,     controller[0].dev,       3) },
    { ORDATA   (ЗОНА_0,     controller[0].zone,     10) },
    { ORDATA   (ДОРОЖКА_0,  controller[0].track,     2) },
    { ORDATA   (МОЗУ_0,     controller[0].memory,   20) },
    { ORDATA   (РС_0,       controller[0].status,   24) },
    { 0 },
    { ORDATA   (КУС_1,      controller[1].op,       24) },
    { ORDATA   (ЛИНЕЙКА_1,  controller[1].group,     2) },
    { ORDATA   (УСТР_1,     controller[1].dev,       3) },
    { ORDATA   (ЗОНА_1,     controller[1].zone,     10) },
    { ORDATA   (ДОРОЖКА_1,  controller[1].track,     2) },
    { ORDATA   (МОЗУ_1,     controller[1].memory,   20) },
    { ORDATA   (РС_1,       controller[1].status,   24) },
    { ORDATA   (ОШ,         disk_fail,               6) },
    { 0 }
};

static FILE * syslog = NULL;
t_stat disk_setsyslog (UNIT *up, int32 v, CONST char *cp, void *dp) {
    if (syslog) {
        fclose(syslog);
        syslog = NULL;
    }
    if (strcasecmp(cp, "OFF") == 0)
        return SCPE_OK;
    syslog = fopen(cp, "a");
    if (!syslog)
        return sim_messagef (SCPE_OPENERR, "Failed to open SYSLOG file %s: %s\n", cp, strerror(errno));
    return SCPE_OK;
}

#define DISK_TYPE_MASK (1 << UNIT_V_UF)
#define DISK_TYPE_7_25M 0
#define DISK_TYPE_29M  (1 << UNIT_V_UF)
#define IS_29MB(u) (((u)->flags & DISK_TYPE_MASK) == DISK_TYPE_29M)

t_stat disk_set_type (UNIT *up, int32 v, CONST char *cp, void *dp) {
    int first_unit = (up->dptr - md_dev) * 8;
    int unit;
    for (unit = first_unit; unit < first_unit + 8; ++unit) {
        if (md_unit[unit].flags & UNIT_ATT)
            return sim_messagef(SCPE_ALATT, "Drive type cannot be set if any unit is attached");
    }
    for (unit = first_unit; unit < first_unit + 8; ++unit) {
        md_unit[unit].flags &= ~DISK_TYPE_MASK;
        md_unit[unit].flags |= v;
        md_unit[unit].capac = 7250000 * (v ? 4 : 1);
    }
    return SCPE_OK;
}


MTAB disk_mod[] = {
    { MTAB_VDV, DISK_TYPE_7_25M, NULL, "EC-5052", &disk_set_type, NULL, NULL, "EC-5052 drive (7.25 Mb)"},
    { MTAB_VDV, DISK_TYPE_29M, NULL, "EC-5061", &disk_set_type, NULL, NULL, "EC-5061 drive (29 Mb)"},
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 1, NULL,
      "SYSLOG", &disk_setsyslog, NULL, NULL, "file name (always appending) or OFF" },
    { 0 }
};


t_stat disk_reset (DEVICE *dptr);
t_stat disk_attach (UNIT *uptr, CONST char *cptr);
t_stat disk_detach (UNIT *uptr);

#define DEB_OPS 000001
#define DEB_RRD 000002
#define DEB_RWR 000004
#define DEB_INT 000010
#define DEB_TRC 000020
#define DEB_DAT 000040
#define DEB_STA 000100

DEBTAB disk_deb[] = {
    { "OPS",        DEB_OPS, "transactions" },
    { "RRD",        DEB_RRD, "register reads" },
    { "RWR",        DEB_RWR, "register writes" },
    { "INTERRUPT",  DEB_INT, "interrupts" },
    { "TRACE",      DEB_TRC, "trace" },
    { "DATA",       DEB_DAT, "transfer data" },
    { "STATUS",     DEB_STA, "status check" },
    { NULL, 0 }
    };

DEVICE md_dev[8] = {
    {
        "MD0", md_unit, disk_reg, disk_mod,
        8, 8, 21, 50, 8, 50,
        NULL, NULL, &disk_reset, NULL, &disk_attach, &disk_detach,
        NULL, DEV_DISABLE | DEV_DEBUG, 0, disk_deb
    },
    {
        "MD1", md_unit + 8, disk_reg, disk_mod,
        8, 8, 21, 50, 8, 50,
        NULL, NULL, &disk_reset, NULL, &disk_attach, &disk_detach,
        NULL, DEV_DISABLE | DEV_DEBUG, 0, disk_deb
    },
    {
        "MD2", md_unit + 16, disk_reg, disk_mod,
        8, 8, 21, 50, 8, 50,
        NULL, NULL, &disk_reset, NULL, &disk_attach, &disk_detach,
        NULL, DEV_DISABLE | DEV_DEBUG, 0, disk_deb
    },
    {
        "MD3", md_unit + 24, disk_reg, disk_mod,
        8, 8, 21, 50, 8, 50,
        NULL, NULL, &disk_reset, NULL, &disk_attach, &disk_detach,
        NULL, DEV_DISABLE | DEV_DEBUG, 0, disk_deb
    },
    {
        "MD4", md_unit + 32, disk_reg + 8, disk_mod,
        8, 8, 21, 50, 8, 50,
        NULL, NULL, &disk_reset, NULL, &disk_attach, &disk_detach,
        NULL, DEV_DISABLE | DEV_DEBUG, 0, disk_deb
    },
    {
        "MD5", md_unit + 40, disk_reg + 8, disk_mod,
        8, 8, 21, 50, 8, 50,
        NULL, NULL, &disk_reset, NULL, &disk_attach, &disk_detach,
        NULL, DEV_DISABLE | DEV_DEBUG, 0, disk_deb
    },
    {
        "MD6", md_unit + 48, disk_reg + 8, disk_mod,
        8, 8, 21, 50, 8, 50,
        NULL, NULL, &disk_reset, NULL, &disk_attach, &disk_detach,
        NULL, DEV_DISABLE | DEV_DEBUG, 0, disk_deb
    },
    {
        "MD7", md_unit + 56, disk_reg + 8, disk_mod,
        8, 8, 21, 50, 8, 50,
        NULL, NULL, &disk_reset, NULL, &disk_attach, &disk_detach,
        NULL, DEV_DISABLE | DEV_DEBUG, 0, disk_deb
    },
};

/*
 * Определение контроллера по устройству.
 */
static KMD *unit_to_ctlr (UNIT *u)
{
    return &controller[(u - md_unit) / 32];
}

/*
 * Reset routine
 */
t_stat disk_reset (DEVICE *dptr)
{
    int i;
    int ctlr = (dptr - md_dev) / 4;
    int first_unit = (dptr - md_dev) * 8;
    KMD *c = &controller[ctlr];
    memset (c, 0, sizeof (KMD));
    c->sysdata = &memory [030 + ctlr * 8];
    c->mask_grp = GRP_CHAN3_FREE >> ctlr;
    c->mask_fail = 020 >> ctlr;
    for (i = first_unit; i < first_unit + 8; ++i) {
        md_unit[i].dptr = dptr;
        md_unit[i].capac = 7250000 * (IS_29MB(&md_unit[i]) ? 4 : 1);
        sim_cancel (&md_unit[i]);
    }
    return SCPE_OK;
}

t_stat disk_attach (UNIT *u, CONST char *cptr)
{
    t_stat s;
    int32 saved_switches = sim_switches;
    sim_switches |= SWMASK ('E');

    while (1) {
        s = attach_unit (u, cptr);
        if ((s == SCPE_OK) && (sim_switches & SWMASK ('N'))) {
            t_value control[4];  /* block (zone) number, key, userid, checksum */
            int diskno, blkno, word;
            char *filenamepart = NULL;
            const char *pos;
            /* Using the rightmost sequence of digits within the filename
             * provided in the command line as a volume number,
             * e.g. "/var/tmp/besm6/2052.bin" -> 2052
             */
            filenamepart = sim_filepath_parts (u->filename, "n");
            pos = filenamepart + strlen(filenamepart);
            while (pos > filenamepart && !isdigit(*--pos));
            while (pos > filenamepart && isdigit(*pos)) --pos;
            if (!isdigit(*pos)) ++pos;
            diskno = strtoul (pos, NULL, 10);
            free (filenamepart);
            if (diskno < 2048 || diskno > 4095) {
                if (diskno == 0)
                    s = sim_messagef (SCPE_ARG,
                                  "%s: filename must contain volume number 2048..4095\n",
                                      sim_uname(u));
                else
                    s = sim_messagef (SCPE_ARG,
                                      "%s: disk volume %d from filename %s invalid (must be 2048..4095)\n",
                                      sim_uname (u), diskno, cptr);
                filenamepart = strdup (u->filename);
                detach_unit (u);
                remove (filenamepart);
                free (filenamepart);
                return s;          /* not formatting */
            }
            sim_messagef (SCPE_OK, "%s: formatting disk volume %d\n", sim_uname (u), diskno);

            control[1] = SET_PARITY(0, PARITY_NUMBER);
            control[2] = SET_PARITY(0, PARITY_NUMBER);
            control[3] = SET_PARITY(0, PARITY_NUMBER);

            control[1] |= 01370707LL << 24;    /* Magic mark */
            control[1] |= diskno << 12;

            /* Unlike the O/S routine, does not format the (useless) reserve tracks */
            for (blkno = 0; blkno < (IS_29MB(u) ? 4000 : 1000); ++blkno) {
                uint32 val = IS_29MB(u) ? blkno : 2 * blkno;
                control[0] = SET_PARITY((t_value)val << 36, PARITY_NUMBER);
                sim_fwrite(control, sizeof(t_value), 4, u->fileref);
                control[0] = SET_PARITY((t_value)(val+1) << 36, PARITY_NUMBER);
                sim_fwrite(control, sizeof(t_value), 4, u->fileref);
                for (word = 0; word < 02000; ++word) {
                    sim_fwrite(control+2, sizeof(t_value), 1, u->fileref);
                }
            }
        }
        if (s == SCPE_OK ||
            (saved_switches & SWMASK ('E')) ||
            (sim_switches & SWMASK('N')))
            break;
        sim_switches |= SWMASK ('N');
    }
    return SCPE_OK;
}

t_stat disk_detach (UNIT *u)
{
    /* TODO: сброс бита ГРП готовности направления при отключении последнего диска. */
    return detach_unit (u);
}

t_value spread (t_value val)
{
    int i, j;
    t_value res = 0;

    for (i = 0; i < 5; i++)
        for (j = 0; j < 9; j++)
            if (val & (1LL<<(i+j*5)))
                res |= 1LL << (i*9+j);
    return res & BITS48;
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
 * Сложение с переносом вправо.
 */
static unsigned sum_with_right_carry (unsigned a, unsigned b)
{
    unsigned c;

    while (b) {
        c = a & b;
        a ^= b;
        b = c >> 1;
    }
    return a;
}

/*
 * Запись на диск.
 */
void disk_write (UNIT *u)
{
    KMD *c = unit_to_ctlr (u);
    int cnum = c - controller;
    if (u->dptr->dctrl & DEB_DAT)
        besm6_debug ("::: запись МД %02o зона %04o память %05o-%05o",
                     c->dev, c->zone, c->memory, c->memory + 1023);
    if (fseek (u->fileref, ZONE_SIZE * c->zone * 8, SEEK_SET) == 0) {
        sim_fwrite (c->sysdata, 8, 8, u->fileref);
        sim_fwrite (&memory [c->memory], 8, 1024, u->fileref);
    }

    /* Logging system disk accesses */
    if (syslog && ((c->sysdata[1] >> 12) & 0xFFF) == SYSTEM_VOLUME_ID) {
        fprintf(syslog, "W %04o @%05o\n", (c->zone-4)&07777, c->memory);
        fflush(syslog);
    }

    if (ferror (u->fileref))
        longjmp (cpu_halt, SCPE_IOERR);
}

void disk_write_track (UNIT *u)
{
    KMD *c = unit_to_ctlr (u);
    int cnum = c - controller;
    if (u->dptr->dctrl & DEB_DAT)
        besm6_debug ("::: запись МД %02o полузона %04o.%d память %05o-%05o",
                     c->dev, c->zone, c->track, c->memory, c->memory + 511);
    if (fseek (u->fileref, (ZONE_SIZE*c->zone + 4*c->track) * 8,
               SEEK_SET) == 0) {
        sim_fwrite (c->sysdata + 4*c->track, 8, 4, u->fileref);
        if (fseek (u->fileref, (8 + ZONE_SIZE*c->zone + 512*c->track) * 8,
                   SEEK_SET) == 0) {
            sim_fwrite (&memory [c->memory], 8, 512, u->fileref);
        }
    }
    if (ferror (u->fileref))
        longjmp (cpu_halt, SCPE_IOERR);
}

/*
 * Форматирование дорожки.
 */
void disk_format (UNIT *u)
{
    KMD *c = unit_to_ctlr (u);
    t_value fmtbuf[5];
    t_value *ptr;
    int i;
    int cnum = c - controller;
    /* По сути, эмулятору ничего делать не надо. */
    if (! (u->dptr->dctrl & DEB_DAT))
        return;

    /* Находим начало записываемого заголовка. */
    ptr = &memory [c->memory];
    while ((*ptr & BITS48) == 0)
        ptr++;

    /* Декодируем из гребенки в нормальный вид. */
    for (i = 0; i < 5; i++)
        fmtbuf[i] = spread (ptr[i]);

    /* При первой попытке разметки адресный маркер начинается в старшем 5-разрядном слоге,
     * пропускаем первый слог. */
    for (i=0; i<5; i++)
        fmtbuf[i] = ((fmtbuf[i] & BITS48) << 5) |
            (i == 4 ? 0 : (fmtbuf[i+1] >> 40) & BITS(5));

    log_data(fmtbuf, 5);

    /* Печатаем идентификатор, адрес и контрольную сумму адреса. */
    if (u->dptr->dctrl & DEB_TRC)
        if (IS_29MB(u))
            besm6_debug ("::: формат МД %02o зона %04o память %05o skip %02o и-а-кса %010o %010o",
                         c->dev, c->zone, c->memory, ptr - memory -c ->memory,
                         (int) (fmtbuf[0] >> 8 & BITS(30)),
                         (int) (fmtbuf[2] >> 14 & BITS(30)));
        else
            besm6_debug ("::: формат МД %02o полузона %04o.%d память %05o skip %02o и-а-кса %010o %010o",
                         c->dev, c->zone, c->track, c->memory, ptr - memory -c ->memory,
                         (int) (fmtbuf[0] >> 8 & BITS(30)),
                         (int) (fmtbuf[2] >> 14 & BITS(30)));
}

/*
 * Чтение с диска.
 */
void disk_read (UNIT *u)
{
    KMD *c = unit_to_ctlr (u);
    int cnum = c - controller;
    if (u->dptr->dctrl & DEB_DAT)
        besm6_debug ((c->op & DISK_READ_SYSDATA) ?
                     "::: чтение МД %02o зона %04o служебные слова" :
                     "::: чтение МД %02o зона %04o память %05o-%05o",
                     c->dev, c->zone, c->memory, c->memory + 1023);
    if (fseek (u->fileref, ZONE_SIZE * c->zone * 8, SEEK_SET) != 0 ||
        sim_fread (c->sysdata, 8, 8, u->fileref) != 8) {
        /* Чтение неинициализированного диска */
        disk_fail |= c->mask_fail;
        return;
    }
    if (! (c->op & DISK_READ_SYSDATA) &&
        sim_fread (&memory [c->memory], 8, 1024, u->fileref) != 1024) {
        /* Чтение неинициализированного диска */
        disk_fail |= c->mask_fail;
        return;
    }

    /* Logging system disk accesses */
    if (syslog && ((c->sysdata[1] >> 12) & 0xFFF) == SYSTEM_VOLUME_ID) {
        fprintf(syslog, "R %04o @%05o\n", (c->zone-4)&07777, c->memory);
        fflush(syslog);
    }

    if (ferror (u->fileref))
        longjmp (cpu_halt, SCPE_IOERR);
}

t_value collect (t_value val)
{
    int i, j;
    t_value res = 0;

    for (i = 0; i < 5; i++)
        for (j = 0; j < 9; j++)
            if (val & (1LL<<(i*9+j)))
                res |= 1LL << (i+j*5);
    return res & BITS48;
}

void disk_read_track (UNIT *u)
{
    KMD *c = unit_to_ctlr (u);
    int cnum = c - controller;
    if (u->dptr->dctrl & DEB_DAT)
        besm6_debug ((c->op & DISK_READ_SYSDATA) ?
                     "::: чтение МД %02o полузона %04o.%d служебные слова" :
                     "::: чтение МД %02o полузона %04o.%d память %05o-%05o",
                     c->dev, c->zone, c->track, c->memory, c->memory + 511);
    if (fseek (u->fileref, (ZONE_SIZE*c->zone + 4*c->track) * 8, SEEK_SET) != 0 ||
        sim_fread (c->sysdata + 4*c->track, 8, 4, u->fileref) != 4) {
        /* Чтение неинициализированного диска */
        disk_fail |= c->mask_fail;
        return;
    }
    if (! (c->op & DISK_READ_SYSDATA)) {
        if (fseek (u->fileref, (8 + ZONE_SIZE*c->zone + 512*c->track) * 8,
                   SEEK_SET) != 0 ||
            sim_fread (&memory [c->memory], 8, 512, u->fileref) != 512) {
            /* Чтение неинициализированного диска */
            disk_fail |= c->mask_fail;
            return;
        }
    }
    if (ferror (u->fileref))
        longjmp (cpu_halt, SCPE_IOERR);
}

/*
 * Чтение заголовка дорожки.
 */
void disk_read_header (UNIT *u)
{
    KMD *c = unit_to_ctlr (u);
    t_value *sysdata = IS_29MB(u) ? c->sysdata : c->sysdata + 4*c->track;
    int iaksa, i, cyl, head;
    int reserve_start = IS_29MB(u) ? 07640 : 01750;

    /* Адрес: номер цилиндра и головки. */
    if (IS_29MB(u)) {
        head = c->zone;
        cyl = head / 20;
        head %= 20;
        iaksa = (head << 3) + (cyl << 8);
        iaksa <<= 12;
    } else {
        head = (c->zone << 1) + c->track;
        cyl = head / 10;
        head %= 10;
        iaksa = (cyl << 20) | (head << 16);
    }

    /* Идентификатор дорожки замены. */
    if (c->zone >= reserve_start)
        iaksa |= BBIT(30);

    /* Контрольная сумма адреса с переносом вправо. */
    iaksa |= BITS(12) & ~sum_with_right_carry (iaksa >> 12, iaksa >> 24);

    /* Амиакса, 42 нуля, амиакса, много единиц. */
    sysdata[0] = 07404000000000000LL | (t_value) iaksa << 8;
    sysdata[1] = 03740LL;
    sysdata[2] = 00400000000037777LL | (t_value) iaksa << 14;
    sysdata[3] = BITS48;

    if (IS_29MB(u)) {
        for (i=0; i<4; i++) {
            memory[c->memory + i + 014] = SET_PARITY(sysdata[i] & 0777777777777777LL, PARITY_NUMBER);
        }
    }

    /* Кодируем гребенку. */
    for (i=0; i<4; i++)
        sysdata[i] = SET_PARITY (collect (sysdata[i]), PARITY_NUMBER);

}

/*
 * Задание адреса памяти и длины массива для последующего обращения к диску.
 * Номера дисковода и дорожки будут выданы позже, командой 033 0023(0024).
 */
void disk_io (int ctlr, uint32 cmd)
{
    KMD *c = &controller [ctlr];
    int cnum = c - controller;
    uint32 rem = cmd & ~(DISK_PAGE_MODE | DISK_PAGE | DISK_BLOCK | DISK_READ | DISK_READ_SYSDATA);
    if (rem && md_dev[ctlr * 4].dctrl & DEB_RWR) {
        besm6_debug ("::: КМД %c: unknown bits in IO request %08o", ctlr + '3', rem);
    }
    c->op = cmd;
    c->format = 0;
    if (c->op & DISK_PAGE_MODE) {
        /* Обмен страницей */
        c->memory = (cmd & DISK_PAGE) >> 2 | (cmd & DISK_BLOCK) >> 8;
    } else {
        /* Обмен половиной страницы (дорожкой) */
        c->memory = (cmd & (DISK_PAGE | DISK_HALFPAGE)) >> 2 | (cmd & DISK_BLOCK) >> 8;
    }
    if (md_dev[ctlr * 4].dctrl & DEB_RWR)
        besm6_debug ("::: КМД %c: задание на %s %016llo RAM @%05o", ctlr + '3',
                     (c->op & DISK_READ) ? "чтение" : "запись", cmd, c->memory);
    disk_fail &= ~c->mask_fail;

    /* Гасим главный регистр прерываний. */
    GRP &= ~c->mask_grp;
}

/*
 * Управление диском: команда 00 033 0023(0024).
 */
void disk_ctl (int ctlr, uint32 cmd)
{
    KMD *c = &controller [ctlr];
    UNIT *u = &md_unit [c->dev];

    if ((md_dev[ctlr].dctrl & DEB_OPS || c->dev != -1 && u->dptr->dctrl & DEB_OPS) && cmd & BBIT(13)) {
        besm6_debug ("::: КМД %c: bit 13 + %04o",
                     ctlr + '3', cmd & 07777);
    }

    if (cmd & BBIT(12)) {
        if (c->dev == -1)
            besm6_debug("Setting block address for unknown device");

        /* Выдача в КМД адреса дорожки.
         * Здесь же выполняем обмен с диском.
         * Номер дисковода к этому моменту уже известен. */
        if ((u->dptr->flags & DEV_DIS) || ! (u->flags & UNIT_ATT)) {
            /* Device not attached. */
            disk_fail |= c->mask_fail;
            return;
        }
        if (IS_29MB(u)) {
            c->zone = ((cmd & BITS(11)) << 1) | (c->zone & 1);
        } else {
            c->zone = (cmd >> 1) & BITS(10);
            c->track = cmd & 1;
        }

        if (u->dptr->dctrl & DEB_OPS) {
            if (IS_29MB(u))
                besm6_debug ("::: КМД %c: cmd %08o = выдача адреса дорожки %04o",
                         ctlr + '3', cmd, c->zone);
            else
                besm6_debug ("::: КМД %c: cmd %08o = выдача адреса дорожки %04o.%d",
                         ctlr + '3', cmd, c->zone, c->track);
        }
        disk_fail &= ~c->mask_fail;
        if (c->op & DISK_READ) {
            if (IS_29MB(u) || c->op & DISK_PAGE_MODE)
                disk_read (u);
            else
                disk_read_track (u);
        } else {
            if (u->flags & UNIT_RO) {
                /* Read only. */
                /*longjmp (cpu_halt, SCPE_RO);*/
                disk_fail |= c->mask_fail;
                return;
            }
            if (c->format)
                disk_format (u);
            else if (IS_29MB(u) || c->op & DISK_PAGE_MODE)
                disk_write (u);
            else
                disk_write_track (u);
        }

        /* Ждём события от устройства. */
        sim_activate (u, 20*USEC);      /* Ускорим для отладки. */

    } else if (cmd & BBIT(11)) {
        /* Выбора номера устройства и занесение в регистр маски КМД.
         * Бит 8 - устройство 0, бит 7 - устройство 1, ... бит 1 - устройство 7.
         * Также установлен бит 9 - что он означает? */
        if      (cmd & BBIT(8)) c->dev = 7;
        else if (cmd & BBIT(7)) c->dev = 6;
        else if (cmd & BBIT(6)) c->dev = 5;
        else if (cmd & BBIT(5)) c->dev = 4;
        else if (cmd & BBIT(4)) c->dev = 3;
        else if (cmd & BBIT(3)) c->dev = 2;
        else if (cmd & BBIT(2)) c->dev = 1;
        else if (cmd & BBIT(1)) c->dev = 0;
        else if (cmd != BBIT(11)) {
            /* Неверная маска выбора устройства. */
            c->dev = -1;
            besm6_debug("Bad unit selection command %o", cmd);
            return;
        } else {
            c->dev = -1;
            return;
        }
        c->dev += ctlr * 32 + c->group * 8;
        u = &md_unit [c->dev];
        if (IS_29MB(u)) {
            c->zone = (c->zone & ~1) | (cmd & BBIT(10) ? 1 : 0);
        }
        u = &md_unit[c->dev];

        if (u->dptr->dctrl & DEB_OPS)
            besm6_debug ("::: КМД %c: cmd = %08o, выбор устройства %02o",
                         ctlr + '3', cmd, c->dev);

        if ((u->dptr->flags & DEV_DIS) || ! (u->flags & UNIT_ATT)) {
            /* Device not attached. */
            disk_fail |= c->mask_fail;
            GRP &= ~c->mask_grp;
        }
        GRP |= c->mask_grp;

    } else if (cmd & BBIT(9)) {
        /* Group selection, LSB of track #, interrupt */
        if ((cmd & 01774) == 01400) {
            int prev = c->group;
            c->group = cmd & 3;
            c->dev = (c->dev & ~030) | (c->group << 3);
            if (u->dptr->dctrl & DEB_OPS && c->group != prev)
                besm6_debug ("::: КМД %c: selected group %d",
                             ctlr + '3', c->group);
        }        
        GRP |= c->mask_grp;
    } else if (cmd & BBIT(8)) {
        besm6_debug ("::: КМД %c: cmd = %08o\n",
                     ctlr + '3', cmd);

    } else {
        /* Команда, выдаваемая в КМД. */
        switch (cmd & 077) {
        case 000: /* диспак выдаёт эту команду один раз в начале загрузки */
            if (u->dptr->dctrl & DEB_OPS)
                besm6_debug ("::: КМД %c: недокументированная команда %08o",
                             ctlr + '3', cmd);
            break;
        case 001: /* сброс на 0 цилиндр */
#if 1
            if (u->dptr->dctrl & DEB_OPS)
                besm6_debug ("::: КМД %c: сброс на 0 цилиндр",
                             ctlr + '3');
#endif
            break;
        case 002: /* подвод */
            if (u->dptr->dctrl & DEB_OPS)
                besm6_debug ("::: КМД %c: подвод", ctlr + '3');
            break;
        case 003: /* чтение (НСМД-МОЗУ) */
        case 043: /* резервной дорожки */
#if 1
            if (u->dptr->dctrl & DEB_OPS)
                besm6_debug ("::: КМД %c: чтение", ctlr + '3');
#endif
            break;
        case 004: /* запись (МОЗУ-НСМД) */
        case 044: /* резервной дорожки */
#if 1
            if (u->dptr->dctrl & DEB_OPS)
                besm6_debug ("::: КМД %c: запись", ctlr + '3');
#endif
            break;
        case 005: /* разметка */
            c->format = 1;
            break;
        case 006: /* сравнение кодов (МОЗУ-НСМД) */
#if 1
            if (u->dptr->dctrl & DEB_OPS)
                besm6_debug ("::: КМД %c: сравнение кодов", ctlr + '3');
#endif
            break;
        case 007: /* чтение заголовка */
        case 047: /* резервной дорожки */
            if (u->dptr->dctrl & DEB_OPS)
                besm6_debug ("::: КМД %c: чтение %s заголовка", ctlr + '3',
                             cmd & 040 ? "резервного" : "");
            disk_fail &= ~c->mask_fail;
            disk_read_header (u);

            /* Ждём события от устройства. */
            sim_activate (u, 20*USEC);      /* Ускорим для отладки. */
            break;
        case 010: /* гашение PC */
#if 1
            if (u->dptr->dctrl & DEB_OPS)
                besm6_debug ("::: КМД %c: гашение регистра состояния",
                             ctlr + '3');
#endif
            c->status = 0;
            break;
        case 011: /* опрос 1÷12 разрядов PC */
            c->status = 0;
            if (md_unit[c->dev].flags & UNIT_ATT)
                c->status = STATUS_READY;
#if 1
            if (u->dptr->dctrl & DEB_STA)
                besm6_debug ("::: КМД %c: опрос младших разрядов состояния - %04o",
                             ctlr + '3', c->status);
#endif
            break;
        case 031: /* опрос 13÷24 разрядов РС */
            c->status = 0;
            if (md_unit[c->dev].flags & UNIT_DISABLE)
                c->status |= STATUS_ABSENT;
            else if (md_unit[c->dev].flags & UNIT_ATT)
                c->status |= STATUS_POWERUP;
            if (md_unit[c->dev].flags & UNIT_RO)
                c->status |= STATUS_READONLY;
            c->status >>= 12;
#if 1
            if (u->dptr->dctrl & DEB_STA)
                besm6_debug ("::: КМД %c: опрос старших разрядов состояния - %04o",
                             ctlr + '3', c->status);
#endif
            break;
        case 050: /* освобождение НМД */
#if 1
            if (u->dptr->dctrl & DEB_OPS)
                besm6_debug ("::: КМД %c: освобождение накопителя",
                             ctlr + '3');
#endif
            break;
        default:
            besm6_debug ("::: КМД %c: неизвестная команда %02o",
                         ctlr + '3', cmd & 077);
            GRP |= c->mask_grp;     /* чтобы не зависало */
            break;
        }
    }
}

/*
 * Запрос состояния контроллера.
 */
int disk_state (int ctlr)
{
    KMD *c = &controller [ctlr];
    if (md_dev[ctlr*4].dctrl & DEB_RRD ||
        md_dev[ctlr*4+1].dctrl & DEB_RRD ||
        md_dev[ctlr*4+2].dctrl & DEB_RRD ||
        md_dev[ctlr*4+3].dctrl & DEB_RRD)
        besm6_debug ("::: КМД %c: опрос состояния = %04o",
                     ctlr + '3', c->status);
    return c->status;
}

/*
 * Событие: закончен обмен с МД.
 * Устанавливаем флаг прерывания.
 */
t_stat disk_event (UNIT *u)
{
    KMD *c = unit_to_ctlr (u);

    GRP |= c->mask_grp;
    return SCPE_OK;
}

/*
 * Опрос ошибок обмена командой 033 4035.
 */
int disk_errors ()
{
#if 0
    if (u->dptr->dctrl & DEB_RRD)
        besm6_debug ("::: КМД: опрос шкалы ошибок = %04o", disk_fail);
#endif
    return disk_fail;
}
