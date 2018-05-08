#ifndef GLIB_H
#define GLIB_H

#include <stdlib.h>
#if defined(_WIN32)
#include <winsock2.h>
#endif

typedef char gchar;
typedef unsigned int guint;
typedef unsigned short gushort;
typedef void* gpointer;
typedef unsigned long gsize;
typedef const void *gconstpointer;
typedef int gint;
typedef gint gboolean;
typedef struct _GSource {int dummy;} GSource;
typedef struct GPollFD {
#if defined(_WIN32)
  SOCKET        fd;
#else
  gint      fd;
#endif
  gushort   events;
  gushort   revents;
} GPollFD;
typedef struct _GArray {
    gchar *data;
    guint len;
} GArray;

gpointer g_malloc (gsize n_bytes);
gpointer g_malloc0 (gsize n_bytes);
gpointer g_realloc (gpointer mem, gsize n_bytes);
void g_free (gpointer mem);
gchar *g_strdup (const gchar *str);

typedef enum {
    /* Flags */
    G_LOG_FLAG_RECURSION = 1 << 0,
    G_LOG_FLAG_FATAL     = 1 << 1,
    /* Levels */
    G_LOG_LEVEL_ERROR    = 1 << 2,
    G_LOG_LEVEL_CRITICAL = 1 << 3,
    G_LOG_LEVEL_WARNING  = 1 << 4,
    G_LOG_LEVEL_MESSAGE  = 1 << 5,
    G_LOG_LEVEL_INFO     = 1 << 6,
    G_LOG_LEVEL_DEBUG    = 1 << 7,
    G_LOG_LEVEL_MASK     = ~(G_LOG_FLAG_RECURSION | G_LOG_FLAG_FATAL)
    } GLogLevelFlags;

#define GLIB_SYSDEF_POLLIN =1
#define GLIB_SYSDEF_POLLOUT =4
#define GLIB_SYSDEF_POLLPRI =2
#define GLIB_SYSDEF_POLLHUP =16
#define GLIB_SYSDEF_POLLERR =8
#define GLIB_SYSDEF_POLLNVAL =32

typedef enum {
    G_IO_IN     GLIB_SYSDEF_POLLIN,    /* There is data to read. */
    G_IO_OUT    GLIB_SYSDEF_POLLOUT,   /* Data can be written (without blocking). */
    G_IO_PRI    GLIB_SYSDEF_POLLPRI,   /* There is urgent data to read. */
    G_IO_ERR    GLIB_SYSDEF_POLLERR,   /* Error condition. */
    G_IO_HUP    GLIB_SYSDEF_POLLHUP,   /* Hung up (the connection has been broken, usually for pipes and sockets). */
    G_IO_NVAL   GLIB_SYSDEF_POLLNVAL   /* Invalid request. The file descriptor is not open. */
    } GIOCondition;
void g_log (const gchar *log_domain, GLogLevelFlags log_level, const gchar *format, ...);
#if !defined(G_LOG_DOMAIN)
#define G_LOG_DOMAIN ((gchar *)NULL)
#endif
#define g_warning(...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, __VA_ARGS__)

#define g_new(struct_type, n_structs) g_malloc (sizeof(struct_type) * n_structs)


#define g_array_append_val(array, data) g_array_append_vals (array, &data, 1) 
#define g_array_new(zero_terminated, clear, element_size) g_array_sized_new(zero_terminated, clear, element_size, 0)

GArray *
g_array_sized_new (gboolean zero_terminated,
                   gboolean clear,
                   guint element_size,
                   guint reserved_size);
gchar *
g_array_free (GArray *array,
              gboolean free_segment);

#define g_array_index(array, type, index) (((type *)(void *)((array)->data)))[index]

GArray *
g_array_set_size (GArray *array,
                  guint length);
GArray *
g_array_append_vals (GArray *array,
                     gconstpointer data,
                     guint len);
guint
g_array_get_element_size (GArray *array);


#endif
