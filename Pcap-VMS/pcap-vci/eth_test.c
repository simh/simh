
#include <ctype>                        /* Character type classification macros/routines */ 
#include <descrip>                      /* For VMS descriptor manipulation */ 
#include <iodef>                        /* I/O function code definitions */ 
#include <ssdef>                        /* System service return status code definitions */ 
#include <starlet>                      /* System library routine prototypes */ 
#include <stdio>                        /* ANSI C Standard Input/Output */ 
#include <stdlib>                       /* General utilities */ 
#include <string>                       /* String handling */ 
#include <stsdef>                       /* VMS status code definitions */ 
#include "nmadef.h"			/* NMA stuff */
 
#define $SUCCESS(status) (((status) & STS$M_SUCCESS) == SS$_NORMAL) 
#define $FAIL(status) (((status) & STS$M_SUCCESS) != SS$_NORMAL) 
 
#pragma nomember_alignment 
 
struct parm_802e 
{ 
    short pcli_fmt;                     /* Format - 802E */ 
    int fmt_value; 
    short pcli_bfn;
    int bnf_value;
//    short pcli_acc;
 //   int acc_value;
    short pcli_prm;
    int prm_value;
    short pcli_pid;                     /* Protocol ID - 08-00-2B-90-00 */ 
    short pid_length; 
    unsigned char pid_value[5]; 
} setparm_802e = {
    NMA$C_PCLI_FMT, 
    NMA$C_LINFM_802E,
    NMA$C_PCLI_BFN,
    255,
//    NMA$C_PCLI_ACC,
//    NMA$C_ACC_SHR,
    NMA$C_PCLI_PRM,
    NMA$C_STATE_ON,
    NMA$C_PCLI_PID, 5, 8,0,0x2b,0x80,0x00
}; 
 
typedef struct _hdr {
    unsigned char dsap;
    unsigned char ssap;
    unsigned char ctl;
    unsigned char pid[5];
    unsigned char da[6];
    unsigned char sa[6];
    unsigned char pty[2];
} hdr, *hdrPtr;

struct setparmdsc 
{ 
    int parm_len; 
    void *parm_buffer; 
}; 
 
struct setparmdsc setparmdsc_loop = {sizeof(setparm_802e),&setparm_802e}; 
 
struct p5_param                         /* P5 Receive header buffer */ 
{ 
    unsigned char da[6]; 
    unsigned char sa[6]; 
    char misc[20]; 
}; 
 
struct iosb                             /* IOSB structure */ 
{ 
    short w_err;                        /* Completion Status */ 
    short w_xfer_size;                  /* Transfer Size */ 
    short w_addl;                       /* Additional status */ 
    short w_misc;                       /* Miscellaneous */ 
}; 
 
struct ascid                            /* Device descriptor for assign */ 
{ 
    short w_len; 
    short w_info; 
    char *a_string; 
} devdsc = {4,0,"EWA0"}; 
 
struct iosb qio_iosb;                   /* IOSB structure */ 
struct p5_param rcv_param;              /* Receive header structure */ 
struct p5_param xmt_param={             /* Transmit header structure */ 
  0xCF,0,0,0,0,0};                      /* Loopback Assistant Multicast Address */ 
char rcv_buffer[2048];                   /* Receive buffer */ 
char xmt_buffer[20]={                   /* Transmit buffer */ 
  0,0,                                  /* Skip count */ 
  2,0,                                  /* Forward request */ 
  0,0,0,0,0,0,                          /* Forward address */ 
  1,0,                                  /* Reply request */ 
  0,0}; 
 
char sense_buffer[512];                 /* Sensemode buffer */ 
 
struct setparmdsc sensedsc_loop = {sizeof(sense_buffer),&sense_buffer}; 
 
/* 
** Helper routines
*/
char *add_int_value(char *buf, short code, int value)
{
    char *tmpptr = buf;

    *tmpptr = code;
    *tmpptr += 2;
    *tmpptr = value;
    *tmpptr += 4;
    return tmpptr;
}

