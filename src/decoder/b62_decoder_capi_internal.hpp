/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DECODER_CAPI_INTERNAL_HPP
#define ARIBCAPTION_B62_DECODER_CAPI_INTERNAL_HPP

#include "aribcaption/b62_decoder.hpp"

struct aribcc_b62_decoder_t {
    aribcc_b62_decoder_t(aribcaption::Context& context, aribcaption::CaptionType caption_type)
        : decoder(context, caption_type) {}

    aribcaption::B62Decoder decoder;
};

#endif  // ARIBCAPTION_B62_DECODER_CAPI_INTERNAL_HPP
