plugin_c_args = ['-DHAVE_CONFIG_H', '-march=native', '-mtune=native']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-plugin-rknn')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer RKNN Plug-in')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://gstreamer.freedesktop.org')
configure_file(output : 'config.h', configuration : cdata)

# Plugin
plugin_sources = [
  'gstrknn.c',
  'rknn_meta.c',
  'rgaprocess.cc',
  'rknnprocess.cc',
  'postprocess.cc',
  'dmabuf.c',
  'cnpy/cnpy.cpp',
  ]

# Include directories
plugin_include_dirs = include_directories('.')

library('gstrknn',
  plugin_sources,
  c_args: plugin_c_args,
  cpp_args: plugin_c_args,
  include_directories: plugin_include_dirs,
  dependencies : [gst_dep, gstbase_dep, gstallocators_dep, gstvideo_dep, gstpbutils_dep, rga_dep, rknn_dep, rga_dep, opencv_dep, pthread_dep],
  install : true,
  install_dir : plugins_install_dir,
)
