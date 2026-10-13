/**
 * @file settings.cpp
 * @brief FootHoled persistent settings service — NVS implementation.
 *
 * Storage: ESP-IDF NVS flash, namespace "fh_settings".
 * Thread safety: each setter opens/commits/closes its own NVS handle, so
 * concurrent writes from different tasks are safe at the NVS level.
 * The in-RAM cache (s_cfg) is written under a FreeRTOS mutex.
 *
 * First-boot behaviour: if a key is absent from NVS the getter returns the
 * compile-time default; the setter writes it the first time the user changes
 * a value.  Calling footholed_settings_reset_defaults() writes all defaults
 * explicitly so they survive erase-nvs operations.
 */

#include "settings.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG        = "FootHoled/settings";
static const char *NVS_NS     = "fh_settings";

// NVS key strings — must be ≤15 chars (ESP-IDF NVS limit)
static const char *KEY_BRIGHTNESS = "brightness";
static const char *KEY_PRIVACY_ON = "privacy_on";
static const char *KEY_MASK_LEN   = "mask_len";
static const char *KEY_AUTO_ROTATE = "auto_rotate";

// ---------------------------------------------------------------------------
// Compile-time defaults
// ---------------------------------------------------------------------------
#define DEFAULT_BRIGHTNESS  75u
#define DEFAULT_PRIVACY_ON  1u
#define DEFAULT_MASK_LEN    4u
#define DEFAULT_AUTO_ROTATE 1u

// ---------------------------------------------------------------------------
// In-RAM settings cache
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t brightness;   ///< Backlight %, 0–100
    uint8_t privacy_on;   ///< 0 = off, 1 = on
    uint8_t mask_length;  ///< 1–15 characters
    uint8_t auto_rotate;  ///< 0 = off, 1 = on (IMU display auto-rotation)
} fh_settings_t;

static fh_settings_t   s_cfg      = {};
static SemaphoreHandle_t s_mutex  = nullptr;
static bool              s_ready  = false;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/** Acquire the cache mutex.  Returns false if the service is not ready. */
static bool lock(void) {
    if (!s_ready || !s_mutex) return false;
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
}

static void unlock(void) {
    xSemaphoreGive(s_mutex);
}

/**
 * @brief Open NVS, write one uint8 key, close.
 *
 * Errors are logged but not fatal — the in-RAM value is already updated.
 * If NVS is full or corrupted a TODO(HW) note is left for the user.
 */
static void nvs_write_u8(const char *key, uint8_t value) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed (%s) writing '%s'", esp_err_to_name(err), key);
        return;
    }
    err = nvs_set_u8(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs write/commit failed (%s) key='%s'", esp_err_to_name(err), key);
        // TODO(HW): surface a low-storage warning to the UI if err == ESP_ERR_NVS_NOT_ENOUGH_SPACE
    }
    nvs_close(h);
}

/** Read one uint8 from NVS; returns def_val if key is absent or NVS errors. */
static uint8_t nvs_read_u8(nvs_handle_t h, const char *key, uint8_t def_val) {
    uint8_t val = def_val;
    esp_err_t err = nvs_get_u8(h, key, &val);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Key '%s' not found — using default %u", key, def_val);
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_get_u8 '%s' error %s — using default", key, esp_err_to_name(err));
    }
    return val;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool footholed_settings_init(void) {
    if (s_ready) return true;  // idempotent

    // Initialise NVS flash partition (safe to call if already done by board init)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated or version changed — erase and retry.
        // This loses stored settings but prevents a boot loop.
        ESP_LOGW(TAG, "NVS partition issue (%s), erasing and reinitialising",
                 esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        // Apply compile-time defaults so the rest of the firmware still works
        s_cfg.brightness  = DEFAULT_BRIGHTNESS;
        s_cfg.privacy_on  = DEFAULT_PRIVACY_ON;
        s_cfg.mask_length = DEFAULT_MASK_LEN;
        s_cfg.auto_rotate = DEFAULT_AUTO_ROTATE;
        return false;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create settings mutex");
        return false;
    }

    // Read stored values (or defaults on first boot)
    nvs_handle_t h;
    err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        s_cfg.brightness  = nvs_read_u8(h, KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS);
        s_cfg.privacy_on  = nvs_read_u8(h, KEY_PRIVACY_ON, DEFAULT_PRIVACY_ON);
        s_cfg.mask_length = nvs_read_u8(h, KEY_MASK_LEN,   DEFAULT_MASK_LEN);
        s_cfg.auto_rotate = nvs_read_u8(h, KEY_AUTO_ROTATE, DEFAULT_AUTO_ROTATE);
        nvs_close(h);
    } else {
        // Namespace doesn't exist yet (genuine first boot) — use defaults
        ESP_LOGI(TAG, "No settings found in NVS — applying defaults");
        s_cfg.brightness  = DEFAULT_BRIGHTNESS;
        s_cfg.privacy_on  = DEFAULT_PRIVACY_ON;
        s_cfg.mask_length = DEFAULT_MASK_LEN;
        s_cfg.auto_rotate = DEFAULT_AUTO_ROTATE;
    }

    // Clamp values in case NVS held out-of-range data from a prior firmware
    if (s_cfg.brightness > 100)  s_cfg.brightness  = DEFAULT_BRIGHTNESS;
    if (s_cfg.privacy_on > 1)    s_cfg.privacy_on   = DEFAULT_PRIVACY_ON;
    if (s_cfg.mask_length < 1 || s_cfg.mask_length > 15) {
        s_cfg.mask_length = DEFAULT_MASK_LEN;
    }
    if (s_cfg.auto_rotate > 1)   s_cfg.auto_rotate = DEFAULT_AUTO_ROTATE;

    s_ready = true;
    ESP_LOGI(TAG, "Settings loaded — brightness=%u%% privacy=%s mask_len=%u auto_rotate=%s",
             s_cfg.brightness,
             s_cfg.privacy_on ? "on" : "off",
             s_cfg.mask_length,
             s_cfg.auto_rotate ? "on" : "off");
    return true;
}

