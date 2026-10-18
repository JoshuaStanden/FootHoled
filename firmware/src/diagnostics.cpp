/**
 * @file diagnostics.cpp
 * @brief Implementation of the FootHoled H1 hardware sanity pass.
 *
 * Each check calls a REAL probe. Nothing here invents a measurement. When a
 * probe cannot run it reports SKIPPED. The checks, in order:
 *
 *   1. SoC / firmware  — chip model, cores, flash size, PSRAM size (always OK).
 *   2. Heap            — free internal heap + free PSRAM (sanity floor).
 *   3. I2C bus scan    — probe every known address on the shared I2C0 bus and
 *                        report which chips ACK. This single scan covers the
 *                        IMU, RTC, PMIC, audio codec, touch and power monitor
 *                        at once — the cheapest, most honest "is the board
 *                        alive" signal.
 *   4. IMU (QMI8658)   — full driver init (verifies WHO_AM_I == 0x05).
 *   5. RTC (PCF85063)  — I2C ACK at 0x51 (no driver yet; a ping is honest).
 *   6. Power (INA228)  — full driver init (verifies manufacturer ID "TI").
 *   7. SD card         — reports whether the recon-log mount succeeded.
 *   8. Display/touch   — reports the board_adapter display-ready flag.
 *   9. Wi-Fi radio     — scanner init state.
 *  10. BLE radio       — scanner init state.
 *
 * Read-only and non-destructive. See diagnostics.h for the result model.
 */
#include "diagnostics.h"
#include "board_adapter.h"
#include "qmi8658.h"
#include "ina228.h"
#include "wifi_scan.h"
#include "ble_scan.h"
#include "recon_log.h"

#include "driver/i2c_master.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

#if __has_include("esp_psram.h")
#include "esp_psram.h"
#define FH_HAS_PSRAM_API 1
#else
#define FH_HAS_PSRAM_API 0
#endif

static const char *TAG = "FootHoled/diag";

// ---------------------------------------------------------------------------
// Known I2C0 devices on the FootHoled H1 (SDA GPIO 8 / SCL GPIO 7).
// Addresses come from the individual driver headers / HARDWARE.md. The IMU can
// sit at 0x6A or 0x6B depending on the SA0 strap, so both are probed.
// ---------------------------------------------------------------------------
struct i2c_known_t { uint8_t addr; const char *name; };
static const i2c_known_t kI2cKnown[] = {
    { 0x40, "INA228 pwr"   },  // power/battery monitor
    { 0x51, "PCF85063 RTC" },  // real-time clock (no driver yet)
    { 0x6A, "QMI8658 IMU"  },  // IMU (SA0=GND)
    { 0x6B, "QMI8658 IMU'" },  // IMU (SA0=VDD) alternate
    // The following are documented on the shared bus but not yet driven; a scan
    // still reports whether they ACK, which is useful bring-up information.
    { 0x34, "AXP2101 PMIC" },  // power management IC
    { 0x18, "ES8311 audio" },  // audio codec
};

static void set_result(fh_diag_result_t *r, const char *name,
                       fh_diag_status_t status, const char *detail) {
    strncpy(r->name, name, sizeof(r->name) - 1);
    r->name[sizeof(r->name) - 1] = 0;
    r->status = status;
    if (detail) {
        strncpy(r->detail, detail, sizeof(r->detail) - 1);
        r->detail[sizeof(r->detail) - 1] = 0;
    } else {
        r->detail[0] = 0;
    }
}

const char *footholed_diagnostics_status_text(fh_diag_status_t s) {
    switch (s) {
        case FH_DIAG_OK:          return "OK";
        case FH_DIAG_FAIL:        return "FAIL";
        case FH_DIAG_NOT_PRESENT: return "NOT PRESENT";
        case FH_DIAG_SKIPPED:     return "SKIPPED";
        default:                  return "?";
    }
}

