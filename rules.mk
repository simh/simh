
all : ${ALL}

clean :
ifneq ($(WIN32),1)
	${RM} -r ${BIN}
else
	if exist BIN\*.exe del /q BIN\*.exe
	if exist BIN rmdir BIN
endif

ifeq ($(WIN32),1)
${BIN}BuildROMs.exe:
else
${BIN}BuildROMs:
endif
	${MKDIRBIN}
ifeq (agcc,$(findstring agcc,$(firstword $(CC))))
	${HOST_CC} $(wordlist 2,1000,${CC}) sim_BuildROMs.c $(CC_OUTSPEC)
else
	${HOST_CC} sim_BuildROMs.c $(CC_OUTSPEC)
endif
ifneq ($(WIN32),1)
	$@
	${RM} $@
  ifeq (Darwin,$(OSTYPE)) # remove Xcode's debugging symbols folder too
	${RM} -rf $@.dSYM
  endif
else
	$(@D)\$(@F)
	del $(@D)\$(@F)
endif

#
# Individual builds
#
pdp1 : ${BIN}pdp1${EXE}

${BIN}pdp1${EXE} : ${PDP1} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP1} ${SIM} ${PDP1_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp4 : ${BIN}pdp4${EXE}

${BIN}pdp4${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP4_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp7 : ${BIN}pdp7${EXE}

${BIN}pdp7${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP7_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp8 : ${BIN}pdp8${EXE}

${BIN}pdp8${EXE} : ${PDP8} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP8} ${SIM} ${PDP8_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp9 : ${BIN}pdp9${EXE}

${BIN}pdp9${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP9_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp15 : ${BIN}pdp15${EXE}

${BIN}pdp15${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP15_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp10 : ${BIN}pdp10${EXE}

${BIN}pdp10${EXE} : ${PDP10} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP10} ${SIM} ${PDP10_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp11 : ${BIN}pdp11${EXE}

${BIN}pdp11${EXE} : ${PDP11} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP11} ${SIM} ${PDP11_OPT} $(CC_OUTSPEC) ${LDFLAGS}

vax : microvax3900

microvax3900 : ${BIN}microvax3900${EXE}

${BIN}microvax3900${EXE} : ${VAX} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX} ${SIM} ${VAX_OPT} $(CC_OUTSPEC) ${LDFLAGS}
ifneq ($(WIN32),1)
	cp ${BIN}microvax3900${EXE} ${BIN}vax${EXE}
else
	copy $(@D)\microvax3900${EXE} $(@D)\vax${EXE}
endif

microvax1 : ${BIN}microvax1${EXE}

${BIN}microvax1${EXE} : ${VAX610} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX610} ${SIM} ${VAX610_OPT} -o $@ ${LDFLAGS}

rtvax1000 : ${BIN}rtvax1000${EXE}

${BIN}rtvax1000${EXE} : ${VAX630} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX630} ${SIM} ${VAX620_OPT} -o $@ ${LDFLAGS}

microvax2 : ${BIN}microvax2${EXE}

${BIN}microvax2${EXE} : ${VAX630} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX630} ${SIM} ${VAX630_OPT} -o $@ ${LDFLAGS}

vax730 : ${BIN}vax730${EXE}

${BIN}vax730${EXE} : ${VAX730} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX730} ${SIM} ${VAX730_OPT} -o $@ ${LDFLAGS}

vax750 : ${BIN}vax750${EXE}

${BIN}vax750${EXE} : ${VAX750} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX750} ${SIM} ${VAX750_OPT} -o $@ ${LDFLAGS}

vax780 : ${BIN}vax780${EXE}

${BIN}vax780${EXE} : ${VAX780} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX780} ${SIM} ${VAX780_OPT} $(CC_OUTSPEC) ${LDFLAGS}

vax8600 : ${BIN}vax8600${EXE}

${BIN}vax8600${EXE} : ${VAX8600} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX8600} ${SIM} ${VAX8600_OPT} $(CC_OUTSPEC) ${LDFLAGS}

