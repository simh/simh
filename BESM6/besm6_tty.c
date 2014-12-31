/*
 * besm6_tty.c: BESM-6 teletype device
 *
 * Copyright (c) 2009, Leo Broukhis
 * Copyright (c) 2009, Serge Vakulenko
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You can redistribute this program and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your discretion) any later version.
 * See the accompanying file "COPYING" for more details.
 */

#include "besm6_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <time.h>

#define TTY_MAX         24              /* Количество последовательных терминалов */
#define LINES_MAX       TTY_MAX + 2     /* Включая параллельные интерфейсы "Консулов" */
/*
 * Согласно таблице в http://ru.wikipedia.org/wiki/МТК-2
 */
char * rus[] = { 0, "Т", "\r", "О", " ", "Х", "Н", "М", "\n", "Л", "Р", "Г", "И", "П", "Ц", "Ж",
                 "Е", "З", "Д", "Б", "С", "Ы", "Ф", "Ь", "А", "В", "Й", 0, "У", "Я", "К", 0 };

char * lat[] = { 0, "T", "\r", "O", " ", "H", "N", "M", "\n", "L", "R", "G", "I", "P", "C", "V",
                 "E", "Z", "D", "B", "S", "Y", "F", "X", "A", "W", "J", 0, "U", "Q", "K", 0 };

/* $ = Кто там? */
char * dig[] = { 0, "5", "\r", "9", " ", "Щ", ",", ".", "\n", ")", "4", "Ш", "8", "0", ":", "=",
                 "3", "+", "$", "?", "'", "6", "Э", "/", "-", "2", "Ю", 0, "7", "1", "(", 0 };

char ** reg = 0;

char *  process (int sym)
{
    /* Требуется инверсия */
    sym ^= 31;
    switch (sym) {
    case 0:
        reg = rus;
        break;
    case 27:
        reg = dig;
        break;
    case 31:
        reg = lat;
        break;
    default:
        return reg[sym];
    }
    return "";
}

/* Только для последовательных линий */
int tty_active [TTY_MAX+1], tty_sym [TTY_MAX+1];
int tty_typed [TTY_MAX+1], tty_instate [TTY_MAX+1];
time_t tty_last_time [TTY_MAX+1];
int tty_idle_count [TTY_MAX+1];

uint32 vt_sending, vt_receiving;
uint32 tt_sending, tt_receiving;

// Attachments survive the reset
uint32 tt_mask = 0, vt_mask = 0;

uint32 TTY_OUT = 0, TTY_IN = 0, vt_idle = 0;
uint32 CONSUL_IN[2];

uint32 CONS_CAN_PRINT[2] = { 01000, 00400 };
uint32 CONS_HAS_INPUT[2] = { 04000, 02000 };

/* Буфера командных строк для режима telnet. */
char vt_cbuf [CBUFSIZE] [LINES_MAX+1];
char *vt_cptr [LINES_MAX+1];

void tt_print();
void consul_receive();
t_stat vt_clk(UNIT *);
extern char *get_sim_sw (char *cptr);
extern int32 tmr_poll;                              /* calibrated clock timer poll */

UNIT tty_unit [] = {
    { UDATA (vt_clk, UNIT_DIS|UNIT_IDLE, 0) },       /* fake unit, clock */
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    /* The next two units are parallel interface */
    { UDATA (NULL, UNIT_SEQ, 0) },
    { UDATA (NULL, UNIT_SEQ, 0) },
    { 0 }
};

REG tty_reg[] = {
    { 0 }
};

/*
 * Дескрипторы линий для мультиплексора TMXR.
 * Поле .conn содержит номер сокета и означает занятую линию.
 * Для локальных терминалов делаем .conn = 1.
 * Чтобы нумерация линий совпадала с нумерацией терминалов
 * (с единицы), нулевую линию держим занятой (.conn = 1).
 * Поле .rcve устанавливается в 1 для сетевых соединений.
 * Для локальных терминалов оно равно 0.
 */
TMLN tty_line [LINES_MAX+1];
TMXR tty_desc = { LINES_MAX+1, 0, 0, tty_line };        /* mux descriptor */

#define TTY_UNICODE_CHARSET     0
#define TTY_KOI7_JCUKEN_CHARSET (1<<UNIT_V_UF)
#define TTY_KOI7_QWERTY_CHARSET (2<<UNIT_V_UF)
#define TTY_CHARSET_MASK        (3<<UNIT_V_UF)
#define TTY_OFFLINE_STATE       0
#define TTY_TELETYPE_STATE      (1<<(UNIT_V_UF+2))
#define TTY_VT340_STATE         (2<<(UNIT_V_UF+2))
#define TTY_CONSUL_STATE        (3<<(UNIT_V_UF+2))
#define TTY_STATE_MASK          (3<<(UNIT_V_UF+2))
#define TTY_DESTRUCTIVE_BSPACE  0
#define TTY_AUTHENTIC_BSPACE    (1<<(UNIT_V_UF+4))
#define TTY_BSPACE_MASK         (1<<(UNIT_V_UF+4))
#define TTY_CMDLINE_MASK        (1<<(UNIT_V_UF+5))

