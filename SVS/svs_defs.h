/*
 * besm6_defs.h: BESM-6 simulator definitions
 *
 * Copyright (c) 2009, Serge Vakulenko
 * Copyright (c) 2009, Leonid Broukhis
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
#ifndef _BESM6_DEFS_H_
#define _BESM6_DEFS_H_    0

#include "sim_defs.h"				/* simulator defns */
#include <setjmp.h>

/*
 * Memory.
 */
#define NREGS		30			/* number of registers-modifiers */
#define MEMSIZE		(1024 * 1024)		/* memory size, words */

/*
 * Drums and disks.
 *
 * One zone contains 1024 words of user memory and 8 system data words.
 * Every word (t_value) is stored as 8-byte record, low byte first.
 * System data is stored first, then user data.
 */
#define ZONE_SIZE	(8 + 1024)		/* 1kword zone size, words */
#define DRUM_SIZE	(256 * ZONE_SIZE)	/* drum size per controller, words */
#define DISK_SIZE	(1024 * ZONE_SIZE)	/* disk size per unit, words */

/*
 * Simulator stop codes
 */
enum {
	STOP_STOP = 1,				/* STOP */
	STOP_IBKPT,				/* SIMH breakpoint */
	STOP_RWATCH,				/* SIMH read watchpoint */
	STOP_WWATCH,				/* SIMH write watchpoint */
	STOP_RUNOUT,				/* run out end of memory limits */
	STOP_BADCMD,				/* invalid instruction */
	STOP_INSN_CHECK,			/* not an instruction */
	STOP_INSN_PROT,				/* fetch from blocked page */
	STOP_OPERAND_PROT,			/* load from blocked page */
	STOP_RAM_CHECK,				/* RAM parity error */
	STOP_CACHE_CHECK,			/* data cache parity error */
	STOP_OVFL,				/* arith. overflow */
	STOP_DIVZERO,				/* division by 0 or denorm */
	STOP_DOUBLE_INTR,			/* double internal interrupt */
	STOP_DRUMINVDATA,			/* reading unformatted drum */
	STOP_DISKINVDATA,			/* reading unformatted disk */
	STOP_INSN_ADDR_MATCH,			/* fetch address matched breakpt reg */
	STOP_LOAD_ADDR_MATCH,			/* load address matched watchpt reg */
	STOP_STORE_ADDR_MATCH,			/* store address matched watchpt reg */
	STOP_UNIMPLEMENTED,			/* unimplemented 033 or 002 insn feature */
};

/*
 * Разряды машинного слова, справа налево, начиная с 1.
 */
#define BIT(n)		(1LL << (n-1))		/* один бит, от 1 до 32 */
#define BIT40		000010000000000000LL	/* 40-й бит - старший разряд мантиссы */
#define BIT41		000020000000000000LL	/* 41-й бит - знак */
#define BIT42		000040000000000000LL	/* 42-й бит - дубль-знак в мантиссе */
#define BIT48		004000000000000000LL	/* 48-й бит - знак порядка */
#define BIT49	        010000000000000000LL	/* бит 49 */
#define BITS(n)		(~0U >> (32-n))		/* маска битов n..1 */
#define BITS40		00017777777777777LL	/* биты 41..1 - мантисса */
#define BITS41		00037777777777777LL	/* биты 41..1 - мантисса и знак */
#define BITS42		00077777777777777LL	/* биты 42..1 - мантисса и оба знака */
#define BITS48		07777777777777777LL	/* биты 48..1 */
#define BITS48_42	07740000000000000LL	/* биты 48..42 - порядок */
#define ADDR(x)		((x) & BITS(15))	/* адрес слова */

/*
 * Работа со сверткой. Значение разрядов свертки слова равно значению
 * регистров ПКЛ и ПКП при записи слова.
 * 00 - командная свертка
 * 01 или 10 - контроль числа
 * 11 - числовая свертка
 * В памяти биты свертки имитируют четность полуслов.
 */
