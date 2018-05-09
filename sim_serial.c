/* sim_serial.c: OS-dependent serial port routines

   Copyright (c) 2008, J. David Bryan, Mark Pizzolato

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   The author gratefully acknowledges the assistance of Holger Veit with the
   UNIX-specific code and testing.

   07-Oct-08    JDB     [serial] Created file
   22-Apr-12    MP      Adapted from code originally written by J. David Bryan


   This module provides OS-dependent routines to access serial ports on the host
   machine.  The terminal multiplexer library uses these routines to provide
   serial connections to simulated terminal interfaces.

   Currently, the module supports Windows and UNIX.  Use on other systems
   returns error codes indicating that the functions failed, inhibiting serial
   port support in SIMH.

   The following routines are provided:

     sim_open_serial        open a serial port
     sim_config_serial      change baud rate and character framing configuration
     sim_control_serial     manipulate and/or return the modem bits on a serial port
     sim_read_serial        read from a serial port
     sim_write_serial       write to a serial port
     sim_close_serial       close a serial port
     sim_show_serial        shows the available host serial ports


   The calling sequences are as follows:


   SERHANDLE sim_open_serial (char *name)
   --------------------------------------

   The serial port referenced by the OS-dependent "name" is opened.  If the open
   is successful, and "name" refers to a serial port on the host system, then a
   handle to the port is returned.  If not, then the value INVALID_HANDLE is
   returned.


   t_stat sim_config_serial (SERHANDLE port, const char *config)
   -------------------------------------------------------------

   The baud rate and framing parameters (character size, parity, and number of
   stop bits) of the serial port associated with "port" are set.  If any
   "config" field value is unsupported by the host system, or if the combination
   of values (e.g., baud rate and number of stop bits) is unsupported, SCPE_ARG
   is returned.  If the configuration is successful, SCPE_OK is returned.


   sim_control_serial (SERHANDLE port, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits)
   -------------------------------------------------------------------------------------------------

   The DTR and RTS line of the serial port is set or cleared as indicated in 
   the respective bits_to_set or bits_to_clear parameters.  If the 
   incoming_bits parameter is not NULL, then the modem status bits DCD, RNG, 
   DSR and CTS are returned.

   If unreasonable or nonsense bits_to_set or bits_to_clear bits are 
   specified, then the return status is SCPE_ARG;
   If an error occurs, SCPE_IOERR is returned.


   int32 sim_read_serial (SERHANDLE port, char *buffer, int32 count, char *brk)
   ----------------------------------------------------------------------------

   A non-blocking read is issued for the serial port indicated by "port" to get
   at most "count" bytes into the string "buffer".  If a serial line break was
   detected during the read, the variable pointed to by "brk" is set to 1.  If
   the read is successful, the actual number of characters read is returned.  If
   no characters were available, then the value 0 is returned.  If an error
   occurs, then the value -1 is returned.


   int32 sim_write_serial (SERHANDLE port, char *buffer, int32 count)
   ------------------------------------------------------------------

   A write is issued to the serial port indicated by "port" to put "count"
   characters from "buffer".  If the write is successful, the actual number of
   characters written is returned.  If an error occurs, then the value -1 is
   returned.


   void sim_close_serial (SERHANDLE port)
   --------------------------------------

   The serial port indicated by "port" is closed.


   int sim_serial_devices (int max, SERIAL_LIST* list)
   ---------------------------------------------------

   enumerates the available host serial ports


   t_stat sim_show_serial (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, const void* desc)
   ---------------------------------

   displays the available host serial ports

*/


#include "sim_defs.h"
#include "sim_serial.h"
#include "sim_tmxr.h"

#include <ctype.h>

#define SER_DEV_NAME_MAX     256                        /* maximum device name size */
#define SER_DEV_DESC_MAX     256                        /* maximum device description size */
#define SER_DEV_CONFIG_MAX    64                        /* maximum device config size */
#define SER_MAX_DEVICE        64                        /* maximum serial devices */

typedef struct serial_list {
    char    name[SER_DEV_NAME_MAX];
    char    desc[SER_DEV_DESC_MAX];
    } SERIAL_LIST;

typedef struct serial_config {                          /* serial port configuration */
    uint32 baudrate;                                    /* baud rate */
    uint32 charsize;                                    /* character size in bits */
    char   parity;                                      /* parity (N/O/E/M/S) */
    uint32 stopbits;                                    /* 0/1/2 stop bits (0 implies 1.5) */
    } SERCONFIG;

static int       sim_serial_os_devices (int max, SERIAL_LIST* list);
static SERHANDLE sim_open_os_serial    (char *name);
static void      sim_close_os_serial   (SERHANDLE port);
static t_stat    sim_config_os_serial  (SERHANDLE port, SERCONFIG config);


static struct open_serial_device {
    SERHANDLE port;
    TMLN *line;
    char name[SER_DEV_NAME_MAX];
    char config[SER_DEV_CONFIG_MAX];
    } *serial_open_devices = NULL;
static int serial_open_device_count = 0;

static struct open_serial_device *_get_open_device (SERHANDLE port)
{
int i;

for (i=0; i<serial_open_device_count; ++i)
    if (serial_open_devices[i].port == port)
        return &serial_open_devices[i];
return NULL;
}

static struct open_serial_device *_get_open_device_byname (const char *name)
{
int i;

for (i=0; i<serial_open_device_count; ++i)
    if (0 == strcmp(name, serial_open_devices[i].name))
        return &serial_open_devices[i];
return NULL;
}

static struct open_serial_device *_serial_add_to_open_list (SERHANDLE port, TMLN *line, const char *name, const char *config)
{
serial_open_devices = (struct open_serial_device *)realloc(serial_open_devices, (++serial_open_device_count)*sizeof(*serial_open_devices));
memset(&serial_open_devices[serial_open_device_count-1], 0, sizeof(serial_open_devices[serial_open_device_count-1]));
serial_open_devices[serial_open_device_count-1].port = port;
serial_open_devices[serial_open_device_count-1].line = line;
strncpy(serial_open_devices[serial_open_device_count-1].name, name, sizeof(serial_open_devices[serial_open_device_count-1].name)-1);
if (config)
    strncpy(serial_open_devices[serial_open_device_count-1].config, config, sizeof(serial_open_devices[serial_open_device_count-1].config)-1);
return &serial_open_devices[serial_open_device_count-1];
}

static void _serial_remove_from_open_list (SERHANDLE port)
{
int i, j;

for (i=0; i<serial_open_device_count; ++i)
    if (serial_open_devices[i].port == port) {
        for (j=i+1; j<serial_open_device_count; ++j)
            serial_open_devices[j-1] = serial_open_devices[j];
        --serial_open_device_count;
        break;
        }
}

/* Generic error message handler.

   This routine should be called for unexpected errors.  Some error returns may
   be expected, e.g., a "file not found" error from an "open" routine.  These
   should return appropriate status codes to the caller, allowing SCP to print
   an error message if desired, rather than printing this generic error message.
*/

static void sim_error_serial (const char *routine, int error)
{
sim_printf ("Serial: %s fails with error %d\n", routine, error);
return;
}

/* Used when sorting a list of serial port names */
static int _serial_name_compare (const void *pa, const void *pb)
{
const SERIAL_LIST *a = (const SERIAL_LIST *)pa;
const SERIAL_LIST *b = (const SERIAL_LIST *)pb;

return strcmp(a->name, b->name);
}

static int sim_serial_devices (int max, SERIAL_LIST *list)
{
int i, j, ports = sim_serial_os_devices(max, list);

/* Open ports may not show up in the list returned by sim_serial_os_devices 
   so we add the open ports to the list removing duplicates before sorting 
   the resulting list */

for (i=0; i<serial_open_device_count; ++i) {
    for (j=0; j<ports; ++j)
        if (0 == strcmp(serial_open_devices[i].name, list[j].name))
            break;
    if (j<ports)
        continue;
    if (ports >= max)
        break;
    strcpy(list[ports].name, serial_open_devices[i].name);
    strcpy(list[ports].desc, serial_open_devices[i].config);
    ++ports;
    }
if (ports) /* Order the list returned alphabetically by the port name */
    qsort (list, ports, sizeof(list[0]), _serial_name_compare);
return ports;
}

static char* sim_serial_getname (int number, char* name)
{
SERIAL_LIST  list[SER_MAX_DEVICE];
int count = sim_serial_devices(SER_MAX_DEVICE, list);

if (count <= number)
    return NULL;
strcpy(name, list[number].name);
return name;
}

