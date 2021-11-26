simh_i7094_sources = [
  'i7094_cpu.c',
  'i7094_cpu1.c',
  'i7094_io.c',
  'i7094_cd.c',
  'i7094_clk.c',
  'i7094_com.c',
  'i7094_drm.c',
  'i7094_dsk.c',
  'i7094_sys.c',
  'i7094_lp.c',
  'i7094_mt.c',
  'i7094_binloader.c'
]

simh_i7094_def = ['-DUSE_INT64']

executable('simh-i7094', simh_i7094_sources,
  c_args: executable_cflags + simh_i7094_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

