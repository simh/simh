#
# CC Command
#
ifeq ($(WIN32),)
  #Unix Environments
  ifneq (,$(findstring solaris,$(OSTYPE)))
    OS_CCDEFS = -lm -lsocket -lnsl -lrt -lpthread -D_GNU_SOURCE
  else
    ifneq (,$(findstring darwin,$(OSTYPE)))
      OS_CCDEFS = -D_GNU_SOURCE
    else
      OS_CCDEFS = -lrt -lm -D_GNU_SOURCE
    endif
  endif
  CC = gcc -std=c99 -U__STRICT_ANSI__ -g $(OS_CCDEFS) -I .
  ifeq ($(USE_NETWORK),)
  else
    NETWORK_OPT = -DUSE_NETWORK -isystem /usr/local/include /usr/local/lib/libpcap.a
  endif
else
  #Win32 Environments
  LDFLAGS = -lm -lwsock32 -lwinmm
  CC = gcc -std=c99 -U__STRICT_ANSI__ -O2 -I.
  EXE = .exe
  ifeq ($(USE_NETWORK),)
  else
    NETWORK_OPT = -DUSE_NETWORK -lwpcap -lpacket
  endif
endif

#
# Common Libraries
#
BIN = BIN/
SIM = scp.c sim_console.c sim_fio.c sim_timer.c sim_sock.c \
	sim_tmxr.c sim_ether.c sim_tape.c


#
# Emulator source files and compile time options
#
PDP1D = PDP1
PDP1 = ${PDP1D}/pdp1_lp.c ${PDP1D}/pdp1_cpu.c ${PDP1D}/pdp1_stddev.c \
	${PDP1D}/pdp1_sys.c ${PDP1D}/pdp1_dt.c ${PDP1D}/pdp1_drm.c \
	${PDP1D}/pdp1_clk.c ${PDP1D}/pdp1_dcs.c
PDP1_OPT = -I ${PDP1D}


NOVAD = NOVA
NOVA = ${NOVAD}/nova_sys.c ${NOVAD}/nova_cpu.c ${NOVAD}/nova_dkp.c \
	${NOVAD}/nova_dsk.c ${NOVAD}/nova_lp.c ${NOVAD}/nova_mta.c \
	${NOVAD}/nova_plt.c ${NOVAD}/nova_pt.c ${NOVAD}/nova_clk.c \
	${NOVAD}/nova_tt.c ${NOVAD}/nova_tt1.c ${NOVAD}/nova_qty.c
NOVA_OPT = -I ${NOVAD}


ECLIPSE = ${NOVAD}/eclipse_cpu.c ${NOVAD}/eclipse_tt.c ${NOVAD}/nova_sys.c \
	${NOVAD}/nova_dkp.c ${NOVAD}/nova_dsk.c ${NOVAD}/nova_lp.c \
	${NOVAD}/nova_mta.c ${NOVAD}/nova_plt.c ${NOVAD}/nova_pt.c \
	${NOVAD}/nova_clk.c ${NOVAD}/nova_tt1.c ${NOVAD}/nova_qty.c
ECLIPSE_OPT = -I ${NOVAD} -DECLIPSE -DUSE_INT64 


PDP18BD = PDP18B
PDP18B = ${PDP18BD}/pdp18b_dt.c ${PDP18BD}/pdp18b_drm.c ${PDP18BD}/pdp18b_cpu.c \
	${PDP18BD}/pdp18b_lp.c ${PDP18BD}/pdp18b_mt.c ${PDP18BD}/pdp18b_rf.c \
	${PDP18BD}/pdp18b_rp.c ${PDP18BD}/pdp18b_stddev.c ${PDP18BD}/pdp18b_sys.c \
	${PDP18BD}/pdp18b_rb.c ${PDP18BD}/pdp18b_tt1.c ${PDP18BD}/pdp18b_fpp.c
