/* m68k_scp.c: 68k simulator SCP extension

   Copyright (c) 2009-2010 Holger Veit

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
   Holger Veit BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Holger Veit et al shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Holger Veit et al.

   17-Jul-10    HV      Initial version
*/

#include "sim_defs.h"
#include "m68k_cpu.h"
#include <ctype.h>

static t_bool symtrace = TRUE;
static t_stat hdump_cmd(int32 arg, CONST char* buf);
static t_stat symset_cmd(int32 arg, CONST char* buf);
static t_stat symclr_cmd(int32 arg, CONST char* buf);
static t_stat symlist_cmd(int32 arg, CONST char* buf);
static t_stat symtrace_cmd(int32 arg, CONST char* buf);

static CTAB m68k_sim_cmds[] = {
        {"STEP", &run_cmd, RU_STEP,
         "s{tep} {n}               simulate n instructions\n", NULL, &run_cmd_message },
        {"HEXDUMP", &hdump_cmd, 0,
         "hex{dump} range          dump memory\n" },
        {"SYMSET", &symset_cmd, 0,
         "syms{et} name=value        define symbolic name for disassembler/tracer\n"},
        {"SYMCLR",&symclr_cmd, 0,
         "symc{lr} {-a|name}         clear symbolic name / all symbolic names\n"},
        {"SYMLIST", &symlist_cmd, 0,
         "syml{ist} [name]           list symbol table\n"},
        {"SYMTRACE", &symtrace_cmd, 1,
         "symt{race}                 enable symbolic tracing\n"},
        {"NOSYMTRACE", &symtrace_cmd, 0,
         "nosymt{race}               disable symbolic tracing\n"},
        {0,0,0,0}
};

typedef struct _symhash {
    struct _symhash* nnext;
    struct _symhash* vnext;
    char* name;
    t_addr val;
} SYMHASH;
#define SYMHASHSIZE 397

static SYMHASH *symbyname = 0;
static SYMHASH *symbyval = 0;

static void sym_clearall(void)
{
    int i;
    SYMHASH *p,*n;
    
    if (!symbyname) return;
    for (i=0; i<SYMHASHSIZE; i++) {
        p = symbyname[i].nnext;
        while ((n = p) != 0) {
            p = p->nnext;
            free(n->name);
            free(n);
        }
        symbyname[i].nnext = symbyval[i].vnext = 0;
    }
    return;
}

void m68k_sim_init(void)
{
    int i;
    sim_vm_cmd = m68k_sim_cmds;

    sym_clearall();
    symbyname = (SYMHASH*)calloc(sizeof(SYMHASH),SYMHASHSIZE);
    symbyval = (SYMHASH*)calloc(sizeof(SYMHASH),SYMHASHSIZE);
    for (i=0; i<SYMHASHSIZE; i++) 
        symbyval[i].vnext = symbyname[i].nnext = 0;

    symtrace = TRUE;
}

static int getnhash(const char* name)
{
    int i, nhash = 0;
    for (i=0; name[i]; i++) {
        nhash += name[i];
    }
    return nhash % SYMHASHSIZE;
}

static int getvhash(t_addr val)
{
    return val % SYMHASHSIZE;
}

static t_bool sym_lookupname(const char *name,SYMHASH **n)
{
    int hash = getnhash(name);
    SYMHASH *p = symbyname[hash].nnext;
    while (p && strcmp(name,p->name)) p = p->nnext;
    *n = p;
    return p != 0;
}

static t_bool sym_lookupval(t_addr val, SYMHASH **v)
{
    int hash = getvhash(val);
    SYMHASH *p = symbyval[hash].vnext;
    while (p && p->val != val) p = p->vnext;
    *v = p;
    return p != 0;
}

static t_bool sym_enter(const char* name,t_addr val)
{
    int nhash = getnhash(name);
    int vhash = getvhash(val);
    SYMHASH *v, *n, *e;
    
    if (sym_lookupname(name,&n) || sym_lookupval(val,&v)) return FALSE;
    n = symbyname[nhash].nnext;
    v = symbyval[vhash].vnext;
    e = (SYMHASH*)malloc(sizeof(SYMHASH));
    e->nnext = n;
    e->vnext = v;
    e->name = (char *)malloc(strlen(name)+1);
    strcpy(e->name,name);
    e->val = val;
    symbyname[nhash].nnext = symbyval[vhash].vnext = e;
    return TRUE;
}

static t_bool sym_delete(const char* name)
{
    int hash = getnhash(name);
    SYMHASH *p, *q, **n, **v;
    n = &symbyname[hash].nnext;
    while ((p = *n) != 0) {
        if (!strcmp(p->name,name)) { /*found*/
            hash = getvhash(p->val);
            v = &symbyval[hash].vnext;
            while ((q = *v) != 0) {
                if (q->val == p->val) { /*found*/
                    *v = q->vnext;
                    break;
                }
                v = &(q->vnext);
            }
            *n = p->nnext;
            free(p->name);
            free(p);
            return TRUE;
        }
    }
    return FALSE;
}

