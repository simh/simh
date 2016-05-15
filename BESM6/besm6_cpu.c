/*
 * BESM-6 CPU simulator.
 *
 * Copyright (c) 1997-2009, Leonid Broukhis
 * Copyright (c) 2009, Serge Vakulenko
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

 * For more information about BESM-6 computer, visit sites:
 *  - http://www.computer-museum.ru/english/besm6.htm
 *  - http://mailcom.com/besm6/
 *  - http://groups.google.com/group/besm6
 *
 * Release notes for BESM-6/SIMH
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  1) All addresses and data values are displayed in octal.
 *  2) Memory size is 128 kwords.
 *  3) Interrupt system is to be synchronized with wallclock time.
 *  4) Execution times are in 1/10 of microsecond.
 *  5) Magnetic drums are implemented as a single "DRUM" device.
 *  6) Magnetic disks are implemented.
 *  7) Magnetic tape is not implemented.
 *  8) Punch tape reader is implemented, punch card reader is planned.
 *  9) Card puncher is not implemented.
 * 10) Displays are implemented.
 * 11) Printer АЦПУ-128 is implemented.
 * 12) Instruction mnemonics, register names and stop messages
 *     are in Russian using UTF-8 encoding. It is assumed, that
 *     user locale is UTF-8.
 * 13) A lot of comments in Russian (UTF-8).
 */
#include "besm6_defs.h"
#include <math.h>
#include <float.h>
#include <time.h>


t_value memory [MEMSIZE];
uint32 PC, RK, Aex, M [NREGS], RAU, RUU;
t_value ACC, RMR, GRP, MGRP;
uint32 PRP, MPRP;
uint32 READY, READY2;                   /* ready flags of various devices */
int32 tmr_poll = CLK_DELAY;             /* pgm timer poll */

extern const char *scp_errors[];

/* Wired (non-registered) bits of interrupt registers (GRP and PRP)
 * cannot be cleared by writing to the GRP and must be cleared by clearing
 * the registers generating the corresponding interrupts.
 */
#define GRP_WIRED_BITS (GRP_DRUM1_FREE | GRP_DRUM2_FREE |\
                        GRP_CHAN3_DONE | GRP_CHAN4_DONE |\
                        GRP_CHAN5_DONE | GRP_CHAN6_DONE |\
                        GRP_CHAN3_FREE | GRP_CHAN4_FREE |\
                        GRP_CHAN5_FREE | GRP_CHAN6_FREE |\
                        GRP_CHAN7_FREE )

/* So far irrelevant as none of the devices -
 * punchcard I/O and punchtape output - had been implemented.
 */
#define PRP_WIRED_BITS (PRP_UVVK1_END | PRP_UVVK2_END |\
                        PRP_PCARD1_CHECK | PRP_PCARD2_CHECK |\
                        PRP_PCARD1_PUNCH | PRP_PCARD2_PUNCH |\
                        PRP_PTAPE1_PUNCH | PRP_PTAPE2_PUNCH )

int corr_stack;
int redraw_panel;
jmp_buf cpu_halt;

