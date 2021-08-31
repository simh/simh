/*  sys.c: Intel System Configuration Device

    Copyright (c) 2020, William A. Beech

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
        WILLIAM A. BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
        IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
        CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

        Except as contained in this notice, the name of William A. Beech shall not be
        used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from William A. Beech.

    MODIFICATIONS:

        11 Sep 20 - Original file.

    NOTES:


*/

#include "system_defs.h"                /* system header in system dir */

//option board types

#define SBC064      128
#define SBC464      129
#define SBC201      130
#define SBC202      131
#define SBC204      132
#define SBC206      133
#define SBC208      134
#define ZX200A      135

//Single board computer device types

#define i3214       1
#define i8080       2
#define i8085       3
#define i8251       4
#define i8253       5
#define i8255       6
#define i8259       7
#define IOC_CONT    8
#define IPC_CONT    9
#define MULTI       64
#define EPROM       65
#define RAM         66

//System types

#define MDS_210     0
#define MDS_220     1
#define MDS_225     2
#define MDS_230     3
#define MDS_800     4
#define MDS_810     5
#define SDK_80      6
#define SYS_8010    7
#define SYS_8010A   8
#define SYS_8010B   9
#define SYS_8020    10
#define SYS_80204   11
#define SYS_8024    12
#define SYS_8030    13

#define sys_name    "Intel MDS Configuration Controller"

/* external globals */

extern uint16    PCX;

/* function prototypes */

t_stat sys_cfg(uint16 base, uint16 devnum, uint8 dummy);
t_stat sys_clr(void);
t_stat sys_reset(DEVICE *dptr);
static const char* sys_desc(DEVICE *dptr);
t_stat sys_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sys_show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint16, uint16, uint8);
extern uint8 unreg_dev(uint16);
extern t_stat i3214_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern t_stat i8251_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern t_stat i8253_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern t_stat i8255_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern t_stat i8259_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern t_stat ioc_cont_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern t_stat ipc_cont_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern t_stat EPROM_cfg(uint16 base, uint16 size, uint8 devnum);
extern t_stat RAM_cfg(uint16 base, uint16 size, uint8 dummy);
extern t_stat isbc064_cfg(uint16 base, uint16 size, uint8 dummy);
extern t_stat isbc464_cfg(uint16 base, uint16 size, uint8 dummy);
extern t_stat i3214_clr(void);
extern t_stat i8251_clr(void);
extern t_stat i8253_clr(void);
extern t_stat i8255_clr(void);
extern t_stat i8259_clr(void);
extern t_stat ioc_cont_clr(void);
extern t_stat ipc_cont_clr(void);
extern t_stat EPROM_clr(void);
extern t_stat RAM_clr(void);
extern t_stat isbc064_clr(void);
extern t_stat isbc464_clr(void);
extern void clr_dev(void);

/* globals */

int     model = -1;                     //force no model
int     mem_map = 0;                       //memory model

typedef struct device {
    int id;
    const char *name;
    int num;
    int args;
    t_stat (*cfg_routine)(uint16 val1, uint16 val2, uint8 val3);
    t_stat (*clr_routine)(void);
    uint16 val[8];
    } SYS_DEV;

typedef struct system_model {
    int id;
    const char *name;
    int num;
    SYS_DEV devices[30];
    } SYS_MODEL;

#define SYS_NUM 15

