#include "rknnprocess.h"
#include "rknnmeta.h"
#include <string.h>

GType gst_rknn_meta_api_get_type(void) {
  static GType type = 0;
  static const gchar *tags[] = { NULL };
  
  if (g_once_init_enter(&type)) {
    GType _type = gst_meta_api_type_register("GstRknnMetaAPI", tags);
    g_once_init_leave(&type, _type);
  }
  
  return type;
}

const GstMetaInfo *gst_rknn_meta_get_info(void) {
  static const GstMetaInfo *meta_info = NULL;
  
  if (g_once_init_enter(&meta_info)) {
    const GstMetaInfo *info = gst_meta_register(
        GST_RKNN_META_API_TYPE,
        "GstRknnMeta",
        sizeof(GstRknnMeta),
        gst_rknn_meta_init,
        gst_rknn_meta_free,
        gst_rknn_meta_transform);
    g_once_init_leave(&meta_info, info);
  }
  
  return meta_info;
}

gboolean gst_rknn_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
  GstRknnMeta *rknn_meta = (GstRknnMeta *)meta;
  
  /* 初始化 RknnMeta 结构体 */
  memset(&rknn_meta->rknn_meta, 0, sizeof(struct RknnMeta));
  
  return TRUE;
}

void gst_rknn_meta_free(GstMeta *meta, GstBuffer *buffer) {
  /* 不需要额外释放，GstMeta 框架会处理 */
}

gboolean gst_rknn_meta_transform(GstBuffer *transbuf, GstMeta *meta,
                               GstBuffer *buffer, GQuark type, gpointer data) {
  GstRknnMeta *src = (GstRknnMeta *)meta;
  GstRknnMeta *dest;
  
  /* 复制 meta 到目标 buffer */
  dest = (GstRknnMeta *)gst_buffer_add_meta(transbuf, GST_RKNN_META_INFO, NULL);
  if (!dest)
    return FALSE;
    
  memcpy(&dest->rknn_meta, &src->rknn_meta, sizeof(struct RknnMeta));
  
  return TRUE;
}