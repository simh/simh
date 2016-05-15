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
 * "Хороший" статус чтения/записи.
 * Вычислено по текстам ОС Дубна.
 * Диспак доволен.
 */
#define STATUS_GOOD     014000400

/*
 * Total size of a disk in blocks, including hidden blocks
 */
#define DISK_TOTBLK     01767

/*
 * Параметры обмена с внешним устройством.
 */
typedef struct {
    int op;                         /* Условное слово обмена */
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
 * disk_dev     DISK device descriptor
 * disk_unit    DISK unit descriptor
 * disk_reg     DISK register list
 */
UNIT disk_unit [16] = {
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
    { UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
};

REG disk_reg[] = {
    { ORDATA   ( "КУС_0",      controller[0].op,      24) },
    { ORDATA   ( "УСТР_0",     controller[0].dev,      3) },
    { ORDATA   ( "ЗОНА_0",     controller[0].zone,    10) },
    { ORDATA   ( "ДОРОЖКА_0",  controller[0].track,    2) },
    { ORDATA   ( "МОЗУ_0",     controller[0].memory,  20) },
    { ORDATA   ( "РС_0",       controller[0].status,  24) },
    { ORDATA   ( "КУС_1",      controller[1].op,      24) },
    { ORDATA   ( "УСТР_1",     controller[1].dev,      3) },
    { ORDATA   ( "ЗОНА_1",     controller[1].zone,    10) },
    { ORDATA   ( "ДОРОЖКА_1",  controller[1].track,    2) },
    { ORDATA   ( "МОЗУ_1",     controller[1].memory,  20) },
    { ORDATA   ( "РС_1",       controller[1].status,  24) },
    { ORDATA   ( "ОШ",         disk_fail,              6) },
    { 0 }
};

MTAB disk_mod[] = {
    { 0 }
};

t_stat disk_reset (DEVICE *dptr);
t_stat disk_attach (UNIT *uptr, CONST char *cptr);
t_stat disk_detach (UNIT *uptr);

DEVICE disk_dev = {
    "DISK", disk_unit, disk_reg, disk_mod,
    16, 8, 21, 1, 8, 50,
    NULL, NULL, &disk_reset, NULL, &disk_attach, &disk_detach,
    NULL, DEV_DISABLE | DEV_DEBUG
};

/*
 * Определение контроллера по устройству.
 */
static KMD *unit_to_ctlr (UNIT *u)
{
    if (u < &disk_unit[8])
        return &controller[0];
    else
        return &controller[1];
}

/*
 * Reset routine
 */
t_stat disk_reset (DEVICE *dptr)
{
    int i;

    memset (&controller, 0, sizeof (controller));
    controller[0].sysdata = &memory [030];
    controller[1].sysdata = &memory [040];
    controller[0].mask_grp = GRP_CHAN3_FREE;
    controller[1].mask_grp = GRP_CHAN4_FREE;
    controller[0].mask_fail = 020;
    controller[1].mask_fail = 010;
    for (i=0; i<16; ++i)
        sim_cancel (&disk_unit[i]);
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
            const char *pos;
            /* Using the rightmost sequence of digits within the filename
             * as a volume number, e.g. "/var/tmp/besm6/2052.bin" -> 2052
             */
            pos = cptr + strlen(cptr);
            while (pos > cptr && !isdigit(*--pos));
            while (pos > cptr && isdigit(*pos)) --pos;
            if (!isdigit(*pos)) ++pos;
            diskno = atoi(pos);
            if (diskno < 2048 || diskno > 4095) {
                if (diskno == 0)
                    sim_printf ("%s: filename must contain volume number 2048..4095\n", sim_uname(u));
                else
                    sim_printf ("%s: disk volume %d from filename %s invalid (must be 2048..4095)\n",
                                sim_uname (u), diskno, cptr);
                /* unlink (cptr); ??? */
                return SCPE_ARG;
            }
            if (!sim_quiet && !(sim_switches & SWMASK ('Q')))
                sim_printf ("%s: formatting disk volume %d\n", sim_uname (u), diskno);

            control[1] = SET_PARITY(0, PARITY_NUMBER);
            control[2] = SET_PARITY(0, PARITY_NUMBER);
            control[3] = SET_PARITY(0, PARITY_NUMBER);

            control[1] |= 01370707LL << 24;    /* Magic mark */
            control[1] |= diskno << 12;

            for (blkno = 0; blkno < DISK_TOTBLK; ++blkno) {
                control[0] = SET_PARITY((t_value)(2*blkno) << 36, PARITY_NUMBER);
                sim_fwrite(control, sizeof(t_value), 4, u->fileref);
                control[0] = SET_PARITY((t_value)(2*blkno+1) << 36, PARITY_NUMBER);
                sim_fwrite(control, sizeof(t_value), 4, u->fileref);
                for (word = 0; word < 02000; ++word) {
                    sim_fwrite(control+2, sizeof(t_value), 1, u->fileref);
                }
            }
            return SCPE_OK;
        }
        if (s == SCPE_OK ||
            (saved_switches & SWMASK ('E')) ||
            (sim_switches & SWMASK('N')))
            return s;
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

    if (disk_dev.dctrl)
        besm6_debug ("::: запись МД %o зона %04o память %05o-%05o",
                     c->dev, c->zone, c->memory, c->memory + 1023);
    fseek (u->fileref, ZONE_SIZE * c->zone * 8, SEEK_SET);
    sim_fwrite (c->sysdata, 8, 8, u->fileref);
    sim_fwrite (&memory [c->memory], 8, 1024, u->fileref);
    if (ferror (u->fileref))
        longjmp (cpu_halt, SCPE_IOERR);
}

void disk_write_track (UNIT *u)
{
    KMD *c = unit_to_ctlr (u);

    if (disk_dev.dctrl)
        besm6_debug ("::: запись МД %o полузона %04o.%d память %05o-%05o",
                     c->dev, c->zone, c->track, c->memory, c->memory + 511);
    fseek (u->fileref, (ZONE_SIZE*c->zone + 4*c->track) * 8, SEEK_SET);
    sim_fwrite (c->sysdata + 4*c->track, 8, 4, u->fileref);
    fseek (u->fileref, (8 + ZONE_SIZE*c->zone + 512*c->track) * 8,
           SEEK_SET);
    sim_fwrite (&memory [c->memory], 8, 512, u->fileref);
    if (ferror (u->fileref))
        longjmp (cpu_halt, SCPE_IOERR);
}

/*
 * Форматирование дорожки.
 */
void disk_format (UNIT *u)
{
    KMD *c = unit_to_ctlr (u);
    t_value fmtbuf[5], *ptr;
    int i;

    /* По сути, эмулятору ничего делать не надо. */
    if (! disk_dev.dctrl)
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
    for (i=0; i<4; i++)
        fmtbuf[i] = ((fmtbuf[i] & BITS48) << 5) |
            ((fmtbuf[i+1] >> 40) & BITS(5));

    /* Печатаем идентификатор, адрес и контрольную сумму адреса. */
    besm6_debug ("::: формат МД %o полузона %04o.%d память %05o и-а-кса %010o %010o",
                 c->dev, c->zone, c->track, c->memory,
                 (int) (fmtbuf[0] >> 8 & BITS(30)),
                 (int) (fmtbuf[2] >> 14 & BITS(30)));
    /* log_data (fmtbuf, 4); */
}

/*
 * Чтение с диска.
 */
void disk_read (UNIT *u)
{
    KMD *c = unit_to_ctlr (u);

    if (disk_dev.dctrl)
        besm6_debug ((c->op & DISK_READ_SYSDATA) ?
                     "::: чтение МД %o зона %04o служебные слова" :
                     "::: чтение МД %o зона %04o память %05o-%05o",
                     c->dev, c->zone, c->memory, c->memory + 1023);
    fseek (u->fileref, ZONE_SIZE * c->zone * 8, SEEK_SET);
    if (sim_fread (c->sysdata, 8, 8, u->fileref) != 8) {
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

    if (disk_dev.dctrl)
        besm6_debug ((c->op & DISK_READ_SYSDATA) ?
                     "::: чтение МД %o полузона %04o.%d служебные слова" :
                     "::: чтение МД %o полузона %04o.%d память %05o-%05o",
                     c->dev, c->zone, c->track, c->memory, c->memory + 511);
    fseek (u->fileref, (ZONE_SIZE*c->zone + 4*c->track) * 8, SEEK_SET);
    if (sim_fread (c->sysdata + 4*c->track, 8, 4, u->fileref) != 4) {
        /* Чтение неинициализированного диска */
        disk_fail |= c->mask_fail;
        return;
    }
    if (! (c->op & DISK_READ_SYSDATA)) {
        fseek (u->fileref, (8 + ZONE_SIZE*c->zone + 512*c->track) * 8,
               SEEK_SET);
        if (sim_fread (&memory [c->memory], 8, 512, u->fileref) != 512) {
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
    t_value *sysdata = c->sysdata + 4*c->track;
    int iaksa, i, cyl, head;

    /* Адрес: номер цилиндра и головки. */
    head = (c->zone << 1) + c->track;
    cyl = head / 10;
    head %= 10;
    iaksa = (cyl << 20) | (head << 16);

    /* Идентификатор дорожки замены. */
    if (c->zone >= 01750)
        iaksa |= BBIT(30);

    /* Контрольная сумма адреса с переносом вправо. */
    iaksa |= BITS(12) & ~sum_with_right_carry (iaksa >> 12, iaksa >> 24);

    /* Амиакса, 42 нуля, амиакса, много единиц. */
    sysdata[0] = 07404000000000000LL | (t_value) iaksa << 8;
    sysdata[1] = 03740LL;
    sysdata[2] = 00400000000037777LL | (t_value) iaksa << 14;
    sysdata[3] = BITS48;
    if (disk_dev.dctrl)
        log_data (sysdata, 4);

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

    c->op = cmd;
    c->format = 0;
    if (c->op & DISK_PAGE_MODE) {
        /* Обмен страницей */
        c->memory = (cmd & DISK_PAGE) >> 2 | (cmd & DISK_BLOCK) >> 8;
    } else {
        /* Обмен половиной страницы (дорожкой) */
        c->memory = (cmd & (DISK_PAGE | DISK_HALFPAGE)) >> 2 | (cmd & DISK_BLOCK) >> 8;
    }
#if 0
    if (disk_dev.dctrl)
        besm6_debug ("::: КМД %c: задание на %s %08o", ctlr + '3',
                     (c->op & DISK_READ) ? "чтение" : "запись", cmd);
#endif
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
    UNIT *u = &disk_unit [c->dev];

    if (cmd & BBIT(12)) {
        /* Выдача в КМД адреса дорожки.
         * Здесь же выполняем обмен с диском.
         * Номер дисковода к этому моменту уже известен. */
        if ((disk_dev.flags & DEV_DIS) || ! (u->flags & UNIT_ATT)) {
            /* Device not attached. */
            disk_fail |= c->mask_fail;
            return;
        }
        c->zone = (cmd >> 1) & BITS(10);
        c->track = cmd & 1;
#if 0
        if (disk_dev.dctrl)
            besm6_debug ("::: КМД %c: выдача адреса дорожки %04o.%d",
                         ctlr + '3', c->zone, c->track);
#endif
        disk_fail &= ~c->mask_fail;
        if (c->op & DISK_READ) {
            if (c->op & DISK_PAGE_MODE)
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
            else if (c->op & DISK_PAGE_MODE)
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
        else {
            /* Неверная маска выбора устройства. */
            c->dev = -1;
            return;
        }
        c->dev += ctlr << 3;
        u = &disk_unit[c->dev];
#if 0
        if (disk_dev.dctrl)
            besm6_debug ("::: КМД %c: выбор устройства %d",
                         ctlr + '3', c->dev);
#endif
        if ((disk_dev.flags & DEV_DIS) || ! (u->flags & UNIT_ATT)) {
            /* Device not attached. */
            disk_fail |= c->mask_fail;
            GRP &= ~c->mask_grp;
        }
        GRP |= c->mask_grp;

    } else if (cmd & BBIT(9)) {
        /* Проверка прерывания от КМД? */
#if 0
        if (disk_dev.dctrl)
            besm6_debug ("::: КМД %c: проверка готовности",
                         ctlr + '3');
#endif
        GRP |= c->mask_grp;

    } else {
        /* Команда, выдаваемая в КМД. */
        switch (cmd & 077) {
        case 000: /* диспак выдаёт эту команду один раз в начале загрузки */
#if 0
            if (disk_dev.dctrl)
                besm6_debug ("::: КМД %c: недокументированная команда 00",
                             ctlr + '3');
#endif
            break;
        case 001: /* сброс на 0 цилиндр */
#if 0
            if (disk_dev.dctrl)
                besm6_debug ("::: КМД %c: сброс на 0 цилиндр",
                             ctlr + '3');
#endif
            break;
        case 002: /* подвод */
            if (disk_dev.dctrl)
                besm6_debug ("::: КМД %c: подвод", ctlr + '3');
            break;
        case 003: /* чтение (НСМД-МОЗУ) */
        case 043: /* резервной дорожки */
#if 0
            if (disk_dev.dctrl)
                besm6_debug ("::: КМД %c: чтение", ctlr + '3');
#endif
            break;
        case 004: /* запись (МОЗУ-НСМД) */
        case 044: /* резервной дорожки */
#if 0
            if (disk_dev.dctrl)
                besm6_debug ("::: КМД %c: запись", ctlr + '3');
#endif
            break;
        case 005: /* разметка */
            c->format = 1;
            break;
        case 006: /* сравнение кодов (МОЗУ-НСМД) */
#if 0
            if (disk_dev.dctrl)
                besm6_debug ("::: КМД %c: сравнение кодов", ctlr + '3');
#endif
            break;
        case 007: /* чтение заголовка */
        case 047: /* резервной дорожки */
            if (disk_dev.dctrl)
                besm6_debug ("::: КМД %c: чтение %s заголовка", ctlr + '3',
                             cmd & 040 ? "резервного" : "");
            disk_fail &= ~c->mask_fail;
            disk_read_header (u);

            /* Ждём события от устройства. */
            sim_activate (u, 20*USEC);      /* Ускорим для отладки. */
            break;
        case 010: /* гашение PC */
#if 0
            if (disk_dev.dctrl)
                besm6_debug ("::: КМД %c: гашение регистра состояния",
                             ctlr + '3');
#endif
            c->status = 0;
            break;
        case 011: /* опрос 1÷12 разрядов PC */
#if 0
            if (disk_dev.dctrl)
                besm6_debug ("::: КМД %c: опрос младших разрядов состояния",
                             ctlr + '3');
#endif
            if (disk_unit[c->dev].flags & UNIT_ATT)
                c->status = STATUS_GOOD & BITS(12);
            else
                c->status = 0;
            break;
        case 031: /* опрос 13÷24 разрядов РС */
#if 0
            if (disk_dev.dctrl)
                besm6_debug ("::: КМД %c: опрос старших разрядов состояния",
                             ctlr + '3');
#endif
            if (disk_unit[c->dev].flags & UNIT_ATT)
                c->status = (STATUS_GOOD >> 12) & BITS(12);
            else
                c->status = 0;
            break;
        case 050: /* освобождение НМД */
#if 0
            if (disk_dev.dctrl)
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
#if 0
    if (disk_dev.dctrl)
        besm6_debug ("::: КМД %c: опрос состояния = %04o",
                     ctlr + '3', c->status);
#endif
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
    if (disk_dev.dctrl)
        besm6_debug ("::: КМД: опрос шкалы ошибок = %04o", disk_fail);
#endif
    return disk_fail;
}