SYS_MODEL models[SYS_NUM+1] = {
    {MDS_210, "MDS-210       ", 9,
//         id           name       num arg routine       routine1      val1    val2  val3
        {{ i8251,       "I8251",    2,  1, i8251_cfg,    i8251_clr,    0xF4,   0xF6 },
         { i8253,       "I8253",    1,  1, i8253_cfg,    i8253_clr,    0xF0 },
         { i8255,       "I8255",    2,  1, i8255_cfg,    i8255_clr,    0xE4,   0xE8 },
         { i8259,       "I8259",    2,  1, i8259_cfg,    i8259_clr,    0xFA,   0xFC },
         { IOC_CONT,    "IOC-CONT", 1,  1, ioc_cont_cfg, ioc_cont_clr, 0xC0 },
         { IPC_CONT,    "IPC-CONT", 1,  1, ipc_cont_cfg, ipc_cont_clr, 0xFF },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x0FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x0000, 0x7FFF },
         { SBC464,      "SBC464",   1,  2, isbc464_cfg,  isbc464_clr,  0xA800, 0x47FF }},
         },
    {MDS_220, "MDS-220       ", 8,
        {{ i8251,       "I8251",    2,  1, i8251_cfg,    i8251_clr,    0xF4,   0xF6 },
         { i8253,       "I8253",    1,  1, i8253_cfg,    i8253_clr,    0xF0 },
         { i8255,       "I8255",    2,  1, i8255_cfg,    i8255_clr,    0xE4,   0xE8 },
         { i8259,       "I8259",    2,  1, i8259_cfg,    i8259_clr,    0xFA,   0xFC },
         { IOC_CONT,    "IOC-CONT", 1,  1, ioc_cont_cfg, ioc_cont_clr, 0xC0 },
         { IPC_CONT,    "IPC-CONT", 1,  1, ipc_cont_cfg, ipc_cont_clr, 0xFF },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x0FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x0000, 0x7FFF }} 
         },
    {MDS_225, "MDS-225       ", 8,
        {{ i8251,       "I8251",    2,  1, i8251_cfg,    i8251_clr,    0xF4,   0xF6 },
         { i8253,       "I8253",    1,  1, i8253_cfg,    i8253_clr,    0xF0 },
         { i8255,       "I8255",    2,  1, i8255_cfg,    i8255_clr,    0xE4,   0xE8 },
         { i8259,       "I8259",    2,  1, i8259_cfg,    i8259_clr,    0xFA,   0xFC },
         { IOC_CONT,    "IOC-CONT", 1,  1, ioc_cont_cfg, ioc_cont_clr, 0xC0 },
         { IPC_CONT,    "IPC-CONT", 1,  1, ipc_cont_cfg, ipc_cont_clr, 0xFF },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x0FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x0000, 0xFFFF }},
         },
    {MDS_230, "MDS-230       ", 9,
        {{ i8251,       "I8251",    2,  1, i8251_cfg,    i8251_clr,    0xF4,   0xF6 },
         { i8253,       "I8253",    1,  1, i8253_cfg,    i8253_clr,    0xF0 },
         { i8255,       "I8255",    2,  1, i8255_cfg,    i8255_clr,    0xE4,   0xE8 },
         { i8259,       "I8259",    2,  1, i8259_cfg,    i8259_clr,    0xFA,   0xFC },
         { IOC_CONT,    "IOC-CONT", 1,  1, ioc_cont_cfg, ioc_cont_clr, 0xC0 },   
         { IPC_CONT,    "IPC-CONT", 1,  1, ipc_cont_cfg, ipc_cont_clr, 0xFF },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x0FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x0000, 0x7FFF },
         { SBC064,      "SBC064",   1,  2, isbc064_cfg,  isbc064_clr,  0x8000, 0x7FFF }},
         },
    {MDS_800, "MDS-800       ", 5,
        {{ i3214,       "I3214",    1,  1, i3214_cfg,    i3214_clr,    0xFC },
         { i8251,       "I8251",    2,  1, i8251_cfg,    i8251_clr,    0xF4,   0xF6 },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x00FF },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0xF800, 0x07FF },
         { SBC064,      "SBC064",   1,  2, isbc064_cfg,  isbc064_clr,  0x0000, 0x7FFF }},
         },
    {MDS_810, "MDS-810       ", 6,
        {{ i3214,       "I3214",    1,  1, i3214_cfg,    i3214_clr,    0xFC },
         { i8251,       "I8251",    2,  1, i8251_cfg,    i8251_clr,    0xF4,   0xF6 },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x00FF },
         { EPROM,       "EPROM2",   1,  2, EPROM_cfg,    EPROM_clr,    0xF800, 0x07FF },
         { SBC064,      "SBC064",   1,  2, isbc064_cfg,  isbc064_clr,  0x0000, 0x7FFF },
         { SBC464,      "SBC464",   1,  2, isbc464_cfg,  isbc464_clr,  0xA800, 0x47FF }},
         },
    {SDK_80, "SDK-80         ", 4,
        {{ i8251,       "I8251",    1,  1, i8251_cfg,    i8251_clr,    0xFA },
         { i8255,       "I8255",    2,  1, i8255_cfg,    i8255_clr,    0xF4,   0xEC },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x0FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x1000, 0x03FF }},
         },
    {SYS_8010, "SYS-80/10    ", 4,
        {{ i8251,       "I8251",    1,  1, i8251_cfg,    i8251_clr,    0xEC },
         { i8255,       "I8255",    2,  1, i8255_cfg,    i8255_clr,    0xE4,   0xE8 },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x0FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x3c00, 0x03FF }},
         },
    {SYS_8010A, "SYS-80/10A  ", 4,
        {{ i8251,       "I8251",    1,  1, i8251_cfg,    i8251_clr,    0xEC },
         { i8255,       "I8255",    2,  1, i8255_cfg,    i8255_clr,    0xE4,   0xE8 },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x1FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x3c00, 0x03FF }},
         },
    {SYS_8010B, "SYS-80/10B  ", 4,
        {{ i8251,       "I8251",    1,  1, i8251_cfg,    i8251_clr,    0xEC },
         { i8255,       "I8255",    2,  1, i8255_cfg,    i8255_clr,    0xE4,   0xE8 },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x3FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x3c00, 0x03FF }},
         },
    {SYS_8020, "SYS-80/20    ", 6,
        {{ i8251,       "I8251",    1,  1, i8251_cfg,    i8251_clr,    0xEC },
         { i8253,       "I8253",    1,  1, i8253_cfg,    i8253_clr,    0xDC },
         { i8255,       "I8255",    1,  1, i8255_cfg,    i8255_clr,    0xE8 },
         { i8259,       "I8259",    1,  1, i8259_cfg,    i8259_clr,    0xDA },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x1FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x3800, 0x07FF }},
         },
    {SYS_8020-4, "SYS-80/20-4", 6,
        {{ i8251,       "I8251",    1,  1, i8251_cfg,    i8251_clr,    0xEC },
         { i8253,       "I8253",    1,  1, i8253_cfg,    i8253_clr,    0xDC },
         { i8255,       "I8255",    1,  1, i8255_cfg,    i8255_clr,    0xE8 },
         { i8259,       "I8259",    1,  1, i8259_cfg,    i8259_clr,    0xDA },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x1FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x3000, 0x0FFF }},
         },
    {SYS_8024, "SYS-80/24    ", 6,
        {{ i8251,       "I8251",    1,  1, i8251_cfg,    i8251_clr,    0xEC },
         { i8253,       "I8253",    1,  1, i8253_cfg,    i8253_clr,    0xDC },
         { i8255,       "I8255",    1,  1, i8255_cfg,    i8255_clr,    0xE8 },
         { i8259,       "I8259",    1,  1, i8259_cfg,    i8259_clr,    0xDA },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x1FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x3c00, 0x03FF }},
         },
    {SYS_8030, "SYS-80/30    ", 6,
        {{ i8251,       "I8251",    1,  1, i8251_cfg,    i8251_clr,    0xEC },
         { i8253,       "I8253",    1,  1, i8253_cfg,    i8253_clr,    0xDC },
         { i8255,       "I8255",    1,  1, i8255_cfg,    i8255_clr,    0xE8 },
         { i8259,       "I8259",    1,  1, i8259_cfg,    i8259_clr,    0xDA },
         { EPROM,       "EPROM",    1,  2, EPROM_cfg,    EPROM_clr,    0x0000, 0x1FFF },
         { RAM,         "RAM",      1,  2, RAM_cfg,      RAM_clr,      0x2000, 0x3FFF }},
         },
    {0}
    };

