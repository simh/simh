$!
$! BUILD_VMS.COM
$! Written By:  Robert Alan Byer
$!              byer@mail.ourservers.net
$!
$!
$! This script is used to compile and thar various simualtors in the SIMH
$! package for OpenVMS using  DEC C v6.0-001.
$!
$! The script accepts the following parameters.
$!
$!	P1	ALL		Just Build "Everything".
$!		ALTAIR		Just Build The MITS Altair.
$!              ALTAIRZ80       Just Build The MITS Altair Z80.
$!              ECLIPSE		Just Build The Data General Eclipse.
$!		H316		Just Build The Honewell 316/516.
$!		HP2100		Just Build The Hewlett-Packard HP-2100. 
$!		I1401		Just Build The IBM 1401.
$!              IBM1130         Just Build The IBM 1130.
$!		INTERDATA	Just Build The Interdata 4.
$!		NOVA		Just Build The Data General Nova.
$!		PDP1		Just Build The DEC PDP-1.
$!		PDP8		Just Build The DEC PDP-8.
$!		PDP10		Just Build The DEC PDP-10.
$!		PDP11		Just Build The DEC PDP-11.
$!		PDP18B		Just Build The DEC PDP-4, PDP-7, PDP-9 And PDP-15.
$!		S3		Just Build The IBM System 3.
$!		SDS		Just Build The SDS System.
$!		VAX		Just Build The DEC VAX.
$!
$!	P2	DEBUG		Link With Debugger Information.
$!		NODEBUG		Link Withoug Debugger Information.
$!
$!
$! The defaults are "ALL" and "NODEBUG".
$!
$!
$! Define The Simualtors We Have That We Can Build.
$!
$ SIMH_SIMS = "ALTAIR,ALTAIRZ80,ECLIPSE,H316,HP2100,I1401,IBM1130," + -
              "INTERDATA,NOVA,PDP1,PDP8,PDP10,PDP11,PDP18B,S3,SDS,VAX"
$!
$! Check To Make Sure We Have Valid Command Line Parameters.
$!
$ GOSUB CHECK_OPTIONS
$!
$! Check To See If We Are On An AXP Machine.
$!
$ IF (F$GETSYI("CPU").LT.128)
$ THEN
$!
$!  We Are On A VAX Machine So Tell The User.
$!
$   WRITE SYS$OUTPUT "Compiling On A VAX Machine."
$!
$!  Define The Machine Type.
$!
$   MACHINE_TYPE = "VAX"
$!
$! Else, We Are On An AXP Machine.
$!
$ ELSE
$!
$!  We Are On A AXP Machine So Tell The User.
$!
$   WRITE SYS$OUTPUT "Compiling On A AXP Machine."
$!
$!  Define The Machine Type.
$!
$   MACHINE_TYPE = "AXP"
$!
$! End Of The Machine Check.
$!
$ ENDIF
$!
$! Define The Compile Command.
$!
$ CC = "CC/PREFIX=ALL/''OPTIMIZE'/''DEBUGGER'" + -
         "/NEST=PRIMARY/NAME=(AS_IS,SHORTENED)"
