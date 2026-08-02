/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DOCUMENT_H
#define ARIBCAPTION_B62_DOCUMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "b62_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum aribcc_b62_element_type_t {
    ARIBCC_B62_ELEMENT_TYPE_UNKNOWN = 0,
    ARIBCC_B62_ELEMENT_TYPE_DIV = 1,
    ARIBCC_B62_ELEMENT_TYPE_PARAGRAPH = 2,
    ARIBCC_B62_ELEMENT_TYPE_SPAN = 3,
} aribcc_b62_element_type_t;

typedef enum aribcc_b62_resource_kind_t {
    ARIBCC_B62_RESOURCE_KIND_UNKNOWN = 0,
    ARIBCC_B62_RESOURCE_KIND_SVG_FONT = 1,
    ARIBCC_B62_RESOURCE_KIND_WOFF_FONT = 2,
    ARIBCC_B62_RESOURCE_KIND_PNG_IMAGE = 3,
    ARIBCC_B62_RESOURCE_KIND_SVG_IMAGE = 4,
    ARIBCC_B62_RESOURCE_KIND_AUDIO = 5,
} aribcc_b62_resource_kind_t;

typedef enum aribcc_b62_font_format_t {
    ARIBCC_B62_FONT_FORMAT_UNKNOWN = 0,
    ARIBCC_B62_FONT_FORMAT_SVG = 1,
    ARIBCC_B62_FONT_FORMAT_WOFF = 2,
} aribcc_b62_font_format_t;

/** Borrowed view. All pointers remain valid until the owning sidecar is freed. */
typedef struct aribcc_b62_resource_blob_view_t {
    uint64_t scope_id;
    uint32_t index;
    aribcc_b62_resource_kind_t kind;
    const char* mime_type;
    const uint8_t* bytes;
    size_t byte_count;
} aribcc_b62_resource_blob_view_t;

/** Borrowed view. resolved is NULL when the resource could not be resolved. */
typedef struct aribcc_b62_resource_reference_view_t {
    const char* uri;
    const aribcc_b62_resource_blob_view_t* resolved;
} aribcc_b62_resource_reference_view_t;

typedef struct aribcc_b62_ruby_association_view_t {
    aribcc_b62_element_type_t annotation_type;
    aribcc_b62_element_type_t target_type;
    const char* annotation_id;
    const char* target_id;
    const char* annotation_text;
    const char* target_text;
} aribcc_b62_ruby_association_view_t;

typedef struct aribcc_b62_font_source_view_t {
    aribcc_b62_font_format_t format;
    aribcc_b62_resource_reference_view_t resource;
} aribcc_b62_font_source_view_t;

typedef struct aribcc_b62_font_face_view_t {
    const char* id;
    const char* family;
    const char* unicode_range;
    const aribcc_b62_font_source_view_t* sources;
    size_t source_count;
} aribcc_b62_font_face_view_t;

typedef struct aribcc_b62_audio_cue_view_t {
    aribcc_b62_element_type_t owner_type;
    const char* owner_id;
    const char* id;
    aribcc_b62_resource_reference_view_t source;
    int64_t begin_pts;
    bool has_end_pts;
    int64_t end_pts;
    bool loop;
} aribcc_b62_audio_cue_view_t;

typedef struct aribcc_b62_plane_rect_t {
    int x;
    int y;
    int width;
    int height;
} aribcc_b62_plane_rect_t;

typedef struct aribcc_b62_background_image_view_t {
    aribcc_b62_element_type_t owner_type;
    const char* owner_id;
    aribcc_b62_plane_rect_t layout_box;
    aribcc_b62_resource_reference_view_t source;
    int64_t begin_pts;
    bool has_end_pts;
    int64_t end_pts;
} aribcc_b62_background_image_view_t;

typedef struct aribcc_b62_document_sidecar_t aribcc_b62_document_sidecar_t;

typedef struct aribcc_b62_document_result_t {
    aribcc_caption_t* captions;
    uint32_t caption_count;
    aribcc_b62_document_sidecar_t* sidecar;
} aribcc_b62_document_result_t;

ARIBCC_API aribcc_b62_decode_status_t aribcc_b62_decoder_decode_document_with_resources(
    aribcc_b62_decoder_t* decoder,
    const uint8_t* ttml_data,
    size_t length,
    const aribcc_b62_decode_options_t* options,
    const aribcc_b62_resource_context_t* resource_context,
    aribcc_b62_document_result_t* out_result);

ARIBCC_API void aribcc_b62_document_result_cleanup(
    aribcc_b62_document_result_t* result);

ARIBCC_API size_t aribcc_b62_document_sidecar_ruby_association_count(
    const aribcc_b62_document_sidecar_t* sidecar);
ARIBCC_API const aribcc_b62_ruby_association_view_t*
aribcc_b62_document_sidecar_ruby_association_at(
    const aribcc_b62_document_sidecar_t* sidecar, size_t index);

ARIBCC_API size_t aribcc_b62_document_sidecar_font_face_count(
    const aribcc_b62_document_sidecar_t* sidecar);
ARIBCC_API const aribcc_b62_font_face_view_t* aribcc_b62_document_sidecar_font_face_at(
    const aribcc_b62_document_sidecar_t* sidecar, size_t index);

ARIBCC_API size_t aribcc_b62_document_sidecar_audio_cue_count(
    const aribcc_b62_document_sidecar_t* sidecar);
ARIBCC_API const aribcc_b62_audio_cue_view_t* aribcc_b62_document_sidecar_audio_cue_at(
    const aribcc_b62_document_sidecar_t* sidecar, size_t index);

ARIBCC_API size_t aribcc_b62_document_sidecar_background_image_count(
    const aribcc_b62_document_sidecar_t* sidecar);
ARIBCC_API const aribcc_b62_background_image_view_t*
aribcc_b62_document_sidecar_background_image_at(
    const aribcc_b62_document_sidecar_t* sidecar, size_t index);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // ARIBCAPTION_B62_DOCUMENT_H
