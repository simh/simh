## CDC 1700 simulator:

add_simulator(cdc1700
    SOURCES
	cdc1700_cpu.c
	cdc1700_dis.c
	cdc1700_io.c
	cdc1700_sys.c
	cdc1700_dev1.c
	cdc1700_mt.c
	cdc1700_dc.c
	cdc1700_iofw.c
	cdc1700_lp.c
	cdc1700_dp.c
	cdc1700_cd.c
	cdc1700_sym.c
	cdc1700_rtc.c
	cdc1700_drm.c
	cdc1700_msos5.c
    INCLUDES
	${CMAKE_SOURCE_DIR}/CDC1700)