$!
$! Define The SIMH Library Name.
$!
$ SIMHLIB_NAME = "SYS$DISK:[.LIB]SIMH-''MACHINE_TYPE'.OLB"
$!
$! Check To See What We Are To Do.
$!
$ IF (BUILDALL.NES."TRUE")
$ THEN
$!
$!  Define The Name Of The Module We Are To Compile.
$!
$   SIMH_MOD_NAME = P1
$!
$!  Check To See If We Are Going To Build The PDP18B Simulators.
$!
$   IF (SIMH_MOD_NAME.EQS."PDP18B")
$   THEN
$!
$!    Use The Special Build For PDP18B.
$!
$     GOSUB BUILD_PDP18B_MOD
$!
$!  Else...
$!
$   ELSE
$!
$!    Build Just What The User Wants Us To Build.
$!
$     GOSUB BUILD_SIMHLIB_MOD
$!
$!    That's All, Time To EXIT.
$!
$     EXIT
$!
$!  Time To Exit The PDP18B Check.
$!
$   ENDIF
$!
$! Time To End The BUILDALL Check.
$!
$ ENDIF
$!
$! Build The SIMH Library.
$!
$ GOSUB BUILD_SIMHLIB
$!
$! Define A Counter And Set It To "0".
$!
$ SIMH_MOD_COUNTER = 0
$!
$! Top Of The Loop.
$!
$ NEXT_SIMH_MOD_NAME:
$!
$! O.K, Extract The File Module From The File List.
$!
$ SIMH_MOD_NAME = F$ELEMENT(SIMH_MOD_COUNTER,",",SIMH_SIMS)
$!
$! Check To See If We Are At The End Of The Simulator List.
$!
$ IF (SIMH_MOD_NAME.EQS.",") THEN GOTO SIMH_MOD_DONE
$!
$! Increment The Counter.
$!
$ SIMH_MOD_COUNTER = SIMH_MOD_COUNTER + 1
$!
$! Check To See If We Are On VAX.
$!
$ IF (MACHINE_TYPE.EQS."VAX")
$ THEN
$!
$!  Check To See If We Are Build The PDP10 or VAX Simulator.
$!
$   IF (SIMH_MOD_NAME.EQS."PDP10").OR.(SIMH_MOD_NAME.EQS."VAX")
$   THEN
$!
$!    Tell The User We Can't Build PDP10 Or VAX On The VAX
$!    Platform.
$!
$     WRITE SYS$OUTPUT ""
$     WRITE SYS$OUTPUT "Due to the use of INT64, the ''SIMH_MOD_NAME' simulator will not be built for the ''MACHINE_TYPE'"
$     WRITE SYS$OUTPUT "platform."
$     WRITE SYS$OUTPUT ""
$!
$!    Skip And Go To The Next Simulator.
$!
$     GOTO NEXT_SIMH_MOD_NAME
$!
$!  Time To End The PDP10 And VAX Check.
$!
$   ENDIF
$!
$! Time To End The VAX Check.
$!
$ ENDIF
$!
$! Check To See If We Are Going To Build The PDP18B Simulators.
$!
$ IF (SIMH_MOD_NAME.EQS."PDP18B")
$ THEN
$!
$! Use The Special Build For PDP18B.
$!
$  GOSUB BUILD_PDP18B_MOD
$!
$! Else...
$!
$ ELSE
$!
$!  Build The Module.
$!
$  GOSUB BUILD_SIMHLIB_MOD
$!
$! Time To End The PDP18D Check.
$!
$ ENDIF
$!
$! Go Back And Get Another Module Name.
$!
$ GOTO NEXT_SIMH_MOD_NAME
$!
$! End Of The Module List.
$!
$ SIMH_MOD_DONE:
$!
$! All Done Building Modules, Time To EXIT.
$!
$ EXIT
$!
$! Build The SYS$DISK:[.LIB]SIMH-xxx.OLB Library.
$!
$ BUILD_SIMHLIB:
$!
$! Define The C INCLUDES We Are To Use.
$!
$ SIMHLIB_INCLUDES = "INCLUDE=(SYS$DISK:[])"
$!
$! Check To See If We Have A SYS$DISK:[.LIB] Dierctory To Put The
$! Library In.
$!
$ IF (F$SEARCH("SYS$DISK:[]LIB.DIR").EQS."")
$ THEN
$!
$!  A SYS$DISK:[.LIB] Directory Dosen't Exist So Tell The User We
$!  Are Going To Create One.
$!
$   WRITE SYS$OUTPUT "Creating SYS$DISK:[.LIB]"
$!
$!  Create The Directory.
$!
$   CREATE/DIRECTORY SYS$DISK:[.LIB]
$!
$! Time To End The SYS$DISK:[.LIB] Directory Check.
$!
$ ENDIF
$!
$! Check To See If We Already Have A SYS$DISK:[.LIB]SIMH-xxx.OLB Library...
$!
$ IF (F$SEARCH(SIMHLIB_NAME).EQS."")
$ THEN
$!
$!  Guess Not, Create The Library.
$!
$   LIBRARY/CREATE/OBJECT 'SIMHLIB_NAME'
$!
$! End The Library Check.
$!
$ ENDIF
$!
$! Tell The User What We Are Doing.
$!
$ WRITE SYS$OUTPUT ""
$ WRITE SYS$OUTPUT "Compling The ''SIMHLIB_NAME' Library."
$!
$! Tell The User What Compile Command We Are Going To Use.
$!
$ WRITE SYS$OUTPUT "Using Compile Command: ",CC,"/",SIMHLIB_INCLUDES
$ WRITE SYS$OUTPUT ""
$!
$! Top Of The File Loop.
$!
$ NEXT_SIMHLIB_FILE:
$!
$! Define The List Of Files We Are Going To Compile.
$!
$ SIMHLIB_FILES = F$ELEMENT(0,";",F$ELEMENT(1,"]",F$SEARCH("SYS$DISK:[]*.C",1)))
$!
$! Extract The File Name From The File List.
$!
$ SIMHLIB_FILE_NAME = F$ELEMENT(0,".",SIMHLIB_FILES)
$!
$! Check To See If We Are At The End Of The File List.
$!
$ IF (SIMHLIB_FILE_NAME.EQS."]") THEN GOTO SIMHLIB_FILE_DONE
$!
$! Check To See If We We Are At The SCP.C File.
$!
$ IF (SIMHLIB_FILE_NAME.EQS."SCP")
$ THEN
$!
$!  Since We Are At The SCP.C File, Go Back And
$!  Get Another One As We Don't Want Add This To The Library.
$!
$   GOTO NEXT_SIMHLIB_FILE
$!
$! Time To End The SCP.C Check.
$!
$ ENDIF
$!
$! Create The Source File Name.
$!
$ SIMHLIB_SOURCE_FILE = "SYS$DISK:[]" + SIMHLIB_FILE_NAME + ".C"
$!
$! Create The Object File Name.
$!
$ SIMHLIB_OBJECT_FILE = "SYS$DISK:[]" + SIMHLIB_FILE_NAME + ".OBJ"
$!
$! Tell The User What We Are Compiling.
$!
$ WRITE SYS$OUTPUT "	",SIMHLIB_SOURCE_FILE
$!
$! Compile The File.
$!
$ CC/'SIMHLIB_INCLUDES'/OBJECT='SIMHLIB_OBJECT_FILE' 'SIMHLIB_SOURCE_FILE'
$!
$! Add It To The Library.
$!
$ LIBRARY/REPLACE/OBJECT 'SIMHLIB_NAME' 'SIMHLIB_OBJECT_FILE'
$!
$! Delete The Object File.
$!
$ DELETE/NOCONFIRM/NOLOG 'SIMHLIB_OBJECT_FILE';*
$!
$! Go Back And Do It Again.
$!
$ GOTO NEXT_SIMHLIB_FILE
$!
$! All Done Compiling.
$!
$ SIMHLIB_FILE_DONE:
$!
$! That's It, Time To Return From Where We Came From.
$!
$ RETURN
$!
$! Build The Libraries And Simulators..
$!
$ BUILD_SIMHLIB_MOD:
$!
$! Check To See If We Are Going To Build The VAX Simulator.
$!
$ IF (SIMH_MOD_NAME.EQS."VAX")
$ THEN
$!
$!  Define The C INCLUDES For The VAX Simulator.
$!
$   SIMHLIB_MOD_INCLUDES = "/INCLUDE=(SYS$DISK:[]," + -
                           "SYS$DISK:[.''SIMH_MOD_NAME']," + -
                           "SYS$DISK:[.PDP11])"
