/*  m68kcpmsim.c: CP/M for Motorola 68000 definitions

 Copyright (c) 2014, Peter Schorn

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
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 Except as contained in this notice, the name of Peter Schorn shall not
 be used in advertising or otherwise to promote the sale, use or other dealings
 in this Software without prior written authorization from Peter Schorn.

 Based on work by David W. Schultz http://home.earthlink.net/~david.schultz (c) 2014


 MC68000 simulation tailored to support CP/M-68K. It includes:

 16MB of memory. (Flat, function codes and address space types ignored.)

 Console I/O using a MC6850 like serial port with interrupts.

 Simulated disk system:

 Since the intent is to support CP/M-68K and it does disk I/O in 128 byte
 chunks, so will this. Control is via several registers mapped into memory:

 Offset     Function        Description
 0           DMA            address to read/write data to
 4           drive          select disk drive
 8           read sector    sector (128 byte) offset on disk
 12          write sector   sector (128 byte) offset on disk
 16          status         read status of operation

 Operation is simple: Set the drive and DMA address and then write the
 sector number to the sector register. This write triggers the requested
 operation. The status of the operation can be determined by reading the
 status register.
 A zero indicates that no error occured.

 Note that these operations invoke read() and write() system calls directly
 so that they will alter the image on the hard disk. KEEP BACKUPS!

 In addition Linux will buffer the writes so they may note be really complete
 for a while. The BIOS flush function invokes a fsync on all open files.

 There are two options for booting CPM:

 S-records: This loads CPM in two parts. The first is in cpm400.bin which
 is created from the srecords in cpm400.sr. The second is in simbios.bin
 which contains the BIOS. Both of these files must be binaries and not
 srecords.

 If you want to alter the bios, rebuild simbios.bin using:

 asl simbios.s
 p2bin simbios.p

 Use altairz80 cpm68k_s to boot.

 Boot track: A CPM loader is in the boot track of simulated drive C. 32K of
 data is loaded from that file to memory starting at $400.

 Use altairz80 cpm68k to boot.

 */

#include "m68k.h"

/* Read/write macros */
#define READ_BYTE(BASE, ADDR) (BASE)[ADDR]
#define READ_WORD(BASE, ADDR) (((BASE)[ADDR] << 8) |    \
    (BASE)[(ADDR) + 1])
#define READ_LONG(BASE, ADDR) (((BASE)[ADDR] << 24) |   \
    ((BASE)[(ADDR) + 1] << 16) |                        \
    ((BASE)[(ADDR) + 2] << 8) |                         \
    (BASE)[(ADDR) + 3])

#define WRITE_BYTE(BASE, ADDR, VAL) (BASE)[ADDR] = (VAL) & 0xff
#define WRITE_WORD(BASE, ADDR, VAL) (BASE)[ADDR] = ((VAL) >> 8) & 0xff;     \
    (BASE)[(ADDR)+1] = (VAL)&0xff
#define WRITE_LONG(BASE, ADDR, VAL) (BASE)[ADDR] = ((VAL) >> 24) & 0xff;    \
    (BASE)[(ADDR) + 1] = ((VAL) >> 16) & 0xff;                              \
    (BASE)[(ADDR) + 2] = ((VAL) >> 8) & 0xff;                               \
    (BASE)[(ADDR) + 3] = (VAL) & 0xff

/* Memory-mapped IO ports */

/* 6850 serial port like thing. Implements a reduced set of functionallity. */
#define MC6850_STAT     0xff1000L   // command/status register
#define MC6850_DATA     0xff1002L   // receive/transmit data register

/* Memory mapped disk system */
#define DISK_BASE       0xff0000L
#define DISK_SET_DMA    (DISK_BASE)
#define DISK_SET_DRIVE  (DISK_BASE + 4)
#define DISK_SET_SECTOR (DISK_BASE + 8)
#define DISK_READ       (DISK_BASE + 12)
#define DISK_WRITE      (DISK_BASE + 16)
#define DISK_STATUS     (DISK_BASE + 20)
#define DISK_FLUSH      (DISK_BASE + 24)

/* Miscellaneous */
#define M68K_GET_TIME   (0xff7ff8)  // read long to get time in seconds
#define M68K_STOP_CPU   (0xff7ffc)  // write long to stop CPU and return to SIMH prompt

/* IRQ connections */
#define IRQ_NMI_DEVICE  7
#define IRQ_MC6850      5

extern uint32 PCX;

/* Prototypes */
static void MC6850_reset(void);
static void m68k_input_device_update(void);
static int nmi_device_ack(void);
static void int_controller_set(uint32 value);
static void int_controller_clear(uint32 value);

