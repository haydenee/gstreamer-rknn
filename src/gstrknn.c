/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) YEAR AUTHOR_NAME AUTHOR_EMAIL
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-plugin
 *
 * FIXME:Describe plugin here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! plugin ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#include "glib.h"
#include "gst/allocators/gstdmabuf.h"
#include "gst/gstbuffer.h"
#include "gst/gstinfo.h"
#include "gst/gstmemory.h"
#include "gst/gstpad.h"
#include "gst/video/video-format.h"
#include "gst/video/video-info.h"
#include "rknnprocess.h"
#include <unistd.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "dmabuffer.h"
#include "gstrknn.h"

#include "RgaUtils.h"
#include "im2d.h"
#include "rga.h"
#include "rgaprocess.h"
#include "stdio.h"
GST_DEBUG_CATEGORY_STATIC(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

/* Filter signals and args */
enum {
    /* FILL ME */
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_SILENT = 1,
    PROP_BYPASS = 2,
    PROP_MODEL_PATH = 3,
    PROP_LABEL_PATH = 4,
    PROP_SHOW_FPS = 5,
    PROP_FRAME_SKIP = 6,
} PROP_ID;

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw,"
                    "format = (string) { " PLUGIN_RKNN_SUPPORT_FORMATS " } "));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw,"
                    "format = (string) { " PLUGIN_RKNN_SUPPORT_FORMATS " } "));

#define gst_plugin_rknn_parent_class parent_class
/**
 * G_DEFINE_TYPE:
 * @TN: The name of the new type, in Camel case.
 * @t_n: The name of the new type, in lowercase, with words
 *  separated by `_`.
 * @T_P: The #GType of the parent type.
 */
G_DEFINE_TYPE(GstPluginRknn, gst_plugin_rknn, GST_TYPE_ELEMENT);

/**
 * GST_ELEMENT_REGISTER_DEFINE:
 * @e: The element name in lower case, with words separated by '_'.
 * Used to generate `gst_element_register_*(GstPlugin* plugin)`.
 * @e_n: The public name of the element
 * @r: The #GstRank of the element (higher rank means more importance when autoplugging, see #GstRank)
 * @t: The #GType of the element.
 */
GST_ELEMENT_REGISTER_DEFINE(plugin_rknn, "rknn", GST_RANK_NONE,
    GST_TYPE_PLUGIN_RKNN);

static void gst_plugin_rknn_set_property(GObject* object,
    guint prop_id, const GValue* value, GParamSpec* pspec);
static void gst_plugin_rknn_get_property(GObject* object,
    guint prop_id, GValue* value, GParamSpec* pspec);

static gboolean gst_plugin_rknn_sink_event(GstPad* pad,
    GstObject* parent, GstEvent* event);
static gboolean gst_plugin_rknn_src_event(GstPad* pad,
    GstObject* parent, GstEvent* event);
static GstFlowReturn gst_plugin_rknn_chain(GstPad* pad,
    GstObject* parent, GstBuffer* buf);

/* GObject vmethod implementations */

/* initialize the plugin's class */
static void
gst_plugin_rknn_finalize(GObject* object)
{
    GstPluginRknn* filter = GST_PLUGIN_RKNN(object);
    if (filter->task_data) {
        filter->task_data->stop = TRUE;
        // Use an empty buffer as sentinel instead of NULL
        g_async_queue_push(filter->queue, gst_buffer_new());
        g_thread_join(filter->task_thread);
        g_free(filter->task_data);
    }

    if (filter->sink_caps)
        gst_caps_unref(filter->sink_caps);
    if (filter->src_caps)
        gst_caps_unref(filter->src_caps);
    G_OBJECT_CLASS(gst_plugin_rknn_parent_class)->finalize(object);

    for (int i = 0; i < MAX_DMABUF_INSTANCES; ++i) {
        if (filter->cached_dmabuf_ptr[i])
            dmabuf_munmap(filter->cached_dmabuf_ptr[i], filter->cached_dmabuf_size[i]);
        if (filter->cached_dmabuf_fd[i] >= 0)
            close(filter->cached_dmabuf_fd[i]);
        if (filter->cached_allocator[i])
            gst_object_unref(filter->cached_allocator[i]);
        if (filter->cached_dmabuf_mem[i])
            gst_memory_unref(filter->cached_dmabuf_mem[i]);
    }
    if (filter->dma_heap_fd >= 0) {
        dmabuf_heap_close(filter->dma_heap_fd);
        filter->dma_heap_fd = -1;
    }

    if (filter->rknn_process.model_path)
        g_free(filter->rknn_process.model_path);

    rknn_release(&filter->rknn_process);
}

