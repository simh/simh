/* s100_bus.h

   Copyright (c) 2025 Patrick A. Linstruth

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

   Except as contained in this notice, the name of Patrick Linstruth shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   History:
   11/07/25 Initial version

*/

#ifndef _SIM_BUS_H
#define _SIM_BUS_H

#include "sim_defs.h"
#include "sim_tmxr.h"

#define UNIT_BUS_V_VERBOSE      (UNIT_V_UF+0)               /* warn if ROM is written to                    */
#define UNIT_BUS_VERBOSE        (1 << UNIT_BUS_V_VERBOSE)

/* S100 Bus Architecture */

#define ADDRWIDTH           16
#define DATAWIDTH           8

#define ADDRRADIX           16
#define DATARADIX           16

#define MAXADDR             (1 << ADDRWIDTH)
#define MAXDATA             (1 << DATAWIDTH)
#define ADDRMASK            (MAXADDR - 1)
#define DATAMASK            (MAXDATA - 1)

#define LOG2PAGESIZE        8
#define PAGESIZE            (1 << LOG2PAGESIZE)

#define MAXMEMORY           MAXADDR
#define MAXBANKSIZE         MAXADDR
#define MAXPAGE             (MAXADDR >> LOG2PAGESIZE)
#define PAGEMASK            (MAXPAGE - 1)

#define MAXBANK             16
#define MAXBANKS2LOG        5

#define ADDRESS_FORMAT      "[0x%08x]"

/* This is the I/O configuration table.  There are 255 possible
device addresses, if a device is plugged to a port it's routine
address is here, 'nulldev' means no device is available
*/

#define S100_IO_READ  0
#define S100_IO_WRITE 1

/* Interrupt Vectors */
#define MAX_INT_VECTORS         32      /* maximum number of interrupt vectors */

extern uint32 nmiInterrupt;             /* NMI                     */
extern uint32 vectorInterrupt;          /* Vector Interrupt bits */
extern uint8 dataBus[MAX_INT_VECTORS];  /* Data bus value        */

/*
 * Generic device resource information. Pointed to by DEVICE *up7
 */
typedef struct {
    uint32 io_base;      /* I/O Base Address */
    uint32 io_size;      /* I/O Address Space requirement */
    uint32 mem_base;     /* Memory Base Address */
    uint32 mem_size;     /* Memory Address space requirement */
    TMXR *tmxr;          /* TMXR pointer     */
} RES;

/* data structure for IN/OUT instructions */
typedef struct idev {
    int32 (*routine)(CONST int32 addr, CONST int32 rw, CONST int32 data);
    CONST char *name;
} IDEV;

typedef struct { /* Structure to describe memory device address space */
    int32 (*routine)(CONST int32 addr, CONST int32 rw, CONST int32 data);
    CONST char *name; /* name of handler routine */
} MDEV;

extern t_stat s100_bus_addio(int32 port, int32 size, int32 (*routine)(CONST int32, CONST int32, CONST int32), CONST char* name);
extern t_stat s100_bus_addio_in(int32 port, int32 size, int32 (*routine)(CONST int32, CONST int32, CONST int32), CONST char* name);
extern t_stat s100_bus_addio_out(int32 port, int32 size, int32 (*routine)(CONST int32, CONST int32, CONST int32), CONST char* name);
extern t_stat s100_bus_remio(int32 port, int32 size, int32 (*routine)(CONST int32, CONST int32, CONST int32));
extern t_stat s100_bus_remio_in(int32 port, int32 size, int32 (*routine)(CONST int32, CONST int32, CONST int32));
extern t_stat s100_bus_remio_out(int32 port, int32 size, int32 (*routine)(CONST int32, CONST int32, CONST int32));
extern t_stat s100_bus_addmem(int32 baseaddr, uint32 size, 
    int32 (*routine)(CONST int32 addr, CONST int32 rw, CONST int32 data), CONST char *name);
extern t_stat s100_bus_remmem(int32 baseaddr, uint32 size, 
    int32 (*routine)(CONST int32 addr, CONST int32 rw, CONST int32 data));
extern t_stat s100_bus_setmem_dflt(int32 (*routine)(CONST int32 addr, CONST int32 rw, CONST int32 data), CONST char *name);
extern t_stat s100_bus_remmem_dflt(int32 (*routine)(CONST int32 addr, CONST int32 rw, CONST int32 data));

extern void s100_bus_get_idev(int32 port, IDEV *idev_in, IDEV *idev_out);
extern void s100_bus_get_mdev(int32 addr, MDEV *mdev);
extern int32 nulldev(CONST int32 addr, CONST int32 io, CONST int32 data);

extern uint32 s100_bus_set_addr(uint32 pc);
extern uint32 s100_bus_get_addr(void);

extern t_stat s100_bus_console(UNIT *uptr);
extern UNIT *s100_bus_get_console(void);
extern t_stat s100_bus_noconsole(UNIT *uptr);
extern t_stat s100_bus_poll_kbd(UNIT *uptr);

extern int32 s100_bus_in(int32 port);
extern void s100_bus_out(int32 port, int32 data);
extern int32 s100_bus_memr(t_addr addr);
extern void s100_bus_memw(t_addr addr, int32 data);
extern uint32 s100_bus_int(int32 vector, int32 data);
extern uint32 s100_bus_get_int(void);
extern uint32 s100_bus_get_int_data(int32 vector);
extern uint32 s100_bus_clr_int(int32 vector);
extern void s100_bus_nmi(void);
extern int32 s100_bus_get_nmi(void);
extern void s100_bus_clr_nmi(void);

#define S100_BUS_MEMR    0x01
#define S100_BUS_MEMW    0x02
#define S100_BUS_IN      0x04
#define S100_BUS_OUT     0x08

#define RESOURCE_TYPE_MEMORY (S100_BUS_MEMR | S100_BUS_MEMW)
#define RESOURCE_TYPE_IO     (S100_BUS_IN | S100_BUS_OUT)

#define sim_map_resource(a,b,c,d,e,f) s100_map_resource(a,b,c,d,e,f)

extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern void cpu_raise_interrupt(uint32 irq);

#endif
