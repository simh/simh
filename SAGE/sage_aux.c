/* sage_aux.c: serial device for sage-II system

   Copyright (c) 2009, Holger Veit

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

   12-Oct-09    HV      Initial version
*/

#include "sage_defs.h"

static t_stat sageaux_reset(DEVICE* dptr);

UNIT sageaux_unit[] = {
	{ UDATA (NULL, UNIT_FIX | UNIT_BINK, 0) },
	{ UDATA (NULL, UNIT_FIX | UNIT_BINK, 0) },
	{ UDATA (NULL, UNIT_FIX | UNIT_BINK, 0) },
	{ UDATA (NULL, UNIT_FIX | UNIT_BINK, 0) }
};

REG sageaux_reg[] = {
	{ NULL }
};

static MTAB sageaux_mod[] = {
	{ 0 }
};

DEVICE sageaux_dev = {
	"AUX", sageaux_unit, sageaux_reg, sageaux_mod,
	4, 16, 32, 2, 16, 16,
	NULL, NULL, &sageaux_reset,
	NULL, NULL, NULL,
	NULL, DEV_DISABLE|DEV_DIS, 0,
	NULL, NULL, NULL
};

static t_stat sageaux_reset(DEVICE* dptr) 
{
	printf("sageaux_reset\n");
	return SCPE_OK;
}

