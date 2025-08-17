
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "glib.h"
#include "gst/gstbuffer.h"
#include "gst/gstinfo.h"
#include "gst/gstmemory.h"
#include "gst/gstpad.h"
#include "gst/video/video-format.h"
#include <gst/gst.h>
#include <gst/video/gstvideopool.h>
#include <math.h>

#include "dmabuf.h"
#include "gstrknn.h"
#include "rgaprocess.h"
#include "rknnprocess.h"
#include "unistd.h"

/* Forward declarations */
static gpointer push_thread(gpointer data);
static gpointer rknn_engine_thread(gpointer data);

GST_DEBUG_CATEGORY(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

/* Buffer offset 比较函数 */
static gint buffer_offset_compare(gconstpointer a, gconstpointer b) {
  GstBuffer *buf_a = (GstBuffer *)a;
  GstBuffer *buf_b = (GstBuffer *)b;

  if (buf_a->offset < buf_b->offset) {
    return -1;
  } else if (buf_a->offset > buf_b->offset) {
    return 1;
  } else {
    return 0;
  }
}
/* RKNN 推理消费者线程 */
typedef struct {
  GstPluginRknn *filter;
  gint worker_id;
} ThreadData;

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
    struct RknnEngine *rknn_engine = filter->rknn_engines[i];

    // 设置模型路径和标签路径
    if (filter->model_path) {
      rknn_engine->model_path = g_strdup(filter->model_path);
    }

    // 初始化 RKNN 引擎，传入第一个引擎的上下文
    // 第一次调用时，第一个引擎的上下文为0，会初始化新上下文
    // 后续调用时，第一个引擎的上下文已初始化，会复用上下文
    int ret = rknn_prepare(rknn_engine, &filter->rknn_engines[0]->ctx);
    if (ret < 0) {
      GST_ERROR_OBJECT(filter, "Failed to initialize RKNN engine %d", i);
      // 使用 destroy_rknn_engines 来释放所有已分配的资源
      destroy_rknn_engines(filter);
      return FALSE;
    }

    // 设置原始图像尺寸
    rknn_engine->img_width = filter->img_width;
    rknn_engine->img_height = filter->img_height;
    filter->model_width = rknn_engine->model_width;
    filter->model_height = rknn_engine->model_height;
  }
  GST_DEBUG("sink_width %d sink_height %d rknn_width %d rknn_height %d", filter->img_width, filter->img_height, filter->model_width, filter->model_height);
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
  GST_DEBUG("init scale and %s: scale %.2f %dx%d input_pad (%d %d) img_pad (%d %d)", 
            filter->resize_mode, scale, new_width, new_height, model_pad_left, model_pad_top, img_pad_left, img_pad_top);

  GST_INFO_OBJECT(filter, "Initialized %d RKNN engines, model size: %dx%d",
                  filter->workers, filter->model_width, filter->model_height);

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
      g_free(filter->rknn_engines[i]);
    }
  }
  g_free(filter->rknn_engines);
  filter->rknn_engines = NULL;
}

