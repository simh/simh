/*
 * BESM-6 CPU simulator.
 *
 * Copyright (c) 1997-2009, Leonid Broukhis
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
 *
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
#include "svs_defs.h"
#include <math.h>
#include <float.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#undef SOFT_CLOCK

t_mem * memory;
t_shared * shared;
unsigned char tag [MEMSIZE];
uint32 svsPC, RK, Aex, M [NREGS], RAU, RUU;
uint8 svsTAG;
t_value ACC, RMR, GRP, MGRP;
uint32 PRP, MPRP;
uint32 cpu_num;
t_value REQUEST, RESPONSE;

extern const char *scp_error_messages[];

/* нехранящие биты ГРП должны сбрасываться путем обнуления тех регистров,
 * сборкой которых они являются
 */
#define GRP_WIRED_BITS 01400743700000000LL

#define PRP_WIRED_BITS 00400

int corr_stack;
int redraw_panel;
uint32 delay;
jmp_buf cpu_halt;

t_stat cpu_examine (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_deposit (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);

/*
 * CPU data structures
 *
 * cpu_dev      CPU device descriptor
 * cpu_unit     CPU unit descriptor
 * cpu_reg      CPU register list
 * cpu_mod      CPU modifiers list
 */

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX, MEMSIZE) };

REG cpu_reg[] = {
{ "СчАС",  &svsPC,		8, 15, 0, 1 },		/* счётчик адреса команды */
{ "РК",    &RK,		8, 24, 0, 1 },		/* регистр выполняемой команды */
{ "Аисп",  &Aex,	8, 15, 0, 1 },		/* исполнительный адрес */
{ "СМ",    &ACC,	8, 48, 0, 1, REG_VMIO},	/* сумматор */
{ "РМР",   &RMR,	8, 48, 0, 1, REG_VMIO},	/* регистр младших разрядов */
{ "РАУ",   &RAU,	2, 6,  0, 1 },		/* режимы АУ */
{ "М1",    &M[1],	8, 15, 0, 1 },		/* регистры-модификаторы */
{ "М2",    &M[2],	8, 15, 0, 1 },
{ "М3",    &M[3],	8, 15, 0, 1 },
{ "М4",    &M[4],	8, 15, 0, 1 },
{ "М5",    &M[5],	8, 15, 0, 1 },
{ "М6",    &M[6],	8, 15, 0, 1 },
{ "М7",    &M[7],	8, 15, 0, 1 },
{ "М10",   &M[010],	8, 15, 0, 1 },
{ "М11",   &M[011],	8, 15, 0, 1 },
{ "М12",   &M[012],	8, 15, 0, 1 },
{ "М13",   &M[013],	8, 15, 0, 1 },
{ "М14",   &M[014],	8, 15, 0, 1 },
{ "М15",   &M[015],	8, 15, 0, 1 },
{ "М16",   &M[016],	8, 15, 0, 1 },
{ "М17",   &M[017],	8, 15, 0, 1 },		/* указатель магазина */
{ "М20",   &M[020],	8, 15, 0, 1 },		/* MOD - модификатор адреса */
{ "М21",   &M[021],	8, 15, 0, 1 },		/* PSW - режимы УУ */
{ "М27",   &M[027],	8, 15, 0, 1 },		/* SPSW - упрятывание режимов УУ */
{ "М32",   &M[032],	8, 15, 0, 1 },		/* ERET - адрес возврата из экстракода */
{ "М33",   &M[033],	8, 15, 0, 1 },		/* IRET - адрес возврата из прерывания */
{ "М34",   &M[034],	8, 16, 0, 1 },		/* IBP - адрес прерывания по выполнению */
{ "М35",   &M[035],	8, 16, 0, 1 },		/* DWP - адрес прерывания по чтению/записи */
{ "РУУ",   &RUU,	2, 9,  0, 1 },		/* ПКП, ПКЛ, РежЭ, РежПр, ПрИК, БРО, ПрК */
{ "ГРП",   &GRP,	8, 48, 0, 1, REG_VMIO},	/* главный регистр прерываний */
{ "МГРП",  &MGRP,	8, 48, 0, 1, REG_VMIO},	/* маска ГРП */
{ "ПРП",   &PRP,	8, 24, 0, 1 },		/* периферийный регистр прерываний */
{ "МПРП",  &MPRP,	8, 24, 0, 1 },		/* маска ПРП */
{ 0 }
};

