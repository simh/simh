/***************************************************************************************
 *  Nonstandard serial attachment for remote 2741 terminal (IO selectric) used by APL\1130
 *  This implementation may be incomplete and/or incorrect
 ***************************************************************************************/

#include "ibm1130_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#define DEBUG_T2741

#if 0
static TMLN t2741_ldsc = { 0 };                         /* line descr for telnet attachment */
static TMXR t2741_tmxr = { 1, 0, 0, &t2741_ldsc };      /* line mux for telnet attachment */
#endif

#define T2741_DSW_TRANSMIT_NOT_READY            0x4000
#define T2741_DSW_READ_RESPONSE                 0x1000
#define T2741_DSW_READ_OVERRUN                  0x0800
#define T2741_DSW_ATTENTION                     0x0010

#define IS_ONLINE(u) (((u)->flags & (UNIT_ATT|UNIT_DIS)) == UNIT_ATT)

#define UNIT_V_PHYSICAL_TERM (UNIT_V_UF + 0)                /* indicates not telnet but attachment to real terminal */
#define UNIT_V_UPCASE        (UNIT_V_UF + 1)                /* indicates upshift performed */
#define UNIT_V_SENDING       (UNIT_V_UF + 2)                /* indicates not telnet but attachment to real terminal */
#define UNIT_V_RECEIVING     (UNIT_V_UF + 3)                /* indicates not telnet but attachment to real terminal */

#define UNIT_PHYSICAL_TERM   (1u << UNIT_V_PHYSICAL_TERM)
#define UNIT_UPCASE          (1u << UNIT_V_UPCASE)
#define UNIT_SENDING         (1u << UNIT_V_SENDING)
#define UNIT_RECEIVING       (1u << UNIT_V_RECEIVING)

#define CODE_SHIFTUP        0x1C00
#define CODE_SHIFTDOWN      0x7C00
#define CODE_CIRCLEC        0x1F00
#define CODE_CIRCLED        0x1600
#define CODE_RETURN         0x5B00
#define CODE_LINEFEED       0x3B00  
#define CODE_ATTENTION      0x0001          /* pseudocode, never really returned as a received character */
#define CODE_UNKNOWN        0x0000

static t_stat t2741_svc      (UNIT *uptr);
static t_stat t2741_reset    (DEVICE *dptr);
static t_stat t2741_attach   (UNIT *uptr, CONST char *cptr);
static t_stat t2741_detach   (UNIT *uptr);
static uint16 ascii_to_t2741 (int ascii);
static const char * t2741_to_ascii (uint16 code);
static void set_transmit_notready (void);

static uint16 t2741_dsw    = T2741_DSW_TRANSMIT_NOT_READY;  /* device status word      */
static uint32 t2741_swait  = 200;                           /* character send wait     */
static uint32 t2741_rwait  = 2000;                          /* character receive wait  */
static uint16 t2741_char   = 0;                             /* last character received */
static int    overrun      = FALSE;
static uint32 t2741_socket = 1130;

UNIT t2741_unit[1] = {
    { UDATA (&t2741_svc, UNIT_ATTABLE, 0) },
};

REG t2741_reg[] = {
    { HRDATA (DSW,      t2741_dsw,  16) },              /* device status word */
    { DRDATA (RTIME,    t2741_rwait, 24), PV_LEFT },    /* character receive wait */
    { DRDATA (STIME,    t2741_swait, 24), PV_LEFT },    /* character send wait */
    { DRDATA (SOCKET,   t2741_socket,16), PV_LEFT },    /* socket number */
    { HRDATA (LASTCHAR, t2741_char,  16), PV_LEFT },    /* last character read */
    { NULL }  };

DEVICE t2741_dev = {
    "T2741", t2741_unit, t2741_reg, NULL,
    1, 16, 16, 1, 16, 16,
    NULL, NULL, t2741_reset,
    NULL, t2741_attach, t2741_detach};

/* xio_t2741_terminal - XIO command interpreter for the terminal adapter */

