## Interdata simulators:

add_simulator(id16
    SOURCES
	id16_cpu.c
	id16_sys.c
	id_dp.c
	id_fd.c
	id_fp.c
	id_idc.c
	id_io.c
	id_lp.c
	id_mt.c
	id_pas.c
	id_pt.c
	id_tt.c
	id_uvc.c
	id16_dboot.c
	id_ttp.c
    INCLUDES
	${ID16D})


add_simulator(id32
    SOURCES
	id32_cpu.c
	id32_sys.c
	id_dp.c
	id_fd.c
	id_fp.c
	id_idc.c
	id_io.c
	id_lp.c
	id_mt.c
	id_pas.c
	id_pt.c
	id_tt.c
	id_uvc.c
	id32_dboot.c
	id_ttp.c
    INCLUDES
	${ID32D})