#define IS_INSN(x)		(x.tag == TAG_INSN)
#define IS_NUMBER(x)		(x.tag == TAG_INSN || \
				 x.tag == TAG_NUMBER)
#define TAG_INSN		035
#define TAG_NUMBER		036
#define TAG_BITSET		020

typedef struct {
	t_value word;
	unsigned char tag;
} t_mem;

#define NUM_UNITS	16
#define CPU_OFFSET	6
typedef struct {
	unsigned char request[NUM_UNITS];
	unsigned char response[NUM_UNITS];
	unsigned pid[NUM_UNITS];
	t_value configuration;
	t_mem memory[MEMSIZE];
} t_shared;

static inline t_mem SET_TAG(t_value x, int c) { t_mem m; m.word = x; m.tag = c; return m; }
#define RUUTAG (TAG_INSN ^ (RUU & 3))
static inline int IS_TAG_INSN(t_mem x) { return x.tag == TAG_INSN; }

/*
 * Вычисление правдоподобного времени выполнения команды,
 * зная количество тактов в УУ и среднее в АУ.
 * Предполагаем, что в 50% случаев происходит совмещение
 * выполнения, поэтому суммируем большее и половину
 * от меньшего значения.
 */
#define MEAN_TIME(x,y)	(x>y ? x+y/2 : x/2+y)

/*
 * Считаем, что моделируеммая машина имеет опорную частоту 10 МГц.
 */
#define USEC	10		/* одна микросекунда - десять тактов */
#define MSEC	(1000*USEC)	/* одна миллисекунда */

extern uint32 sim_brk_types, sim_brk_dflt, sim_brk_summ; /* breakpoint info */
extern int32 sim_interval, sim_step;
extern FILE *sim_deb, *sim_log;
extern int32 sim_switches;

extern UNIT cpu_unit;
extern t_shared * shared;
extern t_mem * memory;
extern t_value pult [8];
extern uint32 svsPC, RAU, RUU;
extern uint8 svsTAG;
extern uint32 M[NREGS];
extern t_mem BRZ[8];
extern t_value RP[8], GRP, MGRP;
extern uint32 PRP, MPRP;
extern t_value ACC, RMR;
extern uint32 BAZ[8], TABST, RZ;
extern DEVICE cpu_dev, mmu_dev;
extern DEVICE clock_dev;
extern DEVICE tty_dev;
void tty_send (t_value mask, int high_nibble);
t_value tty_query (void);
void tty_strobe();

extern jmp_buf cpu_halt;

/*
 * Разряды режима АУ.
 */
#define RAU_NORM_DISABLE        001     /* блокировка нормализации */
#define RAU_ROUND_DISABLE       002     /* блокировка округления */
#define RAU_LOG                 004     /* признак логической группы */
#define RAU_MULT                010     /* признак группы умножения */
#define RAU_ADD                 020     /* признак группы слодения */
#define RAU_OVF_DISABLE         040     /* блокировка переполнения */

#define RAU_MODE                (RAU_LOG | RAU_MULT | RAU_ADD)
#define SET_MODE(x,m)           (((x) & ~RAU_MODE) | (m))
#define SET_LOGICAL(x)          (((x) & ~RAU_MODE) | RAU_LOG)
#define SET_MULTIPLICATIVE(x)   (((x) & ~RAU_MODE) | RAU_MULT)
#define SET_ADDITIVE(x)         (((x) & ~RAU_MODE) | RAU_ADD)
#define IS_LOGICAL(x)           (((x) & RAU_MODE) == RAU_LOG)
#define IS_MULTIPLICATIVE(x)    (((x) & (RAU_ADD | RAU_MULT)) == RAU_MULT)
#define IS_ADDITIVE(x)          ((x) & RAU_ADD)

/*
 * Искусственный регистр режимов УУ, в реальной машине отсутствует.
 */
