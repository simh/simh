/* sim_serial.c: OS-dependent serial port routines

   Copyright (c) 2008-2020, J. David Bryan

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

   14-Jan-20    JDB     First release version
   22-Jul-19    JDB     Added VMIN and VTIME settings in 'sim_open_serial"
                        Added EAGAIN check to "sim_write_serial"
   07-Oct-08    JDB     Created file


   This module provides OS-dependent routines to access serial ports on the host
   machine.  The terminal multiplexer library uses these routines to provide
   serial connections to simulated terminal interfaces.

   Currently, the module supports Windows and UNIX.  Use on other systems
   returns error codes indicating that the functions failed, inhibiting serial
   port support in SIMH.

   The following routines are provided:

     sim_open_serial        open a serial port
     sim_config_serial      change baud rate and character framing configuration
     sim_control_serial     set or clear the serial control lines (e.g., DTR)
     sim_status_serial      get the serial status line values (e.g. DSR)
     sim_read_serial        read from a serial port
     sim_write_serial       write to a serial port
     sim_close_serial       close a serial port


   The calling sequences are as follows:


   SERHANDLE sim_open_serial (char *name)
   --------------------------------------

   The serial port referenced by the OS-dependent "name" is opened.  If the open
   is successful, and "name" refers to a serial port on the host system, then a
   handle to the port is returned.  If not, then the value INVALID_HANDLE is
   returned.


   t_stat sim_config_serial (SERHANDLE port, SERCONFIG config)
   -----------------------------------------------------------

   The baud rate and framing parameters (character size, parity, and number of
   stop bits) of the serial port associated with "port" are set.  If any
   "config" field value is unsupported by the host system, or if the combination
   of values (e.g., baud rate and number of stop bits) is unsupported, SCPE_ARG
   is returned.  If the configuration is successful, SCPE_OK is returned.  If
   the configuration fails, SCPE_IOERR is returned.


   t_stat sim_control_serial (SERHANDLE port, SERCIRCUIT control)
   --------------------------------------------------------------

   The DTR and RTS control lines of the serial port associated with "port" are
   asserted or denied as directed by the "control" parameter.  If the changes
   are successful, the function returns SCPE_OK.  SCPE_IOERR is returned if an
   error occurs.


   SERCIRCUIT sim_status_serial (SERHANDLE port)
   ---------------------------------------------

   The current DSR, CTS, DCD, and RI status line states of the serial port
   associated with "port" are obtained and returned as the value of the
   function.  If an error occurs, the "Error_Status" value is returned.


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

   The serial port indicated by "port" is closed.  Any errors are ignored.
*/



#include <ctype.h>

#include "sim_defs.h"
#include "sim_serial.h"



/* Console output.

   "cprintf" uses "(f)printf" to to write messages to the console and, if
   console logging is enabled, to the log output stream.  "..." is the format
   string and associated values.
*/

#define cprintf(...) \
          do { \
              printf (__VA_ARGS__); \
              if (sim_log) \
                  fprintf (sim_log, __VA_ARGS__); \
              } \
          while (0)



/* Windows serial implementation */


#if defined (_WIN32)


/* Generic error message handler.

   This routine should be called for unexpected errors.  Some error returns may
   be expected, e.g., a "file not found" error from an "open" routine.  These
   should return appropriate status codes to the caller, allowing SCP to print
   an error message if desired, rather than printing this generic error message.


   Implementation notes:

    1. The returned message has a CR LF appended.  This causes problems when the
       string is output via printf, as the text-mode translation doubles the CR.
       We avoid this by truncating the message at the last control sequence
*/

static void sim_error_serial (const char *routine)
{
const DWORD error = GetLastError ();            /* get the last error code */
LPTSTR      message;
DWORD       length;

length = FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM          /* get the system's error message */
                          | FORMAT_MESSAGE_IGNORE_INSERTS   /*   corresponding to the error code */
                          | FORMAT_MESSAGE_ALLOCATE_BUFFER, /*     into an allocated buffer */
                        NULL, error, 0,                     /*       using the default language setting */
                        (LPTSTR) &message, 0, NULL);

