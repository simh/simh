## Manchester University SSEM (Small Scale Experimental Machine):

set(SSEMD "${CMAKE_SOURCE_DIR}/SSEM")

add_simulator(ssem
    SOURCES
	ssem_cpu.c
	ssem_sys.c
    INCLUDES
	SSEM)
