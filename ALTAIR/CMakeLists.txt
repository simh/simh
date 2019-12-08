## The venerable Altair

add_simulator(altair
    SOURCES
	altair_sio.c
	altair_cpu.c
	altair_dsk.c
	altair_sys.c
    INCLUDES
	${ALTAIRD})
