/*
  object.c - writes RT-11 compatible .OBJ files.

  Ref: RT-11 Software Support Manual, File Formats.
*/

/*

Copyright (c) 2001, Richard Krehbiel
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

o Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

o Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

o Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

*/

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "rad50.h"

#include "object.h"

#include "macro11.h"

/*
  writerec writes "formatted binary records."
  Each is preceeded by any number of 0 bytes, begins with a 1,0 pair,
  followed by 2 byte length, followed by data, followed by 1 byte
  negative checksum.
*/

static int writerec(FILE *fp, char *data, int len)
{
	int chksum;					/* Checksum is negative sum of all
								   bytes including header and length */
	int i;
	unsigned hdrlen = len + 4;

	if(fp == NULL)
		return 1;				/* Silently ignore this attempt to write. */
	
	chksum = 0;
	if(fputc(FBR_LEAD1, fp) == EOF)	/* All recs begin with 1,0 */
		return 0;
	chksum -= FBR_LEAD1;
	if(fputc(FBR_LEAD2, fp) == EOF)
		return 0;
	chksum -= FBR_LEAD2;
	
	i = hdrlen & 0xff;				/* length, lsb */
	chksum -= i;
	if(fputc(i, fp) == EOF)
		return 0;

	i = (hdrlen >> 8) & 0xff;		/* length, msb */
	chksum -= i;
	if(fputc(i, fp) == EOF)
		return 0;

	i = fwrite(data, 1, len, fp);
	if(i < len)
		return 0;

	while(len > 0)				/* All the data bytes */
	{
		chksum -= *data++ & 0xff;
		len--;
	}

	chksum &= 0xff;

	fputc(chksum, fp);			/* Followed by the checksum byte */

	return 1;					/* Worked okay. */
}

/* gsd_init - prepare a GSD prior to writing GSD records */

void gsd_init(GSD *gsd, FILE *fp)
{
	gsd->fp = fp;
	gsd->buf[0] = OBJ_GSD;		/* GSD records start with 1,0 */
	gsd->buf[1] = 0;
	gsd->offset = 2;			/* Offset for further additions */
}

/* gsd_flush - write buffered GSD records */

int gsd_flush(GSD *gsd)
{
	if(gsd->offset > 2)
	{
		if(!writerec(gsd->fp, gsd->buf, gsd->offset))
			return 0;
		gsd_init(gsd, gsd->fp);
	}
	return 1;
}

/* gsd_write - buffers a GSD record */

/* All GSD entries have the following 8 byte format: */
/* 4 bytes RAD50 name */
/* 1 byte flags */
/* 1 byte type */
/* 2 bytes value */

static int gsd_write(GSD *gsd, char *name, int flags,
					 int type, int value)
{
	char *cp;
	unsigned radtbl[2];

	if(gsd->offset > sizeof(gsd->buf) - 8)
	{
		if(!gsd_flush(gsd))
			return 0;
	}

	rad50x2(name, radtbl);

	cp = gsd->buf + gsd->offset;
	*cp++ = radtbl[0] & 0xff;
	*cp++ = (radtbl[0] >> 8) & 0xff;
	*cp++ = radtbl[1] & 0xff;
	*cp++ = (radtbl[1] >> 8) & 0xff;

	*cp++ = flags;
	*cp++ = type;

	*cp++ = value & 0xff;
	*cp = (value >> 8) & 0xff;

	gsd->offset += 8;

	return 1;
}

/* gsd_mod - Write module name to GSD */

int gsd_mod(GSD *gsd, char *modname)
{
	return gsd_write(gsd, modname, 0, GSD_MODNAME, 0);
}

/* gsd_csect - Write a control section name & size to the GSD */
int gsd_csect(GSD *gsd, char *sectname, int size)
{
	return gsd_write(gsd, sectname, 0, GSD_CSECT, size);
}

/* gsd_intname - Write an internal symbol (ignored by RT-11 linker) */
int gsd_intname(GSD *gsd, char *name, unsigned value)
{
	return gsd_write(gsd, name, 0, GSD_ISN, value);
}

/* gsd_xfer - Write a program transfer address to GSD */
int gsd_xfer(GSD *gsd, char *name, unsigned value)
{
	return gsd_write(gsd, name, 010, GSD_XFER, value);
}

/* gsd_global - Write a global definition or reference to GSD */
/* Caller must be aware of the proper flags. */
int gsd_global(GSD *gsd, char *name, int flags, unsigned value)
{
	return gsd_write(gsd, name, flags, GSD_GLOBAL, value);
}