/* Data */
static int m68k_MC6850_control = 0;                 /* MC6850 control register      */
static int m68k_MC6850_status = 2;                  /* MC6850 status register       */
static t_stat keyboardCharacter;                    /* one character buffer         */
static t_bool characterAvailable = FALSE;           /* buffer initially empty       */

static uint32 m68k_int_controller_pending = 0;      /* list of pending interrupts   */
static uint32 m68k_int_controller_highest_int = 0;  /* Highest pending interrupt    */

static uint8 m68k_ram[M68K_MAX_RAM + 1];            /* RAM                          */

/* Interface to HDSK device */
extern void hdsk_prepareRead(void);
extern void hdsk_prepareWrite(void);
extern void hdsk_setSelectedDisk(const int32 disk);
extern void hdsk_setSelectedSector(const int32 sector);
extern void hdsk_setSelectedTrack(const int32 track);
extern void hdsk_setSelectedDMA(const int32 dma);
extern int32 hdsk_getStatus(void);
extern t_bool hdsk_checkParameters(void);
extern int32 hdsk_read(void);
extern int32 hdsk_write(void);
extern int32 hdsk_flush(void);

static uint32 m68k_fc;                              /* Current function code from CPU */

extern uint32 m68k_registers[M68K_REG_CPU_TYPE + 1];
extern UNIT cpu_unit;

#if !UNIX_PLATFORM
extern void pollForCPUStop(void);
#endif

#define M68K_BOOT_LENGTH        (32 * 1024)                 /* size of bootstrap    */
#define M68K_BOOT_PC            0x000400                    /* initial PC for boot  */
#define M68K_BOOT_SP            0xfe0000                    /* initial SP for boot  */

t_stat m68k_hdsk_boot(const int32 unitno, DEVICE *dptr,
                      const uint32 verboseMessage, const int32 hdskNumber) {
    UNIT *uptr;
    size_t i;

    if ((unitno < 0) || (unitno >= hdskNumber))
        return SCPE_ARG;

    uptr = (dptr -> units) + unitno;
    if (((uptr -> flags) & UNIT_ATT) == 0) {
        sim_debug(verboseMessage, dptr, "HDSK%d: Boot drive is not attached.\n", unitno);
        return SCPE_ARG;
    }

    if (sim_fseek(uptr -> fileref, 0, SEEK_SET) != 0) {
        sim_debug(verboseMessage, dptr, "HDSK%d: Boot error seeking start.\n", unitno);
        return SCPE_ARG;
    }

    i = sim_fread(&m68k_ram[M68K_BOOT_PC], 1, M68K_BOOT_LENGTH, uptr -> fileref);
    if (i != M68K_BOOT_LENGTH) {
        sim_debug(verboseMessage, dptr,
                  "HDSK%d: Error: Failed to read %i bytes from boot drive.\n",
                  unitno, M68K_BOOT_LENGTH);
        return SCPE_ARG;
    }

    // Now put in values for the stack and PC vectors
    WRITE_LONG(m68k_ram, 0, M68K_BOOT_SP);  // SP
    WRITE_LONG(m68k_ram, 4, M68K_BOOT_PC);  // PC
    m68k_pulse_reset();                     // also calls MC6850_reset()
    m68k_CPUToView();
    return SCPE_OK;
}

void m68k_CPUToView(void) {
    uint32 reg;
    for (reg = M68K_REG_D0; reg <= M68K_REG_CPU_TYPE; reg++)
        m68k_registers[reg] = m68k_get_reg(NULL, (m68k_register_t)reg);
}

void m68k_viewToCPU(void) {
    uint32 reg;
    for (reg = M68K_REG_D0; reg <= M68K_REG_CPU_TYPE; reg++)
        m68k_set_reg((m68k_register_t)reg, m68k_registers[reg]);
}

t_stat sim_instr_m68k(void) {
    t_stat reason = SCPE_OK;
    m68k_viewToCPU();
    while (TRUE) {
        if (sim_interval <= 0) {                            /* check clock queue    */
#if !UNIX_PLATFORM
            /* poll on platforms without reliable signalling but not too often */
            pollForCPUStop(); /* following sim_process_event will check for stop */
#endif
            if ((reason = sim_process_event()))
                break;
            m68k_input_device_update();
        }
        if (sim_brk_summ && sim_brk_test(m68k_get_reg(NULL, M68K_REG_PC), SWMASK('E'))) {
            /* breakpoint?          */
            reason = STOP_IBKPT;                            /* stop simulation      */
            break;
        }
        PCX = m68k_get_reg(NULL, M68K_REG_PC);
        sim_interval--;
        m68k_execute(1);
        if (stop_cpu) {
            reason = SCPE_STOP;
            break;
        }
    }
    m68k_CPUToView();
    return reason;
}