static char* sim_serial_getname_bydesc (char* desc, char* name)
{
SERIAL_LIST  list[SER_MAX_DEVICE];
int count = sim_serial_devices(SER_MAX_DEVICE, list);
int i;
size_t j=strlen(desc);

for (i=0; i<count; i++) {
    int found = 1;
    size_t k = strlen(list[i].desc);

    if (j != k)
        continue;
    for (k=0; k<j; k++)
        if (tolower(list[i].desc[k]) != tolower(desc[k]))
            found = 0;
    if (found == 0)
        continue;

    /* found a case-insensitive description match */
    strcpy(name, list[i].name);
    return name;
    }
/* not found */
return NULL;
}

static char* sim_serial_getname_byname (char* name, char* temp)
{
SERIAL_LIST  list[SER_MAX_DEVICE];
int count = sim_serial_devices(SER_MAX_DEVICE, list);
size_t n;
int i, found;

found = 0;
n = strlen(name);
for (i=0; i<count && !found; i++) {
    if ((n == strlen(list[i].name)) &&
        (strncasecmp(name, list[i].name, n) == 0)) {
        found = 1;
        strcpy(temp, list[i].name); /* only case might be different */
        }
    }
return (found ? temp : NULL);
}

char* sim_serial_getdesc_byname (char* name, char* temp)
{
SERIAL_LIST  list[SER_MAX_DEVICE];
int count = sim_serial_devices(SER_MAX_DEVICE, list);
size_t n;
int i, found;

found = 0;
n = strlen(name);
for (i=0; i<count && !found; i++) {
    if ((n == strlen(list[i].name)) &&
        (strncasecmp(name, list[i].name, n) == 0)) {
        found = 1;
        strcpy(temp, list[i].desc);
        }
    }
  return (found ? temp : NULL);
}

t_stat sim_show_serial (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, CONST char* desc)
{
SERIAL_LIST  list[SER_MAX_DEVICE];
int number = sim_serial_devices(SER_MAX_DEVICE, list);

fprintf(st, "Serial devices:\n");
if (number == -1)
    fprintf(st, "  serial support not available in simulator\n");
else {
    if (number == 0) {
        fprintf(st, "  no serial devices are available.\n");
        fprintf(st, "You may need to run with privilege or set device permissions\n");
        fprintf(st, "to access local serial ports\n");
        }
    else {
        size_t min, len;
        int i;
        for (i=0, min=0; i<number; i++)
            if ((len = strlen(list[i].name)) > min)
                min = len;
        for (i=0; i<number; i++)
            fprintf(st," ser%d\t%-*s%s%s%s\n", i, (int)min, list[i].name, list[i].desc[0] ? " (" : "", list[i].desc, list[i].desc[0] ? ")" : "");
        }
    }
if (serial_open_device_count) {
    int i;
    char desc[SER_DEV_DESC_MAX], *d;

    fprintf(st,"Open Serial Devices:\n");
    for (i=0; i<serial_open_device_count; i++) {
        d = sim_serial_getdesc_byname(serial_open_devices[i].name, desc);
        fprintf(st, " %s\tLn%02d %s%s%s%s\tConfig: %s\n", serial_open_devices[i].line->mp->dptr->name, (int)(serial_open_devices[i].line->mp->ldsc-serial_open_devices[i].line),
                    serial_open_devices[i].line->destination, d ? " {" : "", d ? d : "", d ? ")" : "", serial_open_devices[i].line->serconfig);
        }
    }
return SCPE_OK;
}

SERHANDLE sim_open_serial (char *name, TMLN *lp, t_stat *stat)
{
char temp1[1024], devname [1024];
char *savname = name;
SERHANDLE port = INVALID_HANDLE;
CONST char *config;
t_stat status;

config = get_glyph_nc (name, devname, ';');             /* separate port name from optional config params */

if ((config == NULL) || (*config == '\0'))
    config = "9600-8N1";

if (stat)
    *stat = SCPE_OK;

/* translate name of type "serX" to real device name */
if ((strlen(devname) <= 5)
    && (tolower(devname[0]) == 's')
    && (tolower(devname[1]) == 'e')
    && (tolower(devname[2]) == 'r')
    && (isdigit(devname[3]))
    && (isdigit(devname[4]) || (devname[4] == '\0'))
   ) {
    int num = atoi(&devname[3]);
    savname = sim_serial_getname(num, temp1);
    if (savname == NULL) {                              /* didn't translate */
        if (stat)
            *stat = SCPE_OPENERR;
        return INVALID_HANDLE;
        }
    }
else {
    /* are they trying to use device description? */
    savname = sim_serial_getname_bydesc(devname, temp1);
    if (savname == NULL) {                              /* didn't translate */
        /* probably is not serX and has no description */
        savname = sim_serial_getname_byname(devname, temp1);
        if (savname == NULL) /* didn't translate */
            savname = devname;
        }
    }

if (_get_open_device_byname (savname)) {
    if (stat)
        *stat = SCPE_OPENERR;
    return INVALID_HANDLE;
    }

port = sim_open_os_serial (savname);

if (port == INVALID_HANDLE) {
    if (stat)
        *stat = SCPE_OPENERR;
    return port;
    }

status = sim_config_serial (port, config);              /* set serial configuration */
if ((lp) && (status == SCPE_OK))                        /* line specified? */
    status = tmxr_set_config_line (lp, config);         /* set line speed parameters */

if (status != SCPE_OK) {                                /* port configuration error? */
    sim_close_serial (port);                            /* close the port */
    if (stat)
        *stat = status;
    port = INVALID_HANDLE;                              /* report error */
    }

if ((port != INVALID_HANDLE) && (*config) && (lp)) {
    lp->serconfig = (char *)realloc (lp->serconfig, 1 + strlen (config));
    strcpy (lp->serconfig, config);
    }
if (port != INVALID_HANDLE)
    _serial_add_to_open_list (port, lp, savname, config);

return port;
}

void sim_close_serial (SERHANDLE port)
{
sim_close_os_serial (port);
_serial_remove_from_open_list (port);
}

t_stat sim_config_serial  (SERHANDLE port, CONST char *sconfig)
{
CONST char *pptr;
CONST char *sptr, *tptr;
SERCONFIG config = { 0 };
t_bool arg_error = FALSE;
t_stat r;
struct open_serial_device *dev;

if ((sconfig == NULL) || (*sconfig == '\0'))
    sconfig = "9600-8N1";                               /* default settings */
pptr = sconfig;

config.baudrate = (uint32)strtotv (pptr, &sptr, 10);    /* parse baud rate */
arg_error = (pptr == sptr);                             /* check for bad argument */

if (*sptr)                                              /* separator present? */
    sptr++;                                             /* skip it */

config.charsize = (uint32)strtotv (sptr, &tptr, 10);    /* parse character size */
arg_error = arg_error || (sptr == tptr);                /* check for bad argument */

if (*tptr)                                              /* parity character present? */
    config.parity = (char)toupper (*tptr++);            /* save parity character */

config.stopbits = (uint32)strtotv (tptr, &sptr, 10);    /* parse number of stop bits */
arg_error = arg_error || (tptr == sptr);                /* check for bad argument */

if (arg_error)                                          /* bad conversions? */
    return SCPE_ARG;                                    /* report argument error */
if (strcmp (sptr, ".5") == 0)                           /* 1.5 stop bits requested? */
    config.stopbits = 0;                                /* code request */

r = sim_config_os_serial (port, config);
dev = _get_open_device (port);
if (dev) {
    dev->line->serconfig = (char *)realloc (dev->line->serconfig, 1 + strlen (sconfig));
    strcpy (dev->line->serconfig, sconfig);
    }
return r;
}

#if defined (_WIN32)

/* Windows serial implementation */

/* Enumerate the available serial ports.

   The serial port names are extracted from the appropriate place in the 
   windows registry (HKLM\HARDWARE\DEVICEMAP\SERIALCOMM\).  The resulting
   list is sorted alphabetically by device name (COMn).  The device description 
   is set to the OS internal name for the COM device.

*/

struct SERPORT {
    HANDLE hPort;
    DWORD dwEvtMask;
    OVERLAPPED oReadSync;
    OVERLAPPED oWriteReady;
    OVERLAPPED oWriteSync;
    };

static int sim_serial_os_devices (int max, SERIAL_LIST* list)
{
int ports = 0;
HKEY hSERIALCOMM;

memset(list, 0, max*sizeof(*list));
if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_QUERY_VALUE, &hSERIALCOMM) == ERROR_SUCCESS) {
    DWORD dwIndex = 0;
    DWORD dwType;
    DWORD dwValueNameSize = sizeof(list[ports].desc);
    DWORD dwDataSize = sizeof(list[ports].name);

    /* Enumerate all the values underneath HKEY_LOCAL_MACHINE\HARDWARE\DEVICEMAP\SERIALCOMM */
    while (RegEnumValueA(hSERIALCOMM, dwIndex, list[ports].desc, &dwValueNameSize, NULL, &dwType, (BYTE *)list[ports].name, &dwDataSize) == ERROR_SUCCESS) {
        /* String values with non-zero size are the interesting ones */
        if ((dwType == REG_SZ) && (dwDataSize > 0)) {
            if (ports < max)
                ++ports;
            else
                break;
            }
        /* Besure to clear the working entry before trying again */
        memset(list[ports].name, 0, sizeof(list[ports].name));
        memset(list[ports].desc, 0, sizeof(list[ports].desc));
        dwValueNameSize = sizeof(list[ports].desc);
        dwDataSize = sizeof(list[ports].name);
        ++dwIndex;
        }
    RegCloseKey(hSERIALCOMM);
    }
