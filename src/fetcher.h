// 060326 HTTPS page fetcher into PSRAM buffer
// 120326 Added fetch_disconnect() for persistent connection cleanup
#pragma once
#include <stddef.h>

#define FETCH_BUF_SIZE (512 * 1024)  // 512KB

// fetch_page: fetches `url` via Brightdata residential proxy.
// Returns raw HTML body byte count, or -1 on error.
// Buffer is null-terminated. Caller must not free buf — static PSRAM allocation.
int fetch_page(const char *url, char **buf_out);

// Close persistent TLS connection and clear cached DNS.
// Call on WiFi disconnect or before deep sleep.
void fetch_disconnect();
