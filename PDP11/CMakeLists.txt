## The venerable PDP-11:

set(PDP11_SOURCES
    pdp11_fp.c
    pdp11_cpu.c
    pdp11_dz.c
    pdp11_cis.c
    pdp11_lp.c
    pdp11_rk.c
    pdp11_rl.c
    pdp11_rp.c
    pdp11_rx.c
    pdp11_stddev.c
    pdp11_sys.c
    pdp11_tc.c
    pdp11_tm.c
    pdp11_ts.c
    pdp11_io.c
    pdp11_rq.c
    pdp11_tq.c
    pdp11_pclk.c
    pdp11_ry.c
    pdp11_pt.c
    pdp11_hk.c
    pdp11_xq.c
    pdp11_xu.c
    pdp11_vh.c
    pdp11_rh.c
    pdp11_tu.c
    pdp11_cpumod.c
    pdp11_cr.c
    pdp11_rf.c
    pdp11_dl.c
    pdp11_ta.c
    pdp11_rc.c
    pdp11_kg.c
    pdp11_ke.c
    pdp11_dc.c
    pdp11_dmc.c
    pdp11_kmc.c
    pdp11_dup.c
    pdp11_rs.c
    pdp11_vt.c
    pdp11_td.c
    pdp11_io_lib.c
    pdp11_rom.c
    pdp11_ch.c
    pdp11_ng.c
    pdp11_daz.c)

set(PDP11_DEFINES VM_PDP11)

if (BUILD_WITH_VIDEO)
    set(PDP11_SOURCES ${PDP11_SOURCES} ${DISPLAYL} ${DISPLAYVT} ${DISPLAYNG})
    set(PDP11_DEFINES ${PDP11_DEFINES} USE_DISPLAY)
else (BUILD_WITH_VIDEO)
    message(STATUS "Not building display support into PDP-11")
endif (BUILD_WITH_VIDEO)

add_simulator(pdp11 
    SOURCES
	${PDP11_SOURCES}
    DEFINES
	${PDP11_DEFINES}
    INCLUDES
	${PDP11D}
    VIDEO)

add_simulator(uc15
    SOURCES
	pdp11_cis.c 
	pdp11_cpu.c
	pdp11_cpumod.c 
	pdp11_cr.c
	pdp11_fp.c 
	pdp11_io.c
	pdp11_io_lib.c
	pdp11_lp.c
	pdp11_rh.c 
	pdp11_rk.c
	pdp11_stddev.c 
	pdp11_sys.c
	pdp11_uc15.c
    DEFINES
	VM_PDP11
	UC15
    INCLUDES
	${PDP11_INCLUDES}
	${PDP18BD})