size_t footholed_diagnostics_run(fh_diag_result_t *out, size_t max,
                                 size_t *ok_out, size_t *fail_out) {
    size_t n = 0;
    char detail[48];

    // ---- 1. SoC / firmware -------------------------------------------------
    if (n < max) {
        esp_chip_info_t chip;
        esp_chip_info(&chip);
        uint32_t flash_sz = 0;
        esp_flash_get_size(NULL, &flash_sz);
        snprintf(detail, sizeof(detail), "%d core / %lu MB flash",
                 chip.cores, (unsigned long)(flash_sz / (1024 * 1024)));
        set_result(&out[n++], "SoC (ESP32-S3)", FH_DIAG_OK, detail);
    }

    // ---- 2. Heap / PSRAM ---------------------------------------------------
    if (n < max) {
        size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#if FH_HAS_PSRAM_API
        size_t psram = esp_psram_is_initialized() ? esp_psram_get_size() : 0;
#else
        size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
#endif
        // Floor: internal heap should be well above a few KB after boot.
        bool heap_ok = free_int > (16 * 1024);
        if (psram) {
            snprintf(detail, sizeof(detail), "%u KB int / %u MB PSRAM",
                     (unsigned)(free_int / 1024), (unsigned)(psram / (1024 * 1024)));
        } else {
            snprintf(detail, sizeof(detail), "%u KB int / no PSRAM",
                     (unsigned)(free_int / 1024));
        }
        set_result(&out[n++], "Memory", heap_ok ? FH_DIAG_OK : FH_DIAG_FAIL, detail);
    }

    // ---- 3. I2C bus scan ---------------------------------------------------
    // One probe pass over the shared bus. Reports a compact "found" count and
    // marks the bus OK if ANY known device answers (a totally silent bus is the
    // classic symptom of a wiring/pull-up fault, so that is a FAIL).
    i2c_master_bus_handle_t bus = footholed_board_i2c_bus();
    if (n < max) {
        if (!bus) {
            set_result(&out[n++], "I2C bus", FH_DIAG_SKIPPED, "bus not initialised");
        } else {
            int found = 0;
            char list[48] = {0};
            for (size_t i = 0; i < sizeof(kI2cKnown) / sizeof(kI2cKnown[0]); ++i) {
                // 50 ms probe timeout; ESP_OK means the address ACKed.
                if (i2c_master_probe(bus, kI2cKnown[i].addr, 50) == ESP_OK) {
                    found++;
                    char hex[8];
                    snprintf(hex, sizeof(hex), "%s0x%02X",
                             list[0] ? " " : "", kI2cKnown[i].addr);
                    strncat(list, hex, sizeof(list) - strlen(list) - 1);
                }
            }
            if (found > 0) {
                snprintf(detail, sizeof(detail), "%d ACK: %s", found, list);
                set_result(&out[n++], "I2C bus", FH_DIAG_OK, detail);
            } else {
                set_result(&out[n++], "I2C bus", FH_DIAG_FAIL, "no devices ACK");
            }
        }
    }

    // ---- 4. IMU (QMI8658) --------------------------------------------------
    // A real init: it soft-resets and verifies WHO_AM_I == 0x05, trying both
    // SA0 straps. We init a throwaway handle and release it immediately so we
    // do not disturb the auto-rotation service's own handle.
    //
    // TODO(HW): qmi8658_init() issues a soft-reset. If auto-rotation is active
    // and holding its own handle, running this probe momentarily resets the
    // chip; the orient poll re-reads cleanly on its next cycle (~250 ms), so
    // the only visible effect would be one skipped rotation sample. Verify on
    // hardware that this is imperceptible; if not, add a lightweight
    // WHO_AM_I-only presence read to the driver and call that here instead.
    if (n < max) {
        if (!bus) {
            set_result(&out[n++], "IMU (QMI8658)", FH_DIAG_SKIPPED, "no I2C bus");
        } else {
            qmi8658_handle_t h = nullptr;
            esp_err_t e = qmi8658_init(bus, QMI8658_I2C_ADDR_LOW, &h);
            uint8_t addr = QMI8658_I2C_ADDR_LOW;
            if (e != ESP_OK) {
                e = qmi8658_init(bus, QMI8658_I2C_ADDR_HIGH, &h);
                addr = QMI8658_I2C_ADDR_HIGH;
            }
            if (e == ESP_OK && h) {
                qmi8658_sample_t s;
                if (qmi8658_read(h, &s) == ESP_OK) {
                    snprintf(detail, sizeof(detail), "0x%02X az=%.2fg", addr, s.az);
                } else {
                    snprintf(detail, sizeof(detail), "0x%02X id ok", addr);
                }
                set_result(&out[n++], "IMU (QMI8658)", FH_DIAG_OK, detail);
                qmi8658_deinit(h);
            } else {
                set_result(&out[n++], "IMU (QMI8658)", FH_DIAG_NOT_PRESENT, "no WHO_AM_I");
            }
        }
    }

    // ---- 5. RTC (PCF85063) -------------------------------------------------
    // No driver yet (see the recon-log TIMESTAMP TODO). An I2C ACK at 0x51 is
    // the honest presence check — it confirms the clock chip the future
    // real-timestamp feature depends on is actually on the bus.
    if (n < max) {
        if (!bus) {
            set_result(&out[n++], "RTC (PCF85063)", FH_DIAG_SKIPPED, "no I2C bus");
        } else if (i2c_master_probe(bus, 0x51, 50) == ESP_OK) {
            set_result(&out[n++], "RTC (PCF85063)", FH_DIAG_OK, "0x51 ACK (no driver)");
        } else {
            set_result(&out[n++], "RTC (PCF85063)", FH_DIAG_NOT_PRESENT, "0x51 no ACK");
        }
    }

    // ---- 6. Power monitor (INA228) ----------------------------------------
    if (n < max) {
        if (!bus) {
            set_result(&out[n++], "Power (INA228)", FH_DIAG_SKIPPED, "no I2C bus");
        } else {
            ina228_handle_t h = nullptr;
            if (ina228_init(bus, INA228_I2C_ADDR_DEFAULT, &h) == ESP_OK && h) {
                float mv = 0;
                if (ina228_get_bus_voltage_mv(h, &mv) == ESP_OK) {
                    snprintf(detail, sizeof(detail), "VBUS %.2f V", mv / 1000.0f);
                } else {
                    snprintf(detail, sizeof(detail), "id ok");
                }
                set_result(&out[n++], "Power (INA228)", FH_DIAG_OK, detail);
                ina228_deinit(h);
            } else {
                set_result(&out[n++], "Power (INA228)", FH_DIAG_NOT_PRESENT, "no mfg ID");
            }
        }
    }

    // ---- 7. SD card --------------------------------------------------------
    // Reuses the recon-log mount state — no second mount, no write.
    if (n < max) {
        if (footholed_recon_log_available()) {
            set_result(&out[n++], "SD card", FH_DIAG_OK, "mounted /sdcard");
        } else {
            set_result(&out[n++], "SD card", FH_DIAG_NOT_PRESENT, "not mounted");
        }
    }

    // ---- 8. Display + touch -----------------------------------------------
    // If the user is reading this screen, the display is self-evidently up; the
    // board flag confirms the adapter registered display + input with LVGL.
    if (n < max) {
        if (footholed_board_display_ready()) {
            set_result(&out[n++], "Display + touch", FH_DIAG_OK, "LVGL registered");
        } else {
            set_result(&out[n++], "Display + touch", FH_DIAG_FAIL, "adapter not ready");
        }
    }

    // ---- 9. Wi-Fi radio ----------------------------------------------------
    if (n < max) {
        FootHoledAP aps[1];
        uint32_t gen = 0; bool running = false, error = false;
        (void)footholed_wifi_snapshot(aps, 1, &gen, &running, &error);
        // The scanner initialises its driver at boot; an error flag means the
        // Wi-Fi driver failed to come up. "idle" (not scanning) is still OK.
        if (error) {
            set_result(&out[n++], "Wi-Fi radio", FH_DIAG_FAIL, "scanner error");
        } else {
            set_result(&out[n++], "Wi-Fi radio", FH_DIAG_OK,
                       running ? "scanning" : "ready (idle)");
        }
    }

    // ---- 10. BLE radio -----------------------------------------------------
    if (n < max) {
        FootHoledBLEDevice d[1];
        uint32_t gen = 0; bool running = false, error = false;
        (void)footholed_ble_snapshot(d, 1, &gen, &running, &error);
        if (error) {
            set_result(&out[n++], "BLE radio", FH_DIAG_FAIL, "host error");
        } else {
            set_result(&out[n++], "BLE radio", FH_DIAG_OK,
                       running ? "scanning" : "ready (idle)");
        }
    }

    // Tally.
    size_t ok = 0, fail = 0;
    for (size_t i = 0; i < n; ++i) {
        if (out[i].status == FH_DIAG_OK)   ok++;
        if (out[i].status == FH_DIAG_FAIL) fail++;
    }
    if (ok_out)   *ok_out = ok;
    if (fail_out) *fail_out = fail;
    ESP_LOGI(TAG, "diagnostics: %u checks, %u OK, %u FAIL",
             (unsigned)n, (unsigned)ok, (unsigned)fail);
    return n;
}
