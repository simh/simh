#ifndef QEMU_TIMER_H
#define QEMU_TIMER_H

#include "qemu/typedefs.h"
#include "qemu-common.h"

#define NANOSECONDS_PER_SECOND 1000000000LL

/* timers */

#define SCALE_MS 1000000
#define SCALE_US 1000
#define SCALE_NS 1

/**
 * QEMUClockType:
 *
 * The following clock types are available:
 *
 * @QEMU_CLOCK_REALTIME: Real time clock
 *
 * The real time clock should be used only for stuff which does not
 * change the virtual machine state, as it is run even if the virtual
 * machine is stopped. The real time clock has a frequency of 1000
 * Hz.
 *
 * @QEMU_CLOCK_VIRTUAL: virtual clock
 *
 * The virtual clock is only run during the emulation. It is stopped
 * when the virtual machine is stopped. Virtual timers use a high
 * precision clock, usually cpu cycles (use ticks_per_sec).
 *
 * @QEMU_CLOCK_HOST: host clock
 *
 * The host clock should be use for device models that emulate accurate
 * real time sources. It will continue to run when the virtual machine
 * is suspended, and it will reflect system time changes the host may
 * undergo (e.g. due to NTP). The host clock has the same precision as
 * the virtual clock.
 *
 * @QEMU_CLOCK_VIRTUAL_RT: realtime clock used for icount warp
 *
 * Outside icount mode, this clock is the same as @QEMU_CLOCK_VIRTUAL.
 * In icount mode, this clock counts nanoseconds while the virtual
 * machine is running.  It is used to increase @QEMU_CLOCK_VIRTUAL
 * while the CPUs are sleeping and thus not executing instructions.
 */

typedef enum {
    QEMU_CLOCK_REALTIME = 0,
    QEMU_CLOCK_VIRTUAL = 1,
    QEMU_CLOCK_HOST = 2,
    QEMU_CLOCK_VIRTUAL_RT = 3,
    QEMU_CLOCK_MAX
} QEMUClockType;




/*
 * QEMUClockType
 */

/*
 * qemu_clock_get_ns;
 * @type: the clock type
 *
 * Get the nanosecond value of a clock with
 * type @type
 *
 * Returns: the clock value in nanoseconds
 */
#if defined(__cplusplus)
extern "C" {
#endif
int64_t qemu_clock_get_ns(QEMUClockType type);
#if defined(__cplusplus)
}
#endif

/**
 * qemu_clock_get_ms;
 * @type: the clock type
 *
 * Get the millisecond value of a clock with
 * type @type
 *
 * Returns: the clock value in milliseconds
 */
static inline int64_t qemu_clock_get_ms(QEMUClockType type)
{
    return qemu_clock_get_ns(type) / SCALE_MS;
}

#endif