MTAB cpu_mod[] = {
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
 * REG: псевдоустройство, содержащее латинские синонимы всех регистров.
 */
REG reg_reg[] = {
{ "PC",    &svsPC,		8, 15, 0, 1 },		/* счётчик адреса команды */
{ "RK",    &RK,		8, 24, 0, 1 },		/* регистр выполняемой команды */
{ "Aex",   &Aex,	8, 15, 0, 1 },		/* исполнительный адрес */
{ "ACC",   &ACC,	8, 48, 0, 1, REG_VMIO}, /* сумматор */
{ "RMR",   &RMR,	8, 48, 0, 1, REG_VMIO}, /* регистр младших разрядов */
{ "RAU",   &RAU,	2, 6,  0, 1 },		/* режимы АУ */
{ "M1",    &M[1],	8, 15, 0, 1 },		/* регистры-модификаторы */
{ "M2",    &M[2],	8, 15, 0, 1 },
{ "M3",    &M[3],	8, 15, 0, 1 },
{ "M4",    &M[4],	8, 15, 0, 1 },
{ "M5",    &M[5],	8, 15, 0, 1 },
{ "M6",    &M[6],	8, 15, 0, 1 },
{ "M7",    &M[7],	8, 15, 0, 1 },
{ "M10",   &M[010],	8, 15, 0, 1 },
{ "M11",   &M[011],	8, 15, 0, 1 },
{ "M12",   &M[012],	8, 15, 0, 1 },
{ "M13",   &M[013],	8, 15, 0, 1 },
{ "M14",   &M[014],	8, 15, 0, 1 },
{ "M15",   &M[015],	8, 15, 0, 1 },
{ "M16",   &M[016],	8, 15, 0, 1 },
{ "M17",   &M[017],	8, 15, 0, 1 },		/* указатель магазина */
{ "M20",   &M[020],	8, 15, 0, 1 },		/* MOD - модификатор адреса */
{ "M21",   &M[021],	8, 15, 0, 1 },		/* PSW - режимы УУ */
{ "M27",   &M[027],	8, 15, 0, 1 },		/* SPSW - упрятывание режимов УУ */
{ "M32",   &M[032],	8, 15, 0, 1 },		/* ERET - адрес возврата из экстракода */
{ "M33",   &M[033],	8, 15, 0, 1 },		/* IRET - адрес возврата из прерывания */
{ "M34",   &M[034],	8, 16, 0, 1 },		/* IBP - адрес прерывания по выполнению */
{ "M35",   &M[035],	8, 16, 0, 1 },		/* DWP - адрес прерывания по чтению/записи */
{ "RUU",   &RUU,        2, 9,  0, 1 },		/* ПКП, ПКЛ, РежЭ, РежПр, ПрИК, БРО, ПрК */
{ "GRP",   &GRP,	8, 48, 0, 1, REG_VMIO},	/* главный регистр прерываний */
{ "MGRP",  &MGRP,	8, 48, 0, 1, REG_VMIO},	/* маска ГРП */
{ "PRP",   &PRP,	8, 24, 0, 1 },		/* периферийный регистр прерываний */
{ "MPRP",  &MPRP,	8, 24, 0, 1 },		/* маска ПРП */

{ "BRZ0",  &BRZ[0].word,	8, 64, 0, 1, REG_VMIO },
{ "BRZ1",  &BRZ[1].word,	8, 64, 0, 1, REG_VMIO },
{ "BRZ2",  &BRZ[2].word,	8, 64, 0, 1, REG_VMIO },
{ "BRZ3",  &BRZ[3].word,	8, 64, 0, 1, REG_VMIO },
{ "BRZ4",  &BRZ[4].word,	8, 64, 0, 1, REG_VMIO },
{ "BRZ5",  &BRZ[5].word,	8, 64, 0, 1, REG_VMIO },
{ "BRZ6",  &BRZ[6].word,	8, 64, 0, 1, REG_VMIO },
{ "BRZ7",  &BRZ[7].word,	8, 64, 0, 1, REG_VMIO },
{ "BAZ0",  &BAZ[0],	8, 16, 0, 1 },
{ "BAZ1",  &BAZ[1],	8, 16, 0, 1 },
{ "BAZ2",  &BAZ[2],	8, 16, 0, 1 },
{ "BAZ3",  &BAZ[3],	8, 16, 0, 1 },
{ "BAZ4",  &BAZ[4],	8, 16, 0, 1 },
{ "BAZ5",  &BAZ[5],	8, 16, 0, 1 },
{ "BAZ6",  &BAZ[6],	8, 16, 0, 1 },
{ "BAZ7",  &BAZ[7],	8, 16, 0, 1 },
{ "TABST", &TABST,	8, 28, 0, 1 },
{ "RP0",   &RP[0],	8, 48, 0, 1, REG_VMIO },
{ "RP1",   &RP[1],	8, 48, 0, 1, REG_VMIO },
{ "RP2",   &RP[2],	8, 48, 0, 1, REG_VMIO },
{ "RP3",   &RP[3],	8, 48, 0, 1, REG_VMIO },
{ "RP4",   &RP[4],	8, 48, 0, 1, REG_VMIO },
{ "RP5",   &RP[5],	8, 48, 0, 1, REG_VMIO },
{ "RP6",   &RP[6],	8, 48, 0, 1, REG_VMIO },
{ "RP7",   &RP[7],	8, 48, 0, 1, REG_VMIO },
{ "RZ",    &RZ,		8, 32, 0, 1 },
{ "FP1",   &pult[1],	8, 50, 0, 1, REG_VMIO },
{ "FP2",   &pult[2],	8, 50, 0, 1, REG_VMIO },
{ "FP3",   &pult[3],	8, 50, 0, 1, REG_VMIO },
{ "FP4",   &pult[4],	8, 50, 0, 1, REG_VMIO },
{ "FP5",   &pult[5],	8, 50, 0, 1, REG_VMIO },
{ "FP6",   &pult[6],	8, 50, 0, 1, REG_VMIO },
{ "FP7",   &pult[7],	8, 50, 0, 1, REG_VMIO },
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
 * sim_name		simulator name string
 * sim_PC		pointer to saved PC register descriptor
 * sim_emax		maximum number of words for examine/deposit
 * sim_devices		array of pointers to simulated devices
 * sim_stop_messages	array of pointers to stop messages
 * sim_load		binary loader
 */

char sim_name[] = "Э1-К2";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;	/* максимальное количество слов в машинной команде */

DEVICE *sim_devices[] = {
	&cpu_dev,
	&reg_dev,
	&mmu_dev,
	&clock_dev,
	&tty_dev,
	0
};

extern t_stat fast_clk (UNIT * this);
extern t_stat slow_clk (UNIT * this);

UNIT clocks[] = {
	{ UDATA(slow_clk, 0, 0) },	/* 10 р, 16 Гц */
	{ UDATA(fast_clk, 0, 0) },	/* программируется РЕГ 57 */
};


const char *sim_stop_messages[] = {
	"Неизвестная ошибка",				/* Unknown error */
	"Останов",					/* STOP */
	"Точка останова",				/* Emulator breakpoint */
	"Точка останова по считыванию",			/* Emulator read watchpoint */
	"Точка останова по записи",			/* Emulator write watchpoint */
	"Выход за пределы памяти",			/* Run out end of memory */
	"Запрещенная команда",				/* Invalid instruction */
	"Контроль команды",				/* A data-tagged word fetched */
	"Команда в чужом листе",			/* Paging error during fetch */
	"Число в чужом листе",				/* Paging error during load/store */
	"Контроль числа МОЗУ",				/* RAM parity error */
	"Контроль числа БРЗ",				/* Write cache parity error */
	"Переполнение АУ",				/* Arith. overflow */
	"Деление на нуль",				/* Division by zero or denorm */
	"Двойное внутреннее прерывание",		/* SIMH: Double internal interrupt */
	"Чтение неформатированного барабана",		/* Reading unformatted drum */
	"Чтение неформатированного диска",		/* Reading unformatted disk */
	"Останов по КРА",				/* Hardware breakpoint */
	"Останов по считыванию",			/* Load watchpoint */
	"Останов по записи",				/* Store watchpoint */
	"Не реализовано",				/* Unimplemented I/O or special reg. access */
};

/*
 * Memory examine
 */
t_stat cpu_examine (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr >= MEMSIZE)
		return SCPE_NXM;
	if (vptr) {
		if (addr < 010)
			*vptr = pult [addr];
		else
			*vptr = memory [addr].word;
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
	if (addr < 010)
		pult [addr] = val & BITS48;
	else
		memory [addr] = SET_TAG (val, TAG_INSN);
	return SCPE_OK;
}

static void cpu_ipc(int signum) {
	if (shared->request[cpu_num + CPU_OFFSET]) {
		PRP |= PRP_REQUEST;
		REQUEST |= BIT(shared->request[cpu_num + CPU_OFFSET]);
		shared->request[cpu_num + CPU_OFFSET] = 0;
	}
	if (shared->response[cpu_num + CPU_OFFSET]) {
		PRP |= PRP_RESPONSE;
		RESPONSE |= BIT(shared->response[cpu_num + CPU_OFFSET]);
		shared->response[cpu_num + CPU_OFFSET] = 0;
	}
}

/*
 * Функция вызывается каждые 4 миллисекунды реального времени.
 */
static void cpu_sigalarm (int signum)
{
	static unsigned counter;

	++counter;

#if 0
#ifndef SOFT_CLOCK
	/* В 9-й части частота таймера 250 Гц (4 мс). */
	PRP |= PRP_TIMER;

	/* Медленный таймер: должен быть 16 Гц.
	 * Но от него почему-то зависит вывод на терминалы,
	 * поэтому ускорим. */
	if ((counter & 3) == 0) {
		GRP |= GRP_SLOW_CLK;
	}
#endif
#endif

	/* Перерисовка панели каждые 64 миллисекунды. */
	if ((counter & 15) == 0) {
		redraw_panel = 1;
	}
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

	/* ЦП №1 */
	cpu_num = 1;

	/* Регистр 17: БлП, БлЗ, ПОП, ПОК, БлПр */
	M[PSW] = PSW_MMAP_DISABLE | PSW_PROT_DISABLE | PSW_INTR_HALT |
		PSW_CHECK_HALT | PSW_INTR_DISABLE;

	/* Регистр 23: БлП, БлЗ, РежЭ, БлПр */
	M[SPSW] = SPSW_MMAP_DISABLE | SPSW_PROT_DISABLE | SPSW_EXTRACODE |
		SPSW_INTR_DISABLE;

	GRP = MGRP = 0;
	sim_brk_types = SWMASK ('E') | SWMASK('R') | SWMASK('W');
	sim_brk_dflt = SWMASK ('E');

	signal (SIGUSR1, cpu_ipc);
	struct itimerval itv;

#if 0
	/* Чтобы ход часов в ДИСПАКе соответствал реальному времени,
	 * используем сигналы от системного таймера. */
	// signal (SIGALRM, cpu_sigalarm);
	itv.it_interval.tv_sec = 0;
	itv.it_interval.tv_usec = 4000;
	itv.it_value.tv_sec = 0;
	itv.it_value.tv_usec = 4000;
	if (setitimer (ITIMER_REAL, &itv, 0) < 0) {
		perror ("setitimer");
		return SCPE_TIMER;
	}
#endif

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
		svsPC, M[020], M[021], M[027], M[032], M[033], M[034], M[035]);
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
	case 0 ... 7:
		/* Запись в БРЗ */
		mmu_setcache (Aex & 7, ACC);
		break;
	case 020 ... 027:
		/* Запись в регистры приписки */
		mmu_setrp (Aex & 7, ACC);
		break;
	case 030 ... 033:
		/* Запись в регистры защиты */
		mmu_setprotection (Aex & 3, ACC);
		break;
	case 036:
		/* Запись в маску главного регистра прерываний */
		MGRP = ACC;
		break;
	case 037:
		/* Гашение главного регистра прерываний */
		/* нехранящие биты невозможно погасить */
		GRP &= ACC | GRP_WIRED_BITS;
		break;
	case 044:
		/* Установка тега для полноразрядной записи */
		svsTAG = ACC;
		besm6_debug("Setting tag %0llo", ACC);
		break;
	case 046:
		/* Запись в маску регистра внешних прерываний */
		// besm6_debug("Setting MPRP %0llo", ACC);
		MPRP = ACC;
		break;
	case 047:
		/* Гашение регистра внешних прерываний */
		// if ((ACC & 0200) == 0) besm6_debug("Clearing ready at %o", svsPC);
		PRP &= ACC | PRP_WIRED_BITS;
		break;
	case 050:
		/* Прерывание типа запрос */
		// besm6_debug("Request interrupt %016llo", ACC);
		if (ACC & BIT(33))
			tty_send(ACC, 0);
		if (ACC & BIT(34))
			tty_strobe();
		break;
	case 051:
		/* Прерывание типа ответ */
		// besm6_debug("Response interrupt %016llo", ACC);
		if (ACC & BIT(33))
			tty_send(ACC, 1);
		break;
	case 052:
		/* Гашение запроса */
		// besm6_debug("Request clear %016llo", ACC);
		REQUEST &= ACC;
		break;
	case 053:
		/* Гашение ответа */
		// besm6_debug("Response clear %016llo", ACC);
		RESPONSE &= ACC;
		break;
	case 054:
		/* Конфигурация */
		// besm6_debug("Configuration %016llo", ACC);
		if (ACC & (BIT(34)|BIT(33))) {
			PRP |= PRP_REQUEST;
			REQUEST |= BIT(33);
		}
		// besm6_debug("PRP = %016llo", PRP);
		break;
	case 057: {
		int value = (BIT49-ACC)*20/37667.0;
		/* Таймер */
		// besm6_debug("Timer setup %016llo == %d ms", ACC, value);
		sim_activate (&clocks[1], value*MSEC);
		break;
	}
	case 060 ... 067:
		/* Запись в регистры приписки режима супервизора */
		mmu_setrp_kernel (Aex & 7, ACC);
		break;
	case 0100 ... 0137:
		/* Бит 1: управление блокировкой режима останова БРО.
		 * Биты 2 и 3 - признаки формирования контрольных
		 * разрядов (ПКП и ПКЛ). */
		if (Aex & 1)
			RUU |= RUU_AVOST_DISABLE;
		else
			RUU &= ~RUU_AVOST_DISABLE;
		if (Aex & 2)
			RUU |= RUU_CONVOL_RIGHT;
		else
			RUU &= ~RUU_CONVOL_RIGHT;
		if (Aex & 4)
			RUU |= RUU_CONVOL_LEFT;
		else
			RUU &= ~RUU_CONVOL_LEFT;
		break;
	case 0140:
			// besm6_debug("Watchdog");
			break;
	case 0141 ... 0177:
		/* TODO: управление блокировкой схемы
		 * автоматического запуска */
		longjmp (cpu_halt, STOP_UNIMPLEMENTED);
		break;
	case 0200 ... 0207:
		/* Чтение БРЗ */
		ACC = mmu_getcache (Aex & 7);
		break;
	case 0237:
		/* Чтение главного регистра прерываний */
		ACC = GRP;
		break;
	case 0246:
		ACC = MPRP;
		break;
	case 0247:
		ACC = PRP;
		break;
	case 0250:
		besm6_debug("Read cpu num, got %d", cpu_num);
		ACC = ~0LL ^ (1LL << (42-cpu_num));
		break;
	case 0252:
		// besm6_debug("Read request, got %016llo", REQUEST);
		ACC = REQUEST;
		break;
	case 0253:
		// besm6_debug("Read request, got %016llo", REQUEST);
		ACC = RESPONSE;
		break;
	default:
		/* Неиспользуемые адреса */
		besm6_debug ("*** %05o%s: РЕГ %o - неправильный адрес спец.регистра",
			svsPC, (RUU & RUU_RIGHT_INSTR) ? "п" : "л", Aex);
		break;
	}
}

