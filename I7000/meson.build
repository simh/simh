simh_i70xx_common_sources = [
  'i7000_cdp.c',
  'i7000_cdr.c',
  'i7000_con.c',
  'i7000_chan.c',
  'i7000_lpr.c',
  'i7000_mt.c',
  'i7000_chron.c',
  'i7000_dsk.c',
  'i7000_com.c',
  'i7000_ht.c'
]

simh_i7090_sources = [
  'i7090_cpu.c',
  'i7090_sys.c',
  'i7090_chan.c',
  'i7090_cdr.c',
  'i7090_cdp.c',
  'i7090_lpr.c',
  'i7000_chan.c',
  'i7000_mt.c',
  'i7090_drum.c',
  'i7090_hdrum.c',
  'i7000_chron.c',
  'i7000_dsk.c',
  'i7000_com.c',
  'i7000_ht.c'
]

simh_i7090_def = ['-DUSE_INT64', '-DI7090', '-DUSE_SIM_CARD']

simh_i7080_sources = simh_i70xx_common_sources + [
  'i7080_cpu.c',
  'i7080_sys.c',
  'i7080_chan.c',
  'i7080_drum.c',
]

simh_i7080_def = ['-DUSE_INT64', '-DI7080', '-DUSE_SIM_CARD']

simh_i7070_sources = simh_i70xx_common_sources + [
  'i7070_cpu.c',
  'i7070_sys.c',
  'i7070_chan.c',
]

simh_i7070_def = ['-DUSE_INT64', '-DI7070', '-DUSE_SIM_CARD']

simh_i7010_sources = simh_i70xx_common_sources + [
  'i7010_cpu.c',
  'i7010_sys.c',
  'i7010_chan.c'
]

simh_i7010_def = ['-DI7010', '-DUSE_SIM_CARD']

simh_i704_sources = [
  'i7090_cpu.c',
  'i7090_sys.c',
  'i7090_chan.c',
  'i7090_cdr.c',
  'i7090_cdp.c',
  'i7090_lpr.c',
  'i7000_mt.c',
  'i7090_drum.c',
  'i7000_chan.c'
]

simh_i704_def = ['-DUSE_INT64', '-DI704', '-DUSE_SIM_CARD']

simh_i701_sources = [
  'i701_cpu.c',
  'i701_sys.c',
  'i701_chan.c',
  'i7090_cdr.c',
  'i7090_cdp.c',
  'i7090_lpr.c',
  'i7000_mt.c',
  'i7090_drum.c',
  'i7000_chan.c'
]

simh_i701_def = ['-DUSE_INT64', '-DI701', '-DUSE_SIM_CARD']

executable('simh-i7090', simh_i7090_sources,
  c_args: executable_cflags + simh_i7090_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-i7080', simh_i7080_sources,
  c_args: executable_cflags + simh_i7080_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-i7070', simh_i7070_sources,
  c_args: executable_cflags + simh_i7070_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-i7010', simh_i7010_sources,
  c_args: executable_cflags + simh_i7010_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-i704', simh_i704_sources,
  c_args: executable_cflags + simh_i704_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-i701', simh_i701_sources,
  c_args: executable_cflags + simh_i701_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