return ports;
}

/* Open a serial port.

   The serial port designated by "name" is opened, and the handle to the port is
   returned.  If an error occurs, INVALID_HANDLE is returned instead.  After
   opening, the port is configured with the default communication parameters
   established by the system, and the timeouts are set for immediate return on a
   read request to enable polling.

   Implementation notes:

    1. We call "GetDefaultCommConfig" to obtain the default communication
       parameters for the specified port.  If the name does not refer to a
       communications port (serial or parallel), the function fails.

    2. There is no way to limit "CreateFile" just to serial ports, so we must
       check after the port is opened.  The "GetCommState" routine will return
       an error if the handle does not refer to a serial port.

    3. Calling "GetDefaultCommConfig" for a serial port returns a structure
       containing a DCB.  This contains the default parameters.  However, some
       of the DCB fields are not set correctly, so we cannot use this directly
       in a call to "SetCommState".  Instead, we must copy the fields of
       interest to a DCB retrieved from a call to "GetCommState".
*/

static SERHANDLE sim_open_os_serial (char *name)
{
HANDLE hPort;
SERHANDLE port;
DCB dcb;
COMMCONFIG commdefault;
DWORD error;
DWORD commsize = sizeof (commdefault);
COMMTIMEOUTS cto;

if (!GetDefaultCommConfig (name, &commdefault, &commsize)) {    /* get default comm parameters */
    error = GetLastError ();                                    /* function failed; get error */

    if (error != ERROR_INVALID_PARAMETER)                       /* not a communications port name? */
        sim_error_serial ("GetDefaultCommConfig", (int) error); /* no, so report unexpected error */

    return INVALID_HANDLE;                                      /* indicate bad port name */
    }

hPort = CreateFile (name, GENERIC_READ | GENERIC_WRITE, /* open the port */
                   0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);

if (hPort == INVALID_HANDLE_VALUE) {                    /* open failed? */
    error = GetLastError ();                            /* get error code */

    if ((error != ERROR_FILE_NOT_FOUND) &&              /* bad filename? */
        (error != ERROR_ACCESS_DENIED))                 /* already open? */
        sim_error_serial ("CreateFile", (int) error);   /* no, so report unexpected error */

    return INVALID_HANDLE;                              /* indicate bad port name */
    }

port = (SERHANDLE)calloc (1, sizeof(*port));            /* instantiate the SERHANDLE */
port->hPort = hPort;

if (!GetCommState (port->hPort, &dcb)) {                /* get the current comm parameters */
    error = GetLastError ();                            /* function failed; get error */

    if (error != ERROR_INVALID_PARAMETER)               /* not a serial port name? */
        sim_error_serial ("GetCommState", (int) error); /* no, so report unexpected error */

    sim_close_os_serial (port);                         /* close port */
    return INVALID_HANDLE;                              /*   and indicate bad port name */
    }

dcb.BaudRate = commdefault.dcb.BaudRate;                /* copy default parameters of interest */
dcb.Parity   = commdefault.dcb.Parity;
dcb.ByteSize = commdefault.dcb.ByteSize;
dcb.StopBits = commdefault.dcb.StopBits;
dcb.fOutX    = commdefault.dcb.fOutX;
dcb.fInX     = commdefault.dcb.fInX;

dcb.fDtrControl = DTR_CONTROL_DISABLE;                  /* disable DTR initially until poll connects */

if (!SetCommState (port->hPort, &dcb)) {                /* configure the port with default parameters */
    sim_error_serial ("SetCommState",                   /* function failed; report unexpected error */
                      (int) GetLastError ());
    sim_close_os_serial (port);                         /* close port */
    return INVALID_HANDLE;                              /*   and indicate failure to caller */
    }

cto.ReadIntervalTimeout         = MAXDWORD;             /* set port to return immediately on read */
cto.ReadTotalTimeoutMultiplier  = 0;                    /* i.e., to enable polling */
cto.ReadTotalTimeoutConstant    = 0;
cto.WriteTotalTimeoutMultiplier = 0;
cto.WriteTotalTimeoutConstant   = 0;

if (!SetCommTimeouts (port->hPort, &cto)) {             /* configure port timeouts */
    sim_error_serial ("SetCommTimeouts",                /* function failed; report unexpected error */
                      (int) GetLastError ());
    sim_close_os_serial (port);                         /* close port */
    return INVALID_HANDLE;                              /*   and indicate failure to caller */
    }

/* Create an event object for use by WaitCommEvent. */

port->oWriteReady.hEvent = CreateEvent(NULL,            /* default security attributes */
                                       TRUE,            /* manual-reset event */
                                       TRUE,            /* signaled */
                                       NULL);           /* no name */
if (port->oWriteReady.hEvent == NULL) {
    sim_error_serial ("CreateEvent",                    /* function failed; report unexpected error */
                      (int) GetLastError ());
    sim_close_os_serial (port);                         /* close port */
    return INVALID_HANDLE;                              /*   and indicate failure to caller */
    }

port->oReadSync.hEvent = CreateEvent(NULL,              /* default security attributes */
                                     TRUE,              /* manual-reset event */
                                     FALSE,             /* not signaled */
                                     NULL);             /* no name */
if (port->oReadSync.hEvent == NULL) {
    sim_error_serial ("CreateEvent",                    /* function failed; report unexpected error */
                      (int) GetLastError ());
    sim_close_os_serial (port);                         /* close port */
    return INVALID_HANDLE;                              /*   and indicate failure to caller */
    }

port->oWriteSync.hEvent = CreateEvent(NULL,             /* default security attributes */
                                      TRUE,             /* manual-reset event */
                                      FALSE,            /* not signaled */
                                      NULL);            /* no name */
if (port->oWriteSync.hEvent == NULL) {
    sim_error_serial ("CreateEvent",                    /* function failed; report unexpected error */
                      (int) GetLastError ());
    sim_close_os_serial (port);                         /* close port */
    return INVALID_HANDLE;                              /*   and indicate failure to caller */
    }

if (!SetCommMask (port->hPort, EV_TXEMPTY)) {
    sim_error_serial ("SetCommMask",                    /* function failed; report unexpected error */
                      (int) GetLastError ());
    sim_close_os_serial (port);                         /* close port */
    return INVALID_HANDLE;                              /*   and indicate failure to caller */
    }

return port;                                            /* return port handle on success */
}


/* Configure a serial port.

   Port parameters are configured as specified in the "config" structure.  If
   "config" contains an invalid configuration value, or if the host system
   rejects the configuration (e.g., by requesting an unsupported combination of
   character size and stop bits), SCPE_ARG is returned to the caller.  If an
   unexpected error occurs, SCPE_IOERR is returned.  If the configuration
   succeeds, SCPE_OK is returned.

   Implementation notes:

    1. We do not enable input parity checking, as the multiplexer library has no
       way of communicating parity errors back to the target simulator.

    2. A zero value for the "stopbits" field of the "config" structure implies
       1.5 stop bits.
*/

static t_stat sim_config_os_serial (SERHANDLE port, SERCONFIG config)
{
static const struct {
    char parity;
    BYTE parity_code;
    } parity_map [] =
        { { 'E', EVENPARITY }, { 'M', MARKPARITY  }, { 'N', NOPARITY },
          { 'O', ODDPARITY  }, { 'S', SPACEPARITY } };

static const int32 parity_count = sizeof (parity_map) / sizeof (parity_map [0]);

DCB dcb;
DWORD error;
int32 i;

if (!GetCommState (port->hPort, &dcb)) {                /* get the current comm parameters */
    sim_error_serial ("GetCommState",                   /* function failed; report unexpected error */
                      (int) GetLastError ());
    return SCPE_IOERR;                                  /* return failure status */
    }

dcb.BaudRate = config.baudrate;                         /* assign baud rate */

if (config.charsize >= 5 && config.charsize <= 8)       /* character size OK? */
    dcb.ByteSize = (BYTE)config.charsize;               /* assign character size */
else
    return SCPE_ARG;                                    /* not a valid size */

for (i = 0; i < parity_count; i++)                      /* assign parity */
    if (config.parity == parity_map [i].parity) {       /* match mapping value? */
        dcb.Parity = parity_map [i].parity_code;        /* assign corresponding code */
        break;
        }

if (i == parity_count)                                  /* parity assigned? */
    return SCPE_ARG;                                    /* not a valid parity specifier */

if (config.stopbits == 1)                               /* assign stop bits */
    dcb.StopBits = ONESTOPBIT;
else if (config.stopbits == 2)
    dcb.StopBits = TWOSTOPBITS;
else if (config.stopbits == 0)                          /* 0 implies 1.5 stop bits */
    dcb.StopBits = ONE5STOPBITS;
else
    return SCPE_ARG;                                    /* not a valid number of stop bits */

if (!SetCommState (port->hPort, &dcb)) {                /* set the configuration */
    error = GetLastError ();                            /* check for error */

    if (error == ERROR_INVALID_PARAMETER)               /* invalid configuration? */
        return SCPE_ARG;                                /* report as argument error */

    sim_error_serial ("SetCommState", (int) error);     /* function failed; report unexpected error */
    return SCPE_IOERR;                                  /* return failure status */
    }

return SCPE_OK;                                         /* return success status */
}


