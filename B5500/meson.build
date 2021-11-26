simh_b5500_sources = [
  'b5500_cpu.c',
  'b5500_io.c',
  'b5500_sys.c',
  'b5500_dk.c',
  'b5500_mt.c',
  'b5500_urec.c',
  'b5500_dr.c',
  'b5500_dtc.c'
]

simh_b5500_def = ['-DB5500', '-DUSE_INT64', '-DUSE_SIM_CARD']

executable('simh-b5500', simh_b5500_sources,
  c_args: executable_cflags + simh_b5500_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

