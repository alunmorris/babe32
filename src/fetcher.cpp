// 060326 Fetch text via textise.net proxy into PSRAM
#include "fetcher.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static char *fetch_buf = nullptr;

static void ensure_buf() {
    if (!fetch_buf) {
        fetch_buf = (char *)heap_caps_malloc(FETCH_BUF_SIZE, MALLOC_CAP_SPIRAM);
        assert(fetch_buf);
    }
}

// Wrap any URL through textise.net
static String make_textise_url(const char *url) {
    return String("https://www.textise.net/showText.aspx?strURL=") + url;
}

int fetch_page(const char *url, char **buf_out) {
    ensure_buf();
    *buf_out = fetch_buf;

    String proxy_url = make_textise_url(url);
    Serial.printf("Fetching: %s\n", proxy_url.c_str());

    WiFiClientSecure client;
    client.setInsecure(); // textise.net cert not pinned
    client.setTimeout(15);

    HTTPClient http;
    http.begin(client, proxy_url);
    http.addHeader("User-Agent", "ESP32Browser/1.0");
    http.addHeader("Accept", "text/html");
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("HTTP error: %d\n", code);
        http.end();
        return -1;
    }

    int len = http.getSize();
    Serial.printf("Content-Length: %d\n", len);

    WiFiClient *stream = http.getStreamPtr();
    size_t total = 0;
    size_t cap   = FETCH_BUF_SIZE - 1;

    while (http.connected() && total < cap) {
        size_t avail = stream->available();
        if (avail) {
            size_t to_read = min(avail, cap - total);
            total += stream->readBytes(fetch_buf + total, to_read);
        } else {
            delay(1);
        }
    }
    fetch_buf[total] = '\0';
    http.end();

    Serial.printf("Fetched %zu bytes\n", total);
    return (int)total;
}
