/**
 * @file splash.cpp
 * @brief FootHoled H1 boot splash screen (LVGL 9, ST7796 320x480 portrait).
 *
 * Layout (portrait 320 wide x 480 tall), centred column:
 *
 *     +----------------------------+
 *     |         (top glow)         |
 *     |        /hex shield\        |   <- geometric hexagon outline
 *     |       |            |       |      (NOT the PNG logo — see splash.h)
 *     |        \__________/        |
 *     |                            |
 *     |       FOOT H O L E D       |   <- wordmark: white "FOOT" + cyan "HOLED"
 *     |   RECON · ANALYZE · EXPAND |   <- tagline in brand colours
 *     |                            |
 *     |     [####______] boot      |   <- animated boot progress bar
 *     +----------------------------+
 *
 * Colours are taken from the approved logo artwork and match the running UI
 * background (ui.cpp uses 0x0D1720 / 0xEAF6F9):
 *   - background : 0x0D1720  (near-black navy)
 *   - "FOOT"     : 0xF2FAFC  (off-white)
 *   - "HOLED"    : 0x1BA7DC  (brand cyan)
 *   - EXPAND /
 *     right glow : 0x2FB457  (brand green)
 *   - RECON /
 *     left glow  : 0x1BA7DC  (brand cyan)
 *
 * Lifetime / threading: everything here runs on the single LVGL/main thread
 * (lv_conf.h sets LV_USE_OS = LV_OS_NONE). The splash owns one screen object
 * and one one-shot timer; both are freed when the splash dismisses, so nothing
 * leaks into the application UI. No peripheral/driver access occurs here.
 */
#include "splash.h"
#include "lvgl.h"

namespace {

// Brand palette (RGB565 will be derived by LVGL from these 24-bit values).
constexpr uint32_t COL_BG      = 0x0D1720;  // matches ui.cpp screen background
constexpr uint32_t COL_WHITE   = 0xF2FAFC;  // "FOOT", "ANALYZE"
constexpr uint32_t COL_CYAN    = 0x1BA7DC;  // "HOLED", "RECON", left accent
constexpr uint32_t COL_GREEN   = 0x2FB457;  // "EXPAND", right accent
constexpr uint32_t COL_SHIELD  = 0x24323C;  // subtle shield fill vs. background

// Fade-out duration once the hold period elapses.
constexpr uint32_t FADE_MS      = 350;
// Never flash the splash for an imperceptible instant.
constexpr uint32_t MIN_HOLD_MS  = 400;

lv_obj_t* s_screen   = nullptr;   // the splash's own screen (freed on dismiss)
lv_obj_t* s_bar      = nullptr;   // boot progress bar
void (*s_on_done)(void) = nullptr;

// ---- dismissal -----------------------------------------------------------

// Called by LVGL once the fade-out animation has fully completed. Deletes the
// splash screen (loading the app screen first so there is always a valid
// active screen) and fires the on_done callback exactly once.
void splash_cleanup(lv_anim_t*) {
    void (*done)(void) = s_on_done;
    s_on_done = nullptr;

    // Hand the display to a fresh application screen BEFORE deleting ours, so
    // lv_screen_active() is never momentarily dangling.
    lv_obj_t* app = lv_obj_create(nullptr);
    lv_screen_load(app);

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
    }
    s_bar = nullptr;

    // Build the real UI on the now-active screen.
    if (done) done();
}

// Fade the whole splash screen's opacity to transparent, then clean up.
void start_fade_out() {
    if (!s_screen) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_screen);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, FADE_MS);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
    });
    lv_anim_set_completed_cb(&a, splash_cleanup);
    lv_anim_start(&a);
}

// One-shot hold timer: fires once after hold_ms, deletes itself, starts fade.
void hold_timer_cb(lv_timer_t* t) {
    lv_timer_delete(t);
    start_fade_out();
}

// Animate the boot bar from 0 to 100 across the hold period for a sense of
// progress. This is cosmetic only — it is NOT tied to real init state.
void animate_boot_bar(uint32_t hold_ms) {
    if (!s_bar) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_bar);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, hold_ms);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_bar_set_value((lv_obj_t*)obj, v, LV_ANIM_OFF);
    });
    lv_anim_start(&a);
}

