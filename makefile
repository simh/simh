# CC Command
#
CC = gcc -O2 -lm -I .
#CC = gcc -O0 -g -lm -I .



#
# Common Libraries
#
BIN = BIN/
SIM = scp.c scp_tty.c sim_sock.c sim_tmxr.c



#
# Emulator source files and compile time options
#
PDP1D = PDP1/
PDP1 = ${PDP1D}pdp1_lp.c ${PDP1D}pdp1_cpu.c ${PDP1D}pdp1_stddev.c \
        ${PDP1D}pdp1_sys.c
PDP1_OPT = -I ${PDP1D}


NOVAD = NOVA/
NOVA = ${NOVAD}nova_sys.c ${NOVAD}nova_cpu.c ${NOVAD}nova_dkp.c \
        ${NOVAD}nova_dsk.c ${NOVAD}nova_lp.c ${NOVAD}nova_mta.c \
        ${NOVAD}nova_plt.c ${NOVAD}nova_pt.c ${NOVAD}nova_clk.c \
        ${NOVAD}nova_tt.c ${NOVAD}nova_tt1.c
NOVA_OPT = -I ${NOVAD}



ECLIPSE = ${NOVAD}eclipse_cpu.c ${NOVAD}eclipse_tt.c ${NOVAD}nova_sys.c \
        ${NOVAD}nova_dkp.c ${NOVAD}nova_dsk.c ${NOVAD}nova_lp.c \
        ${NOVAD}nova_mta.c ${NOVAD}nova_plt.c ${NOVAD}nova_pt.c \
        ${NOVAD}nova_clk.c ${NOVAD}nova_tt1.c
ECLIPSE_OPT = -I ${NOVAD} -DECLIPSE



PDP18BD = PDP18B/
PDP18B = ${PDP18BD}pdp18b_dt.c ${PDP18BD}pdp18b_drm.c ${PDP18BD}pdp18b_cpu.c \
        ${PDP18BD}pdp18b_lp.c ${PDP18BD}pdp18b_mt.c ${PDP18BD}pdp18b_rf.c \
        ${PDP18BD}pdp18b_rp.c ${PDP18BD}pdp18b_stddev.c ${PDP18BD}pdp18b_sys.c \
        ${PDP18BD}pdp18b_tt1.c
PDP4_OPT = -DPDP4 -I ${PDP18BD}
PDP7_OPT = -DPDP7 -I ${PDP18BD}
PDP9_OPT = -DPDP9 -I ${PDP18BD}
PDP15_OPT = -DPDP15 -I ${PDP18BD}



PDP11D = PDP11/
PDP11 = ${PDP11D}pdp11_fp.c ${PDP11D}pdp11_cpu.c ${PDP11D}pdp11_dz.c \
        ${PDP11D}pdp11_cis.c ${PDP11D}pdp11_lp.c ${PDP11D}pdp11_rk.c \
        ${PDP11D}pdp11_rl.c ${PDP11D}pdp11_rp.c ${PDP11D}pdp11_rx.c \
        ${PDP11D}pdp11_stddev.c ${PDP11D}pdp11_sys.c ${PDP11D}pdp11_tc.c \
        ${PDP11D}pdp11_tm.c ${PDP11D}pdp11_ts.c ${PDP11D}pdp11_io.c \
        ${PDP11D}pdp11_rq.c
PDP11_OPT = -I ${PDP11D}



PDP10D = PDP10/
PDP10 = ${PDP10D}pdp10_fe.c ${PDP10D}pdp10_dz.c ${PDP10D}pdp10_cpu.c \
        ${PDP10D}pdp10_ksio.c ${PDP10D}pdp10_lp20.c ${PDP10D}pdp10_mdfp.c \
        ${PDP10D}pdp10_pag.c ${PDP10D}pdp10_pt.c ${PDP10D}pdp10_rp.c \
        ${PDP10D}pdp10_sys.c ${PDP10D}pdp10_tim.c ${PDP10D}pdp10_tu.c \
        ${PDP10D}pdp10_xtnd.c
PDP10_OPT = -DUSE_INT64 -I ${PDP10D}



PDP8D = PDP8/
PDP8 = ${PDP8D}pdp8_cpu.c ${PDP8D}pdp8_clk.c ${PDP8D}pdp8_df.c \
        ${PDP8D}pdp8_dt.c ${PDP8D}pdp8_lp.c ${PDP8D}pdp8_mt.c \
        ${PDP8D}pdp8_pt.c ${PDP8D}pdp8_rf.c ${PDP8D}pdp8_rk.c \
        ${PDP8D}pdp8_rx.c ${PDP8D}pdp8_sys.c ${PDP8D}pdp8_tt.c \
        ${PDP8D}pdp8_ttx.c ${PDP8D}pdp8_rl.c
PDP8_OPT = -I ${PDP8D}



H316D = H316/
H316 = ${H316D}h316_stddev.c ${H316D}h316_lp.c ${H316D}h316_cpu.c \
        ${H316D}h316_sys.c
H316_OPT = -I ${H316D}



HP2100D = HP2100/
HP2100 = ${HP2100D}hp2100_stddev.c ${HP2100D}hp2100_dp.c ${HP2100D}hp2100_lp.c \
        ${HP2100D}hp2100_mt.c ${HP2100D}hp2100_cpu.c ${HP2100D}hp2100_sys.c
HP2100_OPT = -I ${HP2100D}



ID4D = ID4/
ID4 = ${ID4D}id4_fp.c ${ID4D}id4_cpu.c ${ID4D}id4_stddev.c ${ID4D}id4_sys.c
ID4_OPT = -I ${ID4D}



