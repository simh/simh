simh_nova_common_sources = [
  'nova_sys.c',
  'nova_dkp.c',
  'nova_dsk.c',
  'nova_lp.c',
  'nova_mta.c',
  'nova_plt.c',
  'nova_pt.c',
  'nova_clk.c',
  'nova_tt1.c',
  'nova_qty.c'
]

simh_nova_sources = simh_nova_common_sources + [
  'nova_cpu.c',
  'nova_tt.c'
]

simh_eclipse_sources = simh_nova_common_sources + [
  'eclipse_cpu.c',
  'eclipse_tt.c'
]

simh_eclipse_def = ['-DECLIPSE', '-DUSE_INT64']

executable('simh-nova', simh_nova_sources,
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

executable('simh-eclipse', simh_eclipse_sources,
  c_args: executable_cflags + simh_eclipse_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

