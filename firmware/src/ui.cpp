/**
 * @file ui.cpp
 * @brief Experimental LVGL 9 menu navigation for FootHoled H1.
 *
 * This screen tree is a navigable scaffold, not a hardware integration.
 * Recon/Analyze/Expand actions deliberately route to an unavailable-service
 * screen. Settings are persisted to NVS via the settings service.
 *
 * Maintenance: keep hardware access outside this file; introduce service
 * interfaces instead of calling peripheral drivers from LVGL callbacks.
 */
#include "ui.h"
#include "splash_logo.h"
#include "settings.h"
#include "wifi_scan.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <initializer_list>

namespace {
// FootHoled dark purple palette (RGB888 hex)
constexpr uint32_t BG = 0x10121A;
constexpr uint32_t SURFACE = 0x1B1E2A;
constexpr uint32_t BUTTON = 0x252938;
constexpr uint32_t BORDER = 0x484C60;
constexpr uint32_t PURPLE = 0x7C3AED;
constexpr uint32_t PURPLE_ACTIVE = 0x3C285C;
constexpr uint32_t TEXT = 0xF2F2F7;

lv_obj_t* screen = nullptr;
static lv_style_t button_style;
static lv_style_t button_pressed_style;
static lv_style_t panel_style;
static bool styles_ready = false;

static void setup_theme() {
    if (styles_ready) return;
    styles_ready = true;
    lv_style_init(&button_style);
    lv_style_set_bg_color(&button_style, lv_color_hex(BUTTON));
    lv_style_set_bg_opa(&button_style, LV_OPA_COVER);
    lv_style_set_text_color(&button_style, lv_color_hex(TEXT));
    lv_style_set_border_color(&button_style, lv_color_hex(BORDER));
    lv_style_set_border_width(&button_style, 1);
    lv_style_set_radius(&button_style, 10);
    lv_style_set_shadow_width(&button_style, 0);
    lv_style_init(&button_pressed_style);
    lv_style_set_bg_color(&button_pressed_style, lv_color_hex(PURPLE_ACTIVE));
    lv_style_set_border_color(&button_pressed_style, lv_color_hex(PURPLE));
    lv_style_init(&panel_style);
    lv_style_set_bg_color(&panel_style, lv_color_hex(SURFACE));
    lv_style_set_bg_opa(&panel_style, LV_OPA_COVER);
    lv_style_set_border_color(&panel_style, lv_color_hex(BORDER));
    lv_style_set_border_width(&panel_style, 1);
    lv_style_set_text_color(&panel_style, lv_color_hex(TEXT));
    lv_style_set_radius(&panel_style, 10);
}
static lv_obj_t* themed_button(lv_obj_t* parent, bool primary = false) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_add_style(b, &button_style, LV_PART_MAIN);
    lv_obj_add_style(b, &button_pressed_style,
        static_cast<lv_style_selector_t>(LV_PART_MAIN) |
        static_cast<lv_style_selector_t>(LV_STATE_PRESSED));
    if (primary) lv_obj_set_style_bg_color(b, lv_color_hex(PURPLE), LV_PART_MAIN);
    return b;
}
void navigate(const char* section);
static void splash_done(lv_timer_t* timer) {
    lv_timer_del(timer);
    navigate("Home");
}

lv_obj_t* content = nullptr;
lv_obj_t* wifi_list = nullptr;
lv_obj_t* wifi_status = nullptr;
lv_timer_t* wifi_timer = nullptr;
uint32_t wifi_last_generation = UINT32_MAX;

void mask_identifier(char *s) {
    if (!footholed_settings_get_privacy_on()) return;
    size_t len = strlen(s);
    size_t mask = footholed_settings_get_mask_length();
    if (mask > len) mask = len;
    for (size_t i=0; i<mask; ++i) s[i]='*';
}
const char* auth_name(uint8_t a) {
    switch (a) {
        case 0: return "Open";
        case 1: return "WEP";
        case 2: return "WPA";
        case 3: return "WPA2";
        case 4: return "WPA/WPA2";
        case 5: return "Enterprise";
        case 6: return "WPA3";
        case 7: return "WPA2/WPA3";
        default: return "Secured";
    }
}
void wifi_refresh(lv_timer_t*) {
    if (!wifi_list || !wifi_status) return;
    FootHoledAP aps[48];
    uint32_t gen=0;
    bool running=false, error=false;
    size_t n=footholed_wifi_snapshot(aps,48,&gen,&running,&error);
    if (gen == wifi_last_generation && !error) return;
    wifi_last_generation=gen;
    if (error) lv_label_set_text(wifi_status,"Wi-Fi scan failed; retrying...");
    else if (!n) lv_label_set_text(wifi_status,"Scanning for access points...");
    else lv_label_set_text_fmt(wifi_status,"%u APs found / refreshing",(unsigned)n);
    lv_obj_clean(wifi_list);
    for (size_t i=0;i<n;++i) {
        char ssid[33];
        strncpy(ssid, aps[i].ssid, sizeof(ssid)); ssid[32]=0;
        if (!ssid[0]) strcpy(ssid,"<hidden>");
        char mac[18];
        snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",
                 aps[i].bssid[0],aps[i].bssid[1],aps[i].bssid[2],
                 aps[i].bssid[3],aps[i].bssid[4],aps[i].bssid[5]);
        mask_identifier(ssid); mask_identifier(mac);
        char line[180];
        snprintf(line,sizeof(line),"%s\n%s  |  %d dBm  |  CH %u  |  %s",
                 ssid,mac,(int)aps[i].rssi,(unsigned)aps[i].channel,auth_name(aps[i].authmode));
        lv_obj_t* entry=lv_label_create(wifi_list);
        lv_label_set_text(entry,line);
        lv_obj_set_width(entry,lv_pct(100));
        lv_label_set_long_mode(entry,LV_LABEL_LONG_WRAP);
        lv_obj_set_style_pad_bottom(entry,8,0);
    }
}
void on_wifi(lv_event_t*) { navigate("WiFi"); }

