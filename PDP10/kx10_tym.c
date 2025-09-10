/* kx10_tym.c: TYM, interface to Tymbase and Tymnet.

   Copyright (c) 2022, Lars Brinkhoff

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   LARS BRINKHOFF BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#include "kx10_defs.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_TYM
#define NUM_DEVS_TYM 0
#endif

#if NUM_DEVS_TYM > 0

#define TYM_NAME        "TYM"
#define MAX_LINES       32      /* Maximum found in the wild. */

#define KEY    0633751506262LL  /* Base key. */
#define BASE   02000            /* Default base address. */
#define SIZE   01000            /* Default size. */

#define LOCK   000              /* Key. */
#define DUMP   002              /* Base dump loc * 16. */
#define IRNG   003              /* Input ring loc * 16. */
#define ISIZ   004              /* Input ring size * 16. */
#define IHP    005              /* Host set input pointer. */
#define IBP    006              /* Base set input pointer. */
#define ORNG   007              /* Output ring loc * 16. */
#define OSIZ   010              /* Output ring size * 16. */
#define OHP    011              /* Host set output pointer. */
#define OBP    012              /* Base set output pointer. */
#define BCRSH  013              /* Base set crash indicator + reason. */
#define HCRSH  014              /* Host set crash reason. */

#define TYMBAS_ANS   001
#define TYMBAS_SHT   002
#define TYMBAS_CRS   003
#define TYMBAS_DIE   004
#define TYMBAS_NSP   005
#define TYMBAS_LOG   006
#define TYMBAS_AUX   007
#define TYMBAS_NOP   010
#define TYMBAS_OUP   011
#define TYMBAS_GOB   012
#define TYMBAS_ZAP   013
#define TYMBAS_EDC   014
#define TYMBAS_LDC   015
#define TYMBAS_GRN   016
#define TYMBAS_RED   017
#define TYMBAS_YEL   020
#define TYMBAS_ORG   021
#define TYMBAS_HNG   022
#define TYMBAS_ETM   023
#define TYMBAS_LTM   024
#define TYMBAS_LOS   025
#define TYMBAS_SUP   026
#define TYMBAS_SUR   027
#define TYMBAS_AXC   030
#define TYMBAS_TSP   031
#define TYMBAS_TSR   032
#define TYMBAS_SAD   033
#define TYMBAS_ECN   034
#define TYMBAS_ECF   035
#define TYMBAS_TCS   036
#define TYMBAS_TCP   037
#define TYMBAS_TCR   040
#define TYMBAS_HSI   041
#define TYMBAS_DATA  0200

static t_stat tym_reset(DEVICE *dptr);
static t_stat tym_attach(UNIT * uptr, CONST char * cptr);
static t_stat tym_detach(UNIT * uptr);
static t_stat tym_interface_srv(UNIT *);
static t_stat tym_alive_srv(UNIT *);
static t_stat tym_input_srv(UNIT *);
static t_stat tym_output_srv(UNIT *);
static const char *tym_description(DEVICE *dptr);

static TMLN tym_ldsc[MAX_LINES] = { 0 };
static TMXR tym_desc = { MAX_LINES, 0, 0, tym_ldsc };

static UNIT tym_unit[] = {
    { UDATA(tym_input_srv, TT_MODE_8B|UNIT_IDLE|UNIT_ATTABLE, 0) },
    { UDATA(tym_output_srv, UNIT_IDLE, 0) },
    { UDATA(tym_interface_srv, UNIT_IDLE, 0) },
    { UDATA(tym_alive_srv, UNIT_IDLE, 0) }
};

static MTAB tym_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &tym_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &tym_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &tym_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &tym_desc, "Display multiplexer statistics" },
    { 0 }
};

/* Simulator debug controls */
static DEBTAB tym_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"IRQ", DEBUG_IRQ, "Show IRQ requests"},
    {0, 0}
};

static int tym_host = 0;
static int tym_base = BASE;
static int tym_size = SIZE;
static uint64 tym_key = KEY;
static int output_port;
static int output_count;

static REG tym_reg[] = {
    { ORDATA(HOST, tym_host, 16) },
    { ORDATA(BASE, tym_base, 22) },
    { ORDATA(SIZE, tym_size, 18) },
    { ORDATA(KEY, tym_key, 36) },
    { ORDATA(PORTS, tym_desc.lines, 8) },
    { 0 }
};

