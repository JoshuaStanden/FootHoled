/**
 * @file boot_log.cpp
 * @brief Boot / reset-reason diagnostics log. See boot_log.h.
 *
 * Owns the reset-reason decoding, the NVS boot counter, and the CSV row format.
 * It does NOT own or touch the SD card: the formatted row is written via
 * footholed_recon_log_append_line() (recon_log.cpp), which holds the single SD
 * mutex. This keeps SD ownership in one place while giving the reboot log its
 * own clearly-named module.
 */
#include "boot_log.h"
#include "recon_log.h"      // footholed_recon_log_append_line()
#include "esp_log.h"
#include "esp_heap_caps.h"  // heap_caps_* for the internal-RAM heap sample
#include "esp_timer.h"      // esp_timer_get_time() for the uptime column
#include "nvs.h"
#include <stdio.h>

namespace {
const char *TAG = "FootHoled/bootlog";

constexpr const char *BOOT_LOG_PATH = "/sdcard/boot_log.csv";
constexpr const char *BOOT_CSV_HEADER =
    "boot_count,reset_reason,reason_detail,free_heap,min_free_heap_ever,largest_block\n";

constexpr const char *HEAP_LOG_PATH = "/sdcard/heap_log.csv";
constexpr const char *HEAP_CSV_HEADER =
    "uptime_s,context,free_total,free_internal,min_free_internal,largest_internal\n";

// Human-readable name + gloss for the ESP-IDF esp_reset_reason_t values. Kept
// as plain ints so the caller passes (int)esp_reset_reason() without this file
// needing to agree on the enum header. Values are the stable ordinals.
void reset_reason_text(int r, const char **name, const char **detail) {
    switch (r) {
        case 1:  *name="ESP_RST_POWERON";  *detail="normal power-on / reset button"; break;
        case 2:  *name="ESP_RST_EXT";      *detail="external pin reset"; break;
        case 3:  *name="ESP_RST_SW";       *detail="software restart (esp_restart)"; break;
        case 4:  *name="ESP_RST_PANIC";    *detail="crash / panic / abort (incl. heap-alloc failure)"; break;
        case 5:  *name="ESP_RST_INT_WDT";  *detail="interrupt watchdog"; break;
        case 6:  *name="ESP_RST_TASK_WDT"; *detail="task watchdog (a task hung)"; break;
        case 7:  *name="ESP_RST_WDT";      *detail="other watchdog"; break;
        case 8:  *name="ESP_RST_DEEPSLEEP";*detail="wake from deep sleep"; break;
        case 9:  *name="ESP_RST_BROWNOUT"; *detail="brownout (voltage sag)"; break;
        case 10: *name="ESP_RST_SDIO";     *detail="SDIO reset"; break;
        default: *name="ESP_RST_UNKNOWN";  *detail="unknown reset cause"; break;
    }
}

// Next monotonic boot number from a dedicated NVS counter (namespace "fh_boot",
// key "n"). Independent of the settings service. Returns 0 if NVS is
// unavailable (the row still logs, just with boot_count 0).
uint32_t next_boot_count() {
    nvs_handle_t h;
    if (nvs_open("fh_boot", NVS_READWRITE, &h) != ESP_OK) return 0;
    uint32_t n = 0;
    nvs_get_u32(h, "n", &n);   // leaves n=0 if the key is absent (first boot)
    n++;
    if (nvs_set_u32(h, "n", n) == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return n;
}
} // namespace

void footholed_boot_log_record(int reset_reason,
                               uint32_t free_heap,
                               uint32_t min_free_heap_ever,
                               uint32_t largest_block)
{
    const char *name = nullptr, *detail = nullptr;
    reset_reason_text(reset_reason, &name, &detail);
    uint32_t boot_count = next_boot_count();

    // Always report on serial, even with no card.
    ESP_LOGI(TAG, "boot #%u  reset=%s (%s)  free_heap=%u  min_free_ever=%u  largest_block=%u",
             (unsigned)boot_count, name, detail,
             (unsigned)free_heap, (unsigned)min_free_heap_ever, (unsigned)largest_block);

    // Format the row and hand it to the SD owner (recon_log) for a locked write.
    // reason_detail is quoted (it contains spaces / slashes).
    char line[192];
    snprintf(line, sizeof(line), "%u,%s,\"%s\",%u,%u,%u\n",
             (unsigned)boot_count, name, detail,
             (unsigned)free_heap, (unsigned)min_free_heap_ever,
             (unsigned)largest_block);

    if (footholed_recon_log_append_line(BOOT_LOG_PATH, BOOT_CSV_HEADER, line)) {
        ESP_LOGI(TAG, "boot log appended to %s", BOOT_LOG_PATH);
    } else {
        ESP_LOGW(TAG, "boot log not persisted (no SD card)");
    }
}

void footholed_boot_log_heap_sample(const char *context)
{
    if (!context) context = "";

    const uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    // Total free heap (PSRAM-dominated, ~7 MB) — kept for comparison. Use the
    // heap_caps form (MALLOC_CAP_DEFAULT) so the symbol comes from
    // esp_heap_caps.h, which this file already includes.
    const uint32_t free_total =
        (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    // Internal RAM only — the scarce ~512 KB the radio stacks fight over. This
    // is the figure that actually trends toward zero before a panic.
    const uint32_t free_internal =
        (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t min_free_internal =
        (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t largest_internal =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    ESP_LOGI(TAG, "heap[%s] free_total=%u free_int=%u min_int=%u largest_int=%u",
             context, (unsigned)free_total, (unsigned)free_internal,
             (unsigned)min_free_internal, (unsigned)largest_internal);

    char line[160];
    snprintf(line, sizeof(line), "%u,%s,%u,%u,%u,%u\n",
             (unsigned)uptime_s, context,
             (unsigned)free_total, (unsigned)free_internal,
             (unsigned)min_free_internal, (unsigned)largest_internal);

    // Best-effort; no-ops to serial-only when there is no card. Not every
    // sample needs to reach the card, so a failed append is not logged here
    // (the serial line above already carried the data).
    (void)footholed_recon_log_append_line(HEAP_LOG_PATH, HEAP_CSV_HEADER, line);
}
