#ifndef SLIRP_DEBUG_H
#define SLIRP_DEBUG_H

/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#define DBG_CALL 0x1
#define DBG_MISC 0x2
#define DBG_ERROR 0x4

extern int slirp_debug;

#ifndef USE_SIMH_SLIRP_DEBUG  /* simh build indicator */

#ifdef DEBUG

#define dfd stderr

#define DEBUG_CALL(x) do {if (slirp_debug & DBG_CALL) { fprintf (dfd, "%s...\n", x); fflush(dfd); };} while (0)
#define DEBUG_ARG(x, y) do {if (slirp_debug & DBG_CALL) { fprintf (dfd, x, y); _sim_debug_device (slirp_dbit, slirp_dptr, "\n"); fflush(dfd); };} while (0)
#define DEBUG_ARGS(...) do {if (slirp_debug & DBG_CALL) { fprintf (dfd, ##  __VA_ARGS__); fflush(dfd); };} while (0)
#define DEBUG_MISC(...) do {if (slirp_debug & DBG_MISC) { fprintf (dfd, ##  __VA_ARGS__); fflush(dfd); };} while (0)
#define DEBUG_ERROR(...) do {if (slirp_debug & DBG_ERROR) { fprintf (dfd, ##  __VA_ARGS__); fflush(dfd); };} while (0)
#define DPRINTF(fmt, ...) do {if (slirp_debug & DBG_CALL) { fprintf (dfd, fmt, ##  __VA_ARGS__); fflush(dfd);} while (0)

#else

#define DEBUG_CALL(x) do {} while (0)
#define DEBUG_ARG(x, y) do {} while (0)
#define DEBUG_ARGS(...) do {} while (0)
#define DEBUG_MISC(...) do {} while (0)
#define DEBUG_ERROR(...) do {} while (0)
#define DPRINTF(fmt, ...) do {} while (0)

#endif

#else /* defined(USE_SIMH_SLIRP_DEBUG) */

#include <stdio.h>
#define DEVICE void

#if defined(__cplusplus)
extern "C" {
#endif
extern void *slirp_dptr;
extern unsigned int slirp_dbit;

extern void _sim_debug_device (unsigned int dbits, DEVICE* dptr, const char* fmt, ...);

#define DEBUG_CALL(x) do {if (slirp_debug & DBG_CALL) { _sim_debug_device (slirp_dbit, slirp_dptr, "%s...\n", x); };} while (0)
#define DEBUG_ARG(x, y) do {if (slirp_debug & DBG_CALL) {_sim_debug_device (slirp_dbit, slirp_dptr, x, y); _sim_debug_device (slirp_dbit, slirp_dptr, "\n"); };} while (0)
#define DEBUG_ARGS(...) do {if (slirp_debug & DBG_CALL) { _sim_debug_device (slirp_dbit, slirp_dptr, ##  __VA_ARGS__); };} while (0)
#define DEBUG_MISC(...) do {if (slirp_debug & DBG_MISC) { _sim_debug_device (slirp_dbit, slirp_dptr, ##  __VA_ARGS__); };} while (0)
#define DEBUG_ERROR(...) do {if (slirp_debug & DBG_ERROR) { _sim_debug_device (slirp_dbit, slirp_dptr, ##  __VA_ARGS__); };} while (0)
#define DPRINTF(fmt, ...) do {if (slirp_debug & DBG_CALL) { _sim_debug_device (slirp_dbit, slirp_dptr, fmt, ##  __VA_ARGS__); };} while (0)

#if defined(__cplusplus)
}
#endif

#endif

#endif
