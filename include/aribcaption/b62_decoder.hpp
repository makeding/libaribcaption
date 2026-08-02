/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * This file is part of libaribcaption.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DECODER_HPP
#define ARIBCAPTION_B62_DECODER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "aribcc_export.h"
#include "caption.hpp"
#include "context.hpp"

namespace aribcaption {

namespace internal {
class B62DecoderImpl;
}

enum class B62DecodeStatus {
    kError = 0,
    kNoCaption = 1,
    kGotCaption = 2,
};

enum class B62OperationMode {
    kLive = 0,
    kSegment = 1,
    kProgram = 2,
};

struct B62DecodeOptions {
    // Presentation time of the TTML document's MPU on the media timeline.
    int64_t document_pts = PTS_NOPTS;
    // Media-timeline position corresponding to TTML time zero.
    int64_t time_base_pts = PTS_NOPTS;
    B62OperationMode operation_mode = B62OperationMode::kSegment;
    bool align_earliest_to_document_pts = false;
    bool ignore_document_timing = false;
    bool discontinuity = false;
};

/**
 * Non-owning view of one ARIB-TTML same-MPU resource.
 *
 * The decoder copies the bytes and MIME type before Decode() returns. The
 * resource index is the subsample number referenced by subt://<index>.
 */
struct B62ResourceView {
    uint32_t index = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;
    const char* mime_type = nullptr;
};

/**
 * Non-owning view of the resources available to one ARIB-TTML document.
 *
 * scope_id identifies one closed-caption data transmission unit (one MPU for
 * MMT). A subt:// reference is resolved only within that scope. Presentation
 * continuations may keep an older scope alive internally, but callers must
 * not reuse its identifier for a different MPU. Reset() and a discontinuity
 * discard all retained scopes. A zero scope_id is transient and makes the
 * supplied resources available only during this Decode() call.
 */
struct B62ResourceContextView {
    uint64_t scope_id = 0;
    const B62ResourceView* resources = nullptr;
    size_t resource_count = 0;
};

enum class B62ElementType : uint8_t {
    kUnknown = 0,
    kDiv = 1,
    kParagraph = 2,
    kSpan = 3,
};

/**
 * Document-level arib-tt:ruby association metadata.
 *
 * TR-B39 defines this association as metadata only; it must not change the
 * drawing size or position of either element.
 */
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

struct B62DecodeResult {
    std::vector<Caption> captions;
};

/**
 * Immutable B62-only metadata accompanying DecodeDocument() captions.
 *
 * The opaque implementation lets later B62 features add accessors without
 * extending Caption, CaptionChar, or the result structure.
 */
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

/**
 * Result of DecodeDocument(). Kept separate from B62DecodeResult so the
 * historical C++ ABI and legacy Caption snapshot contract remain unchanged.
 */
struct B62DocumentDecodeResult {
    std::vector<Caption> captions;
    std::shared_ptr<const B62DocumentSidecar> sidecar;
};

/**
 * ARIB STD-B62 / ARIB-TTML decoder.
 *
 * The decoder accepts one UTF-8 TTML document and maps its timed paragraphs
 * into the same Caption structures used by the ARIB STD-B24 decoder. A single
 * document may produce more than one Caption.
 */
class B62Decoder {
public:
    ARIBCC_API explicit B62Decoder(Context& context);
    ARIBCC_API ~B62Decoder();
    ARIBCC_API B62Decoder(B62Decoder&&) noexcept;
    ARIBCC_API B62Decoder& operator=(B62Decoder&&) noexcept;

public:
    ARIBCC_API void Reset();

public:
    /**
     * Decode a UTF-8 ARIB-TTML document.
     *
     * If base_pts is specified, the earliest explicitly timed paragraph is
     * aligned to it while relative TTML timing is preserved.
     */
    ARIBCC_API B62DecodeStatus Decode(const uint8_t* ttml_data,
                                      size_t length,
                                      int64_t base_pts,
                                      B62DecodeResult& out_result);

    ARIBCC_API B62DecodeStatus Decode(const uint8_t* ttml_data,
                                      size_t length,
                                      const B62DecodeOptions& options,
                                      B62DecodeResult& out_result);

    /**
     * Decode a UTF-8 ARIB-TTML document with its same-MPU resources.
     *
     * This overload is additive: the existing Decode() overloads retain
     * their historical behaviour and act as if no resource context exists.
     */
    ARIBCC_API B62DecodeStatus Decode(const uint8_t* ttml_data,
                                      size_t length,
                                      const B62DecodeOptions& options,
                                      const B62ResourceContextView& resource_context,
                                      B62DecodeResult& out_result);

    /**
     * Decode through the document-preserving B62 path.
     *
     * Unlike the historical Decode() overloads, this path preserves
     * broadcaster geometry and exposes B62-only semantic sidecars.
     */
    ARIBCC_API B62DecodeStatus DecodeDocument(
        const uint8_t* ttml_data,
        size_t length,
        const B62DecodeOptions& options,
        const B62ResourceContextView& resource_context,
        B62DocumentDecodeResult& out_result);

public:
    B62Decoder(const B62Decoder&) = delete;
    B62Decoder& operator=(const B62Decoder&) = delete;

private:
    std::unique_ptr<internal::B62DecoderImpl> pimpl_;
};

}  // namespace aribcaption

#endif  // ARIBCAPTION_B62_DECODER_HPP