/* 在运行时申请的资源 */
static gboolean allocate_rknn_resources(GstPluginRknn *filter) {
  GST_LOG_OBJECT(filter, "allocate_state_resources start");

  // 只有在模型路径存在时才初始化 RKNN 引擎
  if (filter->model_path) {
    if (!init_rknn_engines(filter)) {
      GST_ERROR_OBJECT(filter, "Failed to initialize RKNN engines");
      return FALSE;
    }
  } else {
    GST_ERROR_OBJECT(filter,
                     "No model path specified, RKNN engines not initialized");
    return FALSE;
  }

  // 创建缓冲区池来替代直接创建缓冲区
  if (filter->sink_caps) {
    GST_INFO_OBJECT(filter,
                    "Creating buffer pools to replace direct buffer creation");

    // 使用新的工具函数创建 RKNN 输入缓冲区池
    GError *error = NULL;
    filter->rknn_buffer_pool = dma_buffer_pool_new(
        GST_VIDEO_FORMAT_RGB, filter->model_width, filter->model_height, 1, 1);

    if (!filter->rknn_buffer_pool) {
      GST_ERROR_OBJECT(filter, "Failed to create RKNN buffer pool: %s",
                       error ? error->message : "Unknown error");
      if (error) {
        g_error_free(error);
      }
      return FALSE;
    }
    GST_INFO_OBJECT(filter,
                    "RKNN buffer pool created and started successfully");
  }
  // Initialize task data and start multiple consumer threads
  GST_INFO_OBJECT(filter, "Creating %d RKNN consumer threads", filter->workers);
  filter->task_threads = g_new0(GThread *, filter->workers);

  for (int i = 0; i < filter->workers; i++) {
    ThreadData *thread_data = g_new0(ThreadData, 1);
    thread_data->filter = filter;
    thread_data->worker_id = i;

    gchar thread_name[32];
    g_snprintf(thread_name, sizeof(thread_name), "rknn-consumer-%d", i);
    filter->task_threads[i] =
        g_thread_new(thread_name, rknn_engine_thread, thread_data);
    GST_INFO_OBJECT(filter, "Created RKNN consumer thread %d with name: %s", i,
                    thread_name);
  }

  // 创建 output_collector_thread 线程
  GST_INFO_OBJECT(filter, "Creating output collector thread");
  filter->output_collector_thread =
      g_thread_new("output-collector", push_thread, filter);
  GST_INFO_OBJECT(filter, "Created output collector thread");

  GST_LOG_OBJECT(filter, "allocate_state_resources end");
  return TRUE;
}

