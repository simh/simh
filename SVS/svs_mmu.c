/*
 * svs_mmu.c: SVS fast write cache and TLB registers
 *
 * Copyright (c) 2011, Leonid Broukhis
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
#include "svs_defs.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * MMU data structures
 *
 * mmu_dev	MMU device descriptor
 * mmu_unit	MMU unit descriptor
 * mmu_reg	MMU register list
 */
UNIT mmu_unit = {
	UDATA (NULL, UNIT_FIX, 8)
};

t_mem BRZ[8];
uint32 BAZ[8], TABST, RZ, OLDEST, FLUSH;

t_mem BRS[4];
uint32 BAS[4];
uint32 BRSLRU;

/*
 * 64-битные регистры RP0-RP7 - для отображения регистров приписки,
 * группами по 4 ради компактности, 12 бит на страницу.
 * TLB0-TLB31 - постраничные регистры приписки, копии RPi.
 * Обращение к памяти должно вестись через TLBi.
 */
t_value RP[8];
uint32 TLB[32], TLBk[32];

unsigned iintr_data;	/* protected page number or parity check location */

t_value pult[8];

REG mmu_reg[] = {
{ "БРЗ0",  &BRZ[0].word,	8, 64, 0, 1, REG_VMIO},	/* Буферные регистры записи */
{ "БРЗ1",  &BRZ[1].word,	8, 64, 0, 1, REG_VMIO},
{ "БРЗ2",  &BRZ[2].word,	8, 64, 0, 1, REG_VMIO},
{ "БРЗ3",  &BRZ[3].word,	8, 64, 0, 1, REG_VMIO},
{ "БРЗ4",  &BRZ[4].word,	8, 64, 0, 1, REG_VMIO},
{ "БРЗ5",  &BRZ[5].word,	8, 64, 0, 1, REG_VMIO},
{ "БРЗ6",  &BRZ[6].word,	8, 64, 0, 1, REG_VMIO},
{ "БРЗ7",  &BRZ[7].word,	8, 64, 0, 1, REG_VMIO},
{ "БАЗ0",  &BAZ[0],	8, 16, 0, 1 },		/* Буферные адреса записи */
{ "БАЗ1",  &BAZ[1],	8, 16, 0, 1 },
{ "БАЗ2",  &BAZ[2],	8, 16, 0, 1 },
{ "БАЗ3",  &BAZ[3],	8, 16, 0, 1 },
{ "БАЗ4",  &BAZ[4],	8, 16, 0, 1 },
{ "БАЗ5",  &BAZ[5],	8, 16, 0, 1 },
{ "БАЗ6",  &BAZ[6],	8, 16, 0, 1 },
{ "БАЗ7",  &BAZ[7],	8, 16, 0, 1 },
{ "ТАБСТ", &TABST,	8, 28, 0, 1, REG_HIDDEN },/* Таблица старшинства БРЗ */
{ "ЗпТР",  &FLUSH,	8,  4, 0, 1, REG_HIDDEN },/* Признак выталкивания БРЗ */
{ "Старш", &OLDEST,	8,  3, 0, 1 },		/* Номер вытолкнутого БРЗ */
{ "РП0",   &RP[0],	8, 48, 0, 1, REG_VMIO},	/* Регистры приписки, по 12 бит */
{ "РП1",   &RP[1],	8, 48, 0, 1, REG_VMIO},
{ "РП2",   &RP[2],	8, 48, 0, 1, REG_VMIO},
{ "РП3",   &RP[3],	8, 48, 0, 1, REG_VMIO},
{ "РП4",   &RP[4],	8, 48, 0, 1, REG_VMIO},
{ "РП5",   &RP[5],	8, 48, 0, 1, REG_VMIO},
{ "РП6",   &RP[6],	8, 48, 0, 1, REG_VMIO},
{ "РП7",   &RP[7],	8, 48, 0, 1, REG_VMIO},
{ "РЗ",    &RZ,		8, 32, 0, 1 },		/* Регистр защиты */
{ "ТР1",   &pult[1],	8, 50, 0, 1, REG_VMIO},	/* Тумблерные регистры */
{ "ТР2",   &pult[2],	8, 50, 0, 1, REG_VMIO},
{ "ТР3",   &pult[3],	8, 50, 0, 1, REG_VMIO},
{ "ТР4",   &pult[4],	8, 50, 0, 1, REG_VMIO},
{ "ТР5",   &pult[5],	8, 50, 0, 1, REG_VMIO},
{ "ТР6",   &pult[6],	8, 50, 0, 1, REG_VMIO},
{ "ТР7",   &pult[7],	8, 50, 0, 1, REG_VMIO},
{ "БРС0",  &BRS[0].word,	8, 64, 0, 1, REG_VMIO}, /* Буферные регистры слов */
{ "БРС1",  &BRS[1].word,	8, 64, 0, 1, REG_VMIO},
{ "БРС2",  &BRS[2].word,	8, 64, 0, 1, REG_VMIO},
{ "БРС3",  &BRS[3].word,	8, 64, 0, 1, REG_VMIO},
{ "БАС0",  &BAS[0],	8, 16, 0, 1 },		/* Буферные адреса слов */
{ "БАС1",  &BAS[1],	8, 16, 0, 1 },
{ "БАС2",  &BAS[2],	8, 16, 0, 1 },
{ "БАС3",  &BAS[3],	8, 16, 0, 1 },
{ "БРСст", &BRSLRU,	8,  6, 0, 1, REG_HIDDEN},
{ 0 }
};

