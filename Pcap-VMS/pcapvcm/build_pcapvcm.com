$!***************************************************************************
$!*                                                                         *
$!*  Copyright (c) 2003 Hewlett-Packard Corporation                         *
$!*                                                                         *
$!*  All Rights Reserved.                                                   *
$!*  Unpublished rights reserved under the copyright laws  of  the  United  *
$!*  States.                                                                *
$!*                                                                         *
$!*  The software contained on this media is proprietary to  and  embodies  *
$!*  the  confidential  technology  of  Hewlett-Packard Corporation.        *
$!*  Possession, use, duplication or dissemination  of  the  software  and  *
$!*  media  is  authorized  only  pursuant to a valid written license from  *
$!*  Hewlett-Packard Corporation.                                           *
$!*                                                                         *
$!*  RESTRICTED RIGHTS LEGEND Use, duplication, or disclosure by the  U.S.  *
$!*  Government  is  subject  to restrictions as set forth in Subparagraph  *
$!*  (c)(1)(ii) of DFARS 252.227-7013, or in FAR 52.227-19, as applicable.  *
$!***************************************************************************
$! This command procedure will build the PCAPVCM execlet.
$! The resulting PCAPVCM.EXE must be copied to the
$! SYS$LOADABLE_IMAGE directory for it to be useable.
$
$! Define our location
$
$ SAVE_DEFAULT  = F$ENVIRONMENT("DEFAULT")
$ NEW_DEFAULT   = F$ENVIRONMENT("PROCEDURE")
$ NEW_DEFAULT   = F$PARSE(NEW_DEFAULT,,,"DEVICE") + -
                  F$PARSE(NEW_DEFAULT,,,"DIRECTORY")
$ SET DEFAULT 'NEW_DEFAULT'
$
$ DEFINE PCAPVCM$OBJ 'NEW_DEFAULT'
$
$! Assemble the source
$
$ MACRO/MIGRATE/NOTIE/list/machine PCAPVCM_INIT + SYS$LIBRARY:ARCH_DEFS.MAR + -
                      SYS$LIBRARY:LIB.MLB/LIB
$ MACRO/MIGRATE/NOTIE/list/machine VCI_JACKET + SYS$LIBRARY:ARCH_DEFS.MAR + -
                      SYS$LIBRARY:LIB.MLB/LIB
$ CC/INSTRUCTION=NOFLOAT/EXTERN=STRICT_REFDEF/NAMES=UPPER/list/machine -
      PCAPVCM + SYS$LIBRARY:SYS$LIB_C.TLB/LIB/POINTER_SIZE=SHORT/diag
$ CC/INSTRUCTION=NOFLOAT/EXTERN=STRICT_REFDEF/NAMES=UPPER/list/machine -
      VCMUTIL +   SYS$LIBRARY:SYS$LIB_C.TLB/LIB/pointer_size=short
$
$! Create a little object library, to make linking easier.
$
$ IF F$SEARCH("PCAPVCM.OLB") .EQS. ""
$ THEN
$     LIBRARY/CREATE/OBJECT PCAPVCM
$     LIBRARY/OBJECT/INSERT PCAPVCM PCAPVCM_INIT,PCAPVCM,vci_jacket,vcmutil
$ ELSE
$     LIBRARY/OBJECT/REPLACE PCAPVCM PCAPVCM_INIT,PCAPVCM,vci_jacket,vcmutil
$ ENDIF
$
$! Link it
$ LINK/NOTRACE/NOUSERLIB/MAP=pcapvcm/FULL/NOSYSLIB/NOSYSSHR -
                  /SHARE -
                  /NATIVEONLY -
                  /BPAGE=14/NOTRACEBACK/NODEMAND_ZERO -
                  /SECTION_BINDING -
                  /SYSEXE=SELECTIVE -
                  /EXE=PCAPVCM.EXE -
                  SYS$INPUT/OPT

SYMBOL_TABLE=GLOBALS

