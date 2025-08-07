#ifndef _COMMON_STRUCTS_H_
#define _COMMON_STRUCTS_H_

#ifdef __cplusplus
extern "C" {
#endif

#define OBJ_NAME_MAX_SIZE 16
#define OBJ_NUMB_MAX_SIZE 64

typedef struct _BOX_RECT
{
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;

typedef struct __detect_result_t
{
    char name[OBJ_NAME_MAX_SIZE];
    BOX_RECT box;
    float prop;
} detect_result_t;

typedef struct _detect_result_group_t
{
    int id;
    int count;
    detect_result_t results[OBJ_NUMB_MAX_SIZE];
} detect_result_group_t;

#ifdef __cplusplus
}
#endif

#endif // _COMMON_STRUCTS_H_