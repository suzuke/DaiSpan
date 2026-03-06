# DaiSpan v2 - ESP-IDF top-level CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# esp-homekit-sdk components
set(EXTRA_COMPONENT_DIRS
    $ENV{HOME}/esp/esp-homekit-sdk/components
    $ENV{HOME}/esp/esp-homekit-sdk/components/homekit
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(daispan)
