/* sage_sys.c: SYS definitions for sage-II system

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

   04-Oct-09    HV      Initial version
*/

#include <ctype.h>
#include "sage_defs.h"

extern DEVICE sagecpu_dev;
extern DEVICE sagepic_dev;
extern DEVICE sagetimer1_dev;
extern DEVICE sagetimer2_dev;
extern DEVICE sagedip_dev;
extern DEVICE sagefd_dev;
extern DEVICE sagecons_dev;
extern DEVICE sagesio_dev;
extern DEVICE sagelp_dev;
#if 0
extern DEVICE sageieee_dev;
#endif
#ifdef SAGE_IV
extern DEVICE sagehd_dev;
extern DEVICE sageaux_dev;
#endif

char    sim_name[] = "Sage-II/IV 68k";

REG     *sim_PC = &m68kcpu_reg[18];
int     sim_emax = SIM_EMAX;
DEVICE  *sim_devices[] = {
        &sagecpu_dev,
        &sagepic_dev,
        &sagetimer1_dev,
        &sagetimer2_dev,
        &sagedip_dev,
        &sagefd_dev,
        &sagecons_dev,
        &sagesio_dev,
        &sagelp_dev,
#if 0
        &sageieee_dev,
#endif
#ifdef SAGE_IV
        &sagehd_dev,
        &sageaux_dev,
#endif
        NULL
};
