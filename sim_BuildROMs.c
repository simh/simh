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

*/
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
/*

   This program builds C include files which can be used to contain the contents
   of ROM or other boot code needed by simulators.

   Current Internal ROM files being built:

      ROM/Boot File:     Include File:            Size:                 Checksum:
      =======================================================================================
*/
struct ROM_File_Descriptor {
    const char *BinaryName;             const char *IncludeFileName; size_t expected_size; unsigned int checksum;  const char *ArrayName;            const char *Comments;} ROMs[] = {
   {"VAX/ka655x.bin",                   "VAX/vax_ka655x_bin.h",                    131072,            0xFF7672D5,        "vax_ka655x_bin"},
   {"VAX/ka620.bin",                    "VAX/vax_ka620_bin.h",                      65536,            0xFF7F930F,        "vax_ka620_bin"},
   {"VAX/ka630.bin",                    "VAX/vax_ka630_bin.h",                      65536,            0xFF7F73EF,        "vax_ka630_bin"},
   {"VAX/ka610.bin",                    "VAX/vax_ka610_bin.h",                      16384,            0xFFEF3312,        "vax_ka610_bin"},
   {"VAX/ka410.bin",                    "VAX/vax_ka410_bin.h",                     262144,            0xFEDA0B61,        "vax_ka410_bin"},
   {"VAX/ka411.bin",                    "VAX/vax_ka411_bin.h",                     262144,            0xFECB7EE3,        "vax_ka411_bin"},
   {"VAX/ka412.bin",                    "VAX/vax_ka412_bin.h",                     262144,            0xFED96BB4,        "vax_ka412_bin"},
   {"VAX/ka41a.bin",                    "VAX/vax_ka41a_bin.h",                     262144,            0xFECBAC7B,        "vax_ka41a_bin"},
   {"VAX/ka41d.bin",                    "VAX/vax_ka41d_bin.h",                     262144,            0xFECB8513,        "vax_ka41d_bin"},
   {"VAX/ka42a.bin",                    "VAX/vax_ka42a_bin.h",                     262144,            0xFED8967F,        "vax_ka42a_bin"},
   {"VAX/ka42b.bin",                    "VAX/vax_ka42b_bin.h",                     262144,            0xFECBB2EF,        "vax_ka42b_bin"},
   {"VAX/ka43a.bin",                    "VAX/vax_ka43a_bin.h",                     262144,            0xFEAB1DF9,        "vax_ka43a_bin"},
   {"VAX/ka46a.bin",                    "VAX/vax_ka46a_bin.h",                     262144,            0xFE8D094C,        "vax_ka46a_bin"},
   {"VAX/ka47a.bin",                    "VAX/vax_ka47a_bin.h",                     262144,            0xFE8D8DDA,        "vax_ka47a_bin"},
   {"VAX/ka48a.bin",                    "VAX/vax_ka48a_bin.h",                     262144,            0xFEBB854D,        "vax_ka48a_bin"},
   {"VAX/is1000.bin",                   "VAX/vax_is1000_bin.h",                    524288,            0xFCBCD74A,        "vax_is1000_bin"},
   {"VAX/ka410_xs.bin",                 "VAX/vax_ka410_xs_bin.h",                   32768,            0xFFD8BD83,        "vax_ka410_xs_bin"},
   {"VAX/ka420_rdrz.bin",               "VAX/vax_ka420_rdrz_bin.h",                131072,            0xFF747E93,        "vax_ka420_rdrz_bin"},
   {"VAX/ka420_rzrz.bin",               "VAX/vax_ka420_rzrz_bin.h",                131072,            0xFF7A9A51,        "vax_ka420_rzrz_bin"},
   {"VAX/ka4xx_4pln.bin",               "VAX/vax_ka4xx_4pln_bin.h",                 65536,            0xFF9CD286,        "vax_ka4xx_4pln_bin"},
   {"VAX/ka4xx_8pln.bin",               "VAX/vax_ka4xx_8pln_bin.h",                 65536,            0xFFA2FF59,        "vax_ka4xx_8pln_bin"},
   {"VAX/ka4xx_dz.bin",                 "VAX/vax_ka4xx_dz_bin.h",                   32768,            0xFFD84C02,        "vax_ka4xx_dz_bin"},
   {"VAX/ka4xx_spx.bin"            ,    "VAX/vax_ka4xx_spx_bin.h",                 131072,            0xFF765752,        "vax_ka4xx_spx_bin"},
   {"VAX/ka750_new.bin",                "VAX/vax_ka750_bin_new.h",                   1024,            0xFFFE7BE5,        "vax_ka750_bin_new", "From ROM set: E40A9, E41A9, E42A9, E43A9 (Boots: A=DD, B=DB, C=DU"},
   {"VAX/ka750_old.bin",                "VAX/vax_ka750_bin_old.h",                   1024,            0xFFFEBAA5,        "vax_ka750_bin_old", "From ROM set: 990A9, 948A9, 906A9, 905A9 (Boots: A=DD, B=DM, C=DL, D=DU"},
   {"VAX/vcb02.bin",                    "VAX/vax_vcb02_bin.h",                      16384,            0xFFF1D2AD,        "vax_vcb02_bin"},
   {"VAX/vmb.exe",                      "VAX/vax_vmb_exe.h",                        44544,            0xFFC014BB,        "vax_vmb_exe"},
   {"PDP11/lunar11/lunar.lda",          "PDP11/pdp11_vt_lunar_rom.h",               13824   ,         0xFFF15D00,        "lunar_lda"},
   {"PDP11/dazzledart/dazzle.lda",      "PDP11/pdp11_dazzle_dart_rom.h",             6096,            0xFFF83848,        "dazzle_lda"},
   {"PDP11/11logo/11logo.lda",          "PDP11/pdp11_11logo_rom.h",                 26009,            0xFFDD77F7,        "logo_lda"},
   {"swtp6800/swtp6800/swtbug.bin",     "swtp6800/swtp6800/swtp_swtbug_bin.h",       1024,            0xFFFE4FBC,        "swtp_swtbug_bin"},
   {"3B2/rom_400.bin",                  "3B2/rom_400_bin.h",                        32768,            0xFFD55762,        "rom_400_bin"},
   };


