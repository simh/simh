simh_tt2500_sources = [
  'tt2500_sys.c',
  'tt2500_cpu.c',
  'tt2500_dpy.c',
  'tt2500_crt.c',
  'tt2500_tv.c',
  'tt2500_key.c',
  'tt2500_uart.c',
  'tt2500_rom.c'
]

executable('simh-tt2500', simh_tt2500_sources,
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

