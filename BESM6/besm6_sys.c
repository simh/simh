/*
 * besm6_sys.c: BESM-6 simulator interface
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
 *
 * This file implements three essential functions:
 *
 * sim_load()   - loading and dumping memory and CPU state
 *                in a way, specific for BESM-6 architecture
 * fprint_sym() - print a machune instruction using
 *                opcode mnemonic or in a digital format
 * parse_sym()  - scan a string and build an instruction
 *                word from it
 */
#include "besm6_defs.h"
#include <math.h>

const char *opname_short_bemsh [64] = {
    "зп",  "зпм", "рег", "счм", "сл",  "вч",  "вчоб","вчаб",
    "сч",  "и",   "нтж", "слц", "знак","или", "дел", "умн",
    "сбр", "рзб", "чед", "нед", "слп", "вчп", "сд",  "рж",
    "счрж","счмр","э32", "увв", "слпа","вчпа","сда", "ржа",
    "уи",  "уим", "счи", "счим","уии", "сли", "э46", "э47",
    "э50", "э51", "э52", "э53", "э54", "э55", "э56", "э57",
    "э60", "э61", "э62", "э63", "э64", "э65", "э66", "э67",
    "э70", "э71", "э72", "э73", "э74", "э75", "э76", "э77",
};

static const char *opname_long_bemsh [16] = {
    "э20", "э21", "мода","мод", "уиа", "слиа","по",  "пе",
    "пб",  "пв",  "выпр","стоп","пио", "пино","э36", "цикл",
};

const char *opname_short_madlen [64] = {
    "atx",  "stx",  "mod",  "xts",  "a+x",  "a-x",  "x-a",  "amx",
    "xta",  "aax",  "aex",  "arx",  "avx",  "aox",  "a/x",  "a*x",
    "apx",  "aux",  "acx",  "anx",  "e+x",  "e-x",  "asx",  "xtr",
    "rte",  "yta",  "*32",  "ext",  "e+n",  "e-n",  "asn",  "ntr",
    "ati",  "sti",  "ita",  "its",  "mtj",  "j+m",  "*46",  "*47",
    "*50",  "*51",  "*52",  "*53",  "*54",  "*55",  "*56",  "*57",
    "*60",  "*61",  "*62",  "*63",  "*64",  "*65",  "*66",  "*67",
    "*70",  "*71",  "*72",  "*73",  "*74",  "*75",  "*76",  "*77",
};

static const char *opname_long_madlen [16] = {
    "*20",  "*21",  "utc",  "wtc",  "vtm",  "utm",  "uza",  "u1a",
    "uj",   "vjm",  "ij",   "stop", "vzm",  "v1m",  "*36",  "vlm",
};

/*
 * Выдача мнемоники по коду инструкции.
 * Код должен быть в диапазоне 000..077 или 0200..0370.
 */
const char *besm6_opname (int opcode)
{
    if (sim_switches & SWMASK ('L')) {
        /* Latin mnemonics. */
        if (opcode & 0200)
            return opname_long_madlen [(opcode >> 3) & 017];
        return opname_short_madlen [opcode];
    }
    if (opcode & 0200)
        return opname_long_bemsh [(opcode >> 3) & 017];
    return opname_short_bemsh [opcode];
}

/*
 * Выдача кода инструкции по мнемонике (UTF-8).
 */
int besm6_opcode (char *instr)
{
    int i;

    for (i=0; i<64; ++i)
        if (strcmp (opname_short_bemsh[i], instr) == 0 ||
            strcmp (opname_short_madlen[i], instr) == 0)
            return i;
    for (i=0; i<16; ++i)
        if (strcmp (opname_long_bemsh[i], instr) == 0 ||
            strcmp (opname_long_madlen[i], instr) == 0)
            return (i << 3) | 0200;
    return -1;
}

/*
 * Выдача на консоль и в файл протокола.
 * Если первый символ формата - подчерк, на консоль не печатаем.
 * Добавляет перевод строки.
 */
