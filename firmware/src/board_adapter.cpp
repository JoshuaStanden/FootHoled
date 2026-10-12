/**
 * @file board_adapter.cpp
 * @brief Hardware-specific display and touch initialization boundary.
 *
 * TODO(HW): Integrate and verify the Waveshare 3.5B AXS15231B BSP. Register
 * the LVGL display and touch input device before returning true. Do not
 * substitute drivers or pin assignments from the non-B board variant.
 */
#include "board_adapter.h"
#include "esp_log.h"

bool footholed_board_init() {
    ESP_LOGW("FootHoled", "Waveshare 3.5B AXS15231B QSPI display/touch BSP NOT integrated. Headless LVGL scaffold only.");
    return false;
}