void m68k_clear_memory(void ) {
    uint32 i;
    for (i = 0; i <= M68K_MAX_RAM; i++)
        m68k_ram[i] = 0;
}

void m68k_cpu_reset(void) {
    WRITE_LONG(m68k_ram, 0, 0x00006000);    // SP
    WRITE_LONG(m68k_ram, 4, 0x00000200);    // PC
    m68k_pulse_reset(); // also calls MC6850_reset()
    m68k_CPUToView();
}

/* Implementation for the MC6850 like device

 Only those bits of the control register that enable/disable receive and
 transmit interrupts are implemented.

 In the status register, the Receive Data Register Full, Transmit Data
 Register Empty, and IRQ flags are implemented. Although the transmit
 data register is always empty.
 */

static void MC6850_reset(void) {
    m68k_MC6850_control = 0;
    m68k_MC6850_status = 2;
    characterAvailable = FALSE;
    int_controller_clear(IRQ_MC6850);
}

#define INITIAL_IDLE    100
#define IDLE_SLEEP      20
static uint32 idleCount = INITIAL_IDLE;

static void m68k_input_device_update(void) {
    if (characterAvailable) {
        m68k_MC6850_status |= 1;
        if ((m68k_MC6850_control & 0x80) && !(m68k_MC6850_status & 0x80)) {
            int_controller_set(IRQ_MC6850);
            m68k_MC6850_status |= 0x80;
        }
    } else if (--idleCount == 0) {
        const t_stat ch = sim_poll_kbd();
        idleCount = INITIAL_IDLE;
        if (IDLE_SLEEP)
            sim_os_ms_sleep(IDLE_SLEEP);
        if (ch) {
            characterAvailable = TRUE;
            keyboardCharacter = ch;
        }
    }
}

/* wait until character becomes available */
static uint32 MC6850_data_read(void) {
    t_stat ch;
    int_controller_clear(IRQ_MC6850);
    m68k_MC6850_status &= ~0x81;          // clear data ready and interrupt flag
    if (characterAvailable) {
        ch = keyboardCharacter;
        characterAvailable = FALSE;
    } else
        ch = sim_poll_kbd();
    while ((ch <= 0) && (!stop_cpu)) {
        if (IDLE_SLEEP)
            sim_os_ms_sleep(IDLE_SLEEP);
        ch = sim_poll_kbd();
    }
    if (ch == SCPE_STOP)
        stop_cpu = TRUE;
    return (((ch > 0) && (!stop_cpu)) ? ch & 0xff : 0xff);
}

static int MC6850_status_read() {
    return m68k_MC6850_status;
}

/* Implementation for the output device */
static int MC6850_device_ack(void) {
    return M68K_INT_ACK_AUTOVECTOR;
}

static void MC6850_data_write(uint32 value) {
    sim_putchar(value);
    if ((m68k_MC6850_control & 0x60) == 0x20) { // transmit interupt enabled?
        int_controller_clear(IRQ_MC6850);
        int_controller_set(IRQ_MC6850);
    }
}

static void MC6850_control_write(uint32 val) {
    m68k_MC6850_control = val;
}

/* Read data from RAM */
unsigned int m68k_cpu_read_byte_raw(unsigned int address) {
    if (address > M68K_MAX_RAM) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            sim_printf("M68K: 0x%08x Attempt to read byte from non existing memory 0x%08x." NLP,
                   PCX, address);
        return 0xff;
    }
    return READ_BYTE(m68k_ram, address);
}

unsigned int m68k_cpu_read_byte(unsigned int address) {
    switch(address) {
        case MC6850_DATA:
            return MC6850_data_read();
        case MC6850_STAT:
            return MC6850_status_read();
        default:
            break;
    }
    if (address > M68K_MAX_RAM) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            sim_printf("M68K: 0x%08x Attempt to read byte from non existing memory 0x%08x." NLP,
                   PCX, address);
        return 0xff;
     }
   return READ_BYTE(m68k_ram, address);
}

unsigned int m68k_cpu_read_word(unsigned int address) {
    switch(address) {
        case DISK_STATUS:
            return hdsk_getStatus();
        default:
            break;
    }
    if (address > M68K_MAX_RAM-1) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            sim_printf("M68K: 0x%08x Attempt to read word from non existing memory 0x%08x." NLP,
                   PCX, address);
        return 0xffff;
    }
    return READ_WORD(m68k_ram, address);
}

