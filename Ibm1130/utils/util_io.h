// util_io.h - definitions for simulator io routines

#ifndef FILE
#    include <stdio.h>
#endif

void util_io_init(void);
size_t fxread  (void *bptr, size_t size, size_t count, FILE *fptr);
size_t fxwrite (void *bptr, size_t size, size_t count, FILE *fptr);