DEVICE tym_dev = {
    "TYM", tym_unit, tym_reg, tym_mod,
    4, 8, 0, 1, 8, 36,
    NULL, NULL, &tym_reset, NULL, &tym_attach, &tym_detach,
    NULL, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, tym_debug,
    NULL, NULL, NULL, NULL, NULL, &tym_description
};

static t_stat tym_reset(DEVICE *dptr)
{
    if (tym_unit[0].flags & UNIT_ATT) {
        sim_activate(&tym_unit[2], 1000);
        sim_activate(&tym_unit[3], 1000);
    } else {
        sim_cancel(&tym_unit[0]);
        sim_cancel(&tym_unit[1]);
        sim_cancel(&tym_unit[2]);
        sim_cancel(&tym_unit[3]);
    }
    return SCPE_OK;
}

static void block(void)
{
    int i;
    for (i = 0; i < MAX_LINES; i++) {
        tym_ldsc[i].rcve = 0;
        tym_ldsc[i].xmte = 0;
    }
}

static t_stat tym_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat stat = tmxr_attach(&tym_desc, uptr, cptr);
    block();
    tym_reset(&tym_dev);
    return stat;
}

static t_stat tym_detach(UNIT *uptr)
{
    t_stat stat = tmxr_detach(&tym_desc, uptr);
    block();
    tym_reset(&tym_dev);
    return stat;
}

static uint64 word(int pointer, int base)
{
    t_addr address = (M[tym_base + base] >> 4) & RMASK;
    address += M[tym_base + pointer] & RMASK;
    return M[address];
}

static void next(int pointer, int size)
{
    uint64 modulo = M[tym_base + size] >> 4;
    M[tym_base + pointer] = (M[tym_base + pointer] + 1) % modulo;
}

static void tym_input(uint64 data)
{
    t_addr address = (M[tym_base + IRNG] >> 4) & RMASK;
    address += M[tym_base + IBP] & RMASK;
    M[address] = data;
}

static uint64 room (int h, int t, int s)
{
    uint64 head = M[tym_base + h];
    uint64 tail = M[tym_base + t];
    uint64 size = M[tym_base + s] >> 4;
    return size - (head - tail) % size;
}

static void send_word(int type, int port, int data1, int data2)
{
    uint64 data;
    sim_debug(DEBUG_DETAIL, &tym_dev,
               "Input from base: %03o %03o %03o %03o\n",
               type, port, data1, data2);
    /* The input ring buffer must have at least one word free. */
    if (room (IBP, IHP, ISIZ) <= 1)
        return;
    data = (uint64)type << 28;
    data |= (uint64)port << 20;
    data |= (uint64)data1 << 12;
    data |= (uint64)data2 << 4;
    tym_input(data);
    next(IBP, ISIZ);
}

static void send_character(int port, int c)
{
    sim_debug(DEBUG_DATA, &tym_dev, "Base: send port %d %03o '%c'.\n",
               port, c, c);
    send_word(TYMBAS_DATA | 1, port, c, 0);
}

static void send_string(int port, const char *string)
{
    int n = strlen(string);
    int m;

    for (;;) {
        if (n <= 0)
            return;
        else if (n == 1) {
            send_character(port, string[0]);
            return;
        }

        m = n > 0177 ? 0177 : n;
        n -= m;
        send_word(TYMBAS_DATA | m, port, string[0], string[1]);
        string += 2;
        m -= 2;
        while (m > 0) {
            send_word(string[0], string[1], string[2], string[3]);
            string += 4;
            m -= 4;
        }
    }
}

static void send_login(int port)
{
    sim_debug(DEBUG_CMD, &tym_dev, "Base: send login %d.\n", port);
    send_word(TYMBAS_LOG, port, 0, 0);
    /* This isn't right, but good enough for now. */
    send_string(port, ".....USER\r");
}

static void send_zap(int port)
{
    sim_debug(DEBUG_CMD, &tym_dev, "Base: send zap %d.\n", port);
    send_word(TYMBAS_ZAP, port, 0, 0);
}

static void send_orange (int port)
{
    sim_debug(DEBUG_CMD, &tym_dev, "Base: send orange ball %d.\n", port);
    send_word(TYMBAS_ORG, port, 0, 0);
}

static void recv_ans(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, "system is answering\n");
    sim_activate(&tym_unit[0], 1000);
}

static void recv_sht(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, "system is up but shut\n");
}

static void recv_crs(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, "sender is crashed\n");
}

static void recv_die(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, "recipient should crash\n");
}

static void recv_nsp(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, "base taken over by new supervisor\n");
}

static void recv_log(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, "login\n");
    //next 4 data chrs are the info about terminal type, and port or
    //origin, then name, etc.
}