t_stat cpu_examine (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_deposit (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_req (UNIT *u, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_pult (UNIT *u, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_pult (FILE *st, UNIT *up, int32 v, CONST void *dp);


/*
 * CPU data structures
 *
 * cpu_dev      CPU device descriptor
 * cpu_unit     CPU unit descriptor
 * cpu_reg      CPU register list
 * cpu_mod      CPU modifiers list
 */

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX, MEMSIZE) };

#define ORDATAVM(nm,loc,wd) REGDATA(nm,(loc),8,wd,0,1,NULL,NULL,REG_VMIO,0,0)

REG cpu_reg[] = {
    { ORDATA   ( "СчАС",  PC,       15) }, /* счётчик адреса команды */
    { ORDATA   ( "РК",    RK,       24) }, /* регистр выполняемой команды */
    { ORDATA   ( "Аисп",  Aex,      15) }, /* исполнительный адрес */
    { ORDATAVM ( "СМ",    ACC,      48) }, /* сумматор */
    { ORDATAVM ( "РМР",   RMR,      48) }, /* регистр младших разрядов */
    { BINRDATA ( "РАУ",   RAU,       6) }, /* режимы АУ */
    { ORDATA   ( "М1",    M[1],     15) }, /* регистры-модификаторы */
    { ORDATA   ( "М2",    M[2],     15) },
    { ORDATA   ( "М3",    M[3],     15) },
    { ORDATA   ( "М4",    M[4],     15) },
    { ORDATA   ( "М5",    M[5],     15) },
    { ORDATA   ( "М6",    M[6],     15) },
    { ORDATA   ( "М7",    M[7],     15) },
    { ORDATA   ( "М10",   M[010],   15) },
    { ORDATA   ( "М11",   M[011],   15) },
    { ORDATA   ( "М12",   M[012],   15) },
    { ORDATA   ( "М13",   M[013],   15) },
    { ORDATA   ( "М14",   M[014],   15) },
    { ORDATA   ( "М15",   M[015],   15) },
    { ORDATA   ( "М16",   M[016],   15) },
    { ORDATA   ( "М17",   M[017],   15) }, /* указатель магазина */
    { ORDATA   ( "М20",   M[020],   15) }, /* MOD - модификатор адреса */
    { ORDATA   ( "М21",   M[021],   15) }, /* PSW - режимы УУ */
    { ORDATA   ( "М27",   M[027],   15) }, /* SPSW - упрятывание режимов УУ */
    { ORDATA   ( "М32",   M[032],   15) }, /* ERET - адрес возврата из экстракода */
    { ORDATA   ( "М33",   M[033],   15) }, /* IRET - адрес возврата из прерывания */
    { ORDATA   ( "М34",   M[034],   16) }, /* IBP - адрес прерывания по выполнению */
    { ORDATA   ( "М35",   M[035],   16) }, /* DWP - адрес прерывания по чтению/записи */
    { BINRDATA ( "РУУ",   RUU,       9) }, /* ПКП, ПКЛ, РежЭ, РежПр, ПрИК, БРО, ПрК */
    { ORDATAVM ( "ГРП",   GRP,      48) }, /* главный регистр прерываний */
    { ORDATAVM ( "МГРП",  MGRP,     48) }, /* маска ГРП */
    { ORDATA   ( "ПРП",   PRP,      24) }, /* периферийный регистр прерываний */
    { ORDATA   ( "МПРП",  MPRP,     24) }, /* маска ПРП */
    { 0 }
};

MTAB cpu_mod[] = {
    { MTAB_XTD|MTAB_VDV,
        0, "IDLE",  "IDLE",     &sim_set_idle,      &sim_show_idle,     NULL,
                                "Enables idle detection mode" },
    { MTAB_XTD|MTAB_VDV,
        0, NULL,    "NOIDLE",   &sim_clr_idle,      NULL,               NULL,
                                "Disables idle detection" },
    { MTAB_XTD|MTAB_VDV,
        0, NULL,    "REQ",      &cpu_req,           NULL,               NULL,
                                "Sends a request interrupt" },
    { MTAB_XTD|MTAB_VDV,
        0, "PANEL", "PANEL",    &besm6_init_panel,  &besm6_show_panel,  NULL,
                                "Displays graphical panel" },
    { MTAB_XTD|MTAB_VDV,
        0, NULL,    "NOPANEL",  &besm6_close_panel, NULL,               NULL,
                                "Closes graphical panel" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALO,
        0, "PULT",  "PULT",     &cpu_set_pult,      &cpu_show_pult,     NULL,
                                "Selects a hardwired program or switch reg." },
    { 0 }
};

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 17, 1, 8, 50,
    &cpu_examine, &cpu_deposit, &cpu_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG
};

/*
 * REG: A pseudo-device containing Latin synonyms of all CPU registers.
 */
REG reg_reg[] = {
    { ORDATA   ( "PC",    PC,         15) },          /* program counter */
    { ORDATA   ( "RK",    RK,         24) },          /* instruction register */
    { ORDATA   ( "Aex",   Aex,        15) },          /* effective address */
    { ORDATAVM ( "ACC",   ACC,        48) },          /* accumulator */
    { ORDATAVM ( "RMR",   RMR,        48) },          /* LSB register */
    { BINRDATA ( "RAU",   RAU,         6) },          /* ALU modes */
    { ORDATA   ( "M1",    M[1],       15) },          /* index (modifier) registers */
    { ORDATA   ( "M2",    M[2],       15) },
    { ORDATA   ( "M3",    M[3],       15) },
    { ORDATA   ( "M4",    M[4],       15) },
    { ORDATA   ( "M5",    M[5],       15) },
    { ORDATA   ( "M6",    M[6],       15) },
    { ORDATA   ( "M7",    M[7],       15) },
    { ORDATA   ( "M10",   M[010],     15) },
    { ORDATA   ( "M11",   M[011],     15) },
    { ORDATA   ( "M12",   M[012],     15) },
    { ORDATA   ( "M13",   M[013],     15) },
    { ORDATA   ( "M14",   M[014],     15) },
    { ORDATA   ( "M15",   M[015],     15) },
    { ORDATA   ( "M16",   M[016],     15) },
    { ORDATA   ( "M17",   M[017],     15) },          /* also the stack pointer */
    { ORDATA   ( "M20",   M[020],     15) },          /* MOD - address modifier register */
    { ORDATA   ( "M21",   M[021],     15) },          /* PSW - CU modes */
    { ORDATA   ( "M27",   M[027],     15) },          /* SPSW - saved CU modes */
    { ORDATA   ( "M32",   M[032],     15) },          /* ERET - extracode return address */
    { ORDATA   ( "M33",   M[033],     15) },          /* IRET - interrupt return address */
    { ORDATA   ( "M34",   M[034],     16) },          /* IBP - instruction bkpt address */
    { ORDATA   ( "M35",   M[035],     16) },          /* DWP - watchpoint address */
    { BINRDATA ( "RUU",   RUU,         9) },          /* execution modes  */
    { ORDATAVM ( "GRP",   GRP,        48) },          /* main interrupt reg */
    { ORDATAVM ( "MGRP",  MGRP,       48) },          /* mask of the above  */
    { ORDATA   ( "PRP",   PRP,        24) },          /* peripheral interrupt reg */
    { ORDATA   ( "MPRP",  MPRP,       24) },          /* mask of the above*/

    { ORDATAVM ( "BRZ0",  BRZ[0],     50) },
    { ORDATAVM ( "BRZ1",  BRZ[1],     50) },
    { ORDATAVM ( "BRZ2",  BRZ[2],     50) },
    { ORDATAVM ( "BRZ3",  BRZ[3],     50) },
    { ORDATAVM ( "BRZ4",  BRZ[4],     50) },
    { ORDATAVM ( "BRZ5",  BRZ[5],     50) },
    { ORDATAVM ( "BRZ6",  BRZ[6],     50) },
    { ORDATAVM ( "BRZ7",  BRZ[7],     50) },
    { ORDATA   ( "BAZ0",  BAZ[0],     16) },
    { ORDATA   ( "BAZ1",  BAZ[1],     16) },
    { ORDATA   ( "BAZ2",  BAZ[2],     16) },
    { ORDATA   ( "BAZ3",  BAZ[3],     16) },
    { ORDATA   ( "BAZ4",  BAZ[4],     16) },
    { ORDATA   ( "BAZ5",  BAZ[5],     16) },
    { ORDATA   ( "BAZ6",  BAZ[6],     16) },
    { ORDATA   ( "BAZ7",  BAZ[7],     16) },
    { ORDATA   ( "TABST", TABST,      28) },
    { ORDATAVM ( "RP0",   RP[0],      48) },
    { ORDATAVM ( "RP1",   RP[1],      48) },
    { ORDATAVM ( "RP2",   RP[2],      48) },
    { ORDATAVM ( "RP3",   RP[3],      48) },
    { ORDATAVM ( "RP4",   RP[4],      48) },
    { ORDATAVM ( "RP5",   RP[5],      48) },
    { ORDATAVM ( "RP6",   RP[6],      48) },
    { ORDATAVM ( "RP7",   RP[7],      48) },
    { ORDATA   ( "RZ",    RZ,         32) },
    { ORDATAVM ( "FP1",   pult[0][1], 50) },
    { ORDATAVM ( "FP2",   pult[0][2], 50) },
    { ORDATAVM ( "FP3",   pult[0][3], 50) },
    { ORDATAVM ( "FP4",   pult[0][4], 50) },
    { ORDATAVM ( "FP5",   pult[0][5], 50) },
    { ORDATAVM ( "FP6",   pult[0][6], 50) },
    { ORDATAVM ( "FP7",   pult[0][7], 50) },
    { 0 }
};

UNIT reg_unit = {
    UDATA (NULL, 0, 8)
};

DEVICE reg_dev = {
    "REG", &reg_unit, reg_reg, NULL,
    1, 8, 1, 1, 8, 50,
};

/*
 * SCP data structures and interface routines
 *
 * sim_name             simulator name string
 * sim_PC               pointer to saved PC register descriptor
 * sim_emax             maximum number of words for examine/deposit
 * sim_devices          array of pointers to simulated devices
 * sim_stop_messages    array of pointers to stop messages
 * sim_load             binary loader
 */

char sim_name[] = "БЭСМ-6";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;     /* max number of addressable units per instruction */

DEVICE *sim_devices[] = {
    &cpu_dev,
    &reg_dev,
    &drum_dev,
    &disk_dev,
    &mmu_dev,
    &clock_dev,
    &printer_dev,
    &fs_dev,
    &tty_dev,       /* терминалы - телетайпы, видеотоны, "Консулы" */
    0
};

const char *sim_stop_messages[] = {
    "Неизвестная ошибка",                 /* Unknown error */
    "Останов",                            /* STOP */
    "Точка останова",                     /* Emulator breakpoint */
    "Точка останова по считыванию",       /* Emulator read watchpoint */
    "Точка останова по записи",           /* Emulator write watchpoint */
    "Выход за пределы памяти",            /* Run out end of memory */
    "Запрещенная команда",                /* Invalid instruction */
    "Контроль команды",                   /* A data-tagged word fetched */
    "Команда в чужом листе",              /* Paging error during fetch */
    "Число в чужом листе",                /* Paging error during load/store */
    "Контроль числа МОЗУ",                /* RAM parity error */
    "Контроль числа БРЗ",                 /* Write cache parity error */
    "Переполнение АУ",                    /* Arith. overflow */
    "Деление на нуль",                    /* Division by zero or denorm */
    "Двойное внутреннее прерывание",      /* SIMH: Double internal interrupt */
    "Чтение неформатированного барабана", /* Reading unformatted drum */
    "Чтение неформатированного диска",    /* Reading unformatted disk */
    "Останов по КРА",                     /* Hardware breakpoint */
    "Останов по считыванию",              /* Load watchpoint */
    "Останов по записи",                  /* Store watchpoint */
    "Не реализовано",                     /* Unimplemented I/O or special reg. access */
};

/*
 * Memory examine
 */
t_stat cpu_examine (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MEMSIZE)
        return SCPE_NXM;
    if (vptr) {
        if (addr < 010) {
            if ((pult[pult_packet_switch][0] >> addr) & 1) {
                /* hardwired */
                *vptr = pult[pult_packet_switch][addr];
            } else {
                /* from switch regs */
                *vptr = pult[0][addr];
            }
        } else
            *vptr = memory [addr];
    }
    return SCPE_OK;
}

/*
 * Memory deposit
 */
t_stat cpu_deposit (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MEMSIZE)
        return SCPE_NXM;
    if (addr < 010) {
        /* Deposited values for the switch register address range
         * always go to switch registers.
         */
        pult [0][addr] = SET_PARITY (val, PARITY_INSN);
    } else
        memory [addr] = SET_PARITY (val, PARITY_INSN);
    return SCPE_OK;
}

/*
 * Reset routine
 */
t_stat cpu_reset (DEVICE *dptr)
{
    int i;

    ACC = 0;
    RMR = 0;
    RAU = 0;
    RUU = RUU_EXTRACODE | RUU_AVOST_DISABLE;
    for (i=0; i<NREGS; ++i)
        M[i] = 0;

    /* Punchcard readers not yet implemented thus not ready */
    READY2 |= 042000000;

    /* Регистр 17: БлП, БлЗ, ПОП, ПОК, БлПр */
    M[PSW] = PSW_MMAP_DISABLE | PSW_PROT_DISABLE | PSW_INTR_HALT |
        PSW_CHECK_HALT | PSW_INTR_DISABLE;

    /* Регистр 23: БлП, БлЗ, РежЭ, БлПр */
    M[SPSW] = SPSW_MMAP_DISABLE | SPSW_PROT_DISABLE | SPSW_EXTRACODE |
        SPSW_INTR_DISABLE;

    GRP = MGRP = 0;
    // Disabled due to a conflict with loading
    // PC = 1;                 /* "reset cpu; go" should start from 1  */

    sim_brk_types = SWMASK ('E') | SWMASK('R') | SWMASK('W');
    sim_brk_dflt = SWMASK ('E');

    besm6_draw_panel(1);

    return SCPE_OK;
}

