/* s100_rom.h

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

#ifndef _S100_ROM_H
#define _S100_ROM_H

#include "sim_defs.h"

#define UNIT_ROM_V_VERBOSE      (UNIT_V_UF+0)               /* warn if ROM is written to                    */
#define UNIT_ROM_VERBOSE        (1 << UNIT_ROM_V_VERBOSE)
#define UNIT_ROM_V_DBL          (UNIT_V_UF+1)               /* Enable/Disable Disk Boot Loader              */
#define UNIT_ROM_DBL            (1 << UNIT_ROM_V_DBL    )
#define UNIT_ROM_V_HDSK         (UNIT_V_UF+2)               /* Enable/Disable Hard Disk Boot Loader         */
#define UNIT_ROM_HDSK           (1 << UNIT_ROM_V_HDSK   )
#define UNIT_ROM_V_ALTMON       (UNIT_V_UF+3)               /* Enable/Disable Altmon                        */
#define UNIT_ROM_ALTMON         (1 << UNIT_ROM_V_ALTMON )
#define UNIT_ROM_V_TURMON       (UNIT_V_UF+4)               /* Enable/Disable Turnkey Monitor               */
#define UNIT_ROM_TURMON         (1 << UNIT_ROM_V_TURMON )
#define UNIT_ROM_V_CDBL         (UNIT_V_UF+5)               /* Enable/Disable Combined Disk Boot Loader     */
#define UNIT_ROM_CDBL           (1 << UNIT_ROM_V_CDBL)
#define UNIT_ROM_V_AZ80DBL      (UNIT_V_UF+6)               /* Enable/Disable AltairZ80 Disk Boot Loader    */
#define UNIT_ROM_AZ80DBL        (1 << UNIT_ROM_V_AZ80DBL)

typedef struct {
    uint32 flag;
    int32 *rom;
    int32 baseaddr;
    int32 size;
    char *name;
    char *desc;
} ROM;

#endif