static void recv_aux(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, "supervisor response to establishing auxillary circuit\n");
}

static void recv_nop(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " backpressure on\n");
}

static void recv_oup(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " backpressure off\n");
}

static void recv_gob(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " character gobbler\n");
}

static void recv_zap(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, "Zap circuit, port %d\n", port);
    tmxr_reset_ln (&tym_ldsc[port]);
    tym_ldsc[port].rcve = 0;
    tym_ldsc[port].xmte = 0;
}

static void recv_edc(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " enter defered echo mode\n");
}

static void recv_ldc(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " leave deferred echo mode\n");
}

static void recv_grn(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " green ball\n");
}

static void recv_red(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " red ball\n");
}

static void recv_yel(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " yellow ball\n");
    send_orange (port);
}

static void recv_org(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " orange ball\n");
}

static void recv_hng(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " hang character - not used\n");
}

static void recv_etm(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " enter 2741 transparent mode\n");
}

static void recv_ltm(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " leave 2741 transparent mode\n");
}

static void recv_los(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " lost ball\n");
    //data has been lost from buffers. the data filed may tell how
    //many were lost
}

static void recv_sup(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " supervisor request(aux circuits)\n");
}

static void recv_sur(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " supervisor response(aux circuits)\n");
}

static void recv_axc(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " supervisor string character\n");
}

static void recv_tsp(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " test pattern probe\n");
}

static void recv_tsr(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " test pattern response\n");
}

static void recv_sad(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " host sad\n");
}

static void recv_ecn(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " echo on\n");
}

static void recv_ecf(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " echo off\n");
}

static void recv_tcs(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " term characteristics\n");
    //first data byte indicates which characteristics second data byte
    //indicates value to set to
}

static void recv_tcp(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " term characteristcs probe\n");
    //data byte indicates which terminal characteristic were requested
}

static void recv_tcr(int port, int subtype, int data)
{
    sim_debug(DEBUG_CMD, &tym_dev, " term characteristcs response\n");
    //data is just like tcs, comes in response to a probe; also is
    //reflected by remote when terminal characteristics are sent
}

static void recv_hsi(int port, int subtype, int data)
{
    tym_host = (subtype << 8) | data;
    sim_debug(DEBUG_CMD, &tym_dev, "Host number %06o, %d ports\n",
              tym_host, port);
    /* Close ports above the host-annonuced maximum. */
    tym_desc.lines = port;
    if (tym_desc.lines > MAX_LINES)
        tym_desc.lines = MAX_LINES;
    for (port = tym_desc.lines; port < MAX_LINES; port++)
        tmxr_reset_ln (&tym_ldsc[port]);
    sim_activate(&tym_unit[0], 1000);
}

typedef void (*msgfn) (int, int, int);
static msgfn output[] = {
    NULL,
    recv_ans,
    recv_sht,
    recv_crs,
    recv_die,
    recv_nsp,
    recv_log,
    recv_aux,
    recv_nop,
    recv_oup,
    recv_gob,
    recv_zap,
    recv_edc,
    recv_ldc,
    recv_grn,
    recv_red,
    recv_yel,
    recv_org,
    recv_hng,
    recv_etm,
    recv_ltm,
    recv_los,
    recv_sup,
    recv_sur,
    recv_axc,
    recv_tsp,
    recv_tsr,
    recv_sad,
    recv_ecn,
    recv_ecf,
    recv_tcs,
    recv_tcp,
    recv_tcr,
    recv_hsi
};

static void output_data(int port, int n)
{
    uint64 data;
    int i = 2, c;

    sim_debug(DEBUG_DATA, &tym_dev,
               "Output from host: %d characters to port %d\n", n, port);

    data = word(OBP, ORNG) << 16; /* Discard header. */
    goto start;

    while (n > 0) {
        data = word(OBP, ORNG);
    start:
        for (; i < 4 && n > 0; i++, n--) {
            c = (data >> 28) & 0177;
            data <<= 8;
            sim_debug(DEBUG_DATA, &tym_dev,
                      "Host: send port %d %03o '%c'.\n", port, c, c);
            tmxr_putc_ln(&tym_ldsc[port], c);
            tmxr_poll_tx(&tym_desc);
        }
        i = 0;
        next(OBP, OSIZ);
    }
}

