/**
 * @file splash.h
 * @brief Boot splash screen for the FootHoled H1 (ST7796 320x480 portrait).
 *
 * Shows a branded startup screen (FOOTHOLED wordmark, RECON/ANALYZE/EXPAND
 * tagline, animated boot bar) immediately at power-on, then automatically
 * fades out and hands control to footholed_ui_init() after a short delay.
 *
 * Branding note (see AGENTS.md rule #11 / docs/BRANDING.md): this screen is
 * built entirely from the FootHoled *name and tagline* styled with the brand
 * colours and simple geometry. It deliberately does NOT redraw or substitute
 * the approved shield/boot-print PNG logo — the only approved logo asset. If a
 * future iteration wants the actual shield graphic on the splash, convert
 * assets/footholed-logo.png to an LVGL RGB565 image and draw it here with
 * lv_image; do not hand-draw a second logo variant.
 */
#pragma once

#include <stdint.h>

/**
 * @brief Build and show the boot splash on a dedicated LVGL screen.
 *
 * Must be called AFTER lv_init() and footholed_board_init() (an LVGL display
 * must exist). The splash loads onto its own screen so it never pollutes the
 * active UI screen tree.
 *
 * The splash schedules its own dismissal via an LVGL timer: after
 * @p hold_ms it fades out over a short animation and then invokes
 * @p on_done exactly once (on the LVGL/main thread). Pass the UI builder
 * (e.g. footholed_ui_init) as @p on_done so the menu is created only once the
 * splash has finished.
 *
 * Non-blocking: returns immediately. All timing is driven by lv_timer_handler()
 * in the main loop, so the caller must keep pumping LVGL as usual.
 *
 * @param hold_ms   How long the fully-drawn splash stays before fading out.
 *                  Clamped to a sane minimum. Typical value ~1800 ms.
 * @param on_done   Callback invoked once, after the fade-out completes. May be
 *                  nullptr (then the splash just clears itself).
 */
void footholed_splash_show(uint32_t hold_ms, void (*on_done)(void));
