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
#define DKUF_W_FMT      2                               /* 2b of formats */
#define DKUF_M_FMT      ((1u << DKUF_W_FMT) - 1)
#define DKUF_F_AUTO      0                              /* Auto detect format format */
#define DKUF_F_STD       1                              /* SIMH format */
#define DKUF_F_RAW       2                              /* Raw Physical Disk Access */
#define DKUF_F_VHD       3                              /* VHD format */
#define DKUF_V_NOAUTOSIZE (DKUF_V_FMT + DKUF_W_FMT)     /* Don't Autosize disk option */
#define DKUF_V_UF       (DKUF_V_NOAUTOSIZE + 1)
#define DKUF_WLK        UNIT_WLK
#define DKUF_FMT        (DKUF_M_FMT << DKUF_V_FMT)
#define DKUF_WRP        (DKUF_WLK | UNIT_RO)
#define DKUF_NOAUTOSIZE (1 << DKUF_V_NOAUTOSIZE)

#define DK_F_STD        (DKUF_F_STD << DKUF_V_FMT)
#define DK_F_RAW        (DKUF_F_RAW << DKUF_V_FMT)
#define DK_F_VHD        (DKUF_F_VHD << DKUF_V_FMT)

#define DK_GET_FMT(u)   (((u)->flags >> DKUF_V_FMT) & DKUF_M_FMT)

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
                             size_t reserved_sectors);  /* Unused sectors beyond the file system */
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
t_stat sim_disk_set_asynch (UNIT *uptr, int latency);
t_stat sim_disk_clr_asynch (UNIT *uptr);
t_stat sim_disk_reset (UNIT *uptr);
t_stat sim_disk_perror (UNIT *uptr, const char *msg);
t_stat sim_disk_clearerr (UNIT *uptr);
t_bool sim_disk_isavailable (UNIT *uptr);
t_bool sim_disk_isavailable_a (UNIT *uptr, DISK_PCALLBACK callback);
t_bool sim_disk_wrp (UNIT *uptr);
t_stat sim_disk_pdp11_bad_block (UNIT *uptr, int32 sec, int32 wds);
t_offset sim_disk_size (UNIT *uptr);
t_bool sim_disk_vhd_support (void);
t_bool sim_disk_raw_support (void);
void sim_disk_data_trace (UNIT *uptr, const uint8 *data, size_t lba, size_t len, const char* txt, int detail, uint32 reason);
t_stat sim_disk_info_cmd (int32 flag, CONST char *ptr);
t_stat sim_disk_set_noautosize (int32 flag, CONST char *cptr);
t_stat sim_disk_test (DEVICE *dptr, const char *cptr);

#ifdef  __cplusplus
}
#endif

#endif