static void release_rknn_resources(GstPluginRknn *filter) {
  // 停止并清理多个任务线程
  if (filter->task_threads) {
    GST_INFO_OBJECT(filter, "Stopping %d RKNN consumer threads",
                    filter->workers);

    // 向每个线程发送 NULL buffer 作为停止信号
    for (int i = 0; i < filter->workers; i++) {
      GstBuffer *null_buffer = gst_buffer_new();
      g_async_queue_push(filter->rknn_input_queue, null_buffer);
    }

    // 等待所有线程结束
    for (int i = 0; i < filter->workers; i++) {
      if (filter->task_threads[i]) {
        GST_DEBUG_OBJECT(filter, "Waiting for worker %d thread to join", i);
        g_thread_join(filter->task_threads[i]);
        filter->task_threads[i] = NULL;
        GST_DEBUG_OBJECT(filter, "Worker %d thread joined successfully", i);
      }
    }

    g_free(filter->task_threads);
    filter->task_threads = NULL;
    GST_INFO_OBJECT(filter, "All RKNN consumer threads stopped and cleaned up");
  }

  // 停止并清理 output_collector_thread
  if (filter->output_collector_thread) {
    GST_INFO_OBJECT(filter, "Stopping output collector thread");
    // 向 output_collector_thread 发送 NULL buffer 作为停止信号
    GstBuffer *null_buffer = gst_buffer_new();
    g_async_queue_push(filter->raw_output_queue, null_buffer);

    // 等待线程结束
    GST_DEBUG_OBJECT(filter, "Waiting for output collector thread to join");
    g_thread_join(filter->output_collector_thread);
    filter->output_collector_thread = NULL;
    GST_DEBUG_OBJECT(filter, "Output collector thread joined successfully");
    GST_INFO_OBJECT(filter, "Output collector thread stopped and cleaned up");
  }

  /* Free allocated resources */
  if (filter->model_path) {
    g_free(filter->model_path);
    filter->model_path = NULL;
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

  // 清空处理中的队列
  if (filter->rknn_input_queue) {
    GstBuffer *buf;
    while ((buf = g_async_queue_try_pop(filter->rknn_input_queue)) != NULL) {
      gst_buffer_unref(buf);
    }
    g_async_queue_unref(filter->rknn_input_queue);
    filter->rknn_input_queue = NULL;
  }

  if (filter->rknn_output_queue) {
    GstBuffer *buf;
    while ((buf = g_async_queue_try_pop(filter->rknn_output_queue)) != NULL) {
      gst_buffer_unref(buf);
    }
    g_async_queue_unref(filter->rknn_output_queue);
    filter->rknn_output_queue = NULL;
  }

  if (filter->raw_input_queue) {
    GstBuffer *buf;
    while ((buf = g_async_queue_try_pop(filter->raw_input_queue)) != NULL) {
      gst_buffer_unref(buf);
    }
    g_async_queue_unref(filter->raw_input_queue);
    filter->raw_input_queue = NULL;
  }

  if (filter->raw_output_queue) {
    GstBuffer *buf;
    while ((buf = g_async_queue_try_pop(filter->raw_output_queue)) != NULL) {
      gst_buffer_unref(buf);
    }
    g_async_queue_unref(filter->raw_output_queue);
    filter->raw_output_queue = NULL;
  }

  // 释放缓冲区池
  if (filter->rknn_buffer_pool) {
    GST_INFO_OBJECT(filter, "Stopping RKNN buffer pool");
    gst_buffer_pool_set_active(GST_BUFFER_POOL(filter->rknn_buffer_pool),
                               FALSE);
    gst_object_unref(filter->rknn_buffer_pool);
    filter->rknn_buffer_pool = NULL;
  }

  // 清空乱序缓冲区
  if (filter->out_of_order_buffers) {
    GList *l;
    for (l = filter->out_of_order_buffers; l != NULL; l = l->next) {
      GstBuffer *buf = (GstBuffer *)l->data;
      gst_buffer_unref(buf);
    }
    g_list_free(filter->out_of_order_buffers);
    filter->out_of_order_buffers = NULL;
  }

  // 释放 RKNN 引擎
  destroy_rknn_engines(filter);
}

/* 预处理格式转换和缩放 */
gboolean preprocess_buffer(GstPluginRknn *filter, GstBuffer *raw_buffer) {
  GstBuffer *rknn_buffer = NULL;
  gboolean ret = FALSE;

  GST_LOG_OBJECT(filter, "Starting preprocess_buffer function");
  GST_DEBUG_OBJECT(filter, "raw_input before preprocess");
  if (filter->mppjpegdec_offset_workaround) {
    GstVideoMeta *meta = gst_buffer_get_video_meta(raw_buffer);
    GST_DEBUG_OBJECT(
        filter, "Apply offset workaround to raw_input. Before %zu After %d",
        meta->offset[1], meta->stride[0] * GST_ROUND_UP_16(meta->height));
    meta->offset[1] = meta->stride[0] * GST_ROUND_UP_16(meta->height);
  }
  log_buffer_info(raw_buffer);

  // 从缓冲区池获取 RKNN 缓冲区
  GstFlowReturn acquire_result = gst_buffer_pool_acquire_buffer(
      GST_BUFFER_POOL(filter->rknn_buffer_pool), &rknn_buffer, NULL);
  if (acquire_result != GST_FLOW_OK || !rknn_buffer) {
    GST_WARNING_OBJECT(filter, "Failed to acquire RKNN buffer from pool: %s",
                       gst_flow_get_name(acquire_result));
    goto error;
  }

  GST_DEBUG_OBJECT(filter, "Source format: %s, dimensions: %dx%d",
                   gst_video_format_to_string(filter->sink_format),
                   filter->img_width, filter->img_height);

  if (raw_to_rknn(filter->rknn_engines[0], raw_buffer, rknn_buffer) != 0) {
    GST_ERROR_OBJECT(filter, "Failed to convert format to RGB");
    goto error;
  }

  // 将处理后的缓冲区放入处理队列
  g_async_queue_push(filter->rknn_input_queue, rknn_buffer);
  g_async_queue_push(filter->raw_input_queue, raw_buffer);

  GST_LOG_OBJECT(filter, "Preprocessing completed successfully");
  ret = TRUE;
  return ret;

error:
  // 释放已分配的资源
  if (rknn_buffer) {
    gst_buffer_unref(rknn_buffer);
  }
  if (raw_buffer) {
    gst_buffer_unref(raw_buffer);
  }
  return ret;
}

static gpointer rknn_engine_thread(gpointer data) {
  ThreadData *thread_data = (ThreadData *)data;
  GstPluginRknn *filter = thread_data->filter;
  gint worker_id = thread_data->worker_id;
  struct RknnEngine *rknn_engine = filter->rknn_engines[worker_id];

  GST_INFO_OBJECT(filter, "Starting RKNN engine thread %d", worker_id);
  while (TRUE) {
    GstBuffer *rknn_buffer = g_async_queue_pop(filter->rknn_input_queue);
    if (gst_buffer_get_size(rknn_buffer) == 0) {
      GST_INFO_OBJECT(filter, "Thread %d recieved ending buffer, stoping",
                      worker_id);
      gst_buffer_unref(rknn_buffer);
      g_free(thread_data);
      return NULL;
    }
    GstBuffer *raw_buffer = g_async_queue_pop(filter->raw_input_queue);

    GST_INFO_OBJECT(filter, "Thread %d recieved buffer %zu", worker_id,
                    rknn_buffer->offset);
    GstMemory *rknn_mem = gst_buffer_peek_memory(rknn_buffer, 0);
    GstMapInfo rknn_map_info;
    GstMemory *raw_mem = gst_buffer_peek_memory(raw_buffer, 0);

    GstMapInfo rgb_map_info;
    gst_memory_map(rknn_mem, &rknn_map_info, GST_MAP_READWRITE);
    gst_memory_map(raw_mem, &rgb_map_info, GST_MAP_READWRITE);
    rknn_engine->inputs[0].buf = rknn_map_info.data;
    rknn_engine->inputs[0].size = rknn_map_info.size;

    // 执行推理
    GST_DEBUG_OBJECT(filter, "Thread %d offset %zu start infer", worker_id,
                     rknn_buffer->offset);
    rknn_inference(rknn_engine);
    // save_rgb_to_bmp("rknn_input.bmp", rknn_map_info.data, rknn_engine->model_width, rknn_engine->model_height);
    // rknn_dump_io(rknn_engine);
    // GST_DEBUG_OBJECT(filter, "Dumped npy");


    // 后处理
    GST_DEBUG_OBJECT(filter, "Thread %d offset %zu start postprocess",
                     worker_id, rknn_buffer->offset);
    rknn_postprocess(rknn_engine, 0.6, 0.45);  
    // 可视化
    GST_DEBUG_OBJECT(filter, "Thread %d offset %zu start visualize", worker_id, rknn_buffer->offset);
    rknn_outputs_release(rknn_engine->ctx, rknn_engine->io_num.n_output, rknn_engine->outputs);
    // 将网络输出存到 npy
    if (filter->draw_boxes) {
      rknn_visualize(rknn_engine, raw_buffer);
    }

    GST_INFO_OBJECT(filter, "Thread %d offset %zu done", worker_id,
                    rknn_buffer->offset);

    gst_memory_unmap(rknn_mem, &rknn_map_info);
    gst_memory_unmap(raw_mem, &rgb_map_info);
    gst_buffer_unref(rknn_buffer);
    g_async_queue_push(filter->raw_output_queue, raw_buffer);
  }
}

static gpointer push_thread(gpointer data) {
  GstPluginRknn *filter = GST_PLUGIN_RKNN(data);
  GST_DEBUG_OBJECT(filter, "Thread output start");
  while (TRUE) {
    // 从 rgb_output_queue 中取出 buffer
    GstBuffer *rgb_buf = g_async_queue_pop(filter->raw_output_queue);
    GST_DEBUG_OBJECT(filter, "Thread output recieved buffer offset %zu",
                     rgb_buf->offset);
    // 检查是否是停止信号
    if (gst_buffer_get_size(rgb_buf) == 0) {
      GST_INFO_OBJECT(
          filter, "Output collector thread received ending buffer, stopping");
      gst_buffer_unref(rgb_buf);
      return NULL;
    }
    // gst_pad_set_caps(filter->srcpad, filter->src_caps);

    // 检查 buffer 的 offset 是否等于 next_output_offset
    if (rgb_buf->offset == filter->next_output_offset) {
      GST_DEBUG_OBJECT(
          filter,
          "Buffer offset %zu matches next output offset, pushing directly",
          rgb_buf->offset);
      // 直接输出
      GST_DEBUG_OBJECT(
          filter, "Buffer offset %zu pushed, next output offset is now %zu",
          rgb_buf->offset, filter->next_output_offset);
      log_buffer_info(rgb_buf);
      gst_pad_push(filter->srcpad, rgb_buf);
      filter->next_output_offset++;
      // 检查 out_of_order_buffers 中是否有可以按顺序输出的 buffer
      GST_DEBUG_OBJECT(filter,
                       "Checking out_of_order_buffers for sequential buffers");
      while (filter->out_of_order_buffers != NULL) {
        GList *first = g_list_first(filter->out_of_order_buffers);
        GstBuffer *buf = (GstBuffer *)first->data;

        if (buf->offset == filter->next_output_offset) {
          GST_DEBUG_OBJECT(filter,
                           "Found sequential buffer with offset %zu in "
                           "out_of_order_buffers, pushing",
                           buf->offset);
          // 移除并输出
          filter->out_of_order_buffers =
              g_list_remove_link(filter->out_of_order_buffers, first);
          g_list_free_1(first);
          GST_DEBUG_OBJECT(filter,
                           "Sequential buffer with offset %zu pushed, next "
                           "output offset is now %zu",
                           buf->offset, filter->next_output_offset);
          log_buffer_info(rgb_buf);
          gst_pad_push(filter->srcpad, buf);
          filter->next_output_offset++;

        } else {
          // 如果第一个 buffer 都不是下一个应该输出的，那么后面的也不会是
          GST_DEBUG_OBJECT(filter,
                           "Next buffer in out_of_order_buffers (offset %zu) "
                           "is not sequential (expected %zu), breaking",
                           buf->offset, filter->next_output_offset);
          break;
        }
      }
    } else {
      GST_DEBUG_OBJECT(filter,
                       "Buffer offset %zu does not match next output offset "
                       "%zu, storing in out_of_order_buffers",
                       rgb_buf->offset, filter->next_output_offset);
      // 将 buffer 存储在 out_of_order_buffers 中
      filter->out_of_order_buffers = g_list_insert_sorted(
          filter->out_of_order_buffers, rgb_buf, buffer_offset_compare);
      GST_DEBUG_OBJECT(filter,
                       "Buffer with offset %zu stored in out_of_order_buffers",
                       rgb_buf->offset);
    }
  }

  return NULL;
}

// 以下是 GStreamer Plugin的模板框架，没有实质逻辑

enum {
  PROP_0,
  PROP_MODEL_PATH,
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
  GST_LOG_OBJECT(filter, "Initializing GstPluginRknn element");

  // Initialize all pointers to NULL first
  filter->sinkpad = NULL;
  filter->srcpad = NULL;
  filter->model_path = NULL;
  filter->sink_caps = NULL;
  filter->src_caps = NULL;
  filter->rknn_engines = NULL;
  filter->workers = DEFAULT_WORKERS;            // 使用默认的 workers 数量
  filter->mppjpegdec_offset_workaround = FALSE; // 默认值为 FALSE
  filter->draw_boxes = TRUE;                    // 默认值为 TRUE
  filter->resize_mode = g_strdup("pad");        // 默认值为 "pad"
  filter->model_width = 0;
  filter->model_height = 0;

  // 初始化输入格式信息
  filter->sink_format = GST_VIDEO_FORMAT_UNKNOWN;
  filter->img_width = 0;
  filter->img_height = 0;

  GST_DEBUG_OBJECT(filter, "Pointers initialized to NULL");

  // Create sink pad
  filter->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  GST_DEBUG_OBJECT(filter, "Sink pad creation attempted, result: %p",
                   filter->sinkpad);

  if (!filter->sinkpad) {
    GST_ERROR_OBJECT(filter, "Failed to create sink pad");
    return;
  }

  gst_pad_set_event_function(filter->sinkpad,
                             GST_DEBUG_FUNCPTR(gst_plugin_rknn_sink_event));
  GST_DEBUG_OBJECT(filter, "Sink pad event function set");
  gst_pad_set_chain_function(filter->sinkpad,
                             GST_DEBUG_FUNCPTR(gst_plugin_rknn_chain));
  GST_DEBUG_OBJECT(filter, "Sink pad chain function set");

  if (!gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad)) {
    GST_ERROR_OBJECT(filter, "Failed to add sink pad to element");
    gst_object_unref(filter->sinkpad);
    filter->sinkpad = NULL;
    return;
  }

  GST_DEBUG_OBJECT(filter, "Sink pad added to element");

  // Create source pad
  filter->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  GST_DEBUG_OBJECT(filter, "Source pad creation attempted, result: %p",
                   filter->srcpad);

  if (!filter->srcpad) {
    GST_ERROR_OBJECT(filter, "Failed to create src pad");
    return;
  }

  if (!gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad)) {
    GST_ERROR_OBJECT(filter, "Failed to add src pad to element");
    gst_object_unref(filter->srcpad);
    filter->srcpad = NULL;
    return;
  }

  GST_DEBUG_OBJECT(filter, "Source pad added to element");

  // Initialize queues
  filter->rknn_input_queue = g_async_queue_new();
  filter->rknn_output_queue = g_async_queue_new();
  filter->raw_input_queue = g_async_queue_new();
  filter->raw_output_queue = g_async_queue_new();
  filter->next_output_offset = 0;
  filter->out_of_order_buffers = NULL;
  filter->output_collector_thread = NULL;

  GST_LOG_OBJECT(filter, "GstPluginRknn element initialized successfully");
}

