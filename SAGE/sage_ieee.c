/* sage_ieee.c: IEEE 488 device for Sage-II-system

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

   12-Oct-09    HV      Initial version
*/

/* N O T  Y E T  I M P L E M E N T E D ! ! ! */
#include "sage_defs.h"

#if 0
static t_stat sageieee_reset(DEVICE* dptr);

UNIT sageieee_unit = {
	UDATA (NULL, UNIT_FIX | UNIT_BINK, 0)
};

REG sageieee_reg[] = {
	{ NULL }
};

static MTAB sageieee_mod[] = {
	{ 0 }
};

DEVICE sageieee_dev = {
	"IEEE", &sageieee_unit, sageieee_reg, sageieee_mod,
	1, 16, 32, 2, 16, 16,
	NULL, NULL, &sageieee_reset,
	NULL, NULL, NULL,
	NULL, DEV_DISABLE|DEV_DIS, 0,
	NULL, NULL, NULL
};

static t_stat sageieee_reset(DEVICE* dptr) 
{
	printf("sageieee_reset\n");
	return SCPE_OK;
}

#endif
