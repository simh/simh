/*
 * pdp11_pipanel - PIDP-11/70 panel interface
 *
 * Copyright (c) 2026, John D. Bruner  All Rights Reserved.
 * Portions based upon code from
 *   PDP10/ka10_pipanel.c (Copyright (c) 2022, Richard Cornwell)
 *   PiDP11 (Copyright (c) 2015-2016, Oscar Vermeulen & Joerg Hoppe)
 *   Gray decoding code from Johnny Billquist
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JOHN D. BRUNER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of John D. Bruner shall not be
 * used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from John D. Bruner
 */

#ifdef USE_PIPANEL
 
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <editline/readline.h>
#include <gpiolib.h>
#include "pdp11_defs.h"

#define _countof(x) (sizeof x / sizeof x[0])    // count items in array x

/*
 * pdp11_cpu state and functions
 */
extern t_addr frontpanel_PA;                    // most recent pa
extern t_addr frontpanel_VA;                    // most recent va
extern t_value frontpanel_DATA;                 // most recent data
extern int frontpanel_RW;                       // read=0, write=1
extern int frontpanel_HALT;                     // CPU halted by front panel
extern int     frontpanel_IDMODE;  // 1 = data space access, 0 = instruction space access
extern t_value frontpanel_DATAPATH;// value of shifter in PDP-11 processor data paths
extern t_value frontpanel_IR;      // buffer for instruction register
extern int32 SR;                                // switch register
extern int32 DR;                                // display register
extern int32 saved_PC;                          // program counter
extern int32 cm;                                // current mode in PS
extern int32 MMR0;                              // memory management register 0
extern int32 MMR3;                              // memory management register 3
extern int32 REGFILE[6][2];                     // R0-R5, sets 0 and 1
extern int32 STACKFILE[4];                      // SP: kernel, supervisor, unused, user

extern int32 relocC(int32 va, int32 sw);
extern t_stat iopageR (int32 *data, uint32 addr, int32 access);
extern t_stat iopageW (int32 data, uint32 addr, int32 access);

/*
 * additional panel state
 */
int32 SR22;                                     // panel switches - all 22 bits
t_bool frontpanel_POWER = FALSE;                // power off request
t_stat frontpanel_ADRS_ERR = SCPE_OK;           // address error of last examine/deposit

/*
 * Panel switches
 *
 * GPIO switch row 0 contains the low 12 bits of the switch register
 *  SR11 SR10 SR9 SR8 SR7 SR6 SR5 SR4 SR3 SR2 SR1 SR0
 * GPIO switch row 1 contains the high 10 bits of the switch register and the power switch
 *  0 POWER SR21 SR20 SR19 SR18 SR17 SR16 SR15 SR14 SR13 SR12 
 * GPIO switch row 2 contains the panel actions switches and rotary encoder inputs:
 *   DSEL DSEL ASEL ASEL START S_BUS_CYCLE HALT CONT DEPOSIT EXAM LOAD_ADRS LAMPTEST
 * All switches except LAMPTEST are active low
 * 
 * The current and previous state of the panel action switches are saved in switch_info
 * A 0 to 1 transition of an action switch sets its bit in switch_info.changed
 */
typedef union {
    unsigned bits;
    struct {
        unsigned lamptest:1;                    // lamp test (momentary)
        unsigned load_adrs:1;                   // load address (momentary)
        unsigned exam:1;                        // examine (momentary)
        unsigned deposit:1;                     // deposit (momentary)
        unsigned cont:1;                        // continue (momentary)
        unsigned halt:1;                        // halt (double throw)
        unsigned s_bus_cycle:1;                 // single bus cycle (double throw)
        unsigned start:1;                       // start (momentary)
        unsigned asel:2;                        // address select rotary encoder
        unsigned dsel:2;                        // data select rotary encoder
    };
} switch_info_t;

struct {
    switch_info_t current;                      // current value
    switch_info_t previous;                     // previous value
    switch_info_t changed;                      // value has changed
} switch_state;

