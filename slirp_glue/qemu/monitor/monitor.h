#ifndef MONITOR_H
#define MONITOR_H

#include "qemu-common.h"

void monitor_printf(Monitor *mon, const char *fmt, ...) GCC_FMT_ATTR(2, 3);

#endif /* !MONITOR_H */
