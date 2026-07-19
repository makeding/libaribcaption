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
#include <optional>
#include <string>
#include <vector>
#include "aribcaption/b62_decoder.hpp"
#include "base/logger.hpp"

namespace aribcaption::internal {

class B62DecoderImpl {
public:
    explicit B62DecoderImpl(Context& context);
    ~B62DecoderImpl();

    void SetFontScale(float scale);
    void Reset();
    B62DecodeStatus Decode(const uint8_t* ttml_data, size_t length, int64_t base_pts, B62DecodeResult& out_result);
    B62DecodeStatus Decode(const uint8_t* ttml_data, size_t length,
                           const B62DecodeOptions& options, B62DecodeResult& out_result);

private:
    struct PresentationNode {
        std::string id;
        int64_t start = PTS_NOPTS;
        std::optional<int64_t> end;
        bool indefinite = false;
        Caption caption;
    };

    std::shared_ptr<Logger> log_;
    float font_scale_ = 1.0f;
    std::vector<PresentationNode> live_nodes_;
};

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_DECODER_IMPL_HPP
