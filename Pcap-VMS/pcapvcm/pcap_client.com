$! This command procedure will compile and link the image
$! loader needed to load IP_VCM.EXE.  
$
$ set noon
$! ON WARNING THEN GOTO CLEANUP
$
$ SAVE_DEFAULT  = F$ENVIRONMENT("DEFAULT")
$ NEW_DEFAULT   = F$ENVIRONMENT("PROCEDURE")
$ NEW_DEFAULT   = F$PARSE(NEW_DEFAULT,,,"DEVICE") + -
                  F$PARSE(NEW_DEFAULT,,,"DIRECTORY")
$ SET DEFAULT 'NEW_DEFAULT'
$
$ CC/debug/noopt PCAP_CLIENT/list/machine + -
  SYS$LIBRARY:SYS$LIB_C.TLB/LIB + -
  SYS$LIBRARY:SYS$STARLET_C.TLB/LIB
$
$ LINK/SYSEXE/debug pcap_client,vcmutil/map
$
$ CLEANUP:
$
$ SET DEFAULT 'SAVE_DEFAULT'