static void gst_plugin_rknn_set_property(GObject *object, guint prop_id,
                                         const GValue *value,
                                         GParamSpec *pspec) {
  GstPluginRknn *filter = GST_PLUGIN_RKNN(object);

  GST_DEBUG_OBJECT(filter, "set_property called with prop_id %d", prop_id);

  switch (prop_id) {
  case PROP_MODEL_PATH:
    GST_DEBUG_OBJECT(filter, "Setting model_path property");
    if (filter->model_path)
      g_free(filter->model_path);
    filter->model_path = g_value_dup_string(value);
    GST_DEBUG_OBJECT(filter, "model_path set to %s", filter->model_path);
    break;
  case PROP_WORKERS:
    GST_DEBUG_OBJECT(filter, "Setting workers property");
    filter->workers = g_value_get_int(value);
    GST_DEBUG_OBJECT(filter, "workers set to %d", filter->workers);
    break;
  case PROP_MPPJPEGDEC_OFFSET_WORKAROUND:
    GST_DEBUG_OBJECT(filter, "Setting mppjpegdec_offset_workaround property");
    filter->mppjpegdec_offset_workaround = g_value_get_boolean(value);
    GST_DEBUG_OBJECT(filter, "mppjpegdec_offset_workaround set to %s",
                     filter->mppjpegdec_offset_workaround ? "TRUE" : "FALSE");
    break;
  case PROP_DRAW_BOXES:
    GST_DEBUG_OBJECT(filter, "Setting draw_boxes property");
    filter->draw_boxes = g_value_get_boolean(value);
    GST_DEBUG_OBJECT(filter, "draw_boxes set to %s",
                     filter->draw_boxes ? "TRUE" : "FALSE");
    break;
  case PROP_RESIZE_MODE:
    GST_DEBUG_OBJECT(filter, "Setting resize_mode property");
    if (filter->resize_mode)
      g_free(filter->resize_mode);
    filter->resize_mode = g_value_dup_string(value);
    GST_DEBUG_OBJECT(filter, "resize_mode set to %s", filter->resize_mode);
    break;
  default:
    GST_WARNING_OBJECT(filter, "Invalid property ID: %d", prop_id);
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_plugin_rknn_get_property(GObject *object, guint prop_id,
                                         GValue *value, GParamSpec *pspec) {
  GstPluginRknn *filter = GST_PLUGIN_RKNN(object);

  GST_DEBUG_OBJECT(filter, "get_property called with prop_id %d", prop_id);

  switch (prop_id) {
  case PROP_MODEL_PATH:
    GST_DEBUG_OBJECT(filter, "Getting model_path property: %s",
                     filter->model_path);
    g_value_set_string(value, filter->model_path);
    break;
  case PROP_WORKERS:
    GST_DEBUG_OBJECT(filter, "Getting workers property: %d", filter->workers);
    g_value_set_int(value, filter->workers);
    break;
  case PROP_MPPJPEGDEC_OFFSET_WORKAROUND:
    GST_DEBUG_OBJECT(filter,
                     "Getting mppjpegdec_offset_workaround property: %s",
                     filter->mppjpegdec_offset_workaround ? "TRUE" : "FALSE");
    g_value_set_boolean(value, filter->mppjpegdec_offset_workaround);
    break;
  case PROP_DRAW_BOXES:
    GST_DEBUG_OBJECT(filter, "Getting draw_boxes property: %s",
                     filter->draw_boxes ? "TRUE" : "FALSE");
    g_value_set_boolean(value, filter->draw_boxes);
    break;
  case PROP_RESIZE_MODE:
    GST_DEBUG_OBJECT(filter, "Getting resize_mode property: %s",
                     filter->resize_mode);
    g_value_set_string(value, filter->resize_mode);
    break;
  default:
    GST_WARNING_OBJECT(filter, "Invalid property ID: %d", prop_id);
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

  GST_DEBUG_OBJECT(filter, "Received %s event: %" GST_PTR_FORMAT,
                   GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_CAPS: {
    GstCaps *caps;

    gst_event_parse_caps(event, &caps);
    if (filter->sink_caps && gst_caps_is_equal(filter->sink_caps, caps)) {
      GST_DEBUG_OBJECT(filter, "Recieved same caps event, ignore");
      ret = gst_pad_event_default(pad, parent, event);
      break;
    } else if (filter->sink_caps) {
      GST_DEBUG_OBJECT(filter, "Recieved different caps event");
      GST_DEBUG_OBJECT(filter, "Before: %" GST_PTR_FORMAT, filter->sink_caps);
      GST_DEBUG_OBJECT(filter, "After: %" GST_PTR_FORMAT, caps);
      release_rknn_resources(filter); // 这里会释放 sink_caps 的。
    }

    filter->sink_caps = gst_caps_copy(caps);
    GST_DEBUG_OBJECT(filter, "Caps event initialization stage");
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

  /* Log buffer information using the new utility function */

  GST_DEBUG_OBJECT(filter,
                   "Buffer received, size: %lu dts %zu pts %zu offset %zu",
                   gst_buffer_get_size(buf), buf->dts, buf->pts, buf->offset);
  log_buffer_info(buf);

  // // TEST
  // GstMapInfo info;
  // gst_buffer_map(buf, &info, GST_MAP_READWRITE);
  // test_draw_rectangle(buf);
  // gst_buffer_unmap(buf, &info);
  // gst_pad_push(filter->srcpad, buf);
  // return GST_FLOW_OK;
  // // DONE

  // 执行预处理
  if (!preprocess_buffer(filter, buf)) {
    GST_WARNING_OBJECT(filter, "Failed to preprocess buffer");
  }

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

  GST_LOG("Plugin debug category initialized");

  gboolean result = GST_ELEMENT_REGISTER(plugin_rknn, plugin);

  GST_LOG("Plugin registration result: %d", result);

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
