#pragma once
#include <stdint.h>
#include <stddef.h>

struct FootHoledAP {
    char ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t authmode;
};

// Thread-safe snapshot interface. Call init once after NVS init.
bool footholed_wifi_init();
void footholed_wifi_start();
void footholed_wifi_stop();
// Returns number copied, sets generation on each completed scan and running state.
size_t footholed_wifi_snapshot(FootHoledAP *out, size_t capacity, uint32_t *generation, bool *running, bool *error);