/* Write a program section to the GSD */
/* Caller must be aware of the proper flags. */
int gsd_psect(GSD *gsd, char *name, int flags, int size)
{
	return gsd_write(gsd, name, flags, GSD_PSECT, size);
}

/* Write program ident to GSD */
int gsd_ident(GSD *gsd, char *name)
{
	return gsd_write(gsd, name, 0, GSD_IDENT, 0);
}

/* Write virtual array declaration to GSD */
int gsd_virt(GSD *gsd, char *name, int size)
{
	return gsd_write(gsd, name, 0, GSD_VSECT, size);
}

/* Write ENDGSD record */

int gsd_end(GSD *gsd)
{
	gsd->buf[0] = OBJ_ENDGSD;
	gsd->buf[1] = 0;
	return writerec(gsd->fp, gsd->buf, 2);
}

/* TEXT and RLD record handling */

/* TEXT records contain the plain binary of the program.  An RLD
   record refers to the prior TEXT record, giving relocation
   information. */

/* text_init prepares a TEXT_RLD prior to writing */

void text_init(TEXT_RLD *tr, FILE *fp, unsigned addr)
{
	tr->fp = fp;

	tr->text[0] = OBJ_TEXT;		/* text records begin with 3, 0 */
	tr->text[1] = 0;
	tr->text[2] = addr & 0xff;	/* and are followed by load address */
	tr->text[3] = (addr >> 8) & 0xff;
	tr->txt_offset = 4;		/* Here's where recording new text will begin */

	tr->rld[0] = OBJ_RLD;		/* RLD records begin with 4, 0 */
	tr->rld[1] = 0;

	tr->txt_addr = addr;
	tr->rld_offset = 2;		/* And are followed by RLD entries */
}

/* text_flush - flushes buffer TEXT and RLD records. */

int text_flush(TEXT_RLD *tr)
{

	if(tr->txt_offset > 4)
	{
		if(!writerec(tr->fp, tr->text, tr->txt_offset))
			return 0;
	}

	if(tr->rld_offset > 2)
	{
		if(!writerec(tr->fp, tr->rld, tr->rld_offset))
			return 0;
	}

	return 1;
}

/* Used to ensure that TEXT and RLD information will be in adjacent
   records.  If not enough space exists in either buffer, both are
   flushed. */

static int text_fit(TEXT_RLD *tr, unsigned addr,
					int txtsize, int rldsize)
{
	if(tr->txt_offset + txtsize <= sizeof(tr->text) &&
		tr->rld_offset + rldsize <= sizeof(tr->rld) &&
		(txtsize == 0 || tr->txt_addr + tr->txt_offset - 4 == addr))
		return 1;		/* All's well. */

	if(!text_flush(tr))
		return 0;
	text_init(tr, tr->fp, addr);

	return 1;
}

/* text_word_i - internal text_word.  Used when buffer space is
   already assured. */

static void text_word_i(TEXT_RLD *tr, unsigned w, int size)
{
	tr->text[tr->txt_offset++] = w & 0xff;
	if(size > 1)
		tr->text[tr->txt_offset++] = (w >> 8) & 0xff;
}

/* text_word - write constant word to text */

int text_word(TEXT_RLD *tr, unsigned *addr, int size, unsigned word)
{
	if(!text_fit(tr, *addr, size, 0))
		return 0;

	text_word_i(tr, word, size);

	*addr += size;				/* Update the caller's DOT */
	return 1;					/* say "ok". */
}

/* rld_word - adds a word to the RLD information. */

static void rld_word(TEXT_RLD *tr, unsigned wd)
{
	tr->rld[tr->rld_offset++] = wd & 0xff;
	tr->rld[tr->rld_offset++] = (wd >> 8) & 0xff;
}

/* rld_byte - adds a byte to rld information. */

static void rld_byte(TEXT_RLD *tr, unsigned byte)
{
	tr->rld[tr->rld_offset++] = byte & 0xff;
}

/* rld_code - write the typical RLD first-word code.  Encodes the
   given address as the offset into the prior TEXT record. */

static void rld_code(TEXT_RLD *tr, unsigned code, unsigned addr, int size)
{
	unsigned offset = addr - tr->txt_addr + 4;
	rld_word(tr, code | offset << 8 | (size == 1 ? 0200 : 0));
}

/* rld_code_naddr - typical RLD entries refer to a text address.  This
   one is used when the RLD code does not. */

static void rld_code_naddr(TEXT_RLD *tr, unsigned code, int size)
{
	rld_word(tr, code | (size == 1 ? 0200 : 0));
}

