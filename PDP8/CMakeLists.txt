# PDP8 simulators:

add_simulator(pdp8
    SOURCES 
        pdp8_cpu.c
        pdp8_clk.c
        pdp8_df.c
        pdp8_dt.c
        pdp8_lp.c
        pdp8_mt.c
        pdp8_pt.c
        pdp8_rf.c
        pdp8_rk.c
        pdp8_rx.c
        pdp8_sys.c
        pdp8_tt.c
        pdp8_ttx.c
        pdp8_rl.c
        pdp8_tsc.c
        pdp8_td.c
        pdp8_ct.c
        pdp8_fpp.c
    INCLUDES
	PDP8
    TEST pdp8)
