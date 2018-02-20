/*
 * debug output, no display:
 * cc -g -o tst340 tst340.c type340.c -DTY340_NODISPLAY -DDEBUG_TY340
 *
 * w/ display:
 * cc -g -o tst340 tst340.c type340.c display.c x11.c -lm -lX11 -lXt
 */

// possible source of test code
// light pen diag:
// http://bitsavers.informatik.uni-stuttgart.de/pdf/dec/pdp7/DIGITAL-7-78-M_370LightPenDiag_Apr64.pdf

#include <stdio.h>
#include <unistd.h>

#include "display.h"
#include "type340.h"
#include "type340cmd.h"

void dump(int *ip);

int words[] = {
#if 0
    // 11sim!
    020154,
    0221000,
    0120000,
    0600001,
    020000,
    0220000,
    0121000,
    0600400,
    03000
#elif 1
    // p budne: character test
    MPT,                        /* param: point mode */
    MPT|H|0,                    /* point: h=0; point mode */
    MPAR|V|512,                 /* point: v=64; par mode */
    MCHR|S3|IN7,                /* param: chr mode, size 3, intensity 7 */
    CHAR('H'-'@', 'E'-'@', 'L'-'@'),
    CHAR('L'-'@', 'O'-'@', ' '),
    CHAR('W'-'@', 'O'-'@', 'R'-'@'),
    CHAR('L'-'@', 'D'-'@', '!'),
    CHAR(' ', 0, 037),
    MCHR|S2|IN7,                /* param: chr mode, size 2, intensity 7 */
    CHAR(CHRCR, CHRLF, 'H'-'@'),
    CHAR('E'-'@', 'L'-'@', 'L'-'@'),
    CHAR('O'-'@', ' ', 'W'-'@'),
    CHAR('O'-'@', 'R'-'@', 'L'-'@'),
    CHAR('D'-'@', '!', CHRESC),
    MCHR|S1|IN7,                /* param: chr mode, size 1, intensity 7 */
    CHAR(CHRCR, CHRLF, 'H'-'@'),
    CHAR('E'-'@', 'L'-'@', 'L'-'@'),
    CHAR('O'-'@', ' ', 'W'-'@'),
    CHAR('O'-'@', 'R'-'@', 'L'-'@'),
    CHAR('D'-'@', '!', CHRESC),

    MCHR|S0|IN2,               /* param: chr mode, size 0, intensity 2 */
    CHAR(CHRUC, CHRCR, CHRLF),
    CHAR(000, 001, 002), CHAR(003, 004, 005), CHAR(006, 007, ' '),
    CHAR(010, 011, 012), CHAR(013, 014, 015), CHAR(016, 017, ' '),
    CHAR(020, 021, 022), CHAR(023, 024, 025), CHAR(026, 027, ' '),
    CHAR(030, 031, 032),      /* 33-37 are control codes */
    CHAR(040, 041, 042), CHAR(043, 044, 045), CHAR(046, 047, ' '),
    CHAR(050, 051, 052), CHAR(053, 054, 055), CHAR(056, 057, ' '),
    CHAR(060, 061, 062), CHAR(063, 064, 065), CHAR(066, 067, ' '),
    CHAR(070, 071, 072), CHAR(073, 074, 075), CHAR(076, 077, ' '),

    CHAR(CHRESC, CHRESC, CHRESC),
    MCHR|S2|IN7,
    CHAR(CHRLC, CHRCR, CHRLF),
    CHAR(000, 001, 002), CHAR(003, 004, 005), CHAR(006, 007, ' '),
    CHAR(010, 011, 012), CHAR(013, 014, 015), CHAR(016, 017, ' '),
    CHAR(020, 021, 022), CHAR(023, 024, 025), CHAR(026, 027, ' '),
    CHAR(030, 031, 032),
    CHAR(CHRESC, 0, 0),
    STP
#elif 1
    /*
030153: PT LPOFF S2 IN3
221000: PT V 512.
103000: VCT IP H 512.
703100:  ESCP INSFY DN YP4 YP2 RT XP64
140000: INCR
304210:  INSFY INCRPT( PR, PR, PR, PR)
325063:  INSFY INCRPT( PUR, PUR, PD, PD)
631777:  ESCP INSFY INCRPT( PD, PD, PDL, PDL)
100000: VCT
600210:  ESCP INSFY LT XP8
140000: INCR
237463:  INSFY INCRPT( PD, PDL, PD, PD)
231673:  INSFY INCRPT( PD, PD, PDR, PDR)
704210:  ESCP INSFY INCRPT( PR, PR, PR, PR)
100000: VCT
203400:  INSFY UP YP4 YP2 YP1
600203:  ESCP INSFY LT XP2 XP1
140000: INCR
377463:  INSFY INCRPT( PDL, PDL, PD, PD)
631273:  ESCP INSFY INCRPT( PD, PU, PDR, PDR)
002000: PAR STOP
    */
    // H-340_Type_340_Precision_Incremental_CRT_System_Nov64.pdf
    MPT|LPON|S2|IN3,            // 0030133,     /* set params */
    MPT|V|512,                  // 0220000,     /* y axis */
    MVCT|H|IP|512,              // 0103760,     /* x axis, draw line */
    ESCP|INSFY|DN|YP4|YP2|XP64, // 0703100,     /* draw curve */
    MINCR,                      // 0140000,     /* set mode */
    INSFY|INCRPT(PR,PR,PR,PR),  // 0304210,     /* draw curve */
    INSFY|INCRPT(PUR,PUR,PD,PD), // 0325063,    /* draw curve */
    ESCP|INSFY|INCRPT(PD,PD,PDL,PDL), // 0631777,
    MVCT,                       // 0100000,     /* set mode */
    ESCP|INSFY|LT|XP8,          // 0600210,     /* draw line */
    MINCR,                      // 0140000,     /* set mode */
    0237463,                    /* draw curve */
    0231673,                    /* draw curve */
    0704210,                    /* draw curve */
    0100000,                    /* set mode */
    0203400,                    /* draw line */
    0600203,                    /* draw line */
    0140000,                    /* set mode */
    0377463,                    /* draw curve */
    0631273,                    /* draw curve */
#if 0
#endif
    0002000,                    /* stop, set done, send data interrupt */
#else
    // ITS SYSTEM;DDT > @ RECYC
    0020157,
    0261777
#endif
};

