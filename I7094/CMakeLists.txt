## IBM 7094:

add_simulator(i7094
    SOURCES
	i7094_cpu.c
	i7094_cpu1.c
	i7094_io.c
	i7094_cd.c
	i7094_clk.c
	i7094_com.c
	i7094_drm.c
	i7094_dsk.c
	i7094_sys.c
	i7094_lp.c
	i7094_mt.c
	i7094_binloader.c
    INCLUDES
	${I7094D}
    INT64)