#define RUU_CONVOL_RIGHT	000001	/* ПКП - признак контроля правой половины */
#define RUU_CONVOL_LEFT		000002	/* ПКЛ - признак контроля левой половины */
#define RUU_EXTRACODE		000004	/* РежЭ - режим экстракода */
#define RUU_INTERRUPT		000010	/* РежПр - режим прерывания */
#define RUU_MOD_RK		000020	/* ПрИК - модификация регистром М[16] */
#define RUU_AVOST_DISABLE	000040	/* БРО - блокировка режима останова */
#define RUU_RIGHT_INSTR		000400	/* ПрК - признак правой команды */

#define IS_SUPERVISOR(x)	((x) & (RUU_EXTRACODE | RUU_INTERRUPT))
#define SET_SUPERVISOR(x,m)	(((x) & ~(RUU_EXTRACODE | RUU_INTERRUPT)) | (m))

/*
 * Специальные регистры.
 */
#define MOD	020	/* модификатор адреса */
#define PSW	021     /* режимы УУ */
#define SPSW	027     /* упрятывание режимов УУ */
#define ERET	032     /* адрес возврата из экстракода */
#define IRET	033     /* адрес возврата из прерывания */
#define IBP	034     /* адрес прерывания по выполнению */
#define DWP	035     /* адрес прерывания по чтению/записи */

/*
 * Регистр 021: режимы УУ.
 * PSW: program status word.
 */
#define PSW_MMAP_DISABLE	000001	/* БлП - блокировка приписки */
#define PSW_PROT_DISABLE	000002	/* БлЗ - блокировка защиты */
#define PSW_INTR_HALT		000004	/* ПоП - признак останова при
					   любом внутреннем прерывании */
#define PSW_CHECK_HALT		000010	/* ПоК - признак останова при
					   прерывании по контролю */
#define PSW_WRITE_WATCH		000020	/* Зп(М29) - признак совпадения адреса
					   операнда прии записи в память
					   с содержанием регистра М29 */
#define PSW_INTR_DISABLE	002000	/* БлПр - блокировка внешнего прерывания */
#define PSW_AUT_B		004000	/* АвтБ - признак режима Автомат Б */

/*
 * Регистр 027: сохранённые режимы УУ.
 * SPSW: saved program status word.
 */
#define SPSW_MMAP_DISABLE	000001	/* БлП - блокировка приписки */
#define SPSW_PROT_DISABLE	000002	/* БлЗ - блокировка защиты */
#define SPSW_EXTRACODE		000004	/* РежЭ - режим экстракода */
#define SPSW_INTERRUPT		000010	/* РежПр - режим прерывания */
#define SPSW_MOD_RK		000020	/* ПрИК(РК) - на регистр РК принята
					   команда, которая должна быть
					   модифицирована регистром М[16] */
#define SPSW_MOD_RR		000040	/* ПрИК(РР) - на регистре РР находится
					   команда, выполненная с модификацией */
#define SPSW_UNKNOWN		000100	/* НОК? вписано карандашом в 9 томе */
#define SPSW_RIGHT_INSTR	000400	/* ПрК - признак правой команды */
#define SPSW_NEXT_RK		001000	/* ГД./ДК2 - на регистр РК принята
					   команда, следующая после вызвавшей
					   прерывание */
#define SPSW_INTR_DISABLE	002000	/* БлПр - блокировка внешнего прерывания */

/*
 * Кириллица Unicode.
 */
