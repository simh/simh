/* i7094_binloader.c: IBM 7094 simulator interface

   Copyright (c) 2008, David G. Pitts

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
*/
/***********************************************************************
*
* binloader.h - IBM 7090 emulator binary loader header.
*
* Changes:
*   10/20/03   DGP   Original.
*   12/28/04   DGP   Changed for new object formats.
*
***********************************************************************/

#define IBSYSSYM        '$'         /* Marks end of object file */
#define WORDPERREC      5           /* Object words per record */
#define LOADADDR        0200        /* Default load address */
#define OBJRECLEN       80          /* Object record length */
#define CHARWORD        12          /* Characters per word */

/*
** Object tags
*/

#define IDT_TAG         '0'         /* 0SSSSSS0LLLLL */
#define ABSENTRY_TAG    '1'         /* 10000000AAAAA */
#define RELENTRY_TAG    '2'         /* 20000000RRRRR */
#define ABSEXTRN_TAG    '3'         /* 3SSSSSS0AAAAA */
#define RELEXTRN_TAG    '4'         /* 4SSSSSS0RRRRR */
#define ABSGLOBAL_TAG   '5'         /* 5SSSSSS0AAAAA */
#define RELGLOBAL_TAG   '6'         /* 6SSSSSS0RRRRR */
#define ABSORG_TAG      '7'         /* 70000000AAAAA */
#define RELORG_TAG      '8'         /* 80000000RRRRR */
#define ABSDATA_TAG     '9'         /* 9AAAAAAAAAAAA */
#define RELADDR_TAG     'A'         /* AAAAAAAARRRRR */
#define RELDECR_TAG     'B'         /* BARRRRRAAAAAA */
#define RELBOTH_TAG     'C'         /* CARRRRRARRRRR */
#define BSS_TAG         'D'         /* D0000000PPPPP */
#define ABSXFER_TAG     'E'         /* E0000000RRRRR */
#define RELXFER_TAG     'F'         /* F0000000RRRRR */
#define EVEN_TAG        'G'         /* G0000000RRRRR */
#define FAPCOMMON_TAG   'H'         /* H0000000AAAAA */

/* Where:
 *    SSSSSS - Symbol
 *    LLLLLL - Length of module
 *    AAAAAA - Absolute field
 *    RRRRRR - Relocatable field
 *    PPPPPP - PC offset field
*/

/***********************************************************************
*
* binloader.c - IBM 7090 emulator binary loader routines for ASM7090
*            and LNK7090 object files.
*
* Changes:
*   10/20/03   DGP   Original.
*   12/28/04   DGP   Changed for new object formats.
*   02/14/05   DGP   Detect IBSYSSYM for EOF.
*   06/09/06   DGP   Make simh callable.
*   
***********************************************************************/

#include "i7094_defs.h"

extern t_uint64 *M;
extern uint32 PC;

t_stat
binloader (FILE *fd, const char *file, int loadpt)
{
#ifdef DEBUGLOADER
   FILE *lfd;
#endif
   int transfer = FALSE;
   int loadaddr = LOADADDR;
   int curraddr = LOADADDR;
   char inbuf[OBJRECLEN+2];

#ifdef DEBUGLOADER
   lfd = fopen ("load.log", "w");
   fprintf (lfd, "binloader: file = '%s', loadpt = %d\n", file, loadpt);
#endif

   if (loadpt > 0)
   {
      loadaddr = loadpt;
      curraddr = loadpt;
   }

   while (fgets (inbuf, sizeof(inbuf), fd))
   {
      char *op = inbuf;
      int i;

      if (*op == IBSYSSYM)                  /* End of object marker */
         break;

      for (i = 0; i < WORDPERREC; i++)
      {
        char otag;
        char item[16];
        t_uint64 ldata;

        otag = *op++;
        if (otag == ' ')
        break;
        strncpy (item, op, CHARWORD);
        item[CHARWORD] = '\0';
        sscanf (item, "%" LL_FMT "o", &ldata);

#ifdef DEBUGLOADER
        fprintf (lfd, "loadaddr = %05o, curraddr = %05o\n",
                      loadaddr, curraddr);
        fprintf (lfd, "   otag = %c, item = %s\n", otag, item);
        fprintf (lfd, "   ldata = %12.12o\n", ldata);
#endif

        switch (otag)
        {
        case IDT_TAG:
            break;

        case ABSORG_TAG:
            curraddr = loadaddr = (int32) ldata & AMASK;
            break;

        case RELORG_TAG:
            curraddr = (int32) (ldata + loadaddr) & AMASK;
            break;

        case BSS_TAG:
            curraddr = (int32) (curraddr + ldata) & AMASK;
            break;

        case RELBOTH_TAG:
            ldata = ldata + loadaddr + (loadaddr << INST_V_DEC);
            goto STORE;

        case RELDECR_TAG:
            ldata = ldata + (loadaddr << INST_V_DEC);
            goto STORE;

        case RELADDR_TAG:
            ldata = ldata + loadaddr;

        case ABSDATA_TAG:
        STORE:
#ifdef DEBUGLOADER
            fprintf (lfd, "   M[%05o] = %12.12o\n", curraddr, ldata);
#endif
            M[curraddr] = ldata & DMASK;
            curraddr++;
            break;

        case ABSXFER_TAG:
            transfer = TRUE;
        case ABSENTRY_TAG:
            PC = (uint32) ldata & AMASK;
#ifdef DEBUGLOADER
            fprintf (lfd, "   PC = %05o\n", PC);
#endif
            if (transfer)
            goto GOSTART;
            break;

        case RELXFER_TAG:
            transfer = TRUE;
        case RELENTRY_TAG:
            ldata = (ldata + loadaddr) & AMASK;
            PC = (uint32) ldata & AMASK;
#ifdef DEBUGLOADER
            fprintf (lfd, "   PC = %05o\n", PC);
#endif
            if (transfer)
            goto GOSTART;
            break;

        default: ;
        }
        op += CHARWORD;
      }
   }

GOSTART:
#ifdef DEBUGLOADER
   fclose (lfd);
#endif

   return SCPE_OK;
}
