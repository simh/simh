#module SSUBS X


**

**  COPYRIGHT (c) 1993, 1994, 1995, 1997 
**  DIGITAL EQUIPMENT CORPORATION, MAYNARD, MASSACHUSETT
**  ALL RIGHTS RESERVE

**  THIS SOFTWARE IS FURNISHED UNDER A LICENSE AND MAY BE USED AND COPI
**  ONLY  IN  ACCORDANCE  OF  THE  TERMS  OF  SUCH  LICENSE  AND WITH T
**  INCLUSION OF THE ABOVE COPYRIGHT NOTICE. THIS SOFTWARE OR  ANY  OTH
**  COPIES THEREOF MAY NOT BE PROVIDED OR OTHERWISE MADE AVAILABLE TO A
**  OTHER PERSON.  NO TITLE TO AND  OWNERSHIP OF THE  SOFTWARE IS  HERE
**  TRANSFERRE

**  THE INFORMATION IN THIS SOFTWARE IS  SUBJECT TO CHANGE WITHOUT NOTI
**  AND  SHOULD  NOT  BE  CONSTRUED  AS A COMMITMENT BY DIGITAL EQUIPME
**  CORPORATIO

**  DIGITAL ASSUMES NO RESPONSIBILITY FOR THE USE  OR  RELIABILITY OF I
**  SOFTWARE ON EQUIPMENT WHICH IS NOT SUPPLIED BY DIGITA
**


                               

**
**  FACILIT

**      DECamds - DEC Availability Manager for Distrubuted Syste

**  ABSTRAC

**	This module contains a number of utility routines used by the consol
**	the comm process, and RMCP. 

**	NOTE WELL: Don't use any "global" structures... Make everything pass
**		   to the routine; otherwise, the headache of "special
**		   compilation will come into play.

**  MODIFICATION HISTOR
**
**	NOTE: This module is a conversion of routines found in SSUBS.B3
**		and CLIENT.B32.  There are edit histories there

**	X-18	SFW		Steve Wride			26-JUN-20
**              Added device EIA0 to the device list

**	X-17	TGC		Tom Carr			03-JUN-19
**		In preparation for the switch to V5.7 of the DECC compil
**		which does alot more code checking, change the return ty
**		for the routine return type from int to void, since nothi
**		was ever returne

**      X-16    BWK             Barry Kierstein                 19-Dec-19
**              Added devices FAA0, FRA0, FWA0, ERA0 and EWA0 to the devi
**              table list, and made the while statement that processes t
**              list a bit cleare

**      X-15    BWK             Barry Kierstein                 18-Dec-19
**                  When modifications were made to access the lock manager 
**              S2 space, a day-one bug was found.  Turns out that in WINDOW.
**              the routine set_window_title was inserting a '\0' in the by
**              just outside the title buffer.  This now was the low byte 
**              the return address of the function, so that the return fr
**              AMDS$StartNodeWindow in routine DisplayActivateAction d
**              not resume program execution where expected, but in the ca
**              above that starts the single disk windo
**                  This took a LONG time to find, and in an effort not to 
**              bitten by overrunning sprintf statements, double the leng
**              of each temporary buffer that sprintf uses as well as f
**              the error in setting the byte just outside the title buffe
**              Not the greatest solution, but the easiest to do witho
**              a major examination of each buffe
**                  Ironically, trying to fix the buffer overrun by terminati
**              the title string with a '\0' is really not fixing the probl
**              as it ignores what other bytes have been run over by the sprin
**              in the first plac
**                  Each buffer that has been doubled in size was done so 
**              adding "2*" to the dimension (e.g. 64 -> 2*64), so that t
**              changes are easy to find.  For the length of the title buffer
**              a new constant was introduced so that the string truncati
**              statements would work on the intended byt

**	X-14	JAF0956		John Ferlan			17-Feb-19
**		Fix trnlnm itemlists to be initialized and after the trnl
**		call to make the string returned end with a 0....  trnl
**		doesn't do that for you... was setting the amdscomm timeo
**		interval to very large interval

**	X-13	JAF0945		John Ferlan			20-Jan-19
**		Modify printf's for memory allocation - also during debu
**		period add check if our allocation will be too large f
**		the "bigmem" or "biggermem" flags... would we be 'truncatin
**		and masking i

**	X-12	JAF0921		John Ferlan			29-Nov-19
**		Make access to mem_list queues (insque/remque) be synch
**		Also add a routine (mem_dump) to help debug memory leak

