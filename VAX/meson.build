simh_vax_common_sources = [
  'vax_cis.c',
  'vax_cmode.c',
  'vax_cpu.c',
  'vax_cpu1.c',
  'vax_fpa.c',
  'vax_lk.c',
  'vax_mmu.c',
  'vax_octa.c',
  'vax_sys.c',
  'vax_syscm.c',
  'vax_vs.c',
  'vax_watch.c',
]

simh_vax_sources = simh_vax_common_sources + [
  'vax_io.c',
  'vax_stddev.c',
  'vax_sysdev.c',
  'vax_syslist.c',
  'vax_vc.c',
  'vax_2681.c',
  '../PDP11/pdp11_rl.c',
  '../PDP11/pdp11_rq.c',
  '../PDP11/pdp11_ts.c',
  '../PDP11/pdp11_dz.c',
  '../PDP11/pdp11_lp.c',
  '../PDP11/pdp11_tq.c',
  '../PDP11/pdp11_xq.c',
  '../PDP11/pdp11_vh.c',
  '../PDP11/pdp11_cr.c',
  '../PDP11/pdp11_td.c',
  '../PDP11/pdp11_io_lib.c',
]

simh_vax4xx_common_sources = [
  'vax4xx_dz.c',
  'vax4xx_stddev.c',
  'vax_nar.c',
  'vax_xs.c',
]

simh_vax410_sources = simh_vax_common_sources + simh_vax4xx_common_sources + [
  'vax410_sysdev.c',
  'vax410_syslist.c',
  'vax4xx_rz80.c',
  'vax4xx_rd.c',
  'vax4xx_va.c',
  'vax4xx_vc.c',
  'vax_gpx.c',
]

simh_vax420_sources = simh_vax_common_sources + simh_vax4xx_common_sources + [
  'vax420_sysdev.c',
  'vax420_syslist.c',
  'vax4xx_rz80.c',
  'vax4xx_ve.c',
  'vax4xx_rd.c',
  'vax4xx_va.c',
  'vax4xx_vc.c',
  'vax_gpx.c',
]

simh_vax43_sources = simh_vax_common_sources + simh_vax4xx_common_sources + [
  'vax43_sysdev.c',
  'vax43_syslist.c',
  'vax4xx_rz80.c',
  'vax4xx_ve.c',
  'vax4xx_vc.c',
]

simh_vax440_sources = simh_vax_common_sources + simh_vax4xx_common_sources + [
  'vax440_sysdev.c',
  'vax440_syslist.c',
  'vax4xx_rz94.c',
]

simh_is1000_sources = simh_vax_common_sources + [
  'vax4xx_rz94.c',
  'vax4nn_stddev.c',
  'is1000_sysdev.c',
  'is1000_syslist.c',
  'vax_nar.c',
  'vax_xs.c',
]

simh_vax6xx_common_sources = simh_vax_common_sources + [
  'vax_vc.c',
  'vax_2681.c',
  '../PDP11/pdp11_rl.c',
  '../PDP11/pdp11_rq.c',
  '../PDP11/pdp11_ts.c',
  '../PDP11/pdp11_dz.c',
  '../PDP11/pdp11_lp.c',
  '../PDP11/pdp11_tq.c',
  '../PDP11/pdp11_xq.c',
  '../PDP11/pdp11_vh.c',
  '../PDP11/pdp11_cr.c',
  '../PDP11/pdp11_td.c',
  '../PDP11/pdp11_io_lib.c',
]

simh_vax610_sources = simh_vax6xx_common_sources + [
  'vax610_io.c',
  'vax610_mem.c',
  'vax610_stddev.c',
  'vax610_sysdev.c',
  'vax610_syslist.c',
]

simh_vax630_sources = simh_vax6xx_common_sources + [
  'vax630_stddev.c',
  'vax630_sysdev.c',
  'vax630_syslist.c',
  'vax630_io.c',
  'vax_va.c',
  'vax_gpx.c',
]

simh_vax_bare_def = ['-DVM_VAX', '-DUSE_INT64']
simh_vax_def = simh_vax_bare_def + ['-DUSE_ADDR64']
simh_vax_sim_video_def = simh_vax_def + ['-DUSE_SIM_VIDEO']

simh_vax410_def = simh_vax_sim_video_def + ['-DVAX_410']
simh_vax420_def = simh_vax_sim_video_def + ['-DVAX_420']
simh_vax411_def = simh_vax420_def + ['-DVAX_411']
simh_vax412_def = simh_vax420_def + ['-DVAX_412']
simh_vax41A_def = simh_vax420_def + ['-DVAX_41A']
simh_vax41D_def = simh_vax420_def + ['-DVAX_41D']
simh_vax42A_def = simh_vax420_def + ['-DVAX_42A']
simh_vax42B_def = simh_vax420_def + ['-DVAX_42B']
simh_vax43_def = simh_vax_sim_video_def + ['-DVAX_43']
simh_vax440_def = simh_vax_sim_video_def + ['-DVAX_440']
simh_vax46_def = simh_vax440_def + ['-DVAX_46']
simh_vax47_def = simh_vax440_def + ['-DVAX_47']
simh_vax48_def = simh_vax440_def + ['-DVAX_48']
simh_is1000_def = simh_vax_sim_video_def + ['-DIS_1000']

simh_vax610_def = simh_vax_sim_video_def + ['-DVAX_610']
simh_vax620_def = simh_vax_def + ['-DVAX_620']
simh_vax630_def = simh_vax_sim_video_def + ['-DVAX_630']

pdp11_inc = '../PDP11'
vax_inc = '../VAX'

executable('simh-vax', simh_vax_sources,
  c_args: executable_cflags + simh_vax_bare_def,
  include_directories: [platform_inc, vax_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-microvax3900', simh_vax_sources,
  c_args: executable_cflags + simh_vax_bare_def,
  include_directories: [platform_inc, vax_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-microvax2000', simh_vax410_sources,
  c_args: executable_cflags + simh_vax410_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-infoserver100', simh_vax420_sources,
  c_args: executable_cflags + simh_vax411_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-infoserver150vxt', simh_vax420_sources,
  c_args: executable_cflags + simh_vax412_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-microvax3100', simh_vax420_sources,
  c_args: executable_cflags + simh_vax41A_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-microvax3100e', simh_vax420_sources,
  c_args: executable_cflags + simh_vax41D_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-vaxstation3100m30', simh_vax420_sources,
  c_args: executable_cflags + simh_vax42A_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-vaxstation3100m38', simh_vax420_sources,
  c_args: executable_cflags + simh_vax42B_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-vaxstation3100m76', simh_vax43_sources,
  c_args: executable_cflags + simh_vax43_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-vaxstation4000m60', simh_vax440_sources,
  c_args: executable_cflags + simh_vax46_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-vaxstation3100m80', simh_vax440_sources,
  c_args: executable_cflags + simh_vax47_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-vaxstation4000vlc', simh_vax440_sources,
  c_args: executable_cflags + simh_vax48_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-infoserver1000', simh_is1000_sources,
  c_args: executable_cflags + simh_is1000_def,
  include_directories: [platform_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-microvax1', simh_vax610_sources,
  c_args: executable_cflags + simh_vax610_def,
  include_directories: [platform_inc, vax_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-rtvax1000', simh_vax630_sources,
  c_args: executable_cflags + simh_vax620_def,
  include_directories: [platform_inc, vax_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-microvax2', simh_vax630_sources,
  c_args: executable_cflags + simh_vax630_def,
  include_directories: [platform_inc, vax_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