#include <ctype.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <sys/utime.h>
#define utimbuf _utimbuf 
#define utime _utime
#else
#include <utime.h>
#endif

int sim_read_ROM_include(const char *include_filename, 
                         int *psize,
                         unsigned char **pROMData,
                         unsigned int *pchecksum,
                         char **prom_array_name,
                         int *defines_found)
{
FILE *iFile;
char line[256];
size_t i;
size_t bytes_written = 0;
size_t allocated_size = 0;
int define_size_found = 0;
int define_filename_found = 0;
int define_array_found = 0;

*psize = 0;
*pchecksum = 0;
*pROMData = NULL;
*prom_array_name = NULL;
if (NULL == (iFile = fopen (include_filename, "r")))
    return -1;

memset (line, 0, sizeof (line));

while (fgets (line, sizeof(line)-1, iFile)) {
    unsigned int byte;
    char *c;

    switch (line[0]) {
        case '#':
            if (0 == strncmp ("#define BOOT_CODE_SIZE ", line, 23))
                define_size_found = 1;
            if (0 == strncmp ("#define BOOT_CODE_FILENAME ", line, 27))
                define_filename_found = 1;
            if (0 == strncmp ("#define BOOT_CODE_ARRAY ", line, 24))
                define_array_found = 1;
            break;
        case ' ':
        case '/':
        case '*':
        case '\n':
            break;
        case 'u': /* unsigned char {array_name}[] */
            *prom_array_name = (char *)calloc(512, sizeof(char));
            if (1 == sscanf (line, "unsigned char %s[]", *prom_array_name)) {
                c = strchr (*prom_array_name, '[');
                if (c)
                    *c = '\0';
                }
            break;
        case '0': /* line containing byte data */
            c = line;
            while (1 == sscanf (c, "0x%2Xd,", &byte)) {
                if (bytes_written >= allocated_size) {
                    allocated_size += 2048;
                    *pROMData = (unsigned char *)realloc(*pROMData, allocated_size);
                    }
                *(*pROMData + bytes_written++) = (unsigned char)byte;
                c += 5;
                }
            break;
        }
    if (strchr (line, '}'))
        break;
    }
fclose (iFile);
for (i=0; i<bytes_written; ++i)
    *pchecksum += *(*pROMData + i);
*pchecksum = ~*pchecksum;
*psize = bytes_written;
*defines_found = (3 == (define_size_found + define_filename_found + define_array_found));
return 0;
}

