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

namespace aribcaption::internal {

class B62DecoderImpl {
public:
    explicit B62DecoderImpl(Context& context);
    ~B62DecoderImpl();

    void SetFontScale(float scale);
    B62DecodeStatus Decode(const uint8_t* ttml_data, size_t length, int64_t base_pts, B62DecodeResult& out_result);

private:
    std::shared_ptr<Logger> log_;
    float font_scale_ = 1.0f;
};

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_DECODER_IMPL_HPP