/* write a word with a psect-relative value */

int text_internal_word(TEXT_RLD *tr, unsigned *addr,
					   int size, unsigned word)
{
	if(!text_fit(tr, *addr, size, 4))
		return 0;

	text_word_i(tr, word, size);
	rld_code(tr, RLD_INT, *addr, size);
	rld_word(tr, word);

	*addr += size;

	return 1;
}

/* write a word which is an absolute reference to a global symbol */

int text_global_word(TEXT_RLD *tr, unsigned *addr,
					 int size, unsigned word, char *global)
{
	unsigned radtbl[2];

	if(!text_fit(tr, *addr, size, 6))
		return 0;

	text_word_i(tr, word, size);
	rld_code(tr, RLD_GLOBAL, *addr, size);

	rad50x2(global, radtbl);
	rld_word(tr, radtbl[0]);
	rld_word(tr, radtbl[1]);

	*addr += size;

	return 1;
}

/* Write a word which is a PC-relative reference to an absolute address */

int text_displaced_word(TEXT_RLD *tr,
						unsigned *addr, int size, unsigned word)
{
	if(!text_fit(tr, *addr, size, 4))
		return 0;

	text_word_i(tr, word, size);
	rld_code(tr, RLD_INT_DISP, *addr, size);
	rld_word(tr, word);

	*addr += size;

	return 1;
}

/* write a word which is a PC-relative reference to a global symbol */

int text_global_displaced_word(TEXT_RLD *tr,
							   unsigned *addr, int size,
							   unsigned word, char *global)
{
	unsigned radtbl[2];

	if(!text_fit(tr, *addr, size, 6))
		return 0;

	text_word_i(tr, word, size);
	rld_code(tr, RLD_GLOBAL_DISP, *addr, size);

	rad50x2(global, radtbl);
	rld_word(tr, radtbl[0]);
	rld_word(tr, radtbl[1]);

	*addr += size;

	return 1;
}

/* write a word which is an absolute reference to a global symbol plus
   an offset */

/* Optimizes to text_global_word when the offset is zero. */

int text_global_offset_word(TEXT_RLD *tr,
							unsigned *addr, int size,
							unsigned word, char *global)
{
	unsigned radtbl[2];

	if(word == 0)
		return text_global_word(tr, addr, size, word, global);
	
	if(!text_fit(tr, *addr, size, 8))
		return 0;

	text_word_i(tr, word, size);

	rld_code(tr, RLD_GLOBAL_OFFSET, *addr, size);

	rad50x2(global, radtbl);
	rld_word(tr, radtbl[0]);
	rld_word(tr, radtbl[1]);
	rld_word(tr, word);

	*addr += size;

	return 1;
}

/* write a word which is a PC-relative reference to a global symbol
   plus an offset */

/* Optimizes to text_global_displaced_word when the offset is zero. */

int text_global_displaced_offset_word(TEXT_RLD *tr,
									  unsigned *addr, int size,
									  unsigned word, char *global)
{
	unsigned radtbl[2];

	if(word == 0)
		return text_global_displaced_word(tr, addr, size, word, global);
	
	if(!text_fit(tr, *addr, size, 8))
		return 0;

	text_word_i(tr, word, size);
	rld_code(tr, RLD_GLOBAL_OFFSET_DISP, *addr, size);

	rad50x2(global, radtbl);
	rld_word(tr, radtbl[0]);
	rld_word(tr, radtbl[1]);
	rld_word(tr, word);

	*addr += size;

	return 1;
}

/* Define current program counter, plus PSECT */
/* Different because it must be the last RLD entry in a block.  That's
   because TEXT records themselves contain the current text
   address. */

int text_define_location(TEXT_RLD *tr, char *name, unsigned *addr)
{
	unsigned radtbl[2];

	if(!text_fit(tr, *addr, 0, 8))	/* No text space used */
		return 0;

	rld_code_naddr(tr, RLD_LOCDEF, 2);	/* RLD code for "location
										   counter def" with no offset */

	rad50x2(name, radtbl);
	rld_word(tr, radtbl[0]);	/* Set current section name */
	rld_word(tr, radtbl[1]);
	rld_word(tr, *addr);		/* Set current location addr */

	if(!text_flush(tr))		/* Flush that block out. */
		return 0;

	text_init(tr, tr->fp, *addr);		/* Set new text address */

	return 1;
}

/* Modify current program counter, assuming current PSECT */
/* Location counter modification is similarly weird */
/* (I wonder - why is this RLD code even here?  TEXT records contain
   thair own start address.) */

