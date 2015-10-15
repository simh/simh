#ifndef INTTYPES_H
#define INTTYPES_H

#include <stdint.h>

#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef __int32 ssize_t;
#endif
#endif
