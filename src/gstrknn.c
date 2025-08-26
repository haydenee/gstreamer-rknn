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

#include <sys/time.h>

/* Forward declarations */
static gpointer push_thread(gpointer data);
static gpointer work_thread(gpointer data);

GST_DEBUG_CATEGORY(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug


/* 初始化 RKNN 推理引擎，并获得 RKNN 输入所需的长宽 */
static gboolean init_rknn_engines(GstPluginRknn *filter) {
  // 为 RKNN 引擎数组分配内存
  filter->rknn_engines = (struct RknnEngine **)g_malloc0(
      sizeof(struct RknnEngine *) * filter->workers);

  // 初始化每个 RKNN 引擎
  for (int i = 0; i < filter->workers; i++) {
    // 为每个引擎分配内存
    filter->rknn_engines[i] =
        (struct RknnEngine *)g_malloc0(sizeof(struct RknnEngine));
    struct RknnEngine *rknn_process = filter->rknn_engines[i];
    // 设置输入输出队列
    rknn_process->rknn_input_queue = g_async_queue_new();
    rknn_process->rknn_output_queue = g_async_queue_new();
    rknn_process->model_path = g_strdup(filter->model_path);
    rknn_process->filter = GST_OBJECT(filter);
    // 初始化 RKNN 引擎，传入第一个引擎的上下文
    // 第一次调用时，第一个引擎的上下文为0，会初始化新上下文
    // 后续调用时，第一个引擎的上下文已初始化，会复用上下文
    rknn_process->worker_id = i;
    
    rknn_process->worker_0_ctx = &filter->rknn_engines[0]->ctx;
    rknn_init_process(rknn_process);

    // 设置原始图像尺寸
    rknn_process->img_width = filter->img_width;
    rknn_process->img_height = filter->img_height;
    filter->model_width = rknn_process->model_width;
    filter->model_height = rknn_process->model_height;
  }
  GST_DEBUG("sink_width %d sink_height %d rknn_width %d rknn_height %d",
            filter->img_width, filter->img_height, filter->model_width,
            filter->model_height);
  // 计算缩放比例和填充
  float scale_w = (float)filter->model_width / filter->img_width;
  float scale_h = (float)filter->model_height / filter->img_height;
  float scale;
  int new_width, new_height;
  int model_pad_left = 0;
  int model_pad_top = 0;
  int model_pad_right = 0;
  int model_pad_bottom = 0;
  int img_pad_left = 0;
  int img_pad_top = 0;
  int img_pad_right = 0;
  int img_pad_bottom = 0;

  // 根据模式计算缩放比例
  if (g_strcmp0(filter->resize_mode, "crop") == 0) {
    // crop 模式：缩放到较长的边刚好足够长度，较短的边裁切掉一部分
    scale = fmax(scale_w, scale_h);
    new_width = (int)(filter->model_width / scale);
    new_height = (int)(filter->model_height / scale);
    img_pad_left = (int)((filter->img_width - new_width) / 2);
    img_pad_top = (int)((filter->img_height - new_height) / 2);
    img_pad_right = filter->img_width - new_width - img_pad_left;
    img_pad_bottom = filter->img_height - new_height - img_pad_top;
  } else {
    // 默认 pad 模式：缩放到较短的边有足够长度，较长的边可以填充
    scale = fmin(scale_w, scale_h);
    new_width = (int)(filter->img_width * scale);
    new_height = (int)(filter->img_height * scale);
    model_pad_left = (int)((filter->model_width - new_width) / 2);
    model_pad_top = (int)((filter->model_height - new_height) / 2);
    model_pad_right = filter->model_width - new_width - model_pad_left;
    model_pad_bottom = filter->model_height - new_height - model_pad_top;
  }

  for (int i = 0; i < filter->workers; i++) {
    filter->rknn_engines[i]->model_pad_left = model_pad_left;
    filter->rknn_engines[i]->model_pad_top = model_pad_top;
    filter->rknn_engines[i]->model_pad_right = model_pad_right;
    filter->rknn_engines[i]->model_pad_bottom = model_pad_bottom;
    filter->rknn_engines[i]->img_pad_left = img_pad_left;
    filter->rknn_engines[i]->img_pad_top = img_pad_top;
    filter->rknn_engines[i]->img_pad_right = img_pad_right;
    filter->rknn_engines[i]->img_pad_bottom = img_pad_bottom;
    filter->rknn_engines[i]->scale_img_to_model = scale;
    filter->rknn_engines[i]->scale_model_to_img = 1.0f / scale;
  }
  // 输出不同模式下的偏移量信息
  GST_DEBUG(
      "init scale and %s: scale %.2f %dx%d input_pad (%d %d) img_pad (%d %d)",
      filter->resize_mode, scale, new_width, new_height, model_pad_left,
      model_pad_top, img_pad_left, img_pad_top);

  GST_INFO("Initialized %d RKNN engines, model size: %dx%d", filter->workers,
           filter->model_width, filter->model_height);

  return TRUE;
}

/* 销毁 RKNN 推理引擎 */
void destroy_rknn_engines(GstPluginRknn *filter) {
  if (filter->rknn_engines == NULL) {
    return;
  }
  for (int i = 0; i < filter->workers; i++) {
    if (filter->rknn_engines[i]) {
      rknn_release(filter->rknn_engines[i]);
      if (filter->rknn_engines[i]->model_path) {
        g_free(filter->rknn_engines[i]->model_path);
      }
      g_async_queue_unref(filter->rknn_engines[i]->rknn_input_queue);
      g_async_queue_unref(filter->rknn_engines[i]->rknn_output_queue);
      g_free(filter->rknn_engines[i]);
    }
  }
  g_free(filter->rknn_engines);
  filter->rknn_engines = NULL;
}

/* 在运行时申请的资源 */
static gboolean allocate_rknn_resources(GstPluginRknn *filter) {
  GST_DEBUG("allocate_state_resources start");

  // 只有在模型路径存在时才初始化 RKNN 引擎
  if (filter->model_path) {
    if (!init_rknn_engines(filter)) {
      GST_ERROR("Failed to initialize RKNN engines");
      return FALSE;
    }
  } else {
    GST_ERROR("No model path specified, RKNN engines not initialized");
    return FALSE;
  }

  // Initialize task data and start multiple consumer threads
  GST_INFO("Creating %d RKNN consumer threads", filter->workers);
  filter->task_threads = g_new0(GThread *, filter->workers);

  for (int i = 0; i < filter->workers; i++) {

    gchar thread_name[32];
    g_snprintf(thread_name, sizeof(thread_name), "rknn-consumer-%d", i);
    filter->task_threads[i] =
        g_thread_new(thread_name, work_thread, filter->rknn_engines[i]);
    GST_DEBUG("Created RKNN consumer thread %d with name: %s", i, thread_name);
  }

  // 创建 output_collector_thread 线程
  GST_INFO("Creating output collector thread");
  filter->push_thread =
      g_thread_new("output-collector", push_thread, filter);

  GST_DEBUG("allocate_state_resources end");
  return TRUE;
}

static void release_rknn_resources(GstPluginRknn *filter) {
  // 停止并清理多个任务线程
  if (filter->task_threads) {
    GST_INFO("Stopping %d RKNN consumer threads", filter->workers);

    // 向每个线程发送 NULL buffer 作为停止信号
    for (int i = 0; i < filter->workers; i++) {
      GstBuffer *null_buffer = gst_buffer_new();
      g_async_queue_push(filter->rknn_engines[i]->rknn_input_queue,
                         null_buffer);
    }

    // 等待所有线程结束
    for (int i = 0; i < filter->workers; i++) {
      if (filter->task_threads[i]) {
        GST_DEBUG("Waiting for worker %d thread to join", i);
        g_thread_join(filter->task_threads[i]);
        filter->task_threads[i] = NULL;
        GST_DEBUG("Worker %d thread joined successfully", i);
      }
    }
    g_free(filter->task_threads);
    GST_INFO("All RKNN consumer threads stopped and cleaned up");
  }

  // 向收集线程发送停止信号
  // 直接发送任意指针会报错，需要一个真实的对象。
  filter->rknn_engines[0]->worker_id = -1;
  g_async_queue_push(filter->workers_queue, filter->rknn_engines[0]);
  GST_DEBUG("Pushed worker_id -1 to workers queue");
  g_thread_join(filter->push_thread);
  GST_INFO("Push thread stopped and cleaned up");
  
  /* Free allocated resources */
  if (filter->model_path) {
    g_free(filter->model_path);
    filter->model_path = NULL;
  }

  if (filter->socket_config_path) {
    g_free(filter->socket_config_path);
    filter->socket_config_path = NULL;
  }

  if (filter->resize_mode) {
    g_free(filter->resize_mode);
    filter->resize_mode = NULL;
  }

  if (filter->sink_caps) {
    gst_caps_unref(filter->sink_caps);
    filter->sink_caps = NULL;
  }

  if (filter->src_caps) {
    gst_caps_unref(filter->src_caps);
    filter->src_caps = NULL;
  }

  if (filter->workers_queue) {
    g_async_queue_unref(filter->workers_queue);
  }

  destroy_rknn_engines(filter);
  GST_DEBUG("Release rknn resources done");
}

/* 预处理格式转换和缩放 */

gboolean scheduler(GstPluginRknn *filter, GstBuffer *raw_buffer) {

  gst_buffer_add_meta(raw_buffer, GST_RKNN_META_INFO, NULL);
  struct RknnMeta* rknn_meta = &gst_buffer_get_rknn_meta(raw_buffer)->rknn_meta;
  rknn_meta->start_time = gst_clock_get_time(filter->clock);
  struct RknnEngine *rknn_engine = filter->rknn_engines[filter->next_worker_id];
  while (g_async_queue_length(filter->workers_queue) > 3) {
    // GST_TRACE("Waiting for free workers");
    g_usleep(1000);
  }
  g_async_queue_push(filter->workers_queue, rknn_engine);
  g_async_queue_push(rknn_engine->rknn_input_queue, raw_buffer);
  GST_DEBUG("Scheduler push buffer %lu to worker %d",
            GST_BUFFER_OFFSET(raw_buffer), filter->next_worker_id);
  filter->next_worker_id++;
  if (filter->next_worker_id == filter->workers) {
    filter->next_worker_id = 0;
  }

  return TRUE;
}
static gpointer work_thread(gpointer data) {
  struct RknnEngine* rknn_engine = (struct RknnEngine*) data;
  GstPluginRknn* filter = GST_PLUGIN_RKNN(rknn_engine->filter);
  gint worker_id = rknn_engine->worker_id;

  // 绑定 CPU 和 NPU 核心
  int npu_core = worker_id % 3;
  rknn_core_mask npu_core_mask = npu_core == 0   ? RKNN_NPU_CORE_0
                                 : npu_core == 1 ? RKNN_NPU_CORE_1
                                                 : RKNN_NPU_CORE_2;
  rknn_set_core_mask(rknn_engine->ctx, npu_core_mask);
  int cpu_core = worker_id % 4 + 4; // 4-7 是大核
  cpu_set_t cpuset;
  pthread_t current_thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(cpu_core, &cpuset);
  pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);

  GST_INFO("Starting RKNN engine thread %d", worker_id);
  while (TRUE) {
    GstBuffer *img_buffer = g_async_queue_pop(rknn_engine->rknn_input_queue);
    GST_DEBUG("Thread %d recieved buffer %zu", worker_id, img_buffer->offset);

    // 终止信号
    if (gst_buffer_get_size(img_buffer) == 0) {
      GST_INFO("Thread %d recieved ending buffer, stoping", worker_id);
      gst_buffer_unref(img_buffer);
      return NULL;
    }

    struct RknnMeta* rknn_meta = &gst_buffer_get_rknn_meta(img_buffer)->rknn_meta;
    rknn_engine->rknn_meta = rknn_meta;
    rknn_meta->queue_done = gst_clock_get_time(filter->clock);

    GST_TRACE("Thread %d offset %zu start infer", worker_id,
              img_buffer->offset);
    rknn_preprocess(rknn_engine, img_buffer);
    rknn_meta->preprocess_done = gst_clock_get_time(filter->clock);
  //   dump_tensor_data("preprocess", GST_BUFFER_OFFSET(img_buffer), &rknn_engine->input_attr, 
  // rknn_engine->input_mem->priv_data, rknn_engine->input_attr.size_with_stride);
    rknn_inference(rknn_engine);
    rknn_meta->infer_done = gst_clock_get_time(filter->clock);
    rknn_postprocess(rknn_engine, 0.6, 0.45);
    rknn_meta->postprocess_done = gst_clock_get_time(filter->clock);
    if (filter->draw_boxes) {
      rknn_visualize(rknn_engine, img_buffer);
    }
    rknn_meta->visualize_done = gst_clock_get_time(filter->clock);
    g_async_queue_push(rknn_engine->rknn_output_queue, img_buffer);
    GST_INFO(
        "Thread %d offset %zu person %d | queue %zuus, preprocess %zuus, infer "
        "%zuus, postprocess %zuus, visualize %zuus.",
        worker_id, img_buffer->offset, rknn_meta->results_size,
        (rknn_meta->queue_done - rknn_meta->start_time) / GST_USECOND,
        (rknn_meta->preprocess_done - rknn_meta->queue_done) / GST_USECOND,
        (rknn_meta->infer_done - rknn_meta->preprocess_done) / GST_USECOND,
        (rknn_meta->postprocess_done - rknn_meta->infer_done) / GST_USECOND,
        (rknn_meta->visualize_done - rknn_meta->postprocess_done) / GST_USECOND);
  }
}

