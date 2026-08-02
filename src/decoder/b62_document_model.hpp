/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DOCUMENT_MODEL_HPP
#define ARIBCAPTION_B62_DOCUMENT_MODEL_HPP

#include <string>
#include <unordered_map>

namespace aribcaption::internal {

using B62Style = std::unordered_map<std::string, std::string>;

struct B62RegionDefinition {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    B62Style style;
};

struct B62InlineSpan {
    std::string text;
    B62Style style;
    std::string id;
    std::string ruby_target_id;
    std::string ruby_text;
    const B62RegionDefinition* region = nullptr;
    bool resets_position = false;
    bool is_ruby = false;
};

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_DOCUMENT_MODEL_HPP
