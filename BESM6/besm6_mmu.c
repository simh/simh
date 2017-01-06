/*
 * besm6_mmu.c: BESM-6 fast write cache and TLB registers
 *（стойка БРУС)
 *
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

/*
 * MMU data structures
 *
 * mmu_dev      MMU device descriptor
 * mmu_unit     MMU unit descriptor
 * mmu_reg      MMU register list
 */
UNIT mmu_unit = {
    UDATA (NULL, UNIT_FIX, 8)
};

t_value BRZ[8];
uint32 BAZ[8], TABST, RZ, OLDEST, FLUSH;

t_value BRS[4];
uint32 BAS[4];
uint32 BRSLRU;

/*
 * 64-битные регистры RP0-RP7 - для отображения регистров приписки,
 * группами по 4 ради компактности, 12 бит на страницу.
 * TLB0-TLB31 - постраничные регистры приписки, копии RPi.
 * Обращение к памяти должно вестись через TLBi.
 */
t_value RP[8];
uint32 TLB[32];

uint32 iintr_data;    /* protected page number or parity check location */

/* There were several hardwired configurations of registers
 * corresponding to up to 7 first words of the memory space, selected by
 * a packet switch. Here selection 0 corresponds to settable switch registers,
 * the others are hardwired.
 * The configuration is selected with "SET CPU PULT=N" where 0 <= N <= 10
 * is the configuration number.
 */
unsigned pult_packet_switch;

