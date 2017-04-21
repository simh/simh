                         ODS2               June '98

          A Program to Read ODS2 Disk Volumes


Say, what is this?
   ODS2 is a program to read VMS disk volumes written in VMS
   ODS2 format.

Why bother?
   Well sometimes it is convenient to see what is on a VMS CD,
   or copy files from VMS disk on another platform. Maybe in
   future it could be used as a basis for a PC Bookreader
   program, or possibly other software?

What other platforms?
   ODS2 is written in 'Standard C' with the intent that it can
   be compiled and run on non-VMS systems. However this requires
   a C compiler which has 32 bit 'int' types, 16 bit 'short'
   types, and 8 bit 'char'. The current version does no special
   'endian' handling so it can only be run on systems which are
   little-endian like the VAX. Fortunately that inludes Intel...

Could it be made to run on big-endian systems?
   Yes it could! This would probably best be done by defining
   C macros or functions to extract words or long words from the
   disk structures as appropriate. On a little-endian system these
   would be simply pass-through, while on a big-endian system they
   would reverse the byte order. I have not attempted this because
   I don't have a suitable test system!!

What else is needed?
   Some operating system support is also required to read an
   absolute disk sector from the target disk. This is NOT as
   easy as it sounds. I do not currently have sufficient
   documentation to set this up for as many platforms as I
   would like!! However I do have modules to read disks under
   VMS (easy!) and floppies and CD under OS2, Windows 95 and
   Windows NT.

How do I build it?
   On a VMS system ODS2 can be compiled by executing the
   procedure BUILD.COM. On other platforms you need to compile
   Ods2.c, Rms.c, Direct.c, Access.c, Device.c, Cache.c, Vmstime.c,
   and the appropriate Phy*.c routine for your system. On OS/2 I
   compile using the gcc command:-
       gcc -fdollars-in-identifiers ods2.c,rms.c,direct.c,
                      access.c,device.c,cache.c,phyos2.c,vmstime.c

What can it do?
   Basically ODS2 provides cut down DIRECTORY, COPY and
   SEARCH commands for VMS volumes on non-VMS systems. These
   can be used to find out what is on a VMS volume, and copy
   files onto the local file sytem.

What file types?
   Basically ODS2 can only deal with sequential files. I do not
   have information on how indexed file types are constructed,
   and relative files are of limited interest.

What about volume sets?
   ODS2 does contain support for multi-volume sets. However there
   is no checking that the correct volumes have been specified and
   error handling is very fragile. You should ensure that you
   specify volume set mount commands very carefully!

What about ODS5?
   Sorry, but I have no idea! I have not seen any ODS5 information
   or specifications. Most likely this program will fall in a heap
   when presented with an ODS5 disk volume.

What about bugs?
   There are plenty!! This code has been tested for several hours
   by a single developer/user on one workstation (absolutely no
   testing or input from any other person up to this point!).
   Contrast this to the VMS filesystem which has had hundreds of
   developers, millions of users, and has run on about 500,000
   systems over the last 20 years!! I would hope to fix some of
   the more severe limitations and provide better error handling
   fairly soon, perhaps even put some comments into the code?
   Maybe the next release might have fewer bugs?

It is free?
   Yeap! It is provided 'as is' to help people in the VMS
   community. However there is NO SUPPORT! I will try to fix
   any reported problems, but as I really only get to work on
   it while the kids are at Scouts on Monday nights, fixes
   happen slowly! But if you have comments or suggestions then
   please feel free to mail me!

Can I use the code?
   Yeap! You can use and modify the code provided that you
   acknowledge the author in the source of any modules which use
   any part of this code. I would also appreciate, where possible,
   being sent any enhancements for my own use.

Can I call the routines from my program?
   You should be able to. Many of the modules follow an 'RMS'
   sort of standard, and programs which only ever read RMS files
   from C might be converted to read VMS volumes on another platform
   without too much effort. In addition to the RMSish interface,
   it would not be difficult to package up an $ASSIGN/$QIOW interface
   for common file operations...

What is the status of ODS2?
   This is the first release - actually more like a prototype
   than an real release! But it may generate useful feedback and
   possibly be useful to others the way it is? However if you are
   tempted to use this version for real file transfers, DO YOUR OWN
   TESTING first! This program may not have encountered volumes and
   files like yours and could easily generate garbage results!!!

Is more work happening?
   Yes! I find the program very useful moving stuff from my
   VAXstation to my PC. I would like to be able to write ODS2 volumes
   so that I can move files in the other direction! And yes I hope
   to generally improve the code - particularly in the area of error
   handling!

Can I make a suggestion?
   You sure can! If you see something which needs improvement then
   let me know. It may be more interesting than whatever I am doing now!
   In fact if I don't hear from anyone then I might even loose interest!

Can I see a command sample?
   Sure:-
     C:> ODS2
       ODS2 v1.2
     $> mount E:,F:
     %MOUNT-I-MOUNTED, Volume FOX1         mounted on E:
     %MOUNT-I-MOUNTED, Volume FOX2         mounted on F:
     $> direct/file E:[*...]
        Directory E:[PNANKERVIS]
     3MONTH.LOWUSE;1      (51,20,2)
     ACCTEST.COM;1        (137,4,1)
     ACCTEST.EXE;1        (53,4,2)
     .....
     $> set default E:[sys0.sysmgr]
     $> dir [-.sys*...].%
     .....
     $> copy *.c  *.*
     %COPY-S-COPIED, E:[SYS0.SYSMGR]ACCESS.C;1 copied to ACCESS.C (3 records)
     ....
     $> show time
       24-MAR-1998 20:15:23.5
     $> dismount E:
     $> exit

What commands are supported?
  A summary is:-
     mount       DRIVE:[,DRIVE:...]
     directory   [/file|/size|/date]   [FILE-SPEC]
     copy        FILE-SPEC  OUTPUT-FILE
     dismount    DRIVE:
     search      FILE-SPEC  STRING
     set default DIR-SPEC
     show default
     show time
     exit
          Note  - DRIVE: is normally the native system drive name,
                  for example D: might be a CD on a PC - xxx: might be
                  /dev/xxx on a Unix system.
                - when a list of drives is specified on the mount command
                  they are assumed to contain a single valid volume set
                  (this is not validated!!)
                - file-spec is in the usual VMS syntax and may contain
                  wildcards (for example  A:[-.*obj%%...]*abc*.obj;-2)

Who would write this?
   Me! Maybe it will become the basis of something more? If you
   have suggestions or want to know more then please mail me at
   paulnank@au1.ibm.com

                                                    Thanks for listening!
                                                    Paul Nankervis
