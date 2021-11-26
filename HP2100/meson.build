simh_hp2100_sources = [
  'hp2100_baci.c',
  'hp2100_cpu.c',
  'hp2100_cpu_fp.c',
  'hp2100_cpu_fpp.c',
  'hp2100_cpu0.c',
  'hp2100_cpu1.c',
  'hp2100_cpu2.c',
  'hp2100_cpu3.c',
  'hp2100_cpu4.c',
  'hp2100_cpu5.c',
  'hp2100_cpu6.c',
  'hp2100_cpu7.c',
  'hp2100_di.c',
  'hp2100_di_da.c',
  'hp2100_disclib.c',
  'hp2100_dma.c',
  'hp2100_dp.c',
  'hp2100_dq.c',
  'hp2100_dr.c',
  'hp2100_ds.c',
  'hp2100_ipl.c',
  'hp2100_lps.c',
  'hp2100_lpt.c',
  'hp2100_mc.c',
  'hp2100_mem.c',
  'hp2100_mpx.c',
  'hp2100_ms.c',
  'hp2100_mt.c',
  'hp2100_mux.c',
  'hp2100_pif.c',
  'hp2100_pt.c',
  'hp2100_sys.c',
  'hp2100_tbg.c',
  'hp2100_tty.c'
]

simh_hp2100_def = ['-DHAVE_INT64']

executable('simh-hp2100', simh_hp2100_sources,
  c_args: executable_cflags + simh_hp2100_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