/*
 * Request routine
 */
t_stat cpu_req (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    GRP |= GRP_PANEL_REQ;
    return SCPE_OK;
}

/*
 * Hardwired program selector validation
 */
t_stat cpu_set_pult (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    int sw;
    if (cptr) sw = atoi(cptr); else sw = 0;
    if (sw >= 0 && sw <= 10) {
        pult_packet_switch = sw;
        if (sw)
            sim_printf("Pult packet switch set to hardwired program %d\n", sw);
        else
            sim_printf("Pult packet switch set to switch registers\n");
        return SCPE_OK;
    }
    printf("Illegal value %s\n", cptr);
    return SCPE_ARG;
}

t_stat cpu_show_pult (FILE *st, UNIT *up, int32 v, CONST void *dp)
{
    fprintf(st, "Pult packet switch position is %d", pult_packet_switch);
    return SCPE_OK;
}

/*
 * Write Unicode symbol to file.
 * Convert to UTF-8 encoding:
 * 00000000.0xxxxxxx -> 0xxxxxxx
 * 00000xxx.xxyyyyyy -> 110xxxxx, 10yyyyyy
 * xxxxyyyy.yyzzzzzz -> 1110xxxx, 10yyyyyy, 10zzzzzz
 */
void
utf8_putc (unsigned ch, FILE *fout)
{
    if (ch < 0x80) {
        putc (ch, fout);
        return;
    }
    if (ch < 0x800) {
        putc (ch >> 6 | 0xc0, fout);
        putc ((ch & 0x3f) | 0x80, fout);
        return;
    }
    putc (ch >> 12 | 0xe0, fout);
    putc (((ch >> 6) & 0x3f) | 0x80, fout);
    putc ((ch & 0x3f) | 0x80, fout);
}

/*
 * *call ОКНО - так называлась служебная подпрограмма в мониторной
 * системе "Дубна", которая печатала полное состояние всех регистров.
 */
void besm6_okno (const char *message)
{
    besm6_log_cont ("_%%%%%% %s: ", message);
    if (sim_log)
        besm6_fprint_cmd (sim_log, RK);
    besm6_log ("_");

    /* СчАС, системные индекс-регистры 020-035. */
    besm6_log ("_    СчАС:%05o  20:%05o  21:%05o  27:%05o  32:%05o  33:%05o  34:%05o  35:%05o",
               PC, M[020], M[021], M[027], M[032], M[033], M[034], M[035]);
    /* Индекс-регистры 1-7. */
    besm6_log ("_       1:%05o   2:%05o   3:%05o   4:%05o   5:%05o   6:%05o   7:%05o",
               M[1], M[2], M[3], M[4], M[5], M[6], M[7]);
    /* Индекс-регистры 010-017. */
    besm6_log ("_      10:%05o  11:%05o  12:%05o  13:%05o  14:%05o  15:%05o  16:%05o  17:%05o",
               M[010], M[011], M[012], M[013], M[014], M[015], M[016], M[017]);
    /* Сумматор, РМР, режимы АУ и УУ. */
    besm6_log ("_      СМ:%04o %04o %04o %04o  РМР:%04o %04o %04o %04o  РАУ:%02o    РУУ:%03o",
               (int) (ACC >> 36) & BITS(12), (int) (ACC >> 24) & BITS(12),
               (int) (ACC >> 12) & BITS(12), (int) ACC & BITS(12),
               (int) (RMR >> 36) & BITS(12), (int) (RMR >> 24) & BITS(12),
               (int) (RMR >> 12) & BITS(12), (int) RMR & BITS(12),
               RAU, RUU);
}

/*
 * Команда "рег"
 */
static void cmd_002 ()
{
#if 0
    besm6_debug ("*** рег %03o", Aex & 0377);
#endif
    switch (Aex & 0377) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        /* Запись в БРЗ */
        mmu_setcache (Aex & 7, ACC);
        break;
    case 020: case 021: case 022: case 023:
    case 024: case 025: case 026: case 027:
        /* Запись в регистры приписки */
        mmu_setrp (Aex & 7, ACC);
        break;
    case 030: case 031: case 032: case 033:
        /* Запись в регистры защиты */
        mmu_setprotection (Aex & 3, ACC);
        break;
    case 036:
        /* Запись в маску главного регистра прерываний */
        MGRP = ACC;
        break;
    case 037:
        /* Clearing the main interrupt register: */
        /* it is impossible to clear wired (stateless) bits this way */
        GRP &= ACC | GRP_WIRED_BITS;
        break;
    case 64: case 65: case 66: case 67: case 68: case 69: case 70: case 71:
    case 72: case 73: case 74: case 75: case 76: case 77: case 78: case 79:
    case 80: case 81: case 82: case 83: case 84: case 85: case 86: case 87:
    case 88: case 89: case 90: case 91: case 92: case 93: case 94: case 95:
        /* 0100 - 0137:
         * Бит 1: управление блокировкой режима останова БРО.
         * Биты 2 и 3 - признаки формирования контрольных
         * разрядов (ПКП и ПКЛ). */
        if (Aex & 1)
            RUU |= RUU_AVOST_DISABLE;
        else
            RUU &= ~RUU_AVOST_DISABLE;
        if (Aex & 2)
            RUU |= RUU_PARITY_RIGHT;
        else
            RUU &= ~RUU_PARITY_RIGHT;
        if (Aex & 4)
            RUU |= RUU_PARITY_LEFT;
        else
            RUU &= ~RUU_PARITY_LEFT;
        break;
    case 0200: case 0201: case 0202: case 0203:
    case 0204: case 0205: case 0206: case 0207:
        /* Чтение БРЗ */
        ACC = mmu_getcache (Aex & 7);
        break;
    case 0237:
        /* Чтение главного регистра прерываний */
        ACC = GRP;
        break;
    default:
        if ((Aex & 0340) == 0140) {
            /* TODO: watchdog reset mechanism */
            longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        }
        /* Неиспользуемые адреса */
        besm6_debug ("*** %05o%s: РЕГ %o - неправильный адрес спец.регистра",
                     PC, (RUU & RUU_RIGHT_INSTR) ? "п" : "л", Aex);
        break;
    }
}

/*
 * Команда "увв"
 */
