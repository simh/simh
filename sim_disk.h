/* sim_disk.h: simulator disk support library definitions

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of Robert M Supnik and 
   Mark Pizzolato shall not be used in advertising or otherwise to promote
   the sale, use or other dealings in this Software without prior written 
   authorization from Robert M Supnik and Mark Pizzolato.

   25-Jan-11    MP      Initial Implemementation
*/

#ifndef SIM_DISK_H_
#define SIM_DISK_H_    0

#ifdef  __cplusplus
extern "C" {
#endif

/* SIMH/Disk format */

typedef uint32          t_seccnt;                       /* disk sector count */
typedef uint32          t_lba;                          /* disk logical block address */

/* Unit flags */

#define DKUF_V_FMT      (UNIT_V_UF + 0)                 /* disk file format */
#define DKUF_W_FMT      2                               /* 2b of container formats */
#define DKUF_M_FMT      ((1u << DKUF_W_FMT) - 1)
#define DKUF_V_ENC      (DKUF_V_FMT + DKUF_W_FMT)       /* data encoding/packing */
#define DKUF_W_ENC      2                               /* 2b of data encoding/packing */
#define DKUF_M_ENC      ((1u << DKUF_W_ENC) - 1)
#define DKUF_V_NOAUTOSIZE (DKUF_V_ENC + DKUF_W_ENC)     /* Don't Autosize disk option */
#define DKUF_V_UF       (DKUF_V_NOAUTOSIZE + 1)
#define DKUF_WLK        UNIT_WLK
#define DKUF_FMT        (DKUF_M_FMT << DKUF_V_FMT)
#define DKUF_ENC        (DKUF_M_ENC << DKUF_V_ENC)
#define DKUF_WRP        (DKUF_WLK | UNIT_RO)
#define DKUF_NOAUTOSIZE (1 << DKUF_V_NOAUTOSIZE)

/* Encoding/Packing specfics */

#define DK_ENC_BYTE         0x00000001
#define DK_ENC_WORD         0x00000002
#define DK_ENC_LONG         0x00000004
#define DK_ENC_LONGLONG     0x00000008
#define DK_ENC_X_LSB        0x80
#define DK_ENC_X_MSB        0x00
#define DK_ENC_XFR_IN       0
#define DK_ENC_XFR_OUT      16
#define DK_ENC_LL_DBD9      (((DK_ENC_X_LSB | 64) << DK_ENC_XFR_OUT) | ((DK_ENC_X_MSB | 36) << DK_ENC_XFR_IN))
#define DK_ENC_LL_DLD9      (((DK_ENC_X_LSB | 64) << DK_ENC_XFR_OUT) | ((DK_ENC_X_LSB | 36) << DK_ENC_XFR_IN))

/* Return status codes */

#define DKSE_OK         0                               /* no error */

typedef void (*DISK_PCALLBACK)(UNIT *unit, t_stat status);

/* Prototypes */

t_stat sim_disk_init (void);
t_stat sim_disk_attach (UNIT *uptr, 
                        const char *cptr, 
                        size_t memory_sector_size,  /* memory footprint of sector data */
                        size_t xfer_element_size, 
                        t_bool dontchangecapac,     /* if false just change uptr->capac as needed */
                        uint32 debugbit,            /* debug bit */
                        const char *drivetype,      /* drive type */
                        uint32 pdp11_tracksize,     /* BAD144 track */
                        int completion_delay);      /* Minimum Delay for asynch I/O completion */
t_stat sim_disk_attach_ex (UNIT *uptr, 
                           const char *cptr, 
                           size_t memory_sector_size,   /* memory footprint of sector data */
                           size_t xfer_element_size, 
                           t_bool dontchangecapac,      /* if false just change uptr->capac as needed */
                           uint32 dbit,                 /* debug bit */
                           const char *dtype,           /* drive type */
                           uint32 pdp11tracksize,       /* BAD144 track */
                           int completion_delay,        /* Minimum Delay for asynch I/O completion */
                           const char **drivetypes);    /* list of drive types (from smallest to largest) */
                                                        /* to try and fit the container/file system into */
t_stat sim_disk_attach_ex2 (UNIT *uptr, 
                            const char *cptr, 
                            size_t memory_sector_size,  /* memory footprint of sector data */
                            size_t xfer_element_size, 
                            t_bool dontchangecapac,     /* if false just change uptr->capac as needed */
                            uint32 dbit,                /* debug bit */
                            const char *dtype,          /* drive type */
                            uint32 pdp11tracksize,      /* BAD144 track */
                            int completion_delay,       /* Minimum Delay for asynch I/O completion */
                            const char **drivetypes,    /* list of drive types (from smallest to largest) */
                                                        /* to try and fit the container/file system into */
                            size_t reserved_sectors);   /* Unused sectors beyond the file system */
t_stat sim_disk_detach (UNIT *uptr);
t_stat sim_disk_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat sim_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects);
t_stat sim_disk_rdsect_a (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects, DISK_PCALLBACK callback);
t_stat sim_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects);
t_stat sim_disk_wrsect_a (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects, DISK_PCALLBACK callback);
t_stat sim_disk_unload (UNIT *uptr);
t_stat sim_disk_erase (UNIT *uptr);
t_stat sim_disk_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sim_disk_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat sim_disk_set_capac (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sim_disk_show_capac (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat sim_disk_set_autosize (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sim_disk_show_autosize (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat sim_disk_set_asynch (UNIT *uptr, int latency);
t_stat sim_disk_clr_asynch (UNIT *uptr);
t_stat sim_disk_reset (UNIT *uptr);
t_stat sim_disk_perror (UNIT *uptr, const char *msg);
t_stat sim_disk_clearerr (UNIT *uptr);
t_bool sim_disk_isavailable (UNIT *uptr);
t_bool sim_disk_isavailable_a (UNIT *uptr, DISK_PCALLBACK callback);
t_bool sim_disk_wrp (UNIT *uptr);
t_stat sim_disk_pdp11_bad_block (UNIT *uptr, int32 sec, int32 wds);
const char *sim_disk_decode_mediaid (uint32 MediaId);
uint32 sim_disk_drive_type_to_mediaid (const char *drive_type, const char *device_type);
uint32 sim_disk_get_mediaid (UNIT *uptr);
t_offset sim_disk_size (UNIT *uptr);
t_bool sim_disk_vhd_support (void);
t_bool sim_disk_raw_support (void);
void sim_disk_data_trace (UNIT *uptr, const uint8 *data, size_t lba, size_t len, const char* txt, int detail, uint32 reason);
t_stat sim_disk_info_cmd (int32 flag, CONST char *ptr);
t_stat sim_disk_set_noautosize (int32 flag, CONST char *cptr);
t_bool sim_disk_autosize_disabled (void);
t_stat sim_disk_set_drive_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sim_disk_set_drive_type_by_name (UNIT *uptr, const char *drive_type);
t_stat sim_disk_show_drive_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
const char *sim_disk_drive_type_set_string (UNIT *uptr);
t_stat sim_disk_test (DEVICE *dptr, const char *cptr);


struct DRVTYP {
    uint32      sect;           /* sectors */
    uint32      surf;           /* surfaces */
    uint32      cyl;            /* cylinders */
    uint32      size;           /* size in LBNs */
    const char  *name;          /* name */
    uint32      sectsize;       /* sector size in bytes */
    uint32      flags;          /* flags */
    const char  *driver_name;   /* OS Driver device name */
    uint32      MediaId;        /* MSCP media id */
    uint32      model;          /* model */
    const char  *name_alias;    /* Alias device type name */
    const char  *name_desc;     /* Descriptive Text for device type */
    uint32      tpg;            /* trk/grp */
    uint32      gpc;            /* grp/cyl */
    uint32      xbn;            /* XBN size */
    uint32      dbn;            /* DBN size */
    uint32      rcts;           /* RCT size */
    uint32      rctc;           /* RCT copies */
    uint32      rbn;            /* RBNs */
    uint32      cylp;           /* first cyl for write precomp */
    uint32      cylr;           /* first cyl for reduced write current */
    uint32      ccs;            /* cyl/cyl skew */
    uint32      devtype;        /* SCSI Device Type */
    uint32      pqual;          /* SCSI pqual */
    uint32      scsiver;        /* SCSI scsi version */
    const char *manufacturer;   /* SCSI manufacturer string */
    const char *product;        /* SCSI product string */
    const char *rev;            /* SCSI revision string */
    uint32      gaplen;         /* SCSI tape gap length */
    };

/* Contents/Values in DRVTYP.flags field */

#define DRVFL_V_TYPE    0                       /* Interface Type */
#define DRVFL_W_TYPE    5       
#define DRVFL_M_TYPE    ((1u << DRVFL_W_TYPE) - 1)
#define DRVFL_TYPE_MFM  (0 << DRVFL_V_TYPE)
#define DRVFL_TYPE_SDI  (1 << DRVFL_V_TYPE)
#define DRVFL_TYPE_RC   (2 << DRVFL_V_TYPE)
#define DRVFL_TYPE_DSSI (3 << DRVFL_V_TYPE)
#define DRVFL_TYPE_SCSI (4 << DRVFL_V_TYPE)
#define DRVFL_TYPE_RM   (5 << DRVFL_V_TYPE)
#define DRVFL_TYPE_RP   (6 << DRVFL_V_TYPE)
#define DRVFL_TYPE_RL   (7 << DRVFL_V_TYPE)
#define DRVFL_GET_IFTYPE(drv) (((drv)->flags >> DRVFL_V_TYPE) & DRVFL_M_TYPE)
#define DRVFL_V_RMV     (DRVFL_V_TYPE + DRVFL_W_TYPE)
#define DRVFL_RMV       (1u << DRVFL_V_RMV)     /* Removable */
#define DRVFL_V_RO      (DRVFL_V_RMV + 1)
#define DRVFL_RO        (1u << DRVFL_V_RO)      /* Read Only */
#define DRVFL_V_DEC144  (DRVFL_V_RO + 1)
#define DRVFL_DEC144    (1u << DRVFL_V_DEC144)  /* DEC 144 Bad Block track */
#define DRVFL_V_SETSIZE (DRVFL_V_DEC144 + 1)
#define DRVFL_SETSIZE   (1u << DRVFL_V_SETSIZE) /* Settable Drive Size/Capacity */
#define DRVFL_V_NOCHNG  (DRVFL_V_SETSIZE + 1)
#define DRVFL_NOCHNG    (1u << DRVFL_V_NOCHNG)  /* Can't change drive type once set */
#define DRVFL_V_NORMV   (DRVFL_V_NOCHNG + 1)
#define DRVFL_NORMV     (1u << DRVFL_V_NORMV)   /* Can't change to a removable drive */

/* DRVTYP Initializer for SCSI disk and/or tape */
#define DRV_SCSI(typ, pq, ver, rmv, bsz, lbn, man, prd, rev, nm, gap, txt) \
    46, 9, 1549, lbn, nm, bsz, (rmv == TRUE) ? DRVFL_RMV : 0,              \
    NULL, 0, 0, NULL, txt,                                                 \
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                          \
    typ, pq, ver, man, prd, rev, gap

#define DRV_MINC       512                             /* min cap LBNs */
#define DRV_MAXC       4194303                         /* max cap LBNs */
#define DRV_EMAXC      2147483647                      /* ext max cap */


#ifdef  __cplusplus
}
#endif

#endif
