/* sim_serial.c: OS-dependent serial port routines

   Copyright (c) 2008, J. David Bryan

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


   This module provides OS-dependent routines to access serial ports on the host
   machine.  The terminal multiplexer library uses these routines to provide
   serial connections to simulated terminal interfaces.

   Currently, the module supports Windows and UNIX.  Use on other systems
   returns error codes indicating that the functions failed, inhibiting serial
   port support in SIMH.

   The following routines are provided:

     sim_open_serial        open a serial port
     sim_config_serial      change baud rate and character framing configuration
     sim_control_serial     connect or disconnect a serial port (controls DTR)
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
   is returned.  If the configuration is successful, SCPE_OK is returned.


   t_bool sim_control_serial (SERHANDLE port, t_bool connect)
   ----------------------------------------------------------

   If "connect" is TRUE, the DTR (modem control) line of the serial port
   associated with "port" is asserted.  If "connect" is false, the line is
   denied.  If the DTR change is successful, the function returns TRUE.  FALSE
   is returned if an error occurs.


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
*/


#include "sim_defs.h"
#include "sim_serial.h"



/* Generic error message handler.

   This routine should be called for unexpected errors.  Some error returns may
   be expected, e.g., a "file not found" error from an "open" routine.  These
   should return appropriate status codes to the caller, allowing SCP to print
   an error message if desired, rather than printing this generic error message.
*/

static void sim_error_serial (char *routine, int error)
{
fprintf (stderr, "Serial: %s fails with error %d\n", routine, error);
return;
}



/* Windows serial implementation */


#if defined (_WIN32)


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

