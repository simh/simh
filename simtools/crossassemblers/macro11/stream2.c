/* functions for managing a stack of file and buffer input streams. */

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
#include <ctype.h>
#include <stdarg.h>

#include "macro11.h"

#include "stream2.h"

/* BUFFER functions */

/* new_buffer allocates a new buffer */

BUFFER *new_buffer(void)
{
	BUFFER *buf = memcheck(malloc(sizeof(BUFFER)));
	buf->length = 0;
	buf->size = 0;
	buf->use = 1;
	buf->buffer = NULL;
	return buf;
}

/* buffer_resize makes the buffer at least the requested size. */
/* If the buffer is already larger, then it will attempt */
/* to shrink it. */

void buffer_resize(BUFFER *buff, int size)
{
	buff->size = size;
	buff->length = size;

	if(size == 0)
	{
		free(buff->buffer);
		buff->buffer = NULL;
	}
	else
	{
		if(buff->buffer == NULL)
			buff->buffer = memcheck(malloc(buff->size));
		else
			buff->buffer = memcheck(realloc(buff->buffer, buff->size));
	}
}

/* buffer_clone makes a copy of a buffer */
/* Basically it increases the use count */

BUFFER *buffer_clone(BUFFER *from)
{
	if(from)
		from->use++;
	return from;
}

/* buffer_free frees a buffer */
/* It decreases the use count, and if zero, */
/* frees the memory. */

void buffer_free(BUFFER *buf)
{
	if(buf)
	{
		if(--(buf->use) == 0)
		{
			free(buf->buffer);
			free(buf);
		}
	}
}

/* Append characters to the buffer. */

void buffer_appendn(BUFFER *buf, char *str, int len)
{
	int needed = buf->length + len + 1;

	if(needed >= buf->size)
	{
		buf->size = needed + GROWBUF_INCR;

		if(buf->buffer == NULL)
			buf->buffer = memcheck(malloc(buf->size));
		else
			buf->buffer = memcheck(realloc(buf->buffer, buf->size));
	}

	memcpy(buf->buffer + buf->length, str, len);
	buf->length += len;
	buf->buffer[buf->length] = 0;
}

/* append a text line (zero or newline-delimited) */

void buffer_append_line(BUFFER *buf, char *str)
{
	char *nl;
	if((nl = strchr(str, '\n')) != NULL)
		buffer_appendn(buf, str, nl - str + 1);
	else
		buffer_appendn(buf, str, strlen(str));
}

/* Base STREAM class methods */

/* stream_construct initializes a newly allocated STREAM */

void stream_construct(STREAM *str, char *name)
{
	str->line = 0;
	str->name = memcheck(strdup(name));
	str->next = NULL;
	str->vtbl = NULL;
}

/* stream_delete destroys and deletes (frees) a STREAM */

void stream_delete(STREAM *str)
{
	free(str->name);
	free(str);
}

/* *** class BUFFER_STREAM implementation */

/* STREAM::gets for a buffer stream */

char *buffer_stream_gets(STREAM *str)
{
	char *nl;
	char *cp;
	BUFFER_STREAM *bstr = (BUFFER_STREAM *)str;
	BUFFER *buf = bstr->buffer;

	if(buf == NULL)
		return NULL;		/* No buffer */
	
	if(bstr->offset >= buf->length)
		return NULL;

	cp = buf->buffer + bstr->offset;

	/* Find the next line in preparation for the next call */

	nl = memchr(cp, '\n', buf->length - bstr->offset);

	if(nl)
		nl++;

	bstr->offset = nl - buf->buffer;
	str->line++;

	return cp;
}

/* STREAM::close for a buffer stream */

void buffer_stream_delete(STREAM *str)
{
	BUFFER_STREAM *bstr = (BUFFER_STREAM *)str;
	buffer_free(bstr->buffer);
	stream_delete(str);
}

/* STREAM::rewind for a buffer stream */

void buffer_stream_rewind(STREAM *str)
{
	BUFFER_STREAM *bstr = (BUFFER_STREAM *)str;
	bstr->offset = 0;
	str->line = 0;
}

/* BUFFER_STREAM vtbl */

STREAM_VTBL buffer_stream_vtbl = { buffer_stream_delete,
								   buffer_stream_gets,
								   buffer_stream_rewind };

