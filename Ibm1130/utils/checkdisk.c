/*
 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to sim@ibm1130.org
 */

// checkdisk - validates and optionally dumps an IBM1130 DMS2 disk image file
//
// Usage:
//      checkdisk [-f] [-d cyl.sec|abssec] [-n count] filename
//
// Examples:
//      checkdisk file.dsk
//              report any misnumbered sectors in file.dsk
//
//      checkdisk -f file.dsk
//              report and fix any misnumbered sectors
//
//      checkdisk -d 198.0 file.dsk
//              dump cylinder 198 sector 0
//
//      checkdisk -d 0 file.dsk
//              dump absolute sector 0
//
//      checkdisk -d 198.0 -n 4 file.dsk
//              dump 4 sectors starting at m.n
// -----------------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util_io.h"

#ifdef _WIN32
#  include <io.h> 
#else
    long filelength (int fno);
#   include <sys/types.h>
#   include <sys/stat.h>
#endif

#ifndef TRUE
#  define BOOL  int
#  define TRUE  1
#  define FALSE 0
#endif

#define DSK_NUMWD   321             /* words/sector */
#define DSK_NUMSC   4               /* sectors/surface */
#define DSK_NUMSF   2               /* surfaces/cylinder */
#define DSK_NUMCY   203             /* cylinders/drive */
#define DSK_NUMDR   5               /* drives/controller */
#define DSK_SIZE (DSK_NUMCY * DSK_NUMSF * DSK_NUMSC * DSK_NUMWD)  /* words/drive */

char *usestr  = "Usage: checkdisk [-f] [-d cyl.sec|abssec] [-n count] diskfile";
char *baddisk = "Cannot fix this";

void bail (char *msg);
char *lowcase (char *str);

int main (int argc, char **argv)
{
    FILE *fp;
    char *fname = NULL, *arg, *argval;
    int i, j, cyl, sec, pos, asec, retry, nbad = 0, nfixed = 0, nline;
    BOOL fixit = FALSE, dump = FALSE;
    int dsec, nsec = 1;
    unsigned short wd, buf[DSK_NUMWD];

    for (i = 1; i < argc;) {
        arg = argv[i++];
        if (*arg == '-') {
            arg++;
            lowcase(arg);
            while (*arg) {
                switch (*arg++) {
                    case 'f':
                        fixit = TRUE;
                        break;

                    case 'd':
                        dump = TRUE;

                        if (i >= argc)
                            bail(usestr);

                        argval = argv[i++];
                        if (strchr(argval, '.') != NULL) {
                            if (sscanf(argval, "%d.%d", &cyl, &sec) != 2)
                                bail(usestr);

                            dsec = cyl*(DSK_NUMSF*DSK_NUMSC) + sec;
                        }
                        else if (sscanf(argval, "%d", &dsec) != 1)
                            bail(usestr);

                        if (dsec < 0 || dsec >= (DSK_NUMCY*DSK_NUMSF*DSK_NUMSC))
                            bail("No such sector");

                        break;

                    case 'n':
                        if (i >= argc)
                            bail(usestr);

                        argval = argv[i++];
                        if (sscanf(argval, "%d", &nsec) != 1)
                            bail(usestr);

                        if (nsec <= 0)
                            bail(usestr);

                        break;

                    default:
                        bail(usestr);
                }
            }
        }
        else if (fname == NULL)
            fname = arg;
        else
            bail(usestr);
    }

    if (fname == NULL)
        bail(usestr);

    if ((fp = fopen(fname, "rb+")) == NULL) {
        perror(fname);
        return 1;
    }

    if (filelength(fileno(fp)) != 2*DSK_SIZE) {
        fprintf(stderr, "File is wrong length, expected %d\n", DSK_SIZE);
        bail(baddisk);
    }

    for (cyl = 0; cyl < DSK_NUMCY; cyl++) {
        for (sec = 0; sec < (DSK_NUMSF*DSK_NUMSC); sec++) {
            retry = 1;
again:
            asec = cyl*(DSK_NUMSF*DSK_NUMSC) + sec;
            pos  = asec*2*DSK_NUMWD;

            if (fseek(fp, pos, SEEK_SET) != 0) {
                fprintf(stderr, "Error seeking to pos %x\n", pos);
                bail(baddisk);
            }

            if (fxread(&wd, sizeof(wd), 1, fp) != 1) {
                fprintf(stderr, "Error reading word at abs sec %x, cyl %x, sec %x at offset %x\n", asec, cyl, sec, pos);
                bail(baddisk);
            }

            if (wd != asec) {
                fprintf(stderr, "Bad sector #%x at abs sec %x, cyl %x, sec %x at offset %x\n", wd, asec, cyl, sec, pos);
                nbad++;

                if (fixit) {
                    if (fseek(fp, pos, SEEK_SET) != 0) {
                        fprintf(stderr, "Error seeking to pos %x\n", pos);
                        bail(baddisk);
                    }

                    if (fxwrite(&asec, sizeof(wd), 1, fp) != 1) {
                        fprintf(stderr, "Error writing sector # to abs sec %x, cyl %x, sec %x at offset %x\n", asec, cyl, sec, pos);
                        bail(baddisk);
                    }

                    if (retry) {
                        retry = 0;
                        nfixed++;
                        goto again;
                    }
    
                    fprintf(stderr, "Failed after retry\n");
                    bail(baddisk);
                }
            }
        }
    }

    if (nbad)
        printf("%d bad sector mark%s %s\n", nbad, (nbad == 1) ? "" : "s", fixit ? "fixed" : "found");
    else if (! dump)
        printf("All sector marks OK\n");

    if (! dump)
        return 0;

    pos  = dsec*2*DSK_NUMWD;
    if (fseek(fp, pos, SEEK_SET) != 0) {
        fprintf(stderr, "Error seeking to pos %x\n", pos);
        bail(baddisk);
    }

    for (i = 0; i < nsec; i++) {
        cyl = dsec / (DSK_NUMSF*DSK_NUMSC);
        sec = dsec - cyl*(DSK_NUMSF*DSK_NUMSC);

        if (fxread(&buf, sizeof(buf[0]), DSK_NUMWD, fp) != DSK_NUMWD) {
            fprintf(stderr, "Error reading abs sec %x, cyl %x, sec %x at offset %x\n", dsec, cyl, sec, pos);
            bail(baddisk);
        }

        printf("\nSector %d.%d - %d - /%04x label %04x\n", cyl, sec, dsec, dsec, buf[0]);
        for (nline = 0, j = 1; j < DSK_NUMWD; j++) {
            printf("%04x", buf[j]);
            if (++nline == 16) {
                putchar('\n');
                nline = 0;
            }
            else
                putchar(' ');
        }

        dsec++;
    }

    return 0;
}

void bail (char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

/* ------------------------------------------------------------------------ 
 * lowcase - force a string to lower case (ASCII)
 * ------------------------------------------------------------------------ */

char *lowcase (char *str)
{
    char *s;

    for (s = str; *s; s++) {
        if (*s >= 'A' && *s <= 'Z')
            *s += 32;
    } 

    return str;
}

#ifndef _WIN32

long filelength (int fno)
{
    struct stat sb;
    
    if (fstat(fno, &sb) != 0)
        return 0;

    return (long) sb.st_size;
}
#endif