// ---------------------------------------------------------------------------
// Brightness
// ---------------------------------------------------------------------------

uint8_t footholed_settings_get_brightness(void) {
    if (!s_ready) return DEFAULT_BRIGHTNESS;
    if (!lock())  return DEFAULT_BRIGHTNESS;
    uint8_t v = s_cfg.brightness;
    unlock();
    return v;
}

void footholed_settings_set_brightness(uint8_t value) {
    if (value > 100) value = 100;
    if (!lock()) return;
    s_cfg.brightness = value;
    unlock();
    nvs_write_u8(KEY_BRIGHTNESS, value);
    ESP_LOGD(TAG, "brightness -> %u%%", value);
}

// ---------------------------------------------------------------------------
// Privacy on/off
// ---------------------------------------------------------------------------

bool footholed_settings_get_privacy_on(void) {
    if (!s_ready) return (bool)DEFAULT_PRIVACY_ON;
    if (!lock())  return (bool)DEFAULT_PRIVACY_ON;
    bool v = (bool)s_cfg.privacy_on;
    unlock();
    return v;
}

void footholed_settings_set_privacy_on(bool on) {
    if (!lock()) return;
    s_cfg.privacy_on = on ? 1u : 0u;
    unlock();
    nvs_write_u8(KEY_PRIVACY_ON, on ? 1u : 0u);
    ESP_LOGD(TAG, "privacy_on -> %s", on ? "true" : "false");
}

// ---------------------------------------------------------------------------
// Display auto-rotation on/off
// ---------------------------------------------------------------------------

bool footholed_settings_get_auto_rotate(void) {
    if (!s_ready) return (bool)DEFAULT_AUTO_ROTATE;
    if (!lock())  return (bool)DEFAULT_AUTO_ROTATE;
    bool v = (bool)s_cfg.auto_rotate;
    unlock();
    return v;
}

void footholed_settings_set_auto_rotate(bool on) {
    if (!lock()) return;
    s_cfg.auto_rotate = on ? 1u : 0u;
    unlock();
    nvs_write_u8(KEY_AUTO_ROTATE, on ? 1u : 0u);
    ESP_LOGD(TAG, "auto_rotate -> %s", on ? "true" : "false");
}

// ---------------------------------------------------------------------------
// Mask length
// ---------------------------------------------------------------------------

uint8_t footholed_settings_get_mask_length(void) {
    if (!s_ready) return DEFAULT_MASK_LEN;
    if (!lock())  return DEFAULT_MASK_LEN;
    uint8_t v = s_cfg.mask_length;
    unlock();
    return v;
}

void footholed_settings_set_mask_length(uint8_t len) {
    if (len < 1)  len = 1;
    if (len > 15) len = 15;
    if (!lock()) return;
    s_cfg.mask_length = len;
    unlock();
    nvs_write_u8(KEY_MASK_LEN, len);
    ESP_LOGD(TAG, "mask_length -> %u", len);
}

// ---------------------------------------------------------------------------
// Reset to defaults
// ---------------------------------------------------------------------------

void footholed_settings_reset_defaults(void) {
    footholed_settings_set_brightness(DEFAULT_BRIGHTNESS);
    footholed_settings_set_privacy_on((bool)DEFAULT_PRIVACY_ON);
    footholed_settings_set_mask_length(DEFAULT_MASK_LEN);
    footholed_settings_set_auto_rotate((bool)DEFAULT_AUTO_ROTATE);
    ESP_LOGI(TAG, "Settings reset to defaults");
}
