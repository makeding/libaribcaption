/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "aribcaption/b62_document.h"

#include <array>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "aribcaption/b62_document.hpp"
#include "decoder/b62_caption_capi_conversion.hpp"
#include "decoder/b62_decoder_capi_internal.hpp"

using namespace aribcaption;
using namespace aribcaption::internal;

struct aribcc_b62_document_sidecar_t {
    using B62ResourceReferenceView = aribcc_b62_resource_reference_view_t;

    explicit aribcc_b62_document_sidecar_t(
        std::shared_ptr<const B62DocumentSidecar> source)
        : sidecar(std::move(source)) {
        BuildViews();
    }

    B62ResourceReferenceView BuildResourceReference(
        const B62ResourceReference& reference) {
        B62ResourceReferenceView view{};
        view.uri = reference.uri.c_str();
        if (!reference.resolved) {
            return view;
        }

        const B62ResourceBlob& source = *reference.resolved;
        aribcc_b62_resource_blob_view_t blob{};
        blob.scope_id = source.scope_id;
        blob.index = source.index;
        blob.kind = static_cast<aribcc_b62_resource_kind_t>(source.kind);
        blob.mime_type = source.mime_type.c_str();
        if (source.bytes) {
            blob.bytes = source.bytes->data();
            blob.byte_count = source.bytes->size();
        }
        resource_blobs.push_back(blob);
        view.resolved = &resource_blobs.back();
        return view;
    }

    void BuildViews() {
        const auto& ruby = sidecar->ruby_associations();
        const auto& faces = sidecar->font_faces();
        const auto& audios = sidecar->audio_cues();
        const auto& images = sidecar->background_images();

        size_t source_count = 0;
        for (const B62FontFace& face : faces) {
            source_count += face.sources.size();
        }
        resource_blobs.reserve(source_count + audios.size() + images.size());
        ruby_associations.reserve(ruby.size());
        font_sources.reserve(source_count);
        font_faces.reserve(faces.size());
        audio_cues.reserve(audios.size());
        background_images.reserve(images.size());

        for (const B62RubyAssociation& association : ruby) {
            aribcc_b62_ruby_association_view_t view{};
            view.annotation_type =
                static_cast<aribcc_b62_element_type_t>(association.annotation_type);
            view.target_type =
                static_cast<aribcc_b62_element_type_t>(association.target_type);
            view.annotation_id = association.annotation_id.c_str();
            view.target_id = association.target_id.c_str();
            view.annotation_text = association.annotation_text.c_str();
            view.target_text = association.target_text.c_str();
            ruby_associations.push_back(view);
        }

        for (const B62FontFace& face : faces) {
            const size_t first_source = font_sources.size();
            for (const B62FontSource& source : face.sources) {
                aribcc_b62_font_source_view_t source_view{};
                source_view.format =
                    static_cast<aribcc_b62_font_format_t>(source.format);
                source_view.resource = BuildResourceReference(source.resource);
                font_sources.push_back(source_view);
            }
            aribcc_b62_font_face_view_t face_view{};
            face_view.id = face.id.c_str();
            face_view.family = face.family.c_str();
            face_view.unicode_range = face.unicode_range.c_str();
            face_view.source_count = face.sources.size();
            if (!face.sources.empty()) {
                face_view.sources = font_sources.data() + first_source;
            }
            font_faces.push_back(face_view);
        }

        for (const B62AudioCue& audio : audios) {
            aribcc_b62_audio_cue_view_t view{};
            view.owner_type = static_cast<aribcc_b62_element_type_t>(audio.owner_type);
            view.owner_id = audio.owner_id.c_str();
            view.id = audio.id.c_str();
            view.source = BuildResourceReference(audio.source);
            view.begin_pts = audio.begin_pts;
            view.has_end_pts = audio.end_pts.has_value();
            view.end_pts = audio.end_pts.value_or(ARIBCC_PTS_NOPTS);
            view.loop = audio.loop;
            audio_cues.push_back(view);
        }

        for (const B62BackgroundImage& image : images) {
            aribcc_b62_background_image_view_t view{};
            view.owner_type = static_cast<aribcc_b62_element_type_t>(image.owner_type);
            view.owner_id = image.owner_id.c_str();
            view.layout_box.x = image.layout_box.x;
            view.layout_box.y = image.layout_box.y;
            view.layout_box.width = image.layout_box.width;
            view.layout_box.height = image.layout_box.height;
            view.source = BuildResourceReference(image.source);
            view.begin_pts = image.begin_pts;
            view.has_end_pts = image.end_pts.has_value();
            view.end_pts = image.end_pts.value_or(ARIBCC_PTS_NOPTS);
            background_images.push_back(view);
        }
    }

    std::shared_ptr<const B62DocumentSidecar> sidecar;
    std::vector<aribcc_b62_resource_blob_view_t> resource_blobs;
    std::vector<aribcc_b62_ruby_association_view_t> ruby_associations;
    std::vector<aribcc_b62_font_source_view_t> font_sources;
    std::vector<aribcc_b62_font_face_view_t> font_faces;
    std::vector<aribcc_b62_audio_cue_view_t> audio_cues;
    std::vector<aribcc_b62_background_image_view_t> background_images;
};

