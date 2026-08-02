/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_CAPTION_CAPI_CONVERSION_HPP
#define ARIBCAPTION_B62_CAPTION_CAPI_CONVERSION_HPP

#include "aribcaption/b62_decoder.h"
#include "aribcaption/b62_decoder.hpp"

namespace aribcaption::internal {

aribcc_b62_decode_status_t ConvertB62DecodeResultToCAPI(
    B62DecodeResult&& result,
    B62DecodeStatus status,
    aribcc_b62_decode_result_t* out_result);

void CleanupB62DecodeResultCAPI(aribcc_b62_decode_result_t* result) noexcept;

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_CAPTION_CAPI_CONVERSION_HPP
