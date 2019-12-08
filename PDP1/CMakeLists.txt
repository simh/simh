# PDP-1 simulator:

set(PDP1_DEFINES "")
set(PDP1_DISPLAY "")
if (BUILD_WITH_VIDEO)
    set(PDP1_DEFINES "${PDP1_DEFINES}" USE_DISPLAY DISPLAY_TYPE=DIS_TYPE30 PIX_SCALE=RES_HALF)
    set(PDP1_DISPLAY ${DISPLAYL})
else (BUILD_WITH_VIDEO)
    message(STATUS "Not building display support into PDP1")
endif ()

add_simulator(pdp1
    SOURCES
	pdp1_lp.c
	pdp1_cpu.c
	pdp1_stddev.c
	pdp1_sys.c
	pdp1_dt.c
	pdp1_drm.c
	pdp1_clk.c
	pdp1_dcs.c
	pdp1_dpy.c
	${PDP1_DISPLAY}
    DEFINES
	${PDP1_DEFINES}
    INCLUDES
	${PDP1D}
    VIDEO
    )