UNIT sys_unit = { UDATA (NULL, 0, 0) };

REG sys_reg[] = {
    { NULL }
};

MTAB sys_mod[] = {
    { MTAB_XTD | MTAB_VDV, 0, NULL, "MODEL", &sys_set_model, NULL, NULL, 
        "Sets the system model" },
    { MTAB_XTD  | MTAB_VDV, 0, "MODEL", NULL, NULL, &sys_show_model, NULL,  
        "Shows the system devices" },
    { 0 }
};

DEBTAB sys_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE sys_dev = {
    "SYS",              //name
    &sys_unit,          //units
    sys_reg,            //registers
    sys_mod,            //modifiers
    1,                  //numunits
    0,                  //aradix
    0,                  //awidth
    0,                  //aincr
    0,                  //dradix
    0,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    sys_reset,          //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    sys_debug,          //debflags
    NULL,               //msize
    NULL,               //lname
    NULL,               //help routine
    NULL,               //attach help routine
    NULL,               //help context
    &sys_desc           //device description
};

static const char* sys_desc(DEVICE *dptr) {
    return sys_name;
}

t_stat sys_cfg(uint16 base, uint16 devnum, uint8 dummy)
{
    int i, j;
    DEVICE *dptr;
    
    if (model == (-1)) return SCPE_ARG; //no valid config
    sim_printf("sys_cfg: Configure %s:\n", models[model].name);
    switch (model) {                    //set memory map type
        case 0:                         //mds-210
            mem_map = 0;                //ipb
            break;
        case 1:                         //mds-220
            mem_map = 0;                //ipb
            break;
        case 2:                         //mds-225
            mem_map = 1;                //ipc
            break;
        case 3:                         //mds-230
            mem_map = 0;                //ipb
            break;
        case 4:                         //mds-800
            mem_map = 2;                //800
            break;
        case 5:                         //mds-810
            mem_map = 2;                //800
            break;
        case 6:                         //sdk-80
            mem_map = 3;                //sdk-80
            break;
        case 7:                         //sys-8010
            mem_map = 4;                //sys-8010
            break;
        case 8:                         //sys-8010A
            mem_map = 4;                //sys-8010A
            break;
        case 9:                         //sys-8010B
            mem_map = 4;                //sys-8010B
            break;
        case 10:                        //sys-8020
            mem_map = 4;                //sys-8020
            break;
        case 11:                        //sys-8020-4
            mem_map = 4;                //sys-8020-4
            break;
        case 12:                        //sys-8024
            mem_map = 4;                //sys-8024
            break;
        case 13:                        //sys-8030
            mem_map = 4;                //sys-8030
            break;
        default:
            return SCPE_ARG;
    }
    for (i=0; i<models[model].num; i++) { //for each device in model
        dptr = find_dev (models[model].devices[i].name);
        if ((dptr != NULL) && ((dptr->flags & DEV_DIS) != 0)) { // disabled
            dptr->flags &= ~DEV_DIS;    //enable device
        }
        for (j=0; j<models[model].devices[i].num; j++) { //for each instance of a device
            switch(models[model].devices[i].args) {
                case 1:                 //one argument
                    models[model].devices[i].cfg_routine (models[model].devices[i].val[j], j, 0);
                    break;
                case 2:                 //two arguments
                    models[model].devices[i].cfg_routine (models[model].devices[i].val[j], 
                        models[model].devices[i].val[j+1], j);
                    break;
                case 3:                 //three arguments
                    models[model].devices[i].cfg_routine (models[model].devices[i].val[j], 
                        models[model].devices[i].val[j+1], models[model].devices[i].val[j+1] & 0xff);
                    break;
                default:
                    return SCPE_ARG;
            }
        }
    }
    return SCPE_OK;
}

