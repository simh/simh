## Honeywell 316/516 simulator:

add_simulator(h316
    SOURCES
	h316_stddev.c
	h316_lp.c
	h316_cpu.c
	h316_sys.c
	h316_mt.c
	h316_fhd.c
	h316_dp.c
	h316_rtc.c
	h316_imp.c
	h316_hi.c
	h316_mi.c
	h316_udp.c
    DEFINES
	VM_IMPTIP
    INCLUDES
	H316)