$!
$! Else...
$!
$ ELSE
$!
$!  Define The Standard C INCLUDES We Are To Use.
$!
$  SIMHLIB_MOD_INCLUDES = "/INCLUDE=(SYS$DISK:[]," + -
                          "SYS$DISK:[.''SIMH_MOD_NAME'])"
$!
$! Time To End The VAX Check.
$!
$ ENDIF
$!
$! Check To See If We Are Going To Build The Eclipse Simulator.
$!
$ IF (SIMH_MOD_NAME.EQS."ECLIPSE")
$ THEN
$!
$!  Define The Module Directory For The Eclipse Simulator.
$!
$   SIMHLIB_MOD_DIR = "SYS$DISK:[.NOVA]"
$!
$! Else...
$!
$ ELSE
$!
$!  Define The Module Directory.
$!
$   SIMHLIB_MOD_DIR = "SYS$DISK:[.''SIMH_MOD_NAME']"
$!
$! Time To End The Ecplise Simulator Check.
$!
$ ENDIF
$!
$! Check To See If We Are Going To Build The Eclipse Simulator.
$!
$ IF (SIMH_MOD_NAME.EQS."ECLIPSE")
$ THEN
$!
$!  Set The Compiler DEFINES For The Eclipse Simulator.
$!
$   SIMHLIB_MOD_DEFINE = "/DEFINE=(""ECLIPSE=1"")"
$!
$! Else...
$!
$ ELSE
$!
$!  Check To See If We Are Going To Build The PDP10 Or VAX Simulator.
$!
$   IF (SIMH_MOD_NAME.EQS."PDP10").OR.(SIMH_MOD_NAME.EQS."VAX")
$   THEN
$!
$!    Set The Compiler DEFINES For The PDP10 Simulator.
$!
$     SIMHLIB_MOD_DEFINE = "/DEFINE=(""USE_INT64=1"")"
$!
$!  Else...
$! 
$   ELSE
$!
$!    Set The Compiler Defines For Everything Else.
$!
$     SIMHLIB_MOD_DEFINE = ""
$!
$!  Time To End The PDP10 And VAX Simulator Check.
$!
$   ENDIF
$!
$! Time To End The Eclipse Simulator Check.
$!
$ ENDIF
$!
$! Check To See If There Are Any Files In The Module Directory.
$!
$ IF (F$SEARCH("''SIMHLIB_MOD_DIR'*.C").EQS."")
$ THEN
$!
$!  There Are No Files To Compile In The Module Directory So
$!  RETURN From Where We Came From And Get Another Module Name.
$!
$   RETURN
$!
$! Time To End The File Check.
$!
$ ENDIF
$!
$! Check To See If We Have A SYS$DISK:[.LIB] Dierctory To Put The
$! Library In.
$!
$ IF (F$SEARCH("SYS$DISK:[]LIB.DIR").EQS."")
$ THEN
$!
$!  A SYS$DISK:[.LIB] Directory Dosen't Exist So Tell The User We
$!  Are Going To Create One.
$!
$   WRITE SYS$OUTPUT "Creating SYS$DISK:[.LIB]"
$!
$!  Create The Directory.
$!
$   CREATE/DIRECTORY SYS$DISK:[.LIB]
$!
$! Time To End The SYS$DISK:[.LIB] Directory Check.
$!
$ ENDIF
$!
$! Create The Module Library Name.
$!
$ SIMHLIB_MOD_LIB_NAME = "SYS$DISK:[.LIB]''SIMH_MOD_NAME'-''MACHINE_TYPE'" + -
                         ".OLB"
