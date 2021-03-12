/* pdp11_m9312.h: M9312 bootrom data

   Copyright (c) 2021, Jos Fries

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

  Modification history:

  12-Jan-21  GAF  Initial version

*/

#ifndef PDP11_ROM_H
#define PDP11_ROM_H

#include "pdp11_defs.h"

typedef struct
{
	t_addr base_address;				/* ROM code base address */
	int16 size;							/* Address space size */
	uint16 (*rom_list)[];				/* ROMs available for this socket */
}
rom_socket;


typedef struct
{
	const char *name;					/* Module name */
	rom_socket (*socket)[];				/* Sockets for this module */
}
module;


#endif PDP11_ROM_H

