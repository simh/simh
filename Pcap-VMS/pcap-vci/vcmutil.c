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


