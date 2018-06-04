#include <stdio.h>

#define ERROR   00404
#include "pdp11_cr_dat.h"

static int  colStart = 1;       /* starting column */
static int  colEnd = 80;        /* ending column */

main ()
{
    int col, c;

    while (1) {
        for (col = colStart; col <= colEnd; ) {
            switch (c = fgetc (stdin)) {
            case EOF:
                /* fall through */
            case '\n':
                while (col <= colEnd) {
                    fputc (o29_code[' '] & 077, stdout);
                    fputc ((o29_code[' '] >> 6) & 077, stdout);
                    col++;
                }
                break;
            case '\t':
                do {
                    fputc (o29_code[' '] & 077, stdout);
                    fputc ((o29_code[' '] >> 6) & 077, stdout);
                    col++;
                } while (((col & 07) != 1) && (col <= colEnd));
                break;
            default:
                fputc (o29_code[c] & 077, stdout);
                fputc ((o29_code[c] >> 6) & 077, stdout);
                col++;
                break;
            }
        }
        /* flush long lines, or flag over-length card */
        if (c != '\n' && c != EOF) {
            printf ("overlength line\n");
            do c = fgetc (stdin);
                while ((c != EOF) && (c != '\n'));
        }
        if (c == EOF)
            break;
    }
    exit (1);
}
