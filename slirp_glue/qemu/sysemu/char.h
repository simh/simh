#ifndef QEMU_CHAR_H
#define QEMU_CHAR_H

#include "qemu-common.h"

/**
 * @qemu_chr_fe_write:
 *
 * Write data to a character backend from the front end.  This function
 * will send data from the front end to the back end.  This function
 * is thread-safe.
 *
 * @buf the data
 * @len the number of bytes to send
 *
 * Returns: the number of bytes consumed
 */
int qemu_chr_fe_write(CharDriverState *s, const uint8_t *buf, int len);


#endif