t_stat tty_reset (DEVICE *dptr)
{
    memset(tty_active, 0, sizeof(tty_active));
    memset(tty_sym, 0, sizeof(tty_sym));
    memset(tty_typed, 0, sizeof(tty_typed));
    memset(tty_instate, 0, sizeof(tty_instate));
    vt_sending = vt_receiving = 0;
    TTY_IN = TTY_OUT = 0;
    CONSUL_IN[0] = CONSUL_IN[1] = 0;
    reg = rus;
    vt_idle = 1;
    tty_line[0].conn = 1;                   /* faked, always busy */
    /* Готовность устройства в READY2 инверсная, а устройство всегда готово */
    /* Провоцируем передачу */
    PRP |= CONS_CAN_PRINT[0] | CONS_CAN_PRINT[1];
    return sim_clock_coschedule (tty_unit, 5*tmr_poll);
}

/* 19 р ГРП, 300 Гц */
t_stat vt_clk (UNIT * this)
{
    int num;

    /* Телетайпы работают на 10 бод */
    static int clk_divider = 1<<29;
    GRP |= MGRP & BBIT(19);

    /* Опрашиваем сокеты на приём. */
    tmxr_poll_rx (&tty_desc);

    vt_print();
    vt_receive();
    consul_receive();

    if (! (clk_divider >>= 1)) {
        tt_print();
        /* Прием не реализован */
        clk_divider = 1<<29;
    }

    /* Есть новые сетевые подключения? */
    num = tmxr_poll_conn (&tty_desc);
    if (num > 0 && num <= LINES_MAX) {
        char buf [80];
        TMLN *t = &tty_line [num];
        besm6_debug ("*** tty%d: новое подключение от %s",
                     num, t->ipad);
        t->rcve = 1;
        tty_unit[num].flags &= ~TTY_STATE_MASK;
        tty_unit[num].flags |= TTY_VT340_STATE;
        if (num <= TTY_MAX)
            vt_mask |= 1 << (TTY_MAX - num);

        switch (tty_unit[num].flags & TTY_CHARSET_MASK) {
        case TTY_KOI7_JCUKEN_CHARSET:
            tmxr_linemsg (t, "Encoding is KOI-7 (jcuken)\r\n");
            break;
        case TTY_KOI7_QWERTY_CHARSET:
            tmxr_linemsg (t, "Encoding is KOI-7 (qwerty)\r\n");
            break;
        case TTY_UNICODE_CHARSET:
            tmxr_linemsg (t, "Encoding is UTF-8\r\n");
            break;
        }
        tty_idle_count[num] = 0;
        tty_last_time[num] = time (0);
        sprintf (buf, "%.24s from %s\r\n",
                 ctime (&tty_last_time[num]),
                 t->ipad);
        tmxr_linemsg (t, buf);

        /* Ввод ^C, чтобы получить приглашение. */
        t->rxb [t->rxbpi++] = '\3';
    }

    /* Опрашиваем сокеты на передачу. */
    tmxr_poll_tx (&tty_desc);

    return sim_clock_coschedule (this, 5*tmr_poll);
}

t_stat tty_setmode (UNIT *u, int32 val, char *cptr, void *desc)
{
    int num = u - tty_unit;
    TMLN *t = &tty_line [num];
    uint32 mask = 1 << (TTY_MAX - num);

    switch (val & TTY_STATE_MASK) {
    case TTY_OFFLINE_STATE:
        if (t->conn) {
            if (t->rcve) {
                tmxr_reset_ln (t);
                t->rcve = 0;
            } else
                t->conn = 0;
            if (num <= TTY_MAX) {
                tty_sym[num] =
                    tty_active[num] =
                    tty_typed[num] =
                    tty_instate[num] = 0;
                vt_mask &= ~mask;
                tt_mask &= ~mask;
            }
        }
        break;
    case TTY_TELETYPE_STATE:
        if (num > TTY_MAX)
            return SCPE_NXPAR;
        t->conn = 1;
        t->rcve = 0;
        tt_mask |= mask;
        vt_mask &= ~mask;
        break;
    case TTY_VT340_STATE:
        t->conn = 1;
        t->rcve = 0;
        if (num <= TTY_MAX) {
            vt_mask |= mask;
            tt_mask &= ~mask;
        }
        break;
    case TTY_CONSUL_STATE:
        if (num <= TTY_MAX)
            return SCPE_NXPAR;
        t->conn = 1;
        t->rcve = 0;
        break;
    }
    return SCPE_OK;
}

/*
 * Разрешение подключения к терминалам через telnet.
 * Делается командой:
 *      attach tty <порт>
 * Здесь <порт> - номер порта telnet, например 4199.
 */
t_stat tty_attach (UNIT *u, char *cptr)
{
    int num = u - tty_unit;
    int r, m, n;

    if (*cptr >= '0' && *cptr <= '9') {
        /* Сохраняем и восстанавливаем все .conn,
         * так как tmxr_attach() их обнуляет. */
        for (m=0, n=1; n<=LINES_MAX; ++n)
            if (tty_line[n].conn)
                m |= 1 << (LINES_MAX-n);
        /* Неважно, какой номер порта указывать в команде задания
         * порта telnet. Можно tty, можно tty1 - без разницы. */
        r = tmxr_attach (&tty_desc, &tty_unit[0], cptr);
        for (n=1; n<=LINES_MAX; ++n)
            if (m >> (LINES_MAX-n) & 1)
                tty_line[n].conn = 1;
        return r;
    }
    if (strcmp (cptr, "/dev/tty") == 0) {
        /* Консоль. */
        u->flags &= ~TTY_STATE_MASK;
        u->flags |= TTY_VT340_STATE;
        tty_line[num].conn = 1;
        tty_line[num].rcve = 0;
        if (num <= TTY_MAX)
            vt_mask |= 1 << (TTY_MAX - num);
        besm6_debug ("*** консоль на T%03o", num);
        return 0;
    }
    if (strcmp (cptr, "/dev/null") == 0) {
        /* Запрещаем терминал. */
        tty_line[num].conn = 1;
        tty_line[num].rcve = 0;
        if (num <= TTY_MAX) {
            vt_mask &= ~(1 << (TTY_MAX - num));
            tt_mask &= ~(1 << (TTY_MAX - num));
        }
        besm6_debug ("*** отключение терминала T%03o", num);
        return 0;
    }
    return SCPE_ALATT;
}

