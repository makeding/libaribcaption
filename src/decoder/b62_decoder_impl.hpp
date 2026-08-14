/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DECODER_IMPL_HPP
#define ARIBCAPTION_B62_DECODER_IMPL_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include "aribcaption/b62_decoder.hpp"
#include "base/logger.hpp"
#include "decoder/b62_presentation_state.hpp"
#include "decoder/b62_resource_store.hpp"

namespace aribcaption::internal {

class B62DecoderImpl {
public:
    B62DecoderImpl(Context& context, CaptionType caption_type);
    ~B62DecoderImpl();

    void Reset();
    B62DecodeStatus Decode(const uint8_t* ttml_data, size_t length, int64_t base_pts, B62DecodeResult& out_result);
    B62DecodeStatus Decode(const uint8_t* ttml_data, size_t length,
                           const B62DecodeOptions& options, B62DecodeResult& out_result);
    B62DecodeStatus Decode(const uint8_t* ttml_data, size_t length,
                           const B62DecodeOptions& options,
                           const B62ResourceContextView& resource_context,
                           B62DecodeResult& out_result);
    B62DecodeStatus DecodeDocument(const uint8_t* ttml_data, size_t length,
                                   const B62DecodeOptions& options,
                                   const B62ResourceContextView& resource_context,
                                   B62DocumentDecodeResult& out_result);

private:
    B62DecodeStatus DecodeInternal(const uint8_t* ttml_data, size_t length,
                                   const B62DecodeOptions& options,
                                   bool preserve_document_layout,
                                   B62PresentationMetadata* document_metadata,
                                   B62DecodeResult& out_result);
    B62DecodeStatus DecodeWithResourceContext(
        const uint8_t* ttml_data, size_t length,
        const B62DecodeOptions& options,
        const B62ResourceContextView& resource_context,
        bool preserve_document_layout,
        B62PresentationMetadata* document_metadata,
        B62DecodeResult& out_result);

private:
    std::shared_ptr<Logger> log_;
    CaptionType caption_type_;
    B62PresentationState legacy_presentation_state_;
    B62PresentationState document_presentation_state_;
    B62ResourceStore resource_store_;
};

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_DECODER_IMPL_HPP
