/*********************************************************************************************
 * ibm1130_fmt.c : interpret tabs in 1130 Assembler or Fortran source
 * Bob Flanders
 * -------------------------------------------------------------------------------------------
 *
 * These routines are used by ibm1130_cr.c when the user has indicated
 * that the input text is formatted with tabs. Input lines are edited
 * into the appropriate column format. Three edit modes are recognized:
 *
 * Assembler mode:
 *      Input lines of the form
 *
 *          [label]<whitespace>[opcode]<tab>[tag][L]<tab>[argument]
 *
 *      are rearranged so that the input fields are placed in the appropriate columns
 *
 *      The label must start on the first character of the line. If there is no label, 
 *      the first character(s) before the opcode must be whitespace. Following the opcode, there
 *      MUST be a tab character, followed by the format and tag. Following the format and tag 
 *      may be exactly one whitespace character, and then starts the argument.
 *
 *      Input lines with * in column 1 and blank lines are turned into Assembler comments,
 *      with the * in the Opcode field.
 *
 *      Assembler directive lines at the beginning of the deck must be preceded by
 *      ! to indicate that they are not comments. For example,
 *
 *      !*LIST
 *      * This is a comment
 *
 * Fortran mode:
 *      Input lines of the form
 *
 *          [label]<tab>statement
 *
 *      or
 *
 *          [label]<tab>Xcontinuation
 *
 *      where X is a non alphabetic contination character are rearranged in the
 *      appropriate manner:
 *
 *                   1         2
 *          12345678901234567890...
 *          ------------------------
 *          label statement
 *          labelXcontinuation
 *
 *      However, you must take care that you don't end up with statement text after column 72.
 *
 *      Input lines with * or C in column 1 are left alone (comments and directives)
 *
 *      (The ! escape is not used before Fortran directives as before Assembler directives)
 *
 * Tab mode:
 *      Tabs are replaced with spaces. Tab settings are assumed to be eight characters wide,
 *      as is standard for vi, notepad, etc.
 *********************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include <string.h>
#include "ibm1130_fmt.h"
#include "sim_defs.h"

#define MAXLINE     81                                  /* maximum output line size */
#define WORKSZ      256                                 /* size for tab work area */
#define TAGOFFSET   12                                  /* offset for tag field */
#define FMTOFFSET   11                                  /* offset for format field */

#define MIN(a,b)    ((a < b) ? a : b)
#define AMSG        " with Assembler Reformat"
#define FMSG        " with FORTRAN Reformat"
#define WMSG        " with tab replacement"
#define AFORMAT     "%20.20s%-60.60s"," "
#define ACOMMENTFMT "%20.20s%-60.60s"," "
#define ABLANKLINE  "%20.20s*"," "
#define FFORMAT     "%-5.5s %-74.74s"
#define FCONTFMT    "%-5.5s%-75.75s"

static char  gszLabel[6];                                   /* work area for label */
static char  gszArg[MAXLINE];                               /* .. argument */
static char  gszOutput[MAXLINE];                            /* .. output */
static short gaiAsmTabs[] = {7,12,15,20,25,30,35,40,45,52,0};/* tab stops for assembler */
static short gaiPlainTabs[42];                              /* tab stops for plain tabs. Settings will be made later. Max # positions when tabs are every 2 positions */
static int   giPlainTabWidth = 0;

/*
 * helper routines
 */

/*************************************************
 * ExpandTabs: Expand tabs to spaces
 */

char* ExpandTabs(char*  p_szInbuf,                      /* expand tabs .. input buffer */
                 char*  p_szOutbuf,                     /* .. output buffer */
                 short* p_aiTabs)                       /* .. array of tab stops (1 based) -- 0 end of array */
{
short   iI,                                                 /* input position */
        iO,                                                 /* output position */
        iT;                                                 /* next tab stop */
        
char    cX;                                                 /* character to test */

    iI = 0;                                                 /* init input position */
    iO = 0;                                                 /* init output position */
    iT = 0;                                                 /* init tab stop */
    
    while ((cX = *(p_szInbuf + iI)) != 0)                   /* while there are characters */
    {
        if (cX == '\t')                                     /* q. tab character? */
        {                                                   /* a. yes .. */
            while ((p_aiTabs[iT] <= iO + 1)                 /* search for next valid stop .. */
                    && (p_aiTabs[iT] != 0))                 /* .. or end of table */
                iT++;                                       /* .. go to next tab */
                
            if (p_aiTabs[iT] != 0)                          /* q. end of tab array? */
            {                                               /* a. no ..  */
                while (iO < (p_aiTabs[iT] - 1))             /* fill to tab with blanks */
                        *(p_szOutbuf + iO++) = ' ';         /* .. put in a blank */
                        
            }
            else                                            /* Otherwise ... */
                *(p_szOutbuf + iO++) = ' ';                 /* .. Translate to blank */
        }                                                   
        else                                                /* Otherwise .. not tab */
            *(p_szOutbuf + iO++) = cX;                      /* .. save the input char */
            
        iI++;                                               /* next input character */
    }
    
    *(p_szOutbuf + iO) = 0;                                 /* end the string.. */
    return p_szOutbuf;                                      /* .. return output area addr */
}

