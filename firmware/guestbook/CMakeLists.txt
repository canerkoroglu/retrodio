cmake_minimum_required(VERSION 3.22)
set(EXTRA_COMPONENT_DIRS ../components)
# Shared settings first; a build may append an overlay, e.g. the soak test:
#   idf.py -B build-soak -D SDKCONFIG=build-soak/sdkconfig \
#          -D SDKCONFIG_DEFAULTS="../sdkconfig.common;sdkconfig.soak" build
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.common")
endif()
set(PROJECT_VER "1.1.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rg_guestbook)
