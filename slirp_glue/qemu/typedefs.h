#ifndef QEMU_TYPEDEFS_H
#define QEMU_TYPEDEFS_H

/* A load of opaque types so that device init declarations don't have to
   pull in all the real definitions.  */
struct Monitor;
typedef struct Monitor Monitor;
typedef struct CharDriverState CharDriverState;
typedef struct QEMUFile QEMUFile;

#endif /* QEMU_TYPEDEFS_H */
