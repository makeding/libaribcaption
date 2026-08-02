/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <array>
#include <cstring>
#include <new>
#include <utility>

#include "aribcaption/b62_decoder.h"
#include "aribcaption/b62_decoder.hpp"
#include "decoder/b62_caption_capi_conversion.hpp"
#include "decoder/b62_decoder_capi_internal.hpp"

using namespace aribcaption;
using namespace aribcaption::internal;

extern "C" {

void aribcc_b62_decode_options_init(aribcc_b62_decode_options_t* options) {
    if (!options) {
        return;
    }
    options->document_pts = ARIBCC_PTS_NOPTS;
    options->time_base_pts = ARIBCC_PTS_NOPTS;
    options->operation_mode = ARIBCC_B62_OPERATION_MODE_SEGMENT;
    options->align_earliest_to_document_pts = false;
    options->ignore_document_timing = false;
    options->discontinuity = false;
}

void aribcc_b62_resource_context_init(aribcc_b62_resource_context_t* resource_context) {
    if (!resource_context) {
        return;
    }
    std::memset(resource_context, 0, sizeof(*resource_context));
    resource_context->struct_size = sizeof(*resource_context);
}

aribcc_b62_decoder_t* aribcc_b62_decoder_alloc(aribcc_context_t* context) {
    if (!context) {
        return nullptr;
    }
    auto* ctx = reinterpret_cast<Context*>(context);
    return new(std::nothrow) aribcc_b62_decoder_t(*ctx);
}

void aribcc_b62_decoder_free(aribcc_b62_decoder_t* decoder) {
    delete decoder;
}

void aribcc_b62_decoder_reset(aribcc_b62_decoder_t* decoder) {
    if (decoder) {
        decoder->decoder.Reset();
    }
}

aribcc_b62_decode_status_t aribcc_b62_decoder_decode(
    aribcc_b62_decoder_t* decoder,
    const uint8_t* ttml_data,
    size_t length,
    int64_t base_pts,
    aribcc_b62_decode_result_t* out_result) {
    if (!decoder || !out_result) {
        return ARIBCC_B62_DECODE_STATUS_ERROR;
    }
    B62DecodeResult result;
    B62DecodeStatus status = decoder->decoder.Decode(ttml_data, length, base_pts, result);
    return ConvertB62DecodeResultToCAPI(std::move(result), status, out_result);
}

aribcc_b62_decode_status_t aribcc_b62_decoder_decode_with_options(
    aribcc_b62_decoder_t* decoder,
    const uint8_t* ttml_data,
    size_t length,
    const aribcc_b62_decode_options_t* options,
    aribcc_b62_decode_result_t* out_result) {
    if (!decoder || !options || !out_result) {
        return ARIBCC_B62_DECODE_STATUS_ERROR;
    }
    B62DecodeOptions cpp_options;
    cpp_options.document_pts = options->document_pts;
    cpp_options.time_base_pts = options->time_base_pts;
    cpp_options.operation_mode = static_cast<B62OperationMode>(options->operation_mode);
    cpp_options.align_earliest_to_document_pts = options->align_earliest_to_document_pts;
    cpp_options.ignore_document_timing = options->ignore_document_timing;
    cpp_options.discontinuity = options->discontinuity;

    B62DecodeResult result;
    B62DecodeStatus status = decoder->decoder.Decode(ttml_data, length, cpp_options, result);
    return ConvertB62DecodeResultToCAPI(std::move(result), status, out_result);
}

aribcc_b62_decode_status_t aribcc_b62_decoder_decode_with_resources(
    aribcc_b62_decoder_t* decoder,
    const uint8_t* ttml_data,
    size_t length,
    const aribcc_b62_decode_options_t* options,
    const aribcc_b62_resource_context_t* resource_context,
    aribcc_b62_decode_result_t* out_result) {
    if (!decoder || !ttml_data || length == 0 || !options || !resource_context || !out_result ||
        resource_context->struct_size < sizeof(aribcc_b62_resource_context_t) ||
        (resource_context->resource_count != 0 && !resource_context->resources) ||
        resource_context->resource_count > 256 ||
        options->operation_mode < ARIBCC_B62_OPERATION_MODE_LIVE ||
        options->operation_mode > ARIBCC_B62_OPERATION_MODE_PROGRAM) {
        if (out_result) {
            std::memset(out_result, 0, sizeof(*out_result));
        }
        return ARIBCC_B62_DECODE_STATUS_ERROR;
    }
    B62DecodeOptions cpp_options;
    cpp_options.document_pts = options->document_pts;
    cpp_options.time_base_pts = options->time_base_pts;
    cpp_options.operation_mode = static_cast<B62OperationMode>(options->operation_mode);
    cpp_options.align_earliest_to_document_pts = options->align_earliest_to_document_pts;
    cpp_options.ignore_document_timing = options->ignore_document_timing;
    cpp_options.discontinuity = options->discontinuity;

    std::array<B62ResourceView, 256> resource_views{};
    for (size_t i = 0; i < resource_context->resource_count; ++i) {
        const aribcc_b62_resource_t& resource = resource_context->resources[i];
        B62ResourceView& view = resource_views[i];
        view.index = resource.index;
        view.data = resource.data;
        view.size = resource.size;
        view.mime_type = resource.mime_type;
    }
    B62ResourceContextView cpp_resource_context;
    cpp_resource_context.scope_id = resource_context->scope_id;
    cpp_resource_context.resources = resource_views.data();
    cpp_resource_context.resource_count = resource_context->resource_count;

    B62DecodeResult result;
#if defined(__cpp_exceptions)
    try {
#endif
    B62DecodeStatus status = decoder->decoder.Decode(
        ttml_data, length, cpp_options, cpp_resource_context, result);
    return ConvertB62DecodeResultToCAPI(std::move(result), status, out_result);
#if defined(__cpp_exceptions)
    } catch (...) {
        CleanupB62DecodeResultCAPI(out_result);
        return ARIBCC_B62_DECODE_STATUS_ERROR;
    }
#endif
}

void aribcc_b62_decode_result_cleanup(aribcc_b62_decode_result_t* result) {
    CleanupB62DecodeResultCAPI(result);
}

}  // extern "C"
