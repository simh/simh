/*************************************************************************
 *                                                                       *
 * $Id: sim_imd.c 1904 2008-05-21 06:57:57Z hharte $                     *
 *                                                                       *
 * Copyright (c) 2007-2008 Howard M. Harte.                              *
 * http://www.hartetec.com                                               *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * Howard M. Harte.                                                      *
 *                                                                       *
 * SIMH Interface based on altairz80_hdsk.c, by Peter Schorn.            *
 *                                                                       *
 * Module Description:                                                   *
 *     ImageDisk Disk Image File access module for SIMH, definitions.    *
 *     see :                                                             *
 *     for details on the ImageDisk format and other utilities.          *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

#include "altairz80_defs.h"
#include "sim_imd.h"

/* #define DBG_MSG */

#ifdef DBG_MSG
#define DBG_PRINT(args) printf args
#else
#define DBG_PRINT(args)
#endif


DISK_INFO * diskOpen(FILE *fileref, int isVerbose)
{
    char cBuf[256];
    char sectorMap[256];
    char sectorHeadMap[256];
    char sectorCylMap[256];
    unsigned int sectorSize, sectRecordType;
    unsigned int i;
    unsigned char start_sect;
    DISK_INFO *myDisk = NULL;

    unsigned int TotalSectorCount = 0;
    IMD_HEADER imd;

    myDisk = (DISK_INFO *)malloc(sizeof(DISK_INFO));

    myDisk->file = fileref;

    /* rewind to the beginning of the file. */
    fseek(myDisk->file, 0, SEEK_SET);

    do {
        cBuf[0] = fgetc(myDisk->file);
        if((cBuf[0] != 0x1a) && isVerbose) putchar(cBuf[0]);
    }
    while (cBuf[0] != 0x1a);

    myDisk->nsides = 1;
    myDisk->ntracks = 0;
    myDisk->flags = 0;      /* Make sure all flags are clear. */

    do {
        DBG_PRINT(("start of track %d at file offset %d\n", myDisk->ntracks, ftell(myDisk->file)));

        fread(&imd, 1, 5, myDisk->file);
        if(feof(myDisk->file)) break;

        sectorSize = (1 << imd.sectsize) * 128;

        DBG_PRINT(("Track %d:\n", myDisk->ntracks));
        DBG_PRINT(("\tMode=%d, Cyl=%d, Head=%d, #sectors=%d, sectsize=%d (%d bytes)\n", imd.mode, imd.cyl, imd.head, imd.nsects, imd.sectsize, sectorSize));

        if((imd.head + 1) > myDisk->nsides) {
            myDisk->nsides = imd.head + 1;
        }

        myDisk->track[imd.cyl][imd.head].mode = imd.mode;
        myDisk->track[imd.cyl][imd.head].nsects = imd.nsects;
        myDisk->track[imd.cyl][imd.head].sectsize = sectorSize;

        fread(sectorMap, 1, imd.nsects, myDisk->file);
        myDisk->track[imd.cyl][imd.head].start_sector = imd.nsects;
        DBG_PRINT(("\tSector Map: "));
        for(i=0;i<imd.nsects;i++) {
            DBG_PRINT(("%d ", sectorMap[i]));
            if(sectorMap[i] < myDisk->track[imd.cyl][imd.head].start_sector) {
                myDisk->track[imd.cyl][imd.head].start_sector = sectorMap[i];
            }
        }
        DBG_PRINT((", Start Sector=%d", myDisk->track[imd.cyl][imd.head].start_sector));

        if(imd.head & IMD_FLAG_SECT_HEAD_MAP) {
            fread(sectorHeadMap, 1, imd.nsects, myDisk->file);
            DBG_PRINT(("\tSector Head Map: "));
            for(i=0;i<imd.nsects;i++) {
                DBG_PRINT(("%d ", sectorHeadMap[i]));
            }
            DBG_PRINT(("\n"));
        } else {
            memset(sectorHeadMap, 0, imd.nsects);
        }

        if(imd.head & IMD_FLAG_SECT_CYL_MAP) {
            fread(sectorCylMap, 1, imd.nsects, myDisk->file);
            DBG_PRINT(("\tSector Cyl Map: "));
            for(i=0;i<imd.nsects;i++) {
                DBG_PRINT(("%d ", sectorCylMap[i]));
            }
            DBG_PRINT(("\n"));
        } else {
            memset(sectorCylMap, 0, imd.nsects);
        }

        DBG_PRINT(("\nSector data at offset 0x%08x\n", ftell(myDisk->file)));

        /* Build the table with location 0 being the start sector. */
        start_sect = myDisk->track[imd.cyl][imd.head].start_sector;

        /* Now read each sector */
        for(i=0;i<imd.nsects;i++) {
            TotalSectorCount++;
            DBG_PRINT(("Sector Phys: %d/Logical: %d: %d bytes: ", i, sectorMap[i], sectorSize));
            sectRecordType = fgetc(myDisk->file);
            switch(sectRecordType) {
                case SECT_RECORD_UNAVAILABLE:   /* Data could not be read from the original media */
                    myDisk->track[imd.cyl][imd.head].sectorOffsetMap[sectorMap[i]-start_sect] = 0xBADBAD;
                    break;
                case SECT_RECORD_NORM:          /* Normal Data */
                case SECT_RECORD_NORM_DAM:      /* Normal Data with deleted address mark */
                case SECT_RECORD_NORM_ERR:      /* Normal Data with read error */
                case SECT_RECORD_NORM_DAM_ERR:  /* Normal Data with deleted address mark with read error */
/*                  DBG_PRINT(("Uncompressed Data\n")); */
                    myDisk->track[imd.cyl][imd.head].sectorOffsetMap[sectorMap[i]-start_sect] = ftell(myDisk->file);
                    fseek(myDisk->file, sectorSize, SEEK_CUR);
                    break;
                case SECT_RECORD_NORM_COMP:     /* Compressed Normal Data */
                case SECT_RECORD_NORM_DAM_COMP: /* Compressed Normal Data with deleted address mark */
                case SECT_RECORD_NORM_COMP_ERR: /* Compressed Normal Data */
                case SECT_RECORD_NORM_DAM_COMP_ERR: /* Compressed Normal Data with deleted address mark */
                    myDisk->track[imd.cyl][imd.head].sectorOffsetMap[sectorMap[i]-start_sect] = ftell(myDisk->file);
                    myDisk->flags |= FD_FLAG_WRITELOCK; /* Write-protect the disk if any sectors are compressed. */
#ifdef VERBOSE_DEBUG
                    DBG_PRINT(("Compressed Data = 0x%02x\n", fgetc(myDisk->file)));
#else
                    fgetc(myDisk->file);
#endif
                    break;
                default:
                    printf("ERROR: unrecognized sector record type %d\n", sectRecordType);
                    break;
            }
            DBG_PRINT(("\n"));
        }

        myDisk->ntracks++;
    }
    while (!feof(myDisk->file));

    DBG_PRINT(("Processed %d sectors\n", TotalSectorCount));

#ifdef VERBOSE_DEBUG
    for(i=0;i<myDisk->ntracks;i++) {
        DBG_PRINT(("Track %02d: ", i));
        for(j=0;j<imd.nsects;j++) {
            DBG_PRINT(("0x%06x ", myDisk->track[i][0].sectorOffsetMap[j]));
        }
        DBG_PRINT(("\n"));
    }
#endif
    if(myDisk->flags & FD_FLAG_WRITELOCK) {
        printf("Disk write-protected because the image contains compressed sectors.  Use IMDU to uncompress.\n");
    }

    return myDisk;
}