unsigned int m68k_cpu_read_long(unsigned int address) {
    switch(address) {
        case DISK_STATUS:
            return hdsk_getStatus();
        case M68K_GET_TIME:
            return (unsigned int)(time(NULL));
        default:
            break;
    }
    if (address > M68K_MAX_RAM-3) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            sim_printf("M68K: 0x%08x Attempt to read long from non existing memory 0x%08x." NLP,
                   PCX, address);
        return 0xffffffff;
    }
    return READ_LONG(m68k_ram, address);
}


/* Write data to RAM or a device */
void m68k_cpu_write_byte_raw(unsigned int address, unsigned int value) {
    if (address > M68K_MAX_RAM) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            sim_printf("M68K: 0x%08x Attempt to write byte 0x%02x to non existing memory 0x%08x." NLP,
                   PCX, value & 0xff, address);
        return;
    }
    WRITE_BYTE(m68k_ram, address, value);
}

void m68k_cpu_write_byte(unsigned int address, unsigned int value) {
    switch(address) {
        case MC6850_DATA:
            MC6850_data_write(value & 0xff);
            return;
        case MC6850_STAT:
            MC6850_control_write(value & 0xff);
            return;
        default:
            break;
    }
    if (address > M68K_MAX_RAM) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            sim_printf("M68K: 0x%08x Attempt to write byte 0x%02x to non existing memory 0x%08x." NLP,
                   PCX, value & 0xff, address);
        return;
    }
    WRITE_BYTE(m68k_ram, address, value);
}

void m68k_cpu_write_word(unsigned int address, unsigned int value) {
    if (address > M68K_MAX_RAM-1) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            sim_printf("M68K: 0x%08x Attempt to write word 0x%04x to non existing memory 0x%08x." NLP,
                   PCX, value & 0xffff, address);
        return;
    }
    WRITE_WORD(m68k_ram, address, value);
}

void m68k_cpu_write_long(unsigned int address, unsigned int value) {
    switch(address) {
        case DISK_SET_DRIVE:
            hdsk_setSelectedDisk(value);
            return;

        case DISK_SET_DMA:
            hdsk_setSelectedDMA(value);
            return;

        case DISK_SET_SECTOR:
            hdsk_setSelectedSector(value);
            return;

        case DISK_READ:
            hdsk_setSelectedSector(value);
            hdsk_setSelectedTrack(0);
            hdsk_prepareRead();
            if (hdsk_checkParameters())
                hdsk_read();
            return;

        case DISK_WRITE:
            hdsk_setSelectedSector(value);
            hdsk_setSelectedTrack(0);
            hdsk_prepareWrite();
            if (hdsk_checkParameters())
                hdsk_write();
            return;

        case DISK_FLUSH:
            hdsk_flush();
            return;

        case M68K_STOP_CPU:
            stop_cpu = TRUE;
            return;

        default:
            break;
    }
    if (address > M68K_MAX_RAM-3) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            sim_printf("M68K: 0x%08x Attempt to write long 0x%08x to non existing memory 0x%08x." NLP,
                   PCX, value, address);
        return;
    }
    WRITE_LONG(m68k_ram, address, value);
}

/* Called when the CPU pulses the RESET line */
void m68k_cpu_pulse_reset(void) {
    MC6850_reset();
}

/* Called when the CPU changes the function code pins */
void m68k_cpu_set_fc(unsigned int fc) {
    m68k_fc = fc;
}

/* Called when the CPU acknowledges an interrupt */
int m68k_cpu_irq_ack(int level) {
    switch(level) {
        case IRQ_NMI_DEVICE:
            return nmi_device_ack();
        case IRQ_MC6850:
            return MC6850_device_ack();
    }
    return M68K_INT_ACK_SPURIOUS;
}

/* Implementation for the NMI device */
static int nmi_device_ack(void) {
    int_controller_clear(IRQ_NMI_DEVICE);
    return M68K_INT_ACK_AUTOVECTOR;
}

/* Implementation for the interrupt controller */
static void int_controller_set(uint32 value) {
    const uint32 old_pending = m68k_int_controller_pending;
    m68k_int_controller_pending |= (1<<value);
    if (old_pending != m68k_int_controller_pending && value > m68k_int_controller_highest_int) {
        m68k_int_controller_highest_int = value;
        m68k_set_irq(m68k_int_controller_highest_int);
    }
}

static void int_controller_clear(uint32 value) {
    m68k_int_controller_pending &= ~(1<<value);
    for (m68k_int_controller_highest_int = 7; m68k_int_controller_highest_int > 0;m68k_int_controller_highest_int--)
        if (m68k_int_controller_pending & (1<<m68k_int_controller_highest_int))
            break;
    m68k_set_irq(m68k_int_controller_highest_int);
}

unsigned int m68k_read_disassembler_16(unsigned int address) {
    return m68k_cpu_read_word(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address) {
    return m68k_cpu_read_long(address);
}
