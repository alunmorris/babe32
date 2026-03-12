// 060326 FreeRTOS network task on core 0
#include "net_task.h"
#include "fetcher.h"
#include "dbglog.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define URL_MAX_LEN 512

static QueueHandle_t   s_url_queue = nullptr;
static page_ready_cb_t s_ready_cb  = nullptr;

static void net_task_fn(void *arg) {
    dbg("net_task started, core %d", xPortGetCoreID());
    char url_buf[URL_MAX_LEN];
    for (;;) {
        dbg("Waiting for URL...");
        if (xQueueReceive(s_url_queue, url_buf, portMAX_DELAY) == pdTRUE) {
            dbg("Got URL: %.40s", url_buf);

            char *html;
            int n = fetch_page(url_buf, &html);
            dbg("fetch_page returned %d", n);

            ParseResult *result = parse_result_alloc();
            if (n > 0 && result) {
                dbg("Parsing HTML...");
                html_parse(html, url_buf, result);
                dbg("Parsed: %d elements", result->count);
            } else if (result) {
                const char *msg = "Error: could not load page.";
                result->elems[0].type  = ELEM_PARAGRAPH;
                result->elems[0].level = 0;
                result->elems[0].href  = NULL;
                strncpy(result->text_pool, msg, sizeof(result->text_pool) - 1);
                result->elems[0].text = result->text_pool;
                result->count = 1;
                result->pool_used = strlen(msg) + 1;
                dbg("Fetch failed, error page");
            }

            if (s_ready_cb) s_ready_cb(result, url_buf);
        }
    }
}

void net_task_start(page_ready_cb_t on_page_ready) {
    s_ready_cb  = on_page_ready;
    s_url_queue = xQueueCreate(3, URL_MAX_LEN);
    xTaskCreatePinnedToCore(net_task_fn, "net_task", 32768, nullptr, 4, nullptr, 0);
}

void net_task_load(const char *url) {
    if (!s_url_queue) return;
    char buf[URL_MAX_LEN];
    strncpy(buf, url, URL_MAX_LEN - 1);
    buf[URL_MAX_LEN - 1] = '\0';
    xQueueSend(s_url_queue, buf, pdMS_TO_TICKS(500));
}
