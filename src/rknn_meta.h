#ifndef _RKNN_META_H_
#define _RKNN_META_H_

#include <gst/gst.h>
#define MAX_PERSON 128

/* 前向声明 RknnMeta 结构体 */
struct RknnMeta {
  GstClockTime start_time;
  GstClockTime queue_done;
  GstClockTime preprocess_done;
  GstClockTime infer_done;
  GstClockTime postprocess_done;
  GstClockTime visualize_done;
  GstClockTime push_pad_done;

  float results[MAX_PERSON][56];
  int results_size;
};

/* RknnMeta GstMeta 实现 */
typedef struct {
  GstMeta meta;
  struct RknnMeta rknn_meta;
} GstRknnMeta;

G_BEGIN_DECLS
/* RknnMeta API 类型 */
GType gst_rknn_meta_api_get_type(void);
#define GST_RKNN_META_API_TYPE (gst_rknn_meta_api_get_type())
const GstMetaInfo *gst_rknn_meta_get_info(void);
#define GST_RKNN_META_INFO (gst_rknn_meta_get_info())
#define gst_buffer_get_rknn_meta(b) ((GstRknnMeta*)gst_buffer_get_meta((b), GST_RKNN_META_API_TYPE))

/* RknnMeta GstMeta 函数声明 */
gboolean gst_rknn_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer);
void gst_rknn_meta_free(GstMeta *meta, GstBuffer *buffer);
gboolean gst_rknn_meta_transform(GstBuffer *transbuf, GstMeta *meta,
                                 GstBuffer *buffer, GQuark type, gpointer data);

G_END_DECLS

#endif /* _RKNN_META_H_ */