void besm6_log (const char *fmt, ...)
{
    va_list args;

    if (*fmt == '_')
        ++fmt;
    else {
        va_start (args, fmt);
        vprintf (fmt, args);
        printf ("\r\n");
        va_end (args);
    }
    if (sim_log) {
        va_start (args, fmt);
        vfprintf (sim_log, fmt, args);
        if (sim_log == stdout)
            fprintf (sim_log, "\r");
        fprintf (sim_log, "\n");
        fflush (sim_log);
        va_end (args);
    }
}

/*
 * Не добавляет перевод строки.
 */
void besm6_log_cont (const char *fmt, ...)
{
    va_list args;

    if (*fmt == '_')
        ++fmt;
    else {
        va_start (args, fmt);
        vprintf (fmt, args);
        va_end (args);
    }
    if (sim_log) {
        va_start (args, fmt);
        vfprintf (sim_log, fmt, args);
        fflush (sim_log);
        va_end (args);
    }
}

/*
 * Выдача на консоль и в файл отладки: если включён режим "cpu debug".
 * Добавляет перевод строки.
 */
void besm6_debug (const char *fmt, ...)
{
    va_list args;

    va_start (args, fmt);
    vprintf (fmt, args);
    printf ("\r\n");
    va_end (args);
    if (sim_deb && sim_deb != stdout) {
        va_start (args, fmt);
        vfprintf (sim_deb, fmt, args);
        fprintf (sim_deb, "\n");
        fflush (sim_deb);
        va_end (args);
    }
}

/*
 * Преобразование вещественного числа в формат БЭСМ-6.
 *
 * Представление чисел в IEEE 754 (double):
 *      64   63———53 52————–1
 *      знак порядок мантисса
 * Старший (53-й) бит мантиссы не хранится и всегда равен 1.
 *
 * Представление чисел в БЭСМ-6:
 *      48——–42 41   40————————————————–1
 *      порядок знак мантисса в доп. коде
 */
t_value ieee_to_besm6 (double d)
{
    t_value word;
    int exponent;
    int sign;

    sign = d < 0;
    if (sign)
        d = -d;
    d = frexp (d, &exponent);
    /* 0.5 <= d < 1.0 */
    d = ldexp (d, 40);
    word = (t_value)d;
    if (d - word >= 0.5)
        word += 1;                      /* Округление. */
    if (exponent < -64)
        return 0LL;                     /* Близкое к нулю число */
    if (exponent > 63) {
        return sign ?
            0xFEFFFFFFFFFFLL :      /* Максимальное число */
            0xFF0000000000LL;       /* Минимальное число */
    }
    if (sign)
        word = 0x20000000000LL-word;    /* Знак. */
    word |= ((t_value) (exponent + 64)) << 41;
    return word;
}

double besm6_to_ieee (t_value word)
{
    double mantissa;
    int exponent;

    /* Убираем свертку */
    word &= BITS48;

    /* Сдвигаем так, чтобы знак мантиссы пришелся на знак целого;
     * таким образом, mantissa равно исходной мантиссе, умноженной на 2**63.
     */
    mantissa = (double)(((t_int64) word) << (64 - 48 + 7));

    exponent = word >> 41;

    /* Порядок смещен вверх на 64, и мантиссу нужно скорректировать */
    return ldexp (mantissa, exponent - 64 - 63);
}

/*
 * Пропуск пробелов.
 */
CONST char *skip_spaces (CONST char *p)
{
    for (;;) {
        if (*p == (char) 0xEF && p[1] == (char) 0xBB && p[2] == (char) 0xBF) {
            /* Skip zero width no-break space. */
            p += 3;
            continue;
        }
        if (*p == ' ' || *p == '\t' || *p == '\r') {
            ++p;
            continue;
        }
        return p;
    }
}

/*
 * Fetch Unicode symbol from UTF-8 string.
 * Advance string pointer.
 */
