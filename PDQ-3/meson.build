simh_pdq3_sources = [
  'pdq3_cpu.c',
  'pdq3_sys.c',
  'pdq3_stddev.c',
  'pdq3_mem.c',
  'pdq3_debug.c',
  'pdq3_fdc.c'
]

executable('simh-pdq3', simh_pdq3_sources,
  c_args: executable_cflags,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)