static void cmd_033 ()
{
#if 0
    besm6_debug ("*** увв %04o, СМ[24:1]=%08o",
                 Aex & 04177, (uint32) ACC & BITS(24));
#endif
    switch (Aex & 04177) {
    case 0:
        /*
         * Releasing the drum printer solenoids. No effect on simulation.
         */
        break;
    case 1: case 2:
        /* Управление обменом с магнитными барабанами */
        drum (Aex - 1, (uint32) ACC);
        break;
    case 3: case 4:
        /* Передача управляющего слова для обмена
         * с магнитными дисками */
        disk_io (Aex - 3, (uint32) ACC);
        break;
    case 5: case 6: case 7:
        /* TODO: управление обменом с магнитными лентами */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 010: case 011:
        /* управление устройствами ввода с перфоленты */
        fs_control (Aex - 010, (uint32) (ACC & 07));
        break;
    case 012: case 013:
        /* TODO: управление устройствами ввода с перфоленты по запаянной программе */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 014: case 015:
        /* Управление АЦПУ */
        printer_control (Aex - 014, (uint32) (ACC & 017));
        break;
    case 023: case 024:
        /* Управление обменом с магнитными дисками */
        disk_ctl (Aex - 023, (uint32) ACC);
        break;
    case 030:
        /* Гашение ПРП */
/*              besm6_debug(">>> гашение ПРП");*/
        PRP &= ACC | PRP_WIRED_BITS;
        break;
    case 031:
        /* Имитация сигналов прерывания ГРП */
        /*besm6_debug ("*** %05o%s: имитация прерываний ГРП %016llo",
          PC, (RUU & RUU_RIGHT_INSTR) ? "п" : "л", ACC << 24);*/
        GRP |= (ACC & BITS(24)) << 24;
        break;
    case 032: case 033:
        /* TODO: имитация сигналов из КМБ в КВУ */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 034:
        /* Запись в МПРП */
/*              besm6_debug(">>> запись в МПРП");*/
        MPRP = ACC & 077777777;
        break;
    case 035:
        /* TODO: управление режимом имитации обмена
         * с МБ и МЛ, имитация обмена */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 040: case 041: case 042: case 043:
    case 044: case 045: case 046: case 047:
    case 050: case 051: case 052: case 053:
    case 054: case 055: case 056: case 057:
        /* Управление молоточками АЦПУ */
        printer_hammer (Aex >= 050, Aex & 7, (uint32) (ACC & BITS(16)));
        break;
    case 0140:
        /* Запись в регистр телеграфных каналов */
        tty_send ((uint32) ACC & BITS(24));
        break;
    case 0141:
        /* TODO: formatting magnetic tape */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 0142:
        /* TODO: имитация сигналов прерывания ПРП */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 0147:
        /* Writing to the power supply control register
         * does not have any observable effect
         */
        break;
    case 0150: case 0151:
        /* TODO: reading from punchcards */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 0153:
        /* гашение аппаратуры сопряжения с терминалами */
/*              besm6_debug(">>> гашение АС: %08o", (uint32) ACC & BITS(24));*/
        break;
    case 0154: case 0155:
        /* TODO: управление выводом на перфокарты */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 0160: case 0167:
        /* TODO: управление электромагнитами пробивки перфокарт */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 0170: case 0171:
        /* TODO: пробивка строки на перфоленте */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 0174: case 0175:
        /* Выдача кода в пульт оператора */
        consul_print (Aex & 1, (uint32) ACC & BITS(8));
        break;
    case 0177:
        /* управление табло ГПВЦ СО АН СССР */
/*              besm6_debug(">>> ТАБЛО: %08o", (uint32) ACC & BITS(24));*/
        break;
    case 04001: case 04002:
        /* TODO: считывание слога в режиме имитации обмена */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 04003: case 04004:
        /* Запрос статуса контроллера магнитных дисков */
        ACC = disk_state (Aex - 04003);
        break;
    case 04006:
        /* TODO: считывание строки с устройства ввода
         * с перфоленты в запаянной программе */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 04007:
        /* TODO: опрос синхроимпульса ненулевой строки
         * в запаянной программе ввода с перфоленты */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 04014: case 04015:
        /* считывание строки с устройства ввода с перфоленты */
        ACC = fs_read (Aex - 04014);
        break;
    case 04016: case 04017:
        /* TODO: считывание строки с устройства
         * ввода с перфоленты */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 04020: case 04021: case 04022: case 04023:
        /* TODO: считывание слога в режиме имитации
         * внешнего обмена */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 04030:
        /* Чтение старшей половины ПРП */
        ACC = PRP & 077770000;
        break;
    case 04031:
        /* Опрос сигналов готовности (АЦПУ и пр.) */
/*              besm6_debug("Reading READY");*/
        ACC = READY;
        break;
    case 04034:
        /* Чтение младшей половины ПРП */
        ACC = (PRP & 07777) | 0377;
        break;
    case 04035:
        /* Опрос триггера ОШМi - наличие ошибок при внешнем обмене. */
        ACC = drum_errors() | disk_errors();
        break;
    case 04100:
        /* Опрос телеграфных каналов связи */
        ACC = tty_query ();
        break;
    case 04102:
        /* Опрос сигналов готовности перфокарт и перфолент */
/*              besm6_debug("Reading punchcard/punchtape READY @%05o", PC);*/
        ACC = READY2;
        break;
    case 04103: case 04104: case 04105: case 04106:
        /* Опрос состояния лентопротяжных механизмов.
         * Все устройства не готовы. */
        ACC = BITS(24);
        break;
    case 04107:
        /* TODO: опрос схемы контроля записи на МЛ */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 04115:
        /* Неизвестное обращение. ДИСПАК выдаёт эту команду
         * группами по 8 штук каждые несколько секунд. */
        ACC = 0;
        break;
    case 04170: case 04171: case 04172: case 04173:
        /* TODO: считывание контрольного кода
         * строки перфоленты */
        longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        break;
    case 04174: case 04175:
        /* Считывание кода с пульта оператора */
        ACC = consul_read (Aex & 1);
        break;
    case 04177:
        /* чтение табло ГПВЦ СО АН СССР */
        ACC = 0;
        break;
    default: {
        unsigned val = Aex & 04177;
        if (0100 <= val && val <= 0137) {
            /* Управление лентопротяжными механизмами
             * и гашение разрядов регистров признаков
             * окончания подвода зоны. Игнорируем. */
        } else if (04140 <= val && val <= 04157) {
            /* TODO: считывание строки перфокарты */
            longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        } else if (04160 <= val && val <= 04167) {
            /* TODO: контрольное считывание строки перфокарты */
            longjmp (cpu_halt, STOP_UNIMPLEMENTED);
        } else {
            /* Неиспользуемые адреса */
/*              if (sim_deb && cpu_dev.dctrl)*/
            besm6_debug ("*** %05o%s: УВВ %o - неправильный адрес ввода-вывода",
                         PC, (RUU & RUU_RIGHT_INSTR) ? "п" : "л", Aex);
            ACC = 0;
        }
    } break;
    }
}

void check_initial_setup ()
{
    const int MGRP_COPY = 01455;    /* OS version specific? */
    const int TAKEN = 0442;         /* fixed? */
    const int YEAR = 0221;          /* fixed */

    /* 47 р. яч. ЗАНЯТА - разр. приказы вообще */
    const t_value SETUP_REQS_ENABLED = 1LL << 46;

    /* 7 р. яч. ЗАНЯТА - разр любые приказы */
    const t_value ALL_REQS_ENABLED = 1 << 6;

    if (!vt_is_idle()) {
        /* Avoid sending setup requests while the OS
         * is still printing boot-up messages.
         */
        return;
    }
    if ((memory[TAKEN] & SETUP_REQS_ENABLED) == 0 || /* not ready for setup */
        (memory[TAKEN] & ALL_REQS_ENABLED) != 0 ||   /* all done */
        (MGRP & GRP_PANEL_REQ) == 0) {               /* not at the moment */
        return;
    }

    /* Выдаем приказы оператора СМЕ и ВРЕ,
     * а дату корректируем непосредственно в памяти.
     */
    /* Номер смены в 22-24 рр. МГРП: если еще не установлен, установить */
    if (((memory[MGRP_COPY] >> 21) & 3) == 0) {
        /* приказ СМЕ: ТР6 = 010, ТР4 = 1, 22-24 р ТР5 - #смены */
        pult[0][6] = 010;
        pult[0][4] = 1;
        pult[0][5] = 1 << 21;
        GRP |= GRP_PANEL_REQ;
    } else {
        struct tm * d;

        /* Яч. ГОД обновляем самостоятельно */
        time_t t;
        t_value date;
        time(&t);
        d = localtime(&t);
        ++d->tm_mon;
        date = (t_value) (d->tm_mday / 10) << 33 |
            (t_value) (d->tm_mday % 10) << 29 |
            (d->tm_mon / 10) << 28 |
            (d->tm_mon % 10) << 24 |
            (d->tm_year % 10) << 20 |
            ((d->tm_year / 10) % 10) << 16 |
            (memory[YEAR] & 7);
        memory[YEAR] = SET_PARITY (date, PARITY_NUMBER);
        /* приказ ВРЕ: ТР6 = 016, ТР5 = 9-14 р.-часы, 1-8 р.-минуты */
        pult[0][6] = 016;
        pult[0][4] = 0;
        pult[0][5] = (d->tm_hour / 10) << 12 |
            (d->tm_hour % 10) << 8 |
            (d->tm_min / 10) << 4 |
            (d->tm_min % 10);
        GRP |= GRP_PANEL_REQ;
    }
}

/*
 * Execute one instruction, placed on address PC:RUU_RIGHT_INSTR.
 * When stopped, perform a longjmp to cpu_halt,
 * sending a stop code.
 */