static void
gst_plugin_rknn_class_init(GstPluginRknnClass* klass)
{
    GObjectClass* gobject_class;
    GstElementClass* gstelement_class;

    gobject_class = (GObjectClass*)klass;
    gstelement_class = (GstElementClass*)klass;

    gobject_class->set_property = gst_plugin_rknn_set_property;
    gobject_class->get_property = gst_plugin_rknn_get_property;
    gobject_class->finalize = (GObjectFinalizeFunc)gst_plugin_rknn_finalize;

    g_object_class_install_property(gobject_class, PROP_SILENT,
        g_param_spec_boolean("silent", "Silent", "Produce verbose output ?",
            TRUE, G_PARAM_READWRITE));
    g_object_class_install_property(gobject_class, PROP_BYPASS,
        g_param_spec_boolean("bypass", "Bypass",
            "Bypass the filter and just pass through the data",
            FALSE, G_PARAM_READWRITE));
    g_object_class_install_property(gobject_class, PROP_MODEL_PATH,
        g_param_spec_string("model-path", "Model Path",
            "Path to the RKNN model file",
            NULL, G_PARAM_READWRITE));
    g_object_class_install_property(gobject_class, PROP_LABEL_PATH, // Add this block
        g_param_spec_string("label-path", "Label Path",
            "Path to the label file",
            NULL, G_PARAM_READWRITE));
    g_object_class_install_property(gobject_class, PROP_SHOW_FPS,
        g_param_spec_boolean("show-fps", "Show FPS",
            "Display FPS on output frames",
            FALSE, G_PARAM_READWRITE));
    g_object_class_install_property(gobject_class, PROP_FRAME_SKIP,
        g_param_spec_int("frame-skip", "Frame Skip",
            "Number of frames to skip between inferences (0=no skip)",
            0, G_MAXINT, 0, G_PARAM_READWRITE));
    gst_element_class_set_details_simple(gstelement_class,
        "Rknn Plugin",
        "FIXME: Rknn Plugin classification",
        "FIXME: Rknn Plugin description", "haydenee <lth0320@163.com>");

    gst_element_class_add_pad_template(gstelement_class,
        gst_static_pad_template_get(&src_factory));
    gst_element_class_add_pad_template(gstelement_class,
        gst_static_pad_template_get(&sink_factory));
}

static gpointer rknn_task_func(gpointer data);

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_plugin_rknn_init(GstPluginRknn* filter)
{
    filter->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
    gst_pad_set_event_function(filter->sinkpad,
        GST_DEBUG_FUNCPTR(gst_plugin_rknn_sink_event));
    gst_pad_set_chain_function(filter->sinkpad,
        GST_DEBUG_FUNCPTR(gst_plugin_rknn_chain));
    gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

    filter->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
    gst_pad_set_event_function(filter->srcpad,
        GST_DEBUG_FUNCPTR(gst_plugin_rknn_src_event));
    gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

    filter->silent = FALSE;
    filter->bypass = FALSE;
    filter->sink_caps = NULL;
    filter->src_caps = NULL;

    filter->dma_heap_fd = dmabuf_heap_open();
    if (filter->dma_heap_fd < 0) {
        GST_ERROR_OBJECT(filter, "Failed to open DMA-BUF heap");
    } else {
        GST_INFO_OBJECT(filter, "Opened DMA-BUF heap, fd = %d", filter->dma_heap_fd);
    }
    filter->src_buffer_index = 0;

    filter->rknn_model_loaded = FALSE;
    if (filter->rknn_process.model_path) {
        GST_INFO_OBJECT(filter, "Using RKNN model: %s", filter->rknn_process.model_path);
        rknn_prepare(&filter->rknn_process);
        GST_INFO_OBJECT(filter, "RKNN model loaded: width=%d, height=%d, channel=%d",
            filter->rknn_process.model_width, filter->rknn_process.model_height, filter->rknn_process.model_channel);
        filter->rknn_model_loaded = TRUE;
    } else {
        GST_INFO_OBJECT(filter, "No RKNN model path specified");
    }

    for (int i = 0; i < MAX_DMABUF_INSTANCES; ++i) {
        filter->cached_dmabuf_fd[i] = -1;
        filter->cached_dmabuf_ptr[i] = NULL;
        filter->cached_dmabuf_size[i] = 0;
        filter->cached_allocator[i] = NULL;
        filter->cached_dmabuf_mem[i] = NULL;
    }

    filter->show_fps = FALSE;
    filter->fps_start_time = 0;
    filter->fps_frame_count = 0;
    filter->current_fps = 0.0;
    filter->fps_update_interval = 1000000; // 1 second in microseconds

    // 初始化跳帧推理相关变量
    filter->frame_skip = 0; // 默认不跳帧
    filter->frame_counter = 0; // 从0开始计数
    filter->need_inference = TRUE; // 第一帧默认做推理

    // for rknn task
    filter->queue = g_async_queue_new();
    GST_LOG_OBJECT(filter, "gst_plugin_rknn_init");
    filter->task_data = g_new0(RknnTaskData, 1);
    filter->task_data->filter = filter;
    filter->task_data->stop = FALSE;
    filter->task_thread = g_thread_new(
        "rknn_task_thread", rknn_task_func, filter->task_data);
}

