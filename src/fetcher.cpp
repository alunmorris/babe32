// 060326 Fetch via Brightdata proxy, return raw HTML body, follow redirects
#include "fetcher.h"
#include "dbglog.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>

extern volatile int g_fetch_kb;

static char *fetch_buf = nullptr;

static void ensure_buf() {
    if (!fetch_buf) {
        fetch_buf = (char *)heap_caps_malloc(FETCH_BUF_SIZE, MALLOC_CAP_SPIRAM);
    }
}

// Brightdata residential proxy
static const char *PROXY_HOST = "brd.superproxy.io";
static const int   PROXY_PORT = 33335;
static const char *PROXY_AUTH =
    "Basic YnJkLWN1c3RvbWVyLWhsX2E2Zjg5MTU3LXpvbmUtcmVzaWRlbnRpYWxfcHJveHkxOmRhcWNtZnhqdTkzNA==";

static int do_request(const char *url, size_t *total_out) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    dbg("DNS+TCP %s:%d...", PROXY_HOST, PROXY_PORT);
    uint32_t t0 = millis();
    if (!client.connect(PROXY_HOST, PROXY_PORT)) {
        dbg("Proxy FAIL after %lums", millis() - t0);
        *total_out = 0;
        return 0;
    }
    dbg("Connected in %lums", millis() - t0);

    const char *hs = strstr(url, "://");
    hs = hs ? hs + 3 : url;
    char host[128] = {};
    const char *sl = strchr(hs, '/');
    size_t hl = sl ? (size_t)(sl - hs) : strlen(hs);
    if (hl >= sizeof(host)) hl = sizeof(host) - 1;
    memcpy(host, hs, hl);

    client.printf("GET %s HTTP/1.1\r\n", url);
    client.printf("Host: %s\r\n", host);
    client.printf("Proxy-Authorization: %s\r\n", PROXY_AUTH);
    client.print("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n");
    client.print("Accept: text/html,application/xhtml+xml;q=0.9,*/*;q=0.8\r\n");
    client.print("Accept-Language: en-US,en;q=0.5\r\n");
    client.print("Accept-Encoding: identity\r\n");
    client.print("Connection: close\r\n\r\n");
    dbg("Request sent");
    t0 = millis();

    size_t total = 0, cap = FETCH_BUF_SIZE - 1;
    uint32_t idle = millis();
    while ((client.connected() || client.available()) && total < cap) {
        int av = client.available();
        if (av > 0) {
            total += client.readBytes(fetch_buf + total, min((size_t)av, cap - total));
            g_fetch_kb = (int)(total / 1024);
            idle = millis();
        } else if (millis() - idle > 10000) {
            dbg("Read timeout");
            break;
        } else {
            delay(1);
        }
    }
    fetch_buf[total] = '\0';
    client.stop();
    *total_out = total;
    dbg("Read %zu bytes in %lums", total, millis() - t0);

    int status = 0;
    if (total > 12) sscanf(fetch_buf, "HTTP/%*d.%*d %d", &status);
    dbg("HTTP status %d", status);
    return status;
}

int fetch_page(const char *url, char **buf_out) {
    dbg("fetch_page: alloc buf");
    ensure_buf();
    if (!fetch_buf) {
        dbg("PSRAM alloc FAILED!");
        *buf_out = nullptr;
        return -1;
    }
    dbg("PSRAM buf OK (%dKB)", FETCH_BUF_SIZE / 1024);
    *buf_out = fetch_buf;

    char cur_url[512];
    strncpy(cur_url, url, sizeof(cur_url) - 1);
    cur_url[sizeof(cur_url) - 1] = '\0';

    for (int attempt = 0; attempt < 5; attempt++) {
        dbg("Attempt %d: %.40s", attempt + 1, cur_url);
        size_t total = 0;
        int status = do_request(cur_url, &total);

        if (status == 0 || total == 0) {
            dbg("No response");
            return -1;
        }

        if (status == 200) {
            char *body = strstr(fetch_buf, "\r\n\r\n");
            if (!body) { dbg("No header separator"); return -1; }
            body += 4;
            size_t body_len = total - (size_t)(body - fetch_buf);
            memmove(fetch_buf, body, body_len);
            fetch_buf[body_len] = '\0';
            dbg("HTML body: %zu bytes", body_len);
            return (int)body_len;
        }

        if (status >= 301 && status <= 308) {
            char *loc = strcasestr(fetch_buf, "\r\nLocation:");
            if (!loc) { dbg("Redirect no Location"); return -1; }
            loc += 11;
            while (*loc == ' ') loc++;
            char *end = strstr(loc, "\r\n");
            if (!end) { dbg("Malformed Location"); return -1; }
            size_t len = (size_t)(end - loc);
            if (len >= sizeof(cur_url)) len = sizeof(cur_url) - 1;
            strncpy(cur_url, loc, len);
            cur_url[len] = '\0';
            dbg("Redirect -> %.40s", cur_url);
            continue;
        }

        dbg("HTTP error %d", status);
        return -1;
    }

    dbg("Too many redirects");
    return -1;
}