void cpu_one_inst ()
{
    int reg, opcode, addr, nextpc, next_mod;
    t_value word;

    /*
     * Instruction execution time in 100 ns ticks; not really used
     * as the amortized 1 MIPS instruction rate is assumed.
     * The assignments of MEAN_TIME(x,y) to the delay variable
     * are kept as a reference.
     */
    uint32 delay;

    corr_stack = 0;
    word = mmu_fetch (PC);
    if (RUU & RUU_RIGHT_INSTR)
        RK = (uint32)word;      /* get right instruction */
    else
        RK = (uint32)(word >> 24);/* get left instruction */

    RK &= BITS(24);

    reg = RK >> 20;
    if (RK & BBIT(20)) {
        addr = RK & BITS(15);
        opcode = (RK >> 12) & 0370;
    } else {
        addr = RK & BITS(12);
        if (RK & BBIT(19))
            addr |= 070000;
        opcode = (RK >> 12) & 077;
    }

    if (sim_deb && cpu_dev.dctrl) {
        fprintf (sim_deb, "*** %05o%s: ", PC,
                 (RUU & RUU_RIGHT_INSTR) ? "п" : "л");
        besm6_fprint_cmd (sim_deb, RK);
        fprintf (sim_deb, "\tСМ=");
        fprint_sym (sim_deb, 0, &ACC, 0, 0);
        fprintf (sim_deb, "\tРАУ=%02o", RAU);
        if (reg)
            fprintf (sim_deb, "\tМ[%o]=%05o", reg, M[reg]);
        fprintf (sim_deb, "\n");
    }
    nextpc = ADDR(PC + 1);
    if (RUU & RUU_RIGHT_INSTR) {
        PC += 1;                        /* increment PC */
        RUU &= ~RUU_RIGHT_INSTR;
    } else {
        mmu_prefetch(nextpc | (IS_SUPERVISOR(RUU) ? BBIT(16) : 0), 0);
        RUU |= RUU_RIGHT_INSTR;
    }

    if (RUU & RUU_MOD_RK) {
        addr = ADDR (addr + M[MOD]);
    }
    next_mod = 0;
    delay = 0;

    switch (opcode) {
    case 000:                                       /* зп, atx */
        Aex = ADDR (addr + M[reg]);
        mmu_store (Aex, ACC);
        if (! addr && reg == 017)
            M[017] = ADDR (M[017] + 1);
        delay = MEAN_TIME (3, 3);
        break;
    case 001:                                       /* зпм, stx */
        Aex = ADDR (addr + M[reg]);
        mmu_store (Aex, ACC);
        M[017] = ADDR (M[017] - 1);
        corr_stack = 1;
        ACC = mmu_load (M[017]);
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (6, 6);
        break;
    case 002:                                       /* рег, mod */
        Aex = ADDR (addr + M[reg]);
        if (! IS_SUPERVISOR (RUU))
            longjmp (cpu_halt, STOP_BADCMD);
        cmd_002 ();
        /* Режим АУ - логический, если операция была "чтение" */
        if (Aex & 0200)
            RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 3);
        break;
    case 003:                                       /* счм, xts */
        mmu_store (M[017], ACC);
        M[017] = ADDR (M[017] + 1);
        corr_stack = -1;
        Aex = ADDR (addr + M[reg]);
        ACC = mmu_load (Aex);
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (6, 6);
        break;
    case 004:                                       /* сл, a+x */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        besm6_add (mmu_load (Aex), 0, 0);
        RAU = SET_ADDITIVE (RAU);
        delay = MEAN_TIME (3, 11);
        break;
    case 005:                                       /* вч, a-x */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        besm6_add (mmu_load (Aex), 0, 1);
        RAU = SET_ADDITIVE (RAU);
        delay = MEAN_TIME (3, 11);
        break;
    case 006:                                       /* вчоб, x-a */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        besm6_add (mmu_load (Aex), 1, 0);
        RAU = SET_ADDITIVE (RAU);
        delay = MEAN_TIME (3, 11);
        break;
    case 007:                                       /* вчаб, amx */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        besm6_add (mmu_load (Aex), 1, 1);
        RAU = SET_ADDITIVE (RAU);
        delay = MEAN_TIME (3, 11);
        break;
    case 010:                                       /* сч, xta */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        ACC = mmu_load (Aex);
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 3);
        break;
    case 011:                                       /* и, aax */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        ACC &= mmu_load (Aex);
        RMR = 0;
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 4);
        break;
    case 012:                                       /* нтж, aex */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        RMR = ACC;
        ACC ^= mmu_load (Aex);
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 3);
        break;
    case 013:                                       /* слц, arx */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        ACC += mmu_load (Aex);
        if (ACC & BIT49)
            ACC = (ACC + 1) & BITS48;
        RMR = 0;
        RAU = SET_MULTIPLICATIVE (RAU);
        delay = MEAN_TIME (3, 6);
        break;
    case 014:                                       /* знак, avx */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        besm6_change_sign (mmu_load (Aex) >> 40 & 1);
        RAU = SET_ADDITIVE (RAU);
        delay = MEAN_TIME (3, 5);
        break;
    case 015:                                       /* или, aox */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        ACC |= mmu_load (Aex);
        RMR = 0;
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 4);
        break;
    case 016:                                       /* дел, a/x */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        besm6_divide (mmu_load (Aex));
        RAU = SET_MULTIPLICATIVE (RAU);
        delay = MEAN_TIME (3, 50);
        break;
    case 017:                                       /* умн, a*x */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        besm6_multiply (mmu_load (Aex));
        RAU = SET_MULTIPLICATIVE (RAU);
        delay = MEAN_TIME (3, 18);
        break;
    case 020:                                       /* сбр, apx */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        ACC = besm6_pack (ACC, mmu_load (Aex));
        RMR = 0;
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 53);
        break;
    case 021:                                       /* рзб, aux */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        ACC = besm6_unpack (ACC, mmu_load (Aex));
        RMR = 0;
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 53);
        break;
    case 022:                                       /* чед, acx */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        ACC = besm6_count_ones (ACC) + mmu_load (Aex);
        if (ACC & BIT49)
            ACC = (ACC + 1) & BITS48;
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 56);
        break;
    case 023:                                       /* нед, anx */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        if (ACC) {
            int n = besm6_highest_bit (ACC);

            /* "Остаток" сумматора, исключая бит,
             * номер которого определен, помещается в РМР,
             * начиная со старшего бита РМР. */
            besm6_shift (48 - n);

            /* Циклическое сложение номера со словом по Аисп. */
            ACC = n + mmu_load (Aex);
            if (ACC & BIT49)
                ACC = (ACC + 1) & BITS48;
        } else {
            RMR = 0;
            ACC = mmu_load (Aex);
        }
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 32);
        break;
    case 024:                                       /* слп, e+x */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        besm6_add_exponent ((mmu_load (Aex) >> 41) - 64);
        RAU = SET_MULTIPLICATIVE (RAU);
        delay = MEAN_TIME (3, 5);
        break;
    case 025:                                       /* вчп, e-x */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        besm6_add_exponent (64 - (mmu_load (Aex) >> 41));
        RAU = SET_MULTIPLICATIVE (RAU);
        delay = MEAN_TIME (3, 5);
        break;
    case 026: {                                     /* сд, asx */
        int n;
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        n = (mmu_load (Aex) >> 41) - 64;
        besm6_shift (n);
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 4 + abs (n));
        break;
    }
    case 027:                                       /* рж, xtr */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        RAU = (mmu_load (Aex) >> 41) & 077;
        delay = MEAN_TIME (3, 3);
        break;
    case 030:                                       /* счрж, rte */
        Aex = ADDR (addr + M[reg]);
        ACC = (t_value) (RAU & Aex & 0177) << 41;
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 3);
        break;
    case 031:                                       /* счмр, yta */
        Aex = ADDR (addr + M[reg]);
        if (IS_LOGICAL (RAU)) {
            ACC = RMR;
        } else {
            t_value x = RMR;
            ACC = (ACC & ~BITS41) | (RMR & BITS40);
            besm6_add_exponent ((Aex & 0177) - 64);
            RMR = x;
        }
        delay = MEAN_TIME (3, 5);
        break;
    case 032:                                       /* э32, ext */
        /* Fall through... */
    case 033:                                       /* увв, ext */
        Aex = ADDR (addr + M[reg]);
        if (! IS_SUPERVISOR (RUU))
            longjmp (cpu_halt, STOP_BADCMD);
        cmd_033 ();
        /* Режим АУ - логический, если операция была "чтение" */
        if (Aex & 04000)
            RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 8);
        break;
    case 034:                                       /* слпа, e+n */
        Aex = ADDR (addr + M[reg]);
        besm6_add_exponent ((Aex & 0177) - 64);
        RAU = SET_MULTIPLICATIVE (RAU);
        delay = MEAN_TIME (3, 5);
        break;
    case 035:                                       /* вчпа, e-n */
        Aex = ADDR (addr + M[reg]);
        besm6_add_exponent (64 - (Aex & 0177));
        RAU = SET_MULTIPLICATIVE (RAU);
        delay = MEAN_TIME (3, 5);
        break;
    case 036: {                                     /* сда, asn */
        int n;
        Aex = ADDR (addr + M[reg]);
        n = (Aex & 0177) - 64;
        besm6_shift (n);
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (3, 4 + abs (n));
        break;
    }
    case 037:                                       /* ржа, ntr */
        Aex = ADDR (addr + M[reg]);
        RAU = Aex & 077;
        delay = MEAN_TIME (3, 3);
        break;
    case 040:                                       /* уи, ati */
        Aex = ADDR (addr + M[reg]);
        if (IS_SUPERVISOR (RUU)) {
            int reg = Aex & 037;
            M[reg] = ADDR (ACC);
            /* breakpoint/watchpoint regs will match physical
             * or virtual addresses depending on the current
             * mapping mode.
             */
            if ((M[PSW] & PSW_MMAP_DISABLE) &&
                (reg == IBP || reg == DWP))
                M[reg] |= BBIT(16);

        } else
            M[Aex & 017] = ADDR (ACC);
        M[0] = 0;
        delay = MEAN_TIME (14, 3);
        break;
    case 041: {                                     /* уим, sti */
        unsigned rg, ad;

        Aex = ADDR (addr + M[reg]);
        rg = Aex & (IS_SUPERVISOR (RUU) ? 037 : 017);
        ad = ADDR (ACC);
        if (rg != 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        ACC = mmu_load (rg != 017 ? M[017] : ad);
        M[rg] = ad;
        if ((M[PSW] & PSW_MMAP_DISABLE) && (rg == IBP || rg == DWP))
            M[rg] |= BBIT(16);
        M[0] = 0;
        RAU = SET_LOGICAL (RAU);
        delay = MEAN_TIME (14, 3);
        break;
    }
    case 042:                                       /* счи, ita */
        delay = MEAN_TIME (6, 3);
    load_modifier:  Aex = ADDR (addr + M[reg]);
        ACC = ADDR(M[Aex & (IS_SUPERVISOR (RUU) ? 037 : 017)]);
        RAU = SET_LOGICAL (RAU);
        break;
    case 043:                                       /* счим, its */
        mmu_store (M[017], ACC);
        M[017] = ADDR (M[017] + 1);
        delay = MEAN_TIME (9, 6);
        goto load_modifier;
    case 044:                                       /* уии, mtj */
        Aex = addr;
        if (IS_SUPERVISOR (RUU)) {
          transfer_modifier:      M[Aex & 037] = M[reg];
            if ((M[PSW] & PSW_MMAP_DISABLE) &&
                ((Aex & 037) == IBP || (Aex & 037) == DWP))
                M[Aex & 037] |= BBIT(16);

        } else
            M[Aex & 017] = M[reg];
        M[0] = 0;
        delay = 6;
        break;
    case 045:                                       /* сли, j+m */
        Aex = addr;
        if ((Aex & 020) && IS_SUPERVISOR (RUU))
            goto transfer_modifier;
        M[Aex & 017] = ADDR (M[Aex & 017] + M[reg]);
        M[0] = 0;
        delay = 6;
        break;
    case 046:                                       /* э46, x46 */
        Aex = addr;
        if (! IS_SUPERVISOR (RUU))
            longjmp (cpu_halt, STOP_BADCMD);
        M[Aex & 017] = ADDR (Aex);
        M[0] = 0;
        delay = 6;
        break;
    case 047:                                       /* э47, x47 */
        Aex = addr;
        if (! IS_SUPERVISOR (RUU))
            longjmp (cpu_halt, STOP_BADCMD);
        M[Aex & 017] = ADDR (M[Aex & 017] + Aex);
        M[0] = 0;
        delay = 6;
        break;
    case 050: case 051: case 052: case 053:
    case 054: case 055: case 056: case 057:
    case 060: case 061: case 062: case 063:
    case 064: case 065: case 066: case 067:
    case 070: case 071: case 072: case 073:
    case 074: case 075: case 076: case 077:         /* э50...э77 */
    case 0200:                                      /* э20 */
    case 0210:                                      /* э21 */
    stop_as_extracode:
            Aex = ADDR (addr + M[reg]);
            if (! sim_deb && sim_log && cpu_dev.dctrl && opcode != 075) {
                /* Если включен console log и cpu debug,
                 * но нет console debug, то печатаем только экстракоды.
                 * Пропускаем э75, их обычно слишком много. */
                t_value word = mmu_load (Aex);
                fprintf (sim_log, "*** %05o%s: ", PC,
                         (RUU & RUU_RIGHT_INSTR) ? "п" : "л");
                besm6_fprint_cmd (sim_log, RK);
                fprintf (sim_log, "\tАисп=%05o (=", Aex);
                fprint_sym (sim_log, 0, &word, 0, 0);
                fprintf (sim_log, ")  СМ=");
                fprint_sym (sim_log, 0, &ACC, 0, 0);
                if (reg)
                    fprintf (sim_log, "  М[%o]=%05o", reg, M[reg]);
                fprintf (sim_log, "\n");
            }
            /*besm6_okno ("экстракод");*/
            /* Адрес возврата из экстракода. */
            M[ERET] = nextpc;
            /* Сохранённые режимы УУ. */
            M[SPSW] = (M[PSW] & (PSW_INTR_DISABLE | PSW_MMAP_DISABLE |
                                 PSW_PROT_DISABLE)) | IS_SUPERVISOR (RUU);
            /* Текущие режимы УУ. */
            M[PSW] = PSW_INTR_DISABLE | PSW_MMAP_DISABLE |
                PSW_PROT_DISABLE | /*?*/ PSW_INTR_HALT;
            M[14] = Aex;
            RUU = SET_SUPERVISOR (RUU, SPSW_EXTRACODE);

            if (opcode <= 077)
                PC = 0500 + opcode;             /* э50-э77 */
            else
                PC = 0540 + (opcode >> 3);      /* э20, э21 */
            RUU &= ~RUU_RIGHT_INSTR;
            delay = 7;
            break;
    case 0220:                                      /* мода, utc */
        Aex = ADDR (addr + M[reg]);
        next_mod = Aex;
        delay = 4;
        break;
    case 0230:                                      /* мод, wtc */
        if (! addr && reg == 017) {
            M[017] = ADDR (M[017] - 1);
            corr_stack = 1;
        }
        Aex = ADDR (addr + M[reg]);
        next_mod = ADDR (mmu_load (Aex));
        delay = MEAN_TIME (13, 3);
        break;
    case 0240:                                      /* уиа, vtm */
        Aex = addr;
        M[reg] = addr;
        M[0] = 0;
        if (IS_SUPERVISOR (RUU) && reg == 0) {
            M[PSW] &= ~(PSW_INTR_DISABLE |
                        PSW_MMAP_DISABLE | PSW_PROT_DISABLE);
            M[PSW] |= addr & (PSW_INTR_DISABLE |
                              PSW_MMAP_DISABLE | PSW_PROT_DISABLE);
        }
        delay = 4;
        break;
    case 0250:                                      /* слиа, utm */
        Aex = ADDR (addr + M[reg]);
        M[reg] = Aex;
        M[0] = 0;
        if (IS_SUPERVISOR (RUU) && reg == 0) {
            M[PSW] &= ~(PSW_INTR_DISABLE |
                        PSW_MMAP_DISABLE | PSW_PROT_DISABLE);
            M[PSW] |= addr & (PSW_INTR_DISABLE |
                              PSW_MMAP_DISABLE | PSW_PROT_DISABLE);
        }
        delay = 4;
        break;
    case 0260:                                      /* по, uza */
        Aex = ADDR (addr + M[reg]);
        RMR = ACC;
        delay = MEAN_TIME (12, 3);
        if (IS_ADDITIVE (RAU)) {
            if (ACC & BIT41)
                break;
        } else if (IS_MULTIPLICATIVE (RAU)) {
            if (! (ACC & BIT48))
                break;
        } else if (IS_LOGICAL (RAU)) {
            if (ACC)
                break;
        } else
            break;
        PC = Aex;
        RUU &= ~RUU_RIGHT_INSTR;
        delay += 3;
        break;
    case 0270:                                      /* пе, u1a */
        Aex = ADDR (addr + M[reg]);
        RMR = ACC;
        delay = MEAN_TIME (12, 3);
        if (IS_ADDITIVE (RAU)) {
            if (! (ACC & BIT41))
                break;
        } else if (IS_MULTIPLICATIVE (RAU)) {
            if (ACC & BIT48)
                break;
        } else if (IS_LOGICAL (RAU)) {
            if (! ACC)
                break;
        } else
            /* fall thru, i.e. branch */;
        PC = Aex;
        RUU &= ~RUU_RIGHT_INSTR;
        delay += 3;
        break;
    case 0300:                                      /* пб, uj */
        Aex = ADDR (addr + M[reg]);
        PC = Aex;
        RUU &= ~RUU_RIGHT_INSTR;
        delay = 7;
        break;
    case 0310:                                      /* пв, vjm */
        Aex = addr;
        M[reg] = nextpc;
        M[0] = 0;
        PC = addr;
        RUU &= ~RUU_RIGHT_INSTR;
        delay = 7;
        break;
    case 0320:                                      /* выпр, iret */
        Aex = addr;
        if (! IS_SUPERVISOR (RUU)) {
            longjmp (cpu_halt, STOP_BADCMD);
        }
        M[PSW] = (M[PSW] & PSW_WRITE_WATCH) |
            (M[SPSW] & (SPSW_INTR_DISABLE |
                        SPSW_MMAP_DISABLE | SPSW_PROT_DISABLE));
        PC = M[(reg & 3) | 030];
        RUU &= ~RUU_RIGHT_INSTR;
        if (M[SPSW] & SPSW_RIGHT_INSTR)
            RUU |= RUU_RIGHT_INSTR;
        else
            RUU &= ~RUU_RIGHT_INSTR;
        RUU = SET_SUPERVISOR (RUU,
                              M[SPSW] & (SPSW_EXTRACODE | SPSW_INTERRUPT));
        if (M[SPSW] & SPSW_MOD_RK)
            next_mod = M[MOD];
        /*besm6_okno ("Выход из прерывания");*/
        delay = 7;
        break;
    case 0330:                                      /* стоп, stop */
        Aex = ADDR (addr + M[reg]);
        delay = 7;
        if (! IS_SUPERVISOR(RUU)) {
            if (M[PSW] & PSW_CHECK_HALT)
                break;
            else {
                opcode = 063;
                goto stop_as_extracode;
            }
        }
        mmu_print_brz ();
        longjmp (cpu_halt, STOP_STOP);
        break;
    case 0340:                                      /* пио, vzm */
    branch_zero:    Aex = addr;
        delay = 4;
        if (! M[reg]) {
            PC = addr;
            RUU &= ~RUU_RIGHT_INSTR;
            delay += 3;
        }
        break;
    case 0350:                                      /* пино, v1m */
        Aex = addr;
        delay = 4;
        if (M[reg]) {
            PC = addr;
            RUU &= ~RUU_RIGHT_INSTR;
            delay += 3;
        }
        break;
    case 0360:                                      /* э36, *36 */
        goto branch_zero;
    case 0370:                                      /* цикл, vlm */
        Aex = addr;
        delay = 4;
        if (! M[reg])
            break;
        M[reg] = ADDR (M[reg] + 1);
        PC = addr;
        RUU &= ~RUU_RIGHT_INSTR;
        delay += 3;
        break;
    default:
        /* Unknown instruction - cannot happen. */
        longjmp (cpu_halt, STOP_STOP);
        break;
    }
    if (next_mod) {
        /* Модификация адреса следующей команды. */
        M[MOD] = next_mod;
        RUU |= RUU_MOD_RK;
    } else
        RUU &= ~RUU_MOD_RK;

    /* Не находимся ли мы в цикле "ЖДУ" диспака? */
    if (RUU == 047 && PC == 04440 && RK == 067704440) {
        check_initial_setup ();
        sim_idle(0, TRUE);
    }
}

