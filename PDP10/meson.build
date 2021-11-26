simh_pdp6_sources = [
  'kx10_cpu.c',
  'kx10_sys.c',
  'kx10_cty.c',
  'kx10_lp.c',
  'kx10_pt.c',
  'kx10_cr.c',
  'kx10_cp.c',
  'pdp6_dct.c',
  'pdp6_dtc.c',
  'pdp6_mtc.c',
  'pdp6_dsk.c',
  'pdp6_dcs.c',
  'kx10_dpy.c',
  'pdp6_slave.c'
]

simh_pdp10_sources = [
  'pdp10_fe.c',
  'pdp10_cpu.c',
  'pdp10_ksio.c',
  'pdp10_lp20.c',
  'pdp10_mdfp.c',
  'pdp10_pag.c',
  'pdp10_rp.c',
  'pdp10_sys.c',
  'pdp10_tim.c',
  'pdp10_tu.c',
  'pdp10_xtnd.c',
  '../PDP11/pdp11_dz.c',
  '../PDP11/pdp11_pt.c',
  '../PDP11/pdp11_ry.c',
  '../PDP11/pdp11_cr.c',
  '../PDP11/pdp11_dup.c',
  '../PDP11/pdp11_dmc.c',
  '../PDP11/pdp11_kmc.c',
  '../PDP11/pdp11_xu.c',
  '../PDP11/pdp11_ch.c'
]

simh_pdp10kx_common_sources = [
  'kx10_cpu.c',
  'kx10_sys.c',
  'kx10_df.c',
  'kx10_dp.c',
  'kx10_mt.c',
  'kx10_cty.c',
  'kx10_lp.c',
  'kx10_pt.c',
  'kx10_dc.c',
  'kx10_rh.c',
  'kx10_rp.c',
  'kx10_rc.c',
  'kx10_dt.c',
  'kx10_dk.c',
  'kx10_cr.c',
  'kx10_cp.c',
  'kx10_tu.c',
  'kx10_rs.c',
  'kx10_disk.c',
]

simh_pdp10ka_sources = simh_pdp10kx_common_sources + [
  'pdp6_dct.c',
  'pdp6_dtc.c',
  'pdp6_mtc.c',
  'pdp6_dsk.c',
  'pdp6_dcs.c',
  'ka10_dpk.c',
  'kx10_dpy.c',
  'ka10_ai.c',
  'ka10_iii.c',
  'ka10_pclk.c',
  'ka10_pd.c',
  'ka10_tk10.c',
  'ka10_mty.c',
  'ka10_imx.c',
  'ka10_ch10.c',
  'ka10_stk.c',
  'ka10_ten11.c',
  'ka10_auxcpu.c',
  'ka10_pmp.c',
  'ka10_dkb.c',
  'kx10_imp.c',
]

simh_pdp10ki_sources = simh_pdp10kx_common_sources +  [
  'kx10_imp.c',
  'kx10_dpy.c',
]

simh_pdp10kl_sources = simh_pdp10kx_common_sources + [
  'kx10_imp.c',
  'kl10_fe.c',
  'ka10_pd.c',
  'ka10_ch10.c',
  'kl10_nia.c',
]

simh_pdp6_def = ['-DPDP6=1', '-DUSE_INT64', '-DUSE_SIM_CARD']
simh_pdp10_def = ['-DVM_PDP10', '-DUSE_INT64']
simh_pdp10ka_def = ['-DKA=1', '-DUSE_INT64']
simh_pdp10ki_def = ['-DKI=1', '-DUSE_INT64']
simh_pdp10kl_def = ['-DKL=1', '-DUSE_INT64']

pdp10_inc = '../PDP10'
pdp11_inc = '../PDP11'

executable('simh-pdp6', simh_pdp6_sources,
  c_args: executable_cflags + simh_pdp6_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-pdp10-ka', simh_pdp10ka_sources,
  c_args: executable_cflags + simh_pdp10ka_def,
  include_directories: [platform_inc, pdp10_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-pdp10-ki', simh_pdp10ki_sources,
  c_args: executable_cflags + simh_pdp10ki_def,
  include_directories: [platform_inc, pdp10_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-pdp10-kl', simh_pdp10kl_sources,
  c_args: executable_cflags + simh_pdp10kl_def,
  include_directories: [platform_inc, pdp10_inc, pdp11_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)