$!
$! Check To See If We Already Have A Library...
$!
$ IF (F$SEARCH("''SIMHLIB_MOD_LIB_NAME'").EQS."")
$ THEN
$!
$!  Guess Not, Create The Library.
$!
$   LIBRARY/CREATE/OBJECT 'SIMHLIB_MOD_LIB_NAME'
$!
$! End The Library Check.
$!
$ ENDIF
$!
$! Tell The User What We Are Doing.
$!
$ WRITE SYS$OUTPUT ""
$ WRITE SYS$OUTPUT "Compling The ''SIMHLIB_MOD_LIB_NAME' Library."
$!
$! Tell The User What Compile Command We Are Going To Use.
$!
$ WRITE SYS$OUTPUT "Using Compile Command: ",CC,"''SIMHLIB_MOD_INCLUDES'", -
                    SIMHLIB_MOD_DEFINE
$ WRITE SYS$OUTPUT ""
$!
$! Top Of The File Loop.
$!
$ NEXT_SIMHLIB_MOD_FILE:
$!
$! Check To See If We Are Going To Build Nova.
$!
$ IF (SIMH_MOD_NAME.EQS."NOVA")
$ THEN
$!
$!  Since Nova And Eclipse Share The Same Directory We Only Want The
$!  Nova Files When We Build Nova.
$!
$   SIMHLIB_MOD_FILES = F$ELEMENT(0,";",F$ELEMENT(1,"]", -
                        F$SEARCH("''SIMHLIB_MOD_DIR'''SIMH_MOD_NAME'*.C",1)))
$!
$! Else...
$!
$ ELSE
$!
$!  Define The List Of Files We Are Going To Compile.
$!
$   SIMHLIB_MOD_FILES = F$ELEMENT(0,";",F$ELEMENT(1,"]", -
                        F$SEARCH("''SIMHLIB_MOD_DIR'*.C",1)))
$!
$! Time To End The Nova Simulator Check.
$!
$ ENDIF
$!
$! Extract The File Name From The File List.
$!
$ SIMHLIB_MOD_FILE_NAME = F$ELEMENT(0,".",SIMHLIB_MOD_FILES)
$!
$! Check To See If We Are At The End Of The File List.
$!
$ IF (SIMHLIB_MOD_FILE_NAME.EQS."]") THEN GOTO SIMHLIB_MOD_FILE_DONE
$!
$! Check To See If We Building Eclipse.
$!
$ IF (SIMH_MOD_NAME.EQS."ECLIPSE")
$ THEN
$!
$!  Check To See If We We Are At The NOVA_CPU.C Or NOVA_TT.C File.
$!
$   IF (SIMHLIB_MOD_FILE_NAME.EQS."NOVA_CPU").OR. -
       (SIMHLIB_MOD_FILE_NAME.EQS."NOVA_TT")
$   THEN
$!
$!    Since We Building The Eclipse And Are At The Either The NOVA_CPU.C Or 
$!    NOVA_TT.C File, Go Back And Get Another File As We Don't Want Add 
$!    These To The Eclipse Library.
$!
$     GOTO NEXT_SIMHLIB_MOD_FILE
$!
$!   Time To End The NOVA_CPU.C Check.
$!
$ ENDIF
$!
$! Time To End The Eclipse Check.
$!
$ ENDIF
$!
$! Check To See If We Building IBM 1130
$!
$ IF (SIMH_MOD_NAME.EQS."IBM1130")
$ THEN
$!
$!  Check To See If We We Are At The SYS$DISK:[.IBM1130]SCP.C File.
$!
$   IF (SIMHLIB_MOD_FILE_NAME.EQS."SCP")
$   THEN
$!
$!    Since We Are Building The IBM 1130 Without The GUI Front Panel
$!    Interface (For Now), Go Back And Get Another File As We Don't
$!    Want To Add This To The IBM 1130 Library.
$!
$     GOTO NEXT_SIMHLIB_MOD_FILE
$!
$!   Time To End The SYS$DISK:[.IBM1130]SCP.C Check.
$!
$ ENDIF
$!
$! Time To End The IBM 1130 Check.
$!
$ ENDIF
$!
$! Create The Source File Name.
$!
$ SIMHLIB_MOD_SOURCE_FILE = "''SIMHLIB_MOD_DIR'''SIMHLIB_MOD_FILE_NAME'.C"
$!
$! Create The Object File Name.
$!
$ SIMHLIB_MOD_OBJECT_FILE = "''SIMHLIB_MOD_DIR'''SIMHLIB_MOD_FILE_NAME'.OBJ"
$!
$! Tell The User What We Are Compiling.
$!
$ WRITE SYS$OUTPUT "	",SIMHLIB_MOD_SOURCE_FILE
$!
$! Compile The File.
$!
$ CC 'SIMHLIB_MOD_INCLUDES''SIMHLIB_MOD_DEFINE'/OBJECT='SIMHLIB_MOD_OBJECT_FILE' -
     'SIMHLIB_MOD_SOURCE_FILE'