#define CACHE_ENB 1

MTAB mmu_mod[] = {
	{ 1, 0, "NOCACHE", "NOCACHE" },
	{ 1, 1, "CACHE", "CACHE" },
	{ 0 }
};

t_stat mmu_reset (DEVICE *dptr);

t_stat mmu_examine (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
	mmu_print_brz();
	return SCPE_NOFNC;
}

DEVICE mmu_dev = {
	"MMU", &mmu_unit, mmu_reg, mmu_mod,
	1, 8, 3, 1, 8, 50,
	&mmu_examine, NULL, &mmu_reset,
	NULL, NULL, NULL, NULL,
	DEV_DEBUG
};

/*
 * Reset routine
 */
t_stat mmu_reset (DEVICE *dptr)
{
	int i;
	for (i = 0; i < 8; ++i) {
		BRZ[i].word = BRZ[i].tag = 0; 
		BAZ[i] = RP[i] = 0;
	}
	TABST = 0;
	OLDEST = 0;
	FLUSH = 0;
	RZ = 0;
	/*
	 * Front panel switches survive the reset
	 */
	sim_cancel (&mmu_unit);
	if (shared == NULL) {
		int fd = open("e1k2.mem", O_RDWR|O_CREAT, 0600);
		if (fd == -1) {
			perror("Cannot open e1k2.mem");
			exit(1);
		}
		ftruncate(fd, sizeof(t_shared));
		shared = mmap(0, sizeof(t_shared), PROT_READ|PROT_WRITE,
				MAP_SHARED, fd, 0);
		if (shared == NULL) {
			perror("Mmapping e1k2.mem");
			exit(1);
		}
		memory = shared->memory;
	}
	/* Приписка ОС сначала 1:1 */
	for (int i = 0; i < 32; ++i) {
		TLBk[i] = i;
	}
	return SCPE_OK;
}

#define loses_to_all(i) ((TABST & win_mask[i]) == 0 && \
			(TABST & lose_mask[i]) == lose_mask[i])

/*
 * N wins over M if the bit is set
 *  M=1   2   3   4   5   6   7
 * N  -------------------------
 * 0| 0   1   2   3   4   5   6
 * 1|     7   8   9  10  11  12
 * 2|        13  14  15  16  17
 * 3|            18  19  20  21
 * 4|                22  23  24
 * 5|                    25  26
 * 6|                        27
 */

static unsigned win_mask[8] = {
	0177,
	0077 << 7,
	0037 << 13,
	0017 << 18,
	0007 << 22,
	0003 << 25,
	0001 << 27,
	0
};

static unsigned lose_mask[8] = {
	0,
	1<<0,
	1<<1|1<<7,
	1<<2|1<<8|1<<13,
	1<<3|1<<9|1<<14|1<<18,
	1<<4|1<<10|1<<15|1<<19|1<<22,
	1<<5|1<<11|1<<16|1<<20|1<<23|1<<25,
	1<<6|1<<12|1<<17|1<<21|1<<24|1<<26|1<<27
};

#define set_wins(i) TABST = (TABST & ~lose_mask[i]) | win_mask[i]