int utf8_to_unicode (CONST char **p)
{
    int c1, c2, c3;

    c1 = (unsigned char) *(*p)++;
    if (! (c1 & 0x80))
        return c1;
    c2 = (unsigned char) *(*p)++;
    if (! (c1 & 0x20))
        return (c1 & 0x1f) << 6 | (c2 & 0x3f);
    c3 = (unsigned char) *(*p)++;
    return (c1 & 0x0f) << 12 | (c2 & 0x3f) << 6 | (c3 & 0x3f);
}

char *besm6_parse_octal (const char *cptr, int *offset)
{
    char *eptr;

    *offset = strtol (cptr, &eptr, 8);
    if (eptr == cptr)
        return 0;
    return eptr;
}

static CONST char *get_alnum (CONST char *iptr, char *optr)
{
    while ((*iptr >= 'a' && *iptr<='z') ||
           (*iptr >= 'A' && *iptr<='Z') ||
           (*iptr >= '0' && *iptr<='9') || (*iptr & 0x80)) {
        *optr++ = *iptr++;
    }
    *optr = 0;
    return iptr;
}

/*
 * Parse single instruction (half word).
 * Allow mnemonics or octal code.
 */
CONST char *parse_instruction (CONST char *cptr, uint32 *val)
{
    int opcode, reg, addr, negate;
    char gbuf[CBUFSIZE];

    cptr = skip_spaces (cptr);                      /* absorb spaces */
    if (*cptr >= '0' && *cptr <= '7') {
        /* Восьмеричное представление. */
        cptr = besm6_parse_octal (cptr, &reg);  /* get register */
        if (! cptr || reg > 15) {
            /*printf ("Bad register\n");*/
            return 0;
        }
        cptr = skip_spaces (cptr);              /* absorb spaces */
        if (*cptr == '2' || *cptr == '3') {
            /* Длинная команда. */
            cptr = besm6_parse_octal (cptr, &opcode);
            if (! cptr || opcode < 020 || opcode > 037) {
                /*printf ("Bad long opcode\n");*/
                return 0;
            }
            opcode <<= 3;
        } else {
            /* Короткая команда. */
            cptr = besm6_parse_octal (cptr, &opcode);
            if (! cptr || opcode > 0177) {
                /*printf ("Bad short opcode\n");*/
                return 0;
            }
        }
        cptr = besm6_parse_octal (cptr, &addr); /* get address */
        if (! cptr || addr > BITS(15) ||
            (opcode <= 0177 && addr > BITS(12))) {
            /*printf ("Bad address\n");*/
            return 0;
        }
    } else {
        /* Мнемоническое представление команды. */
        cptr = get_alnum (cptr, gbuf);          /* get opcode */
        opcode = besm6_opcode (gbuf);
        if (opcode < 0) {
            /*printf ("Bad opname: %s\n", gbuf);*/
            return 0;
        }
        negate = 0;
        cptr = skip_spaces (cptr);              /* absorb spaces */
        if (*cptr == '-') {                     /* negative offset */
            negate = 1;
            cptr = skip_spaces (cptr + 1);  /* absorb spaces */
        }
        addr = 0;
        if (*cptr >= '0' && *cptr <= '7') {
            /* Восьмеричный адрес. */
            cptr = besm6_parse_octal (cptr, &addr);
            if (! cptr || addr > BITS(15)) {
                /*printf ("Bad address: %o\n", addr);*/
                return 0;
            }
            if (negate)
                addr = (- addr) & BITS(15);
            if (opcode <= 077 && addr > BITS(12)) {
                if (addr < 070000) {
                    /*printf ("Bad short address: %o\n", addr);*/
                    return 0;
                }
                opcode |= 0100;
                addr &= BITS(12);
            }
        }
        reg = 0;
        cptr = skip_spaces (cptr);              /* absorb spaces */
        if (*cptr == '(') {
            /* Индекс-регистр в скобках. */
            cptr = besm6_parse_octal (cptr+1, &reg);
            if (! cptr || reg > 15) {
                /*printf ("Bad register: %o\n", reg);*/
                return 0;
            }
            cptr = skip_spaces (cptr);      /* absorb spaces */
            if (*cptr != ')') {
                /*printf ("No closing brace\n");*/
                return 0;
            }
            ++cptr;
        }
    }
    *val = reg << 20 | opcode << 12 | addr;
    return cptr;
}