int
main() {
#ifdef DUMP
    dump(words);
#endif
    for (;;) {
        ty340_reset();
        for (unsigned i = 0; i < sizeof(words)/sizeof(words[0]); i++) {
#ifdef TY340_NODISPLAY
            putchar('\n');
#endif
            int s = ty340_instruction(words[i]);
#ifdef TY340_NODISPLAY
            printf("  status %#o\n", s);
#endif
            if (s & ST340_STOPPED)
                break;
            /* XXX check for LPHIT? fetch coordinates */
        }
#ifdef TY340_NODISPLAY
        break;
#else
        display_age(1000, 1);
        display_sync();
#endif
    }
}

ty340word
ty340_fetch(ty340word addr) {
    printf("ty340_fetch %#o\n", addr);
    return 0;
}

void
ty340_store(ty340word addr, ty340word value) {
    printf("ty340_store %#o %#o\n", addr, value);
}

void
ty340_cond_int(ty340word status) {
    /*printf("ty340_stop_int\n");*/
}

void
ty340_lp_int(ty340word x, ty340word y) {
    printf("ty340_lp_int %d. %d.\n", x, y);
}

void
ty340_rfd(void) {                       /* request for data */
#ifdef TY340_NODISPLAY
    puts("ty340_rfd");
#endif
}

void
cpu_get_switches(unsigned long *p1, unsigned long *p2) {
}

void
cpu_set_switches(unsigned long sw1, unsigned long sw2) {
}

/****************************************************************
 * dump display list using symbols from type340cmd.h
 */

#ifdef DUMP
// Vector & incremental modes
static int
escpinsfy(int word)
{
    int ret = 0;
    if (word & ESCP) {
        printf(" ESCP");
        ret = 1;
    }
    if (word & INSFY)
        printf(" INSFY");
    return ret;
}