int text_modify_location(TEXT_RLD *tr, unsigned *addr)
{
	if(!text_fit(tr, *addr, 0, 4))	/* No text space used */
		return 0;

	rld_code_naddr(tr, RLD_LOCMOD, 2);	/* RLD code for "location
										   counter mod" with no offset */
	rld_word(tr, *addr);		/* Set current location addr */

	if(!text_flush(tr))		/* Flush that block out. */
		return 0;
	text_init(tr, tr->fp, *addr);		/* Set new text address */

	return 1;
}

/* write two words containing program limits (the .LIMIT directive) */

int text_limits(TEXT_RLD *tr, unsigned *addr)
{
	if(!text_fit(tr, *addr, 4, 2))
		return 0;

	text_word_i(tr, 0, 2);
	text_word_i(tr, 0, 2);
	rld_code(tr, RLD_LIMITS, *addr, 2);

	*addr += 4;

	return 1;
}

/* write a word which is the start address of a different PSECT */

int text_psect_word(TEXT_RLD *tr,
					unsigned *addr, int size,
					unsigned word, char *name)
{
	unsigned radtbl[2];

	if(!text_fit(tr, *addr, size, 6))
		return 0;

	text_word_i(tr, word, size);
	
	rld_code(tr, RLD_PSECT, *addr, size);
	
	rad50x2(name, radtbl);
	rld_word(tr, radtbl[0]);
	rld_word(tr, radtbl[1]);

	*addr += size;

	return 1;
}

/* write a word which is an offset from the start of a different PSECT */

/* Optimizes to text_psect_word when offset is zero */

int text_psect_offset_word(TEXT_RLD *tr,
						   unsigned *addr, int size,
						   unsigned word, char *name)
{
	unsigned radtbl[2];

	if(word == 0)
		return text_psect_word(tr, addr, size, word, name);

	if(!text_fit(tr, *addr, size, 8))
		return 0;

	text_word_i(tr, word, size);

	rld_code(tr, RLD_PSECT_OFFSET, *addr, size);

	rad50x2(name, radtbl);
	rld_word(tr, radtbl[0]);
	rld_word(tr, radtbl[1]);
	rld_word(tr, word);

	*addr += size;

	return 1;
}

/* write a word which is the address of a different PSECT, PC-relative */

int text_psect_displaced_word(TEXT_RLD *tr, unsigned *addr,
							  int size, unsigned word, char *name)
{
	unsigned radtbl[2];

	if(!text_fit(tr, *addr, size, 6))
		return 0;
	
	text_word_i(tr, word, size);
	
	rld_code(tr, RLD_PSECT_DISP, *addr, size);
	
	rad50x2(name, radtbl);
	rld_word(tr, radtbl[0]);
	rld_word(tr, radtbl[1]);

	*addr += size;

	return 1;
}

/* write a word which is an offset from the address of a different
   PSECT, PC-relative */

/* Optimizes to text_psect_displaced_word when offset is zero */

int text_psect_displaced_offset_word(TEXT_RLD *tr,
									 unsigned *addr, int size,
									 unsigned word, char *name)
{
	unsigned radtbl[2];

	if(word == 0)
		return text_psect_displaced_word(tr, addr, size, word, name);

	if(!text_fit(tr, *addr, size, 8))
		return 0;
	
	text_word_i(tr, word, size);
	
	rld_code(tr, RLD_PSECT_OFFSET_DISP, *addr, size);
	
	rad50x2(name, radtbl);
	rld_word(tr, radtbl[0]);
	rld_word(tr, radtbl[1]);
	rld_word(tr, word);

	*addr += size;

	return 1;
}

/* complex relocation! */

/* A complex relocation expression is where a piece of code is fed to
   the linker asking it to do some math for you, and store the result
   in a program word. The code is a stack-based language. */

/* complex_begin initializes a TEXT_COMPLEX */

void text_complex_begin(TEXT_COMPLEX *tx)
{
	tx->len = 0;
}

/* text_complex_fit checks if a complex expression will fit and
   returns a pointer to it's location */

static char *text_complex_fit(TEXT_COMPLEX *tx, int size)
{
	int len;

	if(tx->len + size > sizeof(tx->accum))
		return NULL;			/* Expression has grown too complex. */

	len = tx->len;

	tx->len += size;

	return tx->accum + len;
}

/* text_complex_byte stores a single byte. */

static int text_complex_byte(TEXT_COMPLEX *tx, unsigned byte)
{
	char *cp = text_complex_fit(tx, 1);
	if(!cp)
		return 0;
	*cp = byte;
	return 1;
}