void xio_t2741_terminal (int32 iocc_addr, int32 iocc_func, int32 iocc_mod)
{
    char msg[80];
    uint16 code;

    switch (iocc_func) {
        case XIO_READ:                                          /* read: return last character read */
            code = t2741_char & 0xFF00;
            M[iocc_addr & mem_mask] = code;
            overrun = FALSE;
#ifdef DEBUG_T2741
/*          trace_both("T2741 %04x READ %02x %s", prev_IAR, code >> 8, t2741_to_ascii(code)); */
#endif
            break;

        case XIO_WRITE:                                         /* write: initiate character send */
            code = M[iocc_addr & mem_mask] & 0xFF00;
#ifdef DEBUG_T2741
            trace_both("T2741 %04x SEND %02x %s", prev_IAR, code >> 8, t2741_to_ascii(code));
#endif
            SETBIT(t2741_dsw, T2741_DSW_TRANSMIT_NOT_READY);
            SETBIT(t2741_unit->flags, UNIT_SENDING);

            if (code == CODE_SHIFTUP)
                SETBIT(t2741_unit->flags, UNIT_UPCASE);
            else if (code == CODE_SHIFTDOWN)
                CLRBIT(t2741_unit->flags, UNIT_UPCASE);

            sim_activate(t2741_unit, t2741_swait);              /* schedule interrupt */
            break;

        case XIO_SENSE_DEV:                                     /* sense device status */
            ACC = t2741_dsw;
#ifdef DEBUG_T2741
/*          trace_both("T2741 %04x SENS %04x%s", prev_IAR, t2741_dsw, (iocc_mod & 0x01) ? " reset" : ""); */
#endif
            if (iocc_mod & 0x01) {                              /* reset interrupts */
                CLRBIT(t2741_dsw, T2741_DSW_READ_RESPONSE);
                CLRBIT(ILSW[4], ILSW_4_T2741_TERMINAL);
            }
            break;

        case XIO_CONTROL:                                       /* control: do something to interface */
#ifdef DEBUG_T2741
            trace_both("T2741 %04x CTRL %04x", prev_IAR, iocc_mod &0xFF);
#endif
            SETBIT(t2741_unit->flags, UNIT_RECEIVING);          /* set mode to receive mode */
            if (IS_ONLINE(t2741_unit) && (t2741_char != 0 || ! feof(t2741_unit->fileref))) {
                sim_activate(t2741_unit, t2741_rwait);
                t2741_char = (CODE_CIRCLED >> 8);               /* first character received after turnaround is circled */
            }
            break;

        default:
            sprintf(msg, "Invalid T2741 XIO function %x", iocc_func);
            xio_error(msg);
    }
}

static void set_transmit_notready (void)
{
    if (IS_ONLINE(t2741_unit) && ! (t2741_unit->flags & UNIT_SENDING))
        CLRBIT(t2741_dsw, T2741_DSW_TRANSMIT_NOT_READY);
    else
        SETBIT(t2741_dsw, T2741_DSW_TRANSMIT_NOT_READY);
}