/* Location 0 of each configuration is the bitset of its hardwired locations */
t_value pult[11][8] = {
/* Switch registers */
    { 0 },
/* Hardwired program 1, a simple CU test */
    { 0376,
      SET_PARITY(01240000007100002LL, PARITY_INSN),   /* 1: vtm (2), vjm 2(1) */
      SET_PARITY(00657777712577777LL, PARITY_INSN),   /* 2: utm -1(1), utm -1(2) */
      SET_PARITY(00444000317400007LL, PARITY_INSN),   /* 3: mtj 3(1), vzm 7(3) */
      SET_PARITY(01045000317500007LL, PARITY_INSN),   /* 4: j+m 3(2), v1m 7(3)*/
      SET_PARITY(00650000107700002LL, PARITY_INSN),   /* 5: utm 1(1), vlm 2(1) */
      SET_PARITY(01257777713400001LL, PARITY_INSN),   /* 6: utm -1(2), vzm 1(2) */
      SET_PARITY(00330000003000001LL, PARITY_INSN)    /* 7: stop, uj 1 */
    },
/* Hardwired program 2, RAM write test. The "arx" insn (cyclic add)
 * in word 3 could be changed to "atx" insn (load) to use a constant
 * bit pattern with a "constant/variable code" front panel switch (TODO).
 * The bit pattern to use is taken from switch register 7.
 */
    { 0176,
      SET_PARITY(00770000306400012LL, PARITY_INSN), /* 1: vlm 3(1), vtm 12(1) */
      SET_PARITY(00010000000000010LL, PARITY_INSN), /* 2: xta 0, atx 10 */
      SET_PARITY(00010001000130007LL, PARITY_INSN), /* 3: xta 10, arx 7 */
      SET_PARITY(00500777700000010LL, PARITY_INSN), /* 4: atx -1(1), atx 10 */
      SET_PARITY(00512777702600001LL, PARITY_INSN), /* 5: aex -1(1), uza 1 */
      SET_PARITY(00737777703000001LL, PARITY_INSN)  /* 6: stop -1(1), uj 1 */
    },
/* Hardwired program 3, RAM read test to use after program 2, arx/atx applies */
    { 0176,
      SET_PARITY(00770000306400012LL, PARITY_INSN), /* 1: vlm 3(1), vtm 12(1) */
      SET_PARITY(00010000000000010LL, PARITY_INSN), /* 2: xta 0, atx 10 */
      SET_PARITY(00010001000130007LL, PARITY_INSN), /* 3: xta 10, arx 7 */
      SET_PARITY(00000000000000010LL, PARITY_INSN), /* 4: atx 0, atx 10 */
      SET_PARITY(00512777702600001LL, PARITY_INSN), /* 5: aex -1(1), uza 1 */
      SET_PARITY(00737777703000001LL, PARITY_INSN)  /* 6: stop -1(1), uj 1 */
    },
/* Hardwired program 4, RAM write-read test to use after program 2, arx/atx applies */
    { 0176,
      SET_PARITY(00640001200100011LL, PARITY_INSN), /* 1: vtm 12(1), xta 11 */
      SET_PARITY(00000001005127777LL, PARITY_INSN), /* 2: atx 10, aex -1(1) */
      SET_PARITY(00260000407377777LL, PARITY_INSN), /* 3: uza 4, stop -1(1) */
      SET_PARITY(00010001000130007LL, PARITY_INSN), /* 4: xta 10, arx 7 */
      SET_PARITY(00500777707700002LL, PARITY_INSN), /* 5: atx -1(1), vlm 2(1) */
      SET_PARITY(00300000100000000LL, PARITY_INSN)  /* 6: uj 1 */
    },
/* Hardwired program 5, ALU test; switch reg 7 should contain a
   normalized f. p. value, e.g. 1.0 = 4050 0000 0000 0000 */
    { 0176,
      SET_PARITY(00004000700000011LL, PARITY_INSN), /* 1: a+x 7, atx 11 */
      SET_PARITY(00025001100000010LL, PARITY_INSN), /* 2: e-x 11, atx 10 */
      SET_PARITY(00017001000160010LL, PARITY_INSN), /* 3: a*x 10, a/x 10 */
      SET_PARITY(00005001000340145LL, PARITY_INSN), /* 4: a-x 10, e+n 145 */
      SET_PARITY(00270000603300000LL, PARITY_INSN), /* 5: u1a 6, stop */
      SET_PARITY(00010001103000001LL, PARITY_INSN)  /* 6: xta 11, uj 1*/
    },
/* Hardwired program 6, reading from punch tape (originally) or a disk (rework);
 * various bit groups not hardwired, marked [] (TODO). Disk operation is encoded.
 */
    { 0376,
      SET_PARITY(00640000300100006LL, PARITY_INSN), /* 1: vtm [3](1), xta 6 */
      SET_PARITY(00433002004330020LL, PARITY_INSN), /* 2: ext 20(1), ext 20(1) */
      SET_PARITY(00036015204330020LL, PARITY_INSN), /* 3: asn 152, ext 20(1) */
      SET_PARITY(00010000704330000LL, PARITY_INSN), /* 4: xta 7, ext (1) */
      SET_PARITY(00036014404330020LL, PARITY_INSN), /* 5: asn 144, ext 20(1) */
      SET_PARITY(00330000000002401LL, PARITY_INSN), /* 6: stop, =24[01] */
      SET_PARITY(04000000001400000LL, PARITY_NUMBER) /* 7: bits 37-47 not hardwired */
    },
/* Hardwired program 7, RAM peek/poke, bits 1-15 of word 1 not hardwired (TODO) */
    { 0176,
    },
/* Hardwired program 8, reading the test program from a fixed drum location */
    { 0036,
    },
/* Hardwired program 9, drum I/O */
    { 0176,
      SET_PARITY(00647774100100007LL, PARITY_INSN), /* 1: vtm -31(1), xta 7 */
      SET_PARITY(00033000212460000LL, PARITY_INSN), /* 2: ext 2, vtm 60000(2) */
      SET_PARITY(00040000013700003LL, PARITY_INSN), /* 3: ati, vlm 3(2) */
      SET_PARITY(00013000607700002LL, PARITY_INSN), /* 4: arx 6, vlm 2(1) */
      SET_PARITY(00330000103000005LL, PARITY_INSN), /* 5: stop 1, uj 5 */
      SET_PARITY(00000000000010001LL, PARITY_NUMBER) /* 6: =10001 */
    },
/* Hardwired program 10, magtape read */
    { 0176,
    },
};

#define ORDATAVM(nm,loc,wd) REGDATA(nm,(loc),8,wd,0,1,NULL,NULL,REG_VMIO,0,0)
#define ORDATAH(nm,loc,wd) REGDATA(nm,(loc),8,wd,0,1,NULL,NULL,REG_HIDDEN,0,0)

