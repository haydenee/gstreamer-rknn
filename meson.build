project('gst-plugin-rknn', ['c', 'cpp'], version : '1.19.0.1', license : 'LGPL')

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

gst_version = meson.project_version()
gst_version_req = '>=1.19'

api_version = '1.0'

gst_dep = dependency('gstreamer-1.0', version : gst_version_req,
    required : true, fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_version_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0', version : gst_version_req,
  fallback : ['gst-plugins-base', 'allocators_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_version_req,
  fallback : ['gst-plugins-base', 'video_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_version_req,
  fallback : ['gst-plugins-base', 'pbutils_dep'])
opencv_dep = dependency('opencv4', required : false)

project_source_root = meson.project_source_root()
rga_dep = dependency('librga', required : false)
message('Checking for librga dependency')
if not rga_dep.found()
  message('librga dependency not found, fallback to local version in thirdparty')
  rga_lib_dir = project_source_root /'thirdparty/librga/libs'
  rga_inc_dir = 'thirdparty/librga/include'
  rga_lib = cc.find_library('rga', dirs : rga_lib_dir, required : false)
  if rga_lib.found()
    rga_dep = declare_dependency(
      dependencies : rga_lib,
      include_directories : include_directories(rga_inc_dir)
    )
  else
    message('librga not found in thirdparty/librga/libs')
  endif
else
  message('system librga found')
endif

rknn_dep = dependency('librknnrt', required : false)
message('Checking for librknn dependency')
if not rknn_dep.found()
    message('librknn dependency not found, fallback to local version in thirdparty')
    rknn_lib_dir = project_source_root /'thirdparty/librknn_api/libs'
    rknn_inc_dir = 'thirdparty/librknn_api/include'
    rknn_lib = cc.find_library('rknnrt', dirs : rknn_lib_dir, required : false)
    if rknn_lib.found()
        rknn_dep = declare_dependency(
            dependencies : rknn_lib,
            include_directories : include_directories(rknn_inc_dir)
        )
    else
        message('librknn not found in thirdparty/librknn_api/libs')  
    endif
else
    message('system librknn found')
endif



subdir('src')
subdir('test')