I1401D = I1401/
I1401 = ${I1401D}i1401_lp.c ${I1401D}i1401_cpu.c ${I1401D}i1401_iq.c \
        ${I1401D}i1401_cd.c ${I1401D}i1401_mt.c ${I1401D}i1401_sys.c
I1401_OPT = -I ${I1401D}



VAXD = VAX/
VAX = ${VAXD}vax_cpu1.c ${VAXD}vax_cpu.c ${VAXD}vax_fpa.c ${VAXD}vax_io.c \
        ${VAXD}vax_mmu.c ${VAXD}vax_stddev.c ${VAXD}vax_sys.c \
        ${VAXD}vax_sysdev.c \
        ${PDP11D}pdp11_rl.c ${PDP11D}pdp11_rq.c ${PDP11D}pdp11_ts.c \
        ${PDP11D}pdp11_dz.c ${PDP11D}pdp11_lp.c
VAX_OPT = -I ${VAXD} -I ${PDP11D} -DUSE_INT64



SDSD = SDS/
SDS = ${SDSD}sds_stddev.c ${SDSD}sds_fhd.c ${SDSD}sds_io.c ${SDSD}sds_lp.c \
        ${SDSD}sds_mt.c ${SDSD}sds_rad.c ${SDSD}sds_cpu.c ${SDSD}sds_sys.c
SDS_OPT = -I ${SDSD}



S3D = S3/
S3 = ${S3D}s3_cd.c ${S3D}s3_cpu.c ${S3D}s3_disk.c ${S3D}s3_lp.c \
        ${S3D}s3_pkb.c ${S3D}s3_sys.c
S3_OPT = -I ${S3D}



ALTAIRD = ALTAIR/
ALTAIR = ${ALTAIRD}altair_sio.c ${ALTAIRD}altair_cpu.c ${ALTAIRD}altair_dsk.c \
        ${ALTAIRD}altair_sys.c
ALTAIR_OPT = -I ${ALTAIRD}


#
# Build everything
#
all : ${BIN}pdp1 ${BIN}pdp4 ${BIN}pdp7 ${BIN}pdp8 ${BIN}pdp9 ${BIN}pdp15 \
        ${BIN}pdp11 ${BIN}pdp10 ${BIN}vax ${BIN}nova ${BIN}eclipse ${BIN}h316 \
        ${BIN}hp2100 ${BIN}id4 ${BIN}i1401 ${BIN}sds ${BIN}s3 ${BIN}altair



#
# Make sure subdirectory exists
#
${BIN} : simh_doc.txt
        -mkdir ${BIN}
        -touch ${BIN}


#
# Individual builds
#
${BIN}pdp1 : ${PDP1} ${SIM} ${BIN}
        ${CC} ${PDP1} ${SIM} ${PDP1_OPT} -o $@



${BIN}pdp4 : ${PDP18B} ${SIM} ${BIN}
        ${CC} ${PDP18B} ${SIM} ${PDP4_OPT} -o $@



${BIN}pdp7 : ${PDP18B} ${SIM} ${BIN}
        ${CC} ${PDP18B} ${SIM} ${PDP7_OPT} -o $@



${BIN}pdp8 : ${PDP8} ${SIM} ${BIN}
        ${CC} ${PDP8} ${SIM} ${PDP8_OPT} -o $@



${BIN}pdp9 : ${PDP18B} ${SIM} ${BIN}
        ${CC} ${PDP18B} ${SIM} ${PDP9_OPT} -o $@



${BIN}pdp15 : ${PDP18B} ${SIM} ${BIN}
        ${CC} ${PDP18B} ${SIM} ${PDP15_OPT} -o $@



${BIN}pdp10 : ${PDP10} ${SIM} ${BIN}
        ${CC} ${PDP10} ${SIM} ${PDP10_OPT} -o $@



${BIN}pdp11 : ${PDP11} ${SIM} ${BIN}
        ${CC} ${PDP11} ${SIM} ${PDP11_OPT} -o $@



${BIN}vax : ${VAX} ${SIM} ${BIN}
        ${CC} ${VAX} ${SIM} ${VAX_OPT} -o $@



${BIN}nova : ${NOVA} ${SIM} ${BIN}
        ${CC} ${NOVA} ${SIM} ${NOVA_OPT} -o $@



${BIN}eclipse : ${ECLIPSE} ${SIM} ${BIN}
        ${CC} ${ECLIPSE} ${SIM} ${ECLIPSE_OPT} -o $@



${BIN}h316 : ${H316} ${SIM} ${BIN}
        ${CC} ${H316} ${SIM} ${H316_OPT} -o $@



${BIN}hp2100 : ${HP2100} ${SIM} ${BIN}
        ${CC} ${HP2100} ${SIM} ${HP2100_OPT} -o $@



${BIN}id4 : ${ID4} ${SIM} ${BIN}
        ${CC} ${ID4} ${SIM} ${ID4_OPT} -o $@



${BIN}i1401 : ${I1401} ${SIM} ${BIN}
        ${CC} ${I1401} ${SIM} ${I1401_OPT} -o $@



${BIN}sds : ${SDS} ${SIM} ${BIN}
        ${CC} ${SDS} ${SIM} ${SDS_OPT} -o $@



${BIN}s3 : ${S3} ${SIM} ${BIN}
        ${CC} ${S3} ${SIM} ${S3_OPT} -o $@


${BIN}altair : ${ALTAIR} ${SIM} ${BIN}
        ${CC} ${ALTAIR} ${SIM} ${ALTAIR_OPT} -o $@