/*
 * Операция прерывания 1: внутреннее прерывание.
 * Описана в 9-м томе технического описания БЭСМ-6, страница 119.
 */
void op_int_1 (const char *msg)
{
    /*besm6_okno (msg);*/
    M[SPSW] = (M[PSW] & (PSW_INTR_DISABLE | PSW_MMAP_DISABLE |
                         PSW_PROT_DISABLE)) | IS_SUPERVISOR (RUU);
    if (RUU & RUU_RIGHT_INSTR)
        M[SPSW] |= SPSW_RIGHT_INSTR;
    M[IRET] = PC;
    M[PSW] |= PSW_INTR_DISABLE | PSW_MMAP_DISABLE | PSW_PROT_DISABLE;
    if (RUU & RUU_MOD_RK) {
        M[SPSW] |= SPSW_MOD_RK;
        RUU &= ~RUU_MOD_RK;
    }
    PC = 0500;
    RUU &= ~RUU_RIGHT_INSTR;
    RUU = SET_SUPERVISOR (RUU, SPSW_INTERRUPT);
}

/*
 * Операция прерывания 2: внешнее прерывание.
 * Описана в 9-м томе технического описания БЭСМ-6, страница 129.
 */
void op_int_2 ()
{
    /*besm6_okno ("Внешнее прерывание");*/
    M[SPSW] = (M[PSW] & (PSW_INTR_DISABLE | PSW_MMAP_DISABLE |
                         PSW_PROT_DISABLE)) | IS_SUPERVISOR (RUU);
    M[IRET] = PC;
    M[PSW] |= PSW_INTR_DISABLE | PSW_MMAP_DISABLE | PSW_PROT_DISABLE;
    if (RUU & RUU_MOD_RK) {
        M[SPSW] |= SPSW_MOD_RK;
        RUU &= ~RUU_MOD_RK;
    }
    PC = 0501;
    RUU &= ~RUU_RIGHT_INSTR;
    RUU = SET_SUPERVISOR (RUU, SPSW_INTERRUPT);
}

