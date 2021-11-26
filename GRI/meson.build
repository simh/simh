simh_gri_sources = [
  'gri_cpu.c',
  'gri_stddev.c',
  'gri_sys.c'
]

executable('simh-gri', simh_gri_sources,
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