void mmu_protection_check (int addr)
{
	/* Защита блокируется в режиме супервизора для физических (!) адресов 1-7 (ТО-8) - WTF? */
	int tmp_prot_disabled = (M[PSW] & PSW_PROT_DISABLE) ||
		(IS_SUPERVISOR (RUU) && (M[PSW] & PSW_MMAP_DISABLE) && addr < 010);

	/* Защита не заблокирована, а лист закрыт */
	if (! tmp_prot_disabled && (RZ & (1 << (addr >> 10)))) {
		iintr_data = addr >> 10;
		if (mmu_dev.dctrl)
			besm6_debug ("--- (%05o) защита числа", addr);
		longjmp (cpu_halt, STOP_OPERAND_PROT);
	}
}

void mmu_flush (int idx)
{
	if (! BAZ[idx]) {
		/* Был пуст после сброса или выталкивания */
		return;
	}
	/* Вычисляем физический адрес выталкиваемого БРЗ */
	int waddr = BAZ[idx];
	waddr =  (waddr & 01777) | (waddr > 0100000 ?
		TLBk[(waddr - 0100000) >> 10] << 10 :
		TLB[waddr >> 10] << 10);
	memory[waddr] = BRZ[idx];
	BAZ[idx] = 0;
	if (sim_log && mmu_dev.dctrl) {
		fprintf (sim_log, "--- (%05o) запись ", waddr);
		fprint_sym (sim_log, 0, &BRZ[idx].word, 0, 0);
		fprintf (sim_log, " из БРЗ[%d]\n", idx);
	}
}

void mmu_update_oldest ()
{
	int i;

	for (i = 0; i < 8; ++i) {
		if (loses_to_all(i)) {
			OLDEST = i;
			// fprintf(stderr, "Oldest = %d\r\n", i);
			return;
		}
	}
}

int mmu_match (int addr, int fail)
{
	int i;

	for (i = 0; i < 8; ++i) {
		if (addr == BAZ[i]) {
			return i;
		}
	}
	return fail;
}

/*
 * Разнообразные алгоритмы выталкивания БРЗ путем записи
 * по адресам пультовых регистров. Тест УУ проходит дальше всего
 * с mmu_flush_by_age().
 */
void mmu_flush_by_age()
{
	switch (FLUSH) {
	case 0:
		break;
	case 1 ... 8:
		set_wins (OLDEST);
		mmu_update_oldest ();
		mmu_flush (OLDEST);
		if (FLUSH == 7) {
			TABST = 0;
			OLDEST = 0;
		}
		break;
	}
	++FLUSH;
}

void mmu_flush_by_number()
{
	switch (FLUSH) {
	case 0:
		break;
	case 1 ... 8:
		mmu_flush (FLUSH-1);
		set_wins (FLUSH-1);
		if (FLUSH-1 == OLDEST)
			mmu_update_oldest ();
		if (FLUSH == 7) {
			TABST = 0;
			OLDEST = 0;
		}
		break;
	}
	++FLUSH;
}

/*
 * Запись слова в память
 */
void mmu_store (int addr, t_value val, uint8 tag)
{
	int matching;

	addr &= BITS(15);
	if (addr == 0)
		return;
	if (sim_log && mmu_dev.dctrl) {
		fprintf (sim_log, "--- (%05o) запись ", addr);
		fprint_sym (sim_log, 0, &val, 0, 0);
		fprintf (sim_log, "\n");
	}

	mmu_protection_check (addr);

	/* Различаем адреса с припиской и без */
	if (M[PSW] & PSW_MMAP_DISABLE)
		addr |= 0100000;

	/* ЗПСЧ: ЗП */
	if (M[DWP] == addr && (M[PSW] & PSW_WRITE_WATCH))
		longjmp(cpu_halt, STOP_STORE_ADDR_MATCH);

	if (sim_brk_summ & SWMASK('W') &&
	    sim_brk_test (addr, SWMASK('W')))
		longjmp(cpu_halt, STOP_WWATCH);

	if (!(mmu_unit.flags & CACHE_ENB)) {
		static int roundrobin;
		int faked = (++roundrobin ^ addr ^ val) & 7;

		if (addr > 0100000 && addr < 0100010)
			return;

		BRZ[faked] = SET_TAG (val, tag);
		BAZ[faked] = addr;
		mmu_flush (faked);
		return;
	}

	/* Запись в тумблерные регистры - выталкивание БРЗ */
	if (addr > 0100000 && addr < 0100010) {
		mmu_flush_by_age();
		return;
	} else
		FLUSH = 0;

	matching = mmu_match(addr, OLDEST);

	BRZ[matching] = SET_TAG (val, tag);
	BAZ[matching] = addr;
	set_wins (matching);

	if (matching == OLDEST) {
		mmu_update_oldest ();
		mmu_flush (OLDEST);
	}
}