t_stat tty_detach (UNIT *u)
{
    return tmxr_detach (&tty_desc, &tty_unit[0]);
}

/*
 * Управление терминалами.
 * set ttyN unicode     - выбор кодировки UTF-8
 * set ttyN jcuken      - выбор кодировки КОИ-7, раскладка йцукен
 * set ttyN qwerty      - выбор кодировки КОИ-7, раскладка яверты
 * set ttyN off         - отключение
 * set ttyN tt          - установка типа терминала "Телетайп"
 * set ttyN vt          - установка типа терминала "Видеотон-340"
 * set ttyN consul      - установка типа терминала "Consul-254"
 * set ttyN destrbs     - "стирающий" backspace
 * set ttyN authbs      - классический backspace
 * set tty disconnect=N - принудительное завершение сеанса telnet
 * show tty             - просмотр режимов терминалов
 * show tty connections - просмотр IP-адресов и времени соединений
 * show tty statistics  - просмотр счетчиков переданных и принятых байтов
 */
MTAB tty_mod[] = {
    { TTY_CHARSET_MASK, TTY_UNICODE_CHARSET, "UTF-8 input",
      "UNICODE" },
    { TTY_CHARSET_MASK, TTY_KOI7_JCUKEN_CHARSET, "KOI7 (jcuken) input",
      "JCUKEN" },
    { TTY_CHARSET_MASK, TTY_KOI7_QWERTY_CHARSET, "KOI7 (qwerty) input",
      "QWERTY" },
    { TTY_STATE_MASK, TTY_OFFLINE_STATE, "offline",
      "OFF", &tty_setmode },
    { TTY_STATE_MASK, TTY_TELETYPE_STATE, "Teletype",
      "TT", &tty_setmode },
    { TTY_STATE_MASK, TTY_VT340_STATE, "Videoton-340",
      "VT", &tty_setmode },
    { TTY_STATE_MASK, TTY_CONSUL_STATE, "Consul-254",
      "CONSUL", &tty_setmode },
    { TTY_BSPACE_MASK, TTY_DESTRUCTIVE_BSPACE, "destructive backspace",
      "DESTRBS" },
    { TTY_BSPACE_MASK, TTY_AUTHENTIC_BSPACE, NULL,
      "AUTHBS" },
    { MTAB_XTD | MTAB_VDV, 1, NULL,
      "DISCONNECT", &tmxr_dscln, NULL, (void*) &tty_desc },
    { UNIT_ATT, UNIT_ATT, "connections",
      NULL, NULL, &tmxr_show_summ, (void*) &tty_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS",
      NULL, NULL, &tmxr_show_cstat, (void*) &tty_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS",
      NULL, NULL, &tmxr_show_cstat, (void*) &tty_desc },
    { MTAB_XTD | MTAB_VUN | MTAB_NC, 0, NULL,
      "LOG", &tmxr_set_log, &tmxr_show_log, (void*) &tty_desc },
    { MTAB_XTD | MTAB_VUN | MTAB_NC, 0, NULL,
      "NOLOG", &tmxr_set_nolog, NULL, (void*) &tty_desc },
    { 0 }
};

DEVICE tty_dev = {
    "TTY", tty_unit, tty_reg, tty_mod,
    27, 2, 1, 1, 2, 1,
    NULL, NULL, &tty_reset, NULL, &tty_attach, &tty_detach,
    NULL, DEV_NET|DEV_DEBUG
};

void tty_send (uint32 mask)
{
    /* besm6_debug ("*** телетайпы: передача %08o", mask); */

    TTY_OUT = mask;
}

/*
 * Выдача символа на терминал с указанным номером.
 */
void vt_putc (int num, int c)
{
    TMLN *t = &tty_line [num];

    if (! t->conn)
        return;
    if (t->rcve) {
        /* Передача через telnet. */
        tmxr_putc_ln (t, c);
    } else {
        /* Вывод на консоль. */
        if (t->txlog) {                 /* log if available */
            fputc (c, t->txlog);
            if (c == '\n')
                fflush (t->txlog);
        }
        fputc (c, stdout);
        fflush (stdout);
    }
}

/*
 * Выдача строки на терминал с указанным номером.
 */
void vt_puts (int num, const char *s)
{
    TMLN *t = &tty_line [num];

    if (! t->conn)
        return;
    if (t->rcve) {
        /* Передача через telnet. */
        tmxr_linemsg (t, (char*) s);
    } else {
        /* Вывод на консоль. */
        if (t->txlog)                   /* log if available */
            fputs (s, t->txlog);
        fputs (s, stdout);
        fflush (stdout);
    }
}

