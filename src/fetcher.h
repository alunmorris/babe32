// 060326 HTTPS page fetcher into PSRAM buffer
#pragma once
#include <stddef.h>

#define FETCH_BUF_SIZE (512 * 1024)  // 512KB

// fetch_page: fetches textise.net proxy URL for `url` into PSRAM buffer.
// Returns number of bytes fetched, or -1 on error.
// Buffer is null-terminated.
// Caller must not free buf — it is a static PSRAM allocation.
int fetch_page(const char *url, char **buf_out);