// Draw a flat-topped hexagon outline as a lightweight "shield" motif using an
// lv_line. This is deliberately simple geometry, not the logo artwork.
void build_shield(lv_obj_t* parent) {
    // Hexagon points (local coords, ~120x132 box). Pointy left/right flats.
    static lv_point_precise_t pts[] = {
        {60,  0}, {118, 33}, {118, 99}, {60, 132}, {2, 99}, {2, 33}, {60, 0},
    };
    lv_obj_t* hex = lv_line_create(parent);
    lv_line_set_points(hex, pts, sizeof(pts) / sizeof(pts[0]));
    lv_obj_set_style_line_width(hex, 3, 0);
    lv_obj_set_style_line_color(hex, lv_color_hex(COL_CYAN), 0);
    lv_obj_set_style_line_rounded(hex, true, 0);
    lv_obj_set_size(hex, 120, 134);

    // A short monospace-ish "FH" sits inside the shield as a mark.
    lv_obj_t* mark = lv_label_create(hex);
    lv_label_set_text(mark, "FH");
    lv_obj_set_style_text_color(mark, lv_color_hex(COL_WHITE), 0);
    lv_obj_set_style_text_font(mark, &lv_font_montserrat_28, 0);
    lv_obj_center(mark);
}

}  // namespace

void footholed_splash_show(uint32_t hold_ms, void (*on_done)(void)) {
    if (hold_ms < MIN_HOLD_MS) hold_ms = MIN_HOLD_MS;
    s_on_done = on_done;

    // Dedicated screen so the splash is isolated from the app UI tree.
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Vertical centred column for all content.
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_screen, 14, 0);

    build_shield(s_screen);

    // Wordmark: "FOOT" white + "HOLED" cyan, drawn as two inline labels in a
    // transparent row container.
    lv_obj_t* word = lv_obj_create(s_screen);
    lv_obj_remove_style_all(word);
    lv_obj_set_size(word, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(word, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(word, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* foot = lv_label_create(word);
    lv_label_set_text(foot, "FOOT");
    lv_obj_set_style_text_color(foot, lv_color_hex(COL_WHITE), 0);
    lv_obj_set_style_text_font(foot, &lv_font_montserrat_28, 0);

    lv_obj_t* holed = lv_label_create(word);
    lv_label_set_text(holed, "HOLED");
    lv_obj_set_style_text_color(holed, lv_color_hex(COL_CYAN), 0);
    lv_obj_set_style_text_font(holed, &lv_font_montserrat_28, 0);

    // Tagline: RECON (cyan) · ANALYZE (white) · EXPAND (green).
    lv_obj_t* tag = lv_obj_create(s_screen);
    lv_obj_remove_style_all(tag);
    lv_obj_set_size(tag, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tag, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tag, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tag, 6, 0);

    struct { const char* text; uint32_t col; } parts[] = {
        {"RECON",   COL_CYAN},
        {"\xC2\xB7", COL_WHITE},   // middle dot (U+00B7) in UTF-8
        {"ANALYZE", COL_WHITE},
        {"\xC2\xB7", COL_WHITE},
        {"EXPAND",  COL_GREEN},
    };
    for (auto& p : parts) {
        lv_obj_t* l = lv_label_create(tag);
        lv_label_set_text(l, p.text);
        lv_obj_set_style_text_color(l, lv_color_hex(p.col), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    }

    // Boot progress bar.
    s_bar = lv_bar_create(s_screen);
    lv_obj_set_size(s_bar, 180, 6);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(COL_SHIELD), 0);           // track
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 3, 0);
    lv_obj_set_style_radius(s_bar, 3, LV_PART_INDICATOR);

    // Make this screen the active one and run the animations.
    lv_screen_load(s_screen);
    animate_boot_bar(hold_ms);
    lv_timer_create(hold_timer_cb, hold_ms, nullptr);
}