/* Control a serial port.

   The DTR and RTS line of the serial port is set or cleared as indicated in 
   the respective bits_to_set or bits_to_clear parameters.  If the 
   incoming_bits parameter is not NULL, then the modem status bits DCD, RNG, 
   DSR and CTS are returned.

   If unreasonable or nonsense bits_to_set or bits_to_clear bits are 
   specified, then the return status is SCPE_ARG;
   If an error occurs, SCPE_IOERR is returned.
*/

t_stat sim_control_serial (SERHANDLE port, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits)
{
if ((bits_to_set & ~(TMXR_MDM_OUTGOING)) ||         /* Assure only settable bits */
    (bits_to_clear & ~(TMXR_MDM_OUTGOING)) ||
    (bits_to_set & bits_to_clear))                  /* and can't set and clear the same bits */
    return SCPE_ARG;
if (bits_to_set&TMXR_MDM_DTR)
    if (!EscapeCommFunction (port->hPort, SETDTR)) {
        sim_error_serial ("EscapeCommFunction", (int) GetLastError ());
        return SCPE_IOERR;
        }
if (bits_to_clear&TMXR_MDM_DTR)
    if (!EscapeCommFunction (port->hPort, CLRDTR)) {
        sim_error_serial ("EscapeCommFunction", (int) GetLastError ());
        return SCPE_IOERR;
        }
if (bits_to_set&TMXR_MDM_RTS)
    if (!EscapeCommFunction (port->hPort, SETRTS)) {
        sim_error_serial ("EscapeCommFunction", (int) GetLastError ());
        return SCPE_IOERR;
        }
if (bits_to_clear&TMXR_MDM_RTS)
    if (!EscapeCommFunction (port->hPort, CLRRTS)) {
        sim_error_serial ("EscapeCommFunction", (int) GetLastError ());
        return SCPE_IOERR;
        }
if (incoming_bits) {
    DWORD ModemStat;
    if (GetCommModemStatus (port->hPort, &ModemStat)) {
        sim_error_serial ("GetCommModemStatus", (int) GetLastError ());
        return SCPE_IOERR;
        }
    *incoming_bits = ((ModemStat&MS_CTS_ON)  ? TMXR_MDM_CTS : 0) |
                     ((ModemStat&MS_DSR_ON)  ? TMXR_MDM_DSR : 0) |
                     ((ModemStat&MS_RING_ON) ? TMXR_MDM_RNG : 0) |
                     ((ModemStat&MS_RLSD_ON) ? TMXR_MDM_DCD : 0);
    }
return SCPE_OK;
}


/* Read from a serial port.

   The port is checked for available characters.  If any are present, they are
   copied to the passed buffer, and the count of characters is returned.  If no
   characters are available, 0 is returned.  If an error occurs, -1 is returned.
   If a BREAK is detected on the communications line, the corresponding flag in
   the "brk" array is set.

   Implementation notes:

    1. The "ClearCommError" function will set the CE_BREAK flag in the returned
       errors value if a BREAK has occurred.  However, we do not know where in
       the serial stream it happened, as CE_BREAK isn't associated with a
       specific character.  Because the "brk" array does want a flag associated
       with a specific character, we guess at the proper location by setting
       the "brk" entry corresponding to the first NUL in the character stream.
       If no NUL is present, then the "brk" entry associated with the first
       character is set.
*/

int32 sim_read_serial (SERHANDLE port, char *buffer, int32 count, char *brk)
{
DWORD read;
DWORD commerrors;
COMSTAT cs;
char *bptr;

memset (brk, 0, count);                                 /* start with no break indicators */
if (!ClearCommError (port->hPort, &commerrors, &cs)) {  /* get the comm error flags  */
    sim_error_serial ("ClearCommError",                 /* function failed; report unexpected error */
                      (int) GetLastError ());
    return -1;                                          /* return failure to caller */
    }

if (!ReadFile (port->hPort, (LPVOID) buffer,            /* read any available characters */
               (DWORD) count, &read, &port->oReadSync)) {
    sim_error_serial ("ReadFile",                       /* function failed; report unexpected error */
                      (int) GetLastError ());
    return -1;                                          /* return failure to caller */
    }

if (commerrors & CE_BREAK) {                            /* was a BREAK detected? */
    bptr = (char *) memchr (buffer, 0, read);           /* search for the first NUL in the buffer */

    if (bptr)                                           /* was one found? */
        brk = brk + (bptr - buffer);                    /* calculate corresponding position */

    *brk = 1;                                           /* set the BREAK flag */
    }

return read;                                            /* return the number of characters read */
}


/* Write to a serial port.

   "Count" characters are written from "buffer" to the serial port.  The actual
   number of characters written to the port is returned.  If an error occurred
   on writing, -1 is returned.
*/

int32 sim_write_serial (SERHANDLE port, char *buffer, int32 count)
{
if ((!WriteFile (port->hPort, (LPVOID) buffer,   /* write the buffer to the serial port */
                 (DWORD) count, NULL, &port->oWriteSync)) &&
    (GetLastError () != ERROR_IO_PENDING)) {
    sim_error_serial ("WriteFile",              /* function failed; report unexpected error */
                      (int) GetLastError ());
    return -1;                                  /* return failure to caller */
    }
return count;                                   /* return number of characters written/queued */
}


/* Close a serial port.

   The serial port is closed.  Errors are ignored.
*/

static void sim_close_os_serial (SERHANDLE port)
{
if (port->oWriteReady.hEvent)
    CloseHandle (port->oWriteReady.hEvent);               /* close the event handle */
if (port->oReadSync.hEvent)
    CloseHandle (port->oReadSync.hEvent);               /* close the event handle */
if (port->oWriteSync.hEvent)
    CloseHandle (port->oWriteSync.hEvent);              /* close the event handle */
if (port->hPort)
    CloseHandle (port->hPort);                          /* close the port */
free (port);
}



#elif defined (__unix__) || defined(__APPLE__) || defined(__hpux)

struct SERPORT {
    int port;
    };

#if defined(__linux) || defined(__linux__)
#include <dirent.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/stat.h>
#endif /* __linux__ */

/* UNIX implementation */

/* Enumerate the available serial ports.

   The serial port names generated by attempting to open /dev/ttyS0 thru
   /dev/ttyS63 and /dev/ttyUSB0 thru /dev/ttyUSB63 and /dev/tty.serial0
   thru /dev/tty.serial63.  Ones we can open and are ttys (as determined 
   by isatty()) are added to the list.  The list is sorted alphabetically 
   by device name.

*/

