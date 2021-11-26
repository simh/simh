simh_lgp_sources = [
  'lgp_cpu.c',
  'lgp_stddev.c',
  'lgp_sys.c'
]

executable('simh-lgp', simh_lgp_sources,
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