/*
 * Address select rotary control
 */
typedef enum addr_sel_t {
    ADDR_SEL_PROG_PHY,                          // program physical
    ADDR_SEL_CONS_PHY,                          // console physical
    ADDR_SEL_KERNEL_D,                          // kernel data (virtual)
    ADDR_SEL_SUPER_D,                           // supervisor data (virtual)
    ADDR_SEL_USER_D,                            // user data (virtual)
    ADDR_SEL_USER_I,                            // user instruction (virtual)
    ADDR_SEL_SUPER_I,                           // supervisor instruction (virtual)
    ADDR_SEL_KERNEL_I                           // kernel instruction (virtual)
} addr_sel_t;
addr_sel_t addr_sel_knob = ADDR_SEL_CONS_PHY;

/*
 * Data select rotary control
 */
typedef enum data_sel_t {
    DATA_SEL_BUS_REG,                           // bus register
    DATA_SEL_DATA_PATHS,                        // datapath/shifter
    DATA_SEL_uADR_CPU_FPU,                      // microcode
    DATA_SEL_DISPLAY_REGISTER                   // display register
} data_sel_t;
data_sel_t data_sel_knob = DATA_SEL_DATA_PATHS;

int knob_rotation_direction = 0;                // if 1, reverse knob rotation

/*
 * We don't have real microcode, so we can't match the 11/70's uADDR FPP CPU display.
 * Instead, we fake it with different patterns for RUN and HALT modes.
 * The patterns are based upon Oscar's and Joerge's observations of the
 * "Miss Piggy" PDP-11/70 at the Living Computers Museum in 2018.
 * 
 * Run mode: 0% .. 100% 100% 50% 10% 100% 50% 80% 100% 10% 50%
 * Halt mode: 0% .. 100% 100% 100% 50% 50% 50% 50%
 */
static const unsigned micro_addr_pattern_run[] = {
    01677, // 1  1 1 0  1 1 1  1 1 1
    01454, // 1  1 0 0  1 0 1  1 0 0
    01675, // 1  1 1 0  1 1 1  1 0 1
    01454, // 1  1 0 0  1 0 1  1 0 0
    01765  // 1  1 1 1  1 1 0  1 0 1
};
static const unsigned micro_addr_pattern_halt[] = { 0170, 0167 };

/*
 * forward declarations for the blink (real-time GPIO read/write multiplexor) thread
 */
static void *blink(void *ptr); // the real-time GPIO read/write multiplexor 
static pthread_t blink_thread;
_Atomic int blink_thread_terminate;

/*
 * forward declarations for the readline hook function that integrates stdin
 * command input with panel switch processing
 */
static char *vm_readline(char *prompt, char *cptr, int32 sz, FILE *file);
static int process_switches(char *cptr, int32 sz, t_bool first_time);

/*
 * Mapping of CPU registers into console address space
 */
static int32 *const console_reg_map[] = {
    &REGFILE[0][0], &REGFILE[1][0], &REGFILE[2][0], &REGFILE[3][0], 
    &REGFILE[4][0], &REGFILE[5][0], &STACKFILE[0], &saved_PC,
    &REGFILE[1][1], &REGFILE[1][1], &REGFILE[2][1], &REGFILE[3][1], 
    &REGFILE[4][1], &REGFILE[5][1], &STACKFILE[1], &STACKFILE[3]
};
#define CONSOLE_REG_PADDR 017777700
#define NUM_CONSOLE_REG _countof(console_reg_map)
#define PADDR_IS_CONSOLE_REG(x) ((x) >= CONSOLE_REG_PADDR && (x) < CONSOLE_REG_PADDR + NUM_CONSOLE_REG)

/*
 * PIPANEL (pseudo-)device
 *
 * The PIPANEL device allows simh scripts to start/stop the panel by
 * attaching/detaching its singleton unit. PIPANEL registers expose
 * the knob positions and knob rotation direction.
 */