static void tym_output(void)
{
    int type, port, subtype, data;
    while (M[tym_base + OBP] != M[tym_base + OHP]) {
        sim_debug(DEBUG_DETAIL, &tym_dev, "Output from host: %llu %012llo\n",
                   M[tym_base + OBP], word(OBP, ORNG));
        type = (word(OBP, ORNG) >> 28) & 0377;
        port = (word(OBP, ORNG) >> 20) & 0377;
        subtype = (word(OBP, ORNG) >> 12) & 0377;
        data = (word(OBP, ORNG) >> 4) & 0377;
        sim_debug(DEBUG_DETAIL, &tym_dev,
                   "Type %03o, port %03o, subtype %03o, data %03o\n",
                   type, port, subtype, data);
        if (type >= 1 && type <= 41) {
            output[type] (port, subtype, data);
            next(OBP, OSIZ);
        } else if (type & 0200)
            output_data(port, type & 0177);
    }
}

static uint64 lock = ~0ULL;
static uint64 dump = ~0ULL;
static uint64 irng = ~0ULL;
static uint64 isiz = ~0ULL;
static uint64 ihp = ~0ULL;

static uint64 ibp = ~0ULL;
static uint64 orng = ~0ULL;
static uint64 osiz = ~0ULL;
static uint64 ohp = ~0ULL;
static uint64 obp = ~0ULL;
static uint64 bcrsh = ~0ULL;
static uint64 hcrsh = ~0ULL;

static void check(const char *name, int offset, uint64 *value)
{
    uint64 x = M[tym_base + offset];
    if (x != *value) {
        sim_debug(DEBUG_DETAIL, &tym_dev, "%s: %012llo\n", name, x);
        *value = x;
    }
}

static t_stat tym_interface_srv(UNIT *uptr)
{
    check("Dump location", DUMP, &dump);
    check("Input ring location", IRNG, &irng);
    check("Input ring size", ISIZ, &isiz);
    check("Host input pointer", IHP, &ihp);
    check("Base input pointer", IBP, &ibp);
    check("Output ring location", ORNG, &orng);
    check("Output ring size", OSIZ, &osiz);
    check("Host output pointer", OHP, &ohp);
    check("Base output pointer", OBP, &obp);
    check("Base crash reason", BCRSH, &bcrsh);
    check("Host crash reason", HCRSH, &hcrsh);

    if (ohp != obp)
        tym_output();

    sim_activate_after(&tym_unit[2], 1000);
    return SCPE_OK;
}

static t_stat tym_alive_srv(UNIT *uptr)
{
    if (M[tym_base + LOCK] == tym_key) {
        M[tym_base + LOCK] = 1;
    }

    sim_activate_after(&tym_unit[3], 500000);
    return SCPE_OK;
}

static t_stat tym_input_srv(UNIT *uptr)
{
    int32 ch;
    int i;

    if (room (IBP, IHP, ISIZ) > 1) {
        i = tmxr_poll_conn(&tym_desc);
        if (i >= 0) {
            tym_ldsc[i].rcve = 1;
            tym_ldsc[i].xmte = 1;
            send_login(i);
        }
    }

    tmxr_poll_rx(&tym_desc);

    for (i = 0; i < tym_desc.lines; i++) {
        if (tym_ldsc[i].xmte && tym_ldsc[i].conn == 0) {
            tmxr_reset_ln (&tym_ldsc[i]);
            tym_ldsc[i].rcve = 0;
            tym_ldsc[i].xmte = 0;
            send_zap(i);
            continue;
        }

        /* The input ring buffer must have at least one word free. */
        if (room (IBP, IHP, ISIZ) <= 1)
            continue;

        ch = tmxr_getc_ln(&tym_ldsc[i]);
        if (ch & TMXR_VALID)
            send_character(i, ch & 0377);
    }

    sim_activate(&tym_unit[0], 1000);
    return SCPE_OK;
}

static t_stat tym_output_srv(UNIT *uptr)
{
    uint64 data;
    int c;

    if (!tmxr_txdone_ln(&tym_ldsc[output_port])) {
        sim_activate(&tym_unit[1], 1000);
        return SCPE_OK;
    }

    data = word(OBP, ORNG);

    c = (data >> 28) & 0177;
    data <<= 8;
    sim_debug(DEBUG_DATA, &tym_dev,
               "Host: send port %d %03o '%c'.\n", output_port, c, c);
    if (tmxr_putc_ln(&tym_ldsc[output_port], c) == SCPE_STALL)
        return SCPE_OK;

    next(OBP, OSIZ);
    if (output_count > 0)
        sim_activate(&tym_unit[1], 1000);
    return SCPE_OK;
}

static const char *tym_description(DEVICE *dptr)
{
    return "Tymnet interface";
}
#endif