$!
$! Add It To The Library.
$!
$ LIBRARY/REPLACE/OBJECT 'SIMHLIB_MOD_LIB_NAME' 'SIMHLIB_MOD_OBJECT_FILE'
$!
$! Delete The Object File.
$!
$ DELETE/NOCONFIRM/NOLOG 'SIMHLIB_MOD_OBJECT_FILE';*
$!
$! Go Back And Do It Again.
$!
$ GOTO NEXT_SIMHLIB_MOD_FILE
$!
$! All Done Compiling.
$!
$ SIMHLIB_MOD_FILE_DONE:
$!
$! Check To See If We Are Building The VAX Simulator.
$!
$ IF (SIMH_MOD_NAME.EQS."VAX")
$ THEN
$!
$!  Define The PDP11 Files We Need To Include In The SYS$DISK:[.LIB]VAX-xxx.OLB
$!  Library.
$!
$   SIMH_PDP11_LIST = "PDP11_RL,PDP11_RQ,PDP11_TS,PDP11_DZ,PDP11_LP"
$!
$!  Define A Counter And Set It To "0".
$!
$   SIMH_PDP11_COUNTER = 0
$!
$!  Top Of The Loop.
$!
$   NEXT_SIMH_PDP11_NAME:
$!
$!  O.K, Extract The PDP11 File From The File List.
$!
$   SIMH_PDP11_FILE_NAME = F$ELEMENT(SIMH_PDP11_COUNTER,",",SIMH_PDP11_LIST)
$!
$!  Check To See If We Are At The End Of The PDP11 List.
$!
$   IF (SIMH_PDP11_FILE_NAME.EQS.",") THEN GOTO SIMH_PDP11_FILE_DONE
$!
$!  Increment The Counter.
$!
$   SIMH_PDP11_COUNTER = SIMH_PDP11_COUNTER + 1
$!
$!  Create The Source File Name.
$!
$   SIMH_PDP11_SOURCE_FILE = "SYS$DISK:[.PDP11]''SIMH_PDP11_FILE_NAME'.C"
$!
$!  Create The Object File Name.
$!
$   SIMH_PDP11_OBJECT_FILE = "SYS$DISK:[.PDP11]''SIMH_PDP11_FILE_NAME'.OBJ"
$!
$!  Tell The User What We Are Compiling.
$!
$   WRITE SYS$OUTPUT "    ",SIMH_PDP11_SOURCE_FILE
$!
$!  Compile The File.
$!
$   CC 'SIMHLIB_MOD_INCLUDES''SIMHLIB_MOD_DEFINE' -
       /OBJECT='SIMH_PDP11_OBJECT_FILE' 'SIMH_PDP11_SOURCE_FILE'
$!
$!  Add It To The Library.
$!
$   LIBRARY/REPLACE/OBJECT 'SIMHLIB_MOD_LIB_NAME' 'SIMH_PDP11_OBJECT_FILE'
$!
$!  Delete The Object File.
$!
$   DELETE/NOCONFIRM/NOLOG 'SIMH_PDP11_OBJECT_FILE';*
$!
$!  Go Back And Do It Again.
$!
$   GOTO NEXT_SIMH_PDP11_NAME
$!
$!  All Done Compiling.
$!
$   SIMH_PDP11_FILE_DONE:
$!
$! Time To End The VAX Check.
$!
$ ENDIF
$!
$! Display A Blank Line.
$!
$ WRITE SYS$OUTPUT ""
$!
$! Check To See If We Have The SYS$DISK:[.LIB]SIMH-xxx.OLB Library.
$!
$ IF (F$SEARCH("SYS$DISK:[.LIB]SIMH-''MACHINE_TYPE'.OLB").EQS."")
$ THEN
$!
$!  Guess Not, So Build The SYS$DISK:[.LIB]SIMH-xxx.OLB Library.
$!
$   GOSUB BUILD_SIMHLIB
$!
$! End The SYS$DISK:[.LIB]SIMH-xxx.OLB Library Check.
$!
$ ENDIF
$!
$! Check To See If We Have A SYS$DISK:[.BIN] Dierctory To Put The
$! Executable In.
$!
$ IF (F$SEARCH("SYS$DISK:[]BIN.DIR").EQS."")
$ THEN
$!
$!  A SYS$DISK:[.BIN] Directory Dosen't Exist So Tell The User We
$!  Are Going To Create One.
$!
$   WRITE SYS$OUTPUT "Creating SYS$DISK:[.BIN]"
$!
$!  Create The Directory.
$!
$   CREATE/DIRECTORY SYS$DISK:[.BIN]
$!
$! Time To End The SYS$DISK:[.BIN] Directory Check.
$!
$ ENDIF
$!
$! Tell The User What We Building.
$!
$ WRITE SYS$OUTPUT ""
$ WRITE SYS$OUTPUT "Building SYS$DISK:[.BIN]''SIMH_MOD_NAME'-''MACHINE_TYPE'.EXE"
$!
$! Compile The SYS$DISK:[]SCP.C File.
$!
$ CC 'SIMHLIB_MOD_INCLUDES''SIMHLIB_MOD_DEFINE' -
     /OBJECT=SYS$DISK:[]SCP-'MACHINE_TYPE'.OBJ SYS$DISK:[]SCP.C
$!
$! Link The Simulator.
$!
$ LINK/'DEBUGGER'/'TRACEBACK' -
      /EXE=SYS$DISK:[.BIN]'SIMH_MOD_NAME'-'MACHINE_TYPE'.EXE  -
       SYS$DISK:[]SCP-'MACHINE_TYPE'.OBJ,'SIMHLIB_MOD_LIB_NAME'/LIBRARY, -
       'SIMHLIB_NAME'/LIBRARY
