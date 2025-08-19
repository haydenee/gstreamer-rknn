# RGA test
executable('rgatest',
  'rga_test.c',
  c_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, gstallocators_dep, gstvideo_dep, gstpbutils_dep, rga_dep, rknn_dep, opencv_dep],
  install : false,
#   install_dir : plugins_install_dir,
)

# DMA buffer test
executable('dmabuftest',
  'test_dma_buffer_new.c',
  '../src/dmabuf.c',
  c_args: plugin_c_args,
  include_directories : include_directories('../src'),
  dependencies : [gst_dep, gstbase_dep, gstallocators_dep, gstvideo_dep, gstpbutils_dep, rga_dep, rknn_dep, opencv_dep, pthread_dep],
  install : false,
#   install_dir : plugins_install_dir,
)