const char * koi7_rus_to_unicode [32] = {
    "Ю", "А", "Б", "Ц", "Д", "Е", "Ф", "Г",
    "Х", "И", "Й", "К", "Л", "М", "Н", "О",
    "П", "Я", "Р", "С", "Т", "У", "Ж", "В",
    "Ь", "Ы", "З", "Ш", "Э", "Щ", "Ч", "\0x7f",
};

void vt_send(int num, uint32 sym, int destructive_bs)
{
    if (sym < 0x60) {
        switch (sym) {
        case '\031':
            /* Up */
            vt_puts (num, "\033[");
            sym = 'A';
            break;
        case '\032':
            /* Down */
            vt_puts (num, "\033[");
            sym = 'B';
            break;
        case '\030':
            /* Right */
            vt_puts (num, "\033[");
            sym = 'C';
            break;
        case '\b':
            /* Left */
            vt_puts (num, "\033[");
            if (destructive_bs) {
                /* Стираем предыдущий символ. */
                vt_puts (num, "D \033[");
            }
            sym = 'D';
            break;
        case '\v':
        case '\033':
        case '\0':
            /* Выдаём управляющий символ. */
            break;
        case '\037':
            /* Очистка экрана */
            vt_puts (num, "\033[H\033[");
            sym = 'J';
            break;
        case '\n':
            /* На VDT-340 также возвращал курсор в 1-ю позицию */
            vt_putc (num, '\r');
            sym = '\n';
            break;
        case '\f':
            /* Сообщение ERR при нажатии некоторых управляющих
             * клавиш выдается с использованием reverse wraparound.
             */
            vt_puts(num, "\033[");
            sym = 'H';
            break;
        case '\r':
        case '\003':
            /* Неотображаемые символы */
            sym = 0;
            break;
        default:
            if (sym < ' ') {
                /* Нефункциональные ctrl-символы были видны в половинной яркости */
                vt_puts (num, "\033[2m");
                vt_putc (num, sym | 0x40);
                vt_puts (num, "\033[");
                /* Завершаем ESC-последовательность */
                sym = 'm';
            }
        }
        if (sym)
            vt_putc (num, sym);
    } else
        vt_puts (num, koi7_rus_to_unicode[sym - 0x60]);
}

/*
 * Обработка выдачи на все подключенные терминалы.
 */
void vt_print()
{
    uint32 workset = (TTY_OUT & vt_mask) | vt_sending;
    int num;

    if (workset == 0) {
        ++vt_idle;
        return;
    }
    for (num = besm6_highest_bit (workset) - TTY_MAX;
         workset; num = besm6_highest_bit (workset) - TTY_MAX) {
        int mask = 1 << (TTY_MAX - num);
        int c = (TTY_OUT & mask) != 0;
        switch (tty_active[num]*2+c) {
        case 0: /* idle */
            besm6_debug ("Warning: inactive ttys should have been screened");
            continue;
        case 1: /* start bit */
            vt_sending |= mask;
            tty_active[num] = 1;
            break;
        case 18: /* stop bit */
            tty_sym[num] = ~tty_sym[num] & 0x7f;
            vt_send (num, tty_sym[num],
                     (tty_unit[num].flags & TTY_BSPACE_MASK) == TTY_DESTRUCTIVE_BSPACE);
            tty_active[num] = 0;
            tty_sym[num] = 0;
            vt_sending &= ~mask;
            break;
        case 19: /* framing error */
            vt_putc (num, '#');
            break;
        default:
            /* little endian ordering */
            if (c) {
                tty_sym[num] |= 1 << (tty_active[num]-1);
            }
            ++tty_active[num];
            break;
        }
        workset &= ~mask;
    }
    vt_idle = 0;
}

/* Ввод с телетайпа не реализован; вывод работает только при использовании
 * модельного времени.
 */
void tt_print()
{
    uint32 workset = (TTY_OUT & tt_mask) | tt_sending;
    int num;

    if (workset == 0) {
        return;
    }

    for (num = besm6_highest_bit (workset) - TTY_MAX;
         workset; num = besm6_highest_bit (workset) - TTY_MAX) {
        int mask = 1 << (TTY_MAX - num);
        int c = (TTY_OUT & mask) != 0;
        switch (tty_active[num]*2+c) {
        case 0: /* idle */
            break;
        case 1: /* start bit */
            tt_sending |= mask;
            tty_active[num] = 1;
            break;
        case 12: /* stop bit */
            vt_puts (num, process (tty_sym[num]));
            tty_active[num] = 0;
            tty_sym[num] = 0;
            tt_sending &= ~mask;
            break;
        case 13: /* framing error */
            vt_putc (num, '#');
            break;
        default:
            /* big endian ordering */
            if (c) {
                tty_sym[num] |= 1 << (5-tty_active[num]);
            }
            ++tty_active[num];
            break;
        }
        workset &= ~mask;
    }
    vt_idle = 0;
}

/*
 * Перекодировка из Unicode в КОИ-7.
 * Если нет соответствия, возвращает -1.
 */