!
vector_table = sys$share:sys$public_vectors.exe
!
! Ensure fixups are done before our initialization routines are called
!
ALPHA$LIBRARY:STARLET/INCLUDE=(SYS$DOINIT,SYS$SSDEF)
!
PCAPVCM$OBJ:PCAPVCM/INCLUDE=(PCAPVCM_INIT, PCAPVCM, VCI_JACKET, VCMUTIL)
SYS$LIBRARY:VMS$VOLATILE_PRIVATE_INTERFACES/INCLUDE=(BUGCHECK_CODES)
!
CASE_SENSITIVE=YES
!
! Mess up psect attributes, I'm slight;y rusty on this and have
! missed some... so do not try to unload the execlet => crash
!
PSECT_ATTR=$CODE,		PIC,CON,REL,GBL,NOSHR,  EXE,NOWRT,NOVEC,MOD
PSECT_ATTR=$CODE$,		PIC,CON,REL,GBL,NOSHR,  EXE,NOWRT,NOVEC,MOD
!
PSECT_ATTR=$$$100_DATA,		PIC,CON,REL,GBL,NOSHR,NOEXE,  WRT,NOVEC,MOD
PSECT_ATTR=$LINK$,		PIC,CON,REL,GBL,NOSHR,NOEXE,  WRT,NOVEC,MOD
PSECT_ATTR=$LINKAGE$,		PIC,CON,REL,GBL,NOSHR,NOEXE,  WRT,NOVEC,MOD
PSECT_ATTR=$LINKAGE,		PIC,CON,REL,GBL,NOSHR,NOEXE,  WRT,NOVEC,MOD
PSECT_ATTR=$DATA$,		PIC,CON,REL,GBL,NOSHR,NOEXE,  WRT,NOVEC,MOD
PSECT_ATTR=EXEC$NONPAGED_DATA,	PIC,CON,REL,GBL,NOSHR,NOEXE,  WRT,NOVEC,MOD
PSECT_ATTR=$LITERAL$,		PIC,CON,REL,GBL,NOSHR,NOEXE,  WRT,NOVEC,MOD
PSECT_ATTR=$READONLY$,		PIC,CON,REL,GBL,NOSHR,NOEXE,  WRT,NOVEC,MOD
PSECT_ATTR=$BSS$,		PIC,CON,REL,GBL,NOSHR,NOEXE,  WRT,NOVEC,MOD
!
PSECT_ATTR=EXEC$INIT_LINKAGE,	PIC,CON,REL,GBL,NOSHR,  EXE,  WRT,NOVEC,MOD
PSECT_ATTR=EXEC$INIT_000,	PIC,CON,REL,GBL,NOSHR,  EXE,  WRT,NOVEC,MOD
PSECT_ATTR=EXEC$INIT_001,	PIC,CON,REL,GBL,NOSHR,  EXE,  WRT,NOVEC,MOD
PSECT_ATTR=EXEC$INIT_002,	PIC,CON,REL,GBL,NOSHR,  EXE,  WRT,NOVEC,MOD
PSECT_ATTR=EXEC$INIT_CODE,	PIC,CON,REL,GBL,NOSHR,  EXE,  WRT,NOVEC,MOD
PSECT_ATTR=EXEC$INIT_SSTBL_000,	PIC,CON,REL,GBL,NOSHR,  EXE,  WRT,NOVEC,MOD
PSECT_ATTR=EXEC$INIT_SSTBL_001,	PIC,CON,REL,GBL,NOSHR,  EXE,  WRT,NOVEC,MOD
PSECT_ATTR=EXEC$INIT_SSTBL_002	PIC,CON,REL,GBL,NOSHR,  EXE,  WRT,NOVEC,MOD
!
! Collect all the psects into clusters
!
COLLECT=NONPAGED_CODE/ATTRIBUTES=RESIDENT,	$CODE, -
						$CODE$
COLLECT=NONPAGED_DATA/ATTRIBUTES=RESIDENT,	$$$100_DATA, -
						$LINK$, -
						$LINKAGE$, -
						$LINKAGE, -
						$DATA$, -
						$LITERAL$, -
						$READONLY$, -
						$BSS$, -
                                                EXEC$NONPAGED_DATA
COLLECT=INIT/ATTRIBUTES=INITIALIZATION_CODE,	EXEC$INIT_LINKAGE, -
						EXEC$INIT_000, -
						EXEC$INIT_001, -
						EXEC$INIT_002, -
						EXEC$INIT_CODE, -
						EXEC$INIT_SSTBL_000, -
						EXEC$INIT_SSTBL_001, -
						EXEC$INIT_SSTBL_002
$
$ WRITE SYS$OUTPUT ""
$ WRITE SYS$OUTPUT "To use the PCAPVCM.EXE execlet you must copy it to"
$ WRITE SYS$OUTPUT "the SYS$LOADABLE_IMAGES directory."
$ WRITE SYS$OUTPUT ""
$ SET DEFAULT 'SAVE_DEFAULT'
$ EXIT