static gpointer push_thread(gpointer data) {
  GstPluginRknn *filter = GST_PLUGIN_RKNN(data);
  GST_DEBUG("Push thread start");
  while (TRUE) {
    struct RknnEngine* worker = g_async_queue_pop(filter->workers_queue);
    GST_DEBUG("Push thread worker id %d", worker->worker_id);
    if (worker->worker_id == -1) {
      break;
    }
    GstBuffer* buf = g_async_queue_pop(worker->rknn_output_queue);
    
    struct RknnMeta* meta = &gst_buffer_get_rknn_meta(buf)->rknn_meta;
    meta->push_pad_done = gst_clock_get_time(filter->clock);
    GST_DEBUG("Push thread pushed offset %lu", GST_BUFFER_OFFSET(buf));
    gst_pad_push(filter->srcpad, buf);
  }
  GST_DEBUG("Push thread stop");

  return NULL;
}

// 以下是 GStreamer Plugin的模板框架，没有实质逻辑

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
                          "Path to the socket configuration file", NULL,
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

  // Test current dmabuf buffer pool
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */

static void gst_plugin_rknn_init(GstPluginRknn *filter) {
  GST_DEBUG("Initializing GstPluginRknn element");

  // Initialize all pointers to NULL first
  filter->sinkpad = NULL;
  filter->srcpad = NULL;
  filter->model_path = NULL;
  filter->socket_config_path = NULL;
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
  // 初始化输入格式信息
  filter->sink_format = GST_VIDEO_FORMAT_UNKNOWN;
  filter->img_width = 0;
  filter->img_height = 0;

  GST_DEBUG("Pointers initialized to NULL");

  // Create sink pad
  filter->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  GST_DEBUG("Sink pad creation attempted, result: %p", filter->sinkpad);

  if (!filter->sinkpad) {
    GST_ERROR("Failed to create sink pad");
    return;
  }

  gst_pad_set_event_function(filter->sinkpad,
                             GST_DEBUG_FUNCPTR(gst_plugin_rknn_sink_event));
  GST_DEBUG("Sink pad event function set");
  gst_pad_set_chain_function(filter->sinkpad,
                             GST_DEBUG_FUNCPTR(gst_plugin_rknn_chain));
  GST_DEBUG("Sink pad chain function set");

  if (!gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad)) {
    GST_ERROR("Failed to add sink pad to element");
    gst_object_unref(filter->sinkpad);
    filter->sinkpad = NULL;
    return;
  }

  GST_DEBUG("Sink pad added to element");

  // Create source pad
  filter->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  GST_DEBUG("Source pad creation attempted, result: %p", filter->srcpad);

  if (!filter->srcpad) {
    GST_ERROR("Failed to create src pad");
    return;
  }
  

  if (!gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad)) {
    GST_ERROR("Failed to add src pad to element");
    gst_object_unref(filter->srcpad);
    filter->srcpad = NULL;
    return;
  }

  GST_DEBUG("Source pad added to element");

  // Initialize queues
  filter->workers_queue = g_async_queue_new();
  filter->next_worker_id = 0;

  GST_DEBUG("GstPluginRknn element initialized successfully");
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
    GST_DEBUG("Getting socket_config_path property: %s", filter->socket_config_path);
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
    // NV16/NV12 转 RGB 需要宽度 16 对齐，高度 2 对齐
    filter->sink_format = gst_video_format_from_string(format_str);
    // 目前的实现不需要更改 caps 了。
    gst_pad_set_caps(filter->srcpad, filter->sink_caps);

    // caps 协商完成后可以分配资源了。
    ret = allocate_rknn_resources(filter);
    /* and forward */
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

/* chain function
 * this function does the actual processing
 */
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

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean plugin_init(GstPlugin *plugin) {

  /* debug category for filtering log messages
   *
   * exchange the string 'Template plugin' with your description
   */

  GST_DEBUG_CATEGORY_INIT(gst_plugin_rknn_debug, "rknn", 0, "RKNN plugin");

  GST_DEBUG("Plugin debug category initialized");

  gboolean result = GST_ELEMENT_REGISTER(plugin_rknn, plugin);

  GST_DEBUG("Plugin registration result: %d", result);

  return result;
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "gst-plugin-rknn"
#endif

/* gstreamer looks for this structure to register plugins
 *
 * exchange the string 'Template plugin' with your plugin description
 */
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, rknn, "rknn",
                  plugin_init, PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME,
                  GST_PACKAGE_ORIGIN)

