simh_i650_sources = [
  'i650_cpu.c',
  'i650_cdr.c',
  'i650_cdp.c',
  'i650_dsk.c',
  'i650_mt.c',
  'i650_sys.c'
]

simh_i650_def = ['-DUSE_INT64', '-DUSE_SIM_CARD']

executable('simh-i650', simh_i650_sources,
  c_args: executable_cflags + simh_i650_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

