/* vcmutil.c - VCM helper routines

   Copyright (c) 2003, Anders "ankan" Ahgren

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/
//***************************************************************************
//
//
// FACILITY:
//
//      Dynamic Loadable Execlet for PCAP
//
//
// ABSTRACT:
//
//	This module contains helper routines for the PCAP VCM.
//
// AUTHOR:
//
//	Ankan
//
//  CREATION DATE:  21-Mar-2003
//
//  DESIGN ISSUES:
//
//      {@tbs@}
//
// REVISION HISTORY:
//
//      X-1     Ankan			Anders Ahgren		21-Mar-2003
//              Initial version.
//
#include <string.h>
#include <stdlib.h>
#include "pcapvcm.h"

void add_lil_item(LILDEF *lil, int len, int tag, char *value)
{
    LILITEM *lilitm;

    lilitm = (LILITEM *)lil->lil$a_listadr + lil->lil$l_listlen;
    lilitm->len = len + 4;  // Includes len and tag!
    lilitm->tag = tag;
    memcpy((char *)&lilitm->val, value, len);
    lil->lil$l_listlen = len + 4; // 4 is len+tag
}

void add_lil_addr_value(LILDEF *lil, int len, int tag, char *value)
{
    LILITEM *lilitm;
    char **foo;
    char *tmp;

    lilitm = (LILITEM *) lil->lil$a_listadr + lil->lil$l_listlen;
    lilitm->len = len + 4; // Includes len and tag!
    lilitm->tag = tag;
    foo = (char **) &lilitm->val;
    *foo = (char *) &lilitm->val + sizeof(char *);
    tmp = *foo;
    memcpy(tmp, value, len);
    lil->lil$l_listlen = len + 4 + sizeof(char *); // 4 is len+tag
}

/* 
** Ethernet device setup helper routines
*/
char *add_int_value(char *buf, short code, int value)
{
    char *tmpptr = buf;
    short *sptr;
    int *iptr;

    sptr = (short *)tmpptr;
    *sptr = (short) code;
    tmpptr += 2;
    iptr = (int *) tmpptr;
    *iptr = 0;
    *iptr = (int) value;
    tmpptr += 4;
    return tmpptr;
}


char *add_counted_value(char *buf, short code, short len, char *value)
{
    char *tmpptr = buf;
    short *sptr;
    
    sptr = (short *)tmpptr;
    *sptr = (short) code;
    tmpptr += 2;
    sptr = (short *) tmpptr;
    *sptr = (short) len;
    tmpptr += 2;
    memcpy(tmpptr,value,len);
    tmpptr += len;
    return tmpptr;
}


int find_value(int buflen, char *buf, short code, char *retbuf)
{
    int i = 0;
    int item;
    char *tmpbuf = buf;
    int value;
    int status = 0;

    while (i < buflen) {
	item = (tmpbuf[i] + (tmpbuf[i+1]<<8));
	if (0x1000 & item) {
	    if ((item & 0xFFF) == code) {
		memcpy(retbuf, &tmpbuf[i+4],6);
		status = 1;
		break;
	    }
	    i += (tmpbuf[i+2] + (tmpbuf[i+3]<<8)) + 4;
	} else {
	    // A value, ours?
	    if ((item & 0xFFF) == code) {
		// Yep, return it
		memcpy(retbuf, &tmpbuf[i+2], 4);
		status = 1;
		break;
	    }
	    i += 6;
	}
    }
    return status;
}

