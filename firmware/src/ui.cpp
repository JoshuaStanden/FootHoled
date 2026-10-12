/**
 * @file ui.cpp
 * @brief Experimental LVGL 9 menu navigation for FootHoled H1.
 *
 * This screen tree is a navigable scaffold, not a hardware integration.
 * Recon/Analyze/Expand actions deliberately route to an unavailable-service
 * screen. Settings are RAM-only until a persistent settings service exists.
 *
 * Maintenance: keep hardware access outside this file; introduce service
 * interfaces instead of calling peripheral drivers from LVGL callbacks.
 */
#include "ui.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

namespace {
lv_obj_t* screen = nullptr;
lv_obj_t* content = nullptr;
char current_section[32] = "Home";
int mask_length = 4;
bool privacy_on = true;

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
    lv_obj_t* b = lv_button_create(content);
    lv_obj_t* t = lv_label_create(b); lv_label_set_text(t, "Back");
    lv_obj_add_event_cb(b, on_back, LV_EVENT_CLICKED, nullptr);
}
void button(lv_obj_t* parent, const char* text, lv_event_cb_t callback, void* data) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_add_event_cb(b, callback, LV_EVENT_CLICKED, data);
    lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, text); lv_obj_center(l);
}
// Preview uses synthetic identifiers; this is not yet the shared masking API.
void on_mask(lv_event_t* e) {
    mask_length = (int)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    lv_obj_t* preview = (lv_obj_t*)lv_event_get_user_data(e);
    char buf[80]; snprintf(buf,sizeof(buf),"Preview: %.*s%s",mask_length,"***************","-demo-SSID");
    lv_label_set_text(preview,buf);
}
void on_privacy(lv_event_t* e) { privacy_on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED); }
// Rebuild the simple alpha screen tree when navigating. A later iteration can
// introduce reusable views and a back-stack without coupling to drivers.
void navigate(const char* section) {
    strncpy(current_section,section,sizeof(current_section)-1);
    current_section[sizeof(current_section)-1] = 0;
    lv_obj_clean(screen);
    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text_fmt(title,"FootHoled H1 / %s",current_section);
    lv_obj_set_width(title,lv_pct(100));
    lv_obj_t* top = lv_obj_create(screen);
    lv_obj_set_width(top,lv_pct(100)); lv_obj_set_height(top,54);
    lv_obj_set_flex_flow(top,LV_FLEX_FLOW_ROW);
    lv_obj_t* h=lv_button_create(top); lv_obj_add_event_cb(h,on_home,LV_EVENT_CLICKED,nullptr);
    lv_obj_t* hl=lv_label_create(h);lv_label_set_text(hl,"Home");
    lv_obj_t* s=lv_button_create(top); lv_obj_add_event_cb(s,on_settings,LV_EVENT_CLICKED,nullptr);
    lv_obj_t* sl=lv_label_create(s);lv_label_set_text(sl,"Settings");
    content=lv_obj_create(screen);
    lv_obj_set_width(content,lv_pct(100));
    lv_obj_set_flex_grow(content,1);
    lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
    if (!strcmp(section,"Home")) {
        label(content,"Recon. Analyze. Expand.");
        button(content,"RECON",on_nav,(void*)"Recon");
        button(content,"ANALYZE",on_nav,(void*)"Analyze");
        button(content,"EXPAND",on_nav,(void*)"Expand");
    } else if (!strcmp(section,"Recon")) {
        label(content,"Discover and collect / drivers not initialized");
        for(const char* name : {"Wi-Fi","Bluetooth / BLE","GNSS","NFC / RFID","Infrared","Radio tools"}) button(content,name,on_stub,(void*)name);
    } else if (!strcmp(section,"Analyze")) {
        label(content,"No real observations recorded yet.");
        for(const char* name : {"Observations","Sessions","Compare","Statistics","Saved logs"}) button(content,name,on_stub,(void*)name);
    } else if (!strcmp(section,"Expand")) {
        label(content,"Module registry: discovery not connected.");
        for(const char* name : {"Discover modules","Uninitialized","Installed modules","Manual setup","Diagnostics"}) button(content,name,on_stub,(void*)name);
    } else if (!strcmp(section,"Settings")) {
        label(content,"Settings (RAM only; NVS pending)");
        label(content,"Privacy masking enabled");
        lv_obj_t* sw=lv_switch_create(content); if(privacy_on) lv_obj_add_state(sw,LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw,on_privacy,LV_EVENT_VALUE_CHANGED,nullptr);
        label(content,"Mask leading characters (1-15)");
        lv_obj_t* slider=lv_slider_create(content);
        lv_slider_set_range(slider,1,15);lv_slider_set_value(slider,mask_length,LV_ANIM_OFF);
        lv_obj_t* preview=lv_label_create(content);
        lv_label_set_text(preview,"Preview: ****-demo-SSID");
        lv_obj_add_event_cb(slider,on_mask,LV_EVENT_VALUE_CHANGED,preview);
        for(const char* name : {"Display","Storage","Device","Developer","About"}) button(content,name,on_stub,(void*)name);
    }
}
}
void footholed_ui_init() {
    screen=lv_screen_active();
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x0D1720),0);
    lv_obj_set_style_text_color(screen,lv_color_hex(0xEAF6F9),0);
    lv_obj_set_style_pad_all(screen,12,0);
    lv_obj_set_flex_flow(screen,LV_FLEX_FLOW_COLUMN);
    navigate("Home");
}