static int unicode_to_koi7 (unsigned val)
{
    if (val <= '_') return val;
    else if ('a' <= val && val <= 'z') return val + 'Z' - 'z';
    else switch (val) {
        case 0x007f:              return 0x7f;
        case 0x0410: case 0x0430: return 0x61;
        case 0x0411: case 0x0431: return 0x62;
        case 0x0412: case 0x0432: return 0x77;
        case 0x0413: case 0x0433: return 0x67;
        case 0x0414: case 0x0434: return 0x64;
        case 0x0415: case 0x0435: return 0x65;
        case 0x0416: case 0x0436: return 0x76;
        case 0x0417: case 0x0437: return 0x7a;
        case 0x0418: case 0x0438: return 0x69;
        case 0x0419: case 0x0439: return 0x6a;
        case 0x041a: case 0x043a: return 0x6b;
        case 0x041b: case 0x043b: return 0x6c;
        case 0x041c: case 0x043c: return 0x6d;
        case 0x041d: case 0x043d: return 0x6e;
        case 0x041e: case 0x043e: return 0x6f;
        case 0x041f: case 0x043f: return 0x70;
        case 0x0420: case 0x0440: return 0x72;
        case 0x0421: case 0x0441: return 0x73;
        case 0x0422: case 0x0442: return 0x74;
        case 0x0423: case 0x0443: return 0x75;
        case 0x0424: case 0x0444: return 0x66;
        case 0x0425: case 0x0445: return 0x68;
        case 0x0426: case 0x0446: return 0x63;
        case 0x0427: case 0x0447: return 0x7e;
        case 0x0428: case 0x0448: return 0x7b;
        case 0x0429: case 0x0449: return 0x7d;
        case 0x042b: case 0x044b: return 0x79;
        case 0x042c: case 0x044c: return 0x78;
        case 0x042d: case 0x044d: return 0x7c;
        case 0x042e: case 0x044e: return 0x60;
        case 0x042f: case 0x044f: return 0x71;
        }
    return -1;
}

/*
 * Set command
 */
static t_stat cmd_set (int32 num, char *cptr)
{
    char gbuf [CBUFSIZE];
    int len;

    cptr = get_sim_sw (cptr);
    if (! cptr)
        return SCPE_INVSW;
    if (! *cptr)
        return SCPE_NOPARAM;
    cptr = get_glyph (cptr, gbuf, 0);
    if (*cptr)
        return SCPE_2MARG;

    len = strlen (gbuf);
    if (strncmp ("UNICODE", gbuf, len) == 0) {
        tty_unit[num].flags &= ~TTY_CHARSET_MASK;
        tty_unit[num].flags |= TTY_UNICODE_CHARSET;
    } else if (strncmp ("JCUKEN", gbuf, len) == 0) {
        tty_unit[num].flags &= ~TTY_CHARSET_MASK;
        tty_unit[num].flags |= TTY_KOI7_JCUKEN_CHARSET;
    } else if (strncmp ("QWERTY", gbuf, len) == 0) {
        tty_unit[num].flags &= ~TTY_CHARSET_MASK;
        tty_unit[num].flags |= TTY_KOI7_QWERTY_CHARSET;
    } else if (strncmp ("TT", gbuf, len) == 0) {
        tty_unit[num].flags &= ~TTY_STATE_MASK;
        tty_unit[num].flags |= TTY_TELETYPE_STATE;
    } else if (strncmp ("VT", gbuf, len) == 0) {
        tty_unit[num].flags &= ~TTY_STATE_MASK;
        tty_unit[num].flags |= TTY_VT340_STATE;
    } else if (strncmp ("CONSUL", gbuf, len) == 0) {
        tty_unit[num].flags &= ~TTY_STATE_MASK;
        tty_unit[num].flags |= TTY_CONSUL_STATE;
    } else if (strncmp ("DESTRBS", gbuf, len) == 0) {
        tty_unit[num].flags &= ~TTY_BSPACE_MASK;
        tty_unit[num].flags |= TTY_DESTRUCTIVE_BSPACE;
    } else if (strncmp ("AUTHBS", gbuf, len) == 0) {
        tty_unit[num].flags &= ~TTY_BSPACE_MASK;
        tty_unit[num].flags |= TTY_AUTHENTIC_BSPACE;
    } else {
        return SCPE_NXPAR;
    }
    return SCPE_OK;
}

/*
 * Show command
 */
static t_stat cmd_show (int32 num, char *cptr)
{
    TMLN *t = &tty_line [num];
    char gbuf [CBUFSIZE];
    MTAB *m;
    int len;

    cptr = get_sim_sw (cptr);
    if (! cptr)
        return SCPE_INVSW;
    if (! *cptr) {
        sprintf (gbuf, "TTY%d", num);
        tmxr_linemsg (t, gbuf);
        for (m=tty_mod; m->mask; m++) {
            if (m->pstring &&
                (tty_unit[num].flags & m->mask) == m->match) {
                tmxr_linemsg (t, ", ");
                tmxr_linemsg (t, m->pstring);
            }
        }
        if (t->txlog)
            tmxr_linemsg (t, ", log");
        tmxr_linemsg (t, "\r\n");
        return SCPE_OK;
    }
    cptr = get_glyph (cptr, gbuf, 0);
    if (*cptr)
        return SCPE_2MARG;

    len = strlen (gbuf);
    if (strncmp ("STATISTICS", gbuf, len) == 0) {
        sprintf (gbuf, "line %d: input queued/total = %d/%d, "
                 "output queued/total = %d/%d\r\n", num,
                 t->rxbpi - t->rxbpr, t->rxcnt,
                 t->txbpi - t->txbpr, t->txcnt);
        tmxr_linemsg (t, gbuf);
    } else {
        return SCPE_NXPAR;
    }
    return SCPE_OK;
}