int sim_make_ROMs_entry(const char *rom_filename)
{
FILE *rFile;
struct stat statb;
unsigned char *ROMData = NULL;
unsigned int checksum = 0;
char *c;
int i;
char cleaned_rom_filename[512];
char include_filename[512];
char array_name[512];

if (NULL == (rFile = fopen (rom_filename, "rb"))) {
    printf ("Error Opening ROM binary file '%s' for input: %s\n", rom_filename, strerror(errno));
    return -1;
    }
if (stat (rom_filename, &statb)) {
    printf ("Error stating '%s': %s\n", rom_filename, strerror(errno));
    fclose (rFile);
    return -1;
    }
ROMData = (unsigned char *)malloc (statb.st_size);
if ((size_t)(statb.st_size) != fread (ROMData, sizeof(*ROMData), statb.st_size, rFile)) {
    printf ("Error reading '%s': %s\n", rom_filename, strerror(errno));
    fclose (rFile);
    free (ROMData);
    return -1;
    }
fclose (rFile);
for (i=0; i<statb.st_size; ++i)
    checksum += ROMData[i];
checksum = ~checksum;
strncpy (cleaned_rom_filename, rom_filename, sizeof(cleaned_rom_filename)-2);
cleaned_rom_filename[sizeof(cleaned_rom_filename)-1] = '\0';
while ((c = strchr (cleaned_rom_filename, '\\')))
    *c = '/';
strcpy (array_name, cleaned_rom_filename);
for (c=array_name; *c; ++c)
    if (isupper(*c))
        *c = (char)tolower(*c);
if ((c = strchr (array_name, '.')))
    *c = '_';
if ((c = strchr (array_name, '/')))
    *c = '_';
sprintf (include_filename, "%s.h", cleaned_rom_filename);
if ((c = strrchr (include_filename, '/')))
    sprintf (c+1, "%s.h", array_name);
else
    sprintf (include_filename, "%s.h", array_name);
printf ("The ROMs array entry for this new ROM image file should look something like:\n");
printf ("{\"%s\",    \"%s\",     %d,  0x%08X, \"%s\"}\n",
        rom_filename, include_filename, (int)(statb.st_size), checksum, array_name);
free (ROMData);
return 1;
}

