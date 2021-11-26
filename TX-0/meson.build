simh_tx0_sources = [
  'tx0_cpu.c',
  'tx0_dpy.c',
  'tx0_stddev.c',
  'tx0_sys.c',
  'tx0_sys_orig.c'
]

executable('simh-tx0', simh_tx0_sources,
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
