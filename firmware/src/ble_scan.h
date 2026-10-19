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
    // --- capability info parsed from the advertisement (for the recon log) ---
    uint8_t  adv_flags;        // AD Flags byte (0 if not advertised)
    uint16_t appearance;       // GAP Appearance (0 if not advertised)
    int8_t   tx_power;         // advertised TX power dBm (TX_POWER_NONE if absent)
    uint16_t svc_uuids[8];     // advertised 16-bit service UUIDs (capabilities)
    uint8_t  svc_uuid_count;   // number of valid entries in svc_uuids
    uint16_t mfg_id;           // manufacturer ID from mfg-specific data (MFG_NONE if absent)
};

// Sentinels for "field not present in the advertisement".
constexpr int8_t   FH_BLE_TX_POWER_NONE = INT8_MIN;
constexpr uint16_t FH_BLE_MFG_NONE      = 0xFFFF;

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