REG pipanel_reg[] = {
    { FLDATAD (KNOB_ROTATION, knob_rotation_direction, 0,       "knob rotation direction") },
    { GRDATAD (KNOB_ADDR, addr_sel_knob, 10, 3, 0,              "address select knob") },
    { GRDATAD (KNOB_DATA, data_sel_knob, 10, 2, 0,              "data select knob") },
    { NULL }
};

UNIT pipanel_unit = { UDATA(NULL, UNIT_ATTABLE, 0) };

extern t_stat pipanel_attach(UNIT *, const char *);
extern t_stat pipanel_detach(UNIT *);

DEVICE pipanel_dev = {
    .name = "PIPANEL",
    .units = &pipanel_unit, .numunits = 1,
    .registers = pipanel_reg,
    .aradix = 10, .awidth = 32, .aincr = 1, .dradix = 10, .dwidth = 32,
    .attach = pipanel_attach, .detach = pipanel_detach
};

/*
 * Start the panel operation when the PIPANEL pseudo-device is attached.
 * The filename is remembered, but it is not actually used.
 * (It need not exist, and it won't be created if it doesn't exist.)
 */
t_stat
pipanel_attach(UNIT *uptr, const char *file)
{
    struct sched_param rtschedparam = { .sched_priority = 98 };
    int res;
    static t_bool first_time = TRUE;

    if (first_time) {
        // initialize and map gpiolib (do this only once)
        if (gpiolib_init() < 0)
            return sim_messagef(SCPE_IERR, "cannot initialize GPIO\n");
        if (gpiolib_mmap() != 0)
            return sim_messagef(SCPE_IERR,
                                "cannot mmap GPIO device: %s\n",
                                strerror(errno));
        first_time = FALSE;
    }

    pipanel_unit.flags |= UNIT_ATT;
    pipanel_unit.filename = strdup(file);

    // start blink thread
    blink_thread_terminate = 0;
    if ((res = pthread_create(&blink_thread, NULL, blink, &blink_thread_terminate)) != 0)
        return sim_messagef(SCPE_IERR, 
                            "error creating gpio_mux thread (%s)\n",
                            strerror(res));
    if (!sim_quiet)
        sim_messagef(SCPE_OK, "created blink_thread\n");
    
    // attempt to set blink thread to realtime priority (non-fatal if this fails)
    if ((res = pthread_setschedparam(blink_thread, SCHED_FIFO, &rtschedparam)) != 0 && !sim_quiet)
        sim_messagef(SCPE_OK, "unable to set RT priority (%s)\n", strerror(res));
    
    // configure simh to use our custom readline handler
    sim_vm_readline = &vm_readline;
    return SCPE_OK;
}

/*
 * Stop the panel when the PIPANEL pseudo-device is detached.
 */
t_stat
pipanel_detach(UNIT *uptr)
{
    if (!(pipanel_unit.flags & UNIT_ATT))
        return SCPE_UNATT;
    pipanel_unit.flags &= ~UNIT_ATT;
    free(pipanel_unit.filename);
    pipanel_unit.filename = NULL;
    sim_vm_readline = NULL;
    blink_thread_terminate = 1;
    pthread_join(blink_thread, NULL);
    return SCPE_OK;
}

/*
 * Convert the rotary encoder Gray code outputs to rotary knob positions
 * for both the address and data knobs.
 */