PDP4_OPT = -DPDP4 -I ${PDP18BD}
PDP7_OPT = -DPDP7 -I ${PDP18BD}
PDP9_OPT = -DPDP9 -I ${PDP18BD}
PDP15_OPT = -DPDP15 -I ${PDP18BD}


PDP11D = PDP11
PDP11 = ${PDP11D}/pdp11_fp.c ${PDP11D}/pdp11_cpu.c ${PDP11D}/pdp11_dz.c \
	${PDP11D}/pdp11_cis.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_rk.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_rx.c \
	${PDP11D}/pdp11_stddev.c ${PDP11D}/pdp11_sys.c ${PDP11D}/pdp11_tc.c \
	${PDP11D}/pdp11_tm.c ${PDP11D}/pdp11_ts.c ${PDP11D}/pdp11_io.c \
	${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_tq.c ${PDP11D}/pdp11_pclk.c \
	${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_pt.c ${PDP11D}/pdp11_hk.c \
	${PDP11D}/pdp11_xq.c ${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_vh.c \
	${PDP11D}/pdp11_rh.c ${PDP11D}/pdp11_tu.c ${PDP11D}/pdp11_cpumod.c \
	${PDP11D}/pdp11_cr.c ${PDP11D}/pdp11_rf.c ${PDP11D}/pdp11_dl.c \
	${PDP11D}/pdp11_ta.c ${PDP11D}/pdp11_rc.c ${PDP11D}/pdp11_kg.c \
	${PDP11D}/pdp11_ke.c ${PDP11D}/pdp11_dc.c ${PDP11D}/pdp11_io_lib.c
PDP11_OPT = -DVM_PDP11 -I ${PDP11D} ${NETWORK_OPT}


VAXD = VAX
VAX = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c ${VAXD}/vax_io.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_stddev.c ${VAXD}/vax_sysdev.c \
	${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c ${VAXD}/vax_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xq.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_vh.c \
	${PDP11D}/pdp11_cr.c ${PDP11D}/pdp11_io_lib.c
VAX_OPT = -DVM_VAX -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT}


VAX780 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax780_stddev.c ${VAXD}/vax780_sbi.c \
	${VAXD}/vax780_mem.c ${VAXD}/vax780_uba.c ${VAXD}/vax780_mba.c \
	${VAXD}/vax780_fload.c ${VAXD}/vax780_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_tu.c ${PDP11D}/pdp11_hk.c \
	${PDP11D}/pdp11_io_lib.c
VAX780_OPT = -DVM_VAX -DVAX_780 -DUSE_INT64 -DUSE_ADDR64 -I VAX -I ${PDP11D} ${NETWORK_OPT}


PDP10D = PDP10
PDP10 = ${PDP10D}/pdp10_fe.c ${PDP11D}/pdp11_dz.c ${PDP10D}/pdp10_cpu.c \
	${PDP10D}/pdp10_ksio.c ${PDP10D}/pdp10_lp20.c ${PDP10D}/pdp10_mdfp.c \
	${PDP10D}/pdp10_pag.c ${PDP10D}/pdp10_rp.c ${PDP10D}/pdp10_sys.c \
	${PDP10D}/pdp10_tim.c ${PDP10D}/pdp10_tu.c ${PDP10D}/pdp10_xtnd.c \
	${PDP11D}/pdp11_pt.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_xu.c \
	${PDP11D}/pdp11_cr.c
PDP10_OPT = -DVM_PDP10 -DUSE_INT64 -I ${PDP10D} -I ${PDP11D} ${NETWORK_OPT}



PDP8D = PDP8
PDP8 = ${PDP8D}/pdp8_cpu.c ${PDP8D}/pdp8_clk.c ${PDP8D}/pdp8_df.c \
	${PDP8D}/pdp8_dt.c ${PDP8D}/pdp8_lp.c ${PDP8D}/pdp8_mt.c \
	${PDP8D}/pdp8_pt.c ${PDP8D}/pdp8_rf.c ${PDP8D}/pdp8_rk.c \
	${PDP8D}/pdp8_rx.c ${PDP8D}/pdp8_sys.c ${PDP8D}/pdp8_tt.c \
	${PDP8D}/pdp8_ttx.c ${PDP8D}/pdp8_rl.c ${PDP8D}/pdp8_tsc.c \
	${PDP8D}/pdp8_td.c ${PDP8D}/pdp8_ct.c ${PDP8D}/pdp8_fpp.c
PDP8_OPT = -I ${PDP8D}


H316D = H316
H316 = ${H316D}/h316_stddev.c ${H316D}/h316_lp.c ${H316D}/h316_cpu.c \
	${H316D}/h316_sys.c ${H316D}/h316_mt.c ${H316D}/h316_fhd.c \
	${H316D}/h316_dp.c
H316_OPT = -I ${H316D}


HP2100D = HP2100
HP2100 = ${HP2100D}/hp2100_stddev.c ${HP2100D}/hp2100_dp.c ${HP2100D}/hp2100_dq.c \
	${HP2100D}/hp2100_dr.c ${HP2100D}/hp2100_lps.c ${HP2100D}/hp2100_ms.c \
	${HP2100D}/hp2100_mt.c ${HP2100D}/hp2100_mux.c ${HP2100D}/hp2100_cpu.c \
	${HP2100D}/hp2100_fp.c ${HP2100D}/hp2100_sys.c ${HP2100D}/hp2100_lpt.c \
	${HP2100D}/hp2100_ipl.c ${HP2100D}/hp2100_ds.c ${HP2100D}/hp2100_cpu0.c \
	${HP2100D}/hp2100_cpu1.c ${HP2100D}/hp2100_cpu2.c ${HP2100D}/hp2100_cpu3.c \
	${HP2100D}/hp2100_cpu4.c ${HP2100D}/hp2100_cpu5.c ${HP2100D}/hp2100_cpu6.c \
	${HP2100D}/hp2100_cpu7.c ${HP2100D}/hp2100_fp1.c ${HP2100D}/hp2100_baci.c \
	${HP2100D}/hp2100_mpx.c ${HP2100D}/hp2100_pif.c
HP2100_OPT = -DHAVE_INT64 -I ${HP2100D}


I1401D = I1401
I1401 = ${I1401D}/i1401_lp.c ${I1401D}/i1401_cpu.c ${I1401D}/i1401_iq.c \
	${I1401D}/i1401_cd.c ${I1401D}/i1401_mt.c ${I1401D}/i1401_dp.c \
	${I1401D}/i1401_sys.c
I1401_OPT = -I ${I1401D}


I1620D = I1620
I1620 = ${I1620D}/i1620_cd.c ${I1620D}/i1620_dp.c ${I1620D}/i1620_pt.c \
	${I1620D}/i1620_tty.c ${I1620D}/i1620_cpu.c ${I1620D}/i1620_lp.c \
	${I1620D}/i1620_fp.c ${I1620D}/i1620_sys.c
I1620_OPT = -I ${I1620D}


I7094D = I7094
I7094 = ${I7094D}/i7094_cpu.c ${I7094D}/i7094_cpu1.c ${I7094D}/i7094_io.c \
	${I7094D}/i7094_cd.c ${I7094D}/i7094_clk.c ${I7094D}/i7094_com.c \
	${I7094D}/i7094_drm.c ${I7094D}/i7094_dsk.c ${I7094D}/i7094_sys.c \
	${I7094D}/i7094_lp.c ${I7094D}/i7094_mt.c ${I7094D}/i7094_binloader.c
I7094_OPT = -DUSE_INT64 -I ${I7094D}


IBM1130D = Ibm1130
IBM1130 = ${IBM1130D}/ibm1130_cpu.c ${IBM1130D}/ibm1130_cr.c \
	${IBM1130D}/ibm1130_disk.c ${IBM1130D}/ibm1130_stddev.c \
	${IBM1130D}/ibm1130_sys.c ${IBM1130D}/ibm1130_gdu.c \
	${IBM1130D}/ibm1130_gui.c ${IBM1130D}/ibm1130_prt.c \
	${IBM1130D}/ibm1130_fmt.c ${IBM1130D}/ibm1130_ptrp.c \
	${IBM1130D}/ibm1130_plot.c ${IBM1130D}/ibm1130_sca.c \
	${IBM1130D}/ibm1130_t2741.c
IBM1130_OPT = -I ${IBM1130D}


ID16D = Interdata
ID16 = ${ID16D}/id16_cpu.c ${ID16D}/id16_sys.c ${ID16D}/id_dp.c \
	${ID16D}/id_fd.c ${ID16D}/id_fp.c ${ID16D}/id_idc.c ${ID16D}/id_io.c \
	${ID16D}/id_lp.c ${ID16D}/id_mt.c ${ID16D}/id_pas.c ${ID16D}/id_pt.c \
	${ID16D}/id_tt.c ${ID16D}/id_uvc.c ${ID16D}/id16_dboot.c ${ID16D}/id_ttp.c
ID16_OPT = -I ${ID16D}


ID32D = Interdata
ID32 = ${ID32D}/id32_cpu.c ${ID32D}/id32_sys.c ${ID32D}/id_dp.c \
	${ID32D}/id_fd.c ${ID32D}/id_fp.c ${ID32D}/id_idc.c ${ID32D}/id_io.c \
	${ID32D}/id_lp.c ${ID32D}/id_mt.c ${ID32D}/id_pas.c ${ID32D}/id_pt.c \
	${ID32D}/id_tt.c ${ID32D}/id_uvc.c ${ID32D}/id32_dboot.c ${ID32D}/id_ttp.c
ID32_OPT = -I ${ID32D}


S3D = S3
S3 = ${S3D}/s3_cd.c ${S3D}/s3_cpu.c ${S3D}/s3_disk.c ${S3D}/s3_lp.c \
	${S3D}/s3_pkb.c ${S3D}/s3_sys.c
S3_OPT = -I ${S3D}


ALTAIRD = ALTAIR
ALTAIR = ${ALTAIRD}/altair_sio.c ${ALTAIRD}/altair_cpu.c ${ALTAIRD}/altair_dsk.c \
	${ALTAIRD}/altair_sys.c
ALTAIR_OPT = -I ${ALTAIRD}


ALTAIRZ80D = AltairZ80
ALTAIRZ80 = ${ALTAIRZ80D}/altairz80_cpu.c ${ALTAIRZ80D}/altairz80_cpu_nommu.c \
	${ALTAIRZ80D}/altairz80_dsk.c ${ALTAIRZ80D}/disasm.c \
	${ALTAIRZ80D}/altairz80_sio.c ${ALTAIRZ80D}/altairz80_sys.c \
	${ALTAIRZ80D}/altairz80_hdsk.c ${ALTAIRZ80D}/altairz80_net.c \
	${ALTAIRZ80D}/flashwriter2.c ${ALTAIRZ80D}/i86_decode.c \
	${ALTAIRZ80D}/i86_ops.c ${ALTAIRZ80D}/i86_prim_ops.c \
	${ALTAIRZ80D}/i8272.c ${ALTAIRZ80D}/insnsa.c ${ALTAIRZ80D}/insnsd.c \
	${ALTAIRZ80D}/mfdc.c ${ALTAIRZ80D}/n8vem.c ${ALTAIRZ80D}/vfdhd.c \
	${ALTAIRZ80D}/s100_disk1a.c ${ALTAIRZ80D}/s100_disk2.c ${ALTAIRZ80D}/s100_disk3.c\
	${ALTAIRZ80D}/s100_fif.c ${ALTAIRZ80D}/s100_mdriveh.c \
	${ALTAIRZ80D}/s100_mdsad.c ${ALTAIRZ80D}/s100_selchan.c \
	${ALTAIRZ80D}/s100_ss1.c ${ALTAIRZ80D}/s100_64fdc.c \
	${ALTAIRZ80D}/s100_scp300f.c ${ALTAIRZ80D}/sim_imd.c \
	${ALTAIRZ80D}/wd179x.c ${ALTAIRZ80D}/s100_hdc1001.c \
	${ALTAIRZ80D}/s100_if3.c ${ALTAIRZ80D}/s100_adcs6.c
ALTAIRZ80_OPT = -I ${ALTAIRZ80D}


GRID = GRI
GRI = ${GRID}/gri_cpu.c ${GRID}/gri_stddev.c ${GRID}/gri_sys.c
GRI_OPT = -I ${GRID}


LGPD = LGP
LGP = ${LGPD}/lgp_cpu.c ${LGPD}/lgp_stddev.c ${LGPD}/lgp_sys.c
LGP_OPT = -I ${LGPD}


SDSD = SDS
SDS = ${SDSD}/sds_cpu.c ${SDSD}/sds_drm.c ${SDSD}/sds_dsk.c ${SDSD}/sds_io.c \
	${SDSD}/sds_lp.c ${SDSD}/sds_mt.c ${SDSD}/sds_mux.c ${SDSD}/sds_rad.c \
	${SDSD}/sds_stddev.c ${SDSD}/sds_sys.c
SDS_OPT = -I ${SDSD}

#
# Build everything
#
ALL = pdp1 pdp4 pdp7 pdp8 pdp9 pdp15 pdp11 pdp10 \
	vax vax780 nova eclipse hp2100 i1401 i1620 s3 \
	altair altairz80 gri i1620 i7094 ibm1130 id16 \
	id32 sds lgp h316 

all : ${ALL}

clean :
ifeq ($(WIN32),)
	${RM} ${BIN}*
else
	if exist BIN\*.exe del /q BIN\*.exe
endif

#
# Individual builds
#
pdp1 : ${BIN}pdp1${EXE}

${BIN}pdp1${EXE} : ${PDP1} ${SIM}
	${CC} ${PDP1} ${SIM} ${PDP1_OPT} -o $@ ${LDFLAGS}

pdp4 : ${BIN}pdp4${EXE}

${BIN}pdp4${EXE} : ${PDP18B} ${SIM}
	${CC} ${PDP18B} ${SIM} ${PDP4_OPT} -o $@ ${LDFLAGS}

pdp7 : ${BIN}pdp7${EXE}

${BIN}pdp7${EXE} : ${PDP18B} ${SIM}
	${CC} ${PDP18B} ${SIM} ${PDP7_OPT} -o $@ ${LDFLAGS}

pdp8 : ${BIN}pdp8${EXE}

${BIN}pdp8${EXE} : ${PDP8} ${SIM}
	${CC} ${PDP8} ${SIM} ${PDP8_OPT} -o $@ ${LDFLAGS}

pdp9 : ${BIN}pdp9${EXE}

${BIN}pdp9${EXE} : ${PDP18B} ${SIM}
	${CC} ${PDP18B} ${SIM} ${PDP9_OPT} -o $@ ${LDFLAGS}

pdp15 : ${BIN}pdp15${EXE}

${BIN}pdp15${EXE} : ${PDP18B} ${SIM}
	${CC} ${PDP18B} ${SIM} ${PDP15_OPT} -o $@ ${LDFLAGS}

pdp10 : ${BIN}pdp10${EXE}

${BIN}pdp10${EXE} : ${PDP10} ${SIM}
	${CC} ${PDP10} ${SIM} ${PDP10_OPT} -o $@ ${LDFLAGS}

pdp11 : ${BIN}pdp11${EXE}

${BIN}pdp11${EXE} : ${PDP11} ${SIM}
	${CC} ${PDP11} ${SIM} ${PDP11_OPT} -o $@ ${LDFLAGS}

vax : ${BIN}vax${EXE}

${BIN}vax${EXE} : ${VAX} ${SIM}
	${CC} ${VAX} ${SIM} ${VAX_OPT} -o $@ ${LDFLAGS}

vax780 : ${BIN}vax780${EXE}

${BIN}vax780${EXE} : ${VAX780} ${SIM}
	${CC} ${VAX780} ${SIM} ${VAX780_OPT} -o $@ ${LDFLAGS}

nova : ${BIN}nova${EXE}

${BIN}nova${EXE} : ${NOVA} ${SIM}
	${CC} ${NOVA} ${SIM} ${NOVA_OPT} -o $@ ${LDFLAGS}

eclipse : ${BIN}eclipse${EXE}

${BIN}eclipse${EXE} : ${ECLIPSE} ${SIM}
	${CC} ${ECLIPSE} ${SIM} ${ECLIPSE_OPT} -o $@ ${LDFLAGS}

h316 : ${BIN}h316${EXE}

${BIN}h316${EXE} : ${H316} ${SIM}
	${CC} ${H316} ${SIM} ${H316_OPT} -o $@ ${LDFLAGS}

hp2100 : ${BIN}hp2100${EXE}

${BIN}hp2100${EXE} : ${HP2100} ${SIM}
	${CC} ${HP2100} ${SIM} ${HP2100_OPT} -o $@ ${LDFLAGS}

i1401 : ${BIN}i1401${EXE}

${BIN}i1401${EXE} : ${I1401} ${SIM}
	${CC} ${I1401} ${SIM} ${I1401_OPT} -o $@ ${LDFLAGS}

i1620 : ${BIN}i1620${EXE}

${BIN}i1620${EXE} : ${I1620} ${SIM}
	${CC} ${I1620} ${SIM} ${I1620_OPT} -o $@ ${LDFLAGS}

i7094 : ${BIN}i7094${EXE}

${BIN}i7094${EXE} : ${I7094} ${SIM}
	${CC} ${I7094} ${SIM} ${I7094_OPT} -o $@ ${LDFLAGS}

ibm1130 : ${BIN}ibm1130${EXE}

${BIN}ibm1130${EXE} : ${IBM1130}
	${CC} ${IBM1130} ${SIM} ${IBM1130_OPT} -o $@ ${LDFLAGS}

s3 : ${BIN}s3${EXE}

${BIN}s3${EXE} : ${S3} ${SIM}
	${CC} ${S3} ${SIM} ${S3_OPT} -o $@ ${LDFLAGS}

altair : ${BIN}altair${EXE}

${BIN}altair${EXE} : ${ALTAIR} ${SIM}
	${CC} ${ALTAIR} ${SIM} ${ALTAIR_OPT} -o $@ ${LDFLAGS}

altairz80 : ${BIN}altairz80${EXE}

${BIN}altairz80${EXE} : ${ALTAIRZ80} ${SIM} 
	${CC} ${ALTAIRZ80} ${SIM} ${ALTAIRZ80_OPT} -o $@ ${LDFLAGS}

gri : ${BIN}gri${EXE}

${BIN}gri${EXE} : ${GRI} ${SIM}
	${CC} ${GRI} ${SIM} ${GRI_OPT} -o $@ ${LDFLAGS}

lgp : ${BIN}lgp${EXE}

${BIN}lgp${EXE} : ${LGP} ${SIM}
	${CC} ${LGP} ${SIM} ${LGP_OPT} -o $@ ${LDFLAGS}

id16 : ${BIN}id16${EXE}

${BIN}id16${EXE} : ${ID16} ${SIM}
	${CC} ${ID16} ${SIM} ${ID16_OPT} -o $@ ${LDFLAGS}

id32 : ${BIN}id32${EXE}

${BIN}id32${EXE} : ${ID32} ${SIM}
	${CC} ${ID32} ${SIM} ${ID32_OPT} -o $@ ${LDFLAGS}

sds : ${BIN}sds${EXE}

${BIN}sds${EXE} : ${SDS} ${SIM}
	${CC} ${SDS} ${SIM} ${SDS_OPT} -o $@ ${LDFLAGS}