/*
 * Exit command
 */
static t_stat cmd_exit (int32 num, char *cptr)
{
    return SCPE_EXIT;
}

static t_stat cmd_help (int32 num, char *cptr);

static CTAB cmd_table[] = {
    { "SET", &cmd_set, 0,
      "set unicode              select UTF-8 encoding\r\n"
      "set jcuken               select KOI7 encoding, 'jcuken' keymap\r\n"
      "set qwerty               select KOI7 encoding, 'qwerty' keymap\r\n"
      "set tt                   use Teletype mode\r\n"
      "set vt                   use Videoton-340 mode\r\n"
      "set consul               use Consul-254 mode\r\n"
      "set destrbs              destructive backspace\r\n"
      "set authbs               authentic backspace\r\n"
    },
    { "SHOW", &cmd_show, 0,
      "sh{ow}                   show modes of the terminal\r\n"
      "sh{ow} s{tatistics}      show network statistics\r\n"
    },
    { "EXIT", &cmd_exit, 0,
      "exi{t} | q{uit} | by{e}  exit from simulation\r\n"
    },
    { "QUIT", &cmd_exit, 0, NULL
    },
    { "BYE", &cmd_exit, 0, NULL
    },
    { "HELP", &cmd_help, 0,
      "h{elp}                   type this message\r\n"
      "h{elp} <command>         type help for command\r\n"
    },
    { 0 }
};

/*
 * Find command routine
 */
static CTAB *lookup_cmd (char *command)
{
    CTAB *c;
    int len;

    len = strlen (command);
    for (c=cmd_table; c->name; c++) {
        if (strncmp (command, c->name, len) == 0)
            return c;
    }
    return 0;
}

/*
 * Help command
 */
static t_stat cmd_help (int32 num, char *cptr)
{
    TMLN *t = &tty_line [num];
    char gbuf [CBUFSIZE];
    CTAB *c;

    cptr = get_sim_sw (cptr);
    if (! cptr)
        return SCPE_INVSW;
    if (! *cptr) {
        /* Список всех команд. */
        tmxr_linemsg (t, "Commands may be abbreviated.  Commands are:\r\n\r\n");
        for (c=cmd_table; c && c->name; c++)
            if (c->help)
                tmxr_linemsg (t, c->help);
        return SCPE_OK;
    }
    cptr = get_glyph (cptr, gbuf, 0);
    if (*cptr)
        return SCPE_2MARG;
    c = lookup_cmd (gbuf);
    if (! c)
        return SCPE_ARG;
    /* Описание конкретной команды. */
    tmxr_linemsg (t, c->help);
    return SCPE_OK;
}

/*
 * Выполнение командной строки.
 */
void vt_cmd_exec (int num)
{
    TMLN *t = &tty_line [num];
    char *cptr, gbuf [CBUFSIZE];
    CTAB *cmdp;
    t_stat err;
    extern char *scp_errors[];

    cptr = get_glyph (vt_cbuf [num], gbuf, 0);      /* get command glyph */
    cmdp = lookup_cmd (gbuf);                       /* lookup command */
    if (! cmdp) {
        tmxr_linemsg (t, scp_errors[SCPE_UNK - SCPE_BASE]);
        tmxr_linemsg (t, "\r\n");
        return;
    }
    err = cmdp->action (num, cptr);                 /* if found, exec */
    if (err >= SCPE_BASE) {                         /* error? */
        tmxr_linemsg (t, scp_errors [err - SCPE_BASE]);
        tmxr_linemsg (t, "\r\n");
    }
    if (err == SCPE_EXIT) {                         /* close telnet session */
        tmxr_reset_ln (t);
    }
}

/*
 * Режим управляющей командной строки.
 */
void vt_cmd_loop (int num, int c)
{
    TMLN *t = &tty_line [num];
    char *cbuf, **cptr;

    cbuf = vt_cbuf [num];
    cptr = &vt_cptr [num];

    switch (c) {
    case '\r':
    case '\n':
        tmxr_linemsg (t, "\r\n");
        if (*cptr <= cbuf) {
            /* Пустая строка - возврат в обычный режим. */
            tty_unit[num].flags &= ~TTY_CMDLINE_MASK;
            break;
        }
        /* Выполнение. */
        **cptr = 0;
        vt_cmd_exec (num);
        tmxr_linemsg (t, "sim>");
        *cptr = vt_cbuf[num];
        break;
    case '\b':
    case 0177:
        /* Стирание предыдущего символа. */
        if (*cptr <= cbuf)
            break;
        tmxr_linemsg (t, "\b \b");
        while (*cptr > cbuf) {
            --*cptr;
            if (! (**cptr & 0x80))
                break;
        }
        break;
    case 'U' & 037:
        /* Стирание всей строки. */
        erase_line:     while (*cptr > cbuf) {
            --*cptr;
            if (! (**cptr & 0x80))
                tmxr_linemsg (t, "\b \b");
        }
        break;
    case 033:
        /* Escape [ X. */
        if (tmxr_getc_ln (t) != '[' + TMXR_VALID)
            break;
        switch (tmxr_getc_ln (t) - TMXR_VALID) {
        case 'A': /* стрелка вверх */
            if (*cptr <= cbuf) {
                *cptr = cbuf + strlen (cbuf);
                if (*cptr > cbuf)
                    tmxr_linemsg (t, cbuf);
            }
            break;
        case 'B': /* стрелка вниз */
            goto erase_line;
        }
        break;
    default:
        if (c < ' ' || *cptr > cbuf+CBUFSIZE-5)
            break;
        *(*cptr)++ = c;
        tmxr_putc_ln (t, c);
        break;
    }
}

