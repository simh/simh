## PDP-18B-based simulators

set(PDP18B_SOURCES
    pdp18b_dt.c
    pdp18b_drm.c
    pdp18b_cpu.c
    pdp18b_lp.c
    pdp18b_mt.c
    pdp18b_rf.c
    pdp18b_rp.c
    pdp18b_stddev.c
    pdp18b_sys.c
    pdp18b_rb.c
    pdp18b_tt1.c
    pdp18b_fpp.c
    pdp18b_g2tty.c
    pdp18b_dr15.c)

add_simulator(pdp4
    SOURCES
	${PDP18B_SOURCES}
    DEFINES
	PDP4
    INCLUDES
	${PDP18BD})

add_simulator(pdp7
    SOURCES
	${PDP18B_SOURCES}
    DEFINES
	PDP7
    INCLUDES
	${PDP18BD}
    TEST pdp7)

add_simulator(pdp9
    SOURCES
	${PDP18B_SOURCES}
    DEFINES
	PDP9
    INCLUDES
	${PDP18BD})

add_simulator(pdp15
    SOURCES
    	${PDP18B_SOURCES}
    DEFINES
	PDP15
    INCLUDES
	${PDP18BD})