int sim_make_ROM_include(const char *rom_filename,
                         int expected_size,
                         unsigned int expected_checksum,
                         const char *include_filename, 
                         const char *rom_array_name,
                         const char *Comments)
{
FILE *rFile;
FILE *iFile;
time_t now;
int bytes_written = 0;
int include_bytes;
int c;
struct stat statb;
const char *load_filename;
unsigned char *ROMData = NULL;
unsigned char *include_ROMData = NULL;
char *include_array_name = NULL;
unsigned int checksum = 0;
unsigned int include_checksum;
int defines_found;

if (NULL == (rFile = fopen (rom_filename, "rb"))) {
    printf ("Error Opening ROM binary file '%s' for input: %s\n", rom_filename, strerror(errno));
    if (0 != sim_read_ROM_include(include_filename, 
                                  &include_bytes,
                                  &include_ROMData,
                                  &include_checksum,
                                  &include_array_name,
                                  &defines_found))
        return -1;
    c = ((include_checksum == expected_checksum) && 
         (include_bytes == expected_size) &&
         (0 == strcmp(include_array_name, rom_array_name)) &&
         defines_found);
    free(include_ROMData);
    free(include_array_name);
    if (!c)
        printf ("Existing ROM include file: %s has unexpected content\n", include_filename);
    else
        printf ("Existing ROM include file: %s looks good\n", include_filename);
    return (c ? 0 : -1);
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
ROMData = (unsigned char *)malloc (statb.st_size);
if ((size_t)(statb.st_size) != fread (ROMData, sizeof(*ROMData), statb.st_size, rFile)) {
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
    free (ROMData);
    return -1;
    }
/*
 * If the target include file already exists, determine if it contains the exact
 * data in the base ROM image.  If so, then we are already done
 */
if (0 == sim_read_ROM_include(include_filename, 
                              &include_bytes,
                              &include_ROMData,
                              &include_checksum,
                              &include_array_name,
                              &defines_found)) {
    c = ((include_checksum == expected_checksum) && 
         (include_bytes == expected_size) &&
         (0 == strcmp (include_array_name, rom_array_name)) &&
         (0 == memcmp (include_ROMData, ROMData, include_bytes)) &&
         defines_found);
    free(include_ROMData);
    free(include_array_name);
    if (c) {
        free (ROMData);
        return 0;
        }
    }

if (NULL == (iFile = fopen (include_filename, "w"))) {
    printf ("Error Opening '%s' for output: %s\n", include_filename, strerror(errno));
    free (ROMData);
    return -1;
    }
load_filename = strrchr (rom_filename, '/');
if (load_filename)
    ++load_filename;
else
    load_filename = rom_filename;
time (&now);
fprintf (iFile, "#ifndef ROM_%s_H\n", rom_array_name);
fprintf (iFile, "#define ROM_%s_H 0\n", rom_array_name);
fprintf (iFile, "/*\n");
fprintf (iFile, "   %s         produced at %s", include_filename, ctime(&now));
fprintf (iFile, "   from %s which was last modified at %s", rom_filename, ctime(&statb.st_mtime));
fprintf (iFile, "   file size: %d (0x%X) - checksum: 0x%08X\n", (int)statb.st_size, (int)statb.st_size, checksum);
fprintf (iFile, "   This file is a generated file and should NOT be edited or changed by hand.\n");
if (Comments)
    fprintf (iFile, "\n   %s\n\n", Comments);
fprintf (iFile, "*/\n");
fprintf (iFile, "#define BOOT_CODE_SIZE 0x%X\n", (int)statb.st_size);
fprintf (iFile, "#define BOOT_CODE_FILENAME \"%s\"\n", load_filename);
fprintf (iFile, "#define BOOT_CODE_ARRAY %s\n", rom_array_name);
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

void
Usage(void)
{
size_t i;

printf ("sim_BuildROMs Usage:\n");
printf ("sim_BuildROMs\n");
printf ("                  invoked with no arguments will verify and/or produce all\n");
printf ("                  known ROM include files\n");
printf ("sim_BuildROMs -checksum ROM-File-name\n");
printf ("                  computes the checksum on a ROM image file and provides a\n");
printf ("                  template which can be added to the ROMs array in the\n");
printf ("                  source file sim_BuildROMs.c\n");
printf ("sim_BuildROMs ROM-File-name\n");
printf ("                  if the 'ROM-File-name' specified is a file name already\n");
printf ("                  contained in the ROMs array, only that ROM image file's\n");
printf ("                  include file will be verified and/or created\n");
printf ("                  if the 'ROM-File-name' specified is not a file name already\n");
printf ("                  contained in the ROMs array, that ROM's checksum is computed\n");
printf ("                  and a template which can be added to the ROMs array in the\n");
printf ("                  source file sim_BuildROMs.c is displayed.\n");
printf ("\n");
printf ("Current ROM files:\n");
printf ("\n");
printf ("BinaryName:      IncludeFileName:          Size:   Checksum:  ROM Array Name:\n");
printf ("=============================================================================\n");
for (i=0; i<sizeof(ROMs)/sizeof(ROMs[0]); ++i)
    printf("%-17s%-23s%8d  0x%08X  %s\n", ROMs[i].BinaryName, ROMs[i].IncludeFileName, (int)ROMs[i].expected_size, ROMs[i].checksum, ROMs[i].ArrayName);
exit(2);
}

int
main(int argc, char **argv)
{
size_t i;
int status = 0;

if (argc == 1) {  /* invoked without any arguments */
    for (i=0; i<sizeof(ROMs)/sizeof(ROMs[0]); ++i)
        status += sim_make_ROM_include (ROMs[i].BinaryName, ROMs[i].expected_size, ROMs[i].checksum, ROMs[i].IncludeFileName, ROMs[i].ArrayName, ROMs[i].Comments);
    exit((status == 0) ? 0 : 2);
    }
if ((0 == strcmp(argv[1], "/?")) ||
    (0 == strcmp(argv[1], "-?")) ||
    (0 == strcmp(argv[1], "/help")) ||
    (0 == strcmp(argv[1], "-help")))
    Usage();
if ((0 == strcmp(argv[1], "-checksum")) && (argc > 2))
    status = sim_make_ROMs_entry (argv[2]);
else {
    for (i=0; i<sizeof(ROMs)/sizeof(ROMs[0]); ++i)
        if (0 == strcmp(argv[1], ROMs[i].BinaryName))
            break;
    if (i == sizeof(ROMs)/sizeof(ROMs[0]))
        status = sim_make_ROMs_entry (argv[1]);
    else
        status = sim_make_ROM_include (ROMs[i].BinaryName, ROMs[i].expected_size, ROMs[i].checksum, ROMs[i].IncludeFileName, ROMs[i].ArrayName, ROMs[i].Comments);
    }
exit((status == 0) ? 0 : 2);
}
