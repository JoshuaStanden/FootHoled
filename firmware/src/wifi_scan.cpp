#include "wifi_scan.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "recon_log.h"
#include <string.h>

namespace {
constexpr size_t MAX_AP = 48;
FootHoledAP aps[MAX_AP] = {};
size_t ap_count = 0;
uint32_t generation = 0;
bool requested = false;
bool ready = false;
bool error_state = false;
SemaphoreHandle_t guard = nullptr;
TaskHandle_t worker = nullptr;
const char *TAG = "FootHoled/wifi";

void scan_task(void *) {
    while (true) {
        bool active;
        xSemaphoreTake(guard, portMAX_DELAY);
        active = requested;
        xSemaphoreGive(guard);
        if (!active) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        wifi_scan_config_t config = {};
        config.show_hidden = true;
        config.scan_type = WIFI_SCAN_TYPE_ACTIVE; // standard discovery probe requests, no association
        config.scan_time.active.min = 60;
        config.scan_time.active.max = 120;
        esp_err_t err = esp_wifi_scan_start(&config, true); // block only this worker task
        // Wi-Fi AP records are too large for this task's stack. Allocate on heap.
        wifi_ap_record_t *records = static_cast<wifi_ap_record_t *>(
            heap_caps_calloc(MAX_AP, sizeof(wifi_ap_record_t), MALLOC_CAP_8BIT));
        uint16_t count = MAX_AP;
        if (!records) {
            ESP_LOGE(TAG, "Unable to allocate Wi-Fi scan records");
            err = ESP_ERR_NO_MEM;
        } else if (err == ESP_OK) {
            err = esp_wifi_scan_get_ap_records(&count, records);
        }
        xSemaphoreTake(guard, portMAX_DELAY);
        if (requested) {
            error_state = err != ESP_OK;
            if (err == ESP_OK) {
                ap_count = count;
                for (size_t i=0; i<count; ++i) {
                    memcpy(aps[i].ssid, records[i].ssid, 32);
                    aps[i].ssid[32] = 0;
                    memcpy(aps[i].bssid, records[i].bssid, 6);
                    aps[i].rssi = records[i].rssi;
                    aps[i].channel = records[i].primary;
                    aps[i].authmode = (uint8_t)records[i].authmode;
                }
                ++generation;
            }
        }
        xSemaphoreGive(guard);
        // Persist every sighting from this completed scan to the SD-card log.
        // No-ops safely if no card is mounted. Done after releasing the guard
        // so SD I/O never blocks a UI snapshot read.
        if (err == ESP_OK && count > 0) {
            footholed_recon_log_wifi(aps, count);
        }
        heap_caps_free(records);
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}
}

bool footholed_wifi_init() {
    if (ready) return true;
    guard = xSemaphoreCreateMutex();
    if (!guard) return false;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    if (!esp_netif_create_default_wifi_sta()) return false;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG,"WiFi init: %s",esp_err_to_name(err)); return false; }
    esp_wifi_set_mode(WIFI_MODE_STA);
    err = esp_wifi_start();
    if (err != ESP_OK) { ESP_LOGE(TAG,"WiFi start: %s",esp_err_to_name(err)); return false; }
    ready = true;
    if (xTaskCreate(scan_task, "fh_wifi_scan", 6144, nullptr, 4, &worker) != pdPASS) {
        ready = false;
        return false;
    }
    return true;
}
void footholed_wifi_start() {
    if (!ready) return;
    xSemaphoreTake(guard, portMAX_DELAY);
    ap_count=0; ++generation; error_state=false; requested=true;
    xSemaphoreGive(guard);
}
void footholed_wifi_stop() {
    if (!ready) return;
    xSemaphoreTake(guard, portMAX_DELAY);
    requested=false;
    xSemaphoreGive(guard);
    // In-flight scan may finish; next cycle will not start until re-entered.
}
size_t footholed_wifi_snapshot(FootHoledAP *out, size_t capacity, uint32_t *gen, bool *running, bool *error) {
    if (!ready) { if(gen)*gen=0; if(running)*running=false; if(error)*error=true; return 0; }
    xSemaphoreTake(guard, portMAX_DELAY);
    size_t n = ap_count < capacity ? ap_count : capacity;
    if (out && n) memcpy(out, aps, n*sizeof(FootHoledAP));
    if (gen) *gen=generation;
    if (running) *running=requested;
    if (error) *error=error_state;
    xSemaphoreGive(guard);
    return n;
}