unsigned int diskClose(DISK_INFO *myDisk)
{
    unsigned int retval = 0;

    if(myDisk == NULL) {
        return (-1);
    }

    free(myDisk);
    myDisk = NULL;

    return retval;
}

unsigned int imdGetSides(DISK_INFO *myDisk)
{
    if(myDisk != NULL) {
        return(myDisk->nsides);
    }

    return (0);
}

unsigned int imdIsWriteLocked(DISK_INFO *myDisk)
{
    if(myDisk != NULL) {
        return((myDisk->flags & FD_FLAG_WRITELOCK) ? 1 : 0);
    }

    return (0);
}

/* Check that the given track/sector exists on the disk */
int sectSeek(DISK_INFO *myDisk, unsigned int Cyl, unsigned int Head)
{
    if(myDisk->track[Cyl][Head].nsects == 0) {
        DBG_PRINT(("%s: invalid track/head" NLP, __FUNCTION__));
        return(-1);
    }

    return(0);
}


int sectRead(DISK_INFO *myDisk, unsigned int Cyl, unsigned int Head, unsigned int Sector, unsigned char *buf, unsigned int buflen, unsigned int *flags, unsigned int *readlen)
{
    unsigned int sectorFileOffset;
    unsigned char sectRecordType;
    unsigned char start_sect;
    *readlen = 0;
    *flags = 0;

    if(Sector > myDisk->track[Cyl][Head].nsects) {
        DBG_PRINT(("%s: invalid sector" NLP, __FUNCTION__));
        return(-1);
    }

    if(buflen < myDisk->track[Cyl][Head].sectsize) {
        printf("%s: Reading C:%d/H:%d/S:%d, len=%d: user buffer too short, need %d" NLP, __FUNCTION__, Cyl, Head, Sector, buflen, myDisk->track[Cyl][Head].sectsize);
        return(-1);
    }

    start_sect = myDisk->track[Cyl][Head].start_sector;

    sectorFileOffset = myDisk->track[Cyl][Head].sectorOffsetMap[Sector-start_sect];

    DBG_PRINT(("Reading C:%d/H:%d/S:%d, len=%d, offset=0x%08x" NLP, Cyl, Head, Sector, buflen, sectorFileOffset));

    fseek(myDisk->file, sectorFileOffset-1, 0);

    sectRecordType = fgetc(myDisk->file);
    switch(sectRecordType) {
        case SECT_RECORD_UNAVAILABLE:   /* Data could not be read from the original media */
            *flags |= IMD_DISK_IO_ERROR_GENERAL;
            break;
        case SECT_RECORD_NORM_ERR:      /* Normal Data with read error */
        case SECT_RECORD_NORM_DAM_ERR:  /* Normal Data with deleted address mark with read error */
            *flags |= IMD_DISK_IO_ERROR_CRC;
        case SECT_RECORD_NORM:          /* Normal Data */
        case SECT_RECORD_NORM_DAM:      /* Normal Data with deleted address mark */

/*          DBG_PRINT(("Uncompressed Data" NLP)); */
            fread(buf, 1, myDisk->track[Cyl][Head].sectsize, myDisk->file);
            *readlen = myDisk->track[Cyl][Head].sectsize;
            break;
        case SECT_RECORD_NORM_COMP_ERR: /* Compressed Normal Data */
        case SECT_RECORD_NORM_DAM_COMP_ERR: /* Compressed Normal Data with deleted address mark */
            *flags |= IMD_DISK_IO_ERROR_CRC;
        case SECT_RECORD_NORM_COMP:     /* Compressed Normal Data */
        case SECT_RECORD_NORM_DAM_COMP: /* Compressed Normal Data with deleted address mark */
/*          DBG_PRINT(("Compressed Data" NLP)); */
            memset(buf, fgetc(myDisk->file), myDisk->track[Cyl][Head].sectsize);
            *readlen = myDisk->track[Cyl][Head].sectsize;
            *flags |= IMD_DISK_IO_COMPRESSED;
            break;
        default:
            printf("ERROR: unrecognized sector record type %d" NLP, sectRecordType);
            break;
    }

    /* Set flags for deleted address mark. */
    switch(sectRecordType) {
        case SECT_RECORD_NORM_DAM:      /* Normal Data with deleted address mark */
        case SECT_RECORD_NORM_DAM_ERR:  /* Normal Data with deleted address mark with read error */
        case SECT_RECORD_NORM_DAM_COMP: /* Compressed Normal Data with deleted address mark */
        case SECT_RECORD_NORM_DAM_COMP_ERR: /* Compressed Normal Data with deleted address mark */
            *flags |= IMD_DISK_IO_DELETED_ADDR_MARK;
        default:
            break;
    }

    return(0);
}