/*
 * Instruction parse: two commands per word.
 */
t_stat parse_instruction_word (CONST char *cptr, t_value *val)
{
    uint32 left, right;

    *val = 0;
    cptr = parse_instruction (cptr, &left);
    if (! cptr)
        return SCPE_ARG;
    right = 0;
    cptr = skip_spaces (cptr);
    if (*cptr == ',') {
        cptr = parse_instruction (cptr + 1, &right);
        if (! cptr)
            return SCPE_ARG;
    }
    cptr = skip_spaces (cptr);                      /* absorb spaces */
    if (*cptr != 0 && *cptr != ';' && *cptr != '\n' && *cptr != '\r') {
        /*printf ("Extra symbols at eoln: %s\n", cptr);*/
        return SCPE_2MARG;
    }
    *val = (t_value) left << 24 | right;
    return SCPE_OK;
}

/*
 * Печать машинной инструкции с мнемоникой.
 */
/* Use scp.c provided fprintf function */
#define fprintf Fprintf
#define fputs(_s,f) Fprintf(f,"%s",_s)
#define fputc(_c,f) Fprintf(f,"%c",_c)
void besm6_fprint_cmd (FILE *of, uint32 cmd)
{
    int reg, opcode, addr;

    reg = (cmd >> 20) & 017;
    if (cmd & BBIT(20)) {
        opcode = (cmd >> 12) & 0370;
        addr = cmd & BITS(15);
    } else {
        opcode = (cmd >> 12) & 077;
        addr = cmd & 07777;
        if (cmd & BBIT(19))
            addr |= 070000;
    }
    fprintf (of, "%s", besm6_opname (opcode));
    if (addr) {
        fprintf (of, " ");
        if (addr >= 077700)
            fprintf (of, "-%o", (addr ^ 077777) + 1);
        else
            fprintf (of, "%o", addr);
    }
    if (reg) {
        if (! addr)
            fprintf (of, " ");
        fprintf (of, "(%o)", reg);
    }
}

/*
 * Печать машинной инструкции в восьмеричном виде.
 */
void besm6_fprint_insn (FILE *of, uint32 insn)
{
    if (insn & BBIT(20))
        fprintf (of, "%02o %02o %05o ",
                 insn >> 20, (insn >> 15) & 037, insn & BITS(15));
    else
        fprintf (of, "%02o %03o %04o ",
                 insn >> 20, (insn >> 12) & 0177, insn & 07777);
}

/*
 * Symbolic decode
 *
 * Inputs:
 *      *of     = output stream
 *      addr    = current PC
 *      *val    = pointer to data
 *      *uptr   = pointer to unit
 *      sw      = switches
 * Outputs:
 *      return  = status code
 */