/*
 * Ввод символа с терминала с указанным номером.
 * Если нет приёма, возвращает -1.
 * В случае прерывания возвращает 0400 (только для консоли).
 */
int vt_getc (int num)
{
    TMLN *t = &tty_line [num];
    extern int32 sim_int_char;
    int c;
    time_t now;

    if (! t->conn) {
        /* Пользователь отключился. */
        if (t->ipad) {
            besm6_debug ("*** tty%d: отключение %s",
                         num, 
                         t->ipad);
            t->ipad = NULL;
        }
        tty_setmode (tty_unit+num, TTY_OFFLINE_STATE, 0, 0);
        tty_unit[num].flags &= ~TTY_STATE_MASK;
        return -1;
    }
    if (t->rcve) {
        /* Приём через telnet. */
        c = tmxr_getc_ln (t);
        if (! (c & TMXR_VALID)) {
            now = time (0);
            if (now > tty_last_time[num] + 5*60) {
                ++tty_idle_count[num];
                if (tty_idle_count[num] > 3) {
                    tmxr_linemsg (t, "\r\nКОНЕЦ СЕАНСА\r\n");
                    tmxr_reset_ln (t);
                    return -1;
                }
                tmxr_linemsg (t, "\r\nНЕ СПАТЬ!\r\n");
                tty_last_time[num] = now;
            }
            return -1;
        }
        tty_idle_count[num] = 0;
        tty_last_time[num] = time (0);

        if (tty_unit[num].flags & TTY_CMDLINE_MASK) {
            /* Продолжение режима управляющей командной строки. */
            vt_cmd_loop (num, c & 0377);
            return -1;
        }
        if ((c & 0377) == sim_int_char) {
            /* Вход в режим управляющей командной строки. */
            tty_unit[num].flags |= TTY_CMDLINE_MASK;
            tmxr_linemsg (t, "sim>");
            vt_cptr[num] = vt_cbuf[num];
            return -1;
        }
    } else {
        /* Ввод с клавиатуры. */
        c = sim_poll_kbd();
        if (c == SCPE_STOP)
            return 0400;            /* прерывание */
        if (! (c & SCPE_KFLAG))
            return -1;
    }
    return c & 0377;
}

/*
 * Ввод символа с клавиатуры.
 * Перекодировка из UTF-8 в КОИ-7.
 * Полученный символ находится в диапазоне 0..0177.
 * Если нет ввода, возвращает -1.
 * В случае прерывания (^E) возвращает 0400.
 */
static int vt_kbd_input_unicode (int num)
{
    int c1, c2, c3, r;
  again:
    r = vt_getc (num);
    if (r < 0 || r > 0377)
        return r;
    c1 = r & 0377;
    if (! (c1 & 0x80))
        return unicode_to_koi7 (c1);

    r = vt_getc (num);
    if (r < 0 || r > 0377)
        return r;
    c2 = r & 0377;
    if (! (c1 & 0x20))
        return unicode_to_koi7 ((c1 & 0x1f) << 6 | (c2 & 0x3f));

    r = vt_getc (num);
    if (r < 0 || r > 0377)
        return r;
    c3 = r & 0377;
    if (c1 == 0xEF && c2 == 0xBB && c3 == 0xBF) {
        /* Skip zero width no-break space. */
        goto again;
    }
    return unicode_to_koi7 ((c1 & 0x0f) << 12 | (c2 & 0x3f) << 6 |
                            (c3 & 0x3f));
}

/*
 * Альтернативный вариант ввода, не требующий переключения на русскую клавиатуру.
 * Символы "точка" и "запятая" вводятся через shift, "больше-меньше" - "тильда-гравис".
 * "точка с запятой" - "закр. фиг. скобка", "апостроф" - "верт. черта".
 */
static int vt_kbd_input_koi7 (int num)
{
    int r;

    r = vt_getc (num);
    if (r < 0 || r > 0377)
        return r;
    r &= 0377;
    switch (r) {
    case '\r': return '\003';
    case 'q': return 'j';
    case 'w': return 'c';
    case 'e': return 'u';
    case 'r': return 'k';
    case 't': return 'e';
    case 'y': return 'n';
    case 'u': return 'g';
    case 'i': return '{';
    case 'o': return '}';
    case 'p': return 'z';
    case '[': return 'h';
    case '{': return '[';
    case 'a': return 'f';
    case 's': return 'y';
    case 'd': return 'w';
    case 'f': return 'a';
    case 'g': return 'p';
    case 'h': return 'r';
    case 'j': return 'o';
    case 'k': return 'l';
    case 'l': return 'd';
    case ';': return 'v';
    case '}': return ';';
    case '\'': return '|';
    case '|': return '\'';
    case 'z': return 'q';
    case 'x': return '~';
    case 'c': return 's';
    case 'v': return 'm';
    case 'b': return 'i';
    case 'n': return 't';
    case 'm': return 'x';
    case ',': return 'b';
    case '<': return ',';
    case '.': return '`';
    case '>': return '.';
    case '~': return '>';
    case '`': return '<';
    default: return r;
    }
}