int sectWrite(DISK_INFO *myDisk, unsigned int Cyl, unsigned int Head, unsigned int Sector, unsigned char *buf, unsigned int buflen, unsigned int *flags, unsigned int *writelen)
{
    unsigned int sectorFileOffset;
    unsigned char sectRecordType;
    unsigned char start_sect;
    *writelen = 0;
    *flags = 0;

    DBG_PRINT(("Writing C:%d/H:%d/S:%d, len=%d" NLP, Cyl, Head, Sector, buflen));

    if(myDisk->flags & FD_FLAG_WRITELOCK) {
        printf("Disk write-protected because the image contains compressed sectors.  Use IMDU to uncompress." NLP);
    }

    if(Sector > myDisk->track[Cyl][Head].nsects) {
        printf("%s: invalid sector [sector %i > # of sectors %i]" NLP,
            __FUNCTION__, Sector, myDisk->track[Cyl][Head].nsects);
        return(-1);
    }

    if(buflen < myDisk->track[Cyl][Head].sectsize) {
        printf("%s: user buffer too short [buflen %i < sectsize %i]" NLP,
            __FUNCTION__, buflen, myDisk->track[Cyl][Head].sectsize);
        return(-1);
    }

    start_sect = myDisk->track[Cyl][Head].start_sector;

    sectorFileOffset = myDisk->track[Cyl][Head].sectorOffsetMap[Sector-start_sect];

    fseek(myDisk->file, sectorFileOffset-1, 0);

    if (*flags & IMD_DISK_IO_ERROR_GENERAL) {
        sectRecordType = SECT_RECORD_UNAVAILABLE;
    } else if (*flags & IMD_DISK_IO_ERROR_CRC) {
        if (*flags & IMD_DISK_IO_DELETED_ADDR_MARK)
            sectRecordType = SECT_RECORD_NORM_DAM_ERR;
        else
            sectRecordType = SECT_RECORD_NORM_ERR;
    } else {
        sectRecordType = SECT_RECORD_NORM;
    }

    fputc(sectRecordType, myDisk->file);
    fwrite(buf, 1, myDisk->track[Cyl][Head].sectsize, myDisk->file);
    *writelen = myDisk->track[Cyl][Head].sectsize;

    return(0);
}
