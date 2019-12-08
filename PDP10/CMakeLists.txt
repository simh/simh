## PDP-10 simulator family

add_simulator(pdp10
    SOURCES
	pdp10_fe.c
	${PDP11D}/pdp11_dz.c
	pdp10_cpu.c
	pdp10_ksio.c
	pdp10_lp20.c
	pdp10_mdfp.c
	pdp10_pag.c
	pdp10_rp.c
	pdp10_sys.c
	pdp10_tim.c
	pdp10_tu.c
	pdp10_xtnd.c
	${PDP11D}/pdp11_pt.c
	${PDP11D}/pdp11_ry.c
	${PDP11D}/pdp11_cr.c
	${PDP11D}/pdp11_dup.c
	${PDP11D}/pdp11_dmc.c
	${PDP11D}/pdp11_kmc.c
	${PDP11D}/pdp11_xu.c
	${PDP11D}/pdp11_ch.c
    DEFINES
	VM_PDP10
    INCLUDES
	${PDP10D}
	${PDP11D}
    INT64)

if (BUILD_WITH_VIDEO)
    add_simulator(pdp10-ka
	SOURCES
	    kx10_cpu.c 
	    kx10_sys.c 
	    ka10_ai.c
	    kx10_df.c
	    kx10_dp.c 
	    kx10_mt.c 
	    kx10_cty.c
	    kx10_lp.c 
	    kx10_pt.c 
	    kx10_dc.c
	    kx10_rp.c 
	    kx10_rc.c 
	    kx10_dt.c
	    kx10_dk.c 
	    kx10_cr.c 
	    kx10_cp.c
	    kx10_tu.c 
	    kx10_rs.c 
	    ka10_pd.c
	    kx10_imp.c 
	    ka10_tk10.c 
	    ka10_mty.c
	    ka10_imx.c 
	    ka10_ch10.c 
	    ka10_stk.c
	    ka10_ten11.c 
	    ka10_auxcpu.c
	    ka10_pmp.c
	    ka10_dkb.c 
	    pdp6_dct.c 
	    pdp6_dtc.c
	    pdp6_mtc.c 
	    pdp6_dsk.c 
	    pdp6_dcs.c
	    ka10_dpk.c 
	    kx10_dpy.c 
	    ${DISPLAYL} 
	    ${DISPLAY340}
	DEFINES
	    KA=1
	    USE_SIM_CARD
	    USE_DISPLAY
	INCLUDES
	    ${PDP10D}
	INT64
	VIDEO)

    if (NOT WIN32 AND PANDA_LIGHTS)
	# ONLY for Panda display.
	target_sources(pdp10-ka PUBLIC ka10_lights.c)
	target_compile_definitions(pdp10-ka PUBLIC PANDA_LIGHTS)
	target_link_libraries(pdp10-ka "usb-1.0")
    endif ()

    add_simulator(pdp10-ki
	SOURCES
	    kx10_cpu.c 
	    kx10_sys.c 
	    kx10_df.c
	    kx10_dp.c 
	    kx10_mt.c 
	    kx10_cty.c
	    kx10_lp.c 
	    kx10_pt.c 
	    kx10_dc.c
	    kx10_rp.c 
	    kx10_rc.c 
	    kx10_dt.c
	    kx10_dk.c 
	    kx10_cr.c 
	    kx10_cp.c
	    kx10_tu.c 
	    kx10_rs.c 
	    kx10_imp.c
	    kx10_dpy.c
	    ${DISPLAYL} 
	    ${DISPLAY340}
	DEFINES
	    KI=1
	    USE_SIM_CARD
	    USE_DISPLAY
	INCLUDES
	    ${PDP10D}
	INT64
	VIDEO)

    if (NOT WIN32 AND PANDA_LIGHTS)
	# ONLY for Panda display.
	target_sources(pdp10-ki PUBLIC ka10_lights.c)
	target_compile_definitions(pdp10-ki PUBLIC PANDA_LIGHTS)
	target_link_libraries(pdp10-ki "usb-1.0")
    endif ()

    add_simulator(pdp6
	SOURCES
	    kx10_cpu.c 
	    kx10_sys.c 
	    kx10_cty.c
	    kx10_lp.c 
	    kx10_pt.c 
	    kx10_cr.c
	    kx10_cp.c 
	    pdp6_dct.c 
	    pdp6_dtc.c
	    pdp6_mtc.c 
	    pdp6_dsk.c 
	    pdp6_dcs.c
	    kx10_dpy.c
	    ${DISPLAYL} 
	    ${DISPLAY340}
	DEFINES
	    PDP6=1
	    USE_SIM_CARD
	    USE_DISPLAY
	INCLUDES
	    ${PDP10D}
	    ${PDP11D}
	INT64
        VIDEO)
else ()
    message(STATUS "SDL2 video support unavailable: skipping pdp10-ka, pdp10-ki and pdp6")
endif ()