static void
incr(int pt)
{
    pt &= 017;
    //printf(" %#o", pt);
    switch (pt) {
#define P(X) case X: printf(" " #X); return
    P(PR);
    P(PL);
    P(PU);
    P(PD);
    P(PUL);
    P(PUR);
    P(PDL);
    P(PDR);
    }
    printf(" ???");
}

// PAR, SLV, POINT
static int
xmode(int word)
{
    int m = word & MODEMASK;
    switch (m) {
#define M(MODE) case MODE: printf(#MODE); break
    M(MPAR);
    M(MPT);
    M(MSLV);
    M(MCHR);
    M(MVCT);
    M(MVCTC);
    M(MINCR);
    M(MSUBR);
    default: printf("M??"); break;      /* SNH */
    }
    return m;
}

/* returns 1 if not stopped */
int
dump1(int *mp, int word)
{
    int run = 1;
    printf("%06o: ", word);
    switch (*mp) {
    case MPAR:
        *mp = xmode(word);
        // XXX look at reserved bits: 0300600??
        if (word & LPOFF) {
            if ((word & LPON) == LPON)
                printf(" LPON");
            else
                printf(" LPOFF");
        }
        if (word & STP) {
            if ((word & STP) == STP)
                printf(" STP");
            else
                printf(" STOP");
            run = 0;
        }
        switch (word & S3) {
        case S0: printf(" S0"); break;
        case S1: printf(" S1"); break;
        case S2: printf(" S2"); break;
        case S3: printf(" S3"); break;
        }
        switch (word & IN7) {
        case IN0: printf(" IN0"); break;
        case IN1: printf(" IN1"); break;
        case IN2: printf(" IN2"); break;
        case IN3: printf(" IN3"); break;
        case IN4: printf(" IN4"); break;
        case IN5: printf(" IN5"); break;
        case IN6: printf(" IN6"); break;
        case IN7: printf(" IN7"); break;
        }
        break;
    case MPT:
        // 0400000 reserved
        *mp = xmode(word);
        if (word & IP) printf(" IP");
        if (word & V) printf(" V");
        else printf(" H");
        printf(" %d.", word & 01777);
        break;
    case MSLV:
        *mp = xmode(word);
        // reserved: 010000
        printf(" XXX SLAVE");
        break;
    case MCHR:
        printf(" XXX CHR");
        if ((word>>12)&077 == 037 ||
            (word>>6)&077 == 037 ||
            (word&077) == 037)
            *mp = 0;
    case MVCT:
    case MVCTC:
        if (escpinsfy(word)) *mp = 0;
        if (word & 077400) {
            if (word & DN) printf(" DN");
            else printf(" UP");
            if (word & YP64) printf(" YP64");
            if (word & YP32) printf(" YP32");
            if (word & YP16) printf(" YP16");
            if (word & YP8) printf(" YP8");
            if (word & YP4) printf(" YP4");
            if (word & YP2) printf(" YP2");
            if (word & YP1) printf(" YP1");
        }
        if (word & 0377) {
            if (word & LT) printf(" LT");
            else printf(" RT");
            if (word & XP64) printf(" XP64");
            if (word & XP32) printf(" XP32");
            if (word & XP16) printf(" XP16");
            if (word & XP8) printf(" XP8");
            if (word & XP4) printf(" XP4");
            if (word & XP2) printf(" XP2");
            if (word & XP1) printf(" XP1");
        }
        break;
    case MINCR:
        if (escpinsfy(word)) *mp = 0;
        printf(" INCRPT(");
        incr(word >> 12); putchar(',');
        incr(word >> 8);  putchar(',');
        incr(word >> 4);  putchar(',');
        incr(word); putchar(')');
        break;
    case MSUBR:
        puts("XXX SUBR: quitting");
        run = 0;
        break;
    }
    putchar('\n');
    return run;
}

void
dump(int *ip)
{
    int mode = 0;
    puts(" === DUMP ===");
    while (dump1(&mode, *ip++))
        ;
    puts("=== END DUMP ===");
}
#endif