int odd_parity(unsigned char c)
{
    c = (c & 0x55) + ((c >> 1) & 0x55);
    c = (c & 0x33) + ((c >> 2) & 0x33);
    c = (c & 0x0F) + ((c >> 4) & 0x0F);
    return c & 1;
}

/*
 * Обработка ввода со всех подключенных терминалов.
 */
void vt_receive()
{
    uint32 workset = vt_mask;
    int num;

    TTY_IN = 0;
    for (num = besm6_highest_bit (workset) - TTY_MAX;
         workset; num = besm6_highest_bit (workset) - TTY_MAX) {
        uint32 mask = 1 << (TTY_MAX - num);
        switch (tty_instate[num]) {
        case 0:
            switch (tty_unit[num].flags & TTY_CHARSET_MASK) {
            case TTY_KOI7_JCUKEN_CHARSET:
                tty_typed[num] = vt_kbd_input_koi7 (num);
                break;
            case TTY_KOI7_QWERTY_CHARSET:
                tty_typed[num] = vt_getc (num);
                break;
            case TTY_UNICODE_CHARSET:
                tty_typed[num] = vt_kbd_input_unicode (num);
                break;
            default:
                tty_typed[num] = '?';
                break;
            }
            if (tty_typed[num] < 0) {
                /* TODO: обработать исключение от "неоператорского" терминала */
                sim_interval = 0;
                break;
            }
            if (tty_typed[num] <= 0177) {
                if (tty_typed[num] == '\r' || tty_typed[num] == '\n')
                    tty_typed[num] = 3;     /* ^C - конец строки */
                if (tty_typed[num] == '\177')
                    tty_typed[num] = '\b';  /* ASCII DEL -> BS */
                tty_instate[num] = 1;
                TTY_IN |= mask;         /* start bit */
                GRP |= GRP_TTY_START;   /* не используется ? */
                MGRP |= BBIT(19);       /* для терминалов по методу МГУ */
                vt_receiving |= mask;
            }
            break;
        case 1: case 2: case 3: case 4: case 5: case 6: case 7:
            /* need inverted byte */
            TTY_IN |= (tty_typed[num] & (1 << (tty_instate[num]-1))) ? 0 : mask;
                tty_instate[num]++;
                break;
        case 8:
            TTY_IN |= odd_parity(tty_typed[num]) ? 0 : mask;        /* even parity of inverted */
            tty_instate[num]++;
            break;
        case 9: case 10: case 11:
            /* stop bits are 0 */
            tty_instate[num]++;
            break;
        case 12:
            tty_instate[num] = 0;   /* ready for the next char */
            vt_receiving &= ~mask;
            break;
        }
        workset &= ~mask;
    }
    if (vt_receiving)
        vt_idle = 0;
}

/*
 * Выясняем, остановлены ли терминалы. Нужно для входа в "спящий" режим.
 */
int vt_is_idle ()
{
    return (tt_mask ? vt_idle > 300 : vt_idle > 10);
}

int tty_query ()
{
    /*      besm6_debug ("*** телетайпы: приём");*/
    return TTY_IN;
}

void consul_print (int dev_num, uint32 cmd)
{
    int line_num = dev_num + TTY_MAX + 1;
    if (tty_dev.dctrl)
        besm6_debug(">>> CONSUL%o: %03o", line_num, cmd & 0377);
    cmd &= 0177;
    switch (tty_unit[line_num].flags & TTY_STATE_MASK) {
    case TTY_VT340_STATE:
        vt_send (line_num, cmd,
                 (tty_unit[line_num].flags & TTY_BSPACE_MASK) == TTY_DESTRUCTIVE_BSPACE);
        break;
    case TTY_CONSUL_STATE:
        besm6_debug(">>> CONSUL%o: Native charset not implemented", line_num);
        break;
    }
    // PRP |= CONS_CAN_PRINT[dev_num];
    vt_idle = 0;
}

void consul_receive ()
{
    int c, line_num, dev_num;

    for (dev_num = 0; dev_num < 2; ++dev_num){
        line_num = dev_num + TTY_MAX + 1;
        if (! tty_line[line_num].conn)
            continue;
        switch (tty_unit[line_num].flags & TTY_CHARSET_MASK) {
        case TTY_KOI7_JCUKEN_CHARSET:
            c = vt_kbd_input_koi7 (line_num);
            break;
        case TTY_KOI7_QWERTY_CHARSET:
            c = vt_getc (line_num);
            break;
        case TTY_UNICODE_CHARSET:
            c = vt_kbd_input_unicode (line_num);
            break;
        default:
            c = '?';
            break;
        }
        if (c >= 0 && c <= 0177) {
            CONSUL_IN[dev_num] = odd_parity(c) ? c | 0200 : c;
            if (c == '\r' || c == '\n')
                CONSUL_IN[dev_num] = 3;
            PRP |= CONS_HAS_INPUT[dev_num];
            vt_idle = 0;
        }
    }
}

uint32 consul_read (int num)
{
    if (tty_dev.dctrl)
        besm6_debug("<<< CONSUL%o: %03o", num+TTY_MAX+1, CONSUL_IN[num]);
    return CONSUL_IN[num];
}
