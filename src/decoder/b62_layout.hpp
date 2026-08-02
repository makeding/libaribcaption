/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_LAYOUT_HPP
#define ARIBCAPTION_B62_LAYOUT_HPP

#include <array>
#include <optional>
#include <vector>

#include "aribcaption/caption.hpp"
#include "decoder/b62_document_model.hpp"

namespace aribcaption::internal {

std::optional<std::array<int, 2>> B62ParseLengthPair(
    const char* value,
    const std::array<int, 2>& plane);

bool B62IsVerticalWritingMode(const B62Style& style);

B62RegionDefinition B62MakeFormattingDefinition(
    const B62RegionDefinition& region,
    const B62Style& effective_style);

void B62LayoutHorizontal(const std::vector<B62InlineSpan>& spans,
                         const B62RegionDefinition& definition,
                         const B62RegionDefinition& clip_definition,
                         const std::array<int, 2>& plane,
                         Caption& caption,
                         bool preserve_region_bounds);

void B62LayoutVertical(const std::vector<B62InlineSpan>& spans,
                       const B62RegionDefinition& definition,
                       const B62RegionDefinition& clip_definition,
                       const std::array<int, 2>& plane,
                       Caption& caption,
                       bool preserve_region_bounds);

void B62MergeCaptionRegionsWithSameClip(Caption& caption);

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_LAYOUT_HPP
