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
#include "socket_utils.h"

#include <sys/time.h>

/* Forward declarations */
static gpointer push_thread(gpointer data);
static gpointer work_thread(gpointer data);

GST_DEBUG_CATEGORY_EXTERN(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

/* 初始化 RKNN 推理引擎，并获得 RKNN 输入所需的长宽 */
gboolean init_rknn_engines(GstPluginRknn *filter) {
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
gboolean allocate_rknn_resources(GstPluginRknn *filter) {
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
  filter->push_thread = g_thread_new("output-collector", push_thread, filter);

  GST_DEBUG("allocate_state_resources end");
  return TRUE;
}

void release_rknn_resources(GstPluginRknn *filter) {
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
  struct RknnMeta *rknn_meta = &gst_buffer_get_rknn_meta(raw_buffer)->rknn_meta;
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
gpointer work_thread(gpointer data) {
  struct RknnEngine *rknn_engine = (struct RknnEngine *)data;
  GstPluginRknn *filter = GST_PLUGIN_RKNN(rknn_engine->filter);
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
    GstBuffer *img_buffer = GST_BUFFER(g_async_queue_pop(rknn_engine->rknn_input_queue));
    GST_DEBUG("Thread %d recieved buffer %zu", worker_id, img_buffer->offset);

    // 终止信号
    if (gst_buffer_get_size(img_buffer) == 0) {
      GST_INFO("Thread %d recieved ending buffer, stoping", worker_id);
      gst_buffer_unref(img_buffer);
      return NULL;
    }

    struct RknnMeta *rknn_meta =
        &gst_buffer_get_rknn_meta(img_buffer)->rknn_meta;
    rknn_engine->rknn_meta = rknn_meta;
    rknn_meta->queue_done = gst_clock_get_time(filter->clock);

    GST_TRACE("Thread %d offset %zu start infer", worker_id,
              img_buffer->offset);
    rknn_preprocess(rknn_engine, img_buffer);
    rknn_meta->preprocess_done = gst_clock_get_time(filter->clock);
    //   dump_tensor_data("preprocess", GST_BUFFER_OFFSET(img_buffer),
    //   &rknn_engine->input_attr,
    // rknn_engine->input_mem->priv_data,
    // rknn_engine->input_attr.size_with_stride);
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
        (rknn_meta->visualize_done - rknn_meta->postprocess_done) /
            GST_USECOND);
  }
}

gpointer push_thread(gpointer data) {
  GstPluginRknn *filter = GST_PLUGIN_RKNN(data);
  GST_DEBUG("Push thread start");
  while (TRUE) {
    struct RknnEngine *worker = (struct RknnEngine *)g_async_queue_pop(filter->workers_queue);
    GST_DEBUG("Push thread worker id %d", worker->worker_id);
    if (worker->worker_id == -1) {
      break;
    }
    GstBuffer *buf = GST_BUFFER(g_async_queue_pop(worker->rknn_output_queue));

    struct RknnMeta *meta = &gst_buffer_get_rknn_meta(buf)->rknn_meta;
    meta->push_pad_done = gst_clock_get_time(filter->clock);
    GST_DEBUG("Push thread pushed offset %lu", GST_BUFFER_OFFSET(buf));
    gst_pad_push(filter->srcpad, buf);
  }
  GST_DEBUG("Push thread stop");

  return NULL;
}

