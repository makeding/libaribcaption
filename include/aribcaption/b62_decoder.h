/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DECODER_H
#define ARIBCAPTION_B62_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aribcc_export.h"
#include "caption.h"
#include "context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum aribcc_b62_decode_status_t {
    ARIBCC_B62_DECODE_STATUS_ERROR = 0,
    ARIBCC_B62_DECODE_STATUS_NO_CAPTION = 1,
    ARIBCC_B62_DECODE_STATUS_GOT_CAPTION = 2,
} aribcc_b62_decode_status_t;

typedef enum aribcc_b62_operation_mode_t {
    ARIBCC_B62_OPERATION_MODE_LIVE = 0,
    ARIBCC_B62_OPERATION_MODE_SEGMENT = 1,
    ARIBCC_B62_OPERATION_MODE_PROGRAM = 2,
} aribcc_b62_operation_mode_t;

typedef struct aribcc_b62_decode_options_t {
    int64_t document_pts;
    int64_t time_base_pts;
    aribcc_b62_operation_mode_t operation_mode;
    bool align_earliest_to_document_pts;
    bool ignore_document_timing;
    bool discontinuity;
} aribcc_b62_decode_options_t;

/**
 * Non-owning view of one ARIB-TTML same-MPU resource.
 *
 * The decoder copies data and mime_type before the decode call returns.
 */
typedef struct aribcc_b62_resource_t {
    uint32_t index;
    const uint8_t* data;
    size_t size;
    const char* mime_type;
} aribcc_b62_resource_t;

/**
 * Versioned resource context for aribcc_b62_decoder_decode_with_resources().
 *
 * Initialize this structure with aribcc_b62_resource_context_init().
 * A zero scope_id is transient; resources are retained only for the call.
 */
typedef struct aribcc_b62_resource_context_t {
    size_t struct_size;
    uint64_t scope_id;
    const aribcc_b62_resource_t* resources;
    size_t resource_count;
} aribcc_b62_resource_context_t;

typedef struct aribcc_b62_decode_result_t {
    aribcc_caption_t* captions;
    uint32_t caption_count;
} aribcc_b62_decode_result_t;

typedef struct aribcc_b62_decoder_t aribcc_b62_decoder_t;

ARIBCC_API void aribcc_b62_decode_options_init(aribcc_b62_decode_options_t* options);
ARIBCC_API void aribcc_b62_resource_context_init(aribcc_b62_resource_context_t* resource_context);
ARIBCC_API aribcc_b62_decoder_t* aribcc_b62_decoder_alloc(aribcc_context_t* context);
ARIBCC_API aribcc_b62_decoder_t* aribcc_b62_decoder_alloc_with_type(
    aribcc_context_t* context,
    aribcc_captiontype_t caption_type);
ARIBCC_API void aribcc_b62_decoder_free(aribcc_b62_decoder_t* decoder);
ARIBCC_API void aribcc_b62_decoder_reset(aribcc_b62_decoder_t* decoder);

ARIBCC_API aribcc_b62_decode_status_t aribcc_b62_decoder_decode(
    aribcc_b62_decoder_t* decoder,
    const uint8_t* ttml_data,
    size_t length,
    int64_t base_pts,
    aribcc_b62_decode_result_t* out_result);

ARIBCC_API aribcc_b62_decode_status_t aribcc_b62_decoder_decode_with_options(
    aribcc_b62_decoder_t* decoder,
    const uint8_t* ttml_data,
    size_t length,
    const aribcc_b62_decode_options_t* options,
    aribcc_b62_decode_result_t* out_result);

ARIBCC_API aribcc_b62_decode_status_t aribcc_b62_decoder_decode_with_resources(
    aribcc_b62_decoder_t* decoder,
    const uint8_t* ttml_data,
    size_t length,
    const aribcc_b62_decode_options_t* options,
    const aribcc_b62_resource_context_t* resource_context,
    aribcc_b62_decode_result_t* out_result);

ARIBCC_API void aribcc_b62_decode_result_cleanup(aribcc_b62_decode_result_t* result);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // ARIBCAPTION_B62_DECODER_H