if (length > 0) {                                       /* if the message was found */
    while (iscntrl (message [--length]))                /*   then trim off the trailing CR LF */
        message [length] = '\0';                        /*     that FormatMessage has appended */

    cprintf ("%s simulator serial I/O error %lu from %s:\n  %s\n",  /* report the error to the console */
             sim_name, error, routine, message);

    LocalFree (message);                                /* free the allocated buffer */
    }

else                                                            /* otherwise the message was not found */
    cprintf ("%s simulator serial I/O error %lu from %s.\n",    /*   so report the error code only */
             sim_name, error, routine);

return;
}


/* Open a serial port.

   The serial port designated by the host "name" is opened, and the handle to
   the port is returned.  If an error occurs, INVALID_HANDLE is returned
   instead.  After opening, the port is configured with the default
   communication parameters established by the system, and the timeouts are set
   for immediate return on a read request to enable polling.


   Implementation notes:

    1. The "commsize" value cannot be "const" because the "GetDefaultCommConfig"
       takes a variable parameter.

    2. We call "GetDefaultCommConfig" to obtain the default communication
       parameters for the specified port.  If the name does not refer to a
       communications port (serial or parallel), the function fails.

    3. There is no way to limit "CreateFile" just to serial ports, so we must
       check after the port is opened.  The "GetCommState" routine will return
       an error if the handle does not refer to a serial port.

    4. Calling "GetDefaultCommConfig" for a serial port returns a structure
       containing a DCB.  This contains the default parameters.  However, some
       of the DCB fields are not set correctly, so we cannot use this directly
       in a call to "SetCommState".  Instead, we must copy the fields of
       interest to a DCB retrieved from a call to "GetCommState".
*/

