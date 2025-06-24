# Plugin
rgatest_sources = [
  'rga_test.c',
  ]

rgatest = executable('rgatest',
  rgatest_sources,
  c_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, gstallocators_dep, gstvideo_dep, gstpbutils_dep, rga_dep, rknn_dep, opencv_dep],
  install : false,
#   install_dir : plugins_install_dir,
)