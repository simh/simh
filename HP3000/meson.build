simh_hp3000_sources = [
  'hp_disclib.c',
  'hp_tapelib.c',
  'hp3000_atc.c',
  'hp3000_clk.c',
  'hp3000_cpu.c',
  'hp3000_cpu_base.c',
  'hp3000_cpu_fp.c',
  'hp3000_cpu_cis.c',
  'hp3000_ds.c',
  'hp3000_iop.c',
  'hp3000_lp.c',
  'hp3000_mem.c',
  'hp3000_mpx.c',
  'hp3000_ms.c',
  'hp3000_scmb.c',
  'hp3000_sel.c',
  'hp3000_sys.c'
]

executable('simh-hp3000', simh_hp3000_sources,
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

