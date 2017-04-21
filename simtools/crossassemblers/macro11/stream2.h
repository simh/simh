#ifndef STREAM2_H
#define STREAM2_H

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

struct stream;

typedef struct stream_vtbl
{
	void (*delete)(struct stream *stream);		// Destructor
	char *(*gets)(struct stream *stream);		// "gets" function
	void (*rewind)(struct stream *stream);		// "rewind" function
} STREAM_VTBL;

typedef struct stream
{
	STREAM_VTBL *vtbl;		// Pointer to dispatch table
	char *name;				// Stream name
	int line;				// Current line number in stream
	struct stream *next;	// Next stream in stack
} STREAM;

typedef struct file_stream
{
	STREAM stream;			// Base class
	FILE *fp;				// File pointer
	char *buffer;			// Line buffer
} FILE_STREAM;

typedef struct buffer
{
	char *buffer;			// Pointer to text
	int size;				// Size of buffer
	int length;				// Occupied size of buffer
	int use;				// Number of users of buffer
} BUFFER;

#define GROWBUF_INCR 1024	// Buffers grow by leaps and bounds

typedef struct buffer_stream
{
	STREAM stream;			// Base class
	BUFFER *buffer;			// text buffer
	int offset;				// Current read offset
} BUFFER_STREAM;

typedef struct stack
{
	STREAM *top;			// Top of stacked stream pieces
} STACK;

#define STREAM_BUFFER_SIZE 1024	// This limits the max size of an input line.
BUFFER *new_buffer(void);
BUFFER *buffer_clone(BUFFER *from);
void buffer_resize(BUFFER *buff, int size);
void buffer_free(BUFFER *buf);
void buffer_appendn(BUFFER *buf, char *str, int len);
void buffer_append_line(BUFFER *buf, char *str);

STREAM *new_buffer_stream(BUFFER *buf, char *name);
void buffer_stream_set_buffer(BUFFER_STREAM *bstr, BUFFER *buf);

/* Provide these so that macro11 can derive from a BUFFER_STREAM */
extern STREAM_VTBL buffer_stream_vtbl;
void buffer_stream_construct(BUFFER_STREAM *bstr, BUFFER *buf, char *name);
char *buffer_stream_gets(STREAM *str);
void buffer_stream_delete(STREAM *str);
void buffer_stream_rewind(STREAM *str);

STREAM *new_file_stream(char *filename);

void stack_init(STACK *stack);
void stack_push(STACK *stack, STREAM *str);
void stack_pop(STACK *stack);
char *stack_gets(STACK *stack);

#endif /* STREAM2_H */