**	X-11	JAF0859		John Ferlan			12-Sep-19
**		Make some changes based on feedback from McCabe's tool
**		Also, add code to get system page size and place that val
**		in the page_size field (rather than the previous hardcoded 51

**	X-10	JAF0817		John Ferlan			01-Jul-19
**		On mem_free force min block size of 12!  If not, then pri
**		an error and punt/return.  We've overwritten somethin

**	X-9	JAF0813		John Ferlan			27-Jun-19
**		In mem_free check for badbloadr as error return from lib$free_
**		If present, don't signal it.. There are just too many combi
**		ations that could cause the error to worry about... We'l
**		keep the extra memory (or if we've already deleted, th
**		it doesn't matter anyways!

**	X-8	JAF0793		John Ferlan			03-Jun-19
**		Change the way the global section is used.  Make sure th
**		data within the buffer is aligned on longword or quadwo
**		boundaries based on architecture type (at build time).  Th
**		removes the need for some fields.

**	X-7	JAF0780		John Ferlan			26-May-19
**		Forgot to add code to copy good device name in translati
**		of amds$device logica

**	X-6	JAF0774		John Ferlan			09-May-19
**		Trap some memory bugs and ignore a couple others.

**	X-5	JAF0764		John Ferlan			02-May-19
**		Fix bug in get_dl_chan, where return status was not returne

**	X-4	JAF0757		John Ferlan			20-Apr-19
**		Changes for VCI...  Split out P2 buffer fill routine so th
**		RMCP can call it separately... Modify channel startup t
**		remove any RMCP dependancies... such as print out routine

**		JAF0758		John Ferlan			20-Apr-19
**		Changes for AXP console...  The $CRMPSC service requires t
**		page size for AXP's to be a pagelet not a page...  So don
**		need to create/map the file based on different page sizes.


**	X-3	JAF0747		John Ferlan			09-Feb-19
**		Change the term ethernet to LAN, where appropriatee... Al
**		references to hardware and physical addressing, so tha
**		hardware and physical refer to the 08-00-2b and use DECn
**		instead of what was physical.

**	X-2	JAF0715		John Ferlan			17-Nov-19
**		Make DLLIST and MEM_LIST be static structur
**		Only start one channel when use amds$comm process in dl_sta

**	X-1	JAF0708		John Ferlan			10-Nov-19
**		New module, a bliss to c port of SSUBS.B32 and CLIENT.B32. 
**		With some changes made for continuous improvemen

**		Added two new adapters to the list.. ICA0 and IRA
**





**  INCLUDE FIL




 * This should be the only include file.  All other inclusions are do
 * within the context of this fi
 
#include "src$:sprcdecls.h
#include "src$:nmadef.h"		/*  NMA definitions 



**  TYPEDE






**  TABLE OF CONTEN



 * Shared global section handling routin
 
extern int	util$alloc_circ_buffer (
extern int 	util$circ_bytes_used (
extern int 	util$circ_bytes_free (
extern int 	util$put_circ (
extern int 	util$get_circ (

extern int AMDS$if_true (
extern int AMDS$lnm_getint_value (

static int get_dl_chan ();		/* Open channel to the datalink 
extern void AMDS$Fill_P2 ();		/* Fills the P2 desc with data 
extern int AMDS$Check_amdsdevice_log();	/* Checks AMDS$DEVICE logical valid 
extern int AMDS$startup_dl ();		/* Start the datalink channel... 
extern void report_we_are_lost (

extern void	init_mem_queues ();	/* Initialize the memory queues 
static void	list_mem_queues ();	/* Debug routine 
extern int	mem_alloc (
extern void	mem_free (



**  EXTERNAL REFERENC


extern CSDB *console_block




**  MACRO DEFINITIO


#define MAX_CHANS 9	/* We'll support up to 8, with the ninth entry null 

#define MIN_BLOCK_SIZE AMEM$K_LENGTH + 12    /* 12 is fl/bl/siz/typ/subtyp 
#define MAX_BLOCK_SIZE 4747		     /* A guess... 



 * The following constants are used for alignment in the global section buffe

 * For VAX we use longword alignment, since it's a performance enhance

 * For AXP we use quadword alignment, since it's part of the architectur
 
 * Also on AXP systems, we've had read/write ordering problems between th
 * two processes (console and AMDS$COMM
 
#if defined(VAX) || defined(__VAX) || defined (vax) || defined (__va
#define CBF_ADD	
#define CBF_ALIGN	0xFFFFFFF
#define CBF_MIN		4	/* Minimum bytes that have to be there... 
#el
#define CBF_ADD	
#define CBF_ALIGN	0xFFFFFFF
#define CBF_MIN		8	/* Minimum bytes that have to be there... 
#end


**  DECLARATIO


static int virpagcn
static int page_siz
static int acount = 
static int dcount = 
#if DEB
static int lastallocaddr = 
#end
struct MEMLI

    int	mem_q_f
    int	mem_q_b
    int	mem_q_siz
    int	mem_q_le
    int mem_q_ma
    int mem_q_loc
};         
static struct MEMLIST mem_list[ AMEM$K_INVALID ] = {       
		0, 0, AMEM$K_SML_Q_SIZ, 0, AMEM$K_SML_Q_MAX, 
		0, 0, AMEM$K_MED_Q_SIZ, 0, AMEM$K_MED_Q_MAX, 
		0, 0, AMEM$K_LGE_Q_SIZ, 0, AMEM$K_LGE_Q_MAX, 
		0, 0, AMEM$K_XLG_Q_SIZ, 0, AMEM$K_XLG_Q_MAX, 
		0, 0, AMEM$K_XXL_Q_SIZ, 0, AMEM$K_XXL_Q_MAX, 
		0, 0, AMEM$K_ECM_Q_SIZ, 0, AMEM$K_ECM_Q_MAX, 
		0, 0, AMEM$K_HUG_Q_SIZ, 0, AMEM$K_HUG_Q_MAX, 
		

static int AMDS$gblsec_sysgbl = 
static int AMDS$gblsec_group = 
static  char errstr[132]; /* module level error string 
                              
struct DLLIS

    int	devlist_inde
    int	dl_cha
};         

static int	rm_active = 0;		/* Boolean for using RMA0 as channel 
static int 	dl_chan_count = 0;	/* Number of datalink channels...
static struct	DLLIST	dl_chanlist [MAX_CHANS] = {/* Initialize our channel 
			0, 0,		/* list. This is basically an array 
			0, 0,		/* of all available datalink 
			0, 0,		/* channels...  Some systems 
			0, 0,		/* support up to 8 adaptors of 
			0, 0,		/* one kind...  Not sure if that 
			0, 0,		/* means 8 DEBNI plus 8 DEMNA plus 
			0, 0,		/* 8 DEMFA... But if we have more 
			0, 0,		/* 8 we'll punt anyways... 
			0, 0



 * Current list of supported devices.  Ensure that the " " is the la
 * 'device' in the list, as it is the marker to end the list.. I.e. if 
 * get here, they're aren't anymore LAN devices to connect t
 */                            
static char	*devlist [] = { "RMA0",		/* Ourself? only when comm 
				"AMDS$DEVICE",	/* Logical name 
				"FXA0",		/* FDDI 
				"FCA0
				"ECA0",		/* Turbochannel to LAN 
				"ICA0",		/* Turbochannel to Token Ring 
				"IRA0",		/* EISA to Token Ring 
				"XEA0",		/* All below are Ethernet 
				"XQA0
				"EFA0
				"ETA0
				"ESA0
				"EXA0
				"EZA0
                                "FAA0
                                "FRA0
                                "FWA0
                                "ERA0
                                "EWA0
                                "EIA0
				" "


extern int util$alloc_circ_buffer ( int size, int *bufaddr
				    struct dsc$descriptor *name

**

** FUNCTIONAL DESCRIPTI

**	This routine will allocate a circular buffer for send and the consol
**	to use when communicating.  This buffer will be a global sectio

** FORMAL PARAMETE

**   size is the byte size of the data buffer to allocate, and should 
**        at least as large as the largest data message + 1.  Passed b
**	 reference.                         
**                                             
**   buf is the address of a variable to receive the address of the fir
** 	byte in the buffer (low byte of the SIZE longword

**   name (optional) is a global section name passed by descriptor.  
**	supplied, the circular buffer will be allocated in a global section s
**	named.  At this time this argument is require

** IMPLICIT INPUTS	Non

** IMPLICIT OUTPUTS	Non

** RETURN VAL
**       
**   status is returned stat

** SIDE EFFECTS:	Non

**


    int inadr[2];	/* params to global section routine 
    int retadr[2]

    int numpgs = ( size + CBF$K_HEADER_LENGTH + 512 - 1 ) / 51
	
    CBF *bu

    int gblsec_fl = ( SEC$M_GBL | SEC$M_WRT | SEC$M_PAGFIL 
    int statu

    $DESCRIPTOR ( sysgbl_lnm, "AMDS$GBLSEC_SYSGBL" 


    /
     * Check for which type of global section group or syst
     
    if ( AMDS$if_true ( &sysgbl_lnm )
   
	gblsec_fl |= SEC$M_SYSGB
	AMDS$gblsec_sysgbl = 
   
    el
	AMDS$gblsec_group = 1;	/* default 
                    
    
     * Get some memory for the global secti
     
    status = sys$expreg ( numpgs,  &inadr[0], 0, 0
    if ( status != SS$_NORMAL
	return ( status 
              
    
     * Create the mapped secti
     
    status = sys$crmpsc ( &inadr[0], &retadr[0], PSL$C_USER, gblsec_f
			  name, 0, 0, 0, numpgs, 0, 0, 0 
    buf = ( CBF *) inadr[0
    if ( status == SS$_CREATED
   
	buf->CBF$L_SIZE		= ( numpgs * 512 ) - CBF$K_HEADER_LENGT
	buf->CBF$L_IN		= 
	buf->CBF$L_OUT		= 
   
    *bufaddr = bu
    return ( status 




extern int util$circ_bytes_used ( CBF *buf

**
** FUNCTIONAL DESCRIPTIO
*
**   	Returns number of bytes currently used in a circular buffe
*
**	NOTE: This call is completed regardless of the fact that someon
**		may be currently changing the pointers..  All this ca
**		is designed to do is to return the number of bytes current
**		not in use.  It is up to the call process to figure out i
**		it can do its operatio

** FORMAL PARAMETER
*
**   	buf  -  Pointer to CBF structu
**  
** IMPLICIT INPUTS:     No
*
** IMPLICIT OUTPUTS:	No
*
** ROUTINE VALU
*
**   	Count of bytes Use
*
** SIDE EFFECTS:	No
*
**


    int nbyte
    nbytes = buf->CBF$L_IN - buf->CBF$L_OU
    if ( nbytes >= 0
	return ( nbytes 
    el
	return ( buf->CBF$L_SIZE + nbytes 



extern int util$circ_bytes_free ( CBF *buf

**
** FUNCTIONAL DESCRIPTIO
*
**   	Returns number of bytes currently free in a circular buffe
*
**	NOTE: This call is completed regardless of the fact that someon
**		may be currently changing the pointers..  All this ca
**		is designed to do is to return the number of bytes current
**		not in use.  It is up to the call process to figure out i
**		it can do its operatio

** FORMAL PARAMETER
*
**   	buf  -  Pointer to CBF structu
*
** IMPLICIT INPUTS:     No
*
** IMPLICIT OUTPUTS:	No
*
** ROUTINE VALU
*
**   	Count of bytes FRE
*
** SIDE EFFECTS:	No
*
**


    int nbyte
    nbytes = buf->CBF$L_OUT - buf->CBF$L_I
    if ( nbytes > 0
	return ( nbytes 
    el
	return ( buf->CBF$L_SIZE + nbytes 

                                               

extern int util$put_circ ( int nbyt, CBF *buf, unsigned char *data

**

** FUNCTIONAL DESCRIPTI

**	The PUT process uses this routine to write data to the buffe

**   	If there are at least nbyt+1 bytes in the buffer, UTIL$PUT_CIRC copie
**	them into the buffer and advances the IN pointer appropriately.
**	Normal return with SS$_NORMAL statu

**   	If there is not enough free space in the buffer, UTIL$PUT_CIRC retur
**   	the status code SS$_TOOMUCHDATA and does not change the pointer

**   	When PUT gets NBYT bytes of data to give to GET
**   	PUT checks to see if there is room in the buffer.  This can happen i
**   	eithe
**   	    (P1)  OUT > IN, in which case there are exactly OUT-IN bytes fr

**	    (P2)  OUT <= IN, in which case there are exactly SIZE+OUT-IN byt
**			      fr

**   	It is important that there be MORE THAN NBYT BYTES FREE.  You are no
**	allowed to exactly fill the buffer, since that would result in th
**	pointers being advaced until OUT=IN, which indicates an empty buffe
**	This results in the following alternative

**   	    If there are more than NBYT bytes available, store the NBYT byte
**	    in the buffer and advance IN by NBYT.  It may GET two move oper
**	    tions to store the data.
*
**   	    Do not store anything in IN until all the move operations are don
**	    then store only the correct final value

**   	    If there are NBYT or less bytes available, stall until GET remove
**	    some of the data from the buffe

**   	IN is changed only by the PUT proces

** FORMAL PARAMETE

**   	nbyt	Number of bytes of data to send (passed by reference

**   	buf 	The address of the first byte of the circular buffe

**  	data 	The address of the first byte to be written to the buff

** IMPLICIT INPUTS	Non

** IMPLICIT OUTPUTS	Non

** RETURN VAL
**                              
**	Status (SS$_NORMAL or SS$_TOOMUCHDAT
**                                           
** SIDE EFFECTS:	Non


**
*/                                    

    int fre_bytes, end_byt
    unsigned char *add

    
     * If the number of bytes to put into the gblsec is less than our minim
     * value, return an error... This value is architecture specific dependi
     * on whether or not we're doing long or quad word alignmen
     
    if ( nbyt < CBF_MIN
	return ( SS$_BADPARAM 

    
     * This should never hold true, since we check to make sure we have 
     * least room enough for two ECM's in the global section before ever tryi
     * to write to it, but one never know
    
     * Note: Gotta have the >= because, if exact then IN would equal OU
     *		which would mean there's nothing in ther
     */    
    fre_bytes = util$circ_bytes_free ( buf 
    if ( nbyt >= fre_bytes
   
	return ( SS$_TOOMUCHDATA 
   

    /*               
     * See if one contiguous chunk or two separate chunks.  Since we kn
     * we already have room in the buffer, all we need do is ask if the
     * is space from IN to LIMIT -- we can't overrun OU
     
    if ( ( nbyt + buf->CBF$L_IN ) < buf->CBF$L_SIZE
    {    
	addr = buf->CBF$A_DATA + buf->CBF$L_I
	memcpy ( addr, data, nbyt 

	
	 * Let's figure out where the "next" buffer will appear assuming w
	 * desire longword alignmen
	 
	end_byte = ( nbyt + CBF_ADD ) & CBF_ALIGN
	buf->CBF$L_IN += end_byt
   
    el
   
	int segsiz = buf->CBF$L_SIZE - buf->CBF$L_I
                  
	
	 * Move in two chun
	 
	addr = buf->CBF$A_DATA + buf->CBF$L_I
	memcpy ( addr, data, segsiz 
	addr = buf->CBF$A_DATA;	/* Move pointer 
	memcpy ( addr, &data[segsiz], ( nbyt - segsiz ) 
	
	 * Let's figure out where the "next" buffer will appear assuming w
	 * desire longword alignmen
	 
	end_byte = ( (nbyt-segsiz) + CBF_ADD ) & CBF_ALIGN
	buf->CBF$L_IN = end_byt
   
    return ( SS$_NORMAL 


extern int util$get_circ ( int nbyt, CBF *buf, unsigned char *data

**
**  FUNCTIONAL DESCRIPTIO

**   	If there are at least nbyt bytes of data in the buffer, UTIL$GET_CI
**   	copies them from the buffer and advances the OUT pointer appropriatel
**   	Normal return with SS$_NORMAL statu

**   	If there are not at least nbyt bytes of data in the buffer, retur
**   	the status code SS$_NODATA and do not change the pointer

**   	If the PUT and GET processes are using fixed-size messages th
**   	one call to UTIL$GET_CIRC is all that is required to read a messag
**   	If instead the message size is variable then some convention will 
**   	needed so that GET will be able to determine the number of bytes 
**   	request, lest an abutting message be read as part of one reques
**   	This can be accomplished by various PUT/GET conventions.  For examp
**   	PUT could store a longword of length before the message, and GET cou
**   	make two calls to UTIL$GET_CIRC, first a four-byte "length" reques
**   	then a second "data" request, passing the returned "length" valu

**  FORMAL PARAMETERS:

**   	nbyt	= Number of bytes to be read from the buff

**   	buf 	= The address of the first byte of the circular buffe

**   	data 	= The address of where the buffer contents are to be writt

**  IMPLICIT INPUTS:    no

**  IMPLICIT OUTPUTS:   no

**  RETURNS:          

**	Status. (SS$_NORMAL and SS$_NODATA

**  SIDE EFFECTS:       no
**


    int usd_bytes, end_byt
    unsigned char *add

    
     * If the number of bytes to take out of the gblsec is less than our minim
     * value, return an error... This value is architecture specific dependi
     * on whether or not we're doing long or quad word alignmen
     
    if ( nbyt < CBF_MIN
	return ( SS$_BADPARAM 

    
     * The following should never be true, but we must check
     */       
     usd_bytes = util$circ_bytes_used ( buf 
     if ( usd_bytes < nbyt
	return ( SS$_NODATA 

    
     * If we don't need to "wrap" to the head and we can get in one chunk.
     
    if ( ( buf->CBF$L_SIZE - buf->CBF$L_OUT ) >= nbyt
   
	addr = buf->CBF$A_DATA + buf->CBF$L_OUT
	memcpy ( data, addr, nbyt 

	
	 * Let's figure out where the "next" buffer will appear assuming w
	 * desire longword alignmen
	 
	end_byte = ( nbyt + CBF_ADD ) & CBF_ALIGN
	buf->CBF$L_OUT += end_byte;   
   
    el
   
	int segsiz = buf->CBF$L_SIZE - buf->CBF$L_OU

	
	 * Otherwise, we need to move the data in two chunks, first t
	 * "end" of the buffer, then start at the head again.
	 
	addr = buf->CBF$A_DATA + buf->CBF$L_OUT
	memcpy ( data, addr, segsiz 
	addr = buf->CBF$A_DAT
	memcpy ( &data[segsiz], addr, ( nbyt - segsiz ) 
	
	 * Let's figure out where the "next" buffer will appear assuming w
	 * desire longword alignmen
	 
	end_byte = ( (nbyt-segsiz) + CBF_ADD ) & CBF_ALIGN
	buf->CBF$L_OUT = end_byt
   
    return ( SS$_NORMAL 




extern int AMDS$if_true ( struct dsc$descriptor *ldesc

**
**  FUNCTIONAL DESCRIPTIO

**	This routine try's to translate the logical name provided as inp
**	and returns a status of 1 (TRUE) if the first character of t
**	equivalence name is a T(rue), E(nable), Y(es) or 1.  Otherwise
**	0 (FALSE) is returne

**  FORMAL PARAMETERS:

**	ldesc		Pointer to a string descriptor of the logical nam

**  IMPLICIT INPUTS:    no

**  IMPLICIT OUTPUTS:   no

**  RETURNS:          

**		1 or 0 true/fal

**  SIDE EFFECTS:       no
**


    int status;                

    int mask = LNM$M_CASE_BLIN
    char t_buffer[32
    int retlen = 
    struct _itmlst trnlnm_itmlst[] =
	32, LNM$_STRING, &t_buffer, &retle
	0, 0, 0, 0

    $DESCRIPTOR ( lnm_tbl, "LNM$FILE_DEV");               

    t_buffer[0] = 
    status = sys$trnlnm ( &mask, &lnm_tbl, ldesc, 0, trnlnm_itmlst 
    t_buffer[retlen] = 
    if ( status == SS$_NORMAL
   
	
	 * Upcase the first charact
	 
	if ( t_buffer[0] >= 'a' && t_buffer[0] <= 'z'
	    t_buffer[0] -= 0x2

	
	 * Check for valid affirmative respons
	 
	if ( t_buffer[0] == 'T' ||	/* True 
	     t_buffer[0] == 'Y' ||	/* Yes 
	     t_buffer[0] == 'E' ||	/* Enable 
	     t_buffer[0] == '1' )	/* 1 

	    return ( 1 
   

    return ( 0 



            
extern int AMDS$lnm_getint_value ( struct dsc$descriptor *ldesc

**
**  FUNCTIONAL DESCRIPTIO

**	This routine will attempt to parse the incoming logical name a
**	return a value if the logical name is parsed.

**  FORMAL PARAMETERS:

**	ldesc		Pointer to a string descriptor of the logical na

**  IMPLICIT INPUTS:    no

**  IMPLICIT OUTPUTS:   no

**  RETURNS:          

**		Integer value for logical name translatio

**  SIDE EFFECTS:       no
**


    int statu
    int ret_value = 

    int mask = LNM$M_CASE_BLIN
    char t_buffer[32
    int retlen = 
    struct _itmlst trnlnm_itmlst[] =
	32, LNM$_STRING, &t_buffer, &retle
	0, 0, 0, 0
                
    $DESCRIPTOR ( lnm_tbl, "LNM$FILE_DEV"

    t_buffer[0] = 
    status = sys$trnlnm ( &mask, &lnm_tbl, ldesc, 0, trnlnm_itmlst 
    t_buffer[retlen] = 
    if ( status == SS$_NORMAL ) ret_value = atoi ( t_buffer 
    return ( ret_value 
  



extern void report_we_are_lost ( int errlen, char *errstr



**
**  FUNCTIONAL DESCRIPTIO

**	This is the central repository to report all types of cannotgethere
**	and shouldnotgethere'

**  FORMAL PARAMETERS:

**	errlen		Length of error stri

**	errstr		Error stri

**  IMPLICIT INPUTS:    no

**  IMPLICIT OUTPUTS:   no

**  RETURNS:            non

**  SIDE EFFECTS:       no
**
*/                                    

    lib$signal ( AMDS$_CANNOTGETHERE, 2, errlen, errstr 



static int check_rm_active 

**
** FUNCTIONAL DESCRIPTION                                     

**	This function returns 1 if the driver is available to be use
*
** INPUT


** OUTPUT

**	Status = From the $ASSIGN servi

**


    int		dev_desc [2] = {0, 0
    int		chan;				/* channel 
    short int 	status; 			/* status 
    int		retval = 0;			/* Assume not... 

    
     * Assigna changle to rmdriver.
     
    dev_desc [0] = strlen(devlist[0]
    dev_desc [1] = devlist[0
    status = sys$assign ( &dev_desc, &chan, 0, 0

    /*   
     * If we are RMA0, we need to check if we're enabled as wel
     
    if ( status == SS$_NORMAL
   
	short int	iosb [4];	/* Standard I/O Status Block 
	int dvi_devst
	itmlst	getdvi_itmlst [] = { /* Define GETDVI item list 
		4, DVI$_DEVSTS, &dvi_devsts, 0
		0, 0, 0, 0

	status = sys$getdviw ( 	0, chan, &dev_desc, getdvi_itmlst
				iosb, 0, 0, 0
	
	 * If we are not started, then deassign chann
	 
	if ( ( dvi_devsts & RM$M_READY ) == 0
	    sys$dassgn ( chan 
	el
	{     
	    dl_chanlist[ dl_chan_count ].devlist_index = 
	    dl_chanlist[ dl_chan_count ].dl_chan = cha
	    dl_chan_count+
	    rm_active = 
	    retval = 

   
    return ( retval 



static int get_dl_chan (

/
**
** FUNCTIONAL DESCRIPTION                                     

**	This routine will attempt to open up a channel to all foun
**	adaptors.
*
** INPUT

**	non

** OUTPUT

**	Status = From the $ASSIGN servi

**
*/                                   

    int 	idx;				/* Loop 
    int		userdefined = 0;		/* AMDS$DEVICE defined/used 
    int		chan;				/* channel 
    int 	status = SS$_NORMAL;		/* status 

    int		dev_desc [2] = {0, 0
    int		length
    char	lan_buf[4];		/* 4 bytes 
    int		lan_desc[2] = {0,0};	/* Descriptor to hold trnlnm name 


    
     * If RMA0 is not active, then we need to find a datali
     * with whom to communicate our protoco
     
    if ( !check_rm_active()
   
	
	 * Next check to see if AMDS$DEVICE is defined.  If so, ensure th
	 * the device it points to is cool...  If cool then we're going 
	 * set our channel to it; otherwise, we'll start looking in th
	 * channel list at the 3rd entry (rma0=1,amds$device=
	 
	status = AMDS$Check_amdsdevice_log ( lan_buf, &length 
	if ( status == SS$_NORMAL

	    
	     * If we have a valid AMDS$DEVICE we know that we've alread
	     * assigned a channel to it (temporarily at least) so no nee
	     * to check that... just get the channel, populate the structur
	     * and get out of her
	     
	    dev_desc [0] = strlen(devlist[1]
	    dev_desc [1] = devlist[1
	    sys$assign ( &dev_desc, &chan, 0, 0
	    dl_chanlist[dl_chan_count].devlist_index = 
	    dl_chanlist[dl_chan_count].dl_chan = cha
	    dl_chan_count+

	el

	    /*       
	     * Loop through trying to assign a channel to the device.  If fou
	     * save the channel's information in our global channel list, an
	     * try the next channel until we've exhausted our lis
	    
	     * Note: If the AMDS$DEVICE logical is defined
	     *		only use that channel.
	     
	    idx = 2;	/* NB: We start at the 3rd entry... 
	    while (*devlist[ idx ] != ' ' && dl_chan_count < MAX_CHAN
	   
		
		 * Set up a descriptor for the sys$assign servic
		 * to try to make the connection t
		 
		dev_desc [0] = strlen(devlist[ idx ]
		dev_desc [1] = devlist[ idx 
		status = sys$assign ( &dev_desc, &chan, 0, 0

		
		 * If found then populate structure.
		 
		if ( status == SS$_NORMAL
	
		    
		     * Fill in our array for datalink channels.
		     
		    dl_chanlist[dl_chan_count].devlist_index 	= id
		    dl_chanlist[dl_chan_count].dl_chan	  	= cha
		    dl_chan_count+
	
		
		 * We expect SS$_NOSUCHDEV, signal others.
		 
		else if (status != SS$_NOSUCHDE
		    lib$signal ( status 
		
		 * Increment loop counter, and try next chann
		 
		idx+
	   

   

    
     * If we didn't find one, then signal a messa
     
    if ( dl_chan_count == 0 ) status = AMDS$_NODATALIN
    else status = SS$_NORMA

    return ( status );                                          




int AMDS$Check_amdsdevice_log ( char *lanname, int *le

**++                                                       
** FUNCTIONAL DESCRIPTI

**	This routine will check the validity of the AMDS$DEVICE logica
**	If bad, then it errors ou

** INPU

**	lanname = Name of the lan device to be returned if AMDS$DEVICE 
**		  a define
**	len = Length of the name (4 or 

** OUTPU

**	status 
**


    int		status = SS$_NORMAL;	/* Status 
    short int	iosb [4];		/* Standard I/O Status Block 


    char	t_buffer[80];		/* Buffer to hold our name 
    int		trn_retlen = 0;		/* Return length of descriptor 
    itmlst	trnlnm_itmlst [] = {	/* Define our trnlnm item list 
	80, LNM$_STRING, t_buffer, &trn_retle
	0, 0, 0, 0
    $DESCRIPTOR ( lnm_tbl, "LNM$FILE_DEV");               
    $DESCRIPTOR ( trn_name, "AMDS$DEVICE"

                       
    
     * Translate the logical... if successful and we have something, th
     * let's check it out.
     
    t_buffer[0] = 
    status = sys$trnlnm (0, &lnm_tbl, &trn_name, 0, trnlnm_itmlst
    t_buffer[trn_retlen] = 
    if ( status == SS$_NORMA
    {
	if ( trn_retlen == 4

	    int assign_statu
	    int cha

	    
	     * We check the validity by attempting to assign a channel to t
	     * device.  If successful, then we have a good device, otherwi
	     * set the return status to the status received and let the call
	     * take the appropriate actio
	     
	    assign_status = sys$assign ( &trn_name, &chan, 0, 0
	    if ( assign_status != SS$_NORMAL 
	   
		status = assign_statu
		lib$signal ( AMDS$_BADLANADR, 2, trn_retlen, t_buffer 
	   
 	    el
	   
		sys$dassgn ( chan 
		*len = trn_retle
		
		 * Capitalize.
		 
		if ( t_buffer[0] >= 'a' && t_buffer[0] <= 'z
		    t_buffer[0] -= 0x2
		if ( t_buffer[1] >= 'a' && t_buffer[1] <= 'z
		    t_buffer[1] -= 0x2
		if ( t_buffer[2] >= 'a' && t_buffer[2] <= 'z
		    t_buffer[2] -= 0x2
		strncpy ( lanname, t_buffer, 4
	   

	el

	    
	     * If by chance the logical was defined, but not to something tha
	     * is defined as "legal", we need to state 
	     
	    lib$signal ( AMDS$_UNKSTYLE, 2, trn_retlen, t_buffer 

   


    return ( status 



  
extern void AMDS$Fill_P2 ( int *p2desc, int num_buffers


**++                  
**               
** FUNCTIONAL DESCRIPTI
**     
**	This routine will fill in the data in the P2 Buffer that needs 
**	be sent to the datalink to start the DECamds protocol on the wir

**	This routine is shared by RMCP and AMDS$CO

** 	The P2 buffer sent to our QIO to initialize the particular LA
**	driver about what type of information is going to be sent over th
**	wire.  The buffer must be set up in the format o

**	PARAMETER ID (word
**	Longword Value or Counted Stri

** 	All values shown in I/O User's Guide Part II (middle of chaper 

** 	This structure is set up to let us use a private protocol across t
** 	wire in the LAN/802 Extended Format.

** 	Required parameters ar

** 	NMA$C_PCLI_FMT -> Packet Format (802
** 	NMA$C_PCLI_PID -> Protocol Identifier (08-00-2B-80-48

** 	Error parameters ar

** 	NMA$C_PCLI_PTY -> Protocol Typ
** 	NMA$C_PCLI_SAP -> 802 formap SAP (Service Access Poin
** 	NMA$C_PCLI_ACC -> Protocol Access Mo
** 	NMA$C_PCLI_DES -> Shared Protocol Destination Addre
** 	NMA$C_PCLI_PAD -> Use of message size field on X-mit/receive messag
** 	NMA$C_PCLI_SRV -> Channel Servi
** 	NMA$C_PCLI_GSP -> Group S

** 	Optional parameters are: (default or our value in parens afte
**	description (Asterisked (*) values are ones we us

**     *NMA$C_PCLI_BFN -> Number of buffers to preallocat
** 	NMA$C_PCLI_BSZ -> Device Buffer Size ( max of 150
**     *NMA$C_PCLI_BUS -> Max. allowable channel receive buff. siz
** 	NMA$C_PCLI_CON -> Controller Mode (NMA$LINCN_NO
** 	NMA$C_PCLI_CRC -> CRC Generation State (NMA$C_STATE_O
**     *NMA$C_PCLI_DCH -> Data Chaining State (NMA$C_STATE_OF
** 	NMA$C_PCLI_EKO -> Echo Mode (NMA$C_STATE_OF
**	NMA$C_PCLI_ILP -> Internal Loopback Mode. (NMA$C_STATE_OF
**     *NMA$C_PCLI_MCA -> Multicast Address (NMA$C_LINMC_SET: 09-00-2B-02-01-0
**	NMA$C_PCLI_MLT -> Multicast Address State (NMA$C_STATE_OF
**     *NMA$C_PCLI_PHA -> Physical Port Address (current/hdwr addres
**	NMA$C_PCLI_PRM -> Promiscuous mode (NMA$C_STATE_OF
**     *NMA$C_PCLI_RES -> Restart. (NMA$C_LINRES_EN


** INPUT

**	p2desc		Pointer to a two longword buffer.
**			It is assumed that the second longword contains
**			buffer of at least 100 byte
**	num_buffers	Number of buffers for DL to prealloca

** OUTPUTS:                                    

** 	The filled buffe
*
**


/*

 * Initialize the P2 buffer.  The P2 buffer is the buffer sent to t
 * datalink to start up DECamds's protocol on the LA

 * The NMA$ constants and types used below are defined in the I/O User'
 * Guide in the chapter discussing LAN/802 Device Driver
 * The NMA$ constants come from the NMADEF.SDL in the <NCP> VMS facilit
 * They are defined in LIB, but I couldn't find the VAXC equivalent 
 * LIB.MLB or LIB.L32 so I just copied the NMADEF.SDL out of the <NC
 * facility and use it...

 
    int		bufsiz = 
    long	*buflpos;	/* Pointers into quota list for long access *
    short int	*bufwpos;	/* Pointers into quota list for word access *
    char	*bufcpos;	/* Pointers into quota list for char access *


 * This has got to be the *UGLIEST* way to set up the buffer, but it w
 * the only way it worked correctl
 
    bufwpos = (short int *)p2desc[1];	/* Address of our buffer 


 * Start of word parameter_id / longword value pai
 
    *bufwpos++ 	= NMA$C_PCLI_FMT;	/* Packet Format 
    buflpos 	= bufwpo
    *buflpos++	= NMA$C_LINFM_802E;	/* 802E 
    bufsiz	+= 

    bufwpos	= buflpo
    *bufwpos++	= NMA$C_PCLI_BFN;	/* Number of buffers to preallocate 
    buflpos	= bufwpo
    *buflpos++	= num_buffers;		/* From logical translation 
    bufsiz	+= 

    bufwpos	= buflpo
    *bufwpos++	= NMA$C_PCLI_DCH;	/* Data Chaining State 
    buflpos	= bufwpo
    *buflpos++	= NMA$C_STATE_OFF;	/* OFF - (default) 
    bufsiz	+= 

    bufwpos	= buflpos;		/* NOTE: Only valid > V5.4-3 
    *bufwpos++	= NMA$C_PCLI_CCA;	/* Can Change Address 
    buflpos	= bufwpos;                                   
    *buflpos++	= NMA$C_STATE_ON;	/* ON 
    bufsiz	+= 

    bufwpos	= buflpos;                                   
    *bufwpos++	= NMA$C_PCLI_BUS;	/* Max. Channel Rcv. buffer size 
    buflpos	= bufwpo
    *buflpos++	= AMDS$K_LAN_BUF_SIZ;	/* Use sparcdef defined symbol 
    bufsiz	+= 

    bufwpos	= buflpo
    *bufwpos++	= NMA$C_PCLI_RES;	/* Automatic channel restart 
    buflpos	= bufwpo
    *buflpos++	= NMA$C_LINRES_ENA;	/* Enable automatic restart 
    bufsiz	+= 


 * End of word paremter_id / longword value pai

 * Start of word parameter_id / counted character string pair
 
    bufwpos	= buflpo
    *bufwpos++	= NMA$C_PCLI_PHA;	/* Physical Port Address 
    *bufwpos++	= 2;              	/* Counted byte string 
    *bufwpos++	= NMA$C_LINMC_SD
    bufsiz	+= 


    *bufwpos++	= NMA$C_PCLI_MCA;	/* Multi-cast Address 
    *bufwpos++	= 8;			/* Counted byte string... 
    *bufwpos++	= NMA$C_LINMC_SET;	/* Set the multicast address 
    bufcpos	= bufwpo
    *bufcpos++	= 0x09;			/* 09-00-2B-02-01-09 
    *bufcpos++	= 0x0
    *bufcpos++	= 0x2
    *bufcpos++	= 0x0
    *bufcpos++	= 0x0
    *bufcpos++	= 0x0
    bufsiz	+= 1

    bufwpos	= bufcpo
    *bufwpos++	= NMA$C_PCLI_PID;	/* Protocol ID 
    *bufwpos++	= 5;			/* Counted string 
    bufcpos	= bufwpo
    *bufcpos++	= 0x08;			/* 08-00-2B-80-48 
    *bufcpos++	= 0x0
    *bufcpos++	= 0x2
    *bufcpos++	= 0x8
    *bufcpos++	= 0x4
    bufsiz	+= 

    p2desc[0] = bufsiz;     /* Number of bytes of actual data to be sent 

                                       




extern int AMDS$startup_dl ( int num_buffers, int from_com
			     int *returned_chan

**++                  

** FUNCTIONAL DESCRIPTI
**     
**	This routine will attempt to start up datalink protocol on the list 
**	valid LAN adapter types.  This routine is to be shared by RM
**	and AMDS$COMM as a single point of entry for both.  The major differen
**	between the two is that if we are being called from AMDS$COMM then 
**	need to check if RMA0 is active and if so use it since it will be t
**	conduit for all our message

** INPUT
**          
**	num_buffers	Number of buffers to be preallocated by datalin
**	from_comm	currently unuse
**	returned_chan	Channel to return to calle


** Outputs:                                    

** 	returned_chan 	= Value to return our channel in, if an
** 	status		= Status of success or failu

**            
**


    int i = 0;			/* Loop 
    int statu
    int	nmabuf[100];		/* buffer to fill ... 
    int	nmadsc[2] = {0,&nmabuf};/* NMA buffer descriptor 
    short int stiosb[4

    
     * Find all the available datalink channe
     */        
    status = get_dl_chan ( 
    if ( status != SS$_NORMAL 
   
	*returned_chan = 
	lib$signal ( status 
   
    
     * If we know our channel is the RMA0 channel, then just exit the start
     * isn't going to do anything, the protocol is already start
     
    else if ( rm_active
   
	*returned_chan = dl_chanlist[ 0 ].dl_chan;	/* Set return value 
   
    
     * Otherwise, try to find a line to start up on.
     */                      
    el
    {         
	
	 * Fill in the P2 Buff
	 
	AMDS$Fill_P2 ( &nmadsc, num_buffers 

	
	 * Loop through all valid channels... trying to start the protocol.
	 
	while ( i < dl_chan_count

	    
	     * Issue a QIOW to the particular LAN driver we have a channel t
	     * to set controller mode and start the controller po
	     
	    status = sys$qiow (	0, dl_chanlist[ i ].dl_chan
				IO$_SETMODE | IO$M_CTRL | IO$M_STARTU
				&stiosb, 0, 
				0, nmadsc, 0, 0, 0, 0
	    if ( status == SS$_NORMAL && stiosb[0] == SS$_NORMAL
	   
		brea
	   
	    el
	   
		/
		 * If it's not fatal, then we'll try the next line.
		 * but if fatal, then this will force ex
		 * Also, note that we ensure we don't signal the sam
		 * message twice - the stiosb[0] should be the same a
		 * the status, but I'm not taking any chances.
		 
		lib$signal ( stiosb [0] 
		if ( stiosb [0] != status ) lib$signal ( status )
	   
	    i+


	
	 * Set up return chann
	 
	*returned_chan = dl_chanlist[ i ].dl_cha
   
    return ( status 



extern void init_mem_queues 

**
**  FUNCTIONAL DESCRIPTIO

**	This routien will initialize the various memory lookaside list he
**	pointers...  This gets called once at application startup... Fo
**	both console and com

**  FORMAL PARAMETERS:  no

**  IMPLICIT INPUTS:    no

**  IMPLICIT OUTPUTS:   no

**  RETURNS:            non

**  SIDE EFFECTS:       no
**


    int id
    int statu
    struct _itmlst getsyi_itmlst[] =
	sizeof(int), SYI$_PAGE_SIZE, &page_size, 
	sizeof (int), SYI$_VIRTUALPAGECNT, &virpagcnt, 
	0, 0, 0, 0 

    
     * Get and save the system's page_size and the virtual page coun
     
    status = sys$getsyi(0, 0, 0, getsyi_itmlst, 0, 0, 0)
    if ( status != SS$_NORMAL
	lib$signal ( status 

    
     * Initialize the queue headers.. the mem_q_size, mem_q_len, an
     * mem_q_max fields are already filled in.
     
    for ( idx = AMEM$K_SML_Q; idx < AMEM$K_INVALID; idx++
   
	mem_list[idx].mem_q_fl = &mem_list[idx].mem_q_f
	mem_list[idx].mem_q_bl = &mem_list[idx].mem_q_f
   


 
extern void list_mem_queues 

**
**  FUNCTIONAL DESCRIPTIO

**	This routine will list some data about memory queue

**  FORMAL PARAMETERS:  no

**  IMPLICIT INPUTS:    no

**  IMPLICIT OUTPUTS:   no

**  RETURNS:            non

**  SIDE EFFECTS:       no
**


    int total = 
    int id
    int av

    printf ( "\nList mem queues... Acount: %d, Dcount: %d, diff: %d
			acount, dcount, acount-dcount 

    for ( idx = AMEM$K_SML_Q; idx < AMEM$K_INVALID; idx++
   
	total += ( mem_list[idx].mem_q_len 
		   ( mem_list[idx].mem_q_size + AMEM$K_LENGTH ) 
	printf ( "\n\tMem qsize: %d, qlen: %d"
			mem_list[idx].mem_q_size, mem_list[idx].mem_q_len 
   

    avg = total / page_siz
    printf ("\nTotal queued pages: %d bytes, : %d pages ", total, avg 
                  
}	  
                                  

extern int mem_alloc ( syze, type, va_alist
    int syz
    int typ
    va_dcl		/* NB: no semicolon 

**
**  FUNCTIONAL DESCRIPTIO

**	This routine handles the crux of all of DECamds' memory allocati
**	using the LIB$GET_VM routine.  It is smart enough to also che
**	for special blocks which are used quite frequently within the cour
**	of running DECamds.  If this routine detects one of these speci
**	blocks it will attempt to REMQUE from a lookaside list of blocks th
**  	have already been used (and deallocated) instead of using the overhe
**	of multiple LIB$GET_VM call

**	It will allocate a buffer of requested size plus an internal head
**	as follow

**		---------------
**		|<size  | FEED |
**		--------------
**		|sbt|typ| size>|
**		--------------

**  FORMAL PARAMETERS:

**	syze		Size of block to be allocat
**	type		Type of block to be allocat
**	subtype		(opt) Any subtype to be placed he

**  IMPLICIT INPUTS:    no

**  IMPLICIT OUTPUTS:   no

**  RETURNS:          

**	Address of block to be allocat

**  SIDE EFFECTS:       no
**


    va_list a
    int idx;		/* loop 
    int size = syz
    int block_size = -
    int got_bloc
    int statu
    int subtyp
    int big_mem = 0 + ( AMDS$K_BIG_MEM_OK << AMDS$K_BIG_MEM_SHIFT 
    int bigger_mem = 0 + ( AMDS$K_BIGGER_MEM_OK << AMDS$K_BIGGER_MEM_SHIFT 
      
    AMEM *membl
    STD *bl

    if ( size < 0 ) printf ("\n size: %d   <  0!", size

    /*  
     * Did we get a 3rd para
     
    va_start ( ap );	/* start varargs... 
    va_count ( subtype 
    if ( subtype > 2 )	/* More than 2 args? 
	subtype = va_arg ( ap, int );	/* Yep, 3rd is SUBTYPE 
    el
	subtype = 
    va_end ( ap 

    /*                                           
     * Size check... Can only allocate up to max_block_size bytes, unle
     * a special flag or two is set in the left 16 bits... This is done t
     * ensure we know at certain portions of code that we will be allocati
     * a rather large chunk of memory and that's what we want to d
     
    if ( ( size & big_mem ) == big_mem 
   
	
	 * If the size and !big mem is greater than a big mem alloc, we'
	 * have problems just 'cutting' off the size.
	 
	if ( ( size & ~big_mem ) > 0x0000FFFF ) /* 65535 

	    printf ("\n AMDS BIG MEM Alloc masking incorrectly: %d ", size


	size &= 0x0000FFF
   
    else if  ( ( size & bigger_mem ) == bigger_mem
   
	if ( ( size & ~bigger_mem ) > 0x00FFFFFF ) /* 16,777,215 

	    printf ("\n AMDS BIGGER MEM Alloc masking incorrectly: %d ", size

	size &= 0x00FFFFF
   
    else if ( size > MAX_BLOCK_SIZE
    	printf ("\n*** NOBIGMEM... AMDS Alloc HUGE block... size : %d ", size 

    
     * Check to make sure we're less than virtual page count, which happe
     * to be the largest packet we can successfully handle...  If not th
     * is a fatal error.
     
    if ( ( size / page_size ) > virpagcnt
   
	printf ("\n***AMDS$INFO Alloc block pages : %d gtr than virpagcnt: %d
			size/page_size, virpagcnt 
	printf ("\n***AMDS$INFO, increase SYSGEN parameter VIRPAGCNT "
	lib$signal ( AMDS$_NOCONT )
   
                                                     
    
     * Increment alloc count... Then modify block size to be on a longwo
     * boundary for "congruity
     
    acount+
    if ( size < MIN_BLOCK_SIZE
	size = MIN_BLOCK_SIZ
    el
	size = (size + 3) & 0xFFFFFFF

    
     * Check to see if we can take this block from one of our look-aside lis
     * first... If not, then we'll bite the bullet and do the lib$get_
    
     * BTW: _REMQUE returns 0,1 when entry removed, and 2 when queue was emp
     
    idx = AMEM$K_SML_
    got_block = 2;	/* To fake the QUEUE_EMPTY 
    while ( idx < AMEM$K_INVALID ) 
   
	if ( size <= mem_list[idx].mem_q_size

	    
	     * To ensure that if our REMQUE fails that we allocate a blo
	     * big enough to fit on the queue when we dealloc the blo
	     * set up the block_size parameter to be the queue size
	     
	    block_size = mem_list[idx].mem_q_siz

	    /
	     * Are we messing with this queue alread
	     
	    if ( !_BBSSI( 0, &mem_list[idx].mem_q_lock )
	   
		
		 * Do we have block?  If so then decrement our count of bloc
		 * in our queue and then use the block in questi
		 
		got_block = _REMQUE ( (int *)mem_list[idx].mem_q_fl, &memblk 
		if ( got_block != 2 ) mem_list[idx].mem_q_len-
		mem_list[idx].mem_q_lock = 
	   

	    
	     * Since I don't want to allocate a "smaller" block from one of t
	     * larger sized queues, we force loop exit by setting the idx t
	     * the max.  This way whether or not I have taken an element o
	     * I won't be abusing my memory rights.
	     
	    idx = AMEM$K_INVALID

	idx+
   

    
     * Now I either have taken a block off one of my lookaside lists, or
     * do not yet have a block... If I don't yet have a block, then I ne
     * to allocate one
     
    if ( got_block == 2 )	/* Empty queue or I didn't get one... 
   
	
	 * If we have block that doesn't fit into one of our lookaside lis
	 * sizes, then set the block_size value to the size we want, sinc
	 * block_size is only "set" when we have a block that would fit.
	 
	if ( block_size == -1 ) block_size = siz

	block_size += AMEM$K_LENGTH;	/* Add in our header 
	status = lib$get_vm ( &block_size, &memblk 
	if ( status != SS$_NORMAL

	    lib$signal ( status 
	    return ( NULL 

#if DEB
	lastallocaddr = (int)membl
#end
	memblk->AMEM$L_SIZE = block_size; 	/* Entire block size.. 
    }						/* Including header 
    el
	memblk = (int)memblk - AMEM$K_LENGTH;	       	/* Since we queued the fl/bl 

    
     * Fill in some information about the allocated bloc
     
    memblk->AMEM$W_MARKER = 0xFEE
    memblk->AMEM$B_TYPE = typ
    memblk->AMEM$B_SUBTYPE = subtyp

    
     * Get to the area we are going to use
     
    blk = (int)memblk + AMEM$K_LENGT

#if DEBUG_M
    memset ( blk, 0x47, size 
#end
    blk->STD$L_FLINK = &blk->STD$L_FLIN
    blk->STD$L_BLINK = &blk->STD$L_FLIN
    blk->STD$W_SIZE = siz
    blk->STD$B_TYPE = typ
    blk->STD$B_SUBTYPE = subtyp

    return ( blk 




extern void mem_free ( blk, va_alist
    STD *bl
    va_dcl		/* NB: no semicolon 

**
**  FUNCTIONAL DESCRIPTIO

**	This routine will call LIB$FREE_VM for most cases.  Although 
**	we come across a deallocation on one of our special blocks th
**	DECamds uses alot, we will INSQUE the block onto a lookaside li
**	so that the allocation will make less calls to lib$get_v
*
*
**	When the memory is really deleted it will look as follow
*
**	---------------
**	|<size  | DEAD |
**	--------------
**	|sbt|typ| size>|
**	--------------
*
**	When the memory is placed on a lookaside list it will look as follow
**	  
**	---------------
**	|<size  | DEAF |
**	--------------
**	|sbt|typ| size>|
**	--------------
*
**  FORMAL PARAMETERS:

**	blk 		Address of block to dealloca
**	size 		NOT USED, but there so that lots of code doesn't ch

**  IMPLICIT INPUTS:    no

**  IMPLICIT OUTPUTS:   no

**  RETURNS:            non

**  SIDE EFFECTS:       no
**


    va_list a
    int q_insert = 0;		/* Assume false 
    int statu
    int siz
    int id
    int free_siz
    int max_factor = 
    AMEM *memblk = (int)blk - AMEM$K_LENGT
          
    
     * Make some checks for validi
     
    if ( memblk->AMEM$W_MARKER == 0xDEAD || memblk->AMEM$W_MARKER == 0xDEAF
   
	printf ( "\n***AMDS$INFO already deleted this block size: %d type: %d
		  memblk->AMEM$L_SIZE, memblk->AMEM$B_TYPE 
	retur
   
    else if ( memblk->AMEM$W_MARKER != 0xFEED
   
	printf ("\n***AMDS$INFO, corruption possible size: %d, type: %d, st: %d
		  memblk->AMEM$L_SIZE, memblk->AMEM$B_TYP
		  memblk->AMEM$B_SUBTYPE 
	retur
   

    
     * Get the size from the memory block header.  This is to be the si
     * used if we need to do a free_vm call.  otherwise, the size of our da
     * packet on a lookaside list would be the "free_size" - header_size (s
     * the mem_alloc routine above
     
    free_size = memblk->AMEM$L_SIZ
    if ( free_size < MIN_BLOCK_SIZE
   
	printf ("\n***AMDS$INFO, corruption, free_size: %d < 0.. type: %d, st:%d"
		  memblk->AMEM$L_SIZE, memblk->AMEM$B_TYP
		  memblk->AMEM$B_SUBTYPE 
	return;	/* punt 
   
    size = free_size - AMEM$K_LENGT

    
     * If we have console block (i.e., not RMCP) and event collect i
     * being used, then we want to save more data on our lookaside lis
     * since we'll likely use more.
     
	if  (console_blockP != NULL 
	     console_blockP->CSDB$R_FLAGS_OVERLA
			     CSDB$R_FLAG_BIT
			     CSDB$V_EVENT_COLLECT 
	    max_factor = 
	  

#if DEBUG_M
    memset ( blk, 0x92, size 
#end

    
     * Count this dalloc.. and reset our queue header for insertion.
     
    dcount+
    blk->STD$L_FLINK = &blk->STD$L_FLIN
    blk->STD$L_BLINK = &blk->STD$L_FLIN

    
     * First let's try to place the block on a look aside list so that we c
     * reuse the memory without the overhead of a get_vm call
     
    memblk->AMEM$W_MARKER = 0xDEAF;	/* Indicates not used, but allocated 
    idx = AMEM$K_SML_
    while ( idx < AMEM$K_INVALID ) 
   
	/* 
	 * If the size of the block (from the allocation) is the same as t
	 * current mem_q size and inserting the block won't "overflow" th
	 * maximum allowed elems in the queue.. then insert the element a
	 * exit the lo
	 
	if ( size == mem_list[idx].mem_q_size 
	     mem_list[idx].mem_q_len < ( max_factor * mem_list[idx].mem_q_max)

	    
	     * Are we already messing w/ this queu
	     
	    if ( !_BBSSI( 0, &mem_list[idx].mem_q_lock )
	   
		
		 * NOTE: We are inserting the "BLOCK" header into the queu
		 * 		not including our "mem header...
		 
		_INSQUE ( blk, (int *)mem_list[idx].mem_q_bl 
		mem_list[idx].mem_q_len+
		mem_list[idx].mem_q_lock = 
		q_insert = 
	   

	    
	     * Since we've done the insert, set the idx to the m
	     
	    idx = AMEM$K_INVALID

	idx+
   

    
     * If we didn't insert the element, then really delete it.
     * remember, free_size is the size we had allocate
     */                                                        
    if ( !q_insert
   
	memblk->AMEM$W_MARKER = 0xDEA
	status = lib$free_vm ( &free_size, &memblk 
	
	 * The BADBLOADR seems to come at odd times.. just ignore it.
	 * it gets signalled for far too many reasons to determine th
	 * root cause.. most likely though it's freeing well over a pa
	 * of memory and choki
	 
	if ( status != SS$_NORMAL && status != LIB$_BADBLOADR
	    lib$signal ( status 
   



#if DEB

static int not_static ( int btype, int size

    int retval = 

    switch ( btype
   
	/* List of "STATIC" types 
	case TYP$K_ACTION
	case TYP$K_ACTITM
	case TYP$K_CDB
	case TYP$K_CFG
	case TYP$K_CSDB
	case TYP$K_CSTDB
	case TYP$K_CST_INT
	case TYP$K_CST_FLT
	case TYP$K_DIDB 
	case TYP$K_EVENT
	case TYP$K_INTW
	case TYP$K_IOP
	case TYP$K_LADB
	case TYP$K_NDB
	case TYP$K_OSDB
	case TYP$K_OSVB
	case TYP$K_PLB
	case TYP$K_POPM
	case TYP$K_SECBUF
	case TYP$K_STD
	case TYP$K_SYSOB
	case TYP$K_THRDEF
	case TYP$K_THRVAL
	case TYP$K_TIMR
	case TYP$K_WIN

	    retval = 
	    brea

   

    return ( retval 


static void mem_dump (

**
**  FUNCTIONAL DESCRIPTIO


**  FORMAL PARAMETERS:
**              

**  IMPLICIT INPUTS:    no

**  IMPLICIT OUTPUTS:   no

**  RETURNS:            non

**  SIDE EFFECTS:       no
**


    struct	FAB	fab;		/* File Access Block 
    struct	RAB	rab;		/* Record Access Block 
    int statu
    int id


    
     * Load up the FAB. 
     
    fab = cc$rms_fab;				/* Default values for RMS 
    fab.fab$l_dna = "AMDS$DUMP:.LOG";		/* File specification 
    fab.fab$b_dns = strlen("AMDS$DUMP:.LOG"
    fab.fab$l_fna = "MEM-DUMP.LOG";		/* File name 
    fab.fab$b_fns = strlen("MEM-DUMP.LOG")
    fab.fab$w_mrs = 80;				/* Maximum record size 
    fab.fab$b_shr = FAB$V_SHRGET;		/* Shared GET access 
    fab.fab$l_alq = 
    fab.fab$w_deq = 1;        
    fab.fab$l_fop = FAB$M_CBT | FAB$M_SUP | FAB$M_SQO | FAB$M_DF
    fab.fab$b_fac = FAB$V_PU
    fab.fab$b_rat = FAB$V_PRN;             
    fab.fab$b_rfm = FAB$C_VA

    
     * Attempt to open our fi
     */                                                  
    status = sys$create ( &fab 
    if ( status != RMS$_NORMAL && status != RMS$_FILEPURGED 
   
	lib$signal ( status 
	retur
   

    
     * If we can at least CREATE the file we will set up the RA
     
    rab = cc$rms_rab;				/* Default RAB information 
    rab.rab$l_fab = &fab;			/* Our FAB 
    rab.rab$l_rop = RAB$M_EOF | RAB$M_WBH | RAB$M_RLK
                           
    
     * Connect to the file using our R
     
    status = sys$connect ( &rab 
    if ( status == RMS$_NORMAL 
   
	AMEM *memblk = (AMEM *)console_blockP;	/* Our first MEM_ALLOC! 
	int done = 
	int *nextadd
	int staticsize = 
	int nonstaticsize = 

	while (!done)                  

	    if (_PROBER ( PSL$C_USER, 4, memblk ) == 0 
	   
		if ( memblk > lastallocaddr 
	
		    char line [2*80
		    sprintf ( line
			  "Done at: %x, static_size: %d, nonstatic_size: %d"
			  memblk, staticsize, nonstaticsize 
		    rab.rab$l_rbf = line
		    rab.rab$w_rsz = strlen(line);   
		    status = SYS$PUT (&rab
		    if (status != RMS$_NORMAL) lib$signal ( status 
		    done = 
	
	   
	    el
	   
		/* is this location our memory? 
		if ( memblk->AMEM$W_MARKER == 0xFEED
	
		    if ( not_static ( (int)memblk->AMEM$B_TYP
				     memblk->AMEM$L_SIZE )
		   
			char line [2*80
			sprintf ( line
			      "Block at: %x, type: %d, st: %d, size: %d
			      memblk, (int)memblk->AMEM$B_TYP
			      (int)memblk->AMEM$B_SUBTYPE, memblk->AMEM$L_SIZE
                                                                            
			rab.rab$l_rbf = line
			rab.rab$w_rsz = strlen(line);   
			status = SYS$PUT (&rab
			if (status != RMS$_NORMAL) lib$signal ( status 
			if ( memblk->AMEM$B_TYPE >= 0 
			     memblk->AMEM$B_TYPE <= TYP$K_ZQS
			    nonstaticsize += memblk->AMEM$L_SIZ
		   
		    el
		   
			if ( memblk->AMEM$B_TYPE >= 0 
			     memblk->AMEM$B_TYPE <= TYP$K_ZQS
			    staticsize += memblk->AMEM$L_SIZ
		   
	
	   
            nextaddr = (int *)membl
	    nextaddr+
	    memblk = (AMEM *)nextadd

   
    el
	lib$signal ( status 

    /*
     * Close the file.
     
    sys$close ( &fab 

#end



**
**  FUNCTIONAL DESCRIPTIO


**  FORMAL PARAMETERS:
**              

**  IMPLICIT INPUTS:    no

**  IMPLICIT OUTPUTS:   no

**  RETURNS:            non

**  SIDE EFFECTS:       no
**

