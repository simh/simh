## SDS simulator:

add_simulator(sds
    SOURCES
	sds_cpu.c
	sds_drm.c
	sds_dsk.c
	sds_io.c
	sds_lp.c
	sds_mt.c
	sds_mux.c
	sds_rad.c
	sds_stddev.c
	sds_sys.c
    INCLUDES
	SDS)