static int sim_serial_os_devices (int max, SERIAL_LIST* list)
{
int i;
int port;
int ports = 0;

memset(list, 0, max*sizeof(*list));
#if defined(__linux) || defined(__linux__)
if (1) {
    struct dirent **namelist = NULL;
    struct stat st;

    i = scandir("/sys/class/tty/", &namelist, NULL, NULL);

    while (0 < i--) {
        if (strcmp(namelist[i]->d_name, ".") &&
            strcmp(namelist[i]->d_name, "..")) {
            char path[1024], devicepath[1024], driverpath[1024];

            sprintf (path, "/sys/class/tty/%s", namelist[i]->d_name);
            sprintf (devicepath, "/sys/class/tty/%s/device", namelist[i]->d_name);
            sprintf (driverpath, "/sys/class/tty/%s/device/driver", namelist[i]->d_name);
            if ((lstat(devicepath, &st) == 0) && S_ISLNK(st.st_mode)) {
                char buffer[1024];

                memset (buffer, 0, sizeof(buffer));
                if (readlink(driverpath, buffer, sizeof(buffer)) > 0) {
                    sprintf (list[ports].name, "/dev/%s", basename (path));
                    port = open (list[ports].name, O_RDWR | O_NOCTTY | O_NONBLOCK);     /* open the port */
                    if (port != -1) {                   /* open OK? */
                        if (isatty (port))              /* is device a TTY? */
                            ++ports;
                        close (port);
                        }
                    }
                }
            }
        free (namelist[i]);
        }
    free (namelist);
    }
#elif defined(__hpux)
for (i=0; (ports < max) && (i < 64); ++i) {
    sprintf (list[ports].name, "/dev/tty%dp%d", i/8, i%8);
    port = open (list[ports].name, O_RDWR | O_NOCTTY | O_NONBLOCK);     /* open the port */
    if (port != -1) {                                   /* open OK? */
        if (isatty (port))                              /* is device a TTY? */
            ++ports;
        close (port);
        }
    }
#else /* Non Linux/HP-UX, just try some well known device names */
for (i=0; (ports < max) && (i < 64); ++i) {
    sprintf (list[ports].name, "/dev/ttyS%d", i);
    port = open (list[ports].name, O_RDWR | O_NOCTTY | O_NONBLOCK);     /* open the port */
    if (port != -1) {                                   /* open OK? */
        if (isatty (port))                              /* is device a TTY? */
            ++ports;
        close (port);
        }
    }
for (i=0; (ports < max) && (i < 64); ++i) {
    sprintf (list[ports].name, "/dev/ttyUSB%d", i);
    port = open (list[ports].name, O_RDWR | O_NOCTTY | O_NONBLOCK);     /* open the port */
    if (port != -1) {                                   /* open OK? */
        if (isatty (port))                              /* is device a TTY? */
            ++ports;
        close (port);
        }
    }
for (i=1; (ports < max) && (i < 64); ++i) {
    sprintf (list[ports].name, "/dev/tty.serial%d", i);
    port = open (list[ports].name, O_RDWR | O_NOCTTY | O_NONBLOCK);     /* open the port */
    if (port != -1) {                                   /* open OK? */
        if (isatty (port))                              /* is device a TTY? */
            ++ports;
        close (port);
        }
    }
for (i=0; (ports < max) && (i < 64); ++i) {
    sprintf (list[ports].name, "/dev/tty%02d", i);
    port = open (list[ports].name, O_RDWR | O_NOCTTY | O_NONBLOCK);     /* open the port */
    if (port != -1) {                                   /* open OK? */
        if (isatty (port))                              /* is device a TTY? */
            ++ports;
        close (port);
        }
    }
for (i=0; (ports < max) && (i < 8); ++i) {
    sprintf (list[ports].name, "/dev/ttyU%d", i);
    port = open (list[ports].name, O_RDWR | O_NOCTTY | O_NONBLOCK);     /* open the port */
    if (port != -1) {                                   /* open OK? */
        if (isatty (port))                              /* is device a TTY? */
            ++ports;
        close (port);
        }
    }
#endif
return ports;
}

/* Open a serial port.

   The serial port designated by "name" is opened, and the handle to the port is
   returned.  If an error occurs, INVALID_HANDLE is returned instead.  After
   opening, the port is configured to "raw" mode.

   Implementation notes:

    1. We use a non-blocking open to allow for polling during reads.

    2. There is no way to limit "open" just to serial ports, so we must check
       after the port is opened.  We do this with a combination of "isatty" and
       "tcgetattr".

    3. We configure with PARMRK set and IGNBRK and BRKINT cleared.  This will
       mark a communication line BREAK condition in the input stream with the
       three-character sequence \377 \000 \000.  This is detected during
       reading.
*/

static SERHANDLE sim_open_os_serial (char *name)
{
static const tcflag_t i_clear = IGNBRK |                /* ignore BREAK */
                                BRKINT |                /* signal on BREAK */
                                INPCK  |                /* enable parity checking */
                                ISTRIP |                /* strip character to 7 bits */
                                INLCR  |                /* map NL to CR */
                                IGNCR  |                /* ignore CR */
                                ICRNL  |                /* map CR to NL */
                                IXON   |                /* enable XON/XOFF output control */
                                IXOFF;                  /* enable XON/XOFF input control */

static const tcflag_t i_set   = PARMRK |                /* mark parity errors and line breaks */
                                IGNPAR;                 /* ignore parity errors */

static const tcflag_t o_clear = OPOST;                  /* post-process output */

static const tcflag_t o_set   = 0;

static const tcflag_t c_clear = HUPCL;                  /* hang up line on last close */

static const tcflag_t c_set   = CREAD |                 /* enable receiver */
                                CLOCAL;                 /* ignore modem status lines */

static const tcflag_t l_clear = ISIG    |               /* enable signals */
                                ICANON  |               /* canonical input */
                                ECHO    |               /* echo characters */
                                ECHOE   |               /* echo ERASE as an error correcting backspace */
                                ECHOK   |               /* echo KILL */
                                ECHONL  |               /* echo NL */
                                NOFLSH  |               /* disable flush after interrupt */
                                TOSTOP  |               /* send SIGTTOU for background output */
                                IEXTEN;                 /* enable extended functions */

static const tcflag_t l_set   = 0;
int port;
SERHANDLE serport;
struct termios tio;

port = open (name, O_RDWR | O_NOCTTY | O_NONBLOCK);     /* open the port */

if (port == -1) {                                       /* open failed? */
    if (errno != ENOENT && errno != EACCES)             /* file not found or can't open? */
        sim_error_serial ("open", errno);               /* no, so report unexpected error */

    return INVALID_HANDLE;                              /* indicate failure to caller */
    }

if (!isatty (port)) {                                   /* is device a TTY? */
    close (port);                                       /* no, so close it */
    return INVALID_HANDLE;                              /*   and return failure to caller */
    }

if (tcgetattr (port, &tio)) {                           /* get the terminal attributes */
    sim_error_serial ("tcgetattr", errno);              /* function failed; report unexpected error */
    close (port);                                       /* close the port */
    return INVALID_HANDLE;                              /*   and return failure to caller */
    }

// which of these methods is best?

#if 1

tio.c_iflag = (tio.c_iflag & ~i_clear) | i_set;           /* configure the serial line for raw mode */
tio.c_oflag = (tio.c_oflag & ~o_clear) | o_set;
tio.c_cflag = (tio.c_cflag & ~c_clear) | c_set;
tio.c_lflag = (tio.c_lflag & ~l_clear) | l_set;
#ifdef VMIN
tio.c_cc[VMIN] = 1;
#endif
#ifdef VTIME
tio.c_cc[VTIME] = 0;
#endif

#elif 0

tio.c_iflag &= ~(IGNBRK | BRKINT | INPCK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF);
tio.c_iflag |= PARMRK | IGNPAR;
tio.c_oflag &= ~(OPOST);
tio.c_cflag &= ~(HUPCL);
tio.c_cflag |= CREAD | CLOCAL;
tio.c_lflag &= ~(ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL | NOFLSH | TOSTOP | IEXTEN);

#elif 0

tio.c_iflag = PARMRK | IGNPAR;
tio.c_oflag = 0;
tio.c_cflag = tio.c_cflag | CLOCAL | CREAD;
tio.c_lflag = 0;

#endif

if (tcsetattr (port, TCSANOW, &tio)) {                  /* set the terminal attributes */
    sim_error_serial ("tcsetattr", errno);              /* function failed; report unexpected error */
    close (port);                                       /* close the port */
    return INVALID_HANDLE;                              /*   and return failure to caller */
    }

serport = (SERHANDLE)calloc (1, sizeof(*serport));
serport->port = port;
return serport;                                         /* return port fd for success */
}


/* Configure a serial port.

   Port parameters are configured as specified in the "config" structure.  If
   "config" contains an invalid configuration value, or if the host system
   rejects the configuration (e.g., by requesting an unsupported combination of
   character size and stop bits), SCPE_ARG is returned to the caller.  If an
   unexpected error occurs, SCPE_IOERR is returned.  If the configuration
   succeeds, SCPE_OK is returned.

   Implementation notes:

    1. 1.5 stop bits is not a supported configuration.

*/

