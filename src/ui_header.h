// 060326 Header bar: back/forward buttons + URL textarea
#pragma once
#include <lvgl.h>

typedef void (*navigate_cb_t)(const char *url);
typedef void (*back_cb_t)();
typedef void (*forward_cb_t)();

lv_obj_t *header_create(lv_obj_t *parent,
                         navigate_cb_t on_navigate,
                         back_cb_t on_back,
                         forward_cb_t on_forward);

void header_set_url(const char *url);
void header_set_back_enabled(bool en);
void header_set_forward_enabled(bool en);
void header_set_loading(bool loading);