t_stat sys_clr(void)
{
    int i, j;
    DEVICE *dptr;
    
    printf("sys_clr: Unconfiguring %s\n", models[model].name);
    for (i=0; i<models[model].num; i++) { //for each device in model
        dptr = find_dev (models[model].devices[i].name);
        if ((dptr != NULL) && (dptr->flags & DEV_DIS)) { // enabled
            dptr->flags |= DEV_DIS;     //disable device
        }
        for (j=0; j<models[model].devices[i].num; j++) { //for each instance of a device
            printf("   %s%d\n", models[model].devices[i].name, j);
            models[model].devices[i].clr_routine ();
        }
    }
    sim_name[0] = '\0';
//    models[model].name[0] = '\0';
    model = -1;
    mem_map = 0;
    return SCPE_OK;
}

t_stat sys_reset(DEVICE *dptr)
{
    if (dptr == NULL)
        return SCPE_ARG;
    sim_printf("SYS Reset\n");
    sys_cfg(0, 0, 0);
    return SCPE_OK;
}

/* Set/show CPU model */

t_stat sys_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int i, j;
    DEVICE *dptr;
    
    if (cptr == NULL)
        return SCPE_ARG;
    if (model != -1) sys_clr();
    for (i=0; i<SYS_NUM; i++) {               //search stored configurations
        if (!strncmp(cptr, models[i].name, strlen(cptr))) { //find the system
            model = models[i].id;
            strncpy(sim_name, models[i].name, 11);
            printf("sys_set_model: Configuring %s\n", sim_name);
            switch (model) {            //set memory map type
                case 0:                 //mds-210
                    mem_map = 0;        //ipb
                    break;
                case 1:                 //mds-220
                    mem_map = 0;        //ipb
                    break;
                case 2:                 //mds-225
                    mem_map = 1;        //ipc
                    break;
                case 3:                 //mds-230
                    mem_map = 0;        //ipb
                    break;
                case 4:                 //mds-800
                    mem_map = 2;        //800
                    break;
                case 5:                 //mds-810
                    mem_map = 2;        //800
                    break;
                case 6:                 //SDK-80
                    mem_map = 3;        //sdk-80
                    break;
                case 7:                 //sys-8010
                    mem_map = 4;        //sys-8010
                    break;
                case 8:                 //sys-8010A
                    mem_map = 4;        //sys-8010A
                    break;
                case 9:                 //sys-8010B
                    mem_map = 4;        //sys-8010B
                    break;
                case 10:                //sys-8020
                    mem_map = 4;        //sys-8020
                    break;
                case 11:                //sys-8020-4
                    mem_map = 4;        //sys-8020-4
                    break;
                case 12:                //sys-8024
                    mem_map = 4;        //sys-8024
                    break;
                case 13:                //sys-8030
                    mem_map = 4;        //sys-8030
                    break;
                default:
                    return SCPE_ARG;
            }
            for (i=0; i<models[model].num; i++) { //for each device in model
                dptr = find_dev (models[model].devices[i].name);
                if ((dptr != NULL) && ((dptr->flags & DEV_DIS) != 0)) { // disabled
                    dptr->flags &= ~DEV_DIS; //enable device
                }
                for (j=0; j<models[model].devices[i].num; j++) { //for each instance of a device
                    switch(models[model].devices[i].args) {
                        case 1:         //one argument
                            models[model].devices[i].cfg_routine (models[model].devices[i].val[j], j, 0);
                            break;
                        case 2:         //two arguments
                            models[model].devices[i].cfg_routine (models[model].devices[i].val[j], 
                                models[model].devices[i].val[j+1], j);
                            break;
                        case 3:         //three arguments
                            models[model].devices[i].cfg_routine (models[model].devices[i].val[j], 
                                models[model].devices[i].val[j+1], models[model].devices[i].val[j+1] & 0xff);
                            break;
                        default:
                            return SCPE_ARG;
                    }
                }
            }
            return SCPE_OK;
        }
    }
    printf("Unknown Model Name %s\n", cptr);
    return SCPE_ARG;
}

t_stat sys_show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int i, j;
    
    if (uptr == NULL)
        return SCPE_ARG;
    return SCPE_OK;
    fprintf(st, "%s:  %d\n", models[model].name, models[model].num);
    for (i=0; i<models[model].num; i++) {
        fprintf(st, "  %s:", models[model].devices[i].name);
        fprintf(st, " %d", models[model].devices[i].num);
        fprintf(st, " %d", models[model].devices[i].args);
        for (j=0; j<models[model].devices[i].num; j++) {
            if (models[model].devices[i].args == 2)
                fprintf(st, " 0%04XH 0%04XH", models[model].devices[i].val[j], 
                    models[model].devices[i].val[j+1]);
            else
                fprintf(st, " 0%04XH", models[model].devices[i].val[j]);
        }
        fprintf(st, "\n");
    }
    return SCPE_OK;
}

/* end of sys.c */
