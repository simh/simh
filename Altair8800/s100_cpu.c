/* s100_cpu.c: MITS Altair CPU Management

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
   07-Nov-2025 Initial version

*/

#include "sim_defs.h"
#include "altair8800_sys.h"
#include "s100_bus.h"
#include "s100_z80.h"
#include "s100_cpu.h"

REG *sim_PC;

static int32 poc = TRUE; /* Power On Clear */

static ChipType cpu_type = CHIP_TYPE_8080;

static char *cpu_chipname[] = {
    "Intel 8080",
    "Zilog Z80"
};

static t_stat (*cpu_instr)(void) = NULL;
static t_stat (*cpu_parse_sym)(const char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw) = NULL;
static int32  (*cpu_dasm)(char *S, const uint32 *val, const int32 addr) = NULL;

static t_stat cpu_reset(DEVICE *dptr);
static void cpu_set_instr(t_stat (*routine)(void));
static void cpu_set_pc(REG *reg);
static void cpu_set_pc_value(t_value (*routine)(void));
static void cpu_set_parse_sym(t_stat (*routine)(const char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw));
static void cpu_set_dasm(int32 (*routine)(char *S, const uint32 *val, const int32 addr));
static void cpu_set_is_subroutine_call(t_bool (*routine)(t_addr **ret_addrs));

static const char* cpu_description(DEVICE *dptr) {
    return "Central Processing Unit";
}

static CPU cpu[] = {
    { &z80_dev, &z80_pc_reg, &z80_chiptype, &z80_instr, &z80_pc_value,
        &z80_parse_sym, &z80_dasm, &z80_is_pc_a_subroutine_call, &z80_show_help }, // 8080

    { &z80_dev, &z80_pc_reg, &z80_chiptype, &z80_instr, &z80_pc_value,
        &z80_parse_sym, &z80_dasm, &z80_is_pc_a_subroutine_call, &z80_show_help }, // Z80

    { NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};

static UNIT cpu_unit = {
    UDATA (NULL, 0, 0)
};

static REG cpu_reg[] = {
    { NULL }
};

static MTAB cpu_mod[] = {
    { 0 }
};

/* Debug Flags */
static DEBTAB cpu_dt[] = {
    { NULL, 0 }
};

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    NULL, NULL, &cpu_reset,
    NULL, NULL, NULL,
    NULL, (DEV_DISABLE | DEV_DEBUG), 0,
    cpu_dt, NULL, NULL, NULL, NULL, NULL, &cpu_description
};

static t_stat cpu_reset(DEVICE *dptr) {
    if (dptr->flags & DEV_DIS) {    /* Disable Device */
        poc = TRUE;
    }
    else {
        cpu_set_instr(cpu[cpu_type].instr);
        cpu_set_pc(*cpu[cpu_type].pc_reg);
        cpu_set_pc_value(cpu[cpu_type].pc_val);
        cpu_set_parse_sym(cpu[cpu_type].parse_sym);
        cpu_set_dasm(cpu[cpu_type].dasm);
        cpu_set_is_subroutine_call(cpu[cpu_type].isc);

        dptr->units = cpu[cpu_type].dev->units;
        dptr->registers = cpu[cpu_type].dev->registers;
        dptr->modifiers = cpu[cpu_type].dev->modifiers;
        dptr->help = cpu[cpu_type].dev->help;
        dptr->help_ctx = cpu[cpu_type].dev->help_ctx;
        dptr->description = cpu[cpu_type].dev->description;

        if (poc) {
            poc = FALSE;
        }
    }

    /* Reset selected CPU */
    if (cpu[cpu_type].dev != NULL) {
        cpu[cpu_type].dev->reset(cpu[0].dev);
    }

    return SCPE_OK;
}

void cpu_set_chiptype(ChipType new_type)
{
    ChipType old_type = cpu_type;

    if (cpu_type == new_type) {
        return;
    }

    switch (new_type) {
        case CHIP_TYPE_8080:
        case CHIP_TYPE_Z80:
            cpu_type = new_type;
            break;

        default:
            break;
    }

    if (cpu_dev.units[0].flags & UNIT_CPU_VERBOSE) {
        sim_printf("CPU changed from %s to %s\n", cpu_get_chipname(old_type), cpu_get_chipname(new_type));
    }

    /* Install new CPU device */
    if (cpu[cpu_type].chiptype != NULL) {
        *cpu[cpu_type].chiptype = cpu_type;
    }

    cpu_reset(&cpu_dev);
}

ChipType cpu_get_chiptype()
{
    return cpu_type;
}

char * cpu_get_chipname(ChipType type)
{
    return cpu_chipname[type];
}

t_stat sim_instr()
{
    t_stat reason = SCPE_NXDEV;

    if (cpu_instr != NULL) {
        reason = (*cpu_instr)();
    }

    return reason;
}

static void cpu_set_instr(t_stat (*routine)(void))
{
    cpu_instr = routine;
}

static void cpu_set_pc(REG *reg)
{
    sim_PC = reg;
}

static void cpu_set_pc_value(t_value (*routine)(void))
{
    sim_vm_pc_value = routine;
}

static void cpu_set_parse_sym(t_stat (*routine)(const char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw))
{
    cpu_parse_sym = routine;
}

static void cpu_set_dasm(int32 (*routine)(char *S, const uint32 *val, const int32 addr))
{
    cpu_dasm = routine;
}

static void cpu_set_is_subroutine_call(t_bool (*routine)(t_addr **ret_addrs))
{
    sim_vm_is_subroutine_call = routine;
}

t_stat fprint_sym(FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
    char disasm_result[128];
    int32 ch = val[0] & 0x7f;
    long r = 1;
    if (sw & (SWMASK('A') | SWMASK('C'))) {
        fprintf(of, ((0x20 <= ch) && (ch < 0x7f)) ? "'%c'" : "%02x", ch);
        return SCPE_OK;
    }
    if (!(sw & SWMASK('M'))) {
        return SCPE_ARG;
    }

    if (cpu_dasm != NULL) {
        r = cpu_dasm(disasm_result, val, addr);

        fprintf(of, "%s", disasm_result);
    }

    return 1 - r;
}

t_stat parse_sym(const char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
    while (isspace(*cptr)) {
        cptr++; /* absorb spaces */
    }

    if ((sw & (SWMASK('A') | SWMASK('C'))) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
        if (cptr[0] == 0) {
            return SCPE_ARG;    /* must have one char */
        }

        val[0] = (uint32) cptr[0];
        return SCPE_OK;
    }

    if (cpu_parse_sym != NULL) {
        return cpu_parse_sym(cptr, addr, uptr, val, sw);
    }

    return SCPE_OK;
}

