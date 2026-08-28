/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DOCUMENT_CAPI_INTERNAL_HPP
#define ARIBCAPTION_B62_DOCUMENT_CAPI_INTERNAL_HPP

#include <memory>

#include "aribcaption/b62_document.h"
#include "aribcaption/b62_document.hpp"

namespace aribcaption::internal {

std::shared_ptr<const B62DocumentSidecar> GetB62DocumentSidecarFromCAPI(
    const aribcc_b62_document_sidecar_t* sidecar) noexcept;

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_DOCUMENT_CAPI_INTERNAL_HPP