t_mem mmu_memaccess (int addr)
{
	t_mem val;

	/* Вычисляем физический адрес слова */
	addr =  (addr & 01777) | (addr > 0100000 ?
		TLBk[(addr - 0100000) >> 10] << 10 :
		TLB[addr >> 10] << 10);
	if (addr >= 010) {
		/* Из памяти */
		val = memory[addr];
	} else {
		/* С тумблерных регистров */
		if (mmu_dev.dctrl)
			besm6_debug("--- (%05o) чтение ТР%o", svsPC, addr);
		val = SET_TAG(pult[addr], TAG_INSN);
	}
	if (sim_log && (mmu_dev.dctrl || (cpu_dev.dctrl && sim_deb))) {
		fprintf (sim_log, "--- (%05o) чтение ", addr & BITS(15));
		fprint_sym (sim_log, 0, &val.word, 0, 0);
		fprintf (sim_log, "\n");
	}

	return val;
}

static t_mem syncread(int addr) {
	t_value val = memory[addr].word;
	if ((val & BIT(17)) == 0) {
		register unsigned atom __asm__("ebx") = val | BIT(17);
		__asm__("lock cmpxchg8b %0" : : 
			"m" (memory[addr].word), "A" (val),
			"c" ((unsigned) (val >> 32)), "r" (atom));
	}
	return SET_TAG(val, memory[addr].tag);
}
t_mem mmu_memaccess_sync(int addr)
{
	t_mem val;

	/* Вычисляем физический адрес слова */
	addr = (addr & 01777) | (addr > 0100000 ? 
		TLBk[(addr - 0100000) >> 10] << 10 :
		TLB[addr >> 10] << 10);
	if (addr >= 010) {
		/* Из памяти */
		val = syncread(addr);
	} else {
		besm6_debug("CЧСНХ %o ???", addr);
		val = SET_TAG(pult[addr], TAG_INSN);
	}
	if (sim_log && (mmu_dev.dctrl || (cpu_dev.dctrl && sim_deb))) {
		fprintf (sim_log, "--- (%05o) СНХ чтение ", addr & BITS(15));
		fprint_sym (sim_log, 0, &val.word, 0, 0);
		fprintf (sim_log, "\n");
	}

	return val;
}
/*
 * Чтение операнда
 */
t_mem mmu_load_full (int addr)
{
	int matching = -1;
	t_mem val;

	addr &= BITS(15);
	if (addr == 0)
		return SET_TAG(0, TAG_NUMBER);

	mmu_protection_check (addr);

	/* Различаем адреса с припиской и без */
	if (M[PSW] & PSW_MMAP_DISABLE)
		addr |= 0100000;

	/* ЗПСЧ: СЧ */
	if (M[DWP] == addr && !(M[PSW] & PSW_WRITE_WATCH))
		longjmp(cpu_halt, STOP_LOAD_ADDR_MATCH);

	if (sim_brk_summ & SWMASK('R') &&
	    sim_brk_test (addr, SWMASK('R')))
		longjmp(cpu_halt, STOP_RWATCH);

	if (!(mmu_unit.flags & CACHE_ENB)) {
		return mmu_memaccess (addr);
	}

	matching = mmu_match(addr, -1);

	if (matching == -1) {
		val = mmu_memaccess (addr);
	} else {
		/* старшинство обновляется, только если оно не затрагивает
		 * старший БРЗ (ТО-2).
		 */
		if (matching != OLDEST)
			set_wins (matching);
		val = BRZ[matching];
		if (sim_log && (mmu_dev.dctrl || (cpu_dev.dctrl && sim_deb))) {
			fprintf (sim_log, "--- (%05o) чтение ", addr & BITS(15));
			fprint_sym (sim_log, 0, &val.word, 0, 0);
			fprintf (sim_log, " из БРЗ\n");
		}
		if (! IS_NUMBER (val)) {
			iintr_data = matching;
			besm6_debug ("--- (%05o) контроль числа БРЗ", addr);
			longjmp (cpu_halt, STOP_CACHE_CHECK);
		}
	}
	return val;
}

t_value mmu_load(int addr) {
	t_mem val = mmu_load_full(addr);
	/* На тумблерных регистрах контроля числа не бывает */
	if (addr >= 010 && ! IS_NUMBER (val) && val.tag != TAG_BITSET) {
		iintr_data = addr & 7;
		besm6_debug ("--- (%05o) контроль числа, тег %03o", addr, val.tag);
		longjmp (cpu_halt, STOP_RAM_CHECK);
	}
	return val.word & BITS48;
}

