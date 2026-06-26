# Top-level ESP-IDF project file.
#
# This drives the FIRMWARE build only (run via `idf.py build`). The native
# host/unit-test build is a separate CMake project rooted at tests/ — see
# tests/CMakeLists.txt and CMakePresets.json. Keeping them separate lets the
# host build run with a plain compiler and no ESP-IDF installed.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp_resso C CXX)