static t_stat sim_config_os_serial (SERHANDLE port, SERCONFIG config)
{
struct termios tio;
int32 i;

static const struct {
    uint32  rate;
    speed_t rate_code;
    } baud_map [] =
        { { 50,     B50     }, { 75,     B75     }, { 110,    B110    }, {  134,   B134   },
          { 150,    B150    }, { 200,    B200    }, { 300,    B300    }, {  600,   B600   },
          { 1200,   B1200   }, { 1800,   B1800   }, { 2400,   B2400   }, {  4800,  B4800  },
          { 9600,   B9600   }, { 19200,  B19200  }, { 38400,  B38400  }, {  57600, B57600 },
          { 115200, B115200 } };

static const int32 baud_count = sizeof (baud_map) / sizeof (baud_map [0]);

static const tcflag_t charsize_map [4] = { CS5, CS6, CS7, CS8 };


if (tcgetattr (port->port, &tio)) {                     /* get the current configuration */
    sim_error_serial ("tcgetattr", errno);              /* function failed; report unexpected error */
    return SCPE_IOERR;                                  /* return failure status */
    }

for (i = 0; i < baud_count; i++)                        /* assign baud rate */
    if (config.baudrate == baud_map [i].rate) {         /* match mapping value? */
        cfsetispeed(&tio, baud_map [i].rate_code);      /* set input rate */
        cfsetospeed(&tio, baud_map [i].rate_code);      /* set output rate */
        break;
        }

if (i == baud_count)                                    /* baud rate assigned? */
    return SCPE_ARG;                                    /* invalid rate specified */

if ((config.charsize >= 5) && (config.charsize <= 8))   /* character size OK? */
    tio.c_cflag = (tio.c_cflag & ~CSIZE) |              /* replace character size code */
                charsize_map [config.charsize - 5];
else
    return SCPE_ARG;                                    /* not a valid size */

switch (config.parity) {                                /* assign parity */
    case 'E':
        tio.c_cflag = (tio.c_cflag & ~PARODD) | PARENB; /* set for even parity */
        break;

    case 'N':
        tio.c_cflag = tio.c_cflag & ~PARENB;            /* set for no parity */
        break;

    case 'O':
        tio.c_cflag = tio.c_cflag | PARODD | PARENB;    /* set for odd parity */
        break;

    default:
        return SCPE_ARG;                                /* not a valid parity specifier */
    }

if (config.stopbits == 1)                               /* one stop bit? */
    tio.c_cflag = tio.c_cflag & ~CSTOPB;                /* clear two-bits flag */
else if (config.stopbits == 2)                          /* two stop bits? */
    tio.c_cflag = tio.c_cflag | CSTOPB;                 /* set two-bits flag */
else                                                    /* some other number? */
    return SCPE_ARG;                                    /* not a valid number of stop bits */

if (tcsetattr (port->port, TCSAFLUSH, &tio)) {          /* set the new configuration */
    sim_error_serial ("tcsetattr", errno);              /* function failed; report unexpected error */
    return SCPE_IERR;                                   /* return failure status */
    }

return SCPE_OK;                                         /* configuration set successfully */
}


/* Control a serial port.

   The DTR and RTS line of the serial port is set or cleared as indicated in 
   the respective bits_to_set or bits_to_clear parameters.  If the 
   incoming_bits parameter is not NULL, then the modem status bits DCD, RNG, 
   DSR and CTS are returned.

   If unreasonable or nonsense bits_to_set or bits_to_clear bits are 
   specified, then the return status is SCPE_ARG;
   If an error occurs, SCPE_IOERR is returned.
*/

t_stat sim_control_serial (SERHANDLE port, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits)
{
int bits;

if ((bits_to_set & ~(TMXR_MDM_OUTGOING)) ||         /* Assure only settable bits */
    (bits_to_clear & ~(TMXR_MDM_OUTGOING)) ||
    (bits_to_set & bits_to_clear))                  /* and can't set and clear the same bits */
    return SCPE_ARG;
if (bits_to_set) {
    bits = ((bits_to_set&TMXR_MDM_DTR) ? TIOCM_DTR : 0) |
           ((bits_to_set&TMXR_MDM_RTS) ? TIOCM_RTS : 0);
    if (ioctl (port->port, TIOCMBIS, &bits)) {      /* set the desired bits */
        sim_error_serial ("ioctl", errno);          /* report unexpected error */
        return SCPE_IOERR;                          /* return failure status */
        }
    }
if (bits_to_clear) {
    bits = ((bits_to_clear&TMXR_MDM_DTR) ? TIOCM_DTR : 0) |
           ((bits_to_clear&TMXR_MDM_RTS) ? TIOCM_RTS : 0);
    if (ioctl (port->port, TIOCMBIC, &bits)) {      /* clear the desired bits */
        sim_error_serial ("ioctl", errno);          /* report unexpected error */
        return SCPE_IOERR;                          /* return failure status */
        }
    }
if (incoming_bits) {
    if (ioctl (port->port, TIOCMGET, &bits)) {      /* get the modem bits */
        sim_error_serial ("ioctl", errno);          /* report unexpected error */
        return SCPE_IOERR;                          /* return failure status */
        }
    *incoming_bits = ((bits&TIOCM_CTS) ? TMXR_MDM_CTS : 0) |
                     ((bits&TIOCM_DSR) ? TMXR_MDM_DSR : 0) |
                     ((bits&TIOCM_RNG) ? TMXR_MDM_RNG : 0) |
                     ((bits&TIOCM_CAR) ? TMXR_MDM_DCD : 0);
    }

return SCPE_OK;
}


/* Read from a serial port.

   The port is checked for available characters.  If any are present, they are
   copied to the passed buffer, and the count of characters is returned.  If no
   characters are available, 0 is returned.  If an error occurs, -1 is returned.
   If a BREAK is detected on the communications line, the corresponding flag in
   the "brk" array is set.

   Implementation notes:

    1. A character with a framing or parity error is indicated in the input
       stream by the three-character sequence \377 \000 \ccc, where "ccc" is the
       bad character.  A communications line BREAK is indicated by the sequence
       \377 \000 \000.  A received \377 character is indicated by the
       two-character sequence \377 \377.  If we find any of these sequences,
       they are replaced by the single intended character by sliding the
       succeeding characters backward by one or two positions.  If a BREAK
       sequence was encountered, the corresponding location in the "brk" array
       is determined, and the flag is set.  Note that there may be multiple
       sequences in the buffer.
*/

int32 sim_read_serial (SERHANDLE port, char *buffer, int32 count, char *brk)
{
int read_count;
char *bptr, *cptr;
int32 remaining;

read_count = read (port->port, (void *) buffer, (size_t) count);/* read from the serial port */

if (read_count == -1)                                       /* read error? */
    if (errno == EAGAIN)                                    /* no characters available? */
        return 0;                                           /* return 0 to indicate */
    else                                                    /* some other problem */
        sim_error_serial ("read", errno);                   /* report unexpected error */

else {                                                      /* read succeeded */
    cptr = buffer;                                          /* point at start of buffer */
    remaining = read_count - 1;                             /* stop search one char from end of string */

    while (remaining > 0 &&                                 /* still characters to search? */
           (bptr = (char*)memchr (cptr, '\377', remaining))) {/* search for start of PARMRK sequence */
        remaining = remaining - (bptr - cptr) - 1;          /* calc characters remaining */

        if (*(bptr + 1) == '\377') {                        /* is it a \377 \377 sequence? */
            memmove (bptr + 1, bptr + 2, remaining);        /* slide string backward to leave first \377 */
            remaining = remaining - 1;                      /* drop remaining count */
            read_count = read_count - 1;                    /*   and read count by char eliminated */
            }

        else if (remaining > 0 && *(bptr + 1) == '\0') {    /* is it a \377 \000 \ccc sequence? */
            memmove (bptr, bptr + 2, remaining);            /* slide string backward to leave \ccc */
            remaining = remaining - 2;                      /* drop remaining count */
            read_count = read_count - 2;                    /*   and read count by chars eliminated */

            if (*bptr == '\0')                              /* is it a BREAK sequence? */
                *(brk + (bptr - buffer)) = 1;               /* set corresponding BREAK flag */
            }

        cptr = bptr + 1;                                    /* point at remainder of string */
        }
    }

return (int32) read_count;                                  /* return the number of characters read */
}


/* Write to a serial port.

   "Count" characters are written from "buffer" to the serial port.  The actual
   number of characters written to the port is returned.  If an error occurred
   on writing, -1 is returned.
*/

int32 sim_write_serial (SERHANDLE port, char *buffer, int32 count)
{
int written;

written = write (port->port, (void *) buffer, (size_t) count);/* write the buffer to the serial port */

if (written == -1) {
    if (errno == EWOULDBLOCK)
        written = 0;                                        /* not an error, but nothing written */
#if defined(EAGAIN)
    else if (errno == EAGAIN)
        written = 0;                                        /* not an error, but nothing written */
#endif
    else                                                    /* unexpected error? */
        sim_error_serial ("write", errno);                  /* report it */
    }

return (int32) written;                                     /* return number of characters written */
}


/* Close a serial port.

   The serial port is closed.  Errors are ignored.
*/

static void sim_close_os_serial (SERHANDLE port)
{
close (port->port);                                           /* close the port */
free (port);
}


#elif defined (VMS)

/* VMS implementation */