/* text_complex_add - add top two stack elements */

int text_complex_add(TEXT_COMPLEX *tx)
{
	return text_complex_byte(tx, CPLX_ADD);
}

/* text_complex_sub - subtract top two stack elements. */
/* You know, I think these function labels are self-explanatory... */

int text_complex_sub(TEXT_COMPLEX *tx)
{
	return text_complex_byte(tx, CPLX_SUB);
}

int text_complex_mul(TEXT_COMPLEX *tx)
{
	return text_complex_byte(tx, CPLX_MUL);
}

int text_complex_div(TEXT_COMPLEX *tx)
{
	return text_complex_byte(tx, CPLX_DIV);
}

int text_complex_and(TEXT_COMPLEX *tx)
{
	return text_complex_byte(tx, CPLX_AND);
}

int text_complex_or(TEXT_COMPLEX *tx)
{
	return text_complex_byte(tx, CPLX_OR);
}

int text_complex_xor(TEXT_COMPLEX *tx)
{
	return text_complex_byte(tx, CPLX_XOR);
}

int text_complex_com(TEXT_COMPLEX *tx)
{
	return text_complex_byte(tx, CPLX_COM);
}

int text_complex_neg(TEXT_COMPLEX *tx)
{
	return text_complex_byte(tx, CPLX_NEG);
}

/* text_complex_lit pushes a literal value to the stack. */

int text_complex_lit(TEXT_COMPLEX *tx, unsigned word)
{
	char *cp = text_complex_fit(tx, 3);
	if(!cp)
		return 0;
	*cp++ = CPLX_CONST;
	*cp++ = word & 0xff;
	*cp = (word >> 8) & 0xff;
	return 1;
}

/* text_complex_global pushes the value of a global variable to the
   stack */

int text_complex_global(TEXT_COMPLEX *tx, char *name)
{
	unsigned radtbl[2];
	char *cp = text_complex_fit(tx, 5);
	if(!cp)
		return 0;

	rad50x2(name, radtbl);
	*cp++ = CPLX_GLOBAL;
	*cp++ = radtbl[0] & 0xff;
	*cp++ = (radtbl[0] >> 8) & 0xff;
	*cp++ = radtbl[1] & 0xff;
	*cp = (radtbl[1] >> 8) & 0xff;
	return 1;
}

/* text_complex_psect pushes the value of an offset into a PSECT to
   the stack. */

/* What was not documented in the Software Support manual is that
   PSECT "sect" numbers are assigned in the order they appear in the
   source program, and the order they appear in the GSD.  i.e. the
   first PSECT GSD is assigned sector 0 (which is always the default
   absolute section so that's a bad example), the next sector 1,
   etc. */

int text_complex_psect(TEXT_COMPLEX *tx, unsigned sect, unsigned offset)
{
	char *cp = text_complex_fit(tx, 4);
	if(!cp)
		return 0;
	*cp++ = CPLX_REL;
	*cp++ = sect & 0xff;
	*cp++ = offset & 0xff;
	*cp = (offset >> 8) & 0xff;
	return 1;
}

/* text_complex_commit - store the result of the complex expression
   and end the RLD code. */

int text_complex_commit(TEXT_RLD *tr, unsigned *addr,
						int size, TEXT_COMPLEX *tx, unsigned word)
{
	int i;

	text_complex_byte(tx, CPLX_STORE);

	if(!text_fit(tr, *addr, size, tx->len + 2))
		return 0;

	rld_code(tr, RLD_COMPLEX, *addr, size);

	for(i = 0; i < tx->len; i++)
		rld_byte(tr, tx->accum[i]);

	text_word_i(tr, word, size);

	*addr += size;

	return 1;
}

/* text_complex_commit_displaced - store the result of the complex
   expression, relative to the current PC, and end the RLD code */

int text_complex_commit_displaced(TEXT_RLD *tr,
								  unsigned *addr, int size,
								  TEXT_COMPLEX *tx, unsigned word)
{
	int i;

	text_complex_byte(tx, CPLX_STORE_DISP);

	if(!text_fit(tr, *addr, size, tx->len + 2))
		return 0;

	rld_code(tr, RLD_COMPLEX, *addr, size);

	for(i = 0; i < tx->len; i++)
		rld_byte(tr, tx->accum[i]);

	text_word_i(tr, word, size);

	*addr += size;

	return 1;
}

/* Write end-of-object-module to file. */

int write_endmod(FILE *fp)
{
	char endmod[2] = { OBJ_ENDMOD, 0 };
	return writerec(fp, endmod, 2);
}