/*
 * Main instruction fetch/decode loop
 */
t_stat sim_instr (void)
{
    t_stat r;
    int iintr = 0;

    /* Restore register state */
    PC = PC & BITS(15);                             /* mask PC */
    mmu_setup ();                                   /* copy RP to TLB */

    /* An internal interrupt or user intervention */
    r = setjmp (cpu_halt);
    if (r) {
        M[017] += corr_stack;
        if (cpu_dev.dctrl) {
            const char *message = (r >= SCPE_BASE) ?
                scp_errors [r - SCPE_BASE] :
                sim_stop_messages [r];
            besm6_debug ("/// %05o%s: %s", PC,
                         (RUU & RUU_RIGHT_INSTR) ? "п" : "л",
                         message);
        }

        /*
         * ПоП и ПоК вызывают останов при любом внутреннем прерывании
         * или прерывании по контролю, соответственно.
         * Если произошёл останов по ПоП или ПоК,
         * то продолжение выполнения начнётся с команды, следующей
         * за вызвавшей прерывание. Как если бы кнопка "ТП" (тип
         * перехода) была включена. Подробнее на странице 119 ТО9.
         */
        switch (r) {
        default:
        ret:                    besm6_draw_panel(1);
            return r;
        case STOP_RWATCH:
        case STOP_WWATCH:
            /* Step back one insn to reexecute it */
            if (! (RUU & RUU_RIGHT_INSTR)) {
                --PC;
            }
            RUU ^= RUU_RIGHT_INSTR;
            goto ret;
        case STOP_BADCMD:
            if (M[PSW] & PSW_INTR_HALT)             /* ПоП */
                goto ret;
            op_int_1 (sim_stop_messages[r]);
            // SPSW_NEXT_RK is not important for this interrupt
            GRP |= GRP_ILL_INSN;
            break;
        case STOP_INSN_CHECK:
            if (M[PSW] & PSW_CHECK_HALT)            /* ПоК */
                goto ret;
            op_int_1 (sim_stop_messages[r]);
            // SPSW_NEXT_RK must be 0 for this interrupt; it is already
            GRP |= GRP_INSN_CHECK;
            break;
        case STOP_INSN_PROT:
            if (M[PSW] & PSW_INTR_HALT)             /* ПоП */
                goto ret;
            if (RUU & RUU_RIGHT_INSTR) {
                ++PC;
            }
            RUU ^= RUU_RIGHT_INSTR;
            op_int_1 (sim_stop_messages[r]);
            // SPSW_NEXT_RK must be 1 for this interrupt
            M[SPSW] |= SPSW_NEXT_RK;
            GRP |= GRP_INSN_PROT;
            break;
        case STOP_OPERAND_PROT:
#if 0
/* ДИСПАК держит признак ПоП установленным.
 * При запуске СЕРП возникает обращение к чужому листу. */
            if (M[PSW] & PSW_INTR_HALT)             /* ПоП */
                goto ret;
#endif
            if (RUU & RUU_RIGHT_INSTR) {
                ++PC;
            }
            RUU ^= RUU_RIGHT_INSTR;
            op_int_1 (sim_stop_messages[r]);
            M[SPSW] |= SPSW_NEXT_RK;
            // The offending virtual page is in bits 5-9
            GRP |= GRP_OPRND_PROT;
            GRP = GRP_SET_PAGE (GRP, iintr_data);
            break;
        case STOP_RAM_CHECK:
            if (M[PSW] & PSW_CHECK_HALT)            /* ПоК */
                goto ret;
            op_int_1 (sim_stop_messages[r]);
            // The offending interleaved block # is in bits 1-3.
            GRP |= GRP_CHECK | GRP_RAM_CHECK;
            GRP = GRP_SET_BLOCK (GRP, iintr_data);
            break;
        case STOP_CACHE_CHECK:
            if (M[PSW] & PSW_CHECK_HALT)            /* ПоК */
                goto ret;
            op_int_1 (sim_stop_messages[r]);
            // The offending BRZ # is in bits 1-3.
            GRP |= GRP_CHECK;
            GRP &= ~GRP_RAM_CHECK;
            GRP = GRP_SET_BLOCK (GRP, iintr_data);
            break;
        case STOP_INSN_ADDR_MATCH:
            if (M[PSW] & PSW_INTR_HALT)             /* ПоП */
                goto ret;
            if (RUU & RUU_RIGHT_INSTR) {
                ++PC;
            }
            RUU ^= RUU_RIGHT_INSTR;
            op_int_1 (sim_stop_messages[r]);
            M[SPSW] |= SPSW_NEXT_RK;
            GRP |= GRP_BREAKPOINT;
            break;
        case STOP_LOAD_ADDR_MATCH:
            if (M[PSW] & PSW_INTR_HALT)             /* ПоП */
                goto ret;
            if (RUU & RUU_RIGHT_INSTR) {
                ++PC;
            }
            RUU ^= RUU_RIGHT_INSTR;
            op_int_1 (sim_stop_messages[r]);
            M[SPSW] |= SPSW_NEXT_RK;
            GRP |= GRP_WATCHPT_R;
            break;
        case STOP_STORE_ADDR_MATCH:
            if (M[PSW] & PSW_INTR_HALT)             /* ПоП */
                goto ret;
            if (RUU & RUU_RIGHT_INSTR) {
                ++PC;
            }
            RUU ^= RUU_RIGHT_INSTR;
            op_int_1 (sim_stop_messages[r]);
            M[SPSW] |= SPSW_NEXT_RK;
            GRP |= GRP_WATCHPT_W;
            break;
        case STOP_OVFL:
            /* Прерывание по АУ вызывает останов, если БРО=0
             * и установлен ПоП или ПоК.
             * Страница 118 ТО9.*/
            if (! (RUU & RUU_AVOST_DISABLE) &&      /* ! БРО */
                ((M[PSW] & PSW_INTR_HALT) ||        /* ПоП */
                 (M[PSW] & PSW_CHECK_HALT)))        /* ПоК */
                goto ret;
            op_int_1 (sim_stop_messages[r]);
            GRP |= GRP_OVERFLOW|GRP_RAM_CHECK;
            break;
        case STOP_DIVZERO:
            if (! (RUU & RUU_AVOST_DISABLE) &&      /* ! БРО */
                ((M[PSW] & PSW_INTR_HALT) ||        /* ПоП */
                 (M[PSW] & PSW_CHECK_HALT)))        /* ПоК */
                goto ret;
            op_int_1 (sim_stop_messages[r]);
            GRP |= GRP_DIVZERO|GRP_RAM_CHECK;
            break;
        }
        ++iintr;
    }

    if (iintr > 1) {
        besm6_draw_panel(1);
        return STOP_DOUBLE_INTR;
    }
    /* Main instruction fetch/decode loop */
    for (;;) {
        if (sim_interval <= 0) {                /* check clock queue */
            r = sim_process_event ();
            if (r) {
                besm6_draw_panel(1);
                return r;
            }
        }

        if (PC > BITS(15) && IS_SUPERVISOR(RUU)) {
          /*
           * Runaway instruction execution in supervisor mode
           * warrants attention.
           */
            besm6_draw_panel(1);
            return STOP_RUNOUT;                 /* stop simulation */
        }

        if (sim_brk_summ & SWMASK('E') &&       /* breakpoint? */
            sim_brk_test (PC, SWMASK ('E'))) {
            besm6_draw_panel(1);
            return STOP_IBKPT;                  /* stop simulation */
        }

        if (redraw_panel) {
            /* Periodic panel redraw is not forcing */
            besm6_draw_panel(0);
            redraw_panel = 0;
        }

        if (PRP & MPRP) {
            /* There are interrupts pending in the peripheral
             * interrupt register */
            GRP |= GRP_SLAVE;
        }

        if (! iintr && ! (RUU & RUU_RIGHT_INSTR) &&
            ! (M[PSW] & PSW_INTR_DISABLE) && (GRP & MGRP)) {
            /* external interrupt */
            op_int_2();
        }
        cpu_one_inst ();                        /* one instr */
        iintr = 0;

        sim_interval -= 1;                      /* count down instructions */
    }
}

