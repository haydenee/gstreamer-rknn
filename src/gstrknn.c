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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstrknn.h"

GST_DEBUG_CATEGORY_STATIC (gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_BYPASS
} PROP_ID;

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_plugin_rknn_parent_class parent_class
/**
 * G_DEFINE_TYPE:
 * @TN: The name of the new type, in Camel case.
 * @t_n: The name of the new type, in lowercase, with words 
 *  separated by `_`.
 * @T_P: The #GType of the parent type.
 */
G_DEFINE_TYPE (GstPluginRknn, gst_plugin_rknn, GST_TYPE_ELEMENT);

/**
 * GST_ELEMENT_REGISTER_DEFINE:
 * @e: The element name in lower case, with words separated by '_'.
 * Used to generate `gst_element_register_*(GstPlugin* plugin)`.
 * @e_n: The public name of the element
 * @r: The #GstRank of the element (higher rank means more importance when autoplugging, see #GstRank)
 * @t: The #GType of the element.
 */
GST_ELEMENT_REGISTER_DEFINE (plugin_rknn, "rknn", GST_RANK_NONE,
    GST_TYPE_PLUGIN_RKNN);

static void gst_plugin_rknn_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_plugin_rknn_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_plugin_rknn_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_plugin_rknn_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);

/* GObject vmethod implementations */

/* initialize the plugin's class */
static void
gst_plugin_rknn_class_init (GstPluginRknnClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_plugin_rknn_set_property;
  gobject_class->get_property = gst_plugin_rknn_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_BYPASS,
      g_param_spec_boolean ("bypass", "Bypass",
          "Bypass the filter and just pass through the data",
          FALSE, G_PARAM_READWRITE));

  gst_element_class_set_details_simple (gstelement_class,
      "Rknn Plugin",
      "FIXME: Rknn Plugin classification",
      "FIXME: Rknn Plugin description", "haydenee <lth0320@163.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_plugin_rknn_init (GstPluginRknn * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_plugin_rknn_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_plugin_rknn_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->silent = FALSE;
}

static void
gst_plugin_rknn_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPluginRknn *filter = GST_PLUGIN_RKNN (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case PROP_BYPASS:
      filter->bypass = g_value_get_boolean (value);
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_plugin_rknn_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPluginRknn *filter = GST_PLUGIN_RKNN (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_BYPASS:
      g_value_set_boolean (value, filter->bypass);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_plugin_rknn_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstPluginRknn *filter;
  gboolean ret;

  filter = GST_PLUGIN_RKNN (parent);

  GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      /* do something with the caps */

      /* and forward */
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_plugin_rknn_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstPluginRknn *filter;

  filter = GST_PLUGIN_RKNN (parent);

  if (filter->silent == FALSE)
    g_print ("I'm plugged, therefore I'm in.\n");

  if (filter->bypass) {
    /* if we are in bypass mode, just push the buffer out */
    GST_LOG_OBJECT (filter, "Bypassing the filter, pushing buffer out");
    return gst_pad_push (filter->srcpad, buf);
  }
  /* just push out the incoming buffer without touching it */
  return gst_pad_push (filter->srcpad, buf);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  /* debug category for filtering log messages
   *
   * exchange the string 'Template plugin' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_plugin_rknn_debug, "rknn",
      0, "RKNN plugin");

  return GST_ELEMENT_REGISTER (plugin_rknn, plugin);
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
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rknn,
    "rknn",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
