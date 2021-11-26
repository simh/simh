simh_ssem_sources = [
  'ssem_cpu.c',
  'ssem_sys.c',
]

executable('simh-ssem', simh_ssem_sources,
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