#if defined(__VAX)
#define sys$assign SYS$ASSIGN
#define sys$qio SYS$QIO
#define sys$qiow SYS$QIOW
#define sys$dassgn SYS$DASSGN
#define sys$device_scan SYS$DEVICE_SCAN
#define sys$getdviw SYS$GETDVIW
#endif

#include <descrip.h>
#include <ttdef.h>
#include <tt2def.h>
#include <iodef.h>
#include <ssdef.h>
#include <dcdef.h>
#include <dvsdef.h>
#include <dvidef.h>
#include <starlet.h>
#include <unistd.h>

typedef struct {
    unsigned short sense_count;
    unsigned char sense_first_char;
    unsigned char sense_reserved;
    unsigned int stat;
    unsigned int stat2; } SENSE_BUF;

typedef struct {
    unsigned short status;
    unsigned short count;
    unsigned int dev_status; } IOSB;

typedef struct {
    unsigned short buffer_size;
    unsigned short item_code;
    void *buffer_address;
    void *return_length_address;
    } ITEM;

struct SERPORT {
    uint32 port;
    IOSB write_iosb;
    };

/* Enumerate the available serial ports.

   The serial port names generated by attempting to open /dev/ttyS0 thru
   /dev/ttyS53 and /dev/ttyUSB0 thru /dev/ttyUSB0.  Ones we can open and
   are ttys (as determined by isatty()) are added to the list.  The list 
   is sorted alphabetically by device name.

*/

static int sim_serial_os_devices (int max, SERIAL_LIST* list)
{
$DESCRIPTOR (wild, "*");
char devstr[sizeof(list[0].name)];
$DESCRIPTOR (device, devstr);
int ports;
IOSB iosb;
uint32 status;
uint32 devsts;
#define UCB$M_TEMPLATE 0x2000       /* Device is a template device */
#define UCB$M_ONLINE   0x0010       /* Device is online */
uint32 devtype;
uint32 devdepend;
#define DEV$M_RTM 0x20000000
uint32 devnamlen = 0;
t_bool done = FALSE;
uint32 context[2];
uint32 devclass = DC$_TERM; /* Only interested in terminal devices */
ITEM select_items[] = { {sizeof (devclass), DVS$_DEVCLASS, &devclass, NULL},
                        {                  0,               0,        NULL, NULL}};
ITEM valid_items[] =  { {    sizeof (devsts),        DVI$_STS,     &devsts, NULL},
                        {     sizeof(devstr),     DVI$_DEVNAM,      devstr, &devnamlen},
                        {    sizeof(devtype),    DVI$_DEVTYPE,    &devtype, NULL},
                        {  sizeof(devdepend),  DVI$_DEVDEPEND,  &devdepend, NULL},
                        {                  0,               0,        NULL, NULL}};

memset(context, 0, sizeof(context));
memset(devstr, 0, sizeof(devstr));
memset(list, 0, max*sizeof(*list));
for (ports=0; (ports < max); ++ports) {
    device.dsc$w_length = sizeof (devstr) - 1;
    status = sys$device_scan (&device,
                              &device.dsc$w_length,
                              &wild,
                              select_items,
                              &context);
    switch (status) {
        case SS$_NOSUCHDEV:
        case SS$_NOMOREDEV:
            done = TRUE;
            break;
        default:
            if (0 == (status&1))
                done = TRUE;
            else {
                status = sys$getdviw (0, 0, &device, valid_items, &iosb, NULL, 0, NULL);
                if (status == SS$_NORMAL)
                    status = iosb.status;
                if (status != SS$_NORMAL) {
                    done = TRUE;
                    break;
                    }
                device.dsc$w_length = devnamlen;
                if ((0 == (devsts & UCB$M_TEMPLATE)) &&
                    (0 != (devsts & UCB$M_ONLINE)) &&
                    (0 == (devdepend & DEV$M_RTM))) {
                    devstr[device.dsc$w_length] = '\0';
                    strcpy (list[ports].name, devstr);
                    while (list[ports].name[0] == '_')
                        strcpy (list[ports].name, list[ports].name+1);
                    }
                else
                    --ports;
                }
            break;
        }
    if (done)
        break;
    }
return ports;
}

/* Open a serial port.

   The serial port designated by "name" is opened, and the handle to the port is
   returned.  If an error occurs, INVALID_HANDLE is returned instead.  After
   opening, the port is configured to "raw" mode.

   Implementation notes:

    1. We use a non-blocking open to allow for polling during reads.

    2. There is no way to limit "open" just to serial ports, so we must check
       after the port is opened.  We do this with sys$getdvi.

*/