/*
 * A 250 Hz clock as per the original documentation,
 * and matching the available software binaries.
 * Some installations used 50 Hz with a modified OS
 * for a better user time/system time ratio.
 */
t_stat fast_clk (UNIT * this)
{
    static unsigned counter;
    static unsigned tty_counter;

    ++counter;
    ++tty_counter;

    GRP |= GRP_TIMER;

    if ((counter & 15) == 0) {
        /*
         * The OS used the (undocumented, later addition)
         * slow clock interrupt to initiate servicing
         * terminal I/O. Its frequency was reportedly 16 Hz;
         * 64 ms is a good enough approximation. It can be sped up
         * for faster console response (16 ms might be a good choice).
         */
        GRP |= GRP_SLOW_CLK;
    }

    /* Requesting a panel sample every 32 ms
     * (a redraw actually happens at every other sample). */
    if ((counter & 7) == 0) {
        redraw_panel = 1;
    }

    /* Baudot TTYs are synchronised to the main timer rather than the
     * serial line clock. Their baud rate is 50.
     */
    if (tty_counter == CLK_TPS/50) {
        tt_print();
        tty_counter = 0;
    }

    tmr_poll = sim_rtcn_calb (CLK_TPS, 0);              /* calibrate clock */
    return sim_activate_after (this, 1000000/CLK_TPS);  /* reactivate unit */
}

UNIT clocks[] = {
    { UDATA(fast_clk, UNIT_IDLE, 0), CLK_DELAY },   /* Bit 40 of the GRP, 250 Hz */
};

t_stat clk_reset (DEVICE * dev)
{
    sim_register_clock_unit (&clocks[0]);

    /* Схема автозапуска включается по нереализованной кнопке "МР" */

    if (!sim_is_running) {                              /* RESET (not IORESET)? */
        tmr_poll = sim_rtcn_init (clocks[0].wait, 0);   /* init timer */
        sim_activate (&clocks[0], tmr_poll);            /* activate unit */
        }
    return SCPE_OK;
}

DEVICE clock_dev = {
    "CLK", clocks, NULL, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG
};