char *add_counted_value(char *buf, short code, short len, char *value)
{
    char *tmpptr = buf;

    *tmpptr = code;
    *tmpptr += 2;
    *tmpptr = len;
    *tmpptr += 2;
    memcpy(tmpptr,value,len);
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

/* 
 * MAIN 
 */ 
 
main(int argc, char *argv[]) 
{ 
    int i, j;                           /* Scratch */ 
    int chan;                           /* Channel assigned */ 
    int status;                         /* Return status */ 
    char phyaddr[6];
    int val; 
    hdrPtr r_head;

    /* 
     * Start a channel. 
     */ 
 
    status = sys$assign(&devdsc,&chan,0,0); 
    if ($FAIL(status)) exit(status); 

    status = sys$qiow(0,chan,IO$_SETMODE|IO$M_CTRL|IO$M_STARTUP,&qio_iosb,0,0,0, 
                      &setparmdsc_loop,0,0,0,0); 
    if ($SUCCESS(status)) status = qio_iosb.w_err; 
    if ($FAIL(status)) 
    { 
        printf("IOSB addl status = %04X %04X (on startup)\n",qio_iosb.w_addl,qio_iosb.w_misc); 
        exit(status); 
    } 
 
    /* 
     * Issue the SENSEMODE QIO to get our physical address for the loopback message. 
     */ 
 
    status = sys$qiow(0,chan,IO$_SENSEMODE|IO$M_CTRL,&qio_iosb,0,0,0, 
                      &sensedsc_loop,0,0,0,0); 
    if ($SUCCESS(status)) status = qio_iosb.w_err; 
    if ($FAIL(status)) 
    { 
        printf("IOSB addl status = %04X %04X (on sensemode)\n", 
                                   qio_iosb.w_addl,qio_iosb.w_misc); 
        exit(status); 
    } 
 
    status = find_value(qio_iosb.w_xfer_size, sense_buffer, 
			NMA$C_PCLI_PHA, phyaddr);
    /* 
     * Locate the PHA parameter in the SENSEMODE buffer and copy it into the 
     * LOOPBACK transmit message.  The PHA parameter is a string parameter. 
     */ 
 
    j = 0; 
    while (j < sizeof(sense_buffer)) 
    { 
        i = (sense_buffer[j] + (sense_buffer[j+1]<<8)); 
        if (0x1000 & i) 
        { 
            if ((i & 0xFFF) == NMA$C_PCLI_PHA) 
            { 
                memcpy(&xmt_buffer[4],&sense_buffer[j+4],6); 
                break; 
            } 
            j += (sense_buffer[j+2] + (sense_buffer[j+3]<<8)) + 4; 
        } 
        else 
        { 
            j += 6;                     /* Skip over longword parameter */ 
        } 
    } 
 
    /* 
     * Transmit the loopback message. 
     */ 
 
    status = sys$qiow(0,chan,IO$_WRITEVBLK,&qio_iosb,0,0,&xmt_buffer[0], 
             sizeof(xmt_buffer),0,0,&xmt_param,0); 
    if ($SUCCESS(status)) status = qio_iosb.w_err; 
    if ($FAIL(status)) 
    { 
        printf("IOSB addl status = %04X %04X (on transmit)\n", 
                                   qio_iosb.w_addl,qio_iosb.w_misc); 
        exit(status); 
    } 
 
    /* 
     * Look for a response.  We use the NOW function modifier on the READ so that 
     * we don't hang here waiting forever if there is no response.  If there is no 
     * response in 1000 receive attempts, we declare no response status. 
     */ 
 
    for (i=0;i<1000;i++) 
    { 
//        status = sys$qiow(0,chan,IO$_READVBLK|IO$M_NOW,&qio_iosb,0,0,&rcv_buffer[0], 
        status = sys$qiow(0,chan,IO$_READVBLK,&qio_iosb,0,0,&rcv_buffer[0], 
                 sizeof(rcv_buffer),0,0,&rcv_param,0); 
        if ($SUCCESS(status)) status = qio_iosb.w_err; 
        if ($SUCCESS(status)) {
	    
	    printf("da %02x%02x%02x%02x%02x%02x, sa %02x%02x%02x%02x%02x%02x\n",
		rcv_param.da[0],
		rcv_param.da[1],
		rcv_param.da[2],
		rcv_param.da[3],
		rcv_param.da[4],
		rcv_param.da[5],
		rcv_param.sa[0],
		rcv_param.sa[1],
		rcv_param.sa[2],
		rcv_param.sa[3],
		rcv_param.sa[4],
		rcv_param.sa[5]);
	} else {
	    printf("Error\n");
	    exit(status);
	    break;
	}
    } 
    if ($SUCCESS(status)) 
        printf("Successful test\n"); 
    else 
        printf("No response\n"); 
 
} 
 
