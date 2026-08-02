/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DOCUMENT_PARSER_HPP
#define ARIBCAPTION_B62_DOCUMENT_PARSER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "aribcaption/b62_document.hpp"
#include "base/tinyxml2.h"

namespace aribcaption::internal {

enum class B62ParseDisposition {
    kInvalid,
    kEmpty,
    kNoBody,
    kReady,
};

// Owns the XML storage for every borrowed element pointer produced while
// decoding one TTML document.
class B62ParsedDocument {
public:
    B62ParseDisposition Parse(const uint8_t* data, size_t length);

    [[nodiscard]] const tinyxml2::XMLElement* root() const noexcept {
        return root_;
    }
    [[nodiscard]] const tinyxml2::XMLElement* body() const noexcept {
        return body_;
    }
    [[nodiscard]] const char* error() const noexcept {
        return document_.ErrorStr();
    }

private:
    tinyxml2::XMLDocument document_;
    const tinyxml2::XMLElement* root_ = nullptr;
    const tinyxml2::XMLElement* body_ = nullptr;
};

struct B62RawCue {
    // Borrowed from B62ParsedDocument and valid only while it remains alive.
    const tinyxml2::XMLElement* paragraph = nullptr;
    std::string id;
    std::optional<int64_t> start;
    std::optional<int64_t> end;
    bool indefinite_start = false;
    bool indefinite = false;
};

struct B62RawAudioCue {
    B62ElementType owner_type = B62ElementType::kUnknown;
    std::string owner_id;
    std::string id;
    std::string source;
    std::optional<int64_t> start;
    std::optional<int64_t> end;
    bool loop = false;
    bool indefinite = false;
};

struct B62RawBackgroundImage {
    B62ElementType owner_type = B62ElementType::kUnknown;
    std::string owner_id;
    std::string source;
    std::string region_id;
    std::optional<int64_t> start;
    std::optional<int64_t> end;
    bool indefinite = false;
};

struct B62TimedContent {
    std::vector<B62RawCue> cues;
    std::vector<B62RawAudioCue> audio_cues;
    std::vector<B62RawBackgroundImage> background_images;
    std::optional<int64_t> minimum_start;

    [[nodiscard]] bool empty() const noexcept {
        return cues.empty() && audio_cues.empty() && background_images.empty();
    }
};

B62TimedContent B62CollectTimedContent(const tinyxml2::XMLElement* body);

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_DOCUMENT_PARSER_HPP
