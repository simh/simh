
# SEL32 Concept/32 Simulator

The installs directory contains the simh command files to install and run
multiple UTX, MPX1X, and MPX3X systems.  The install tape images are in
the tapes directory and created disks are in the dsk directory.  The dsk
and tapes directories are initially missing but will be created during
the installation.  The required tape(s) are read from the repo at
https://github.com/AZBevier/SEL32-installs when the installation
simh command file is run.  The tapes are tar gzipped files that are
extracted during the installation.  Only the tape(s) required for the
specific UTX or MPX installation are downloaded.  A bootable disk is
created in the dsk directory.  This disk can then be booted and the
installed operating system executed by using a second simh command file.

This method of installation replaces the prebuilt disks from previous
releases.  This allows more versions to be supplied in the minimum
amount of storage space.  Minimal or no user input is required to
create the bootable system.  The command files are in pairs, one to do
the install and one to execute the installed system.  Some MPX systems
also contain some of the NBC software environment.  See the NBC project
at github.com/AZBevier/nbc for all of the NBC software.  See the MPX
manuals at bitsavers.org/pdf/sel/sel32_mpx for using MPX 1X or 3X.

# SEL32 installation configuration files in the install directory:

--------------------

#sel32load1x.ini - sel32sdt.tap; type "../sel32 sel32load1x.ini
This is a minimal MPX 1.5F installation to a UDP/DPII 300 MB disk.  It
will initialize the disk and install an MPX bootable system.  The disk
image is in the dsk directory named sel32disk.

#sel32run1x.ini - dsk/sel32disk; type "../sel32 sel32run1x.ini" to run.
The disk is booted up to the TSM> prompt logged in as "SYSTEM".  Use
@@A to log into the system console.

--------------------

#loaddma1x67.ini - mpx1xsdt.tap; type "../sel32 loaddma1x67.ini
This is an MPX 1.5F installation to a UDP/DPII 300 MB disk.  It will
initialize the disk and install an MPX bootable system.  The disk
image is in the dsk directory named mpx1xdma0.  Once the MPX software
is loaded an MPX command file is executed that runs the SYSGEN program
to create a new MPX O/S image.  That system is then restarted to
install the new image to the disk as the new bootable image.  The
system reboots to the new image, logs in as SYSTEM and exits TSM.
Use @@A to relogin to the console.

#rundma1x67.ini - dsk/mpx1xdma0; type "../sel32 rundma1x67.ini" to run.
The disk is booted up to the TSM> prompt and logged in as "SYSTEM".
MPX can be accessed from a second Linux screen by using the command
"telnet localhost 4747".  This will bring up the "ENTER OWNERNAME
AND KEY:".  Any name is valid, but SYSTEM should be used.  At the
"TSM>" prompt, type "EXIT" to exit TSM.  Use ^G to get the login
prompt when the "RING IN FOR SERVICE" message is displayed.

--------------------

#loaddma21b.ini - utx21b1.tap, utx21b2.tap, utx21b3.tap;
type "../sel32 loaddma21b.ini" to install UTX 21b to UDP/DPII disk.
This is an automated installation of UTX 21b to disk.  Two disks,
21bdisk0.dma and 21bdisk1.dma are initialized and then the file
systems are created and loaded.  Tape 1 loads "/" and tape 2 and 3
loads "/usr.POWERNODE" filesystems. The system boots from tape and
installs the root filesystem.  The system restarts and boots from
the new root filesystem where the 2nd & 3rd tapes are then loaded to
/usr.POWERNODE.  A third empty file system is created and mounted
as /usr/POWERNODE/src.  The second disk is one large filesystem and is
mounted under /home.  Several files are modified during installation
to allow the system to be booted into multiuser mode.  Only the user
"root" is created and is the only allowable user login.

#rundma21b.ini - dsk/21bdisk0.dma & dsk/21bdisk1.dma;
type "../sel32 rundma21b.ini" to run the installed UTX system.
The disk is booted up to the "login:" prompt for the user to login
as "root" in multi-user mode.

--------------------

#loadscsi21b.ini - utx21b1.tap, utx21b2.tap, utx21b3.tap;
type "../sel32 loadscsi21b.ini" to install UTX 21b to MFP SCSI disks.
This is an automated installation of UTX 21b to disk.  Two disks,
scsidiska0 and scsidiska1 are initialized and then the file systems
are created and loaded.  Tape 1 loads root "/" and tapes 2 and 3
loads "/usr.POWERNODE" filesystem. The system boots from tape and
installs the root filesystem.  The system restarts and boots from
the new root filesystem where the 2nd & 3rd tapes are then loaded to
/usr.POWERNODE.  A third empty file system is created and mounted
as /usr/POWERNODE/src.  The second disk is one large filesystem and is
mounted under /home.  Several files are modified during installation
to allow the system to be booted into multiuser mode.  Only the user
"root" is created and is the only allowable user login.

