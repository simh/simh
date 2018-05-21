/* 3b2_defs.h: AT&T 3B2 Model 400 system-specific logic implementation

   Copyright (c) 2017, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

#include "3b2_sys.h"

#include "3b2_cpu.h"
#include "3b2_iu.h"
#include "3b2_if.h"
#include "3b2_id.h"
#include "3b2_mmu.h"
#include "3b2_ctc.h"
#include "3b2_ports.h"
#include "3b2_sysdev.h"

char sim_name[] = "AT&T 3B2 Model 400";

REG *sim_PC = &cpu_reg[0];

/* All opcodes are 1 or 2 bytes. Operands may be up to 6 bytes, and
   there may be up to 3 operands, for a maximum of 20 bytes */
int32 sim_emax = 20;

extern instr *cpu_instr;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &mmu_dev,
    &timer_dev,
    &tod_dev,
    &nvram_dev,
    &csr_dev,
    &tti_dev,
    &tto_dev,
    &contty_dev,
    &iu_timer_dev,
    &dmac_dev,
    &if_dev,
    &id_dev,
    &ports_dev,
    &ctc_dev,
    NULL
};

const char *sim_stop_messages[] = {
    "Unknown error",
    "Reserved Instruction",
    "Breakpoint",
    "Invalid Opcode",
    "IRQ",
    "Exception/Trap",
    "Exception Stack Too Deep",
    "Unimplemented MMU Feature",
    "System Powered Off",
    "Simulator Error"
};

void full_reset()
{
    cpu_reset(&cpu_dev);
    tti_reset(&tti_dev);
    contty_reset(&contty_dev);
    iu_timer_reset(&iu_timer_dev);
    timer_reset(&timer_dev);
    if_reset(&if_dev);
    id_reset(&id_dev);
    csr_reset(&csr_dev);
    ports_reset(&ports_dev);
    ctc_reset(&ctc_dev);
}

t_stat sim_load(FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    int32 i;
    uint32 addr = 0;
    int32 cnt = 0;

    if ((*cptr != 0) || (flag != 0)) {
        return SCPE_ARG;
    }

    addr = R[NUM_PC];

    while ((i = getc (fileref)) != EOF) {
        pwrite_b(addr, (uint8)i);
        addr++;
        cnt++;
    }

    printf ("%d Bytes loaded.\n", cnt);

    return SCPE_OK;
}

t_stat parse_sym(CONST char *cptr, t_addr exta, UNIT *uptr, t_value *val, int32 sw)
{
    DEVICE *dptr;
    t_stat r;
    int32 k, num, vp;
    int32 len = 4;

    if (sw & (int32) SWMASK ('B')) {
        len = 1;
    } else if (sw & (int32) SWMASK ('H')) {
        len = 2;
    } else if (sw & (int32) SWMASK ('W')) {
        len = 4;
    }

    // Parse cptr
    num = (int32) get_uint(cptr, 16, WORD_MASK, &r);

    if (r != SCPE_OK) {
        return r;
    }

    if (uptr == NULL) {
        uptr = &cpu_unit;
    }

    dptr = find_dev_from_unit(uptr);

    if (dptr == NULL) {
        return SCPE_IERR;
    }

    vp = 0;
    for (k = len - 1; k >= 0; k--) {
        val[vp++] = (num >> (k * 8)) & 0xff;
    }

    return -(vp - 1);
}

t_stat fprint_sym(FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
    uint32 len = 4;
    int32 k, vp, num;
    unsigned int c;

    num = 0;
    vp = 0;

    if (sw & (int32) SWMASK('M')) {
        return fprint_sym_m(of, addr, val);
    }

    if (sw & (int32) SWMASK ('B')) {
        len = 1;
    } else if (sw & (int32) SWMASK ('H')) {
        len = 2;
    } else if (sw & (int32) SWMASK ('W')) {
        len = 4;
    }

    if (sw & (int32) SWMASK('C')) {
        len = 16;
        for (k = (int32) len - 1; k >= 0; k--) {
            c = (unsigned int)val[vp++];
            if (c >= 0x20 && c < 0x7f) {
                fprintf(of, "%c", c);
            } else {
                fprintf(of, ".");
            }
        }
        return -(vp - 1);
    }

    for (k = len - 1; k >= 0; k--) {
        num = num | (((int32) val[vp++]) << (k * 8));
    }

    fprint_val(of, (uint32) num, 16, len * 8, PV_RZRO);

    return -(vp - 1);
}
