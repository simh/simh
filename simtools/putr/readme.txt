This directory contains PUTR.COM V2.01, a program for accessing DEC file
systems from DOS PCs.

Supported file systems:
DOS/BATCH (both disks and DECtapes)
Files-11 (read only)
OS/8
RSTS/E
RT-11
TSS/8.24 DECtape (PUTR.SAV format)
TSS/8.24 system disk (read only)
XXDP+

Supported media:
disk image files (can apply/remove interleave for floppy image files)
RX33, RX50 floppies in 1.2MB drive
RX01/02/03 workalike floppies in 1.2MB drive
RX23, RX24, RX26 disks in 3.5" 720KB/1.44MB/2.88MB drive
RX01 real floppies in 8" drive with appropriate controller
TU58 drives plugged into PC COM port
SCSI disks through ASPI driver
CD-ROM
(Catweasel/ISA driver is unfinished and therefore undocumented)

The program accepts a subset of the usual DOS commands, but allows you to
MOUNT non-DOS disks (with a fake drive letter or "ddu:" style name).  Once
you've mounted a foreign disk, you can "log in" to it just like a DOS disk,
and TYPE, COPY (including COPY /B), DIR etc. will work (no RENAME yet, sorry).

The program's basic point of pride is the fact that you can mount more than
one foreign disk at once, and they may be from different operating systems,
so you can copy file between foreign file systems w/o the intermediate step
of copying them into a DOS directory and back out again (especially handy if
space is tight and/or your files have dates before 01/01/1980).

By the way, whatever your stinking web browser may tell you, "putr.doc" is
a plain ASCII file!  This shouldn't be a problem when accessing this server
via HTTP, because the server's MIME types file has this set up properly.

John Wilson  wilson@dbit.com   05-Sep-2001
