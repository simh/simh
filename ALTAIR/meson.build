simh_altair_sources = [
  'altair_cpu.c',
  'altair_dsk.c',
  'altair_sio.c',
  'altair_sys.c'
]

executable('simh-altair', simh_altair_sources,
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
