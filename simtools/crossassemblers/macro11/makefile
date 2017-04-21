CFLAGS = -O -g

MACRO11_SRCS = macro11.c mlb.c object.c stream2.c util.c rad50.c

MACRO11_OBJS = $(MACRO11_SRCS:.c=.o)

DUMPOBJ_SRCS = dumpobj.c rad50.c

DUMPOBJ_OBJS = $(DUMPOBJ_SRCS:.c=.o)

ALL_SRCS = $(MACRO11_SRCS) $(DUMPOBJ_SRCS)

all: macro11 dumpobj

tags: macro11 dumpobj
	ctags *.c *.h

macro11: $(MACRO11_OBJS) makefile
	$(CC) $(CFLAGS) -o macro11 $(MACRO11_OBJS) -lm

dumpobj: $(DUMPOBJ_OBJS) makefile
	$(CC) $(CFLAGS) -o dumpobj $(DUMPOBJ_OBJS)

MACRO11_OBJS: makefile
DUMPOBJ_OBJS: makefile

clean:
	-rm -f $(MACRO11_OBJS) $(DUMPOBJ_OBJS) macro11 dumpobj

macro11.o: macro11.c macro11.h rad50.h object.h  stream2.h \
 mlb.h util.h
mlb.o: mlb.c  rad50.h stream2.h mlb.h macro11.h util.h
object.o: object.c rad50.h object.h 
stream2.o: stream2.c macro11.h  stream2.h
util.o: util.c util.h
rad50.o: rad50.c rad50.h
dumpobj.o: dumpobj.c rad50.h util.h
rad50.o: rad50.c rad50.h
