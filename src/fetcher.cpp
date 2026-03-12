// 060326 Fetch via Brightdata proxy, return raw HTML body, follow redirects
// 120326 Persistent TLS connection, keep-alive, batched write, DNS cache
#include "fetcher.h"
#include "dbglog.h"
#include <Arduino.h>
#include <WiFi.h>
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

// Persistent connection state
static WiFiClientSecure *s_client = nullptr;
static IPAddress s_proxy_ip;
static bool s_dns_cached = false;

static bool ensure_connected() {
    if (!s_client) {
        s_client = new WiFiClientSecure();
        s_client->setInsecure();
        s_client->setTimeout(10);
    }

    if (s_client->connected()) {
        dbg("Reusing TLS connection");
        return true;
    }

    // DNS resolve (cached)
    if (!s_dns_cached) {
        dbg("DNS resolve %s...", PROXY_HOST);
        uint32_t t0 = millis();
        if (!WiFiGenericClass::hostByName(PROXY_HOST, s_proxy_ip)) {
            dbg("DNS FAIL after %lums", millis() - t0);
            return false;
        }
        s_dns_cached = true;
        dbg("DNS: %s -> %s in %lums", PROXY_HOST, s_proxy_ip.toString().c_str(), millis() - t0);
    }

    dbg("TLS connect %s:%d...", s_proxy_ip.toString().c_str(), PROXY_PORT);
    uint32_t t0 = millis();
    if (!s_client->connect(s_proxy_ip, PROXY_PORT)) {
        dbg("Proxy FAIL after %lums", millis() - t0);
        return false;
    }
    dbg("TLS connected in %lums", millis() - t0);
    return true;
}

void fetch_disconnect() {
    if (s_client) {
        s_client->stop();
        delete s_client;
        s_client = nullptr;
    }
    s_dns_cached = false;
    s_proxy_ip = IPAddress();
    dbg("Fetch connection closed");
}

// Read exactly `len` bytes into fetch_buf starting at `offset`.
// Returns bytes actually read.
static size_t read_exact(size_t offset, size_t len) {
    size_t got = 0;
    size_t cap = FETCH_BUF_SIZE - 1;
    if (offset + len > cap) len = cap - offset;
    uint32_t idle = millis();
    while (got < len && (s_client->connected() || s_client->available())) {
        int av = s_client->available();
        if (av > 0) {
            size_t want = min((size_t)av, len - got);
            got += s_client->readBytes(fetch_buf + offset + got, want);
            g_fetch_kb = (int)((offset + got) / 1024);
            idle = millis();
        } else if (millis() - idle > 10000) {
            dbg("Read timeout at %zu/%zu", got, len);
            break;
        } else {
            delay(1);
        }
    }
    return got;
}

// Read until connection closes or buffer full. For fallback / error cases.
static size_t read_until_close(size_t offset) {
    size_t total = 0;
    size_t cap = FETCH_BUF_SIZE - 1 - offset;
    uint32_t idle = millis();
    while ((s_client->connected() || s_client->available()) && total < cap) {
        int av = s_client->available();
        if (av > 0) {
            total += s_client->readBytes(fetch_buf + offset + total, min((size_t)av, cap - total));
            g_fetch_kb = (int)((offset + total) / 1024);
            idle = millis();
        } else if (millis() - idle > 10000) {
            dbg("Read timeout");
            break;
        } else {
            delay(1);
        }
    }
    return total;
}

