#
# Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#

cmake_minimum_required(VERSION 3.1)

add_executable(test_b62
    EXCLUDE_FROM_ALL
        test.cpp
)

add_executable(test_b62_capi
    EXCLUDE_FROM_ALL
        test_capi.c
)

target_compile_features(test_b62
    PRIVATE
        cxx_std_17
)

target_include_directories(test_b62
    PRIVATE
        ../../include
)

target_link_libraries(test_b62
    PRIVATE
        aribcaption
)

target_include_directories(test_b62_capi
    PRIVATE
        ../../include
)

target_link_libraries(test_b62_capi
    PRIVATE
        aribcaption
)

set_target_properties(test_b62
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

set_target_properties(test_b62_capi
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
