cmake_minimum_required(VERSION 3.16)

# FootHoled application sources are in src/, not the
# standard ESP-IDF main/ component directory.
set(EXTRA_COMPONENT_DIRS "src")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(footholed_h1)