static t_stat t2741_svc (UNIT *uptr)
{
    int ch = EOF;
    uint16 code;

    if (uptr->flags & UNIT_SENDING) {                       /* xmit: no interrupt, as far as I know. just clr busy bit */
        CLRBIT(uptr->flags, UNIT_SENDING);
        set_transmit_notready();
    }

    if (uptr->flags & UNIT_RECEIVING) {                     /* rcv: fire interrupt */
        t2741_char <<= 8;

        if (t2741_char == 0) {                              /* there is no 2nd character from previous ascii input */
            if ((ch = getc(t2741_unit->fileref)) == EOF)
                t2741_char = 0;
            else {
                if (ch == '\r') {                               /* if we get CR, jump to LF */
                    if ((ch = getc(t2741_unit->fileref)) != '\n') {
                        ungetc(ch, t2741_unit->fileref);
                        ch = '\r';
                    }
                }

                if (ch == '\027') {
                    t2741_char = CODE_LINEFEED;                 /* attention key sends line feed character */
#ifdef DEBUG_T2741
                    trace_both("T2741 ---- ATTENTION");
#endif
                    SETBIT(t2741_dsw, T2741_DSW_ATTENTION);     /* no character returned ? */
                }
                else {
                    t2741_char = ascii_to_t2741(ch);            /* translate to 2741 code(s) */
                }
            }
        }

        code = t2741_char & 0xFF00;

        if (t2741_char != 0) {
            if (overrun)                                        /* previous character was not picked up! */
                SETBIT(t2741_dsw, T2741_DSW_READ_OVERRUN);

            SETBIT(t2741_dsw, T2741_DSW_READ_RESPONSE);
            SETBIT(ILSW[4], ILSW_4_T2741_TERMINAL);             /* issue interrupt */
            calc_ints();

#ifdef DEBUG_T2741
            trace_both("T2741 ---- RCVD %02x '%s' RDRESP%s%s", code >> 8, t2741_to_ascii(code),
                (t2741_dsw & T2741_DSW_READ_OVERRUN) ? "|OVERRUN" : "",
                (t2741_dsw & T2741_DSW_ATTENTION)    ? "|ATTENTION" : "");
#endif

            overrun = TRUE;                                     /* arm overrun flag */
        }

        if (t2741_char == CODE_CIRCLEC)                         /* end of line (CIRCLEC after RETURN) auto downshifts */
            CLRBIT(t2741_unit->flags, UNIT_UPCASE);

        if (t2741_char == 0 || code == CODE_CIRCLEC)
            CLRBIT(uptr->flags, UNIT_RECEIVING);                /* on enter or EOF, stop typing */
        else
            sim_activate(t2741_unit, t2741_rwait);              /* schedule next character to arrive */
    }

    return SCPE_OK;
}

static t_stat t2741_attach (UNIT *uptr, CONST char *cptr)
{
    int rval;

    if ((rval = attach_unit(uptr, cptr)) == SCPE_OK) {          /* use standard attach */
        t2741_char = 0;
        overrun    = FALSE;

        CLRBIT(t2741_unit->flags, UNIT_UPCASE);

        if ((t2741_unit->flags & UNIT_RECEIVING) && ! feof(t2741_unit->fileref))
            sim_activate(t2741_unit, t2741_rwait);                  /* schedule interrupt */
    }

    set_transmit_notready();

    return rval;
}

static t_stat t2741_detach (UNIT *uptr)
{
    t_stat rval;

    if (t2741_unit->flags & UNIT_RECEIVING)                 /* if receive was pending, cancel interrupt */
        sim_cancel(t2741_unit);

    t2741_char = 0;
    overrun    = FALSE;

    rval = detach_unit(uptr);                               /* use standard detach */

    set_transmit_notready();

    return rval;
}

static t_stat t2741_reset  (DEVICE *dptr)
{
    sim_cancel(t2741_unit);

    CLRBIT(t2741_unit->flags, UNIT_SENDING|UNIT_RECEIVING|UNIT_UPCASE);

    t2741_char = 0;
    t2741_dsw  = 0;
    overrun    = FALSE;

    set_transmit_notready();

    CLRBIT(ILSW[4], ILSW_4_T2741_TERMINAL);
    calc_ints();

    return SCPE_OK;
}