static SERHANDLE sim_open_os_serial (char *name)
{
uint32 status;
uint32 chan = 0;
IOSB iosb;
$DESCRIPTOR (devnam, name);
uint32 devclass;
ITEM items[] = { {sizeof (devclass), DVI$_DEVCLASS, &devclass, NULL},
                 {                0,             0,      NULL, NULL}};
SENSE_BUF start_mode = { 0 };
SENSE_BUF run_mode = { 0 };
SERHANDLE port;

devnam.dsc$w_length = strlen (devnam.dsc$a_pointer);
status = sys$assign (&devnam, &chan, 0, 0);
if (status != SS$_NORMAL) 
    return INVALID_HANDLE;
status = sys$getdviw (0, chan, NULL, items, &iosb, NULL, 0, NULL);
if ((status != SS$_NORMAL)      || 
    (iosb.status != SS$_NORMAL) ||
    (devclass != DC$_TERM)) {
    sys$dassgn (chan);
    return INVALID_HANDLE;
    }
status = sys$qiow (0, chan, IO$_SENSEMODE, &iosb, 0, 0,
    &start_mode, sizeof (start_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) {
    sys$dassgn (chan);
    return INVALID_HANDLE;
    }
run_mode = start_mode;
run_mode.stat = start_mode.stat | TT$M_NOECHO & ~(TT$M_HOSTSYNC | TT$M_TTSYNC | TT$M_HALFDUP);
run_mode.stat2 = start_mode.stat2 | TT2$M_PASTHRU;
status = sys$qiow (0, chan, IO$_SETMODE, &iosb, 0, 0,
    &run_mode, sizeof (run_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) {
    sys$dassgn (chan);
    return INVALID_HANDLE;
    }
port = (SERHANDLE)calloc (1, sizeof(*port));
port->port = chan;
port->write_iosb.status = 1;
return port;                                            /* return channel for success */
}


/* Configure a serial port.

   Port parameters are configured as specified in the "config" structure.  If
   "config" contains an invalid configuration value, or if the host system
   rejects the configuration (e.g., by requesting an unsupported combination of
   character size and stop bits), SCPE_ARG is returned to the caller.  If an
   unexpected error occurs, SCPE_IOERR is returned.  If the configuration
   succeeds, SCPE_OK is returned.

   Implementation notes:

    1. 1.5 stop bits is not a supported configuration.

*/

static t_stat sim_config_os_serial (SERHANDLE port, SERCONFIG config)
{
int32 i;
SENSE_BUF sense;
uint32 status, speed, parity, charsize, stopbits;
IOSB iosb;
static const struct {
    uint32  rate;
    uint32  rate_code;
    } baud_map [] =
        { { 50,     TT$C_BAUD_50     }, { 75,     TT$C_BAUD_75     }, { 110,    TT$C_BAUD_110    }, {  134,   TT$C_BAUD_134   },
          { 150,    TT$C_BAUD_150    }, { 300,    TT$C_BAUD_300    }, {  600,   TT$C_BAUD_600    }, {  1200,  TT$C_BAUD_1200  },
          { 1800,   TT$C_BAUD_1800   }, { 2000,   TT$C_BAUD_2000   }, { 2400,   TT$C_BAUD_2400   }, {  3600,  TT$C_BAUD_3600  },
          { 4800,   TT$C_BAUD_4800   }, { 7200,   TT$C_BAUD_7200   }, { 9600,   TT$C_BAUD_9600   }, { 19200,  TT$C_BAUD_19200 },
          { 38400,  TT$C_BAUD_38400  }, { 57600,  TT$C_BAUD_57600  }, { 76800,  TT$C_BAUD_76800  }, { 115200, TT$C_BAUD_115200} };

static const int32 baud_count = sizeof (baud_map) / sizeof (baud_map [0]);

status = sys$qiow (0, port->port, IO$_SENSEMODE, &iosb, 0, 0, &sense, sizeof(sense), 0, NULL, 0, 0);
if (status == SS$_NORMAL)
    status = iosb.status;
if (status != SS$_NORMAL) {
    sim_error_serial ("config-SENSEMODE", status);      /* report unexpected error */
    return SCPE_IOERR;
    }

for (i = 0; i < baud_count; i++)                        /* assign baud rate */
    if (config.baudrate == baud_map [i].rate) {         /* match mapping value? */
        speed = baud_map [i].rate_code << 8 |           /* set input rate */
                baud_map [i].rate_code;                 /* set output rate */
        break;
        }

if (i == baud_count)                                    /* baud rate assigned? */
    return SCPE_ARG;                                    /* invalid rate specified */

if (config.charsize >= 5 && config.charsize <= 8)       /* character size OK? */
    charsize = TT$M_ALTFRAME | config.charsize;         /* set character size */
else
    return SCPE_ARG;                                    /* not a valid size */

switch (config.parity) {                                /* assign parity */
    case 'E':
        parity = TT$M_ALTRPAR | TT$M_PARITY;            /* set for even parity */
        break;

    case 'N':
        parity = TT$M_ALTRPAR;                          /* set for no parity */
        break;

    case 'O':
        parity = TT$M_ALTRPAR | TT$M_PARITY | TT$M_ODD; /* set for odd parity */
        break;

    default:
        return SCPE_ARG;                                /* not a valid parity specifier */
    }


switch (config.stopbits) {
    case 1:                                             /* one stop bit? */
        stopbits = 0;
        break;
    case 2:                                             /* two stop bits? */
        if ((speed & 0xff) <= TT$C_BAUD_150) {          /* Only valid for */
            stopbits = TT$M_TWOSTOP;                    /* speeds 150baud or less */
            break;
            }
    default:
        return SCPE_ARG;                                /* not a valid number of stop bits */
    }

status = sys$qiow (0, port->port, IO$_SETMODE, &iosb, 0, 0,
    &sense, sizeof (sense), speed, 0, parity | charsize | stopbits, 0);
if (status == SS$_NORMAL)
    status = iosb.status;
if (status != SS$_NORMAL) {
    sim_error_serial ("config-SETMODE", status);        /* report unexpected error */
    return SCPE_IOERR;
    }
return SCPE_OK;                                         /* configuration set successfully */
}


/* Control a serial port.

   The DTR and RTS line of the serial port is set or cleared as indicated in 
   the respective bits_to_set or bits_to_clear parameters.  If the 
   incoming_bits parameter is not NULL, then the modem status bits DCD, RNG, 
   DSR and CTS are returned.

   If unreasonable or nonsense bits_to_set or bits_to_clear bits are 
   specified, then the return status is SCPE_ARG;
   If an error occurs, SCPE_IOERR is returned.
*/

t_stat sim_control_serial (SERHANDLE port, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits)
{
uint32 status;
IOSB iosb;
uint32 bits[2] = {0, 0};

if ((bits_to_set & ~(TMXR_MDM_OUTGOING)) ||         /* Assure only settable bits */
    (bits_to_clear & ~(TMXR_MDM_OUTGOING)) ||
    (bits_to_set & bits_to_clear))                  /* and can't set and clear the same bits */
    return SCPE_ARG;
if (bits_to_set)
    bits[0] |= (((bits_to_set&TMXR_MDM_DTR) ? TT$M_DS_DTR : 0) |
                ((bits_to_set&TMXR_MDM_RTS) ? TT$M_DS_RTS : 0)) << 16;
if (bits_to_clear)
    bits[0] |= (((bits_to_clear&TMXR_MDM_DTR) ? TT$M_DS_DTR : 0) |
                ((bits_to_clear&TMXR_MDM_RTS) ? TT$M_DS_RTS : 0)) << 24;
if (bits_to_set || bits_to_clear) {
    status = sys$qiow (0, port->port, IO$_SETMODE|IO$M_SET_MODEM|IO$M_MAINT, &iosb, 0, 0,
                       bits, 0, 0, 0, 0, 0);
    if (status == SS$_NORMAL)
        status = iosb.status;
    if (status != SS$_NORMAL) {
        sim_error_serial ("control-SETMODE", status);      /* report unexpected error */
        return SCPE_IOERR;
        }
    }
if (incoming_bits) {
    uint32 modem;

    status = sys$qiow (0, port->port, IO$_SENSEMODE|IO$M_RD_MODEM, &iosb, 0, 0,
                       bits, 0, 0, 0, 0, 0);
    if (status == SS$_NORMAL)
        status = iosb.status;
    if (status != SS$_NORMAL) {
        sim_error_serial ("control-SENSEMODE", status);      /* report unexpected error */
        return SCPE_IOERR;
        }
    modem = bits[0] >> 16;
    *incoming_bits = ((modem&TT$M_DS_CTS)     ? TMXR_MDM_CTS : 0) |
                     ((modem&TT$M_DS_DSR)     ? TMXR_MDM_DSR : 0) |
                     ((modem&TT$M_DS_RING)    ? TMXR_MDM_RNG : 0) |
                     ((modem&TT$M_DS_CARRIER) ? TMXR_MDM_DCD : 0);
    }

return SCPE_OK;
}


/* Read from a serial port.

   The port is checked for available characters.  If any are present, they are
   copied to the passed buffer, and the count of characters is returned.  If no
   characters are available, 0 is returned.  If an error occurs, -1 is returned.
   If a BREAK is detected on the communications line, the corresponding flag in
   the "brk" array is set.

   Implementation notes:

    1. A character with a framing or parity error is indicated in the input
       stream by the three-character sequence \377 \000 \ccc, where "ccc" is the
       bad character.  A communications line BREAK is indicated by the sequence
       \377 \000 \000.  A received \377 character is indicated by the
       two-character sequence \377 \377.  If we find any of these sequences,
       they are replaced by the single intended character by sliding the
       succeeding characters backward by one or two positions.  If a BREAK
       sequence was encountered, the corresponding location in the "brk" array
       is determined, and the flag is set.  Note that there may be multiple
       sequences in the buffer.
*/

int32 sim_read_serial (SERHANDLE port, char *buffer, int32 count, char *brk)
{
int read_count = 0;
uint32 status;
static uint32 term[2] = {0, 0};
unsigned char buf[4];
IOSB iosb;
SENSE_BUF sense;

status = sys$qiow (0, port->port, IO$_SENSEMODE | IO$M_TYPEAHDCNT, &iosb,
    0, 0, &sense, 8, 0, term, 0, 0);
if (status == SS$_NORMAL)
    status = iosb.status;
if (status != SS$_NORMAL) {
    sim_error_serial ("read", status);                      /* report unexpected error */
    return -1;
    }
if (sense.sense_count == 0)                                 /* no characters available? */
    return 0;                                               /* return 0 to indicate */
status = sys$qiow (0, port->port, IO$_READLBLK | IO$M_NOECHO | IO$M_NOFILTR | IO$M_TIMED | IO$M_TRMNOECHO, 
                   &iosb, 0, 0, buffer, (count < sense.sense_count) ? count : sense.sense_count, 0, term, 0, 0);
if (status == SS$_NORMAL)
    status = iosb.status;
if (status != SS$_NORMAL) {
    sim_error_serial ("read", status);                      /* report unexpected error */
    return -1;
    }
return (int32)iosb.count;                                   /* return the number of characters read */
}


/* Write to a serial port.

   "Count" characters are written from "buffer" to the serial port.  The actual
   number of characters written to the port is returned.  If an error occurred
   on writing, -1 is returned.
*/

int32 sim_write_serial (SERHANDLE port, char *buffer, int32 count)
{
uint32 status;

if (port->write_iosb.status == 0)           /* Prior write not done yet? */
    return 0;
status = sys$qio (0, port->port, IO$_WRITELBLK | IO$M_NOFORMAT,
                  &port->write_iosb, 0, 0, buffer, count, 0, 0, 0, 0);
if (status != SS$_NORMAL) {
    sim_error_serial ("write", status);                 /* report unexpected error */
    return -1;
    }
return (int32)count;                                    /* return number of characters written */
}


/* Close a serial port.

   The serial port is closed.  Errors are ignored.
*/

static void sim_close_os_serial (SERHANDLE port)
{
sys$dassgn (port->port);                                /* close the port */
free (port);
}

#else

/* Non-implemented stubs */

/* Enumerate the available serial ports. */

static int sim_serial_os_devices (int max, SERIAL_LIST* list)
{
return 0;
}

/* Open a serial port */

static SERHANDLE sim_open_os_serial (char *name)
{
return INVALID_HANDLE;
}


/* Configure a serial port */

static t_stat sim_config_os_serial (SERHANDLE port, SERCONFIG config)
{
return SCPE_IERR;
}


/* Control a serial port */

t_stat sim_control_serial (SERHANDLE port, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits)
{
return SCPE_NOFNC;
}


/* Read from a serial port */

int32 sim_read_serial (SERHANDLE port, char *buffer, int32 count, char *brk)
{
return -1;
}


/* Write to a serial port */

int32 sim_write_serial (SERHANDLE port, char *buffer, int32 count)
{
return -1;
}


/* Close a serial port */

static void sim_close_os_serial (SERHANDLE port)
{
}



#endif                                                  /* end else !implemented */
