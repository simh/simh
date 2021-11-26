simh_ibm1130_sources = [
  'ibm1130_cpu.c',
  'ibm1130_cr.c',
  'ibm1130_disk.c',
  'ibm1130_stddev.c',
  'ibm1130_sys.c',
  'ibm1130_gdu.c',
  'ibm1130_gui.c',
  'ibm1130_prt.c',
  'ibm1130_fmt.c',
  'ibm1130_ptrp.c',
  'ibm1130_plot.c',
  'ibm1130_sca.c',
  'ibm1130_t2741.c'
]

simh_ibm1130_def = []

simh_ibm1130_gui = []
if host_machine.system() == 'windows'
  simh_ibm1130_gui = cc.find_library('gdi32', required: true)
  simh_ibm1130_def = ['-DGUI_SUPPORT']
endif

executable('simh-ibm1130', simh_ibm1130_sources,
  c_args: executable_cflags + simh_ibm1130_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
	simh_ibm1130_gui
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