static struct tag_t2741_map {
    int code;
    int lcase, ucase;
    t_bool shifts;
} t2741_map[] = {
    {0x4F00, 'A', 'a', TRUE},
    {0x3700, 'B', 'b', TRUE},
    {0x2F00, 'C', 'c', TRUE},
    {0x2A00, 'D', 'd', TRUE},
    {0x2900, 'E', 'e', TRUE},
    {0x6700, 'F', '_', TRUE},
    {0x6200, 'G', 'g', TRUE},
    {0x3200, 'H', 'h', TRUE},
    {0x4C00, 'I', 'i', TRUE},
    {0x6100, 'J', 'j', TRUE},
    {0x2C00, 'K', '\'', TRUE},
    {0x3100, 'L', 'l', TRUE},
    {0x4300, 'M', '|', TRUE},
    {0x2500, 'N', 'n', TRUE},
    {0x5100, 'O', 'o', TRUE},
    {0x6800, 'P', '*', TRUE},
    {0x6D00, 'Q', '?', TRUE},
    {0x4A00, 'R', 'r', TRUE},
    {0x5200, 'S', 's', TRUE},
    {0x2000, 'T', '~', TRUE},
    {0x2600, 'U', 'u', TRUE},
    {0x4600, 'V', 'v', TRUE},
    {0x5700, 'W', 'w', TRUE},
    {0x2300, 'X', 'x', TRUE},
    {0x7300, 'Y', 'y', TRUE},
    {0x1500, 'Z', 'z', TRUE},
    {0x1300, '0', '&', TRUE},
    {0x0200, '1', '?', TRUE},
    {0x0400, '2', '?', TRUE},
    {0x0700, '3', '<', TRUE},
    {0x1000, '4', '?', TRUE},
    {0x0800, '5', '=', TRUE},
    {0x0D00, '6', '?', TRUE},
    {0x0B00, '7', '>', TRUE},
    {0x0E00, '8', '?', TRUE},
    {0x1600, '9', '|', TRUE},
    {0x7000, '/', '\\', TRUE},
    {0x7600, '+', '-', TRUE},
    {0x6400, '?', '?', TRUE},
    {0x4000, '<', '>', TRUE},
    {0x6B00, '[', '(', TRUE},
    {0x4900, ']', ')', TRUE},
    {0x6E00, ',', ';', TRUE},
    {0x4500, '.', ':', TRUE},
    {0x0100, ' ',  0,  FALSE},
    {0x5B00, '\r', 0,  FALSE},
    {0x3B00, '\n', 0,  FALSE},
    {0x5D00, '\b', 0,  FALSE},
    {0x5E00, '\t', 0,  FALSE},
    {0x0001, '\027', 0,  FALSE},
};

static uint16 ascii_to_t2741 (int ascii)
{
    int i;
    uint16 rval = 0;

    ascii &= 0xFF;

    if (ascii == '\n')          /* turn newlines into returns + CIRCLED? */
        return CODE_RETURN | (CODE_CIRCLEC >> 8);

    for (i = sizeof(t2741_map)/sizeof(t2741_map[0]); --i >= 0; ) {
        if (t2741_map[i].shifts) {
            if (t2741_map[i].lcase == ascii) {
                rval = t2741_map[i].code;
                if (t2741_unit->flags & UNIT_UPCASE) {
                    CLRBIT(t2741_unit->flags, UNIT_UPCASE);
                    rval = CODE_SHIFTDOWN | (rval >> 8);
                }
                return rval;
            }
            if (t2741_map[i].ucase == ascii) {
                rval = t2741_map[i].code;
                if (! (t2741_unit->flags & UNIT_UPCASE)) {
                    SETBIT(t2741_unit->flags, UNIT_UPCASE);
                    rval = CODE_SHIFTUP | (rval >> 8);
                }
                return rval;
            }
        }
        else if (t2741_map[i].lcase == ascii)
            return t2741_map[i].code;
    }

    return CODE_UNKNOWN;
}

static const char * t2741_to_ascii (uint16 code)
{
    int i;
    static char string[2] = {'?', '\0'};

    switch (code) {
        case CODE_SHIFTUP:      return "SHIFTUP";
        case CODE_SHIFTDOWN:    return "SHIFTDN";
        case CODE_CIRCLEC:      return "CIRCLEC";
        case CODE_CIRCLED:      return "CIRCLED";
    }

    for (i = sizeof(t2741_map)/sizeof(t2741_map[0]); --i >= 0; ) {
        if (t2741_map[i].code == code) {
            if (t2741_map[i].shifts) {
                string[0] = (t2741_unit->flags & UNIT_UPCASE) ? t2741_map[i].ucase : t2741_map[i].lcase;
                return string;
            }
            switch (t2741_map[i].lcase) {
                case ' ':   return " ";
                case '\r':  return "RETURN";
                case '\n':  return "LINEFEED";
                case '\b':  return "BS";
                case '\t':  return "IDLE";
            }
            break;
        }
    }

    return "?";
}
