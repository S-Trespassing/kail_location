cmake_minimum_required(VERSION 3.18.1)

project("kail_native_hook")

# 使用 Prefab 寻找 shadowhook
find_package(shadowhook REQUIRED CONFIG)

add_library(kail_native_hook SHARED
        SensorHook.cpp
        elf_util.cpp
)

target_link_libraries(kail_native_hook
        log
        android
        shadowhook::shadowhook
)
