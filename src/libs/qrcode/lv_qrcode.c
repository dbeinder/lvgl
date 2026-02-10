/**
 * @file lv_qrcode.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "../../core/lv_obj_class_private.h"
#include "lv_qrcode_private.h"

#if LV_USE_QRCODE

#include "../../misc/cache/lv_cache.h"
#include "qrcodegen.h"

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS (&lv_qrcode_class)

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_qrcode_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_qrcode_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static int32_t get_satisfied_size(int32_t min_version, int32_t size, int32_t * scale);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t lv_qrcode_class = {
    .constructor_cb = lv_qrcode_constructor,
    .destructor_cb = lv_qrcode_destructor,
    .instance_size = sizeof(lv_qrcode_t),
    .base_class = &lv_canvas_class,
    .name = "lv_qrcode",
};

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * lv_qrcode_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

void lv_qrcode_set_size(lv_obj_t * obj, int32_t size)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_draw_buf_t * old_buf = lv_canvas_get_draw_buf(obj);
    lv_draw_buf_t * new_buf = lv_draw_buf_create(size, size, LV_COLOR_FORMAT_I1, LV_STRIDE_AUTO);
    if(new_buf == NULL) {
        LV_LOG_ERROR("malloc failed for canvas buffer");
        return;
    }

    lv_canvas_set_draw_buf(obj, new_buf);
    LV_LOG_INFO("set canvas buffer: %p, size = %d", (void *)new_buf, (int)size);

    if(old_buf != NULL) lv_draw_buf_destroy(old_buf);
}

void lv_qrcode_set_dark_color(lv_obj_t * obj, lv_color_t color)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_qrcode_t * qrcode = (lv_qrcode_t *)obj;
    qrcode->dark_color = color;
}

void lv_qrcode_set_light_color(lv_obj_t * obj, lv_color_t color)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_qrcode_t * qrcode = (lv_qrcode_t *)obj;
    qrcode->light_color = color;
}

lv_result_t lv_qrcode_update(lv_obj_t * obj, const void * data, uint32_t data_len)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_qrcode_t * qrcode = (lv_qrcode_t *)obj;

    lv_draw_buf_t * draw_buf = lv_canvas_get_draw_buf(obj);
    if(draw_buf == NULL) {
        LV_LOG_ERROR("canvas draw buffer is NULL");
        return LV_RESULT_INVALID;
    }

    uint8_t * buf_u8 = draw_buf->data + 8;    /*+8 skip the palette*/
    lv_memset(buf_u8, 0xFF, draw_buf->header.stride*draw_buf->header.w); /* fill white */
    lv_draw_buf_flush_cache(draw_buf, NULL);
    lv_image_cache_drop(draw_buf);

    lv_obj_invalidate(obj);

    if(data_len > qrcodegen_BUFFER_LEN_MAX) return LV_RESULT_INVALID;

    int32_t qr_version = qrcodegen_getMinFitVersion(qrcodegen_Ecc_MEDIUM, data_len);
    int32_t quiet_zone_scale = 0;
    if(qrcode->quiet_zone) qr_version = get_satisfied_size(qr_version, draw_buf->header.w, &quiet_zone_scale);
    if(qr_version <= 0 || (qrcode->quiet_zone && quiet_zone_scale <= 0)) return LV_RESULT_INVALID;

    const int32_t qr_size = qrcodegen_version2size(qr_version);
    if(qr_size <= 0) return LV_RESULT_INVALID;
    const int32_t scale = qrcode->quiet_zone ? quiet_zone_scale : draw_buf->header.w / qr_size;

    uint8_t * qr0 = lv_malloc(qrcodegen_BUFFER_LEN_FOR_VERSION(qr_version));
    LV_ASSERT_MALLOC(qr0);
    uint8_t * data_tmp = lv_malloc(qrcodegen_BUFFER_LEN_FOR_VERSION(qr_version));
    LV_ASSERT_MALLOC(data_tmp);
    lv_memcpy(data_tmp, data, data_len);

    bool ok = qrcodegen_encodeBinary(data_tmp, data_len,
                                     qr0, qrcodegen_Ecc_MEDIUM,
                                     qr_version, qr_version,
                                     qrcodegen_Mask_AUTO, true);

    if(!ok) {
        lv_free(qr0);
        lv_free(data_tmp);
        return LV_RESULT_INVALID;
    }

    /* Temporarily disable invalidation to improve the efficiency of lv_canvas_set_px */
    lv_display_enable_invalidation(lv_obj_get_display(obj), false);

    int32_t obj_w = draw_buf->header.w;
    int scaled = qr_size * scale;
    int margin = (obj_w - scaled) / 2;
    uint32_t row_byte_cnt = draw_buf->header.stride;

    for(int y = 0; y < qr_size; y++) {
        int y_start = y*scale + margin;
        int y_idx = y_start * row_byte_cnt;

        for(int x = 0; x < qr_size; x++) {
            if(!qrcodegen_getModule(qr0, x, y)) continue;

            int x_start = x*scale + margin;

            /* fill black pixels */
            for(uint32_t off = x_start; off < x_start+scale; off++) {
                buf_u8[y_idx + (off/8)] &= ~(1 << (7-(off&0x07)));
            }

            /* copy duplicate rows */
            for(int r = 1; r < scale; r++) {
                lv_memcpy(&buf_u8[y_idx + r*row_byte_cnt], &buf_u8[y_idx], row_byte_cnt);
            }
        }
    }

    /* invalidate the canvas to refresh it */
    lv_display_enable_invalidation(lv_obj_get_display(obj), true);

    lv_free(qr0);
    lv_free(data_tmp);
    return LV_RESULT_OK;
}

void lv_qrcode_set_data(lv_obj_t * obj, const char * data)
{
    if(data == NULL) return;
    lv_qrcode_update(obj, data, lv_strlen(data));
}

void lv_qrcode_set_quiet_zone(lv_obj_t * obj, bool enable)
{
    lv_qrcode_t * qrcode = (lv_qrcode_t *)obj;
    qrcode->quiet_zone = enable;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_qrcode_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    /*Set default size*/
    lv_qrcode_set_size(obj, LV_DPI_DEF);

    /*Set default color*/
    lv_qrcode_set_dark_color(obj, lv_color_black());
    lv_qrcode_set_light_color(obj, lv_color_white());
}

static void lv_qrcode_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    lv_draw_buf_t * draw_buf = lv_canvas_get_draw_buf(obj);
    if(draw_buf == NULL) return;
    lv_image_cache_drop(draw_buf);

    /*@fixme destroy buffer in cache free_cb.*/
    lv_draw_buf_destroy(draw_buf);
}

static int32_t get_satisfied_size(int32_t min_version, int32_t size, int32_t * scale)
{
    if(min_version <= 0) return -1;

    int32_t offset = size;
    int32_t satisfied_version = min_version;
    if(scale) *scale = 0;

    for(int32_t version = min_version; version <= min_version + 2 && version <= qrcodegen_VERSION_MAX - 3; version++) {
        int32_t version_size = qrcodegen_version2size(version + 1);
        int32_t tmp_offset = size % version_size;
        int32_t tmp_scale = size / version_size;

        if(tmp_offset < offset) {
            offset = tmp_offset;
            satisfied_version = version;
            if(scale) *scale = tmp_scale;
        }
    }
    return satisfied_version;
}

#endif /*LV_USE_QRCODE*/
