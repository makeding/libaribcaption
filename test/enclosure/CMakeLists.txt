#
# Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#

add_executable(test_enclosure_geometry
    EXCLUDE_FROM_ALL
        test.cpp
        ../../src/renderer/enclosure_geometry.cpp
)

target_compile_features(test_enclosure_geometry PRIVATE cxx_std_17)
target_include_directories(test_enclosure_geometry PRIVATE ../../include ../../src)

set_target_properties(test_enclosure_geometry
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
