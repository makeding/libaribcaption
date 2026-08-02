/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_time.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "decoder/b62_text_util.hpp"

namespace aribcaption::internal {

std::optional<int64_t> B62ParseTime(const char* value, bool* indefinite) {
    if (indefinite) {
        *indefinite = false;
    }
    if (!value) {
        return std::nullopt;
    }
    std::string text = B62TrimASCII(value);
    if (text == "indefinite") {
        if (indefinite) {
            *indefinite = true;
        }
        return std::nullopt;
    }
    if (text.size() > 2 && text.compare(text.size() - 2, 2, "ms") == 0) {
        char* end = nullptr;
        double millis = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || std::string_view(end) != "ms" || !std::isfinite(millis)) {
            return std::nullopt;
        }
        return static_cast<int64_t>(std::llround(millis));
    }
    if (!text.empty() && text.back() == 's') {
        char* end = nullptr;
        double seconds = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || std::string_view(end) != "s" || !std::isfinite(seconds)) {
            return std::nullopt;
        }
        return static_cast<int64_t>(std::llround(seconds * 1000.0));
    }

    int hours = 0;
    int minutes = 0;
    double seconds = 0.0;
    char trailing = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%lf%c", &hours, &minutes, &seconds,
                    &trailing) != 3 ||
        hours < 0 || minutes < 0 || minutes > 59 || seconds < 0 || seconds >= 60) {
        return std::nullopt;
    }
    return static_cast<int64_t>(
        std::llround((hours * 3600.0 + minutes * 60.0 + seconds) * 1000.0));
}

}  // namespace aribcaption::internal