REG mmu_reg[] = {
    { ORDATAVM ( "БРЗ0",  BRZ[0],     50) },                      /* Буферные регистры записи */
    { ORDATAVM ( "БРЗ1",  BRZ[1],     50) },
    { ORDATAVM ( "БРЗ2",  BRZ[2],     50) },
    { ORDATAVM ( "БРЗ3",  BRZ[3],     50) },
    { ORDATAVM ( "БРЗ4",  BRZ[4],     50) },
    { ORDATAVM ( "БРЗ5",  BRZ[5],     50) },
    { ORDATAVM ( "БРЗ6",  BRZ[6],     50) },
    { ORDATAVM ( "БРЗ7",  BRZ[7],     50) },
    { ORDATA   ( "БАЗ0",  BAZ[0],     16) },                      /* Буферные адреса записи */
    { ORDATA   ( "БАЗ1",  BAZ[1],     16) },
    { ORDATA   ( "БАЗ2",  BAZ[2],     16) },
    { ORDATA   ( "БАЗ3",  BAZ[3],     16) },
    { ORDATA   ( "БАЗ4",  BAZ[4],     16) },
    { ORDATA   ( "БАЗ5",  BAZ[5],     16) },
    { ORDATA   ( "БАЗ6",  BAZ[6],     16) },
    { ORDATA   ( "БАЗ7",  BAZ[7],     16) },
    { ORDATAH  ( "ТАБСТ", TABST,      28) },                      /* Таблица старшинства БРЗ */
    { ORDATAH  ( "ЗпТР",  FLUSH,       4) },                      /* Признак выталкивания БРЗ */
    { ORDATA   ( "Старш", OLDEST,      3) },                      /* Номер вытолкнутого БРЗ */
    { ORDATAVM ( "РП0",   RP[0],      48) },                      /* Регистры приписки, по 12 бит */
    { ORDATAVM ( "РП1",   RP[1],      48) },
    { ORDATAVM ( "РП2",   RP[2],      48) },
    { ORDATAVM ( "РП3",   RP[3],      48) },
    { ORDATAVM ( "РП4",   RP[4],      48) },
    { ORDATAVM ( "РП5",   RP[5],      48) },
    { ORDATAVM ( "РП6",   RP[6],      48) },
    { ORDATAVM ( "РП7",   RP[7],      48) },
    { ORDATA   ( "РЗ",    RZ,         32) },                      /* Регистр защиты */
    { ORDATAVM ( "ТР1",   pult[0][1], 50) },                      /* Тумблерные регистры */
    { ORDATAVM ( "ТР2",   pult[0][2], 50) },
    { ORDATAVM ( "ТР3",   pult[0][3], 50) },
    { ORDATAVM ( "ТР4",   pult[0][4], 50) },
    { ORDATAVM ( "ТР5",   pult[0][5], 50) },
    { ORDATAVM ( "ТР6",   pult[0][6], 50) },
    { ORDATAVM ( "ТР7",   pult[0][7], 50) },
    { ORDATAVM ( "БРС0",  BRS[0],     50) },                      /* Буферные регистры слов */
    { ORDATAVM ( "БРС1",  BRS[1],     50) },
    { ORDATAVM ( "БРС2",  BRS[2],     50) },
    { ORDATAVM ( "БРС3",  BRS[3],     50) },
    { ORDATA   ( "БАС0",  BAS[0],     16) },                      /* Буферные адреса слов */
    { ORDATA   ( "БАС1",  BAS[1],     16) },
    { ORDATA   ( "БАС2",  BAS[2],     16) },
    { ORDATA   ( "БАС3",  BAS[3],     16) },
    { ORDATA   ( "БРСст", BRSLRU,      6) },
    { 0 }
};

#define CACHE_ENB 1

MTAB mmu_mod[] = {
    { 1, 0, "NOCACHE", "NOCACHE" },
    { 1, 1, "CACHE",   "CACHE" },
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
        BRZ[i] = RP[i] = BAZ[i] = 0;
    }
    TABST = 0;
    OLDEST = 0;
    FLUSH = 0;
    RZ = 0;
    /*
     * Front panel switches survive the reset
     */
    sim_cancel (&mmu_unit);
    return SCPE_OK;
}

#define loses_to_all(i) ((TABST & win_mask[i]) == 0 &&                  \
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
    int waddr = BAZ[idx];

    if (! BAZ[idx]) {
        /* Был пуст после сброса или выталкивания */
        return;
    }
    /* Вычисляем физический адрес выталкиваемого БРЗ */
    waddr = (waddr > 0100000) ? (waddr - 0100000) :
        (waddr & 01777) | (TLB[waddr >> 10] << 10);
    memory[waddr] = BRZ[idx];
    BAZ[idx] = 0;
    if (sim_log && mmu_dev.dctrl) {
        fprintf (sim_log, "--- (%05o) запись ", waddr);
        fprint_sym (sim_log, 0, &BRZ[idx], 0, 0);
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
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
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
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
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
void mmu_store (int addr, t_value val)
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

        BRZ[faked] = SET_PARITY (val, RUU ^ PARITY_INSN);
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

    BRZ[matching] = SET_PARITY (val, RUU ^ PARITY_INSN);
    BAZ[matching] = addr;
    set_wins (matching);

    if (matching == OLDEST) {
        mmu_update_oldest ();
        mmu_flush (OLDEST);
    }
}

