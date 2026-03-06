// 060326 Task 6: test HTTP fetcher
#include <Arduino.h>
#include "wifi_mgr.h"
#include "fetcher.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    wifi_mgr_init();
    if (!wifi_mgr_connect()) {
        wifi_mgr_start_portal();
    }

    char *buf;
    int n = fetch_page("https://en.wikipedia.org/wiki/ESP32", &buf);
    if (n > 0) {
        Serial.printf("Fetch OK: %d bytes\n", n);
        Serial.write(buf, min(n, 500));
        Serial.println();
    } else {
        Serial.println("Fetch failed");
    }
}

void loop() { delay(1000); }