namespace {

void CleanupDocumentResult(aribcc_b62_document_result_t* result) noexcept {
    if (!result) {
        return;
    }
    aribcc_b62_decode_result_t captions{result->captions, result->caption_count};
    CleanupB62DecodeResultCAPI(&captions);
    delete result->sidecar;
    result->captions = nullptr;
    result->caption_count = 0;
    result->sidecar = nullptr;
}

template <typename T>
const T* ViewAt(const std::vector<T>& views, size_t index) noexcept {
    return index < views.size() ? &views[index] : nullptr;
}

}  // namespace

extern "C" {

aribcc_b62_decode_status_t aribcc_b62_decoder_decode_document_with_resources(
    aribcc_b62_decoder_t* decoder,
    const uint8_t* ttml_data,
    size_t length,
    const aribcc_b62_decode_options_t* options,
    const aribcc_b62_resource_context_t* resource_context,
    aribcc_b62_document_result_t* out_result) {
    if (out_result) {
        std::memset(out_result, 0, sizeof(*out_result));
    }
    if (!decoder || !ttml_data || length == 0 || !options || !resource_context ||
        !out_result ||
        resource_context->struct_size < sizeof(aribcc_b62_resource_context_t) ||
        (resource_context->resource_count != 0 && !resource_context->resources) ||
        resource_context->resource_count > 256 ||
        options->operation_mode < ARIBCC_B62_OPERATION_MODE_LIVE ||
        options->operation_mode > ARIBCC_B62_OPERATION_MODE_PROGRAM) {
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
        resource_views[i].index = resource.index;
        resource_views[i].data = resource.data;
        resource_views[i].size = resource.size;
        resource_views[i].mime_type = resource.mime_type;
    }
    B62ResourceContextView cpp_resource_context;
    cpp_resource_context.scope_id = resource_context->scope_id;
    cpp_resource_context.resources = resource_views.data();
    cpp_resource_context.resource_count = resource_context->resource_count;

    aribcc_b62_document_result_t converted{};
#if defined(__cpp_exceptions)
    try {
#endif
        B62DocumentDecodeResult document;
        B62DecodeStatus status = decoder->decoder.DecodeDocument(
            ttml_data, length, cpp_options, cpp_resource_context, document);
        if (status != B62DecodeStatus::kGotCaption) {
            return static_cast<aribcc_b62_decode_status_t>(status);
        }

        B62DecodeResult captions;
        captions.captions = std::move(document.captions);
        aribcc_b62_decode_result_t converted_captions{};
        aribcc_b62_decode_status_t converted_status = ConvertB62DecodeResultToCAPI(
            std::move(captions), status, &converted_captions);
        if (converted_status != ARIBCC_B62_DECODE_STATUS_GOT_CAPTION) {
            return converted_status;
        }
        converted.captions = converted_captions.captions;
        converted.caption_count = converted_captions.caption_count;

        if (document.sidecar) {
            converted.sidecar = new(std::nothrow)
                aribcc_b62_document_sidecar_t(std::move(document.sidecar));
            if (!converted.sidecar) {
                CleanupDocumentResult(&converted);
                return ARIBCC_B62_DECODE_STATUS_ERROR;
            }
        }
        *out_result = converted;
        return converted_status;
#if defined(__cpp_exceptions)
    } catch (...) {
        CleanupDocumentResult(&converted);
        return ARIBCC_B62_DECODE_STATUS_ERROR;
    }
#endif
}

void aribcc_b62_document_result_cleanup(aribcc_b62_document_result_t* result) {
    CleanupDocumentResult(result);
}

size_t aribcc_b62_document_sidecar_ruby_association_count(
    const aribcc_b62_document_sidecar_t* sidecar) {
    return sidecar ? sidecar->ruby_associations.size() : 0;
}

const aribcc_b62_ruby_association_view_t*
aribcc_b62_document_sidecar_ruby_association_at(
    const aribcc_b62_document_sidecar_t* sidecar, size_t index) {
    return sidecar ? ViewAt(sidecar->ruby_associations, index) : nullptr;
}

size_t aribcc_b62_document_sidecar_font_face_count(
    const aribcc_b62_document_sidecar_t* sidecar) {
    return sidecar ? sidecar->font_faces.size() : 0;
}

const aribcc_b62_font_face_view_t* aribcc_b62_document_sidecar_font_face_at(
    const aribcc_b62_document_sidecar_t* sidecar, size_t index) {
    return sidecar ? ViewAt(sidecar->font_faces, index) : nullptr;
}

size_t aribcc_b62_document_sidecar_audio_cue_count(
    const aribcc_b62_document_sidecar_t* sidecar) {
    return sidecar ? sidecar->audio_cues.size() : 0;
}

const aribcc_b62_audio_cue_view_t* aribcc_b62_document_sidecar_audio_cue_at(
    const aribcc_b62_document_sidecar_t* sidecar, size_t index) {
    return sidecar ? ViewAt(sidecar->audio_cues, index) : nullptr;
}

size_t aribcc_b62_document_sidecar_background_image_count(
    const aribcc_b62_document_sidecar_t* sidecar) {
    return sidecar ? sidecar->background_images.size() : 0;
}

const aribcc_b62_background_image_view_t*
aribcc_b62_document_sidecar_background_image_at(
    const aribcc_b62_document_sidecar_t* sidecar, size_t index) {
    return sidecar ? ViewAt(sidecar->background_images, index) : nullptr;
}

}  // extern "C"