nova : ${BIN}nova${EXE}

${BIN}nova${EXE} : ${NOVA} ${SIM}
	${MKDIRBIN}
	${CC} ${NOVA} ${SIM} ${NOVA_OPT} $(CC_OUTSPEC) ${LDFLAGS}

eclipse : ${BIN}eclipse${EXE}

${BIN}eclipse${EXE} : ${ECLIPSE} ${SIM}
	${MKDIRBIN}
	${CC} ${ECLIPSE} ${SIM} ${ECLIPSE_OPT} $(CC_OUTSPEC) ${LDFLAGS}

h316 : ${BIN}h316${EXE}

${BIN}h316${EXE} : ${H316} ${SIM}
	${MKDIRBIN}
	${CC} ${H316} ${SIM} ${H316_OPT} $(CC_OUTSPEC) ${LDFLAGS}

hp2100 : ${BIN}hp2100${EXE}

${BIN}hp2100${EXE} : ${HP2100} ${SIM}
	${MKDIRBIN}
	${CC} ${HP2100} ${SIM} ${HP2100_OPT} $(CC_OUTSPEC) ${LDFLAGS}

i1401 : ${BIN}i1401${EXE}

${BIN}i1401${EXE} : ${I1401} ${SIM}
	${MKDIRBIN}
	${CC} ${I1401} ${SIM} ${I1401_OPT} $(CC_OUTSPEC) ${LDFLAGS}

i1620 : ${BIN}i1620${EXE}

${BIN}i1620${EXE} : ${I1620} ${SIM}
	${MKDIRBIN}
	${CC} ${I1620} ${SIM} ${I1620_OPT} $(CC_OUTSPEC) ${LDFLAGS}

i7094 : ${BIN}i7094${EXE}

${BIN}i7094${EXE} : ${I7094} ${SIM}
	${MKDIRBIN}
	${CC} ${I7094} ${SIM} ${I7094_OPT} $(CC_OUTSPEC) ${LDFLAGS}

ibm1130 : ${BIN}ibm1130${EXE}

${BIN}ibm1130${EXE} : ${IBM1130}
	${MKDIRBIN}
ifneq ($(WIN32),)
	windres ${IBM1130D}/ibm1130.rc $(BIN)ibm1130.o
	${CC} ${IBM1130} ${SIM} ${IBM1130_OPT} $(BIN)ibm1130.o $(CC_OUTSPEC) ${LDFLAGS}
 ifeq ($(WIN32),1)
	del BIN\ibm1130.o
 else
	rm BIN/ibm1130.o
 endif
else
	${CC} ${IBM1130} ${SIM} ${IBM1130_OPT} $(CC_OUTSPEC) ${LDFLAGS}
endif  

s3 : ${BIN}s3${EXE}

${BIN}s3${EXE} : ${S3} ${SIM}
	${MKDIRBIN}
	${CC} ${S3} ${SIM} ${S3_OPT} $(CC_OUTSPEC) ${LDFLAGS}

altair : ${BIN}altair${EXE}

${BIN}altair${EXE} : ${ALTAIR} ${SIM}
	${MKDIRBIN}
	${CC} ${ALTAIR} ${SIM} ${ALTAIR_OPT} $(CC_OUTSPEC) ${LDFLAGS}

altairz80 : ${BIN}altairz80${EXE}

${BIN}altairz80${EXE} : ${ALTAIRZ80} ${SIM}
	${MKDIRBIN}
	${CC} ${ALTAIRZ80} ${SIM} ${ALTAIRZ80_OPT} $(CC_OUTSPEC) ${LDFLAGS}

gri : ${BIN}gri${EXE}

${BIN}gri${EXE} : ${GRI} ${SIM}
	${MKDIRBIN}
	${CC} ${GRI} ${SIM} ${GRI_OPT} $(CC_OUTSPEC) ${LDFLAGS}

lgp : ${BIN}lgp${EXE}