#define CYRILLIC_CAPITAL_LETTER_A		0x0410
#define CYRILLIC_CAPITAL_LETTER_BE		0x0411
#define CYRILLIC_CAPITAL_LETTER_VE		0x0412
#define CYRILLIC_CAPITAL_LETTER_GHE		0x0413
#define CYRILLIC_CAPITAL_LETTER_DE		0x0414
#define CYRILLIC_CAPITAL_LETTER_IE		0x0415
#define CYRILLIC_CAPITAL_LETTER_ZHE		0x0416
#define CYRILLIC_CAPITAL_LETTER_ZE		0x0417
#define CYRILLIC_CAPITAL_LETTER_I		0x0418
#define CYRILLIC_CAPITAL_LETTER_SHORT_I		0x0419
#define CYRILLIC_CAPITAL_LETTER_KA		0x041a
#define CYRILLIC_CAPITAL_LETTER_EL		0x041b
#define CYRILLIC_CAPITAL_LETTER_EM		0x041c
#define CYRILLIC_CAPITAL_LETTER_EN		0x041d
#define CYRILLIC_CAPITAL_LETTER_O		0x041e
#define CYRILLIC_CAPITAL_LETTER_PE		0x041f
#define CYRILLIC_CAPITAL_LETTER_ER		0x0420
#define CYRILLIC_CAPITAL_LETTER_ES		0x0421
#define CYRILLIC_CAPITAL_LETTER_TE		0x0422
#define CYRILLIC_CAPITAL_LETTER_U		0x0423
#define CYRILLIC_CAPITAL_LETTER_EF		0x0424
#define CYRILLIC_CAPITAL_LETTER_HA		0x0425
#define CYRILLIC_CAPITAL_LETTER_TSE		0x0426
#define CYRILLIC_CAPITAL_LETTER_CHE		0x0427
#define CYRILLIC_CAPITAL_LETTER_SHA		0x0428
#define CYRILLIC_CAPITAL_LETTER_SHCHA		0x0429
#define CYRILLIC_CAPITAL_LETTER_HARD_SIGN	0x042a
#define CYRILLIC_CAPITAL_LETTER_YERU		0x042b
#define CYRILLIC_CAPITAL_LETTER_SOFT_SIGN	0x042c
#define CYRILLIC_CAPITAL_LETTER_E		0x042d
#define CYRILLIC_CAPITAL_LETTER_YU		0x042e
#define CYRILLIC_CAPITAL_LETTER_YA		0x042f
#define CYRILLIC_SMALL_LETTER_A			0x0430
#define CYRILLIC_SMALL_LETTER_BE		0x0431
#define CYRILLIC_SMALL_LETTER_VE		0x0432
#define CYRILLIC_SMALL_LETTER_GHE		0x0433
#define CYRILLIC_SMALL_LETTER_DE		0x0434
#define CYRILLIC_SMALL_LETTER_IE		0x0435
#define CYRILLIC_SMALL_LETTER_ZHE		0x0436
#define CYRILLIC_SMALL_LETTER_ZE		0x0437
#define CYRILLIC_SMALL_LETTER_I			0x0438
#define CYRILLIC_SMALL_LETTER_SHORT_I		0x0439
#define CYRILLIC_SMALL_LETTER_KA		0x043a
#define CYRILLIC_SMALL_LETTER_EL		0x043b
#define CYRILLIC_SMALL_LETTER_EM		0x043c
#define CYRILLIC_SMALL_LETTER_EN		0x043d
#define CYRILLIC_SMALL_LETTER_O			0x043e
#define CYRILLIC_SMALL_LETTER_PE		0x043f
#define CYRILLIC_SMALL_LETTER_ER		0x0440
#define CYRILLIC_SMALL_LETTER_ES		0x0441
#define CYRILLIC_SMALL_LETTER_TE		0x0442
#define CYRILLIC_SMALL_LETTER_U			0x0443
#define CYRILLIC_SMALL_LETTER_EF		0x0444
#define CYRILLIC_SMALL_LETTER_HA		0x0445
#define CYRILLIC_SMALL_LETTER_TSE		0x0446
#define CYRILLIC_SMALL_LETTER_CHE		0x0447
#define CYRILLIC_SMALL_LETTER_SHA		0x0448
#define CYRILLIC_SMALL_LETTER_SHCHA		0x0449
#define CYRILLIC_SMALL_LETTER_HARD_SIGN		0x044a
#define CYRILLIC_SMALL_LETTER_YERU		0x044b
#define CYRILLIC_SMALL_LETTER_SOFT_SIGN		0x044c
#define CYRILLIC_SMALL_LETTER_E			0x044d
#define CYRILLIC_SMALL_LETTER_YU		0x044e
#define CYRILLIC_SMALL_LETTER_YA		0x044f