static t_stat symset_cmd(int32 arg, CONST char* buf)
{
    const char *name,*vstr;
    char gbuf[2*CBUFSIZE];
    t_addr val;

    gbuf[sizeof(gbuf)-1] = '\0';
    strncpy(gbuf, buf, sizeof(gbuf)-1);
    if ((name = strtok(gbuf, "= ")) == 0) return SCPE_2FARG;
    if ((vstr = strtok(NULL, " \t\n")) == 0) return SCPE_2FARG;
    val = strtol(vstr, 0, 16);
    if (!sym_enter(name, val))
        printf("Name or value already exists\n");
    return SCPE_OK;
}

static t_stat symclr_cmd(int32 arg, CONST char* buf)
{
    char* token;
    if (buf[0] == '-' && buf[1]=='a') {
        sym_clearall();
        return SCPE_OK;
    } else {
        char gbuf[2*CBUFSIZE];

        gbuf[sizeof(gbuf)-1] = '\0';
        strncpy(gbuf, buf, sizeof(gbuf)-1);
        token = strtok(gbuf," \t\n");
        if (!token) return SCPE_2FARG;
        return sym_delete(token) ? SCPE_OK : SCPE_ARG;
    }
}

static t_stat symlist_cmd(int32 arg, CONST char* buf)
{
    int i;
    SYMHASH* n;
    char gbuf[2*CBUFSIZE];
    char *name;
    t_bool found = FALSE;
    
    gbuf[sizeof(gbuf)-1] = '\0';
    strncpy(gbuf, buf, sizeof(gbuf)-1);
    name = strtok(gbuf," \t\n");
    if (name) {
        if (sym_lookupname(name,&n))
            printf("  %s = 0x%08x\n",n->name,n->val);
        else
            printf("Unknown\n");
    } else {
        for (i=0; i<SYMHASHSIZE; i++) {
            n = symbyname[i].nnext;
            while (n) {
                printf("  %s = 0x%08x\n",n->name,n->val);
                n = n->nnext;
                found = TRUE;
            }
        }
        if (!found) printf("Symbol table is empty\n");
    }
    return SCPE_OK;
}

static t_stat symtrace_cmd(int32 arg, CONST char* buf)
{
    if (!*buf)
        symtrace = arg ? TRUE : FALSE;

    printf("Symbolic tracing %sabled\n",symtrace ? "en" : "dis");
    return SCPE_OK;
}

static void putascii(uint32* buf)
{
    int i;
    putchar('|');
    for (i=0; i<16; i++) {
        if (isprint(buf[i])) putchar(buf[i]);
        else putchar('.');
    }
    putchar('|');
}

static t_stat hdump_cmd(int32 arg, CONST char* buf)
{
    int i;
    t_addr low, high, base, top;
    char gbuf[2*CBUFSIZE];
    char *token;
    uint32 byte[16];
    t_bool ascii = FALSE;
    t_bool first = TRUE;
    
    if (buf[0]=='-' && buf[1]=='a') {
        ascii = TRUE;
        buf += 2;
        while (*buf && isspace(*buf)) buf++;
    }
    memset(byte,0,sizeof(uint32)*16);
    
    gbuf[sizeof(gbuf)-1] = '\0';
    strncpy(gbuf, buf, sizeof(gbuf)-1);
    token = strtok(gbuf,"- \t\n");
    if (!token) return SCPE_2FARG;
    low = strtol(token,0,16);
    token = strtok(NULL,"- \t\n");
    if (!token) return SCPE_2FARG;
    high = strtol(token,0,16);
    
    base = low - (low % 16);
    top = (high + 15) - ((high+15) % 16);
    for (; base<top; base++) {
        if ((base % 16)==0) {
            if (!first && ascii) putascii(byte);
            printf("\n%08x: ",base);
            first = FALSE;
        }
        if (base < low) printf("   ");
        else if (base > high) printf("   ");
        else {
            i = base %16;
            if (ReadPB(base,byte+i) != SCPE_OK) printf("?? ");
            else printf("%02x ",byte[i] & 0xff);
        }
    }
    if (!first && ascii) putascii(byte);
    putchar('\n');
    return SCPE_OK;
}

char* m68k_getsym(t_addr val,const char* fmt, char* outbuf)
{
    SYMHASH *v;
    if (symtrace && sym_lookupval(val,&v))
        return v->name;
    else {
        sprintf(outbuf,fmt,val);
        return outbuf;
    }
}