void check_initial_setup ()
{
	const int MGRP_COPY = 01455;	/* OS version specific? */
	const int TAKEN = 0442;		/* fixed? */
	const int YEAR = 0221;		/* fixed */

	/* 47 р. яч. ЗАНЯТА - разр. приказы вообще */
	const t_value SETUP_REQS_ENABLED = 1LL << 46;

	/* 7 р. яч. ЗАНЯТА - разр любые приказы */
	const t_value ALL_REQS_ENABLED = 1 << 6;

	if ((memory[TAKEN].word & SETUP_REQS_ENABLED) == 0 ||
	    (memory[TAKEN].word & ALL_REQS_ENABLED) != 0 ||
	    (MGRP & GRP_PANEL_REQ) == 0) {
		/* Слишком рано, или уже не надо, или невовремя */
		return;
	}

	/* Выдаем приказы оператора СМЕ и ВРЕ,
	 * а дату корректируем непосредственно в памяти.
	 */
	/* Номер смены в 22-24 рр. МГРП: если еще не установлен, установить */
	if (((memory[MGRP_COPY].word >> 21) & 3) == 0) {
	/* приказ СМЕ: ТР6 = 010, ТР4 = 1, 22-24 р ТР5 - #смены */
		pult[6] = 010;
		pult[4] = 1;
		pult[5] = 1 << 21;
		GRP |= GRP_PANEL_REQ;
	} else {
	/* Яч. ГОД обновляем самостоятельно */
		time_t t;
		t_value date;
       	        time(&t);
		struct tm * d;
		d = localtime(&t);
		++d->tm_mon;
		date = (t_value) (d->tm_mday / 10) << 33 |
		(t_value) (d->tm_mday % 10) << 29 |
		(d->tm_mon / 10) << 28 |
		(d->tm_mon % 10) << 24 |
		(d->tm_year % 10) << 20 |
		((d->tm_year / 10) % 10) << 16 |
		(memory[YEAR].word & 7);
		memory[YEAR] = SET_TAG (date, TAG_NUMBER);
	/* приказ ВРЕ: ТР6 = 016, ТР5 = 9-14 р.-часы, 1-8 р.-минуты */
		pult[6] = 016;
		pult[4] = 0;
		pult[5] = (d->tm_hour / 10) << 12 |
			(d->tm_hour % 10) << 8 |
			(d->tm_min / 10) << 4 |
			(d->tm_min % 10);
		GRP |= GRP_PANEL_REQ;
	}
}

