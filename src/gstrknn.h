/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 Niels De Graef <niels.degraef@gmail.com>
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

#ifndef __GST_PLUGIN_RKNN_H__
#define __GST_PLUGIN_RKNN_H__

#include "glib.h"
#include "gst/gsttask.h"
#include "gst/video/video-format.h"
#include "gst/video/video-info.h"
#include <gst/gst.h>
#include "RgaUtils.h"
#include "im2d.h"
#include "rga.h"
#include "rknn_api.h"
#define MAX_QUEUE_LENGTH 3
#define MAX_DMABUF_INSTANCES 4

#define PLUGIN_RKNN_SUPPORT_FORMATS MPP_SUPPORT_FORMATS "," RGA_SUPPORT_FORMATS
#define MPP_SUPPORT_FORMATS    \
    "NV12, I420, YUY2, UYVY, " \
    "BGR16, RGB16, BGR, RGB, " \
    "ABGR, ARGB, BGRA, RGBA, xBGR, xRGB, BGRx, RGBx"
#define RGA_SUPPORT_FORMATS                \
    "NV12, NV21, I420, YV12, NV16, NV61, " \
    "BGR16, RGB, BGR, RGBA, BGRA, RGBx, BGRx"
G_BEGIN_DECLS

#define GST_TYPE_PLUGIN_RKNN (gst_plugin_rknn_get_type())
/*
 * G_DECLARE_FINAL_TYPE:
 * @ModuleObjName: The name of the new type, in camel case (like `GtkWidget`)
 * @module_obj_name: The name of the new type in lowercase, with words
 *  separated by `_` (like `gtk_widget`)
 * @MODULE: The name of the module, in all caps (like `GTK`)
 * @OBJ_NAME: The bare name of the type, in all caps (like `WIDGET`)
 * @ParentName: the name of the parent type, in camel case (like `GtkWidget`)
 *
 */
G_DECLARE_FINAL_TYPE(GstPluginRknn, gst_plugin_rknn,
    GST, PLUGIN_RKNN, GstElement)
    
typedef struct {
    GstPluginRknn *filter;
    gboolean stop;
} RknnTaskData;

struct _GstPluginRknn {
    GstElement element;

    GstPad *sinkpad, *srcpad;

    gboolean silent;
    gboolean bypass;

    GAsyncQueue* queue;
    GThread* task_thread;
    RknnTaskData* task_data;
    GstCaps* sink_caps;
    GstCaps* src_caps;

    gint64 last_buffer_full_log_time;

    int dma_heap_fd;

    int cached_dmabuf_fd[MAX_DMABUF_INSTANCES];
    void* cached_dmabuf_ptr[MAX_DMABUF_INSTANCES];
    gsize cached_dmabuf_size[MAX_DMABUF_INSTANCES];
    GstAllocator* cached_allocator[MAX_DMABUF_INSTANCES];
    GstMemory* cached_dmabuf_mem[MAX_DMABUF_INSTANCES];
    
    guint sink_width;
    guint sink_height;
    GstVideoFormat sink_format;
    GstVideoInfo sink_info;
    RgaSURF_FORMAT sink_rga_format;

    rknn_context rknn_ctx;
    rknn_input rknn_inputs[1];
    rknn_input_output_num rknn_io_num;
    char *rknn_model_path;
    gboolean rknn_model_loaded;
    guint model_width;
    guint model_height;
    guint model_channel;


};

G_END_DECLS

#endif /* __GST_PLUGIN_RKNN_H__ */
