/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <cassert>
#include <vector>

#include "renderer/enclosure_geometry.hpp"

namespace {

bool HasSegment(const std::vector<aribcaption::ColoredEnclosureSegment>& segments,
                const aribcaption::Rect& rect,
                aribcaption::ColorRGBA color) {
    for (const auto& segment : segments) {
        if (segment.rect == rect && segment.color.u32 == color.u32) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    using namespace aribcaption;
    const ColorRGBA black(0, 0, 0);
    const std::vector<ColoredEnclosureRect> cells = {
        {{80, 16, 160, 32}, black},
        {{160, 16, 310, 32}, black},
        {{80, 32, 200, 48}, black},
        {{200, 32, 350, 48}, black},
    };
    const auto segments = BuildMergedEnclosureSegments(cells, 1, 1);

    // Internal character and line edges disappear.
    assert(!HasSegment(segments, Rect(159, 16, 160, 32), black));
    assert(!HasSegment(segments, Rect(80, 31, 160, 32), black));
    assert(!HasSegment(segments, Rect(160, 31, 310, 32), black));

    // The two lines form one stepped outer contour.
    assert(HasSegment(segments, Rect(80, 16, 160, 17), black));
    assert(HasSegment(segments, Rect(160, 16, 310, 17), black));
    assert(HasSegment(segments, Rect(310, 32, 350, 33), black));
    assert(HasSegment(segments, Rect(349, 32, 350, 48), black));
    assert(HasSegment(segments, Rect(200, 47, 350, 48), black));
    assert(HasSegment(segments, Rect(80, 47, 200, 48), black));

    // A touching rectangle of another color remains a separate enclosure.
    const ColorRGBA red(255, 0, 0);
    const auto different_colors = BuildMergedEnclosureSegments({
        {{0, 0, 10, 10}, black},
        {{10, 0, 20, 10}, red},
    }, 1, 1);
    assert(HasSegment(different_colors, Rect(9, 0, 10, 10), black));
    assert(HasSegment(different_colors, Rect(10, 0, 11, 10), red));

    const auto thick = BuildMergedEnclosureSegments({
        {{0, 0, 10, 10}, black},
        {{10, 0, 20, 10}, black},
    }, 3, 2);
    assert(HasSegment(thick, Rect(0, 0, 10, 2), black));
    assert(HasSegment(thick, Rect(0, 0, 3, 10), black));
    assert(!HasSegment(thick, Rect(7, 0, 10, 10), black));
    return 0;
}