SERHANDLE sim_open_serial (char *name)
{
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

port = CreateFile (name, GENERIC_READ | GENERIC_WRITE,  /* open the port */
                   0, NULL, OPEN_EXISTING, 0, 0);

if (port == INVALID_HANDLE_VALUE) {                     /* open failed? */
    error = GetLastError ();                            /* get error code */

    if ((error != ERROR_FILE_NOT_FOUND) &&              /* bad filename? */
        (error != ERROR_ACCESS_DENIED))                 /* already open? */
        sim_error_serial ("CreateFile", (int) error);   /* no, so report unexpected error */

    return INVALID_HANDLE;                              /* indicate bad port name */
    }

if (!GetCommState (port, &dcb)) {                       /* get the current comm parameters */
    error = GetLastError ();                            /* function failed; get error */

    if (error != ERROR_INVALID_PARAMETER)               /* not a serial port name? */
        sim_error_serial ("GetCommState", (int) error); /* no, so report unexpected error */

    CloseHandle (port);                                 /* close the port */
    return INVALID_HANDLE;                              /*   and indicate bad port name */
    }

dcb.BaudRate = commdefault.dcb.BaudRate;                /* copy default parameters of interest */
dcb.Parity   = commdefault.dcb.Parity;
dcb.ByteSize = commdefault.dcb.ByteSize;
dcb.StopBits = commdefault.dcb.StopBits;
dcb.fOutX    = commdefault.dcb.fOutX;
dcb.fInX     = commdefault.dcb.fInX;

dcb.fDtrControl = DTR_CONTROL_DISABLE;                  /* disable DTR initially until poll connects */

if (!SetCommState (port, &dcb)) {                       /* configure the port with default parameters */
    sim_error_serial ("SetCommState",                   /* function failed; report unexpected error */
                      (int) GetLastError ());
    CloseHandle (port);                                 /* close port */
    return INVALID_HANDLE;                              /*   and indicate failure to caller */
    }

cto.ReadIntervalTimeout         = MAXDWORD;             /* set port to return immediately on read */
cto.ReadTotalTimeoutMultiplier  = 0;                    /* i.e., to enable polling */
cto.ReadTotalTimeoutConstant    = 0;
cto.WriteTotalTimeoutMultiplier = 0;
cto.WriteTotalTimeoutConstant   = 0;

if (!SetCommTimeouts (port, &cto)) {                    /* configure port timeouts */
    sim_error_serial ("SetCommTimeouts",                /* function failed; report unexpected error */
                      (int) GetLastError ());
    CloseHandle (port);                                 /* close port */
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

t_stat sim_config_serial (SERHANDLE port, SERCONFIG config)
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

if (!GetCommState (port, &dcb)) {                       /* get the current comm parameters */
    sim_error_serial ("GetCommState",                   /* function failed; report unexpected error */
                      (int) GetLastError ());
    return SCPE_IOERR;                                  /* return failure status */
    }

dcb.BaudRate = config.baudrate;                         /* assign baud rate */

if (config.charsize >= 5 && config.charsize <= 8)       /* character size OK? */
    dcb.ByteSize = config.charsize;                     /* assign character size */
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

if (!SetCommState (port, &dcb)) {                       /* set the configuration */
    error = GetLastError ();                            /* check for error */

    if (error == ERROR_INVALID_PARAMETER)               /* invalid configuration? */
        return SCPE_ARG;                                /* report as argument error */

    sim_error_serial ("SetCommState", (int) error);     /* function failed; report unexpected error */
    return SCPE_IOERR;                                  /* return failure status */
    }

return SCPE_OK;                                         /* return success status */
}


/* Control a serial port.

   The DTR line of the serial port is set or cleared.  If "connect" is true,
   then the line is set to enable the serial device.  If "connect" is false, the
   line is disabled to disconnect the device.  If the line change was
   successful, the function returns TRUE.
*/

t_bool sim_control_serial (SERHANDLE port, t_bool connect)
{
if (!EscapeCommFunction (port, connect ? SETDTR : CLRDTR)) {
    sim_error_serial ("EscapeCommFunction", (int) GetLastError ());
    return FALSE;
    }

return TRUE;
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

if (!ClearCommError (port, &commerrors, &cs)) {         /* get the comm error flags  */
    sim_error_serial ("ClearCommError",                 /* function failed; report unexpected error */
                      (int) GetLastError ());
    return -1;                                          /* return failure to caller */
    }

if (!ReadFile (port, (LPVOID) buffer,                   /* read any available characters */
               (DWORD) count, &read, NULL)) {
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
DWORD written;

if (!WriteFile (port, (LPVOID) buffer,                  /* write the buffer to the serial port */
                (DWORD) count, &written, NULL)) {
    sim_error_serial ("WriteFile",                      /* function failed; report unexpected error */
                      (int) GetLastError ());
    return -1;                                          /* return failure to caller */
    }
else
    return written;                                     /* return number of characters written */
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


#elif defined (__unix__)


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

SERHANDLE sim_open_serial (char *name)
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


SERHANDLE port;
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

tio.c_iflag = tio.c_iflag & ~i_clear | i_set;           /* configure the serial line for raw mode */
tio.c_oflag = tio.c_oflag & ~o_clear | o_set;
tio.c_cflag = tio.c_cflag & ~c_clear | c_set;
tio.c_lflag = tio.c_lflag & ~l_clear | l_set;

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

return port;                                            /* return port fd for success */
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


if (tcgetattr (port, &tio)) {                           /* get the current configuration */
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

if (config.charsize >= 5 && config.charsize <= 8)       /* character size OK? */
    tio.c_cflag = tio.c_cflag & ~CSIZE |                /* replace character size code */
                charsize_map [config.charsize - 5];
else
    return SCPE_ARG;                                    /* not a valid size */

switch (config.parity) {                                /* assign parity */
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
        return SCPE_ARG;                                /* not a valid parity specifier */
    }

if (config.stopbits == 1)                               /* one stop bit? */
    tio.c_cflag = tio.c_cflag & ~CSTOPB;                /* clear two-bits flag */
else if (config.stopbits == 2)                          /* two stop bits? */
    tio.c_cflag = tio.c_cflag | CSTOPB;                 /* set two-bits flag */
else                                                    /* some other number? */
    return SCPE_ARG;                                    /* not a valid number of stop bits */

if (tcsetattr (port, TCSAFLUSH, &tio)) {                /* set the new configuration */
    sim_error_serial ("tcsetattr", errno);              /* function failed; report unexpected error */
    return SCPE_IERR;                                   /* return failure status */
    }

return SCPE_OK;                                         /* configuration set successfully */
}


/* Control a serial port.

   The DTR line of the serial port is set or cleared.  If "connect" is true,
   then the line is set to enable the serial device.  If "connect" is false, the
   line is disabled to disconnect the device.  If the line change was
   successful, the function returns TRUE.
*/

t_bool sim_control_serial (SERHANDLE port, t_bool connect)
{
int request;
static const int dtr = TIOCM_DTR;

if (connect)                                            /* request for DTR set? */
    request = TIOCMBIS;                                 /* use "set" control request */
else                                                    /* DTR clear */
    request = TIOCMBIC;                                 /* use "clear" control request */

if (ioctl (port, request, &dtr)) {                      /* set or clear the DTR line */
    if (errno != EINVAL)                                /* DTR control not supported? */
        sim_error_serial ("ioctl", errno);              /* no, so report unexpected error */

    return FALSE;                                       /* return failure status */
    }

return TRUE;                                            /* control request succeeded */
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

read_count = read (port, (void *) buffer, (size_t) count);  /* read from the serial port */

if (read_count == -1)                                       /* read error? */
    if (errno == EAGAIN)                                    /* no characters available? */
        return 0;                                           /* return 0 to indicate */
    else                                                    /* some other problem */
        sim_error_serial ("read", errno);                   /* report unexpected error */

else {                                                      /* read succeeded */
    cptr = buffer;                                          /* point at start of buffer */
    remaining = read_count - 1;                             /* stop search one char from end of string */

    while (remaining > 0 &&                                 /* still characters to search? */
           (bptr = memchr (cptr, '\377', remaining))) {     /* search for start of PARMRK sequence */
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

written = write (port, (void *) buffer, (size_t) count);    /* write the buffer to the serial port */

if (written == -1)                                          /* write error? */
    sim_error_serial ("write", errno);                      /* report unexpected error */

return (int32) written;                                     /* return number of characters written */
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

t_bool sim_control_serial (SERHANDLE port, t_bool connect)
{
return FALSE;
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



#endif                                                  /* end else !implemented */
