  
/* Record types: */

#define T_LABEL   1		/* Label. */
#define T_BEGIN   2		/* Start of SaveSet. */
#define T_END     3		/* End of SaveSet. */
#define T_FILE    4		/* File data. */
#define T_UFD     5		/* UFD data. */
#define T_EOV     6		/* End of volume. */
#define T_COMM    7		/* Comment. */
#define T_CONT    8		/* Continuation. */

/* Offsets into header block: */

#define G_TYPE    0		/* Record type. */
#define G_SEQ     1		/* Sequence #. */
#define G_RTNM    2		/* Relative tape #. */
#define G_FLAGS   3		/* Flags: */
#define   GF_EOF  0400000	/*   End of file. */
#define   GF_RPT  0200000	/*   Repeat of last record. */
#define   GF_NCH  0100000	/*   Ignore checksum. */
#define   GF_SOF  0040000	/*   Start of file. */
#define G_CHECK   4		/* Checksum. */
#define G_SIZE    5		/* Size of data in this block. */
#define G_LND     6		/* Length of non-data block. */

/* Non-data block types: */

#define O_NAME    1		/* File name. */
#define O_ATTR    2		/* File attributes. */
#define O_DIRECT  3		/* Directory attributes. */
#define O_SYSNAME 4		/* System name block. */
#define O_SAVESET 5		/* SaveSet name block. */

/* Offsets in attribute block: */

#define A_FHLN	  0		/* header length word */
#define A_FLGS	  1		/* flags */
#define A_WRIT	  2		/* creation date/time */
#define A_ALLS	  3		/* allocated size */
#define A_MODE	  4		/* mode */
#define A_LENG	  5		/* length */
#define A_BSIZ	  6		/* byte size */
#define A_VERS	  7		/* version */
#define A_PROT	  8		/* protection */
#define A_ACCT	  9		/* byte pointer account string */
#define A_NOTE	  10		/* byte pointer to anonotation string */
#define A_CRET	  11		/* creation date/time of this generation */
#define A_REDT	  12		/* last read date/time of this generation */
#define A_MODT	  13		/* monitor set last write date/time */
#define A_ESTS	  14		/* estimated size in words */
#define A_RADR	  15		/* requested disk address */
#define A_FSIZ	  16		/* maximum file size in words */
#define A_MUSR	  17		/* byte ptr to id of last modifier */
#define A_CUSR	  18		/* byte ptr to id of creator */
#define A_BKID	  19		/* byte ptr to save set of previous backup */
#define A_BKDT	  20		/* date/time of last backup */
#define A_NGRT	  21		/* number of generations to retain */
#define A_NRDS	  22		/* nbr opens for read this generation */
#define A_NWRT	  23		/* nbr opens for write this generation */
#define A_USRW	  24		/* user word */
#define A_PCAW	  25		/* privileged customer word */
#define A_FTYP	  26		/* file type and flags */
#define A_FBSZ	  27		/* byte sizes */
#define A_FRSZ	  28		/* record and block sizes */
#define A_FFFB	  29		/* application/customer word */

/* T_BEGIN, T_END & T_CONT header offsets: */

#define S_DATE    12
#define S_FORMAT  13
#define S_BVER    14
#define S_MONTYP  15
#define S_SVER    16
#define S_APR     17
#define S_DEVICE  18
#define S_MTCHAR  19
#define S_REELID  20
#define S_LTYPE   21

