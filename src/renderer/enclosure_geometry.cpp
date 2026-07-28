/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "renderer/enclosure_geometry.hpp"

#include <algorithm>
#include <utility>

namespace aribcaption {
namespace {

using Interval = std::pair<int, int>;

template <typename MakeRect>
void AppendUncoveredSegments(int begin,
                             int end,
                             std::vector<Interval> covered,
                             ColorRGBA color,
                             MakeRect make_rect,
                             std::vector<ColoredEnclosureSegment>& out) {
    std::sort(covered.begin(), covered.end());
    int cursor = begin;
    for (const auto& interval : covered) {
        if (interval.second <= cursor || interval.first >= end) {
            continue;
        }
        if (interval.first > cursor) {
            out.push_back({make_rect(cursor, std::min(interval.first, end)), color});
        }
        cursor = std::max(cursor, interval.second);
        if (cursor >= end) {
            return;
        }
    }
    if (cursor < end) {
        out.push_back({make_rect(cursor, end), color});
    }
}

bool SameColor(ColorRGBA lhs, ColorRGBA rhs) {
    return lhs.u32 == rhs.u32;
}

}  // namespace

std::vector<ColoredEnclosureSegment> BuildMergedEnclosureSegments(
    const std::vector<ColoredEnclosureRect>& rectangles,
    int horizontal_thickness,
    int vertical_thickness) {
    std::vector<ColoredEnclosureSegment> result;
    horizontal_thickness = std::max(horizontal_thickness, 1);
    vertical_thickness = std::max(vertical_thickness, 1);

    for (size_t index = 0; index < rectangles.size(); index++) {
        const ColoredEnclosureRect& current = rectangles[index];
        if (current.rect.width() <= 0 || current.rect.height() <= 0) {
            continue;
        }

        std::vector<Interval> top_covered;
        std::vector<Interval> bottom_covered;
        std::vector<Interval> left_covered;
        std::vector<Interval> right_covered;
        for (size_t other_index = 0; other_index < rectangles.size(); other_index++) {
            if (other_index == index) {
                continue;
            }
            const ColoredEnclosureRect& other = rectangles[other_index];
            if (!SameColor(current.color, other.color)) {
                continue;
            }

            int horizontal_begin = std::max(current.rect.left, other.rect.left);
            int horizontal_end = std::min(current.rect.right, other.rect.right);
            if (horizontal_begin < horizontal_end) {
                if (other.rect.top < current.rect.top && other.rect.bottom >= current.rect.top) {
                    top_covered.emplace_back(horizontal_begin, horizontal_end);
                }
                if (other.rect.top <= current.rect.bottom && other.rect.bottom > current.rect.bottom) {
                    bottom_covered.emplace_back(horizontal_begin, horizontal_end);
                }
            }

            int vertical_begin = std::max(current.rect.top, other.rect.top);
            int vertical_end = std::min(current.rect.bottom, other.rect.bottom);
            if (vertical_begin < vertical_end) {
                if (other.rect.left < current.rect.left && other.rect.right >= current.rect.left) {
                    left_covered.emplace_back(vertical_begin, vertical_end);
                }
                if (other.rect.left <= current.rect.right && other.rect.right > current.rect.right) {
                    right_covered.emplace_back(vertical_begin, vertical_end);
                }
            }
        }

        AppendUncoveredSegments(
            current.rect.left, current.rect.right, std::move(top_covered), current.color,
            [&](int left, int right) {
                return Rect(left, current.rect.top, right,
                            std::min(current.rect.top + vertical_thickness, current.rect.bottom));
            }, result);
        AppendUncoveredSegments(
            current.rect.left, current.rect.right, std::move(bottom_covered), current.color,
            [&](int left, int right) {
                return Rect(left, std::max(current.rect.bottom - vertical_thickness, current.rect.top),
                            right, current.rect.bottom);
            }, result);
        AppendUncoveredSegments(
            current.rect.top, current.rect.bottom, std::move(left_covered), current.color,
            [&](int top, int bottom) {
                return Rect(current.rect.left, top,
                            std::min(current.rect.left + horizontal_thickness, current.rect.right), bottom);
            }, result);
        AppendUncoveredSegments(
            current.rect.top, current.rect.bottom, std::move(right_covered), current.color,
            [&](int top, int bottom) {
                return Rect(std::max(current.rect.right - horizontal_thickness, current.rect.left), top,
                            current.rect.right, bottom);
            }, result);
    }
    return result;
}

}  // namespace aribcaption
