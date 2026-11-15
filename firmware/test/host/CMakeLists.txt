# Host unit tests for the hardware-independent parts of components/rg.
#   cmake -S firmware/test/host -B build/host && cmake --build build/host && ctest --test-dir build/host
cmake_minimum_required(VERSION 3.16)
project(rg_host_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

set(RG ${CMAKE_CURRENT_SOURCE_DIR}/../../components/rg)
add_compile_options(-Wall -Wextra -Werror -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE)
include_directories(stub ${RG}/include)

add_library(rg_pure STATIC
    ${RG}/rg_catalog_scan.c
    ${RG}/rg_crc32.c
    ${RG}/rg_mic_conv.c
    ${RG}/rg_settings.c
    ${RG}/rg_wav.c
)
target_link_libraries(rg_pure PUBLIC m)

foreach(t catalog crc32 mic_conv settings wav)
    add_executable(test_${t} test_${t}.c)
    target_link_libraries(test_${t} rg_pure)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()

# The shipped microSD template must parse without warnings.
target_compile_definitions(test_settings PRIVATE
    RG_TEMPLATE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../../../sdcard/audio/config.txt")
