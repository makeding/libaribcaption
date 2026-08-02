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
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "aribcaption/b62_decoder.hpp"
#include "base/logger.hpp"
#include "decoder/b62_presentation_state.hpp"

namespace aribcaption::internal {

class B62DecoderImpl {
public:
    explicit B62DecoderImpl(Context& context);
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
    struct B62OwnedResource {
        std::vector<uint8_t> data;
        std::string mime_type;
    };
    using B62ResourceScope = std::unordered_map<uint32_t, B62OwnedResource>;

    bool StoreResourceContext(const B62ResourceContextView& resource_context);
    void EraseResourceContext(uint64_t scope_id);
    void EnforceResourceContextLimits(uint64_t protected_scope_id);
    void ClearResourceContexts();
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
    B62PresentationState legacy_presentation_state_;
    B62PresentationState document_presentation_state_;
    std::unordered_map<uint64_t, B62ResourceScope> resource_scopes_;
    std::deque<uint64_t> resource_scope_order_;
    B62ResourceScope transient_resource_scope_;
    uint64_t active_resource_scope_id_ = 0;
    bool has_active_resource_scope_ = false;
    size_t total_resource_bytes_ = 0;
};

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_DECODER_IMPL_HPP