#rundscsi21b.ini - dsk/scsidiska0 & dsk/scsidiska1;
type "../sel32 rundscsi21b.ini" to run the installed UTX system.
The disk is booted up to the "login:" prompt for the user to login
as "root" in multi-user mode.

--------------------

#loadscsi3x.ini - mpxsdt69.tap;
type "../sel32 loadscsi3x.ini" to install MPX 3.4 to MFP SCSI disks.
This is an automated installation of MPX 3.4 to disk.  Two 300MB disks,
mpx3xsba0.dsk and mpx3xsbb0.dsk are initialized and then the file
systems are created and loaded.  The user sdt tape contains system
and user files that are loaded to multiple directories.  The second
disk is initialized and formatted and only a system directory defined.
The install is exited and @@A is used to login into MPX.  The username
SYSTEM is used to login into TSM without a password.

#runscsi3x.ini - dsk/mpx3xsba0.dsk & dsk/mpx3csbb0.dsk;
               - dsk/scsi35m1disk0 & dsk/scsi35m2disk0;
type "../sel32 rundscsi3x.ini" to run the installed MPX system.
The disk is booted up to the MPX message "Press Attention for TSM".
Use @@A to get login prompt.  Login as SYSTEM.  The WORK volume will
be mounted along with the SYSTEM volume and the system is ready for
use.  MPX can be accessed from a second Linux screen by using the
command "telnet locallhost 4747".  This will bring up the "Connected
to the SEL-32 simulator COMC device, line 0".  Use ^G as the wakeup
character to get the "ENTER YOUR OWNERNAME:" login prompt.  Any name
is valid, but SYSTEM should be used.  At the "TSM>" prompt, type
"EXIT" to exit TSM.  Use ^G to get the login prompt when the "RING
IN FOR SERVICE" message is displayed.

--------------------

#user36esdtp2.ini - user36esdtp2.tap;
type "../sel32 user36esdtp2.ini" to install MPX 3.6 to HSDP disks.
This is an automated installation of MPX 3.6 to disk.  A 300MB system
disk volume (user36p2udp0) and a 600MB work disk volume (user36s1udp1)
are initialized and then the file systems are created and loaded using
the volmgr.  The user sdt tape contains system and user files that
are loaded to multiple directories.  The second disk is initialized
and formatted and only a system directory defined.  The disk is mounted
as the volume "work" as the 2nd disk drive.  The installed MPX system
also has 2 scsi disks configured into the system.  Two 700MB SCSI disks
are created, but they are not initialized and no directories are
created.  The usage of these disks is left as an exercise for the user.
A third HSDP 600MD disk is also configured in MPX, but not used.  The
user can provide other data volumes that can be mounted for use on the
system.  The install is exited and @@A can be used to login into MPX.
The username SYSTEM is used to login into TSM without a password.  Any
username is valid until an m.key file is created for valid user logins.

#user36erunp2.ini - dsk/user36p2udp0 & dsk/user36s1udp1;
type "../sel32 user36erunp2.ini" to run the installed MPX 3.6 system.
The disk is booted up to the MPX message "Press Attention for TSM".
@@A is used to get the login prompt and the user is logged in as SYSTEM.
The WORK volume will be mounted along with the SYSTEM volume and the
system is ready for use at the TSM> prompt.  The install tape also has
some of the NBC development system.  A complete installation tape is
available at github.com/azbevier/nbc.

--------------------

Other MPX versions support:

I have recently received some old MPX 3.X save tapes.  Using these
I have been able to hand build a MPX3.6 SDT tape that can be used
to install MPX3.6.  Once installed, the system can be used to build
a new user SDT tape and install it elsewhere.  Both based and non-
based O/S images can be created.  More images for installation will
be made available in the future as I work my way through the save
tapes. I still do not have a master SDT tape for any of the MPX 1.X
or MPX 3.X systems.  I have a 1600/6250 BPI tape drive that can read
9 track tapes and convert them to .tap files.  If you have a master
SDT, I would be very thankfull.  Please keep looking.

James C. Bevier
02/27/2022 

