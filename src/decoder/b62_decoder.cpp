/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "aribcaption/b62_decoder.hpp"
#include "decoder/b62_decoder_impl.hpp"

#include <utility>

namespace aribcaption {

struct B62DocumentSidecar::Impl {
    Impl(std::vector<B62RubyAssociation> associations,
         std::vector<B62FontFace> faces,
         std::vector<B62AudioCue> audios,
         std::vector<B62BackgroundImage> images)
        : ruby_associations(std::move(associations)),
          font_faces(std::move(faces)),
          audio_cues(std::move(audios)),
          background_images(std::move(images)) {}

    std::vector<B62RubyAssociation> ruby_associations;
    std::vector<B62FontFace> font_faces;
    std::vector<B62AudioCue> audio_cues;
    std::vector<B62BackgroundImage> background_images;
};

B62DocumentSidecar::B62DocumentSidecar(
    std::vector<B62RubyAssociation> ruby_associations,
    std::vector<B62FontFace> font_faces,
    std::vector<B62AudioCue> audio_cues,
    std::vector<B62BackgroundImage> background_images)
    : pimpl_(std::make_shared<Impl>(std::move(ruby_associations),
                                    std::move(font_faces),
                                    std::move(audio_cues),
                                    std::move(background_images))) {}

B62DocumentSidecar::~B62DocumentSidecar() = default;

const std::vector<B62RubyAssociation>&
B62DocumentSidecar::ruby_associations() const noexcept {
    return pimpl_->ruby_associations;
}

const std::vector<B62FontFace>& B62DocumentSidecar::font_faces() const noexcept {
    return pimpl_->font_faces;
}

const std::vector<B62AudioCue>& B62DocumentSidecar::audio_cues() const noexcept {
    return pimpl_->audio_cues;
}

const std::vector<B62BackgroundImage>&
B62DocumentSidecar::background_images() const noexcept {
    return pimpl_->background_images;
}

B62Decoder::B62Decoder(Context& context) : pimpl_(std::make_unique<internal::B62DecoderImpl>(context)) {}

B62Decoder::~B62Decoder() = default;

B62Decoder::B62Decoder(B62Decoder&&) noexcept = default;

B62Decoder& B62Decoder::operator=(B62Decoder&&) noexcept = default;

void B62Decoder::Reset() {
    pimpl_->Reset();
}

B62DecodeStatus B62Decoder::Decode(const uint8_t* ttml_data,
                                   size_t length,
                                   int64_t base_pts,
                                   B62DecodeResult& out_result) {
    return pimpl_->Decode(ttml_data, length, base_pts, out_result);
}

B62DecodeStatus B62Decoder::Decode(const uint8_t* ttml_data,
                                   size_t length,
                                   const B62DecodeOptions& options,
                                   B62DecodeResult& out_result) {
    return pimpl_->Decode(ttml_data, length, options, out_result);
}

B62DecodeStatus B62Decoder::Decode(const uint8_t* ttml_data,
                                   size_t length,
                                   const B62DecodeOptions& options,
                                   const B62ResourceContextView& resource_context,
                                   B62DecodeResult& out_result) {
    return pimpl_->Decode(ttml_data, length, options, resource_context, out_result);
}

B62DecodeStatus B62Decoder::DecodeDocument(
    const uint8_t* ttml_data,
    size_t length,
    const B62DecodeOptions& options,
    const B62ResourceContextView& resource_context,
    B62DocumentDecodeResult& out_result) {
    return pimpl_->DecodeDocument(ttml_data, length, options, resource_context, out_result);
}

}  // namespace aribcaption