static void
gray_decode(unsigned switchscan)
{
    /*
     * 2 rotary encoders. Each has two switch pins. Normally, both are 0 - no rotation.
     * encoder 1: row1, bits 8,9. Encoder 2: row1, bits 10,11
     * Gray encoding: rotate up sequence   = 11 -> 01 -> 00 -> 10 -> 11
     * Gray encoding: rotate down sequence = 11 -> 10 -> 00 -> 01 -> 11

     * Movement direction based upon previous code and current code
     * Use by looking up gray_shift[previous][current]
     */
    const static enum direction { NOP, CW, CCW } gray_shift[4][4] = {
        { NOP, CCW, CW, NOP },
        { CW, NOP, NOP, CCW },
        { CCW, NOP, NOP, CW },
        { NOP, CW, CCW, NOP }
    };
    const unsigned KNOB_SCALE = 4; // 4 code changes per knob position
    static struct {
        const int scanshift;    // shift offset of bits in switchscan
        const unsigned mask;    // (scale * number of positions) - 1
        unsigned state;         // current state (scale * current position)
        unsigned previous;      // previous value
    } knob[2] = {
        { 8, (KNOB_SCALE * 8) - 1 },    // address select knob
        { 10, (KNOB_SCALE * 4) - 1 }    // data select knob
    };
    static int first_run = 1;
    int knob_increment = knob_rotation_direction ? -1 : 1;
    int i;

    /*
     * For each knob, determine the movement direction and adjust
     * the state accordingly. The knob value only changes when there
     * have been cumulative KNOB_SCALE changes in the same direction.
     */
    for (i = 0; i < 2; i++) {
        unsigned knob_global = (i == 0) ? addr_sel_knob : data_sel_knob;
        unsigned current = (switchscan >> knob[i].scanshift) & 3;
        if (first_run) {
            // Initialize the knob state based upon the current knobValue,
            // which may have been set by a command line argument or
            // environment variable
            knob[i].state = knob_global * KNOB_SCALE;
        } else {
            // If the knob register was changed, reset the knob state
            if (knob[i].state / KNOB_SCALE != knob_global)
                knob[i].state = knob_global * KNOB_SCALE;
            // Update the knob state based upon the previous and
            // current Gray codes
            switch (gray_shift[knob[i].previous][current]) {
            case CW:    knob[i].state += knob_increment; break;
            case CCW:   knob[i].state -= knob_increment; break;
            }
        }
        knob[i].previous = current;
        knob[i].state &= knob[i].mask;
    }
    addr_sel_knob = knob[0].state / KNOB_SCALE;
    data_sel_knob = knob[1].state / KNOB_SCALE;
    first_run = 0;
}

/*
 * count the number of 1 bits in a t_value
 */
static unsigned
bitcount(t_value v) {
    unsigned n;
    for (n = 0; v; v >>= 1)
        if (v & 1)
            n++;
    return n;
}

/*
 * GPIO multiplexor, run in a dedicated thread at real-time priority
 *
 * Each LED is addressed by an ordered pair (LEDrow, column)
 * Each switch is addressed by an ordered pair (row, column)
 * 
 * Determine the values for the LEDs, display each row in turn, and
 * then scan each switch row in turn. Repeat until *terminate is FALSE.
 */
