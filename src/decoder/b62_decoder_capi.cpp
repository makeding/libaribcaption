/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <cstdlib>
#include <cstring>
#include <new>
#include <unordered_map>
#include <utility>

#include "aribcaption/b62_decoder.h"
#include "aribcaption/b62_decoder.hpp"
#include "decoder/b62_decoder_impl.hpp"

using namespace aribcaption;
using namespace aribcaption::internal;

namespace {

void ConvertCaptionRegionToCAPI(const CaptionRegion& region, aribcc_caption_region_t* out_region) {
    out_region->x = region.x;
    out_region->y = region.y;
    out_region->width = region.width;
    out_region->height = region.height;
    out_region->is_ruby = region.is_ruby;
    out_region->char_count = static_cast<uint32_t>(region.chars.size());
    if (!region.chars.empty()) {
        out_region->chars = static_cast<aribcc_caption_char_t*>(
            std::calloc(out_region->char_count, sizeof(aribcc_caption_char_t)));
    }
    for (uint32_t i = 0; i < out_region->char_count; ++i) {
        out_region->chars[i] = *reinterpret_cast<const aribcc_caption_char_t*>(&region.chars[i]);
    }
}

void ConvertCaptionToCAPI(Caption&& caption, aribcc_caption_t* out_caption) {
    out_caption->type = static_cast<aribcc_captiontype_t>(caption.type);
    out_caption->flags = static_cast<aribcc_captionflags_t>(caption.flags);
    out_caption->iso6392_language_code = caption.iso6392_language_code;
    out_caption->pts = caption.pts;
    out_caption->wait_duration = caption.wait_duration;
    out_caption->plane_width = caption.plane_width;
    out_caption->plane_height = caption.plane_height;
    out_caption->has_builtin_sound = caption.has_builtin_sound;
    out_caption->builtin_sound_id = caption.builtin_sound_id;

    if (!caption.text.empty()) {
        out_caption->text = static_cast<char*>(std::malloc(caption.text.size() + 1));
        if (out_caption->text) {
            std::memcpy(out_caption->text, caption.text.c_str(), caption.text.size() + 1);
        }
    }
    out_caption->region_count = static_cast<uint32_t>(caption.regions.size());
    if (!caption.regions.empty()) {
        out_caption->regions = static_cast<aribcc_caption_region_t*>(
            std::calloc(out_caption->region_count, sizeof(aribcc_caption_region_t)));
    }
    for (uint32_t i = 0; i < out_caption->region_count; ++i) {
        ConvertCaptionRegionToCAPI(caption.regions[i], &out_caption->regions[i]);
    }
    if (!caption.drcs_map.empty()) {
        auto* drcs_map = new(std::nothrow) std::unordered_map<uint32_t, DRCS>(std::move(caption.drcs_map));
        out_caption->drcs_map = reinterpret_cast<aribcc_drcsmap_t*>(drcs_map);
    }
}

aribcc_b62_decode_status_t ConvertResult(B62DecodeResult&& result,
                                         B62DecodeStatus status,
                                         aribcc_b62_decode_result_t* out_result) {
    std::memset(out_result, 0, sizeof(*out_result));
    if (status != B62DecodeStatus::kGotCaption) {
        return static_cast<aribcc_b62_decode_status_t>(status);
    }
    out_result->caption_count = static_cast<uint32_t>(result.captions.size());
    if (!result.captions.empty()) {
        out_result->captions = static_cast<aribcc_caption_t*>(
            std::calloc(out_result->caption_count, sizeof(aribcc_caption_t)));
        if (!out_result->captions) {
            out_result->caption_count = 0;
            return ARIBCC_B62_DECODE_STATUS_ERROR;
        }
    }
    for (uint32_t i = 0; i < out_result->caption_count; ++i) {
        ConvertCaptionToCAPI(std::move(result.captions[i]), &out_result->captions[i]);
    }
    return static_cast<aribcc_b62_decode_status_t>(status);
}

}  // namespace

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

aribcc_b62_decoder_t* aribcc_b62_decoder_alloc(aribcc_context_t* context) {
    if (!context) {
        return nullptr;
    }
    auto* ctx = reinterpret_cast<Context*>(context);
    auto* decoder = new(std::nothrow) B62DecoderImpl(*ctx);
    return reinterpret_cast<aribcc_b62_decoder_t*>(decoder);
}

void aribcc_b62_decoder_free(aribcc_b62_decoder_t* decoder) {
    delete reinterpret_cast<B62DecoderImpl*>(decoder);
}

void aribcc_b62_decoder_set_font_scale(aribcc_b62_decoder_t* decoder, float scale) {
    if (decoder) {
        reinterpret_cast<B62DecoderImpl*>(decoder)->SetFontScale(scale);
    }
}

void aribcc_b62_decoder_reset(aribcc_b62_decoder_t* decoder) {
    if (decoder) {
        reinterpret_cast<B62DecoderImpl*>(decoder)->Reset();
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
    static_assert(sizeof(aribcc_caption_char_t) == sizeof(CaptionChar));
    B62DecodeResult result;
    B62DecodeStatus status = reinterpret_cast<B62DecoderImpl*>(decoder)->Decode(
        ttml_data, length, base_pts, result);
    return ConvertResult(std::move(result), status, out_result);
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
    static_assert(sizeof(aribcc_caption_char_t) == sizeof(CaptionChar));
    B62DecodeOptions cpp_options;
    cpp_options.document_pts = options->document_pts;
    cpp_options.time_base_pts = options->time_base_pts;
    cpp_options.operation_mode = static_cast<B62OperationMode>(options->operation_mode);
    cpp_options.align_earliest_to_document_pts = options->align_earliest_to_document_pts;
    cpp_options.ignore_document_timing = options->ignore_document_timing;
    cpp_options.discontinuity = options->discontinuity;

    B62DecodeResult result;
    B62DecodeStatus status = reinterpret_cast<B62DecoderImpl*>(decoder)->Decode(
        ttml_data, length, cpp_options, result);
    return ConvertResult(std::move(result), status, out_result);
}

void aribcc_b62_decode_result_cleanup(aribcc_b62_decode_result_t* result) {
    if (!result) {
        return;
    }
    if (result->captions) {
        for (uint32_t i = 0; i < result->caption_count; ++i) {
            aribcc_caption_cleanup(&result->captions[i]);
        }
        std::free(result->captions);
    }
    result->captions = nullptr;
    result->caption_count = 0;
}

}  // extern "C"
