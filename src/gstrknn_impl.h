#ifndef __GST_RKNN_IMPL_H__
#define __GST_RKNN_IMPL_H__

#ifdef  __cplusplus
#define G_BEGIN_DECLS  extern "C" {
#define G_END_DECLS    }
#else
#define G_BEGIN_DECLS
#define G_END_DECLS
#endif

#include "gstrknn.h"
#include "rknnprocess.h"
#include "rknn_meta.h"

G_BEGIN_DECLS

/* 函数声明 */
gboolean init_rknn_engines(GstPluginRknn *filter);
void destroy_rknn_engines(GstPluginRknn *filter);
gboolean allocate_rknn_resources(GstPluginRknn *filter);
void release_rknn_resources(GstPluginRknn *filter);
gboolean scheduler(GstPluginRknn *filter, GstBuffer *raw_buffer);

G_END_DECLS

#endif /* __GST_RKNN_IMPL_H__ */