${BIN}lgp${EXE} : ${LGP} ${SIM}
	${MKDIRBIN}
	${CC} ${LGP} ${SIM} ${LGP_OPT} $(CC_OUTSPEC) ${LDFLAGS}

id16 : ${BIN}id16${EXE}

${BIN}id16${EXE} : ${ID16} ${SIM}
	${MKDIRBIN}
	${CC} ${ID16} ${SIM} ${ID16_OPT} $(CC_OUTSPEC) ${LDFLAGS}

id32 : ${BIN}id32${EXE}

${BIN}id32${EXE} : ${ID32} ${SIM}
	${MKDIRBIN}
	${CC} ${ID32} ${SIM} ${ID32_OPT} $(CC_OUTSPEC) ${LDFLAGS}

sds : ${BIN}sds${EXE}

${BIN}sds${EXE} : ${SDS} ${SIM}
	${MKDIRBIN}
	${CC} ${SDS} ${SIM} ${SDS_OPT} $(CC_OUTSPEC) ${LDFLAGS}

swtp6800mp-a : ${BIN}swtp6800mp-a${EXE}

${BIN}swtp6800mp-a${EXE} : ${SWTP6800MP-A} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${SWTP6800MP-A} ${SIM} ${SWTP6800_OPT} $(CC_OUTSPEC) ${LDFLAGS}

swtp6800mp-a2 : ${BIN}swtp6800mp-a2${EXE}

${BIN}swtp6800mp-a2${EXE} : ${SWTP6800MP-A2} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${SWTP6800MP-A2} ${SIM} ${SWTP6800_OPT} $(CC_OUTSPEC) ${LDFLAGS}

tx-0 : ${BIN}tx-0${EXE}

${BIN}tx-0${EXE} : ${TX0} ${SIM}
	${MKDIRBIN}
	${CC} ${TX0} ${SIM} ${TX0_OPT} $(CC_OUTSPEC) ${LDFLAGS}

ssem : ${BIN}ssem${EXE}

${BIN}ssem${EXE} : ${SSEM} ${SIM}
	${MKDIRBIN}
	${CC} ${SSEM} ${SIM} ${SSEM_OPT} $(CC_OUTSPEC) ${LDFLAGS}

besm6 : ${BIN}besm6${EXE}

${BIN}besm6${EXE} : ${BESM6} ${SIM}
	${MKDIRBIN}
	${CC} ${BESM6} ${SIM} ${BESM6_OPT} $(CC_OUTSPEC) ${LDFLAGS}

sigma : ${BIN}sigma${EXE}

${BIN}sigma${EXE} : ${SIGMA} ${SIM}
	${MKDIRBIN}
	${CC} ${SIGMA} ${SIM} ${SIGMA_OPT} $(CC_OUTSPEC) ${LDFLAGS}

alpha : ${BIN}alpha${EXE}

${BIN}alpha${EXE} : ${ALPHA} ${SIM}
	${MKDIRBIN}
	${CC} ${ALPHA} ${SIM} ${ALPHA_OPT} $(CC_OUTSPEC) ${LDFLAGS}

sage : ${BIN}sage${EXE}

${BIN}sage${EXE} : ${SAGE} ${SIM}
	${MKDIRBIN}
	${CC} ${SAGE} ${SIM} ${SAGE_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdq3 : ${BIN}pdq3${EXE}

${BIN}pdq3${EXE} : ${PDQ3} ${SIM}
	${MKDIRBIN}
	${CC} ${PDQ3} ${SIM} ${PDQ3_OPT} $(CC_OUTSPEC) ${LDFLAGS}

# Front Panel API Demo/Test program

frontpaneltest : ${BIN}frontpaneltest${EXE}

${BIN}frontpaneltest${EXE} : frontpanel/FrontPanelTest.c sim_sock.c sim_frontpanel.c
	${MKDIRBIN}
	${CC} frontpanel/FrontPanelTest.c sim_sock.c sim_frontpanel.c $(CC_OUTSPEC) ${LDFLAGS}