t_value mmu_memaccess (int addr)
{
    t_value val;

    /* Вычисляем физический адрес слова */
    addr = (addr > 0100000) ? (addr - 0100000) :
        (addr & 01777) | (TLB[addr >> 10] << 10);
    if (addr >= 010) {
        /* Из памяти */
        val = memory[addr];
    } else {
        /* С тумблерных регистров */
        if (mmu_dev.dctrl)
            besm6_debug("--- (%05o) чтение ТР%o", PC, addr);
        if ((pult[pult_packet_switch][0] >> addr) & 1) {
            /* hardwired */
            val = pult[pult_packet_switch][addr];
        } else {
            /* from switch regs */
            val = pult[0][addr];
        }
    }
    if (sim_log && (mmu_dev.dctrl || (cpu_dev.dctrl && sim_deb))) {
        fprintf (sim_log, "--- (%05o) чтение ", addr & BITS(15));
        fprint_sym (sim_log, 0, &val, 0, 0);
        fprintf (sim_log, "\n");
    }

    /* На тумблерных регистрах контроля числа не бывает */
    if (addr >= 010 && ! IS_NUMBER (val)) {
        iintr_data = addr & 7;
        besm6_debug ("--- (%05o) контроль числа", addr);
        longjmp (cpu_halt, STOP_RAM_CHECK);
    }
    return val;
}

/*
 * Чтение операнда
 */
t_value mmu_load (int addr)
{
    int matching = -1;
    t_value val;

    addr &= BITS(15);
    if (addr == 0)
        return 0;

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
        return mmu_memaccess (addr) & BITS48;
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
            fprint_sym (sim_log, 0, &val, 0, 0);
            fprintf (sim_log, " из БРЗ\n");
        }
        if (! IS_NUMBER (val)) {
            iintr_data = matching;
            besm6_debug ("--- (%05o) контроль числа БРЗ", addr);
            longjmp (cpu_halt, STOP_CACHE_CHECK);
        }
    }
    return val & BITS48;
}

/* A little BRS LRU table */
#define brs_loses_to_all(i) ((BRSLRU & brs_win_mask[i]) == 0 &&         \
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
t_value mmu_prefetch (int addr, int actual)
{
    t_value val;
    int i;

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
        return 0;
    } else {
        /* Чтобы лампочки мигали */
        i = addr & 3;
    }

    if (addr < 0100000) {
        int page = TLB[addr >> 10];

        /* Вычисляем физический адрес слова */
        addr = (addr & 01777) | (page << 10);
    } else {
        addr = addr & BITS(15);
    }

    if (addr < 010) {
        if ((pult[pult_packet_switch][0] >> addr) & 1) {
            /* hardwired */
            val = pult[pult_packet_switch][addr];
        } else {
            /* from switch regs */
            val = pult[0][addr];
        }
    } else
        val = memory[addr];
    BRS[i] = val;
    return val;
}

/*
 * Выборка команды
 */
t_value mmu_fetch (int addr)
{
    t_value val;

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
        fprint_sym (sim_log, 0, &val, 0, SWMASK ('I'));
        fprintf (sim_log, "\n");
    }

    /* Тумблерные регистры пока только с командной сверткой */
    if (addr >= 010 && ! IS_INSN (val)) {
        besm6_debug ("--- (%05o) контроль команды", addr);
        longjmp (cpu_halt, STOP_INSN_CHECK);
    }
    return val & BITS48;
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
    RZ = (uint32)((RZ & ~mask) | val);
}

void mmu_setcache (int idx, t_value val)
{
    BRZ[idx] = SET_PARITY (val, RUU ^ PARITY_INSN);
}

t_value mmu_getcache (int idx)
{
    return BRZ[idx] & BITS48;
}

void mmu_print_brz ()
{
    int i, k;

    for (i=7; i>=0; --i) {
        besm6_log_cont ("БРЗ [%d] = '", i);
        for (k=47; k>=0; --k)
            besm6_log_cont ("%c", (BRZ[i] >> k & 1) ? '*' : ' ');
        besm6_log ("'");
    }
}