/* A little BRS LRU table */
#define brs_loses_to_all(i) ((BRSLRU & brs_win_mask[i]) == 0 && \
			(BRSLRU & brs_lose_mask[i]) == brs_lose_mask[i])

/*
 * N wins over M if the bit is set
 *  M=1   2   3
 * N  ---------
 * 0| 0   1   2
 * 1|     3   4
 * 2|         5
 */

static unsigned brs_win_mask[4] = {
	07,
	03 << 3,
	01 << 5,
	0
};

static unsigned brs_lose_mask[8] = {
	0,
	1<<0,
	1<<1|1<<3,
	1<<2|1<<4|1<<5
};

#define brs_set_wins(i) BRSLRU = (BRSLRU & ~brs_lose_mask[i]) | brs_win_mask[i]

void mmu_fetch_check (int addr)
{
	/* В режиме супервизора защиты нет */
	if (! IS_SUPERVISOR(RUU)) {
		int page = TLB[addr >> 10];
		/*
		 * Для команд в режиме пользователя признак защиты -
		 * 0 в регистре приписки.
		 */
		if (page == 0) {
			iintr_data = addr >> 10;
			if (mmu_dev.dctrl)
				besm6_debug ("--- (%05o) защита команды", addr);
			longjmp (cpu_halt, STOP_INSN_PROT);
		}
	}
}

/*
 * Предвыборка команды на БРС
 */
t_mem mmu_prefetch (int addr, int actual)
{
	t_value val;
	uint8 tag;
	int i;
	int page;

	if (mmu_unit.flags & CACHE_ENB) {
	    for (i = 0; i < 4; ++i) {
		if (BAS[i] == addr) {
			if (actual) {
				brs_set_wins (i);
			}
			return BRS[i];
		}
	    }

	    for (i = 0; i < 4; ++i) {
		if (brs_loses_to_all (i)) {
			BAS[i] = addr;
			if (actual) {
				brs_set_wins (i);
			}
			break;
		}
	    }
	} else if (!actual) {
		return SET_TAG(0, 0);
	} else {
		/* Чтобы лампочки мигали */
		i = addr & 3;
	}

	if (addr < 0100000) {
		page = TLB[addr >> 10];
	} else {
		addr = addr & BITS(15);
		if (addr >= 010) {
			page = TLBk[addr >> 10];
		} else {
			page = 0;
		}
	}

	/* Вычисляем физический адрес слова */
	addr = (addr & 01777) | (page << 10);

	if (addr < 010) {
		val = pult[addr];
		tag = TAG_INSN;
	} else {
    		val = memory[addr].word;
		tag = memory[addr].tag;
	}
	BRS[i].word = val;
	BRS[i].tag = tag;
	return BRS[i];
}

/*
 * Выборка команды
 */
t_value mmu_fetch (int addr)
{
	t_mem val;

	if (addr == 0) {
		if (mmu_dev.dctrl)
			besm6_debug ("--- передача управления на 0");
		longjmp (cpu_halt, STOP_INSN_CHECK);
	}

	mmu_fetch_check(addr);

	/* Различаем адреса с припиской и без */
	if (IS_SUPERVISOR (RUU))
		addr |= 0100000;

	/* КРА */
	if (M[IBP] == addr)
		longjmp(cpu_halt, STOP_INSN_ADDR_MATCH);

	val = mmu_prefetch(addr, 1);

	if (sim_log && mmu_dev.dctrl) {
		fprintf (sim_log, "--- (%05o) выборка ", addr);
		fprint_sym (sim_log, 0, &val.word, 0, SWMASK ('I'));
		fprintf (sim_log, "\n");
	}

	/* Тумблерные регистры пока только с командной сверткой */
	if (addr >= 010 && ! IS_INSN (val)) {
		besm6_debug ("--- (%05o) контроль команды", addr);
		longjmp (cpu_halt, STOP_INSN_CHECK);
	}
	return val.word & BITS48;
}

