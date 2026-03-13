// 060326 FreeRTOS network task on core 0
// 130326 Add POST support for form submission
#include "net_task.h"
#include "fetcher.h"
#include "dbglog.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define URL_MAX_LEN 512
#define POST_BODY_MAX 4096

// Queue item: URL + optional POST body
struct NetRequest {
    char url[URL_MAX_LEN];
    char post_body[POST_BODY_MAX];  // empty string = GET
};

static QueueHandle_t   s_req_queue = nullptr;
static page_ready_cb_t s_ready_cb  = nullptr;

static void net_task_fn(void *arg) {
    dbg("net_task started, core %d", xPortGetCoreID());
    NetRequest req;
    for (;;) {
        dbg("Waiting for URL...");
        if (xQueueReceive(s_req_queue, &req, portMAX_DELAY) == pdTRUE) {
            dbg("Got URL: %.40s %s", req.url, req.post_body[0] ? "(POST)" : "(GET)");

            char *html;
            int n;
            if (req.post_body[0]) {
                n = fetch_page_post(req.url, req.post_body, &html);
            } else {
                n = fetch_page(req.url, &html);
            }
            dbg("fetch returned %d", n);

            ParseResult *result = parse_result_alloc();
            if (n > 0 && result) {
                dbg("Parsing HTML...");
                html_parse(html, req.url, result);
                dbg("Parsed: %d elements", result->count);
            } else if (result) {
                char msg[64];
                if (n < -1) {
                    int code = -n;
                    snprintf(msg, sizeof(msg), "HTTP %d error", code);
                    result->http_status = code;
                } else {
                    snprintf(msg, sizeof(msg), "Could not load page.");
                }
                result->elems[0].type  = ELEM_PARAGRAPH;
                result->elems[0].level = 0;
                result->elems[0].href  = NULL;
                strncpy(result->text_pool, msg, sizeof(result->text_pool) - 1);
                result->elems[0].text = result->text_pool;
                result->count = 1;
                result->error = true;
                result->pool_used = strlen(msg) + 1;
                dbg("Fetch failed: %s", msg);
            }

            if (s_ready_cb) s_ready_cb(result, req.url);
        }
    }
}

void net_task_start(page_ready_cb_t on_page_ready) {
    s_ready_cb  = on_page_ready;
    s_req_queue = xQueueCreate(2, sizeof(NetRequest));
    xTaskCreatePinnedToCore(net_task_fn, "net_task", 32768, nullptr, 4, nullptr, 0);
}

void net_task_load(const char *url) {
    if (!s_req_queue) return;
    NetRequest req = {};
    strncpy(req.url, url, URL_MAX_LEN - 1);
    xQueueSend(s_req_queue, &req, pdMS_TO_TICKS(500));
}

void net_task_load_post(const char *url, const char *post_body) {
    if (!s_req_queue) return;
    NetRequest req = {};
    strncpy(req.url, url, URL_MAX_LEN - 1);
    if (post_body) strncpy(req.post_body, post_body, POST_BODY_MAX - 1);
    xQueueSend(s_req_queue, &req, pdMS_TO_TICKS(500));
}