$!
$! Delete The SYS$DISK:[]SCP-xxx.OBJ File.
$!
$ DELETE/NOCONFIRM/NOLOG SYS$DISK:[]SCP*.OBJ*;*
$!
$! Time To Return From Where We Came From.
$!
$ RETURN
$!
$! Build The PDP18B Systems.
$!
$ BUILD_PDP18B_MOD:
$!
$! Define The PDP18B System We Are To Build.
$!
$ SIMH_PDP18B_MODS = "PDP4,PDP7,PDP9,PDP15"
$!
$! Define The Compiler INCLUDES.
$!
$ SIMH_PDP18B_INCLUDE = "/INCLUDE=(SYS$DISK:[],SYS$DISK:[.PDP18B])"
$!
$! Define A Counter And Set It To "0".
$!
$ SIMH_PDP18B_COUNTER = 0
$!
$! Top Of The Loop.
$!
$ NEXT_SIMH_PDP18B_NAME:
$!
$! O.K, Extract The File Module From The File List.
$!
$ SIMH_PDP18B_NAME = F$ELEMENT(SIMH_PDP18B_COUNTER,",",SIMH_PDP18B_MODS)
$!
$! Check To See If We Are At The End Of The PDP18B List.
$!
$ IF (SIMH_PDP18B_NAME.EQS.",") THEN GOTO SIMH_PDP18B_DONE
$!
$! Increment The Counter.
$!
$ SIMH_PDP18B_COUNTER = SIMH_PDP18B_COUNTER + 1
$!
$! Define The Compiler DEFINES.
$!
$ SIMH_PDP18B_DEFINE = "/DEFINE=(""''SIMH_PDP18B_NAME'=1"")"
$!
$! Check To See If We Have A SYS$DISK:[.LIB] Dierctory To Put The
$! Library In.
$!
$ IF (F$SEARCH("SYS$DISK:[]LIB.DIR").EQS."")
$ THEN
$!
$!  A SYS$DISK:[.LIB] Directory Dosen't Exist So Tell The User We
$!  Are Going To Create One.
$!
$   WRITE SYS$OUTPUT "Creating SYS$DISK:[.LIB]"
$!
$!  Create The Directory.
$!
$   CREATE/DIRECTORY SYS$DISK:[.LIB]
$!
$! Time To End The SYS$DISK:[.LIB] Directory Check.
$!
$ ENDIF
$!
$! Create The Module Library Name.
$!
$ SIMH_PDP18B_LIB_NAME = "SYS$DISK:[.LIB]''SIMH_PDP18B_NAME'-" + -
                         "''MACHINE_TYPE'.OLB"
$!
$! Check To See If We Already Have A Library...
$!
$ IF (F$SEARCH(SIMH_PDP18B_LIB_NAME).EQS."")
$ THEN
$!
$!  Guess Not, Create The Library.
$!
$   LIBRARY/CREATE/OBJECT 'SIMH_PDP18B_LIB_NAME'
$!
$! End The Library Check.
$!
$ ENDIF
$!
$! Tell The User What We Are Doing.
$!
$ WRITE SYS$OUTPUT ""
$ WRITE SYS$OUTPUT "Compling The ''SIMH_PDP18B_LIB_NAME' Library."
$!
$! Tell The User What Compile Command We Are Going To Use.
$!
$ WRITE SYS$OUTPUT "Using Compile Command: ",CC,SIMH_PDP18B_DEFINE, -
                    SIMH_PDP18B_INCLUDE
$ WRITE SYS$OUTPUT ""
$!
$! Top Of The File Loop.
$!
$ NEXT_SIMH_PDP18B_FILE:
$!
$! Define The List Of Files We Are Going To Compile.
$!
$ SIMH_PDP18B_FILES = F$ELEMENT(0,";",F$ELEMENT(1,"]",-
                      F$SEARCH("SYS$DISK:[.PDP18B]*.C",1)))
$!
$! Extract The File Name From The File List.
$!
$ SIMH_PDP18B_FILE_NAME = F$ELEMENT(0,".",SIMH_PDP18B_FILES)
$!
$! Check To See If We Are At The End Of The File List.
$!
$ IF (SIMH_PDP18B_FILE_NAME.EQS."]") THEN GOTO SIMH_PDP18B_FILE_DONE

$!
$! Create The Source File Name.
$!
$ SIMH_PDP18B_SOURCE_FILE = "SYS$DISK:[.PDP18B]" + SIMH_PDP18B_FILE_NAME + ".C"
$!
$! Create The Object File Name.
$!
$ SIMH_PDP18B_OBJECT_FILE = "SYS$DISK:[.PDP18B]" + SIMH_PDP18B_FILE_NAME + -
                            ".OBJ"
$!
$! Tell The User What We Are Compiling.
$!
$ WRITE SYS$OUTPUT "	",SIMH_PDP18B_SOURCE_FILE
$!
$! Compile The File.
$!
$ CC 'SIMH_PDP18B_DEFINE''SIMH_PDP18B_INCLUDE' -
    /OBJECT='SIMH_PDP18B_OBJECT_FILE' 'SIMH_PDP18B_SOURCE_FILE'