/*
 * Execute one instruction, placed on address PC:RUU_RIGHT_INSTR.
 * Increment delay. When stopped, perform a longjmp to cpu_halt,
 * sending a stop code.
 */
void cpu_one_inst ()
{
	int reg, opcode, addr, nextpc, next_mod;

	corr_stack = 0;
	t_value word = mmu_fetch (svsPC);
	if (RUU & RUU_RIGHT_INSTR)
		RK = word;		/* get right instruction */
	else
		RK = word >> 24;	/* get left instruction */

	RK &= BITS(24);

	reg = RK >> 20;
	if (RK & BIT(20)) {
		addr = RK & BITS(15);
		opcode = (RK >> 12) & 0370;
	} else {
		addr = RK & BITS(12);
		if (RK & BIT(19))
			addr |= 070000;
		opcode = (RK >> 12) & 077;
	}

	if (sim_deb && cpu_dev.dctrl) {
		fprintf (sim_deb, "*** %05o%s: ", svsPC,
			(RUU & RUU_RIGHT_INSTR) ? "п" : "л");
		besm6_fprint_cmd (sim_deb, RK);
		fprintf (sim_deb, "\tСМ=");
		fprint_sym (sim_deb, 0, &ACC, 0, 0);
		fprintf (sim_deb, "\tРАУ=%02o", RAU);
		if (reg)
			fprintf (sim_deb, "\tМ[%o]=%05o", reg, M[reg]);
		fprintf (sim_deb, "\n");
	}
	nextpc = ADDR(svsPC + 1);
	if (RUU & RUU_RIGHT_INSTR) {
		svsPC += 1;			/* increment PC */
		RUU &= ~RUU_RIGHT_INSTR;
	} else {
		mmu_prefetch(nextpc | (IS_SUPERVISOR(RUU) ? BIT(16) : 0), 0);
		RUU |= RUU_RIGHT_INSTR;
	}

	if (RUU & RUU_MOD_RK) {
		addr = ADDR (addr + M[MOD]);
	}
	next_mod = 0;
	delay = 0;

	switch (opcode) {
	case 000:					/* зп, atx */
		Aex = ADDR (addr + M[reg]);
		mmu_store (Aex, ACC, RUUTAG);
		if (! addr && reg == 017)
			M[017] = ADDR (M[017] + 1);
		delay = MEAN_TIME (3, 3);
		break;
	case 001:					/* зпм, stx */
		Aex = ADDR (addr + M[reg]);
		mmu_store (Aex, ACC, RUUTAG);
		M[017] = ADDR (M[017] - 1);
		corr_stack = 1;
		ACC = mmu_load (M[017]);
		RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (6, 6);
		break;
	case 002:					/* рег, mod */
		Aex = ADDR (addr + M[reg]);
		if (! IS_SUPERVISOR (RUU))
			longjmp (cpu_halt, STOP_BADCMD);
		cmd_002 ();
		/* Режим АУ - логический, если операция была "чтение" */
		if (Aex & 0200)
			RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (3, 3);
		break;
	case 003:					/* счм, xts */
		mmu_store (M[017], ACC, RUUTAG);
		M[017] = ADDR (M[017] + 1);
		corr_stack = -1;
		Aex = ADDR (addr + M[reg]);
		ACC = mmu_load (Aex);
		RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (6, 6);
		break;
	case 004:					/* сл, a+x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add (mmu_load (Aex), 0, 0);
		RAU = SET_ADDITIVE (RAU);
		delay = MEAN_TIME (3, 11);
		break;
	case 005:					/* вч, a-x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add (mmu_load (Aex), 0, 1);
		RAU = SET_ADDITIVE (RAU);
		delay = MEAN_TIME (3, 11);
		break;
	case 006:					/* вчоб, x-a */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add (mmu_load (Aex), 1, 0);
		RAU = SET_ADDITIVE (RAU);
		delay = MEAN_TIME (3, 11);
		break;
	case 007:					/* вчаб, amx */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add (mmu_load (Aex), 1, 1);
		RAU = SET_ADDITIVE (RAU);
		delay = MEAN_TIME (3, 11);
		break;
	case 010:					/* сч, xta */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		ACC = mmu_load (Aex);
		RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (3, 3);
		break;
	case 011:					/* и, aax */
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
	case 012:					/* нтж, aex */
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
	case 013:					/* слц, arx */
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
	case 014:					/* знак, avx */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_change_sign (mmu_load (Aex) >> 40 & 1);
		RAU = SET_ADDITIVE (RAU);
		delay = MEAN_TIME (3, 5);
		break;
	case 015:					/* или, aox */
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
	case 016:					/* дел, a/x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_divide (mmu_load (Aex));
		RAU = SET_MULTIPLICATIVE (RAU);
		delay = MEAN_TIME (3, 50);
		break;
	case 017:					/* умн, a*x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_multiply (mmu_load (Aex));
		RAU = SET_MULTIPLICATIVE (RAU);
		delay = MEAN_TIME (3, 18);
		break;
	case 020:					/* сбр, apx */
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
	case 021:					/* рзб, aux */
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
	case 022:					/* чед, acx */
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
	case 023:					/* нед, anx */
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
	case 024:					/* слп, e+x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add_exponent ((mmu_load (Aex) >> 41) - 64);
		RAU = SET_MULTIPLICATIVE (RAU);
		delay = MEAN_TIME (3, 5);
		break;
	case 025:					/* вчп, e-x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add_exponent (64 - (mmu_load (Aex) >> 41));
		RAU = SET_MULTIPLICATIVE (RAU);
		delay = MEAN_TIME (3, 5);
		break;
	case 026: {					/* сд, asx */
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
	case 027:					/* рж, xtr */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		RAU = (mmu_load (Aex) >> 41) & 077;
		delay = MEAN_TIME (3, 3);
		break;
	case 030:					/* счрж, rte */
		Aex = ADDR (addr + M[reg]);
		ACC = (t_value) (RAU & Aex & 0177) << 41;
		RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (3, 3);
		break;
	case 031: 					/* счмр, yta */
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
	case 032:					/* зпп */
		if (! IS_SUPERVISOR (RUU))
			longjmp (cpu_halt, STOP_BADCMD);
		Aex = ADDR (addr + M[reg]);
		besm6_debug("Fullword store to %o: Acc=%016llo Rmr=%016llo", Aex, ACC, RMR);
		mmu_store (Aex, (ACC & BITS48) | ((RMR&0xFFFF00000000LL)<<16), svsTAG);
#if 0
		if (! addr && reg == 017)
			M[017] = ADDR (M[017] + 1);
#endif
		delay = MEAN_TIME (3, 3);
		break;
	case 033:					/* счп */
		if (! IS_SUPERVISOR (RUU))
			longjmp (cpu_halt, STOP_BADCMD);
#if 0
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
#endif
		Aex = ADDR (addr + M[reg]);
		ACC = mmu_load_full (Aex).word;
		RMR = (ACC >> 48) << 32;
		ACC &= BITS48;
		besm6_debug("Fullword read from %o: Acc=%016llo Rmr=%016llo", Aex, ACC, RMR);
		RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (3, 3);
		break;
	case 034:					/* слпа, e+n */
		Aex = ADDR (addr + M[reg]);
		besm6_add_exponent ((Aex & 0177) - 64);
		RAU = SET_MULTIPLICATIVE (RAU);
		delay = MEAN_TIME (3, 5);
		break;
	case 035:					/* вчпа, e-n */
		Aex = ADDR (addr + M[reg]);
		besm6_add_exponent (64 - (Aex & 0177));
		RAU = SET_MULTIPLICATIVE (RAU);
		delay = MEAN_TIME (3, 5);
		break;
	case 036: {					/* сда, asn */
		int n;
		Aex = ADDR (addr + M[reg]);
		n = (Aex & 0177) - 64;
		besm6_shift (n);
		RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (3, 4 + abs (n));
		break;
	}
	case 037:					/* ржа, ntr */
		Aex = ADDR (addr + M[reg]);
		RAU = Aex & 077;
		delay = MEAN_TIME (3, 3);
		break;
	case 040:					/* уи, ati */
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
				M[reg] |= BIT(16);

		} else
			M[Aex & 017] = ADDR (ACC);
		M[0] = 0;
		delay = MEAN_TIME (14, 3);
		break;
	case 041: {					/* уим, sti */
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
			M[rg] |= BIT(16);
		M[0] = 0;
		RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (14, 3);
		break;
	}
	case 042:					/* счи, ita */
		delay = MEAN_TIME (6, 3);
load_modifier:	Aex = ADDR (addr + M[reg]);
		ACC = ADDR(M[Aex & (IS_SUPERVISOR (RUU) ? 037 : 017)]);
		RAU = SET_LOGICAL (RAU);
		break;
	case 043:					/* счим, its */
		mmu_store (M[017], ACC, RUUTAG);
		M[017] = ADDR (M[017] + 1);
		delay = MEAN_TIME (9, 6);
		goto load_modifier;
	case 044:					/* уии, mtj */
		Aex = addr;
		if (IS_SUPERVISOR (RUU)) {
transfer_modifier:	M[Aex & 037] = M[reg];
			if ((M[PSW] & PSW_MMAP_DISABLE) &&
			    ((Aex & 037) == IBP || (Aex & 037) == DWP))
				M[Aex & 037] |= BIT(16);

		} else
			M[Aex & 017] = M[reg];
		M[0] = 0;
		delay = 6;
		break;
	case 045:					/* сли, j+m */
		Aex = addr;
		if ((Aex & 020) && IS_SUPERVISOR (RUU))
			goto transfer_modifier;
		M[Aex & 017] = ADDR (M[Aex & 017] + M[reg]);
		M[0] = 0;
		delay = 6;
		break;
	case 046:					/* счпс */
		besm6_debug("СЧПС %o(%o)", addr, reg);
		Aex = addr;
		switch (reg) {
		case 1: {
			ACC = mmu_memaccess_sync(Aex).word & BITS48;
			break;
		}
		case 5:
			// read with tag
			ACC = mmu_load_full (Aex).word;
			RMR = (ACC >> 48) << 32;
			ACC &= BITS48;
			break;
		}
		SET_LOGICAL(RAU);
		delay = 4;
		break;
	case 047 ... 077:				/* э47...э77 */
	case 0200:					/* э20 */
	case 0210:					/* э21 */
	stop_as_extracode:
		Aex = ADDR (addr + M[reg]);
		if (! sim_deb && sim_log && cpu_dev.dctrl && opcode != 075) {
			/* Если включен console log и cpu debug,
			 * но нет console debug, то печатаем только экстракоды.
			 * Пропускаем э75, их обычно слишком много. */
			t_value word = mmu_load (Aex);
			fprintf (sim_log, "*** %05o%s: ", svsPC,
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
			svsPC = 0500 + opcode;		/* э50-э77 */
		else
			svsPC = 0540 + (opcode >> 3);	/* э20, э21 */
		RUU &= ~RUU_RIGHT_INSTR;
		delay = 7;
		break;
	case 0220:					/* мода, utc */
		Aex = ADDR (addr + M[reg]);
		next_mod = Aex;
		delay = 4;
		break;
	case 0230:					/* мод, wtc */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		next_mod = ADDR (mmu_load (Aex));
		delay = MEAN_TIME (13, 3);
		break;
	case 0240:					/* уиа, vtm */
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
	case 0250:					/* слиа, utm */
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
	case 0260:					/* по, uza */
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
		svsPC = Aex;
		RUU &= ~RUU_RIGHT_INSTR;
		delay += 3;
		break;
	case 0270:					/* пе, u1a */
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
		svsPC = Aex;
		RUU &= ~RUU_RIGHT_INSTR;
		delay += 3;
		break;
	case 0300:					/* пб, uj */
		Aex = ADDR (addr + M[reg]);
		svsPC = Aex;
		RUU &= ~RUU_RIGHT_INSTR;
		delay = 7;
		break;
	case 0310:					/* пв, vjm */
		Aex = addr;
		M[reg] = nextpc;
		M[0] = 0;
		svsPC = addr;
		RUU &= ~RUU_RIGHT_INSTR;
		delay = 7;
		break;
	case 0320:					/* выпр, iret */
		Aex = addr;
		if (! IS_SUPERVISOR (RUU)) {
			longjmp (cpu_halt, STOP_BADCMD);
		}
		M[PSW] = (M[PSW] & PSW_WRITE_WATCH) |
			(M[SPSW] & (SPSW_INTR_DISABLE |
			SPSW_MMAP_DISABLE | SPSW_PROT_DISABLE));
		svsPC = M[(reg & 3) | 030];
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
	case 0330:					/* стоп, stop */
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
	case 0340:					/* пио, vzm */
branch_zero:	Aex = addr;
		delay = 4;
		if (! M[reg]) {
			svsPC = addr;
			RUU &= ~RUU_RIGHT_INSTR;
			delay += 3;
		}
		break;
	case 0350:					/* пино, v1m */
		Aex = addr;
		delay = 4;
		if (M[reg]) {
			svsPC = addr;
			RUU &= ~RUU_RIGHT_INSTR;
			delay += 3;
		}
		break;
	case 0360:					/* э36, *36 */
		for (int i = 0; i < 8; ++i) mmu_flush(i);
		goto branch_zero;
	case 0370:					/* цикл, vlm */
		Aex = addr;
		delay = 4;
		if (! M[reg])
			break;
		M[reg] = ADDR (M[reg] + 1);
		svsPC = addr;
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
	if (RUU == 047 && svsPC == 04440 && RK == 067704440) {
		/* Притормаживаем выполнение каждой команды холостого цикла,
		 * чтобы быстрее обрабатывались прерывания: ускоряются
		 * терминалы и АЦПУ. */
		delay = sim_interval;
#if 0
		/* Если периферия простаивает, освобождаем процессор
		 * до следующего тика таймера. */
		if (vt_is_idle() &&
		    printer_is_idle() && fs_is_idle()) {
			check_initial_setup ();
			pause ();
		}
#endif
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
	M[IRET] = svsPC;
	M[PSW] |= PSW_INTR_DISABLE | PSW_MMAP_DISABLE | PSW_PROT_DISABLE;
	if (RUU & RUU_MOD_RK) {
		M[SPSW] |= SPSW_MOD_RK;
		RUU &= ~RUU_MOD_RK;
	}
	svsPC = 0500;
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
	M[IRET] = svsPC;
	M[PSW] |= PSW_INTR_DISABLE | PSW_MMAP_DISABLE | PSW_PROT_DISABLE;
	if (RUU & RUU_MOD_RK) {
		M[SPSW] |= SPSW_MOD_RK;
		RUU &= ~RUU_MOD_RK;
	}
	svsPC = 0501;
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
	svsPC = svsPC & BITS(15);				/* mask PC */
	sim_cancel_step ();				/* defang SCP step */
	mmu_setup ();					/* copy RP to TLB */

	/* An internal interrupt or user intervention */
	r = setjmp (cpu_halt);
	if (r) {
		M[017] += corr_stack;
		if (cpu_dev.dctrl) {
			const char *message = (r >= SCPE_BASE) ?
				scp_error_messages [r - SCPE_BASE] :
				sim_stop_messages [r];
			besm6_debug ("/// %05o%s: %s", svsPC,
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
ret:			// besm6_draw_panel();
			return r;
		case STOP_RWATCH:
		case STOP_WWATCH:
			/* Step back one insn to reexecute it */
			if (! (RUU & RUU_RIGHT_INSTR)) {
				--svsPC;
			}
			RUU ^= RUU_RIGHT_INSTR;
			goto ret;
		case STOP_BADCMD:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				goto ret;
			op_int_1 (sim_stop_messages[r]);
			// SPSW_NEXT_RK is not important for this interrupt
			GRP |= GRP_ILL_INSN;
			break;
		case STOP_INSN_CHECK:
			if (M[PSW] & PSW_CHECK_HALT)		/* ПоК */
				goto ret;
			op_int_1 (sim_stop_messages[r]);
			// SPSW_NEXT_RK must be 0 for this interrupt; it is already
			GRP |= GRP_INSN_CHECK;
			break;
		case STOP_INSN_PROT:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				goto ret;
			if (RUU & RUU_RIGHT_INSTR) {
				++svsPC;
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
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				goto ret;
#endif
			if (RUU & RUU_RIGHT_INSTR) {
				++svsPC;
			}
			RUU ^= RUU_RIGHT_INSTR;
			op_int_1 (sim_stop_messages[r]);
			M[SPSW] |= SPSW_NEXT_RK;
			// The offending virtual page is in bits 5-9
			GRP |= GRP_OPRND_PROT;
			GRP = GRP_SET_PAGE (GRP, iintr_data);
			break;
		case STOP_RAM_CHECK:
			if (M[PSW] & PSW_CHECK_HALT)		/* ПоК */
				goto ret;
			op_int_1 (sim_stop_messages[r]);
			// The offending interleaved block # is in bits 1-3.
			GRP |= GRP_CHECK | GRP_RAM_CHECK;
			GRP = GRP_SET_BLOCK (GRP, iintr_data);
			break;
		case STOP_CACHE_CHECK:
			if (M[PSW] & PSW_CHECK_HALT)		/* ПоК */
				goto ret;
			op_int_1 (sim_stop_messages[r]);
			// The offending BRZ # is in bits 1-3.
			GRP |= GRP_CHECK;
			GRP &= ~GRP_RAM_CHECK;
			GRP = GRP_SET_BLOCK (GRP, iintr_data);
			break;
		case STOP_INSN_ADDR_MATCH:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				goto ret;
			if (RUU & RUU_RIGHT_INSTR) {
				++svsPC;
			}
			RUU ^= RUU_RIGHT_INSTR;
			op_int_1 (sim_stop_messages[r]);
			M[SPSW] |= SPSW_NEXT_RK;
			GRP |= GRP_BREAKPOINT;
			break;
		case STOP_LOAD_ADDR_MATCH:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				goto ret;
			if (RUU & RUU_RIGHT_INSTR) {
				++svsPC;
			}
			RUU ^= RUU_RIGHT_INSTR;
			op_int_1 (sim_stop_messages[r]);
			M[SPSW] |= SPSW_NEXT_RK;
			GRP |= GRP_WATCHPT_R;
			break;
		case STOP_STORE_ADDR_MATCH:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				goto ret;
			if (RUU & RUU_RIGHT_INSTR) {
				++svsPC;
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
			if (! (RUU & RUU_AVOST_DISABLE) &&	/* ! БРО */
			    ((M[PSW] & PSW_INTR_HALT) ||	/* ПоП */
			     (M[PSW] & PSW_CHECK_HALT)))	/* ПоК */
				goto ret;
			op_int_1 (sim_stop_messages[r]);
			GRP |= GRP_OVERFLOW|GRP_RAM_CHECK;
			break;
		case STOP_DIVZERO:
			if (! (RUU & RUU_AVOST_DISABLE) &&	/* ! БРО */
			    ((M[PSW] & PSW_INTR_HALT) ||	/* ПоП */
			     (M[PSW] & PSW_CHECK_HALT)))	/* ПоК */
				goto ret;
			op_int_1 (sim_stop_messages[r]);
			GRP |= GRP_DIVZERO|GRP_RAM_CHECK;
			break;
		}
		++iintr;
	}

	if (iintr > 1) {
		// besm6_draw_panel();
		return STOP_DOUBLE_INTR;
	}
	/* Main instruction fetch/decode loop */
	for (;;) {
		if (sim_interval <= 0) {		/* check clock queue */
			r = sim_process_event ();
			if (r) {
				// besm6_draw_panel();
				return r;
			}
		}

		if (svsPC > BITS(15)) {			/* выход за пределы памяти */
			// besm6_draw_panel();
			return STOP_RUNOUT;		/* stop simulation */
		}

		if (sim_brk_summ & SWMASK('E') &&	/* breakpoint? */
		    sim_brk_test (svsPC, SWMASK ('E'))) {
			// besm6_draw_panel();
			return STOP_IBKPT;		/* stop simulation */
		}

		if (! iintr && ! (RUU & RUU_RIGHT_INSTR) &&
		    ! (M[PSW] & PSW_INTR_DISABLE) && ((GRP & MGRP) || (PRP & MPRP))) {
			/* external interrupt */
			op_int_2();
		}
		cpu_one_inst ();			/* one instr */
		iintr = 0;
		if (redraw_panel) {
			// besm6_draw_panel();
			redraw_panel = 0;
		}

		if (delay < 1)
			delay = 1;
		sim_interval -= delay;			/* count down delay */
		if (sim_step && (--sim_step <= 0)) {	/* do step count */
			// besm6_draw_panel();
			return SCPE_STOP;
		}
	}
}

t_stat slow_clk (UNIT * this)
{
	/*besm6_debug ("*** таймер 80 мсек");*/
	GRP |= GRP_SLOW_CLK;
	return sim_activate (this, MSEC*125/2);
}

/*
 * В 9-й части частота таймера 250 Гц (4 мс),
 * в жизни - 50 Гц (20 мс).
 */
t_stat fast_clk (UNIT * this)
{
	// besm6_debug ("*** таймер");
	PRP |= PRP_TIMER;
	return SCPE_OK; // sim_activate (this, 20*MSEC);
}

t_stat clk_reset (DEVICE * dev)
{
	/* Схема автозапуска включается по нереализованной кнопке "МР" */
#ifdef SOFT_CLOCK
	sim_activate (&clocks[0], MSEC*125/2);
	return sim_activate (&clocks[1], 20*MSEC);
#else
	return SCPE_OK;
#endif
}

DEVICE clock_dev = {
	"CLK", clocks, NULL, NULL,
	2, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL, NULL,
	DEV_DEBUG
};