static void *
blink(void *terminate)
{
    /*
     * GPIO pins for LED row selects, switch row selects, and column selects
     */
    static const unsigned ledrows[] = { 20, 21, 22, 23, 24, 25 };
    static const unsigned rows[] = { 16, 17, 18 };
    static const unsigned cols[] = { 26, 27, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };

    /*
     * address and data select knob mappings to LEDs in rows 4 and 5
     *
     * row4: BUS_REG DATA_PATHS CONS_PHY KERNEL_D SUPER_D USER_D PARITY_HIGH PARITY_LOW D15 D14 D13 D12
     * row5: DISPLAY_REGISTER uADR_FPU_CPU PROG_PHY KERNEL_I SUPER_I USER_I 0 0 0 0 0 0
     */
    static const unsigned asel_row4[8] = { 0, 01000, 00400, 00200, 00100, 0, 0, 0 };
    static const unsigned asel_row5[8] = { 01000, 0, 0, 0, 0, 00100, 00200, 00400 };
    static const unsigned dsel_row4[4] = { 04000, 02000, 0, 0 };
    static const unsigned dsel_row5[4] = { 0, 0, 02000, 04000 };

    /*
     * mappings for (MMR0bit0,MMR3bit4) and current PS mode into row 2 LEDS
     * PAR_ERR ADRS_ERR RUN PAUSE MASTER USER SUPER KERNEL DATA A16 A18 A22
     */
    static const unsigned mmr_leds[4]  = { 4, 4, 2, 1 };
    static const unsigned mode_leds[4] = { 0020, 0040, 0, 0100 };

    // indices into micro_addr_pattern_run and micro_addr_pattern_halt
    static unsigned uaddr_run_idx = 0, uaddr_halt_idx = 0;

    unsigned address_leds, data_leds, switchscan[_countof(rows)];
    register int i, j;
    void *exitstatus = (void *)-1;

    switch_state.current.bits = switch_state.changed.bits = 0;

    // initialize LED rows (output, low)
    for (i = 0; i < _countof(ledrows); i++) {
        gpio_set_fsel(ledrows[i], GPIO_FSEL_OUTPUT);
        gpio_set_drive(ledrows[i], DRIVE_LOW);
    }

    // initialize switch rows (output, high)
    for (i = 0; i < _countof(rows); i++) {
        gpio_set_fsel(rows[i], GPIO_FSEL_OUTPUT);
        gpio_set_drive(rows[i], DRIVE_HIGH);
    }

    // initialize columns as inputs with pull up
    for (i = 0; i < _countof(cols); i++) {
        gpio_set_fsel(cols[i], GPIO_FSEL_INPUT);
        gpio_set_pull(cols[i], PULL_UP);
    } 

    // main loop: repeat until we're told to terminate
    while (!*(_Atomic int *)terminate) {
        // configure columns as outputs
        for (i = 0; i < _countof(cols); i++)
            gpio_set_fsel(cols[i], GPIO_FSEL_OUTPUT);

        /*
         * address LEDS:
         *  when simulation is running, display the PC
         *  when simulation is not running, determine the address based upon
         *  the current address knob position
         */
        if (sim_is_running)
            address_leds = saved_PC;
        else switch (addr_sel_knob) {
        case ADDR_SEL_PROG_PHY: address_leds = frontpanel_PA; break;
        case ADDR_SEL_CONS_PHY: address_leds = frontpanel_PA; break;
        default:                address_leds = frontpanel_VA; break;
        }

        /*
         * data LEDS:  display the data selected by the current data knob
         */
        switch (data_sel_knob) {
        case DATA_SEL_BUS_REG:      data_leds = frontpanel_DATA; break;
        case DATA_SEL_DATA_PATHS:   data_leds = frontpanel_DATAPATH; break;
        case DATA_SEL_DISPLAY_REGISTER: data_leds = DR; break;
        case DATA_SEL_uADR_CPU_FPU: 
            if (sim_is_running) {
                data_leds = micro_addr_pattern_run[uaddr_run_idx++ >> 3];
                uaddr_run_idx %= _countof(micro_addr_pattern_run) << 3;
            } else {
                data_leds = micro_addr_pattern_halt[uaddr_halt_idx++ >> 4];
                uaddr_halt_idx %= _countof(micro_addr_pattern_halt) << 4;
            }
            break;
        }
    
        /*
         * light up each row of LEDS
         * determine the column values for this row
         * drive one LED row low for each set of columns
         */
        for (i = 0; i < _countof(ledrows); i++) {
            unsigned led_values = 0;
            if (switch_state.current.lamptest)
                led_values = 07777;
            else switch (i) {
            case 0:
                // low 12 bits of address
                led_values = address_leds;
                break;
            case 1:
                // high 10 bits of address
                led_values = address_leds >> 12;
                break;
            case 2:
                /*
                 * PAR_ERR ADRS_ERR RUN PAUSE MASTER USER SUPER KERNEL DATA A16 A18 A22
                 *
                 * ADRS_ERR is only implemented for panel examine/deposit
                 * PAR_ERR and PAUSE are currently not implemented (always off)
                 * MASTER is faked - on when halted, off when running
                 */
                led_values = mmr_leds[((MMR0 & 1) << 1) | ((MMR3 >> 4) & 1)] |
                             (frontpanel_IDMODE ? 010 : 0) |
                             mode_leds[cm & 3] |
                             (sim_is_running ? 01000 : 00200) |
                             ((!sim_is_running && frontpanel_ADRS_ERR) ? 02000 : 0);
                break;
            case 3:
                // low 12 bits of data
                led_values = data_leds;
                break;
            case 4:
                // high-order bits of data, some address and data select LEDs, parity LEDs
                led_values = ((data_leds >> 12) & 017) |
                              asel_row4[addr_sel_knob] |
                              dsel_row4[data_sel_knob];
                if (frontpanel_RW == 0) {       // parity is always 0 for a write
                    // parity LED is on for an even number of bits
                    led_values |= (!(bitcount(frontpanel_DATA & 0377) & 1)) << 4;
                    led_values |= (!(bitcount((frontpanel_DATA >> 8) & 0377) &1)) << 5;
                }
                break;
            case 5:
                // remaining address and data select LEDs
                led_values = asel_row5[addr_sel_knob] | dsel_row5[data_sel_knob];
                break;
            }

            // set the column values (inverted)
            for (j = 0; j < _countof(cols); j++)
                gpio_set_drive(cols[j], (led_values & (1 << j)) ? DRIVE_LOW : DRIVE_HIGH);
            
            // light up the row
            gpio_set_drive(ledrows[i], DRIVE_HIGH);
            usleep(50);

            // turn off the row (allowing time to settle)
            gpio_set_drive(ledrows[i], DRIVE_LOW);
            usleep(1);
        }

        /*
         * prepare to read switches
         * configure columns as inputs
         */
        for (i = 0; i < _countof(cols); i++)
            gpio_set_fsel(cols[i], GPIO_FSEL_INPUT);
        
        // enable each row and read the switches in that row
        for (i = 0; i < _countof(rows); i++) {
            gpio_set_drive(rows[i], DRIVE_LOW);
            usleep(1); // allow inputs to settle
            switchscan[i] = 0;
            for (j = 0; j < _countof(cols); j++)
                switchscan[i] |= (gpio_get_level(cols[j]) == DRIVE_HIGH) << j;
            gpio_set_drive(rows[i], DRIVE_HIGH);
        }

        /*
         * switch rows 0 and 1 contain the SR switches
         * switch row 2 contains the other active switches and the rotary encoders
         * all switches except lamp test are active low
         * all switches are momentary except HALT and SINGLE BUS CYCLE
         */
        SR22 = (~switchscan[0] & 07777) | ((~switchscan[1] & 01777) << 12);
        SR = SR22 & DMASK;
        gray_decode(switchscan[2]);             // update knob positions
        switch_state.previous.bits = switch_state.current.bits;
        switch_state.current.bits = (switchscan[2] & 0377) ^ 0376;
        switch_state.changed.bits |= (switch_state.current.bits & ~switch_state.previous.bits) & 0236;

        /*
         * if the power switch is pressed, force a halt
         * vm_readline will then generate an exit command
         * 
         * otherwise, halt if the HALT switch is down
         */
        frontpanel_POWER |= !(switchscan[1] & 02000); // latch the power-off request
        frontpanel_HALT = frontpanel_POWER ? 1 : switch_state.current.halt;
    }
    exitstatus = NULL;
    return exitstatus;
}

