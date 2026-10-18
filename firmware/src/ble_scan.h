#pragma once
#include <stdint.h>
#include <stddef.h>

// One discovered Bluetooth Low Energy advertiser.
// Mirrors the FootHoledAP shape used by the Wi-Fi scanner so the UI layer
// treats both recon services identically.
struct FootHoledBLEDevice {
    char name[33];      // advertised local name, "" if none was advertised
    uint8_t addr[6];    // device address (public or random), MSB-first
    uint8_t addr_type;  // 0 = public, 1 = random (BLE_ADDR_* from the controller)
    int8_t rssi;        // signal strength in dBm
    uint8_t adv_type;   // advertising PDU type (connectable/scannable/etc.)
};

// Thread-safe snapshot interface, identical in shape to the Wi-Fi scanner.
// Call init once after NVS init (and after the Wi-Fi scanner, since both share
// the radio coexistence stack). start()/stop() gate the scan worker; snapshot()
// copies the current device table under a mutex.
bool footholed_ble_init();
void footholed_ble_start();
void footholed_ble_stop();
// Returns number copied, sets generation on each completed scan plus running
// and error state. A new generation means the table changed since last read.
size_t footholed_ble_snapshot(FootHoledBLEDevice *out, size_t capacity,
                              uint32_t *generation, bool *running, bool *error);
