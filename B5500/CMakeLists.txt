## burroughs 5500 CPU simulator:

set(B5500D "${CMAKE_SOURCE_DIR}/B5500")

add_simulator(b5500
    SOURCES
	b5500_cpu.c
	b5500_io.c
	b5500_sys.c
	b5500_dk.c
	b5500_mt.c
	b5500_urec.c
	b5500_dr.c
	b5500_dtc.c
    DEFINES
	B5500
    INCLUDES
	B5500
    INT64)