static volatile t_bool input_wait;
static char  *input_buffer = NULL;

/*
 * Handler for EditLine package when line is complete.
 */
static void
read_line_handler(char *line)
{
    if (line != NULL) {
       input_buffer = line;
       add_history(line);
    }
   input_wait = FALSE;
}

/*
 * Process input from stdin or switches.
 *
 * While waiting for input, check panel switches for changes and take any
 * appropriate actions. Some actions are implemented by synthesizing a simh
 * command. Returns when the user has typed a command or when one is
 * synthesized.
 */
static char *
vm_readline(char *prompt, char *cptr, int32 sz, FILE *file)
{
    int fd = fileno(file);  // file descriptor for select()
    t_bool first_time = TRUE;
    extern volatile t_bool sigterm_received;

    ASSURE(sz > 0);
    *cptr = '\0';
    if (input_buffer != NULL)
        free(input_buffer);
    input_buffer = NULL;
    input_wait = TRUE;
    rl_callback_handler_install(prompt, (rl_vcpfunc_t *)&read_line_handler);
    while (input_wait && !sigterm_received) {
        struct timeval tv = { 0, 10000 };  // 10ms
        fd_set read_set;

        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        (void)select(fd+1, &read_set, NULL, NULL, &tv);
        if (FD_ISSET(fd, &read_set)) {
            rl_callback_read_char();
        } else {
            input_wait = process_switches(cptr, sz, first_time);
            first_time = FALSE;
        }
    }
    rl_callback_handler_remove();
    if (*cptr != '\0')
        printf("%s\n", cptr);               // print the synthesized command
    else if (input_buffer != NULL)
        strlcpy(cptr, input_buffer, sz);    // put the input into the caller's buffer
    else
        return NULL;                        // EOF
    return cptr;
}

