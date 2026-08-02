/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_caption_capi_conversion.hpp"

#include <cstdlib>
#include <cstring>
#include <new>
#include <unordered_map>
#include <utility>

namespace aribcaption::internal {
namespace {

static_assert(sizeof(aribcc_caption_char_t) == sizeof(CaptionChar));
static_assert(alignof(aribcc_caption_char_t) == alignof(CaptionChar));

bool ConvertCaptionRegionToCAPI(const CaptionRegion& region,
                               aribcc_caption_region_t* out_region) {
    out_region->x = region.x;
    out_region->y = region.y;
    out_region->width = region.width;
    out_region->height = region.height;
    out_region->is_ruby = region.is_ruby;
    out_region->char_count = static_cast<uint32_t>(region.chars.size());
    if (!region.chars.empty()) {
        out_region->chars = static_cast<aribcc_caption_char_t*>(
            std::calloc(out_region->char_count, sizeof(aribcc_caption_char_t)));
        if (!out_region->chars) {
            out_region->char_count = 0;
            return false;
        }
    }
    for (uint32_t i = 0; i < out_region->char_count; ++i) {
        out_region->chars[i] =
            *reinterpret_cast<const aribcc_caption_char_t*>(&region.chars[i]);
    }
    return true;
}

bool ConvertCaptionToCAPI(Caption&& caption, aribcc_caption_t* out_caption) {
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
        if (!out_caption->text) {
            return false;
        }
        std::memcpy(out_caption->text, caption.text.c_str(), caption.text.size() + 1);
    }

    out_caption->region_count = static_cast<uint32_t>(caption.regions.size());
    if (!caption.regions.empty()) {
        out_caption->regions = static_cast<aribcc_caption_region_t*>(
            std::calloc(out_caption->region_count, sizeof(aribcc_caption_region_t)));
        if (!out_caption->regions) {
            out_caption->region_count = 0;
            return false;
        }
    }
    for (uint32_t i = 0; i < out_caption->region_count; ++i) {
        if (!ConvertCaptionRegionToCAPI(caption.regions[i], &out_caption->regions[i])) {
            return false;
        }
    }

    if (!caption.drcs_map.empty()) {
        auto* drcs_map = new(std::nothrow)
            std::unordered_map<uint32_t, DRCS>(std::move(caption.drcs_map));
        if (!drcs_map) {
            return false;
        }
        out_caption->drcs_map = reinterpret_cast<aribcc_drcsmap_t*>(drcs_map);
    }
    return true;
}

}  // namespace

void CleanupB62DecodeResultCAPI(aribcc_b62_decode_result_t* result) noexcept {
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

aribcc_b62_decode_status_t ConvertB62DecodeResultToCAPI(
    B62DecodeResult&& result,
    B62DecodeStatus status,
    aribcc_b62_decode_result_t* out_result) {
    std::memset(out_result, 0, sizeof(*out_result));
    if (status != B62DecodeStatus::kGotCaption) {
        return static_cast<aribcc_b62_decode_status_t>(status);
    }

    aribcc_b62_decode_result_t converted{};
    converted.caption_count = static_cast<uint32_t>(result.captions.size());
    if (!result.captions.empty()) {
        converted.captions = static_cast<aribcc_caption_t*>(
            std::calloc(converted.caption_count, sizeof(aribcc_caption_t)));
        if (!converted.captions) {
            return ARIBCC_B62_DECODE_STATUS_ERROR;
        }
    }
    for (uint32_t i = 0; i < converted.caption_count; ++i) {
        if (!ConvertCaptionToCAPI(std::move(result.captions[i]), &converted.captions[i])) {
            CleanupB62DecodeResultCAPI(&converted);
            return ARIBCC_B62_DECODE_STATUS_ERROR;
        }
    }
    *out_result = converted;
    return static_cast<aribcc_b62_decode_status_t>(status);
}

}  // namespace aribcaption::internal
