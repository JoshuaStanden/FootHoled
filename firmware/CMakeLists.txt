
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "src")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# LVGL configuration on the ESP-IDF / idf.py build path is driven by Kconfig
# (sdkconfig.defaults), NOT by a custom src/lv_conf.h.
#
# Under idf.py the LVGL managed component compiles itself into its own static
# lib and does NOT see a -DLV_CONF_INCLUDE_SIMPLE added only to the app's "src"
# component, nor an lv_conf.h on the app's include path. That mismatch left the
# LVGL lib built with its defaults (Montserrat 28 off, 32-bit colour) while the
# app expected the custom config, producing the "lv_font_montserrat_28 was not
# declared" error. The fix is CONFIG_LV_CONF_SKIP=y plus the LV_* symbols in
# sdkconfig.defaults, so the app and the LVGL lib compile against one config.
#
# PlatformIO here also uses framework=espidf, so it builds LVGL the same way and
# reads the same Kconfig; src/lv_conf.h has been removed as redundant.

project(FootHoled)