/*
 * Процедуры работы с памятью
 */
extern void mmu_store (int addr, t_value word, uint8 tag);
extern t_mem mmu_load_full (int addr);
extern t_mem mmu_memaccess_sync (int addr);
extern t_value mmu_load(int addr);
extern t_value mmu_fetch (int addr);
extern t_mem mmu_prefetch (int addr, int actual);
extern void mmu_setcache (int idx, t_value word);
extern t_value mmu_getcache (int idx);
extern void mmu_flush (int idx);
extern void mmu_setrp (int idx, t_value word);
extern void mmu_setrp_kernel (int idx, t_value word);
extern void mmu_setup (void);
extern void mmu_setprotection (int idx, t_value word);
extern void mmu_print_brz (void);

/*
 * Отладочная выдача.
 */
void besm6_fprint_cmd (FILE *of, uint32 cmd);
void besm6_log (const char *fmt, ...);
void besm6_log_cont (const char *fmt, ...);
void besm6_debug (const char *fmt, ...);
t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
	UNIT *uptr, int32 sw);
void besm6_draw_panel (void);

/*
 * Арифметика.
 */
double besm6_to_ieee (t_value word);
void besm6_add (t_value val, int negate_acc, int negate_val);
void besm6_divide (t_value val);
void besm6_multiply (t_value val);
void besm6_change_sign (int sign);
void besm6_add_exponent (int val);
int besm6_highest_bit (t_value val);
void besm6_shift (int toright);
int besm6_count_ones (t_value word);
t_value besm6_pack (t_value val, t_value mask);
t_value besm6_unpack (t_value val, t_value mask);

/*
 * Разряды главного регистра прерываний (ГРП)
 * Внешние:
 */
#define GRP_PANEL_REQ	00000020000000000LL	/* 32 */
#define GRP_WATCHDOG	00000000000002000LL	/* 11 */
#define GRP_SLOW_CLK	00000000000001000LL	/* 10 */
/* Внутренние: */
#define GRP_DIVZERO	00000000034000000LL	/* 23-21 */
#define GRP_OVERFLOW	00000000014000000LL	/* 22-21 */
#define GRP_CHECK 	00000000004000000LL	/* 21 */
#define GRP_OPRND_PROT	00000000002000000LL	/* 20 */
#define GRP_WATCHPT_W	00000000000200000LL	/* 17 */
#define GRP_WATCHPT_R	00000000000100000LL	/* 16 */
#define GRP_INSN_CHECK	00000000000040000LL	/* 15 */
#define GRP_INSN_PROT	00000000000020000LL	/* 14 */
#define GRP_ILL_INSN	00000000000010000LL	/* 13 */
#define GRP_BREAKPOINT	00000000000004000LL	/* 12 */
#define GRP_PAGE_MASK	00000000000000760LL	/* 9-5 */
#define GRP_RAM_CHECK	00000000000000010LL	/* 4 */
#define GRP_BLOCK_MASK	00000000000000007LL	/* 3-1 */

#define GRP_SET_BLOCK(x,m)	(((x) & ~GRP_BLOCK_MASK) | ((m) & GRP_BLOCK_MASK))
#define GRP_SET_PAGE(x,m)	(((x) & ~GRP_PAGE_MASK) | (((m)<<4) & GRP_PAGE_MASK))

/* Разряды регистра внешних прерываний */
#define PRP_PROGRAM	0400LL
#define PRP_REQUEST	0200LL
#define PRP_RESPONSE	0100LL
#define PRP_PVV_FAIL	0040LL
#define PRP_RAM_FAIL	0020LL
#define PRP_TIMER	0010LL
#define PRP_INTR_PVV	0004LL
#define PRP_MULTI	0002LL
#define PRP_PANEL_REQ	0001LL

/* Номер блока ОЗУ или номер страницы, вызвавших прерывание */
extern uint32 iintr_data;

/* Номер СВС */
extern uint32 cpu_num;

#endif
