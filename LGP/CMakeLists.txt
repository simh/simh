## LGP simulator:

set(LGPD "${CMAKE_SOURCE_DIR}/LGP")

add_simulator(lgp "${LGP_SOURCES}" "${LGP_DEFINES}" "${LGP_INCLUDES}"
    SOURCES
	lgp_cpu.c
	lgp_stddev.c
	lgp_sys.c
    INCLUDES
	LGP)