static void
gst_plugin_rknn_set_property(GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    GstPluginRknn* filter = GST_PLUGIN_RKNN(object);

    switch (prop_id) {
    case PROP_SILENT:
        filter->silent = g_value_get_boolean(value);
        break;
    case PROP_BYPASS:
        filter->bypass = g_value_get_boolean(value);
        break;
    case PROP_MODEL_PATH:
        if (filter->rknn_process.model_path)
            g_free(filter->rknn_process.model_path);
        filter->rknn_process.model_path = g_value_dup_string(value);
        GST_INFO_OBJECT(filter, "Set RKNN model path to: %s",
            filter->rknn_process.model_path);
        break;
    case PROP_LABEL_PATH:
        if (filter->rknn_process.label_path)
            g_free(filter->rknn_process.label_path);
        filter->rknn_process.label_path = g_value_dup_string(value);
        GST_INFO_OBJECT(filter, "Set RKNN label path to: %s",
            filter->rknn_process.label_path);
        break;
    case PROP_SHOW_FPS:
        filter->show_fps = g_value_get_boolean(value);
        GST_INFO_OBJECT(filter, "Set show FPS to: %s",
            filter->show_fps ? "TRUE" : "FALSE");
        break;
    case PROP_FRAME_SKIP:
        filter->frame_skip = g_value_get_int(value);
        GST_INFO_OBJECT(filter, "Set frame skip to: %d",
            filter->frame_skip);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_plugin_rknn_get_property(GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec)
{
    GstPluginRknn* filter = GST_PLUGIN_RKNN(object);

    switch (prop_id) {
    case PROP_SILENT:
        g_value_set_boolean(value, filter->silent);
        break;
    case PROP_BYPASS:
        g_value_set_boolean(value, filter->bypass);
        break;
    case PROP_MODEL_PATH:
        g_value_set_string(value, (const gchar*)filter->rknn_process.model_path);
        break;
    case PROP_LABEL_PATH:
        g_value_set_string(value, (const gchar*)filter->rknn_process.label_path);
        break;
    case PROP_SHOW_FPS:
        g_value_set_boolean(value, filter->show_fps);
        break;
    case PROP_FRAME_SKIP:
        g_value_set_int(value, filter->frame_skip);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* GstElement vmethod implementations */

/* Helper function to ensure RGB caps are set on the source pad */
static gboolean
gst_plugin_rknn_ensure_rgb_caps(GstPluginRknn* filter)
{
    if (!filter->aligned_width || !filter->aligned_height) {
        GST_WARNING_OBJECT(filter, "Sink dimensions not yet known, can't set RGB caps");
        return FALSE;
    }

    GstCaps* rgb_caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "RGB",
        "width", G_TYPE_INT, filter->aligned_width,
        "height", G_TYPE_INT, filter->aligned_height,
        NULL);

    gboolean ret = gst_pad_set_caps(filter->srcpad, rgb_caps);

    if (ret) {
        GST_INFO_OBJECT(filter, "Successfully set RGB caps: %" GST_PTR_FORMAT, rgb_caps);
        if (filter->src_caps)
            gst_caps_unref(filter->src_caps);
        filter->src_caps = rgb_caps; // Transfer ownership
    } else {
        GST_ERROR_OBJECT(filter, "Failed to set RGB caps: %" GST_PTR_FORMAT, rgb_caps);
        gst_caps_unref(rgb_caps);
    }

    return ret;
}

/* this function handles sink events */
static gboolean
gst_plugin_rknn_sink_event(GstPad* pad, GstObject* parent,
    GstEvent* event)
{
    GstPluginRknn* filter;
    gboolean ret;

    filter = GST_PLUGIN_RKNN(parent);

    GST_LOG_OBJECT(filter, "Received %s event: %" GST_PTR_FORMAT,
        GST_EVENT_TYPE_NAME(event), event);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
        GstCaps* caps;

        gst_event_parse_caps(event, &caps);
        /* do something with the caps */

        if (filter->sink_caps)
            gst_caps_unref(filter->sink_caps);
        filter->sink_caps = gst_caps_copy(caps);
        GST_INFO_OBJECT(filter, "Negotiated sink caps: %" GST_PTR_FORMAT, filter->sink_caps);
        if (gst_video_info_from_caps(&filter->sink_info, filter->sink_caps)) {
            filter->sink_width = GST_VIDEO_INFO_WIDTH(&filter->sink_info);
            filter->sink_height = GST_VIDEO_INFO_HEIGHT(&filter->sink_info);
            filter->sink_format = GST_VIDEO_INFO_FORMAT(&filter->sink_info);
            filter->sink_rga_format = gst_to_rga_format(filter->sink_format);

            // 立即计算对齐后的尺寸
            filter->aligned_width = ALIGN_UP(filter->sink_width, 16);
            filter->aligned_height = ALIGN_UP(filter->sink_height, 16);

            GST_INFO_OBJECT(filter, "Sink pad video info: width=%d, height=%d, format=%s, aligned: %dx%d",
                filter->sink_width, filter->sink_height,
                gst_video_format_to_string(filter->sink_format),
                filter->aligned_width, filter->aligned_height);

            if (!gst_plugin_rknn_ensure_rgb_caps(filter)) {
                GST_ERROR_OBJECT(filter, "Failed to set RGB caps after sink caps negotiation");
                return FALSE;
            }
        } else {
            GST_ERROR_OBJECT(filter, "Failed to parse video info from sink caps");
        }

        /* and forward */
        ret = gst_pad_event_default(pad, parent, event);
        break;
    }
    default:
        ret = gst_pad_event_default(pad, parent, event);
        break;
    }
    return ret;
}

static gboolean
gst_plugin_rknn_src_event(GstPad* pad, GstObject* parent, GstEvent* event)
{
    GstPluginRknn* filter = GST_PLUGIN_RKNN(parent);
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
        GstCaps* caps;
        gst_event_parse_caps(event, &caps);

        if (filter->src_caps)
            gst_caps_unref(filter->src_caps);
        filter->src_caps = gst_caps_copy(caps);

        GST_INFO_OBJECT(filter, "Src pad negotiated caps: %" GST_PTR_FORMAT, filter->src_caps);

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
static GstFlowReturn
gst_plugin_rknn_chain(GstPad* pad, GstObject* parent, GstBuffer* buf)
{
    // passed in buf already has a ref count of 1. If we pass out a buffer, it also needs a ref count of 1.
    GstPluginRknn* filter;

    filter = GST_PLUGIN_RKNN(parent);

    g_async_queue_push(filter->queue, gst_buffer_ref(buf));

    // Drop the oldest buffer if the queue is too long
    if (g_async_queue_length(filter->queue) > MAX_QUEUE_LENGTH) {
        GstBuffer* old_buf = g_async_queue_try_pop(filter->queue);
        if (old_buf) {
            // This buffer is not going to be passed, so need to unref twice
            gst_buffer_unref(old_buf);
            gst_buffer_unref(old_buf);
        }

        if (g_get_monotonic_time() - filter->last_buffer_full_log_time > 1000000) {
            GST_WARNING_OBJECT(filter, "Queue full, dropping oldest buffer. Queue length: %d",
                g_async_queue_length(filter->queue));
            filter->last_buffer_full_log_time = g_get_monotonic_time();
        }
    }

    /* just push out the incoming buffer without touching it */
    return GST_FLOW_OK;
}

// Add FPS calculation function
static void calculate_fps(GstPluginRknn* filter)
{
    gint64 current_time = g_get_monotonic_time();

    if (filter->fps_start_time == 0) {
        filter->fps_start_time = current_time;
        filter->fps_frame_count = 0;
    }

    filter->fps_frame_count++;

    gint64 elapsed = current_time - filter->fps_start_time;
    if (elapsed >= filter->fps_update_interval) {
        filter->current_fps = (gdouble)filter->fps_frame_count * 1000000.0 / elapsed;
        filter->fps_start_time = current_time;
        filter->fps_frame_count = 0;
    }
}
gboolean prepare_dmabuf_memory(GstPluginRknn* filter, int index, gsize mem_size, GstMemory** mem)
{
    if (index < 0 || index >= MAX_DMABUF_INSTANCES)
        return FALSE;

    // 如果已分配且大小一致，直接复用
    if (filter->cached_dmabuf_fd[index] >= 0 && filter->cached_dmabuf_size[index] == mem_size && filter->cached_dmabuf_mem[index]) {
        *mem = filter->cached_dmabuf_mem[index];
        if (!gst_memory_is_writable(*mem)) {
            GST_ERROR_OBJECT(filter, "DMABUF memory fd %d is not writable",
                filter->cached_dmabuf_fd[index]);
        }
        return TRUE;
    }

    // 释放旧的
    if (filter->cached_dmabuf_ptr[index])
        dmabuf_munmap(filter->cached_dmabuf_ptr[index], filter->cached_dmabuf_size[index]);
    if (filter->cached_dmabuf_fd[index] >= 0)
        close(filter->cached_dmabuf_fd[index]);
    if (filter->cached_allocator[index])
        gst_object_unref(filter->cached_allocator[index]);
    if (filter->cached_dmabuf_mem[index])
        gst_memory_unref(filter->cached_dmabuf_mem[index]);

    // 分配新的
    filter->cached_dmabuf_fd[index] = dmabuf_heap_alloc(filter->dma_heap_fd, NULL, mem_size);
    if (filter->cached_dmabuf_fd[index] < 0) {
        GST_ERROR_OBJECT(filter, "Failed to allocate DMABUF fd from heap fd %d, size %zu",
            filter->dma_heap_fd, mem_size);
        return FALSE;
    }
    GST_DEBUG_OBJECT(filter, "Allocated DMABUF fd %d for index %d, size %zu", filter->cached_dmabuf_fd[index], index, mem_size);
    filter->cached_dmabuf_ptr[index] = dmabuf_mmap(filter->cached_dmabuf_fd[index], mem_size);
    if (!filter->cached_dmabuf_ptr[index]) {
        close(filter->cached_dmabuf_fd[index]);
        filter->cached_dmabuf_fd[index] = -1;
        GST_ERROR_OBJECT(filter, "Failed to mmap DMABUF fd %d, size %zu", filter->cached_dmabuf_fd[index], mem_size);
        return FALSE;
    }
    filter->cached_allocator[index] = gst_dmabuf_allocator_new();
    if (!filter->cached_allocator[index]) {
        dmabuf_munmap(filter->cached_dmabuf_ptr[index], mem_size);
        close(filter->cached_dmabuf_fd[index]);
        filter->cached_dmabuf_fd[index] = -1;
        GST_ERROR_OBJECT(filter, "Failed to create DMABUF allocator");
        return FALSE;
    }
    filter->cached_dmabuf_mem[index] = gst_dmabuf_allocator_alloc(filter->cached_allocator[index], filter->cached_dmabuf_fd[index], mem_size);
    if (!filter->cached_dmabuf_mem[index]) {
        gst_object_unref(filter->cached_allocator[index]);
        filter->cached_allocator[index] = NULL;
        dmabuf_munmap(filter->cached_dmabuf_ptr[index], mem_size);
        close(filter->cached_dmabuf_fd[index]);
        filter->cached_dmabuf_fd[index] = -1;
        GST_ERROR_OBJECT(filter, "Failed to allocate DMABUF memory from fd %d, size %zu",
            filter->cached_dmabuf_fd[index], mem_size);
        return FALSE;
    }
    filter->cached_dmabuf_size[index] = mem_size;
    *mem = filter->cached_dmabuf_mem[index];
    return TRUE;
}
static gpointer rknn_task_func(gpointer data)
{
    int ret = GST_FLOW_OK;
    RknnTaskData* task_data = (RknnTaskData*)data;
    GstPluginRknn* filter = task_data->filter;
    GST_INFO_OBJECT(filter, "Starting rknn task thread");
    while (!task_data->stop) {

        if (filter->rknn_process.model_path && !filter->rknn_model_loaded) {
            GST_INFO_OBJECT(filter, "Using RKNN model: %s", filter->rknn_process.model_path);
            rknn_prepare(&filter->rknn_process);
            GST_INFO_OBJECT(filter, "RKNN model loaded: width=%d, height=%d, channel=%d",
                filter->rknn_process.model_width, filter->rknn_process.model_height, filter->rknn_process.model_channel);
            filter->rknn_model_loaded = TRUE;
        }

        GstBuffer* buf = g_async_queue_pop(filter->queue);
        gint64 current_time = g_get_monotonic_time();

        if (task_data->stop) {
            GST_INFO_OBJECT(filter, "Stopping rknn task thread");
            gst_buffer_unref(buf);
            gst_buffer_unref(buf);
            break;
        }

        if (!buf) {
            GST_WARNING_OBJECT(filter, "Received NULL buffer");
            continue;
        }

        if (filter->silent == FALSE)
            g_print("I'm plugged, therefore I'm in.\n");

        if (filter->bypass) {
            /* if we are in bypass mode, just push the buffer out */
            GST_LOG_OBJECT(filter, "Bypassing the filter, pushing buffer out");
            gst_pad_push(filter->srcpad, buf);
            if (ret != GST_FLOW_OK) {
                GST_WARNING_OBJECT(filter, "Failed to push buffer, ret = %d", ret);
            }
            gst_buffer_unref(buf);
            continue;
        }

        // start of processing

        // 处理跳帧推理逻辑
        filter->need_inference = FALSE;
        if (filter->frame_skip == 0 || filter->frame_counter % (filter->frame_skip + 1) == 0) {
            filter->need_inference = TRUE;
        }

        // 更新帧计数器
        filter->frame_counter++;
        if (filter->frame_counter > 1000000) { // 避免溢出
            filter->frame_counter = 0;
        }

        // Normalize to DMABUF memory
        GstMemory* mem_in = gst_buffer_peek_memory(buf, 0);
        GstMemory* mem_in_dmabuf = NULL;
        gsize mem_size = 0;
        if (!mem_in) {
            GST_ERROR_OBJECT(filter, "Failed to get memory from buffer");
            gst_buffer_unref(buf);
            gst_buffer_unref(buf);
            continue;
        }
        if (gst_is_dmabuf_memory(mem_in)) {
            GST_DEBUG_OBJECT(filter, "Processing DMABUF memory");
            // no conversion needed
            mem_in_dmabuf = mem_in;
            mem_size = gst_memory_get_sizes(mem_in, NULL, NULL);
        } else {
            GST_DEBUG_OBJECT(filter, "Processing non-DMABUF memory");
            // will convert to DMABUF memory
            mem_size = gst_memory_get_sizes(mem_in, NULL, NULL);
            if (!prepare_dmabuf_memory(filter, 0, mem_size, &mem_in_dmabuf)) {
                GST_ERROR_OBJECT(filter, "Failed to prepare DMABUF memory");
                gst_buffer_unref(buf);
                gst_buffer_unref(buf);
                continue;
            }
            if (!mem_in_dmabuf) {
                GST_ERROR_OBJECT(filter, "Failed to get DMABUF memory");
                gst_buffer_unref(buf);
                gst_buffer_unref(buf);
                continue;
            }
            GstMapInfo map_info;
            if (!gst_memory_map(mem_in, &map_info, GST_MAP_READ)) {
                GST_ERROR_OBJECT(filter, "Failed to map input memory");
                gst_buffer_unref(buf);
                gst_buffer_unref(buf);
                continue;
            }
            // Copy data to DMABUF memory
            dmabuf_sync_start(gst_dmabuf_memory_get_fd(mem_in_dmabuf));
            memcpy(filter->cached_dmabuf_ptr[0], map_info.data, mem_size);
            dmabuf_sync_stop(gst_dmabuf_memory_get_fd(mem_in_dmabuf));
            gst_memory_unmap(mem_in, &map_info);
            // GST_DEBUG_OBJECT(filter, "Copied %zu bytes to DMABUF memory fd %d",
            //     mem_size, gst_dmabuf_memory_get_fd(mem_in_dmabuf));
        }

        if (filter->rknn_model_loaded == FALSE) {
            GST_ERROR_OBJECT(filter, "RKNN model not loaded, skipping processing");
            gst_buffer_unref(buf);
            gst_buffer_unref(buf);
            continue;
        }

        GstMemory* mem_rknn_in = NULL;
        if (!prepare_dmabuf_memory(filter, 1, filter->rknn_process.model_width * filter->rknn_process.model_height * filter->rknn_process.model_channel, &mem_rknn_in)) {
            GST_ERROR_OBJECT(filter, "Failed to prepare RKNN input memory");
            gst_buffer_unref(buf);
            gst_buffer_unref(buf);
            continue;
        }

        GstMemory* mem_in_rgb = NULL;

        filter->src_buffer_index = (filter->src_buffer_index + 1) % (MAX_DMABUF_INSTANCES - 2);

        if (!prepare_dmabuf_memory(filter, 2 + filter->src_buffer_index, calc_buffer_size(filter->aligned_width, filter->aligned_height, GST_VIDEO_FORMAT_RGB), &mem_in_rgb)) {
            GST_ERROR_OBJECT(filter, "Failed to prepare RGB memory for RGA");
            gst_buffer_unref(buf);
            gst_buffer_unref(buf);
            continue;
        }

        // rga improcess options
        im_opt_t opt;
        memset(&opt, 0, sizeof(opt));
        int usage = IM_SYNC;
        int ret;

        // yuv rgb conversion
        GST_DEBUG_OBJECT(filter, "gst_dmabuf_memory_get_fd(mem_in_dmabuf) = %d, rknn_input_mem fd = %d",
            gst_dmabuf_memory_get_fd(mem_in_dmabuf), gst_dmabuf_memory_get_fd(mem_rknn_in));
        rga_buffer_t rga_buf_in_dmabuf = wrapbuffer_fd_t(gst_dmabuf_memory_get_fd(mem_in_dmabuf), filter->sink_width, filter->sink_height, filter->sink_width, filter->sink_height, filter->sink_rga_format);
        im_rect rect_in_dmabuf = { 0, 0, filter->sink_width, filter->sink_height };
        rga_buffer_t rga_buf_in_rgb = wrapbuffer_fd_t(gst_dmabuf_memory_get_fd(mem_in_rgb), filter->aligned_width, filter->aligned_height, filter->aligned_width, filter->aligned_height, RK_FORMAT_RGB_888);
        im_rect rect_in_rgb = { 0, 0, filter->sink_width, filter->sink_height };
        rga_buffer_t pat = wrapbuffer_virtualaddr_t(NULL, 0, 0, 0, 0, RK_FORMAT_RGB_888); // pattern unused
        im_rect rect_pat = { 0, 0, 0, 0 }; // pattern rect unused
        dmabuf_sync_start(gst_dmabuf_memory_get_fd(mem_in_dmabuf));
        dmabuf_sync_start(gst_dmabuf_memory_get_fd(mem_in_rgb));
        ret = improcess(rga_buf_in_dmabuf, rga_buf_in_rgb, // src, dst buffers
            pat, // pattern buffer unused
            rect_in_dmabuf, rect_in_rgb, rect_pat,
            usage);
        dmabuf_sync_stop(gst_dmabuf_memory_get_fd(mem_in_dmabuf));
        dmabuf_sync_stop(gst_dmabuf_memory_get_fd(mem_in_rgb));
        if (ret != IM_STATUS_SUCCESS) {
            GST_ERROR_OBJECT(filter, "RGA RGB conversion failed: %s", imStrError(ret));
            gst_buffer_unref(buf);
            gst_buffer_unref(buf);
            continue;
        }

        if (filter->need_inference) {
            // rga letterboxing
            rga_buffer_t rga_buf_rknn_input = wrapbuffer_fd_t(gst_dmabuf_memory_get_fd(mem_rknn_in), filter->rknn_process.model_width, filter->rknn_process.model_height, filter->rknn_process.model_width, filter->rknn_process.model_height, RK_FORMAT_RGB_888);
            // Define letterboxing rectangles
            // Calculate scaling ratio to maintain aspect ratio
            float scale_w = (float)filter->rknn_process.model_width / filter->sink_width;
            float scale_h = (float)filter->rknn_process.model_height / filter->sink_height;
            float scale = scale_w < scale_h ? scale_w : scale_h;
            int new_w = (int)(filter->sink_width * scale + 0.5f);
            int new_h = (int)(filter->sink_height * scale + 0.5f);
            int offset_x = (filter->rknn_process.model_width - new_w) / 2;
            int offset_y = (filter->rknn_process.model_height - new_h) / 2;
            im_rect rect_rknn_input = { offset_x, offset_y, new_w, new_h };
            filter->rknn_process.pads.left = offset_x;
            filter->rknn_process.pads.right = offset_x + new_w;
            filter->rknn_process.pads.top = offset_y;
            filter->rknn_process.pads.bottom = offset_y + new_h;
            filter->rknn_process.scale_w = scale;
            filter->rknn_process.scale_h = scale;
            filter->rknn_process.original_width = filter->sink_width;
            filter->rknn_process.original_height = filter->sink_height;
            GST_DEBUG_OBJECT(filter, "RGA letterboxing: src_rect=(%d,%d,%d,%d), dst_rect=(%d,%d,%d,%d)",
                rect_in_dmabuf.x, rect_in_dmabuf.y, rect_in_dmabuf.width, rect_in_dmabuf.height,
                rect_rknn_input.x, rect_rknn_input.y, rect_rknn_input.width, rect_rknn_input.height);

            dmabuf_sync_start(gst_dmabuf_memory_get_fd(mem_in_rgb));
            dmabuf_sync_start(gst_dmabuf_memory_get_fd(mem_rknn_in));
            ret = improcess(rga_buf_in_rgb, rga_buf_rknn_input, // src, dst buffers
                pat, // pattern buffer unused
                rect_in_rgb, rect_rknn_input, rect_pat,
                usage);
            dmabuf_sync_stop(gst_dmabuf_memory_get_fd(mem_in_rgb));
            dmabuf_sync_stop(gst_dmabuf_memory_get_fd(mem_rknn_in));
            if (ret != IM_STATUS_SUCCESS) {
                GST_ERROR_OBJECT(filter, "RGA letterboxing failed: %s", imStrError(ret));
                gst_buffer_unref(buf);
                gst_buffer_unref(buf);
                continue;
            }
        }
        // Save RGB result as BMP (24-bit) for debugging
        // save_rgb_to_bmp("out.bmp", (unsigned char*)(filter->cached_dmabuf_ptr[1]), filter->model_width, filter->model_height);

        // gst_buffer_unref(buf);

        filter->rknn_process.inputs[0].buf = filter->cached_dmabuf_ptr[1];
        // Calculate FPS
        calculate_fps(filter);

        dmabuf_sync_start(gst_dmabuf_memory_get_fd(mem_in_rgb));
        if (filter->need_inference) {
            // 执行推理
            rknn_inference_and_postprocess(&filter->rknn_process, filter->cached_dmabuf_ptr[2 + filter->src_buffer_index], 0.6, 0.45, filter->show_fps ? 1 : 0, filter->current_fps, 1);
        } else {
            // 不推理时，直接将上一帧的结果绘制到当前帧
            rknn_inference_and_postprocess(&filter->rknn_process, filter->cached_dmabuf_ptr[2 + filter->src_buffer_index], 0.6, 0.45, filter->show_fps ? 1 : 0, filter->current_fps, 0);
        }
        dmabuf_sync_stop(gst_dmabuf_memory_get_fd(mem_in_rgb));
        // check refcount of out gst memory

        if (!filter->src_buffer) {
            filter->src_buffer = gst_buffer_new(); // src_buffer refcount=1
        }
        if (gst_buffer_n_memory(filter->src_buffer) == 0) {
            GstMemory* mem_in_rgb_ref = gst_memory_ref(mem_in_rgb); // mem_in_rgb refcount=2
            gst_buffer_append_memory(filter->src_buffer, mem_in_rgb_ref); // mem_in_rgb refcount=2, mem_in_rgb_ref refcount=1, src_buffer refcount=1
        }
        // 继承时间戳和元数据
        GST_BUFFER_PTS(filter->src_buffer) = GST_BUFFER_PTS(buf);
        GST_BUFFER_DTS(filter->src_buffer) = GST_BUFFER_DTS(buf);
        GST_BUFFER_DURATION(filter->src_buffer) = GST_BUFFER_DURATION(buf);

        // Ensure RGB caps are set before pushing buffer
        // gst_plugin_rknn_ensure_rgb_caps(filter);

        gst_pad_push(filter->srcpad, filter->src_buffer); // mem_in_rgb refcount=1?
        filter->src_buffer = NULL;

        // usleep(100000); // 模拟处理延时
        // GST_DEBUG_OBJECT(filter, "Processing buffer with size: %zu", mem_size);

        // if (ret < 0) {
        //     GST_WARNING_OBJECT(filter, "Failed to push buffer, ret = %d", ret);
        // }

        // gst_pad_push(filter->srcpad, buf);
        gst_buffer_unref(buf);
        gst_buffer_unref(buf);
        // print time consumed
        GST_DEBUG_OBJECT(filter, "Processing time: %ld us",
            g_get_monotonic_time() - current_time);
    }
    return NULL;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
plugin_init(GstPlugin* plugin)
{
    /* debug category for filtering log messages
     *
     * exchange the string 'Template plugin' with your description
     */
    GST_DEBUG_CATEGORY_INIT(gst_plugin_rknn_debug, "rknn",
        0, "RKNN plugin");

    return GST_ELEMENT_REGISTER(plugin_rknn, plugin);
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "gst-plugin-rknn"
#endif

/*
 * GST_PLUGIN_DEFINE:
 * @major: major version number of the gstreamer-core that plugin was compiled for
 * @minor: minor version number of the gstreamer-core that plugin was compiled for
 * @name: short, but unique name of the plugin
 * @description: information about the purpose of the plugin
 * @init: function pointer to the plugin_init method with the signature of <code>static gboolean plugin_init (GstPlugin * plugin)</code>.
 * @version: full version string (e.g. VERSION from config.h)
 * @license: under which licence the package has been released, e.g. GPL, LGPL.
 * @package: the package-name (e.g. PACKAGE_NAME from config.h)
 * @origin: a description from where the package comes from (e.g. the homepage URL)
 */
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rknn,
    "rknn",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