t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
                   UNIT *uptr, int32 sw)
{
    t_value cmd;

    if (uptr && (uptr != &cpu_unit))                /* must be CPU */
        return SCPE_ARG;

    cmd = val[0];


    if (sw & SWMASK ('M')) {                        /* symbolic decode? */
        if (sw & SIM_SW_STOP && addr == PC && !(RUU & RUU_RIGHT_INSTR))
            fprintf (of, "-> ");
        besm6_fprint_cmd (of, (uint32)(cmd >> 24));
        if (sw & SIM_SW_STOP)                   /* stop point */
            fprintf (of, ", ");
        else
            fprintf (of, ",\n\t");
        if (sw & SIM_SW_STOP && addr == PC && (RUU & RUU_RIGHT_INSTR))
            fprintf (of, "-> ");
        besm6_fprint_cmd (of, cmd & BITS(24));

    } else if (sw & SWMASK ('I')) {
        besm6_fprint_insn (of, (cmd >> 24) & BITS(24));
        besm6_fprint_insn (of, cmd & BITS(24));
    } else if (sw & SWMASK ('F')) {
        fprintf (of, "%#.2g", besm6_to_ieee(cmd));
    } else if (sw & SWMASK ('B')) {
        fprintf (of, "%03o %03o %03o %03o %03o %03o",
                 (int) (cmd >> 40) & 0377,
                 (int) (cmd >> 32) & 0377,
                 (int) (cmd >> 24) & 0377,
                 (int) (cmd >> 16) & 0377,
                 (int) (cmd >> 8) & 0377,
                 (int) cmd & 0377);
    } else if (sw & SWMASK ('X')) {
        fprintf (of, "%013llx", cmd);
    } else
        fprintf (of, "%04o %04o %04o %04o",
                 (int) (cmd >> 36) & 07777,
                 (int) (cmd >> 24) & 07777,
                 (int) (cmd >> 12) & 07777,
                 (int) cmd & 07777);
    return SCPE_OK;
}

/*
 * Symbolic input
 *
 * Inputs:
 *      *cptr   = pointer to input string
 *      addr    = current PC
 *      *uptr   = pointer to unit
 *      *val    = pointer to output values
 *      sw      = switches
 * Outputs:
 *      status  = error status
 */
t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
    int32 i;

    if (uptr && (uptr != &cpu_unit))                /* must be CPU */
        return SCPE_ARG;
    if (! parse_instruction_word (cptr, val))       /* symbolic parse? */
        return SCPE_OK;

    val[0] = 0;
    for (i=0; i<16; i++) {
        if (*cptr < '0' || *cptr > '7')
            break;
        val[0] = (val[0] << 3) | (*cptr - '0');
        cptr = skip_spaces (cptr+1);            /* next char */
    }
    if (*cptr != 0 && *cptr != ';' && *cptr != '\n' && *cptr != '\r') {
        /*printf ("Extra symbols at eoln: %s\n", cptr);*/
        return SCPE_2MARG;
    }
    return SCPE_OK;
}

/*
 * Чтение строки входного файла.
 * Форматы строк:
 * п 76543                     - адрес пуска
 * в 12345                     - адрес ввода
 * ч -123.45e+6                - вещественное число
 * с 0123 4567 0123 4567       - восьмеричное слово
 * к 00 22 00000 00 010 0000   - команды
 */
t_stat besm6_read_line (FILE *input, int *type, t_value *val)
{
    char buf [512];
    CONST char *p;
    int i, c;
  again:
    if (! fgets (buf, sizeof (buf), input)) {
        *type = 0;
        return SCPE_OK;
    }
    p = skip_spaces (buf);
    if (*p == '\n' || *p == ';')
        goto again;
    c = utf8_to_unicode (&p);
    if (c == CYRILLIC_SMALL_LETTER_VE ||
        c == CYRILLIC_CAPITAL_LETTER_VE ||
        c == 'b' || c == 'B') {
        /* Адрес размещения данных. */
        *type = ':';
        *val = strtol (p, 0, 8);
        return SCPE_OK;
    }
    if (c == CYRILLIC_SMALL_LETTER_PE ||
        c == CYRILLIC_CAPITAL_LETTER_PE ||
        c == 'p' || c == 'P') {
        /* Стартовый адрес. */
        *type = '@';
        *val = strtol (p, 0, 8);
        return SCPE_OK;
    }
    if (c == CYRILLIC_SMALL_LETTER_CHE ||
        c == CYRILLIC_CAPITAL_LETTER_CHE ||
        c == 'f' || c == 'F') {
        /* Вещественное число. */
        *type = '=';
        *val = ieee_to_besm6 (strtod (p, 0));
        return SCPE_OK;
    }
    if (c == CYRILLIC_SMALL_LETTER_ES ||
        c == CYRILLIC_CAPITAL_LETTER_ES ||
        c == 'c' || c == 'C') {
        /* Восьмеричное слово. */
        *type = '=';
        *val = 0;
        for (i=0; i<16; ++i) {
            p = skip_spaces (p);
            if (*p < '0' || *p > '7') {
                if (i == 0) {
                    /* слишком короткое слово */
                    goto bad;
                }
                break;
            }
            *val = *val << 3 | (*p++ - '0');
        }
        return SCPE_OK;
    }
    if (c == CYRILLIC_SMALL_LETTER_KA ||
        c == CYRILLIC_CAPITAL_LETTER_KA ||
        c == 'k' || c == 'K') {
        /* Команда. */
        *type = '*';
        if (parse_instruction_word (p, val) != SCPE_OK)
            goto bad;
        return SCPE_OK;
    }
    /* Неверная строка входного файла */
  bad:    besm6_log ("Invalid input line: %s", buf);
    return SCPE_FMT;
}