// Read chunked transfer encoding body into fetch_buf starting at body_start.
// Returns total decoded body bytes, or -1 on error.
static int read_chunked_body(size_t body_start) {
    size_t out = body_start;
    size_t cap = FETCH_BUF_SIZE - 1;

    while (true) {
        // Read chunk size line
        char line[16];
        size_t li = 0;
        uint32_t idle = millis();
        while (li < sizeof(line) - 1) {
            if (s_client->available()) {
                char c;
                s_client->readBytes((uint8_t *)&c, 1);
                idle = millis();
                if (c == '\n') break;
                if (c != '\r') line[li++] = c;
            } else if (!s_client->connected()) {
                break;
            } else if (millis() - idle > 10000) {
                dbg("Chunked: timeout reading size");
                return -1;
            } else {
                delay(1);
            }
        }
        line[li] = '\0';

        unsigned long chunk_size = strtoul(line, nullptr, 16);
        if (chunk_size == 0) break;  // final chunk

        if (out + chunk_size > cap) {
            dbg("Chunked: body exceeds buffer");
            chunk_size = cap - out;
        }

        size_t got = read_exact(out, chunk_size);
        out += got;
        if (got < chunk_size) {
            dbg("Chunked: short read %zu/%lu", got, chunk_size);
            break;
        }

        // Read trailing \r\n after chunk data
        char crlf[2];
        s_client->readBytes((uint8_t *)crlf, 2);

        g_fetch_kb = (int)(out / 1024);
        if (out >= cap) break;
    }

    return (int)(out - body_start);
}