SERHANDLE sim_open_serial (char *name)
{
SERHANDLE    port;
DCB          dcb;
COMMCONFIG   commdefault;
COMMTIMEOUTS cto;
DWORD        error;
DWORD        commsize = sizeof commdefault;

if (! GetDefaultCommConfig (name, &commdefault, &commsize)) {   /* get the default parameters; if the call failed */
    error = GetLastError ();                                    /*   then get the error code */

    if (error != ERROR_INVALID_PARAMETER)                       /* if it's not a bad port name */
        sim_error_serial ("GetDefaultCommConfig");              /*   then report the unexpected error */

    return INVALID_HANDLE;                                      /* return failure status */
    }

port = CreateFile (name, GENERIC_READ | GENERIC_WRITE,  /* open the port */
                   0, NULL, OPEN_EXISTING, 0, 0);

if (port == INVALID_HANDLE_VALUE) {                     /* if the open failed */
    error = GetLastError ();                            /*   then get the error code */

    if (error != ERROR_FILE_NOT_FOUND                   /* if it's not a bad filename */
      && error != ERROR_ACCESS_DENIED)                  /*   or it's already open */
        sim_error_serial ("CreateFile");                /*     then report the unexpected error */

    return INVALID_HANDLE;                              /* return failure status */
    }

if (! GetCommState (port, &dcb)) {                      /* get the current parameters; if the call failed */
    error = GetLastError ();                            /*   then get the error code */

    if (error != ERROR_INVALID_PARAMETER)               /* if it's something other than a bad port name */
        sim_error_serial ("GetCommState");              /*   then report the unexpected error */

    CloseHandle (port);                                 /* close the port */
    return INVALID_HANDLE;                              /*   and return failure status */
    }

dcb.BaudRate = commdefault.dcb.BaudRate;                /* copy the */
dcb.Parity   = commdefault.dcb.Parity;                  /*   default parameters */
dcb.ByteSize = commdefault.dcb.ByteSize;                /*    of interest */
dcb.StopBits = commdefault.dcb.StopBits;                /*      over the */
dcb.fOutX    = commdefault.dcb.fOutX;                   /*        current parameters */
dcb.fInX     = commdefault.dcb.fInX;

dcb.fDtrControl = DTR_CONTROL_DISABLE;                  /* disable DTR and RTS initially */
dcb.fRtsControl = RTS_CONTROL_DISABLE;                  /*   until the poll connects */

if (! SetCommState (port, &dcb)) {                      /* configure the port with default parameters; if it failed */
    sim_error_serial ("SetCommState");                  /*   then report the unexpected error */

    CloseHandle (port);                                 /* close the port */
    return INVALID_HANDLE;                              /*   and return failure status */
    }

cto.ReadIntervalTimeout         = MAXDWORD;             /* set the port to return immediately on read */
cto.ReadTotalTimeoutMultiplier  = 0;                    /*   i.e., to enable polling */
cto.ReadTotalTimeoutConstant    = 0;
cto.WriteTotalTimeoutMultiplier = 0;
cto.WriteTotalTimeoutConstant   = 0;

if (! SetCommTimeouts (port, &cto)) {                   /* configure the port timeouts; if the call failed */
    sim_error_serial ("SetCommTimeouts");               /*   then report the unexpected error */

    CloseHandle (port);                                 /* close the port */
    return INVALID_HANDLE;                              /*   and return failure status */
    }

return port;                                            /* return the port handle on success */
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

t_stat sim_config_serial (SERHANDLE port, SERCONFIG config)
{
DCB    dcb;
DWORD  error;
uint32 i;

static const struct {
    char parity;
    BYTE parity_code;
    } parity_map [] =
        { { 'E', EVENPARITY }, { 'M', MARKPARITY  }, { 'N', NOPARITY },
          { 'O', ODDPARITY  }, { 'S', SPACEPARITY } };

static const uint32 parity_count = (uint32) (sizeof parity_map / sizeof parity_map [0]);

if (! GetCommState (port, &dcb)) {                      /* get the current comm parameters; if the call failed */
    sim_error_serial ("GetCommState");                  /*   then report the unexpected error */
    return SCPE_IOERR;                                  /*     and return failure status */
    }

dcb.BaudRate = config.baudrate;                         /* assign the baud rate */

if (config.charsize >= 5 && config.charsize <= 8)       /* if the character size is within range */
    dcb.ByteSize = (BYTE) config.charsize;              /*   then assign the character size */
else                                                    /* otherwise */
    return SCPE_ARG;                                    /*   report that the value is not a valid size */

for (i = 0; i < parity_count; i++)                      /* loop through the parity map */
    if (config.parity == parity_map [i].parity) {       /* if the requested parity matches a map entry */
        dcb.Parity = parity_map [i].parity_code;        /*   then assign the corresponding code */
        break;
        }

if (i == parity_count)                                  /* if the requested parity did not match */
    return SCPE_ARG;                                    /*   then report that it is not a valid parity specifier */

if (config.stopbits == 1)                               /* if one stop bit is requested */
    dcb.StopBits = ONESTOPBIT;                          /*   then set the configuration value */
else if (config.stopbits == 2)                          /* otherwise if two stop bits are requested */
    dcb.StopBits = TWOSTOPBITS;                         /*   then set the configuration value */
else if (config.stopbits == 0)                          /* otherwise if 1.5 stop bits are requested */
    dcb.StopBits = ONE5STOPBITS;                        /*   then set the configuration value */
else                                                    /* otherwise */
    return SCPE_ARG;                                    /*   report that the value not a valid number of stop bits */

if (! SetCommState (port, &dcb)) {                      /* set the configuration; if the call failed */
    error = GetLastError ();                            /*   then get the error code */

    if (error == ERROR_INVALID_PARAMETER)               /* if the configuration is invalid */
        return SCPE_ARG;                                /*   then report an argument error */

    else {                                              /* otherwise */
        sim_error_serial ("SetCommState");              /*   report the unexpected error */
        return SCPE_IOERR;                              /*     and return failure status */
        }
    }

return SCPE_OK;                                         /* return success status */
}


/* Control a serial port.

   The DTR and RTS control lines of the serial port associated with "port" are
   asserted or denied as directed by the "control" parameter.  If the changes
   are successful, the function returns SCPE_OK.  SCPE_IOERR is returned if an
   error occurs.
*/

t_stat sim_control_serial (SERHANDLE port, SERCIRCUIT control)
{
DWORD DTR_function, RTS_function;

if (control & DTR_Control)                              /* if DTR assertion is requested */
    DTR_function = SETDTR;                              /*   then set the configuration value */
else                                                    /* otherwise */
    DTR_function = CLRDTR;                              /*   clear the configuration value */

if (control & RTS_Control)                              /* if RTS assertion is requested */
    RTS_function = SETRTS;                              /*   then set the configuration value */
else                                                    /* otherwise */
    RTS_function = CLRRTS;                              /*   clear the configuration value */

if (! EscapeCommFunction (port, DTR_function)) {        /* configure the DTR line; if the call failed */
    sim_error_serial ("EscapeCommFunction DTR");        /*   then report the unexpected error */
    return SCPE_IOERR;                                  /*     and return I/O error status */
    }

else if (! EscapeCommFunction (port, RTS_function)) {   /* otherwise configure the RTS line; if the call failed */
    sim_error_serial ("EscapeCommFunction RTS");        /*   then report the unexpected error */
    return SCPE_IOERR;                                  /*     and return I/O error status */
    }

else                                                    /* otherwise both calls succeeded */
    return SCPE_OK;                                     /*   so return success status */
}


/* Get the current status from a serial port.

   The current DSR, CTS, DCD, and RI status line states of the serial port
   associated with "port" are obtained and returned as the value of the
   function.  If an error occurs, the "Error_Status" value is returned.
*/

SERCIRCUIT sim_status_serial (SERHANDLE port)
{
DWORD      state;
SERCIRCUIT status = No_Signals;

if (! GetCommModemStatus (port, &state)) {              /* get the serial line state; if the call failed */
    sim_error_serial ("GetCommModemStatus");            /*   then report the unexpected error */
    return Error_Status;                                /*     and return the error status */
    }

else {                                                  /* otherwise */
    if (state & MS_DSR_ON)                              /*   if the DSR line is asserted */
        status |= DSR_Status;                           /*     then include DSR status */

    if (state & MS_CTS_ON)                              /* if the CTS line is asserted */
        status |= CTS_Status;                           /*   then include DSR status */

    if (state & MS_RLSD_ON)                             /* if the DCD line is asserted */
        status |= DCD_Status;                           /*   then include DCD status */

    if (state & MS_RING_ON)                             /* if the RI line is asserted */
        status |= RI_Status;                            /*   then include RI status */

    return status;                                      /* return the combined status set */
    }
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
       character is set as our "best guess."
*/

int32 sim_read_serial (SERHANDLE port, char *buffer, int32 count, char *brk)
{
DWORD   read;
DWORD   commerrors;
COMSTAT cs;
char    *bptr;

if (! ClearCommError (port, &commerrors, &cs)) {        /* get the comm error flags; if the call failed  */
    sim_error_serial ("ClearCommError");                /*   then report the unexpected error */
    return -1;                                          /*     and return failure status */
    }

if (! ReadFile (port, (LPVOID) buffer,                  /* read any available characters; if the call failed */
                (DWORD) count, &read, NULL)) {
    sim_error_serial ("ReadFile");                      /*   then report the unexpected error */
    return -1;                                          /*     and return failure status */
    }

if (commerrors & CE_BREAK) {                            /* if a BREAK was detected */
    bptr = (char *) memchr (buffer, 0, read);           /*   then search for the first NUL in the buffer */

    if (bptr != NULL)                                   /* if a NUL was found */
        brk = brk + (bptr - buffer);                    /*   then calculate its position */

    *brk = 1;                                           /* set the BREAK flag in the caller's array */
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
DWORD written;

if (! WriteFile (port, (LPVOID) buffer,                 /* write the buffer to the serial port; if the call failed */
                (DWORD) count, &written, NULL)) {
    sim_error_serial ("WriteFile");                     /*   then report the unexpected error */
    return -1;                                          /*     and return failure status */
    }

else                                                    /* otherwise */
    return written;                                     /*   return the number of characters written */
}


/* Close a serial port.

   The serial port is closed.  Errors are ignored.
*/

void sim_close_serial (SERHANDLE port)
{
CloseHandle (port);                                     /* close the port */
return;
}



/* UNIX implementation */


#elif defined (__unix__) || defined (__APPLE__) && defined (__MACH__)


/* Generic error message handler.

   This routine should be called for unexpected errors.  Some error returns may
   be expected, e.g., a "file not found" error from an "open" routine.  These
   should return appropriate status codes to the caller, allowing SCP to print
   an error message if desired, rather than printing this generic error message.
*/

static void sim_error_serial (const char *routine)
{
cprintf ("%s simulator serial I/O error %d from %s:\n  %s.\n", /* report the error to the console */
         sim_name, errno, routine, strerror (errno));

return;
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

    4. POSIX.1-2001 does not specify whether the setting of O_NONBLOCK takes
       precedence over MIN or TIME settings.  So we configure the latter to
       return immediately as well, using Case D as described in the
       "Non-Canonical Mode Input Processing" section of the Single UNIX
       Specification version 3, which says: "Case D: MIN=0, TIME=0 The minimum
       of either the number of bytes requested or the number of bytes currently
       available shall be returned without waiting for more bytes to be input.
       If no characters are available, read() shall return a value of zero,
       having read no data."
*/

SERHANDLE sim_open_serial (char *name)
{
SERHANDLE port;
struct termios tio;

static const tcflag_t i_clear =                 /* clear these input modes */
    IGNBRK |                                    /*   ignore BREAK */
    BRKINT |                                    /*   signal on BREAK */
    INPCK  |                                    /*   enable parity checking */
    ISTRIP |                                    /*   strip character to 7 bits */
    INLCR  |                                    /*   map NL to CR */
    IGNCR  |                                    /*   ignore CR */
    ICRNL  |                                    /*   map CR to NL */
    IXON   |                                    /*   enable XON/XOFF output control */
    IXOFF;                                      /*   enable XON/XOFF input control */

static const tcflag_t i_set =                   /* set these input modes */
    PARMRK |                                    /*   mark parity errors and line breaks */
    IGNPAR;                                     /*   ignore parity errors */

static const tcflag_t o_clear =                 /* clear these output modes */
    OPOST;                                      /*   post-process output */

static const tcflag_t o_set = 0;                /* set these output modes (none) */

static const tcflag_t c_clear =                 /* clear these control modes */
    HUPCL;                                      /*   hang up line on last close */

static const tcflag_t c_set =                   /* set these control modes */
    CREAD |                                     /*   enable receiver */
    CLOCAL;                                     /*   ignore modem status lines */

static const tcflag_t l_clear =                 /* clear these local modes */
    ISIG    |                                   /*   enable signals */
    ICANON  |                                   /*   canonical input */
    ECHO    |                                   /*   echo characters */
    ECHOE   |                                   /*   echo ERASE as an error correcting backspace */
    ECHOK   |                                   /*   echo KILL */
    ECHONL  |                                   /*   echo NL */
    NOFLSH  |                                   /*   disable flush after interrupt */
    TOSTOP  |                                   /*   send SIGTTOU for background output */
    IEXTEN;                                     /*   enable extended functions */

static const tcflag_t l_set = 0;                /* set these local modes (none) */


port = open (name, O_RDWR | O_NOCTTY | O_NONBLOCK);     /* open the port */

if (port == -1) {                                       /* if the open failed */
    if (errno != ENOENT && errno != EACCES)             /*   then if it's not file not found or can't open */
        sim_error_serial ("open");                      /*     then report the unexpected error */

    return INVALID_HANDLE;                              /* return failure status */
    }

if (! isatty (port)) {                                  /* if the device is not a TTY */
    close (port);                                       /*   then close the port */
    return INVALID_HANDLE;                              /*     and return failure status */
    }

if (tcgetattr (port, &tio)) {                           /* get the terminal attributes; if the call failed */
    sim_error_serial ("tcgetattr");                     /*   then report the unexpected error */

    close (port);                                       /* close the port */
    return INVALID_HANDLE;                              /*   and return failure status */
    }

tio.c_iflag = tio.c_iflag & ~i_clear | i_set;           /* reconfigure the serial line */
tio.c_oflag = tio.c_oflag & ~o_clear | o_set;           /*   for non-canonical */
tio.c_cflag = tio.c_cflag & ~c_clear | c_set;           /*     input mode */
tio.c_lflag = tio.c_lflag & ~l_clear | l_set;

#if defined (VMIN) && defined (VTIME)

tio.c_cc [VMIN]  = 0;                                   /* read returns zero if there is no data */
tio.c_cc [VTIME] = 0;                                   /*   with no timeout */

#endif

if (tcsetattr (port, TCSANOW, &tio)) {                  /* set the terminal attributes; if the call failed */
    sim_error_serial ("tcsetattr");                     /*   then report the unexpected error */

    close (port);                                       /* close the port */
    return INVALID_HANDLE;                              /*   and return failure status */
    }

return port;                                            /* return the port handle on success */
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

t_stat sim_config_serial (SERHANDLE port, SERCONFIG config)
{
struct termios tio;
uint32 i;

static const struct {
    uint32  rate;
    speed_t rate_code;
    } baud_map [] =
        { { 50,     B50     }, { 75,     B75     }, { 110,    B110    }, {  134,   B134   },
          { 150,    B150    }, { 200,    B200    }, { 300,    B300    }, {  600,   B600   },
          { 1200,   B1200   }, { 1800,   B1800   }, { 2400,   B2400   }, {  4800,  B4800  },
          { 9600,   B9600   }, { 19200,  B19200  }, { 38400,  B38400  }, {  57600, B57600 },
          { 115200, B115200 } };

static const uint32 baud_count = (uint32) (sizeof baud_map / sizeof baud_map [0]);

static const tcflag_t charsize_map [4] = { CS5, CS6, CS7, CS8 };


if (tcgetattr (port, &tio)) {                           /* get the current configuration; if the call failed */
    sim_error_serial ("tcgetattr");                     /*   then report the unexpected error */
    return SCPE_IOERR;                                  /*     and return I/O error status */
    }

for (i = 0; i < baud_count; i++)                        /* loop through the baud rate map */
    if (config.baudrate == baud_map [i].rate) {         /* if the requested rate matches a map entry */
        cfsetispeed (&tio, baud_map [i].rate_code);     /*   then set the input rate */
        cfsetospeed (&tio, baud_map [i].rate_code);     /*     and the output rate */
        break;
        }

if (i == baud_count)                                    /* if the baud rate was not assigned */
    return SCPE_ARG;                                    /*   then return bad argument status */

if (config.charsize >= 5 && config.charsize <= 8)       /* if the character size value is valid */
    tio.c_cflag = tio.c_cflag & ~CSIZE                  /*   then replace the character size code */
                    | charsize_map [config.charsize - 5];
else                                                    /* otherwise */
    return SCPE_ARG;                                    /*   return "not a valid size" status */

switch (config.parity) {                                /* assign the requested parity */
    case 'E':
        tio.c_cflag = tio.c_cflag & ~PARODD | PARENB;   /* set for even parity */
        break;

    case 'N':
        tio.c_cflag = tio.c_cflag & ~PARENB;            /* set for no parity */
        break;

    case 'O':
        tio.c_cflag = tio.c_cflag | PARODD | PARENB;    /* set for odd parity */
        break;

    default:
        return SCPE_ARG;                                /*  return "not a valid parity specifier" status */
    }

if (config.stopbits == 1)                               /* if one stop bit is requested */
    tio.c_cflag = tio.c_cflag & ~CSTOPB;                /*   then clear the two-stop-bits flag */
else if (config.stopbits == 2)                          /* otherwise if two stop bits are requested */
    tio.c_cflag = tio.c_cflag | CSTOPB;                 /*   then set the two-stop-bits flag */
else                                                    /* otherwise */
    return SCPE_ARG;                                    /*   return "not a valid number of stop bits" status */

if (tcsetattr (port, TCSAFLUSH, &tio)) {                /* set the new configuration; if the call failed */
    sim_error_serial ("tcsetattr");                     /*   then report the unexpected error */
    return SCPE_IERR;                                   /*     and return failure status */
    }

return SCPE_OK;                                         /* the configuration was set successfully */
}


/* Control a serial port.

   The DTR and RTS control lines of the serial port associated with "port" are
   asserted or denied as directed by the "control" parameter.  If the changes
   are successful, the function returns SCPE_OK.  SCPE_IOERR is returned if an
   error occurs.
*/

t_stat sim_control_serial (SERHANDLE port, SERCIRCUIT control)
{
int state;

if (ioctl (port, TIOCMGET, &state)) {                   /* get the current modem line state; if the call failed */
    if (errno != EINVAL)                                /*   then if the error is not "control not supported" */
        sim_error_serial ("ioctl TIOCMGET");            /*     then report the unexpected error */

    return SCPE_IOERR;                                  /* return failure status */
    }

if (control & DTR_Control)                              /* if DTR assertion is requested */
    state |= TIOCM_DTR;                                 /*   then set the configuration value */
else                                                    /* otherwise */
    state &= ~TIOCM_DTR;                                /*   clear the configuration value */

if (control & RTS_Control)                              /* if RTS assertion is requested */
    state |= TIOCM_RTS;                                 /*   then set the configuration value */
else                                                    /* otherwise */
    state &= ~TIOCM_RTS;                                /*   clear the configuration value */

if (ioctl (port, TIOCMSET, &state)) {                   /* set the new line state; if the call failed */
    if (errno != EINVAL)                                /*   then if the error is not "control not supported" */
        sim_error_serial ("ioctl TIOCMSET");            /*     then report the unexpected error */

    return SCPE_IOERR;                                  /* return failure status */
    }

else                                                    /* otherwise both calls succeeded */
    return SCPE_OK;                                     /*   so return success status */
}


/* Get the current status from a serial port.

   The current DSR, CTS, DCD, and RI status line states of the serial port
   associated with "port" are obtained and returned as the value of the
   function.  If an error occurs, the "Error_Status" value is returned.
*/

SERCIRCUIT sim_status_serial (SERHANDLE port)
{
int        state;
SERCIRCUIT status = No_Signals;

if (ioctl (port, TIOCMGET, &state)) {                   /* get the serial line state; if the call failed */
    if (errno != EINVAL)                                /*   then if it's not "unsupported call" */
        sim_error_serial ("ioctl TIOCMGET");            /*     then report the unexpected error */

    return Error_Status;                                /* return the error status */
    }

else {                                                  /* otherwise */
    if (state & TIOCM_DSR)                              /*   if the DSR line is asserted */
        status |= DSR_Status;                           /*     then include DSR status */

    if (state & TIOCM_CTS)                              /* if the CTS line is asserted */
        status |= CTS_Status;                           /*   then include DSR status */

    if (state & TIOCM_CD)                               /* if the DCD line is asserted */
        status |= DCD_Status;                           /*   then include DCD status */

    if (state & TIOCM_RI)                               /* if the RI line is asserted */
        status |= RI_Status;                            /*   then include RI status */

    return status;                                      /* return the combined status set */
    }
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
int   read_count;
char  *bptr, *cptr;
int32 remaining;

read_count = read (port, (void *) buffer, (size_t) count);  /* read from the serial port */

if (read_count == -1)                                   /* if the call failed */
    if (errno == EAGAIN)                                /*   then if no characters are available */
        return 0;                                       /*     then report no characters were returned */
    else                                                /*   otherwise */
        sim_error_serial ("read");                      /*     report the unexpected error */

else {                                                  /* otherwise the read succeeded */
    cptr = buffer;                                      /*   so point at the start of the buffer */
    remaining = read_count - 1;                         /*     and stop the search one character from the end */

    while (remaining > 0                                /* search for error sequences */
      && (bptr = memchr (cptr, '\377', remaining))) {   /*   starting with a PARMRK sequence */
        remaining = remaining - (bptr - cptr) - 1;      /* found one; calculate the count of characters remaining */

        if (*(bptr + 1) == '\377') {                    /* if this is a \377 \377 sequence */
            memmove (bptr + 1, bptr + 2, remaining);    /*   then slide the string backward to leave one \377 */
            remaining = remaining - 1;                  /* drop the remaining count */
            read_count = read_count - 1;                /*   and the read count by the character eliminated */
            }

        else if (remaining > 0 && *(bptr + 1) == '\0') {    /* otherwise if this is a \377 \000 \ccc sequence */
            memmove (bptr, bptr + 2, remaining);            /*   then slide the string backward to leave \ccc */
            remaining = remaining - 2;                      /* drop the remaining count */
            read_count = read_count - 2;                    /*   and the read count by the characters eliminated */

            if (*bptr == '\0')                          /* if this is a BREAK sequence */
                *(brk + (bptr - buffer)) = 1;           /*   then set the corresponding BREAK flag */
            }

        cptr = bptr + 1;                                /* point at the remainder of the string */
        }                                               /*   and loop until the entire string is searched */
    }

return (int32) read_count;                              /* return the number of characters read */
}


/* Write to a serial port.

   "Count" characters are written from "buffer" to the serial port.  The actual
   number of characters written to the port is returned.  If an error occurred
   on writing, -1 is returned.
*/

int32 sim_write_serial (SERHANDLE port, char *buffer, int32 count)
{
int written;

written = write (port, (void *) buffer, (size_t) count);    /* write the buffer to the serial port */

if (written == -1)                                      /* if an error occurred */
    if (errno == EAGAIN)                                /*   then if the write should be tried again */
        return 0;                                       /*     then return 0 bytes written */
    else                                                /*   otherwise */
        sim_error_serial ("write");                     /*     report an unexpected error */

return (int32) written;                                 /* return number of characters written */
}


/* Close a serial port.

   The serial port is closed.  Errors are ignored.
*/

void sim_close_serial (SERHANDLE port)
{
close (port);                                           /* close the port */
return;
}



/* Non-implemented stubs */

#else


/* Open a serial port */

SERHANDLE sim_open_serial (char *name)
{
return INVALID_HANDLE;
}


/* Configure a serial port */

t_stat sim_config_serial (SERHANDLE port, SERCONFIG config)
{
return SCPE_IERR;
}


/* Control a serial port */

t_stat sim_control_serial (SERHANDLE port, SERCIRCUIT control)
{
return SCPE_IERR;
}


/* Get the current status from a serial port */

SERCIRCUIT sim_status_serial (SERHANDLE port)
{
return Error_Status;
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

void sim_close_serial (SERHANDLE port)
{
return;
}


#endif                                                  /* end else unimplemented */
