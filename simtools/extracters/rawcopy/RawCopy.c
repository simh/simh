#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static 
char buf[1024*1024];

int main(int argc, char **argv)
{
FILE *fin, *fout;
size_t bytesread, readsize = sizeof(buf), totalbytes = 0;

if (argc < 3)
    {
    fprintf(stderr, "Usage: RawCopy <infile> <outfile>\n");
    fprintf(stderr, "On Win32 environments, RAW devices have names for the format:\n");
    fprintf(stderr,"      CD Drives    \\\\.\\CdRom0\n");
    fprintf(stderr,"      Hard Drives  \\\\.\\PhysicalDrive0\n");
    exit(0);
    }
if (NULL == (fin = fopen(argv[1], "rb")))
    {
    fprintf(stderr, "Error Opening '%s' for input: %s\n", argv[1], strerror(errno));
    exit(errno);
    }
if (NULL == (fout = fopen(argv[2], "wb")))
    {
    fprintf(stderr, "Error Opening '%s' for output: %s\n", argv[2], strerror(errno));
    exit(errno);
    }
fprintf(stderr, "Copying '%s' to '%s'\n", argv[1], argv[2]);
while (0 != (bytesread = fread(buf, 1, readsize, fin)))
    {
    if (bytesread != fwrite(buf, 1, bytesread, fout))
        {
        fprintf(stderr, "Error Writing '%s': %s\n", strerror(errno));
        break;
        }
    else
       totalbytes += bytesread;
    if (0 == (totalbytes%(1024*1024)))
        fprintf(stderr, "%6dMB Copied...\r", totalbytes/(1024*1024));
    }
fprintf(stderr, "\n");
fprintf(stderr, "Total Data: %6.2f MBytes (%d bytes)\n", totalbytes/(1024.0*1024.0), totalbytes);
fclose(fin);
fclose(fout);
}
