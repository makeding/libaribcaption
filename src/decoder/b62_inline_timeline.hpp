/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_INLINE_TIMELINE_HPP
#define ARIBCAPTION_B62_INLINE_TIMELINE_HPP

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "base/tinyxml2.h"

namespace aribcaption::internal {

struct B62InlineInterval {
    int64_t begin = 0;
    std::optional<int64_t> end;
};

struct B62InlineScene {
    int64_t begin = 0;
    std::optional<int64_t> end;
};

// Resolves the ARIB operational timing subset: div, p and span use a fixed
// parallel time container, and begin/end are relative to the parent interval.
// Generic TTML seq, dur and set semantics intentionally do not live here.
class B62InlineTimeline {
public:
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] const B62InlineInterval& paragraph_interval() const noexcept {
        return paragraph_interval_;
    }
    [[nodiscard]] bool paragraph_indefinite() const noexcept {
        return paragraph_indefinite_;
    }
    [[nodiscard]] const std::vector<B62InlineScene>& scenes() const noexcept {
        return scenes_;
    }

    [[nodiscard]] bool IsElementActive(
        const tinyxml2::XMLElement* element, int64_t time) const;

private:
    friend B62InlineTimeline B62BuildInlineTimeline(
        const tinyxml2::XMLElement*, std::optional<int64_t>,
        std::optional<int64_t>, bool);

    bool valid_ = false;
    bool paragraph_indefinite_ = false;
    B62InlineInterval paragraph_interval_;
    std::vector<B62InlineScene> scenes_;
    std::unordered_map<const tinyxml2::XMLElement*, B62InlineInterval>
        span_intervals_;
};

B62InlineTimeline B62BuildInlineTimeline(
    const tinyxml2::XMLElement* paragraph,
    std::optional<int64_t> legacy_start,
    std::optional<int64_t> legacy_end,
    bool legacy_indefinite);

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_INLINE_TIMELINE_HPP