/*************************************************
 * extract next token, modify pointer
 */

char* GetToken(char* p_szOut,                           /* output location */
               int   p_iLen,                            /* max output length */
               char**p_pszToken)                        /* pointer to input token */
{
int     iI;                                                 /* work integer */
char*   pszX;                                               /* work pointer */

    pszX = *p_pszToken;                                     /* get pointer to token */

    for (iI = 0; *(pszX + iI) && (!isspace(*(pszX + iI)));) /* while not whitespace & not end */
        iI++;                                               /* .. count token length */

    memset(p_szOut, 0, p_iLen);                             /* zero out output area */

    if (iI > 0)                                             /* q. any chars? */
        memcpy(p_szOut, *p_pszToken, MIN(iI, p_iLen-1));    /* a. yes.. copy max of p_iLen-1 */

    *p_pszToken += iI;                                      /* point beyond token */
    return p_szOut;                                         /* .. return token pointer */
}

/*************************************************
 * EditToAsm - convert tab-formatted text line to 1130 Assembler format
 */

const char *EditToAsm (char* p_pszEdit, int width)          /* convert line to 1130 assembler */
{
char    pszLine[MAXLINE];                                   /* source line */
char    pszWork[WORKSZ];                                    /* work buffer */
char    acTFWrk[2];                                         /* tag/format work area */
size_t  iI;                                                 /* work integer */

    if (p_pszEdit == NULL)                                  /* q. null request? */
        return AMSG;                                        /* a. yes .. return display message */

    if (*p_pszEdit == '!')                                  /* leave lines starting with ! alone */
        return EditToWhitespace(p_pszEdit+1, width);

    if (*p_pszEdit == '*')                                  /* q. comment line? */
    {                                                       /* a. yes..  */
        strlcpy(pszWork, EditToWhitespace(p_pszEdit, width), sizeof pszWork);/* .. convert any tabs */
        sprintf(gszOutput, ACOMMENTFMT, pszWork);           /* .. put the comment out there in the opcode column */
        return gszOutput;                                   /* .. and return it */
    }

    strlcpy(pszLine, p_pszEdit, sizeof pszLine);            /* copy the line local */
    
    ExpandTabs(pszLine, pszWork, gaiAsmTabs);               /* expand the tabs */
    strlcpy(pszLine, pszWork, sizeof pszLine);              /* copy the line back */
    
    for (iI = strlen(pszLine); iI--;)                       /* trim trailing whitespace */
    {
        if (*(pszLine + iI) <= ' ')                         /* q. space or less? */
            *(pszLine + iI) = 0;                            /* a. yes .. remove it */
        else                                                /* otherwise */
            break;                                          /* .. done. Leave loop. */
    }

    if (strlen(pszLine) == 0)                               /* q. blank line? */
    {                                                       /* a. yes ..  Assembler abhors these so */
        sprintf(gszOutput, ABLANKLINE);                     /* format as comment statement */
        return gszOutput;                                   /* .. and return it */
    }


    /* TODO: Add code to process a strip switch 
     * comment?
     */

    if (strlen(pszLine) > (TAGOFFSET + 1))                  /* q. line long enough? */
    {                                                       /* a. yes.. reorder tag/format */
        memcpy(acTFWrk, pszLine + FMTOFFSET, 2);            /* get tag/format */
        memset((pszLine + FMTOFFSET), ' ', 2);              /* .. blank 'em out */
        
        for (iI = 0; iI < 2; iI ++)
            if (isalpha(acTFWrk[iI]))                       /* q. alpha char? */
                *(pszLine + FMTOFFSET) = acTFWrk[iI];       /* a. yes .. make it format */
            else if (isdigit(acTFWrk[iI]))                  /* q. digit? */
                *(pszLine + TAGOFFSET) = acTFWrk[iI];       /* a. yes .. make it the tag */
    }

    sprintf(gszOutput, AFORMAT, pszLine);                   /* format the line */
        
    return gszOutput;                                       /* return formatted line */
}