/*
 * Load memory from file.
 */
t_stat besm6_load (FILE *input)
{
    int addr, type;
    t_value word;
    t_stat err;

    addr = 1;
    PC = 1;
    for (;;) {
        err = besm6_read_line (input, &type, &word);
        if (err)
            return err;
        switch (type) {
        case 0:                 /* EOF */
            return SCPE_OK;
        case ':':               /* address */
            addr = (int)word;
            break;
        case '=':               /* word */
            if (addr < 010)
                pult [0][addr] = SET_PARITY (word, PARITY_NUMBER);
            else
                memory [addr] = SET_PARITY (word, PARITY_NUMBER);
            ++addr;
            break;
        case '*':               /* instruction */
            if (addr < 010)
                pult [0][addr] = SET_PARITY (word, PARITY_INSN);
            else
                memory [addr] = SET_PARITY (word, PARITY_INSN);
            ++addr;
            break;
        case '@':               /* start address */
            PC = (uint32)word;
            break;
        }
        if (addr > MEMSIZE)
            return SCPE_FMT;
    }
    return SCPE_OK;
}

/*
 * Dump memory to file.
 */
t_stat besm6_dump (FILE *of, const char *fnam)
{
    int addr, last_addr = -1;
    t_value word;

    fprintf (of, "; %s\n", fnam);
    for (addr=1; addr<MEMSIZE; ++addr) {
        if (addr < 010)
            word = pult [0][addr];
        else
            word = memory [addr];
        if (word == 0)
            continue;
        if (addr != last_addr+1) {
            fprintf (of, "\nв %05o\n", addr);
        }
        last_addr = addr;
        if (IS_INSN (word)) {
            fprintf (of, "к ");
            besm6_fprint_cmd (of, (uint32)(word >> 24));
            fprintf (of, ", ");
            besm6_fprint_cmd (of, word & BITS(24));
            fprintf (of, "\t\t; %05o - ", addr);
            fprintf (of, "%04o %04o %04o %04o\n",
                     (int) (word >> 36) & 07777,
                     (int) (word >> 24) & 07777,
                     (int) (word >> 12) & 07777,
                     (int) word & 07777);
        } else {
            fprintf (of, "с %04o %04o %04o %04o",
                     (int) (word >> 36) & 07777,
                     (int) (word >> 24) & 07777,
                     (int) (word >> 12) & 07777,
                     (int) word & 07777);
            fprintf (of, "\t\t; %05o\n", addr);
        }
    }
    return SCPE_OK;
}

/*
 * Loader/dumper
 */
t_stat sim_load (FILE *fi, CONST char *cptr, CONST char *fnam, int dump_flag)
{
    if (dump_flag)
        return besm6_dump (fi, fnam);

    return besm6_load (fi);
}
