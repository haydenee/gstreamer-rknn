#define _GNU_SOURCE
#include "rknn_api.h"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gst/gst.h>
#include <gst/gstbuffer.h>
#include <gst/gstinfo.h>
#include <gst/gstmemory.h>
#include <gst/gstpad.h>
#include <gst/video/gstvideopool.h>
#include <gst/video/video-format.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

#include "dmabuf.h"
#include "gstrknn.h"
#include "rknn_meta.h"
#include "rknnprocess.h"
#include "gstrknn_impl.h"

#include <sys/time.h>

GST_DEBUG_CATEGORY(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

// 以下是 rknn plugin 的模板框架，
// 只有属性 setter/getter，初始化结构/函数指针/caps 等内容。没有实质逻辑

enum {
  PROP_0,
  PROP_MODEL_PATH,
  PROP_SOCKET_CONFIG_PATH,
  PROP_WORKERS,
  PROP_MPPJPEGDEC_OFFSET_WORKAROUND,
  PROP_DRAW_BOXES,
  PROP_RESIZE_MODE
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, "
                    "format = (string) { RGB, NV16, NV12 }, "));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, "
                    "format = (string) { RGB, NV16, NV12 } "));

#define gst_plugin_rknn_parent_class parent_class
G_DEFINE_TYPE(GstPluginRknn, gst_plugin_rknn, GST_TYPE_ELEMENT);

GST_ELEMENT_REGISTER_DEFINE(plugin_rknn, "rknn", GST_RANK_NONE,
                            GST_TYPE_PLUGIN_RKNN);

static void gst_plugin_rknn_set_property(GObject *object, guint prop_id,
                                         const GValue *value,
                                         GParamSpec *pspec);
static void gst_plugin_rknn_get_property(GObject *object, guint prop_id,
                                         GValue *value, GParamSpec *pspec);
static gboolean gst_plugin_rknn_sink_event(GstPad *pad, GstObject *parent,
                                           GstEvent *event);
static GstFlowReturn gst_plugin_rknn_chain(GstPad *pad, GstObject *parent,
                                           GstBuffer *buf);

/* GstElement vmethod implementations with detailed logging */

/* GObject vmethod implementations */

/* initialize the plugin's class */
static void gst_plugin_rknn_class_init(GstPluginRknnClass *klass) {
  GST_DEBUG("Entering gst_plugin_rknn_class_init");

  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *)klass;
  gstelement_class = (GstElementClass *)klass;

  GST_DEBUG("Class pointers assigned");

  gobject_class->set_property = gst_plugin_rknn_set_property;
  gobject_class->get_property = gst_plugin_rknn_get_property;

  GST_DEBUG("Property setters/getters assigned");

  g_object_class_install_property(
      gobject_class, PROP_MODEL_PATH,
      g_param_spec_string("model-path", "Model Path",
                          "Path to the RKNN model file", NULL,
                          G_PARAM_READWRITE));

  GST_DEBUG("Model path property installed");

  g_object_class_install_property(
      gobject_class, PROP_SOCKET_CONFIG_PATH,
      g_param_spec_string("socket-config-path", "Socket Config Path",
                          "Path to the socket configuration file", "./socket_config.json",
                          G_PARAM_READWRITE));

  g_object_class_install_property(
      gobject_class, PROP_RESIZE_MODE,
      g_param_spec_string("resize-mode", "Resize Mode",
                          "Resize mode: 'crop' or 'pad'", "pad",
                          G_PARAM_READWRITE));

  g_object_class_install_property(
      gobject_class, PROP_WORKERS,
      g_param_spec_int("workers", "Workers",
                       "Number of workers for buffer pool", 1, 10,
                       DEFAULT_WORKERS, G_PARAM_READWRITE));

  g_object_class_install_property(
      gobject_class, PROP_MPPJPEGDEC_OFFSET_WORKAROUND,
      g_param_spec_boolean(
          "mppjpegdec-offset-workaround", "MPP JPEG Decoder Offset Workaround",
          "Enable workaround for MPP JPEG decoder offset issues", FALSE,
          G_PARAM_READWRITE));

  g_object_class_install_property(
      gobject_class, PROP_DRAW_BOXES,
      g_param_spec_boolean("draw-boxes", "Draw Boxes",
                           "Enable drawing bounding boxes on output", TRUE,
                           G_PARAM_READWRITE));

  GST_DEBUG("Resize mode property installed");

  gst_element_class_set_details_simple(
      gstelement_class, "Plugin", "FIXME:Generic",
      "FIXME:Generic Template Element", "AUTHOR_NAME AUTHOR_EMAIL");

  GST_DEBUG("Element details set");

  gst_element_class_add_pad_template(gstelement_class,
                                     gst_static_pad_template_get(&src_factory));
  gst_element_class_add_pad_template(
      gstelement_class, gst_static_pad_template_get(&sink_factory));

  GST_DEBUG("Pad templates added");
  GST_DEBUG("Exiting gst_plugin_rknn_class_init");
}

