/* sim_buildROMs.c: Boot ROM / Boot program load internal support

   Copyright (c) 2011, Mark Pizzolato

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
   MARK PIZZOLATO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   -

   This program builds C include files which can be used to contain the contents
   of ROM or other boot code needed by simulators.

   Current Internal ROM files being built:

      ROM/Boot File:     Include File:
      =======================================
      VAX/ka655x.bin     VAX/vax_ka655x_bin.h
      VAX/vmb.exe        VAX/vax780_vmb_exe.h


*/

#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <sys/utime.h>
#define utimbuf _utimbuf 
#define utime _utime
#else
#include <utime.h>
#endif

int sim_make_ROM_include(const char *rom_filename,
                         int expected_size,
                         int expected_checksum,
                         const char *include_filename, 
                         const char *rom_array_name)
{
FILE *rFile;
FILE *iFile;
time_t now;
int bytes_written = 0;
int c;
struct stat statb;
unsigned char *ROMData = NULL;
unsigned int checksum = 0;

if (NULL == (rFile = fopen (rom_filename, "rb"))) {
    printf ("Error Opening '%s' for input: %s\n", rom_filename, strerror(errno));
    return -1;
    }
if (stat (rom_filename, &statb)) {
    printf ("Error stating '%s': %s\n", rom_filename, strerror(errno));
    fclose (rFile);
    return -1;
    }
if (statb.st_size != expected_size) {
    printf ("Error: ROM file '%s' has an unexpected size: %d vs %d\n", rom_filename, (int)statb.st_size, expected_size);
    printf ("This can happen if the file was transferred or unpacked incorrectly\n");
    printf ("and in the process tried to convert line endings rather than passing\n");
    printf ("the file's contents unmodified\n");
    fclose (rFile);
    return -1;
    }
ROMData = malloc (statb.st_size);
if (statb.st_size != fread (ROMData, sizeof(*ROMData), statb.st_size, rFile)) {
    printf ("Error reading '%s': %s\n", rom_filename, strerror(errno));
    fclose (rFile);
    free (ROMData);
    return -1;
    }
fclose (rFile);
for (c=0; c<statb.st_size; ++c)
    checksum += ROMData[c];
checksum = ~checksum;
if ((expected_checksum != 0) && (checksum != expected_checksum)) {
    printf ("Error: ROM file '%s' has an unexpected checksum: 0x%08X vs 0x%08X\n", rom_filename, checksum, expected_checksum);
    printf ("This can happen if the file was transferred or unpacked incorrectly\n");
    printf ("and in the process tried to convert line endings rather than passing\n");
    printf ("the file's contents unmodified\n");
    fclose (rFile);
    return -1;
    }
/*
 * If the target include file already exists, determine if it contains the exact
 * data in the base ROM image.  If so, then we are already done
 */
if (iFile = fopen (include_filename, "r")) {
    unsigned char *IncludeData = NULL;
    char line[256];
    int Difference = 0;

    IncludeData = malloc (statb.st_size);

    while (fgets (line, sizeof(line), iFile)) {
        int byte;
        char *c;

        if (memcmp ("0x",line,2))
            continue;
        c = line;
        while (1 == sscanf (c, "0x%2Xd,", &byte)) {
            if (bytes_written >= statb.st_size)
                Difference = 1;
            else
                IncludeData[bytes_written++] = byte;
            c += 5;
            }
        if ((strchr (line,'}')) || Difference)
            break;
        }
    fclose (iFile);
    if (!Difference)
        Difference = memcmp (IncludeData, ROMData, statb.st_size);
    free (IncludeData);
    if (!Difference) {
        free (ROMData);
        return 0;
        }
    }

if (NULL == (iFile = fopen (include_filename, "w"))) {
    printf ("Error Opening '%s' for output: %s\n", include_filename, strerror(errno));
    return -1;
    }
time (&now);
fprintf (iFile, "#ifndef ROM_%s_H\n", rom_array_name);
fprintf (iFile, "#define ROM_%s_H 0\n", rom_array_name);
fprintf (iFile, "/*\n");
fprintf (iFile, "   %s         produced at %s", include_filename, ctime(&now));
fprintf (iFile, "   from %s which was last modified at %s", rom_filename, ctime(&statb.st_mtime));
fprintf (iFile, "   file size: %d (0x%X) - checksum: 0x%08X\n", (int)statb.st_size, (int)statb.st_size, checksum);
fprintf (iFile, "*/\n");
fprintf (iFile, "unsigned char %s[] = {", rom_array_name);
for (bytes_written=0;bytes_written<statb.st_size; ++bytes_written) {
    c = ROMData[bytes_written];
    if (0 == bytes_written%16)
        fprintf (iFile,"\n");
    fprintf (iFile,"0x%02X,", c&0xFF);
    }
free (ROMData);
fprintf (iFile,"};\n");
fprintf (iFile, "#endif /* ROM_%s_H */\n", rom_array_name);
fclose (iFile);
if (1) { /* Set Modification Time on the include file to be the modification time of the ROM file */
    struct utimbuf times;

    times.modtime = statb.st_mtime;
    times.actime = statb.st_atime;
    utime (include_filename, &times);
    }
return 0;
}

int
main(int argc, char **argv)
{
int status = 0;
status += sim_make_ROM_include ("VAX/ka655x.bin",  131072, 0xFF7673B6, "VAX/vax_ka655x_bin.h", "vax_ka655x_bin");
status += sim_make_ROM_include ("VAX/vmb.exe",      44544, 0xFFC014CC, "VAX/vax780_vmb_exe.h", "vax780_vmb_exe");
exit((status == 0) ? 0 : 2);
}
