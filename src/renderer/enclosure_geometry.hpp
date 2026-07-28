/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_ENCLOSURE_GEOMETRY_HPP
#define ARIBCAPTION_ENCLOSURE_GEOMETRY_HPP

#include <vector>

#include "aribcaption/color.hpp"
#include "renderer/rect.hpp"

namespace aribcaption {

struct ColoredEnclosureRect {
    Rect rect;
    ColorRGBA color;
};

struct ColoredEnclosureSegment {
    Rect rect;
    ColorRGBA color;
};

// Builds the visible edge segments of the union of equally-colored rectangles.
// Adjacent character cells consequently form one continuous, stepped enclosure.
std::vector<ColoredEnclosureSegment> BuildMergedEnclosureSegments(
    const std::vector<ColoredEnclosureRect>& rectangles,
    int horizontal_thickness,
    int vertical_thickness);

}  // namespace aribcaption

#endif  // ARIBCAPTION_ENCLOSURE_GEOMETRY_HPP