char current_section[32] = "Home";

void label(lv_obj_t* parent, const char* text, int font_size_unused = 0) {
    (void)font_size_unused;
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
}
void navigate(const char* section);
void on_home(lv_event_t*) { navigate("Home"); }
void on_back(lv_event_t*) { navigate("Home"); }
void on_settings(lv_event_t*) { navigate("Settings"); }
void on_nav(lv_event_t* e) { navigate((const char*)lv_event_get_user_data(e)); }
// Temporary destination for controls whose hardware services are unfinished.
// The UI must never present simulated results as real measurements.
void on_stub(lv_event_t* e) {
    const char* name = (const char*)lv_event_get_user_data(e);
    lv_obj_clean(content);
    label(content, name);
    label(content, "Not implemented: hardware service not connected.");
    lv_obj_t* b = themed_button(content);
    lv_obj_t* t = lv_label_create(b); lv_label_set_text(t, "Back");
    lv_obj_add_event_cb(b, on_back, LV_EVENT_CLICKED, nullptr);
}
void button(lv_obj_t* parent, const char* text, lv_event_cb_t callback, void* data) {
    lv_obj_t* b = themed_button(parent);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_add_event_cb(b, callback, LV_EVENT_CLICKED, data);
    lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, text); lv_obj_center(l);
}
// Preview uses synthetic identifiers; this is not yet the shared masking API.
void on_mask(lv_event_t* e) {
    int new_len = (int)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    footholed_settings_set_mask_length((uint8_t)new_len);
    lv_obj_t* preview = (lv_obj_t*)lv_event_get_user_data(e);
    char buf[80]; snprintf(buf,sizeof(buf),"Preview: %.*s%s",new_len,"***************","-demo-SSID");
    lv_label_set_text(preview,buf);
}
void on_privacy(lv_event_t* e) {
    bool on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    footholed_settings_set_privacy_on(on);
}
void on_auto_rotate(lv_event_t* e) {
    bool on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    footholed_settings_set_auto_rotate(on);
}
// Rebuild the simple alpha screen tree when navigating. A later iteration can
// introduce reusable views and a back-stack without coupling to drivers.
void navigate(const char* section) {
    strncpy(current_section,section,sizeof(current_section)-1);
    current_section[sizeof(current_section)-1] = 0;
    if (wifi_timer) {
        lv_timer_del(wifi_timer);
        wifi_timer = nullptr;
        wifi_list = nullptr;
        wifi_status = nullptr;
        footholed_wifi_stop();
    }
    lv_obj_clean(screen);
    lv_obj_set_style_pad_all(screen, 12, 0);
    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text_fmt(title,"FootHoled H1 / %s",current_section);
    lv_obj_set_width(title,lv_pct(100));
    lv_obj_set_style_text_color(title, lv_color_hex(0xA78BFA), 0);
    lv_obj_t* top = lv_obj_create(screen);
    lv_obj_set_width(top,lv_pct(100)); lv_obj_set_height(top,54);
    lv_obj_add_style(top, &panel_style, LV_PART_MAIN);
    lv_obj_set_flex_flow(top,LV_FLEX_FLOW_ROW);
    lv_obj_t* h=themed_button(top, true); lv_obj_add_event_cb(h,on_home,LV_EVENT_CLICKED,nullptr);
    lv_obj_t* hl=lv_label_create(h);lv_label_set_text(hl,"Home");
    lv_obj_t* s=themed_button(top); lv_obj_add_event_cb(s,on_settings,LV_EVENT_CLICKED,nullptr);
    lv_obj_t* sl=lv_label_create(s);lv_label_set_text(sl,"Settings");
    content=lv_obj_create(screen);
    lv_obj_set_width(content,lv_pct(100));
    lv_obj_add_style(content, &panel_style, LV_PART_MAIN);
    lv_obj_set_flex_grow(content,1);
    lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
    if (!strcmp(section,"Home")) {
        label(content,"Recon. Analyze. Expand.");
        button(content,"RECON",on_nav,(void*)"Recon");
        button(content,"ANALYZE",on_nav,(void*)"Analyze");
        button(content,"EXPAND",on_nav,(void*)"Expand");
    } else if (!strcmp(section,"Recon")) {
        label(content,"Discover and collect");
        button(content,"Wi-Fi",on_wifi,nullptr);
        for(const char* name : {"Bluetooth / BLE","GNSS","NFC / RFID","Infrared","Radio tools"}) button(content,name,on_stub,(void*)name);
    } else if (!strcmp(section,"WiFi")) {
        label(content,"Nearby Wi-Fi access points");
        wifi_status=lv_label_create(content);
        lv_label_set_text(wifi_status,"Scanning for access points...");
        wifi_list=lv_obj_create(content);
        lv_obj_set_width(wifi_list,lv_pct(100));
        lv_obj_set_flex_grow(wifi_list,1);
        lv_obj_set_flex_flow(wifi_list,LV_FLEX_FLOW_COLUMN);
        lv_obj_add_style(wifi_list,&panel_style,LV_PART_MAIN);
        lv_obj_set_scroll_dir(wifi_list,LV_DIR_VER);
        wifi_last_generation=UINT32_MAX;
        footholed_wifi_start();
        wifi_timer=lv_timer_create(wifi_refresh,500,nullptr);
        wifi_refresh(nullptr);
    } else if (!strcmp(section,"Analyze")) {
        label(content,"No real observations recorded yet.");
        for(const char* name : {"Observations","Sessions","Compare","Statistics","Saved logs"}) button(content,name,on_stub,(void*)name);
    } else if (!strcmp(section,"Expand")) {
        label(content,"Module registry: discovery not connected.");
        for(const char* name : {"Discover modules","Uninitialized","Installed modules","Manual setup","Diagnostics"}) button(content,name,on_stub,(void*)name);
    } else if (!strcmp(section,"Settings")) {
        label(content,"Settings (NVS-persisted)");
        label(content,"Privacy masking enabled");
        lv_obj_t* sw=lv_switch_create(content);
        lv_obj_set_style_bg_color(sw, lv_color_hex(PURPLE),
            static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
            static_cast<lv_style_selector_t>(LV_STATE_CHECKED));        
        lv_obj_set_style_bg_color(sw, lv_color_hex(BUTTON), LV_PART_MAIN);
        if(footholed_settings_get_privacy_on()) lv_obj_add_state(sw,LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw,on_privacy,LV_EVENT_VALUE_CHANGED,nullptr);
        label(content,"Auto-rotate display (IMU)");
        lv_obj_t* rot=lv_switch_create(content);
        lv_obj_set_style_bg_color(rot, lv_color_hex(PURPLE),
            static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
            static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
        lv_obj_set_style_bg_color(rot, lv_color_hex(BUTTON), LV_PART_MAIN);
        if(footholed_settings_get_auto_rotate()) lv_obj_add_state(rot,LV_STATE_CHECKED);
        lv_obj_add_event_cb(rot,on_auto_rotate,LV_EVENT_VALUE_CHANGED,nullptr);
        label(content,"Mask leading characters (1-15)");
        lv_obj_t* slider=lv_slider_create(content);
        lv_obj_set_style_bg_color(slider, lv_color_hex(PURPLE), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0xA78BFA), LV_PART_KNOB);
        lv_obj_set_style_bg_color(slider, lv_color_hex(BUTTON), LV_PART_MAIN);
        lv_slider_set_range(slider,1,15);
        lv_slider_set_value(slider,footholed_settings_get_mask_length(),LV_ANIM_OFF);
        lv_obj_t* preview=lv_label_create(content);
        lv_label_set_text(preview,"Preview: ****-demo-SSID");
        lv_obj_add_event_cb(slider,on_mask,LV_EVENT_VALUE_CHANGED,preview);
        for(const char* name : {"Display","Storage","Device","Developer","About"}) button(content,name,on_stub,(void*)name);
    }
}
}
void footholed_ui_init() {
    screen=lv_screen_active();
    setup_theme();
    lv_obj_set_style_bg_color(screen,lv_color_hex(BG),0);
    lv_obj_set_style_text_color(screen,lv_color_hex(TEXT),0);
    lv_obj_set_style_pad_all(screen,12,0);
    lv_obj_set_flex_flow(screen,LV_FLEX_FLOW_COLUMN);
    // Nonblocking splash; UI appears after 2 seconds.
    lv_obj_clean(screen);
    lv_obj_t* splash = lv_obj_create(screen);
    lv_obj_set_size(splash, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(splash, lv_color_hex(BG), 0);
    lv_obj_set_style_border_width(splash, 0, 0);
    lv_obj_set_style_pad_all(splash, 0, 0);
    lv_obj_set_scrollbar_mode(splash, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_t* logo = lv_image_create(splash);
    lv_image_set_src(logo, &footholed_splash_logo);
    lv_image_set_scale(logo, 256);  // LVGL 9: 256 = 100%, 384 ≈ 150%
    lv_obj_center(logo);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, 0);
    lv_timer_create(splash_done, 2000, nullptr);
}
