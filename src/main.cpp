// 060326 Browser entry point: WiFi first, then UI task
// 110326 Force WiFi PHY init before display PSRAM DMA to avoid MMU fault
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "wifi_mgr.h"
#include "ui_task.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ESP32 Text Browser v1.0");

    // ESP32-S3 cache workaround: esp_wifi_start() → esp_phy_enable() flushes the
    // cache bus.  If PSRAM DMA is in-flight at that moment the MMU faults.
    // Fix: complete PHY init synchronously BEFORE any PSRAM DMA starts.
    WiFi.mode(WIFI_STA);
    esp_wifi_start();   // synchronous; PHY enabled on return
    Serial.println("WiFi PHY init done");

    // Safe to start display DMA now — PHY is already up
    ui_task_start();

    wifi_mgr_init();
    if (!wifi_mgr_connect()) {
        Serial.println("No known AP — starting portal");
        wifi_mgr_start_portal();  // blocks until configured; UI task runs independently
    }

    Serial.printf("WiFi: %s\n",
                  wifi_mgr_is_connected() ? "connected" : "failed");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
