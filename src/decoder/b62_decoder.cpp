/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "aribcaption/b62_decoder.hpp"
#include "decoder/b62_decoder_impl.hpp"

namespace aribcaption {

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

}  // namespace aribcaption