static void gst_plugin_rknn_init(GstPluginRknn *filter) {
  GST_DEBUG("Initializing GstPluginRknn element");

  // Initialize all pointers to NULL first
  filter->sinkpad = NULL;
  filter->srcpad = NULL;
  filter->model_path = NULL;
  filter->socket_config_path = g_strdup("./socket_config.json");
  filter->sink_caps = NULL;
  filter->src_caps = NULL;
  filter->rknn_engines = NULL;
  filter->workers = DEFAULT_WORKERS;            // 使用默认的 workers 数量
  filter->mppjpegdec_offset_workaround = FALSE; // 默认值为 FALSE
  filter->draw_boxes = TRUE;                    // 默认值为 TRUE
  filter->resize_mode = g_strdup("pad");        // 默认值为 "pad"
  filter->model_width = 0;
  filter->model_height = 0;
  filter->clock = gst_system_clock_obtain();
  filter->sink_format = GST_VIDEO_FORMAT_UNKNOWN;
  filter->img_width = 0;
  filter->img_height = 0;
  filter->workers_queue = g_async_queue_new();
  filter->next_worker_id = 0;

  filter->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  GST_DEBUG("Sink pad creation attempted, result: %p", filter->sinkpad);
  gst_pad_set_event_function(filter->sinkpad,
                             GST_DEBUG_FUNCPTR(gst_plugin_rknn_sink_event));
  GST_DEBUG("Sink pad event function set");
  gst_pad_set_chain_function(filter->sinkpad,
                             GST_DEBUG_FUNCPTR(gst_plugin_rknn_chain));
  GST_DEBUG("Sink pad chain function set");
  gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);
  GST_DEBUG("Sink pad added to element");
  filter->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  GST_DEBUG("Source pad creation attempted, result: %p", filter->srcpad);
  gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);
  GST_DEBUG("Source pad added to element");
  GST_INFO("GstPluginRknn element initialized.");
}

