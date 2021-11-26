simh_h316_sources = [
  'h316_stddev.c',
  'h316_lp.c',
  'h316_cpu.c',
  'h316_sys.c',
  'h316_mt.c',
  'h316_fhd.c',
  'h316_dp.c',
  'h316_rtc.c',
  'h316_imp.c',
  'h316_hi.c',
  'h316_mi.c',
  'h316_udp.c'
]

simh_h316_def = ['-DVM_IMPTIP']

executable('simh-h316', simh_h316_sources,
  c_args: executable_cflags + simh_h316_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