/*************************************************
 * EditToFortran - convert tab-formatted input text line to FORTRAN format
 * (a la DEC Fortran)
 */

const char *EditToFortran(char* p_pszEdit, int width)       /* convert line to 1130 assembler */
{
char    pszLine[MAXLINE];                                   /* source line */
char*   pszWork;                                            /* work pointer */
size_t  iI;                                                 /* work integer */
int     bContinue;                                          /* true if continue */

    if (p_pszEdit == NULL)                                  /* q. null request? */
        return FMSG;                                        /* a. yes .. return display message */

    if (strchr(p_pszEdit, '\t') == NULL)                    /* q. no tab in the line? */
        return p_pszEdit;                                   /* a. nope, return line as is, assume it's formatted correctly */

    if (*p_pszEdit == 'C' || *p_pszEdit == '*' || *p_pszEdit == '\0')   /* q. comment or directive or blank line? */
    {                                                       /* a. yes.. don't restructure */
        return EditToWhitespace(p_pszEdit, width);
    }

    strlcpy(pszLine, p_pszEdit, sizeof pszLine);            /* copy the line local */

    for (iI = strlen(pszLine); iI--;)                       /* trim trailing whitespace */
    {
        if (*(pszLine + iI) <= ' ')                         /* q. space or less? */
            *(pszLine + iI) = 0;                            /* a. yes .. remove it */
        else                                                /* otherwise */
            break;                                          /* .. done. Leave loop. */
    }

    /*
     * TODO: Add code to process a strip switch 
     * comment?
     */

    pszWork = (char*) pszLine;                              /* set pointer to line */
    GetToken(gszLabel, 6, &pszWork);                        /* get the line, if any. */

    pszWork++;                                              /* skip tab/whitespace */

                                                            /* continuation... */
    bContinue = ((isdigit(*pszWork) && (*pszWork != '0'))   /* if first char non-zero digit */
            || (!isspace(*pszWork) && !isalpha(*pszWork))); /* .. or non-alpha non-blank */
    
    memset(gszArg, 0, MAXLINE);                             /* .. and arguments */

    strncpy(gszArg, pszWork, 75);                           /* copy rest to argument */

    sprintf(gszOutput, (bContinue) ? FCONTFMT : FFORMAT,    /* format the line */
                        gszLabel,                           /* .. statement # */
                        gszArg);                            /* .. code */
        
    return gszOutput;                                       /* return formatted line */
}

/*************************************************
 * EditToWhitespace - expand tabs at n space intervals.
 */

const char* EditToWhitespace(char *p_pszEdit, int width)
{
int     iI;                                                 /* work integer */
int     iPos;                                               /* work integer for settings tab stops */
char    pszLine[MAXLINE];                                   /* source line */
char    pszWork[WORKSZ];                                    /* work buffer */

    if (p_pszEdit == NULL)                                  /* q. null request? */
        return WMSG;                                        /* a. yes .. return display message */

    strncpy(pszLine, p_pszEdit, MAXLINE-1);                 /* copy the line local */

    if (width == 0) width = 8;                              /* default */

    if ((width != giPlainTabWidth) && (width > 1) && (width < 30)) {
        giPlainTabWidth = width;                            /* if width is different, and valid, rebuild tabstop array */
        iI = 0;                                             /* output index */
        iPos = width + 1;                                   /* first tab position */
        while (iPos < 80) {                                 /* fill array up to but not including position 80 */
            gaiPlainTabs[iI++] = iPos;
            iPos += width;
        }
        gaiPlainTabs[iI] = 0;                               /* mark end of array */
    }
    
    ExpandTabs(pszLine, pszWork, gaiPlainTabs);             /* expand the tabs */
    strlcpy(gszOutput, pszWork, sizeof gszOutput);          /* copy the line back  */
    
    for (iI = strlen(gszOutput); iI--;)                     /* look at each character */
    {
        if (*(gszOutput + iI) <= ' ')                       /* q. space or less? */
            *(gszOutput + iI) = 0;                          /* a. yes .. remove it */
        else                                                /* otherwise */
            break;                                          /* .. done. Leave loop. */
    }


    return gszOutput;                                       /* ... return buffer */
}
