# (This makefile is for operating systems other than Windows,
# or compilers other than Microsoft's. For MS builds, use the
# .mak files).
#
# CC Command
#
# Note: -O2 is sometimes broken in GCC when setjump/longjump is being
# used. Try -O2 only with released simulators.
#

CC    = gcc -O0 -lm -I .
#CC   = gcc -O2 -g -lm -I .

BIN   =

IOLIB_DEP = util_io.c util_io.h
IOLIB_SRC = util_io.c

#
# Build everything
#

all : 	${BIN}asm1130 ${BIN}bindump ${BIN}checkdisk \
	${BIN}diskview ${BIN}mkboot ${BIN}viewdeck \
        ${BIN}disklist ${BIN}punches

#
# Individual builds
#

${BIN}asm1130   : asm1130.c   ${IOLIB_DEP}
	${CC}     asm1130.c   ${IOLIB_SRC} -o $@

${BIN}bindump   : bindump.c   ${IOLIB_DEP}
	${CC}     bindump.c   ${IOLIB_SRC} -o $@

${BIN}checkdisk : checkdisk.c ${IOLIB_DEP}
	${CC}     checkdisk.c ${IOLIB_SRC} -o $@

${BIN}disklist  : disklist.c  ${IOLIB_DEP}
	${CC}     disklist.c  ${IOLIB_SRC} -o $@

${BIN}diskview  : diskview.c  ${IOLIB_DEP}
	${CC}     diskview.c  ${IOLIB_SRC} -o $@

${BIN}mkboot    : mkboot.c    ${IOLIB_DEP}
	${CC}     mkboot.c    ${IOLIB_SRC} -o $@

${BIN}viewdeck  : viewdeck.c  ${IOLIB_DEP}
	${CC}     viewdeck.c  ${IOLIB_SRC} -o $@

${BIN}punches  : punches.c  ${IOLIB_DEP}
	${CC}     punches.c  ${IOLIB_SRC} -o $@