void buffer_stream_construct(BUFFER_STREAM *bstr, BUFFER *buf, char *name)
{
	bstr->stream.vtbl = &buffer_stream_vtbl;
	
	bstr->stream.name = memcheck(strdup(name));

	bstr->buffer = buffer_clone(buf);
	bstr->offset = 0;
	bstr->stream.line = 0;
}

void buffer_stream_set_buffer(BUFFER_STREAM *bstr, BUFFER *buf)
{
	if(bstr->buffer)
		buffer_free(bstr->buffer);
	bstr->buffer = buffer_clone(buf);
	bstr->offset = 0;
}

/* new_buffer_stream clones the given buffer, gives it the name, */
/* and creates a BUFFER_STREAM to reference it */

STREAM *new_buffer_stream(BUFFER *buf, char *name)
{
	BUFFER_STREAM *bstr = memcheck(malloc(sizeof(BUFFER_STREAM)));

	buffer_stream_construct(bstr, buf, name);
	return &bstr->stream;
}

/* *** FILE_STREAM implementation */

/* Implement STREAM::gets for a file stream */

static char *file_gets(STREAM *str)
{
	int i, c;
	FILE_STREAM *fstr = (FILE_STREAM *)str;

	if(fstr->fp == NULL)
		return NULL;

	if(feof(fstr->fp))
		return NULL;

	/* Read single characters, end of line when '\n' or '\f' hit */

	i = 0;
	while(c = fgetc(fstr->fp),
		c != '\n' && c != '\f' && c != EOF)
	{
		if(c == 0)
			continue;				/* Don't buffer zeros */
		if(c == '\r')
			continue;				/* Don't buffer carriage returns either */
		if(i < STREAM_BUFFER_SIZE - 2)
			fstr->buffer[i++] = c;
	}

	fstr->buffer[i++] = '\n';		/* Silently transform formfeeds
									   into newlines */
	fstr->buffer[i] = 0;

	if(c == '\n')
		fstr->stream.line++;		/* Count a line */

	return fstr->buffer;
}

/* Implement STREAM::destroy for a file stream */

void file_destroy(STREAM *str)
{
	FILE_STREAM *fstr = (FILE_STREAM *)str;
	fclose(fstr->fp);
	free(fstr->buffer);
	stream_delete(str);
}

/* Implement STREAM::rewind for a file stream */

void file_rewind(STREAM *str)
{
	FILE_STREAM *fstr = (FILE_STREAM *)str;
	rewind(fstr->fp);
	str->line = 0;
}

static STREAM_VTBL file_stream_vtbl = { file_destroy, file_gets,
										file_rewind };

/* Prepare and open a stream from a file. */

STREAM *new_file_stream(char *filename)
{
	FILE *fp;
	FILE_STREAM *str;

	fp = fopen(filename, "r");
	if(fp == NULL)
		return NULL;

	str = memcheck(malloc(sizeof(FILE_STREAM)));

	str->stream.vtbl = &file_stream_vtbl;
	str->stream.name = memcheck(strdup(filename));
	str->buffer = memcheck(malloc(STREAM_BUFFER_SIZE));
	str->fp = fp;
	str->stream.line = 0;

	return &str->stream;
}

/* STACK functions */

/* stack_init prepares a stack */

void stack_init(STACK *stack)
{
	stack->top = NULL;			/* Too simple */
}

/* stack_pop removes and deletes the topmost STRAM on the stack */

void stack_pop(STACK *stack)
{
	STREAM *top = stack->top;
	STREAM *next = top->next;

	top->vtbl->delete(top);
	stack->top = next;
}

/* stack_push pushes a STREAM onto the top of the stack */

void stack_push(STACK *stack, STREAM *str)
{
	str->next = stack->top;
	stack->top = str;
}

/* stack_gets calls vtbl->gets for the topmost stack entry.  When
   topmost streams indicate they're exhausted, they are popped and
   deleted, until the stack is exhausted. */

char *stack_gets(STACK *stack)
{
	char *line;

	if(stack->top == NULL)
		return NULL;

	while((line = stack->top->vtbl->gets(stack->top)) == NULL)
	{
		stack_pop(stack);
		if(stack->top == NULL)
			return NULL;
	}

	return line;
}