static int do_request(const char *url, size_t *total_out) {
    *total_out = 0;

    if (!ensure_connected()) return 0;

    // Extract host from URL
    const char *hs = strstr(url, "://");
    hs = hs ? hs + 3 : url;
    char host[128] = {};
    const char *sl = strchr(hs, '/');
    size_t hl = sl ? (size_t)(sl - hs) : strlen(hs);
    if (hl >= sizeof(host)) hl = sizeof(host) - 1;
    memcpy(host, hs, hl);

    // Batch entire HTTP request into single write
    char req[1024];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Proxy-Authorization: %s\r\n"
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n"
        "Accept: text/html,application/xhtml+xml;q=0.9,*/*;q=0.8\r\n"
        "Accept-Language: en-US,en;q=0.5\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: keep-alive\r\n\r\n",
        url, host, PROXY_AUTH);

    if (req_len >= (int)sizeof(req)) {
        dbg("Request too large for buffer");
        return 0;
    }

    s_client->write((const uint8_t *)req, req_len);
    dbg("Request sent (%d bytes, 1 write)", req_len);
    uint32_t t0 = millis();

    // Read response headers into fetch_buf
    // We read byte-by-byte until we find \r\n\r\n, then handle body separately
    size_t hdr_len = 0;
    size_t cap = FETCH_BUF_SIZE - 1;
    uint32_t idle = millis();
    bool found_end = false;
    while (hdr_len < cap && hdr_len < 8192) {
        if (s_client->available()) {
            fetch_buf[hdr_len] = s_client->read();
            hdr_len++;
            idle = millis();
            if (hdr_len >= 4 &&
                fetch_buf[hdr_len - 4] == '\r' && fetch_buf[hdr_len - 3] == '\n' &&
                fetch_buf[hdr_len - 2] == '\r' && fetch_buf[hdr_len - 1] == '\n') {
                found_end = true;
                break;
            }
        } else if (!s_client->connected()) {
            break;
        } else if (millis() - idle > 10000) {
            dbg("Header read timeout");
            break;
        } else {
            delay(1);
        }
    }
    fetch_buf[hdr_len] = '\0';

    if (!found_end) {
        dbg("No header terminator found");
        s_client->stop();
        return 0;
    }

    // Parse status
    int status = 0;
    if (hdr_len > 12) sscanf(fetch_buf, "HTTP/%*d.%*d %d", &status);
    dbg("HTTP %d (headers %zu bytes, %lums)", status, hdr_len, millis() - t0);

    // Parse Content-Length and Transfer-Encoding from headers
    long content_length = -1;
    bool chunked = false;
    {
        char *cl = strcasestr(fetch_buf, "\r\nContent-Length:");
        if (cl) {
            cl += 17;
            while (*cl == ' ') cl++;
            content_length = strtol(cl, nullptr, 10);
        }
        char *te = strcasestr(fetch_buf, "\r\nTransfer-Encoding:");
        if (te) {
            te += 20;
            while (*te == ' ') te++;
            if (strncasecmp(te, "chunked", 7) == 0) chunked = true;
        }
    }

    // For redirects, we need headers in fetch_buf — read body but we only need the headers
    if (status >= 301 && status <= 308) {
        // Drain body so connection stays clean for reuse
        if (content_length > 0) {
            // Read and discard body
            size_t to_drain = (size_t)content_length;
            size_t drained = 0;
            uint8_t discard[512];
            uint32_t drain_idle = millis();
            while (drained < to_drain && (s_client->connected() || s_client->available())) {
                int av = s_client->available();
                if (av > 0) {
                    size_t want = min((size_t)av, min(sizeof(discard), to_drain - drained));
                    drained += s_client->readBytes(discard, want);
                    drain_idle = millis();
                } else if (millis() - drain_idle > 5000) {
                    break;
                } else {
                    delay(1);
                }
            }
        } else if (chunked) {
            // Read and discard chunked body
            // Simple approach: read chunks until 0-length
            while (s_client->connected() || s_client->available()) {
                char line[16];
                size_t li = 0;
                uint32_t ci = millis();
                while (li < sizeof(line) - 1) {
                    if (s_client->available()) {
                        char c = s_client->read();
                        ci = millis();
                        if (c == '\n') break;
                        if (c != '\r') line[li++] = c;
                    } else if (!s_client->connected() || millis() - ci > 5000) {
                        goto drain_done;
                    } else {
                        delay(1);
                    }
                }
                line[li] = '\0';
                unsigned long cs = strtoul(line, nullptr, 16);
                if (cs == 0) break;
                uint8_t discard[512];
                size_t left = cs;
                while (left > 0 && (s_client->connected() || s_client->available())) {
                    int av = s_client->available();
                    if (av > 0) {
                        size_t want = min((size_t)av, min(sizeof(discard), left));
                        left -= s_client->readBytes(discard, want);
                    } else {
                        delay(1);
                    }
                }
                // Consume trailing \r\n
                char crlf[2];
                s_client->readBytes((uint8_t *)crlf, 2);
            }
            drain_done:;
        }
        *total_out = hdr_len;
        return status;
    }

    // For 200 responses, read body into fetch_buf after headers
    if (status == 200) {
        size_t body_bytes = 0;
        if (content_length >= 0) {
            dbg("Reading %ld bytes (Content-Length)", content_length);
            body_bytes = read_exact(hdr_len, (size_t)content_length);
        } else if (chunked) {
            dbg("Reading chunked body");
            int cb = read_chunked_body(hdr_len);
            if (cb < 0) {
                s_client->stop();
                return 0;
            }
            body_bytes = (size_t)cb;
        } else {
            // No Content-Length, not chunked — read until close (connection won't be reusable)
            dbg("No Content-Length/chunked — reading until close");
            body_bytes = read_until_close(hdr_len);
        }
        *total_out = hdr_len + body_bytes;
        fetch_buf[*total_out] = '\0';
        dbg("Body: %zu bytes in %lums", body_bytes, millis() - t0);
        return status;
    }

    // Other status codes — read body if Content-Length known, else drain
    if (content_length > 0) {
        size_t body_bytes = read_exact(hdr_len, (size_t)content_length);
        *total_out = hdr_len + body_bytes;
    } else {
        *total_out = hdr_len;
    }
    fetch_buf[*total_out] = '\0';
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
            // Connection failed — retry once with fresh connection
            if (attempt == 0 && s_client) {
                dbg("Reconnecting after failure...");
                s_client->stop();
                status = do_request(cur_url, &total);
                if (status == 0 || total == 0) {
                    dbg("Retry also failed");
                    return -1;
                }
            } else {
                dbg("No response");
                return -1;
            }
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
