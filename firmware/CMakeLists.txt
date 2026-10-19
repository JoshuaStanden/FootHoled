
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "src")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Make LVGL use the project's own src/lv_conf.h on the ESP-IDF build path.
# PlatformIO passes -D LV_CONF_INCLUDE_SIMPLE via platformio.ini, but idf.py
# does not, so without this LVGL silently falls back to the component default
# config (only Montserrat 14, 32-bit colour). This pins the custom config:
# Montserrat 28 for the splash wordmark AND the critical LV_COLOR_DEPTH 16
# that must match the ST7796 RGB565 panel in board_adapter.cpp.
idf_build_set_property(COMPILE_OPTIONS "-DLV_CONF_INCLUDE_SIMPLE" APPEND)

project(FootHoled)

