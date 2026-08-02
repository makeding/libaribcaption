/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * This file is part of libaribcaption.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DOCUMENT_HPP
#define ARIBCAPTION_B62_DOCUMENT_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aribcc_export.h"
#include "caption.hpp"

namespace aribcaption {

namespace internal {
class B62DecoderImpl;
}

enum class B62ElementType : uint8_t {
    kUnknown = 0,
    kDiv = 1,
    kParagraph = 2,
    kSpan = 3,
};

struct B62RubyAssociation {
    B62ElementType annotation_type = B62ElementType::kUnknown;
    B62ElementType target_type = B62ElementType::kUnknown;
    std::string annotation_id;
    std::string target_id;
    std::string annotation_text;
    std::string target_text;
};

enum class B62ResourceKind : uint8_t {
    kUnknown = 0,
    kSVGFont = 1,
    kWOFFFont = 2,
    kPNGImage = 3,
    kSVGImage = 4,
    kAudio = 5,
};

struct B62ResourceBlob {
    uint64_t scope_id = 0;
    uint32_t index = 0;
    B62ResourceKind kind = B62ResourceKind::kUnknown;
    std::string mime_type;
    std::shared_ptr<const std::vector<uint8_t>> bytes;
};

struct B62ResourceReference {
    std::string uri;
    std::shared_ptr<const B62ResourceBlob> resolved;
};

enum class B62FontFormat : uint8_t {
    kUnknown = 0,
    kSVG = 1,
    kWOFF = 2,
};

struct B62FontSource {
    B62FontFormat format = B62FontFormat::kUnknown;
    B62ResourceReference resource;
};

struct B62FontFace {
    std::string id;
    std::string family;
    std::string unicode_range;
    std::vector<B62FontSource> sources;
};

struct B62AudioCue {
    B62ElementType owner_type = B62ElementType::kUnknown;
    std::string owner_id;
    std::string id;
    B62ResourceReference source;
    int64_t begin_pts = PTS_NOPTS;
    std::optional<int64_t> end_pts;
    bool loop = false;
};

struct B62PlaneRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct B62BackgroundImage {
    B62ElementType owner_type = B62ElementType::kUnknown;
    std::string owner_id;
    B62PlaneRect layout_box;
    B62ResourceReference source;
    int64_t begin_pts = PTS_NOPTS;
    std::optional<int64_t> end_pts;
};

class B62DocumentSidecar {
public:
    ARIBCC_API ~B62DocumentSidecar();

    ARIBCC_API const std::vector<B62RubyAssociation>& ruby_associations() const noexcept;
    ARIBCC_API const std::vector<B62FontFace>& font_faces() const noexcept;
    ARIBCC_API const std::vector<B62AudioCue>& audio_cues() const noexcept;
    ARIBCC_API const std::vector<B62BackgroundImage>& background_images() const noexcept;

private:
    struct Impl;
    B62DocumentSidecar(std::vector<B62RubyAssociation> ruby_associations,
                       std::vector<B62FontFace> font_faces,
                       std::vector<B62AudioCue> audio_cues,
                       std::vector<B62BackgroundImage> background_images);

    std::shared_ptr<const Impl> pimpl_;
    friend class internal::B62DecoderImpl;
};

struct B62DocumentDecodeResult {
    std::vector<Caption> captions;
    std::shared_ptr<const B62DocumentSidecar> sidecar;
};

}  // namespace aribcaption

#endif  // ARIBCAPTION_B62_DOCUMENT_HPP
