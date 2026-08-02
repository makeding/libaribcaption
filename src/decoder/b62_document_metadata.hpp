/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DOCUMENT_METADATA_HPP
#define ARIBCAPTION_B62_DOCUMENT_METADATA_HPP

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "base/tinyxml2.h"
#include "decoder/b62_document_model.hpp"
#include "decoder/b62_document_parser.hpp"

namespace aribcaption::internal {

class B62ResourceResolver;
struct B62PresentationMetadata;

void B62CollectRubyAssociations(const tinyxml2::XMLElement* document_root,
                                B62PresentationMetadata& out_metadata);

void B62CollectFontFaces(const tinyxml2::XMLElement* document_root,
                         B62ResourceResolver& resource_resolver,
                         B62PresentationMetadata& out_metadata);

void B62MaterializeTimedMetadata(
    const B62TimedContent& timed_content,
    const std::array<int, 2>& plane,
    const std::unordered_map<std::string, B62RegionDefinition>& regions,
    B62ResourceResolver& resource_resolver,
    int64_t timeline_offset,
    int64_t document_pts,
    B62PresentationMetadata& out_metadata);

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_DOCUMENT_METADATA_HPP
