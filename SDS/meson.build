simh_sds_sources = [
  'sds_cpu.c',
  'sds_drm.c',
  'sds_dsk.c',
  'sds_io.c',
  'sds_lp.c',
  'sds_mt.c',
  'sds_mux.c',
  'sds_rad.c',
  'sds_stddev.c',
  'sds_sys.c',
  'sds_cp.c',
  'sds_cr.c'
]

simh_sds_def = ['-DUSE_SIM_CARD']

executable('simh-sds', simh_sds_sources,
  c_args: executable_cflags + simh_sds_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