/*
 * Process (momentary) panel switches
 * We don't expect to see more than one concurrent momentary switch activation.
 * If somehow it happens, process the highest one according to an arbitrary
 * priority and then reset all of the changed bits.
 * 
 * If first_time is TRUE, reset the panel state (erasing the error state and
 * any loaded address). This happens whenever a simh command is executed, because
 * the command could have made changes to the state - we can't assume things are
 * the same.
 *
 * Return TRUE if the switch (if any) was handled here
 * Return FALSE if a synthesized command was placed into the buffer at cptr
 */
static t_bool
process_switches(char *cptr, int32 sz, t_bool first_time)
{
    /*
     * panel address state
     *
     * Remember how the address was last used (load address, examine, deposit).
     * 
     * exam_increment and exam_decrement indicate the increment for each state.
     * Repeated examine or deposits increment the address each time, but
     * alternating examine and deposit does not (uses same address).
     * Increment by 1 when addressing the general registers via the console;
     * otherwise, increment by 2.
     * An increment of -1 means that examine/deposit should not be performed.
     */
    static enum {
        PADDR_NONE,                         // no address loaded
        PADDR_LOADED,                       // LOAD ADR performed
        PADDR_EXAM1,                        // EXAM performed, increment next by 1
        PADDR_EXAM2,                        // EXAM performed, increment next by 2
        PADDR_DEP1,                         // DEP performed, increment next by 1
        PADDR_DEP2,                         // DEP performed, increment next by 2
        PADDR_ERROR                         // ADRS ERR
    } panel_address_state;
    static int exam_increment[] = { -1, 0, 1, 2, 0, 0, -1 };
    static int dep_increment[]  = { -1, 0, 0, 0, 1, 2, -1 };

    /*
     * relocC() is used to convert virtual addresses to physical addresses (if the
     * address select knob is not set to PROG PHY or CONS PHY). relocC_sw maps the
     * virtual address select positions to the corresponding switches for relocC()
     */
    static const int32 relocC_sw[] = {
        0,                                          // PROG PHY
        0,                                          // CONS PHY
        SWMASK('V') | SWMASK('K') | SWMASK('T'),    // KERNEL D
        SWMASK('V') | SWMASK('S') | SWMASK('T'),    // SUPER D
        SWMASK('V') | SWMASK('U') | SWMASK('T'),    // USER D
        SWMASK('V') | SWMASK('U'),                  // USER I
        SWMASK('V') | SWMASK('S'),                  // SUPER I
        SWMASK('V') | SWMASK('K')                   // KERNEL I
    };

    t_bool input_wait = TRUE;

    if (first_time) {
        panel_address_state = PADDR_NONE;   // reset pending panel address state
        frontpanel_ADRS_ERR = SCPE_OK;      // clear any previous address error indication
        switch_state.changed.bits = 0;      // forget any prior momentary switch transitions
    }

    if (frontpanel_POWER) {
        // power (or pressing address select knob) - exit simulation
        snprintf(cptr, sz, "exit %d\r", switch_state.current.halt);
        input_wait = FALSE;
    } else if (switch_state.changed.bits) {
        if (switch_state.changed.load_adrs) {
            panel_address_state = PADDR_LOADED;
            frontpanel_ADRS_ERR = SCPE_OK;
            frontpanel_PA = SR22;
            frontpanel_VA = SR;
        } else if (switch_state.changed.exam || switch_state.changed.deposit) {
            int increment = switch_state.changed.exam ?
                                exam_increment[panel_address_state] :
                                dep_increment[panel_address_state];
            
            if (increment >= 0) {
                // increment last loaded address, relocate if virtual, examine/deposit
                frontpanel_PA = (frontpanel_PA + increment) & PAMASK;
                frontpanel_VA = (frontpanel_VA + increment) & DMASK;
                if (increment == 1 && frontpanel_PA == CONSOLE_REG_PADDR + NUM_CONSOLE_REG)
                    frontpanel_PA = CONSOLE_REG_PADDR;  // wrap console register adddresses

                // convert virtual address to physical address
                if (addr_sel_knob != ADDR_SEL_PROG_PHY && addr_sel_knob != ADDR_SEL_CONS_PHY)
                    frontpanel_PA = relocC(frontpanel_VA, relocC_sw[addr_sel_knob]);

                // perform examine or deposit using the physical address
                if (frontpanel_PA >= MAXMEMSIZE) {
                    // relocation error
                    frontpanel_ADRS_ERR = SCPE_REL;
                    panel_address_state = PADDR_ERROR;
                } else if (ADDR_IS_MEM(frontpanel_PA)) {
                    // valid memory address - read or write, increment next time by 2
                    if (switch_state.changed.exam) {
                        frontpanel_DATAPATH = RdMemW(frontpanel_PA) & DMASK;
                        panel_address_state = PADDR_EXAM2;
                    } else {
                        WrMemW(frontpanel_PA, SR);
                        panel_address_state = PADDR_DEP2;
                    }
                } else if (frontpanel_PA < IOPAGEBASE) {
                    // invalid address
                    frontpanel_ADRS_ERR = SCPE_NXM;
                    panel_address_state = PADDR_ERROR;
                } else if (addr_sel_knob == ADDR_SEL_CONS_PHY &&
                           PADDR_IS_CONSOLE_REG(frontpanel_PA)) {
                    // general register access, read or write, increment next time by 1
                    if (switch_state.changed.exam) {
                        frontpanel_DATAPATH = *console_reg_map[frontpanel_PA - CONSOLE_REG_PADDR];
                        panel_address_state = PADDR_EXAM1;
                    } else {
                        *console_reg_map[frontpanel_PA - CONSOLE_REG_PADDR] = SR;
                        panel_address_state = PADDR_DEP1;
                    }
                } else {
                    // I/O segment - attempt read or write, increment next time by 2
                    t_stat stat;
                    if (switch_state.changed.exam) {
                        int32 iodata;
                        stat = iopageR(&iodata, frontpanel_PA, READC);
                        if (stat == SCPE_OK)
                            frontpanel_DATAPATH = iodata;
                        panel_address_state = PADDR_EXAM2;
                    } else {
                        stat = iopageW(SR, frontpanel_PA, WRITEC);
                        panel_address_state = PADDR_DEP2;
                    }
                    if (stat != SCPE_OK) {
                        // error reading/writing device register
                        frontpanel_ADRS_ERR = stat;
                        panel_address_state = PADDR_ERROR;
                    }
                }
            }
        } else if (switch_state.changed.cont) {
            // S BUS CYCLE is not implemented
            strlcpy(cptr, switch_state.current.halt ? "step\r" : "cont\r", sz);
            input_wait = FALSE;
        } else if (switch_state.changed.start) {
            // reset if HALT is down; otherwise start at loaded address
            if (switch_state.current.halt)
                strlcpy(cptr, "reset\r", sz);
            else
                snprintf(cptr, sz, "run %06o\r", frontpanel_PA & DMASK);
            input_wait = FALSE;
        }
        switch_state.changed.bits = 0;
    }
    return input_wait;
}
#endif