static void gst_plugin_rknn_set_property(GObject *object, guint prop_id,
                                         const GValue *value,
                                         GParamSpec *pspec) {
  GstPluginRknn *filter = GST_PLUGIN_RKNN(object);

  GST_DEBUG("set_property called with prop_id %d", prop_id);

  switch (prop_id) {
  case PROP_MODEL_PATH:
    GST_DEBUG("Setting model_path property");
    if (filter->model_path)
      g_free(filter->model_path);
    filter->model_path = g_value_dup_string(value);
    GST_DEBUG("model_path set to %s", filter->model_path);
    break;
  case PROP_SOCKET_CONFIG_PATH:
    GST_DEBUG("Setting socket_config_path property");
    if (filter->socket_config_path)
      g_free(filter->socket_config_path);
    filter->socket_config_path = g_value_dup_string(value);
    GST_DEBUG("socket_config_path set to %s", filter->socket_config_path);
    break;
  case PROP_WORKERS:
    GST_DEBUG("Setting workers property");
    filter->workers = g_value_get_int(value);
    GST_DEBUG("workers set to %d", filter->workers);
    break;
  case PROP_MPPJPEGDEC_OFFSET_WORKAROUND:
    GST_DEBUG("Setting mppjpegdec_offset_workaround property");
    filter->mppjpegdec_offset_workaround = g_value_get_boolean(value);
    GST_DEBUG("mppjpegdec_offset_workaround set to %s",
              filter->mppjpegdec_offset_workaround ? "TRUE" : "FALSE");
    break;
  case PROP_DRAW_BOXES:
    GST_DEBUG("Setting draw_boxes property");
    filter->draw_boxes = g_value_get_boolean(value);
    GST_DEBUG("draw_boxes set to %s", filter->draw_boxes ? "TRUE" : "FALSE");
    break;
  case PROP_RESIZE_MODE:
    GST_DEBUG("Setting resize_mode property");
    if (filter->resize_mode)
      g_free(filter->resize_mode);
    filter->resize_mode = g_value_dup_string(value);
    GST_DEBUG("resize_mode set to %s", filter->resize_mode);
    break;
  default:
    GST_WARNING("Invalid property ID: %d", prop_id);
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_plugin_rknn_get_property(GObject *object, guint prop_id,
                                         GValue *value, GParamSpec *pspec) {
  GstPluginRknn *filter = GST_PLUGIN_RKNN(object);

  GST_DEBUG("get_property called with prop_id %d", prop_id);

  switch (prop_id) {
  case PROP_MODEL_PATH:
    GST_DEBUG("Getting model_path property: %s", filter->model_path);
    g_value_set_string(value, filter->model_path);
    break;
  case PROP_SOCKET_CONFIG_PATH:
    GST_DEBUG("Getting socket_config_path property: %s",
              filter->socket_config_path);
    g_value_set_string(value, filter->socket_config_path);
    break;
  case PROP_WORKERS:
    GST_DEBUG("Getting workers property: %d", filter->workers);
    g_value_set_int(value, filter->workers);
    break;
  case PROP_MPPJPEGDEC_OFFSET_WORKAROUND:
    GST_DEBUG_OBJECT(filter,
                     "Getting mppjpegdec_offset_workaround property: %s",
                     filter->mppjpegdec_offset_workaround ? "TRUE" : "FALSE");
    g_value_set_boolean(value, filter->mppjpegdec_offset_workaround);
    break;
  case PROP_DRAW_BOXES:
    GST_DEBUG("Getting draw_boxes property: %s",
              filter->draw_boxes ? "TRUE" : "FALSE");
    g_value_set_boolean(value, filter->draw_boxes);
    break;
  case PROP_RESIZE_MODE:
    GST_DEBUG("Getting resize_mode property: %s", filter->resize_mode);
    g_value_set_string(value, filter->resize_mode);
    break;
  default:
    GST_WARNING("Invalid property ID: %d", prop_id);
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean gst_plugin_rknn_sink_event(GstPad *pad, GstObject *parent,
                                           GstEvent *event) {
  GstPluginRknn *filter;
  gboolean ret = FALSE;

  filter = GST_PLUGIN_RKNN(parent);

  GST_DEBUG("Received %s event: %" GST_PTR_FORMAT, GST_EVENT_TYPE_NAME(event),
            event);

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_CAPS: {
    GstCaps *caps;

    gst_event_parse_caps(event, &caps);
    if (filter->sink_caps && gst_caps_is_equal(filter->sink_caps, caps)) {
      GST_DEBUG("Recieved same caps event, ignore");
      ret = gst_pad_event_default(pad, parent, event);
      break;
    } else if (filter->sink_caps) {
      GST_DEBUG("Recieved different caps event");
      GST_DEBUG("Before: %" GST_PTR_FORMAT, filter->sink_caps);
      GST_DEBUG("After: %" GST_PTR_FORMAT, caps);
      release_rknn_resources(filter); // 这里会释放 sink_caps 的。
    }

    filter->sink_caps = gst_caps_copy(caps);
    GST_DEBUG("Caps event initialization stage");
    /* 解析并存储输入格式信息 */
    GstStructure *structure = gst_caps_get_structure(caps, 0);
    const gchar *format_str = gst_structure_get_string(structure, "format");
    gst_structure_get_int(structure, "width", &filter->img_width);
    gst_structure_get_int(structure, "height", &filter->img_height);
    filter->sink_format = gst_video_format_from_string(format_str);
    gst_pad_set_caps(filter->srcpad, filter->sink_caps);
    ret = allocate_rknn_resources(filter);
    ret &= gst_pad_event_default(pad, parent, event);
    break;
  }
  case GST_EVENT_EOS: {
    release_rknn_resources(filter);
    ret = gst_pad_event_default(pad, parent, event);
    break;
  }
  default:
    ret = gst_pad_event_default(pad, parent, event);
    break;
  }
  return ret;
}
static GstFlowReturn gst_plugin_rknn_chain(GstPad *pad, GstObject *parent,
                                           GstBuffer *buf) {
  GstPluginRknn *filter;

  filter = GST_PLUGIN_RKNN(parent);

  /* Add VideoMeta if meta info doesn't exist */
  GstVideoMeta *meta = gst_buffer_get_video_meta(buf);
  if (meta == NULL) {
    GstVideoInfo video_info;
    gst_video_info_from_caps(&video_info, filter->sink_caps);
    gst_buffer_add_video_meta(buf, GST_VIDEO_FRAME_FLAG_NONE,
                              video_info.finfo->format, video_info.width,
                              video_info.height);
  }

  // Workaround mppjpegdec offset bug
  if (filter->mppjpegdec_offset_workaround) {
    GST_DEBUG("Apply offset workaround to raw_input. Before %zu After %d",
              meta->offset[1], meta->stride[0] * GST_ROUND_UP_16(meta->height));
    meta->offset[1] = meta->stride[0] * GST_ROUND_UP_16(meta->height);
  }

  /* Log buffer information using the new utility function */

  GST_DEBUG("Buffer received, size: %lu dts %zu pts %zu offset %zu",
            gst_buffer_get_size(buf), buf->dts, buf->pts, buf->offset);
  log_buffer_info(buf);

  scheduler(filter, buf);
  /* just push out the incoming buffer without touching it */
  return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin) {
  GST_DEBUG_CATEGORY_INIT(gst_plugin_rknn_debug, "rknn", 0, "RKNN plugin");
  GST_DEBUG("Plugin debug category initialized");
  gboolean result = GST_ELEMENT_REGISTER(plugin_rknn, plugin);
  GST_DEBUG("Plugin registration result: %d", result);
  return result;
}

#ifndef PACKAGE
#define PACKAGE "gst-plugin-rknn"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, rknn, "rknn",
                  plugin_init, PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME,
                  GST_PACKAGE_ORIGIN)