void mmu_setrp (int idx, t_value val)
{
	uint32 p0, p1, p2, p3;
        const uint32 mask = (MEMSIZE >> 10) - 1;

	/* Младшие 5 разрядов 4-х регистров приписки упакованы
	 * по 5 в 1-20 рр, 6-е разряды - в 29-32 рр, 7-е разряды - в 33-36 рр и т.п.
	 */
	p0 = (val       & 037) | (((val>>28) & 1) << 5) | (((val>>32) & 1) << 6) |
       (((val>>36) &  1) << 7) | (((val>>40) & 1) << 8) | (((val>>44) & 1) << 9);
	p1 = ((val>>5)  & 037) | (((val>>29) & 1) << 5) | (((val>>33) & 1) << 6) |
       (((val>>37) &  1) << 7) | (((val>>41) & 1) << 8) | (((val>>45) & 1) << 9);
	p2 = ((val>>10) & 037) | (((val>>30) & 1) << 5) | (((val>>34) & 1) << 6) |
       (((val>>38) &  1) << 7) | (((val>>42) & 1) << 8) | (((val>>46) & 1) << 9);
	p3 = ((val>>15) & 037) | (((val>>31) & 1) << 5) | (((val>>35) & 1) << 6) |
       (((val>>39) &  1) << 7) | (((val>>43) & 1) << 8) | (((val>>47) & 1) << 9);

        p0 &= mask;
        p1 &= mask;
        p2 &= mask;
        p3 &= mask;

	RP[idx] = p0 | p1 << 12 | p2 << 24 | (t_value) p3 << 36;
	TLB[idx*4] = p0;
	TLB[idx*4+1] = p1;
	TLB[idx*4+2] = p2;
	TLB[idx*4+3] = p3;
}

void mmu_setrp_kernel (int idx, t_value val)
{
	uint32 p0, p1, p2, p3;
        const uint32 mask = (MEMSIZE >> 10) - 1;

	/* Младшие 5 разрядов 4-х регистров приписки упакованы
	 * по 5 в 1-20 рр, 6-е разряды - в 29-32 рр, 7-е разряды - в 33-36 рр и т.п.
	 */
	p0 = (val       & 037) | (((val>>28) & 1) << 5) | (((val>>32) & 1) << 6) |
       (((val>>36) &  1) << 7) | (((val>>40) & 1) << 8) | (((val>>44) & 1) << 9);
	p1 = ((val>>5)  & 037) | (((val>>29) & 1) << 5) | (((val>>33) & 1) << 6) |
       (((val>>37) &  1) << 7) | (((val>>41) & 1) << 8) | (((val>>45) & 1) << 9);
	p2 = ((val>>10) & 037) | (((val>>30) & 1) << 5) | (((val>>34) & 1) << 6) |
       (((val>>38) &  1) << 7) | (((val>>42) & 1) << 8) | (((val>>46) & 1) << 9);
	p3 = ((val>>15) & 037) | (((val>>31) & 1) << 5) | (((val>>35) & 1) << 6) |
       (((val>>39) &  1) << 7) | (((val>>43) & 1) << 8) | (((val>>47) & 1) << 9);

        p0 &= mask;
        p1 &= mask;
        p2 &= mask;
        p3 &= mask;

	TLBk[idx*4] = p0;
	TLBk[idx*4+1] = p1;
	TLBk[idx*4+2] = p2;
	TLBk[idx*4+3] = p3;
}

void mmu_setup ()
{
	const uint32 mask = (MEMSIZE >> 10) - 1;
	int i;

	/* Перепись РПi в TLBj. */
	for (i=0; i<8; ++i) {
		TLB[i*4] = RP[i] & mask;
		TLB[i*4+1] = RP[i] >> 12 & mask;
		TLB[i*4+2] = RP[i] >> 24 & mask;
		TLB[i*4+3] = RP[i] >> 36 & mask;
	}
}

void mmu_setprotection (int idx, t_value val)
{
	/* Разряды сумматора, записываемые в регистр защиты - 21-28 */
	int mask = 0xff << (idx * 8);
	val = ((val >> 20) & 0xff) << (idx * 8);
	RZ = (RZ & ~mask) | val;
}

void mmu_setcache (int idx, t_value val)
{
	BRZ[idx] = SET_TAG (val, RUUTAG);
}

t_value mmu_getcache (int idx)
{
	return BRZ[idx].word & BITS48;
}

void mmu_print_brz ()
{
	int i, k;

	for (i=7; i>=0; --i) {
		besm6_log_cont ("БРЗ [%d] = '", i);
		for (k=47; k>=0; --k)
			besm6_log_cont ("%c", (BRZ[i].word >> k & 1) ? '*' : ' ');
		besm6_log ("'");
	}
}