$!
$! Add It To The Library.
$!
$ LIBRARY/REPLACE/OBJECT 'SIMH_PDP18B_LIB_NAME' 'SIMH_PDP18B_OBJECT_FILE'
$!
$! Delete The Object File.
$!
$ DELETE/NOCONFIRM/NOLOG 'SIMH_PDP18B_OBJECT_FILE';*
$!
$! Go Back And Do It Again.
$!
$ GOTO NEXT_SIMH_PDP18B_FILE
$!
$! All Done Compiling.
$!
$ SIMH_PDP18B_FILE_DONE:
$!
$! Display A Blank Line.
$!
$ WRITE SYS$OUTPUT ""
$!
$! Check To See If We Have The SYS$DISK:[.LIB]SIMH-xxx.OLB Library.
$!
$ IF (F$SEARCH("SYS$DISK:[.LIB]SIMH-''MACHINE_TYPE'.OLB").EQS."")
$ THEN
$!
$!  Guess Not, So Build The SYS$DISK:[.LIB]SIMH-xxx.OLB Library.
$!
$   GOSUB BUILD_SIMHLIB
$!
$! End The SYS$DISK:[.LIB]SIMH-xxx.OLB Library Check.
$!
$ ENDIF
$!
$! Check To See If We Have A SYS$DISK:[.BIN] Dierctory To Put The
$! Executable In.
$!
$ IF (F$SEARCH("SYS$DISK:[]BIN.DIR").EQS."")
$ THEN
$!
$!  A SYS$DISK:[.BIN] Directory Dosen't Exist So Tell The User We
$!  Are Going To Create One.
$!
$   WRITE SYS$OUTPUT "Creating SYS$DISK:[.BIN]"
$!
$!  Create The Directory.
$!
$   CREATE/DIRECTORY SYS$DISK:[.BIN]
$!
$! Time To End The SYS$DISK:[.BIN] Directory Check.
$!
$ ENDIF
$!
$! Tell The User What We Building.
$!
$ WRITE SYS$OUTPUT ""
$ WRITE SYS$OUTPUT "Building SYS$DISK:[.BIN]''SIMH_PDP18B_NAME'-''MACHINE_TYPE'.EXE"
$!
$! Compile The SYS$DISK:[]SCP.C File.
$!
$ CC 'SIMH_PDP18_MOD_INCLUDES''SIMH_PDP18B_MOD_DEFINE' -
     /OBJECT=SYS$DISK:[]SCP-'MACHINE_TYPE'.OBJ SYS$DISK:[]SCP.C
$!
$! Link The Simulator.
$!
$ LINK/'DEBUGGER'/'TRACEBACK' -
      /EXE=SYS$DISK:[.BIN]'SIMH_PDP18B_NAME'-'MACHINE_TYPE'.EXE  -
       SYS$DISK:[]SCP-'MACHINE_TYPE'.OBJ,'SIMH_PDP18B_LIB_NAME'/LIBRARY, -
       'SIMHLIB_NAME'/LIBRARY
