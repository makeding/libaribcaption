/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "aribcaption/b62_decoder.h"

static const char kTTML[] =
    "<tt xmlns=\"http://www.w3.org/ns/ttml\" "
    "xmlns:arib-tt=\"http://www.arib.or.jp/ns/arib-ttml/v1_0\" xml:lang=\"ja\">"
    "<body><div><p begin=\"0s\" end=\"2s\" "
    "arib-tt:border=\"solid 4px black\">C ABI</p></div></body>"
    "</tt>";

static const char kStructuredEmptyTTML[] =
    "<tt xmlns=\"http://www.w3.org/ns/ttml\"><head/><body/></tt>";

int main(void) {
    aribcc_context_t* context = aribcc_context_alloc();
    assert(context != NULL);
    aribcc_b62_decoder_t* decoder = aribcc_b62_decoder_alloc(context);
    assert(decoder != NULL);

    aribcc_b62_decode_options_t options;
    memset(&options, 0xff, sizeof(options));
    aribcc_b62_decode_options_init(&options);
    assert(options.document_pts == ARIBCC_PTS_NOPTS);
    assert(options.time_base_pts == ARIBCC_PTS_NOPTS);
    assert(options.operation_mode == ARIBCC_B62_OPERATION_MODE_SEGMENT);

    aribcc_b62_decode_result_t result = {0};
    aribcc_b62_decode_status_t status = aribcc_b62_decoder_decode(
        decoder, (const uint8_t*)kTTML, strlen(kTTML), 1000, &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_GOT_CAPTION);
    assert(result.caption_count == 2);
    assert(result.captions[0].pts == 1000);
    assert(strcmp(result.captions[0].text, "C ABI") == 0);
    assert(ARIBCC_COLOR_R(result.captions[0].regions[0].chars[0].enclosure_color) == 0);
    assert(ARIBCC_COLOR_A(result.captions[0].regions[0].chars[0].enclosure_color) == 255);
    assert(result.captions[0].regions[0].chars[0].enclosure_thickness == 4);
    assert(result.captions[1].regions == NULL);

    aribcc_b62_decode_result_cleanup(&result);
    assert(result.captions == NULL && result.caption_count == 0);

    status = aribcc_b62_decoder_decode(
        decoder,
        (const uint8_t*)kStructuredEmptyTTML,
        strlen(kStructuredEmptyTTML),
        2000,
        &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_NO_CAPTION);
    assert(result.captions == NULL && result.caption_count == 0);

    aribcc_b62_decoder_free(decoder);
    aribcc_context_free(context);
    return 0;
}