$!
$! Delete The SYS$DISK:[]SCP-xxx.OBJ File.
$!
$ DELETE/NOCONFIRM/NOLOG SYS$DISK:[]SCP*.OBJ*;*
$!
$! Go Back And Do The Next PDP18B Module.
$!
$ GOTO NEXT_SIMH_PDP18B_NAME
$!
$! End Of The PDP18B Module List.
$!
$ SIMH_PDP18B_DONE:
$!
$! All Done, Time To Return From Where We Came From.
$!
$ RETURN
$!
$! Check The User's Options.
$!
$ CHECK_OPTIONS:
$!
$! Define A Counter And Set It To "0".
$!
$ SIMH_SIMS_COUNTER = 0
$!
$! Check To See If We Are To "Just Build Everything."
$!
$ IF (P1.EQS."").OR.(P1.EQS."ALL")
$ THEN
$!
$!  P1 Is Blank Or "ALL", So Just Build Everything.
$!
$   BUILDALL = "TRUE"
$!
$! Else
$!
$ ELSE
$!
$!  Top Of The Loop.
$!
$   NEXT_SIMH_SIMS:
$!
$!  O.K, Extract The File Name From The File List.
$!
$   SIMH_SIMS_NAME = F$ELEMENT(SIMH_SIMS_COUNTER,",",SIMH_SIMS)
$!
$!  Check To See If We Are At The End Of The Simulator List.
$!
$   IF (SIMH_SIMS_NAME.EQS.",") THEN GOTO SIMH_SIMS_ERROR
$!
$!  Increment The Counter.
$!
$   SIMH_SIMS_COUNTER = SIMH_SIMS_COUNTER + 1
$!
$!  Check To See If P1 Has A Valid Argument.
$!
$   IF (P1.EQS.SIMH_SIMS_NAME)
$   THEN
$!
$!    A Valid Argument.
$!
$     BUILDALL = P1
$!
$!    Exit This Routine.
$!
$     GOTO SIMH_CHECK_OPT_DONE
$!
$!   Else...
$!
$   ELSE
$!
$!    Go Back And Check Agianst The Next Sim In The List.
$!
$     GOTO NEXT_SIMH_SIMS
$!
$!  Time To End The Valid Argument Check.
$!
$   ENDIF
$!
$! We Don't Know What The User Entered, So Tell Them.
$!
$ SIMH_SIMS_ERROR:
$!
$! Tell The User We Don't Know What They Want.
$!
$ WRITE SYS$OUTPUT ""
$ WRITE SYS$OUTPUT "The Option ",P1," Is Invalid.  The Valid Options Are:"
$ WRITE SYS$OUTPUT ""
$ WRITE SYS$OUTPUT "    ALL	   :  Just Build "Everything".
$ WRITE SYS$OUTPUT "    ALTAIR     :  Just Build The MITS Altair."
$ WRITE SYS$OUTPUT "    ALTAIRZ80  :  Just Build The MITS Altair Z80."
$ WRITE SYS$OUTPUT "    ECLIPSE    :  Just Build The Data General Eclipse."
$ WRITE SYS$OUTPUT "    H316       :  Just Build The Honewell 316/516."
$ WRITE SYS$OUTPUT "    HP2100     :  Just Build The Hewlett-Packard HP-3100."
$ WRITE SYS$OUTPUT "    I1401      :  Just Build The IBM 1401."
$ WRITE SYS$OUTPUT "    IBM1130    :  Just Build The IBM 1130."
$ WRITE SYS$OUTPUT "    INTERDATA  :  Just Build The Interdata 4."
$ WRITE SYS$OUTPUT "    NOVA       :  Just Build The Data General Nova."
$ WRITE SYS$OUTPUT "    PDP1       :  Just Build The DEC PDP-1."
$ WRITE SYS$OUTPUT "    PDP8       :  Just Build The DEC PDP-8."
$ WRITE SYS$OUTPUT "    PDP10      :  Just Build The DEC PDP-10."
$ WRITE SYS$OUTPUT "    PDP11      :  Just Build The DEC PDP-11."
$ WRITE SYS$OUTPUT "    PDP18B     :  Just Build The DEC PDP-4, PDP-7, PDP-9 And PDP-15."
$ WRITE SYS$OUTPUT "    S3         :  Just Build The IBM System 3"
$ WRITE SYS$OUTPUT "    SDS        :  Just Build The SDS System"
$ WRITE SYS$OUTPUT "    VAX        :  Just Build The DEC VAX."
$ WRITE SYS$OUTPUT ""
$!
$!    Time To Exit.
$!
$     EXIT
$!
$! Time To End The BUILDALL Check.
$!
$ ENDIF
$ SIMH_CHECK_OPT_DONE:
$!
$! Check To See If We Are To Link Without Debugger Information.
$!
$ IF ((P2.EQS."").OR.(P2.EQS."NODEBUG"))
$ THEN
$!
$!  P2 Is Either Blank Or "NODEBUG" So Link Without Debugger Information.
$!
$   DEBUGGER  = "NODEBUG"
$!
$!  Check To See If We Are On An AXP Machine.
$!
$   IF (F$GETSYI("CPU").LT.128)
$   THEN
$!
$!    We Are On A VAX Machine So Use The VAX Optimizations.

$     OPTIMIZE = "OPTIMIZE"
$!
$!  Else...
$!
$   ELSE
$!
$!    We Are On A AXP Machine So Use The AXP Optimizations.
$!
$     OPTIMIZE = "OPTIMIZE=(INTRINSICS,INLINE=AUTOMATIC,LEVEL=5,UNROLL=0,TUNE=HOST)/ARCH=HOST"
$!
$!  Time To End The Machine Check.
$!
$   ENDIF
$!
$!  Set The Link TRACEBACK Option.
$!
$   TRACEBACK = "NOTRACEBACK"
$!
$!  Tell The User What They Selected.
$!
$   WRITE SYS$OUTPUT ""
$   WRITE SYS$OUTPUT "Runtime Debugger Won't Be Included At Link."
$!
$! Else...
$!
$ ELSE
$!
$!  Check To See If We Are To Link With Debugger Information.
$!
$   IF (P2.EQS."DEBUG")
$   THEN
$!
$!    Compile With Debugger Information.
$!
$     DEBUGGER  = "DEBUG"
$     OPTIMIZE = "NOOPTIMIZE"
$     TRACEBACK = "TRACEBACK"
$!
$!    Tell The User What They Selected.
$!
$     WRITE SYS$OUTPUT ""
$     WRITE SYS$OUTPUT "Runtime Debugger Will Be Included At Link."
$!
$!  Else...
$!
$   ELSE
$!
$!    Tell The User Entered An Invalid Option..
$!
$     WRITE SYS$OUTPUT ""
$     WRITE SYS$OUTPUT "The Option ",P2," Is Invalid.  The Valid Options Are:"
$     WRITE SYS$OUTPUT ""
$     WRITE SYS$OUTPUT "    DEBUG    :  Link With The Debugger Information."
$     WRITE SYS$OUTPUT "    NODEBUG  :  Link Without The Debugger Information."
$     WRITE SYS$OUTPUT ""
$!
$!    Time To EXIT.
$!
$     EXIT
$!
$!  Time To End The Valid P2 Check.
$!
$   ENDIF
$!
$! Time To End The P2 Check.
$!
$ ENDIF
$!
$! Time To Return To Where We Were.
$!
$ RETURN
