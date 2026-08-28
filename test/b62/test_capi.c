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

#include "aribcaption/aribcaption.h"

static const char kTTML[] =
    "<tt xmlns=\"http://www.w3.org/ns/ttml\" "
    "xmlns:arib-tt=\"http://www.arib.or.jp/ns/arib-ttml/v1_0\" xml:lang=\"ja\">"
    "<body><div><p begin=\"0s\" end=\"2s\" "
    "arib-tt:border=\"solid 4px black\">C ABI</p></div></body>"
    "</tt>";

static const char kStructuredEmptyTTML[] =
    "<tt xmlns=\"http://www.w3.org/ns/ttml\"><head/><body/></tt>";

static const char kDocumentTTML[] =
    "<tt xmlns=\"http://www.w3.org/ns/ttml\" "
    "xmlns:tts=\"http://www.w3.org/ns/ttml#styling\" "
    "xmlns:arib-tt=\"http://www.arib.or.jp/ns/arib-tt\" "
    "xmlns:smpte=\"http://www.smpte-ra.org/schemas/2052-1/2013/smpte-tt\" "
    "xml:lang=\"ja\"><head><styling>"
    "<arib-tt:font-face xml:id=\"gaiji\" font-family=\"External\" "
    "unicode-range=\"U+E000-E001\"><arib-tt:src url=\"subt://1\" "
    "format=\"woff\"/></arib-tt:font-face></styling><layout>"
    "<region xml:id=\"image-region\" tts:origin=\"100px 200px\" "
    "tts:extent=\"640px 360px\"/></layout><metadata>"
    "<smpte:image xml:id=\"embedded\" imageType=\"PNG\" encoding=\"Base64\">"
    "iVBORw==</smpte:image></metadata></head><body>"
    "<div><p xml:id=\"ruby-base\" region=\"image-region\" begin=\"0s\" end=\"10s\">"
    "Base<span xml:id=\"ruby-annotation\" arib-tt:ruby=\"ruby-base\">Ruby</span>"
    "</p></div><div xml:id=\"audio-owner\" begin=\"2s\" end=\"4s\">"
    "<arib-tt:audio xml:id=\"audio\" src=\"subt://2\" loop=\"true\"/>"
    "</div><div xml:id=\"external-image\" region=\"image-region\" begin=\"5s\" "
    "end=\"7s\" smpte:backgroundImage=\"subt://3\"/>"
    "<div xml:id=\"embedded-image\" region=\"image-region\" begin=\"8s\" "
    "end=\"10s\" smpte:backgroundImage=\"#embedded\"/></body></tt>";

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
    assert(result.captions[0].type == ARIBCC_CAPTIONTYPE_CAPTION);
    assert(result.captions[0].pts == 1000);
    assert(strcmp(result.captions[0].text, "C ABI") == 0);
    assert(ARIBCC_COLOR_R(result.captions[0].regions[0].chars[0].enclosure_color) == 0);
    assert(ARIBCC_COLOR_A(result.captions[0].regions[0].chars[0].enclosure_color) == 255);
    assert(result.captions[0].regions[0].chars[0].enclosure_thickness == 4);
    assert(result.captions[1].regions == NULL);

    aribcc_b62_decode_result_cleanup(&result);
    assert(result.captions == NULL && result.caption_count == 0);

    assert(aribcc_b62_decoder_alloc_with_type(
        context, (aribcc_captiontype_t)0) == NULL);
    aribcc_b62_decoder_t* superimpose_decoder = aribcc_b62_decoder_alloc_with_type(
        context, ARIBCC_CAPTIONTYPE_SUPERIMPOSE);
    assert(superimpose_decoder != NULL);
    status = aribcc_b62_decoder_decode(
        superimpose_decoder, (const uint8_t*)kTTML, strlen(kTTML), 1000, &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_GOT_CAPTION);
    assert(result.caption_count == 2);
    for (uint32_t i = 0; i < result.caption_count; ++i) {
        assert(result.captions[i].type == ARIBCC_CAPTIONTYPE_SUPERIMPOSE);
    }
    aribcc_b62_decode_result_cleanup(&result);
    aribcc_b62_decoder_free(superimpose_decoder);

    const uint8_t resource_bytes[] = {0x00, 0x01, 0x02, 0x03};
    aribcc_b62_resource_t resource = {
        .index = 1,
        .data = resource_bytes,
        .size = sizeof(resource_bytes),
        .mime_type = "image/svg+xml",
    };
    aribcc_b62_resource_context_t resource_context;
    aribcc_b62_resource_context_init(&resource_context);
    assert(resource_context.struct_size == sizeof(resource_context));
    resource_context.scope_id = 0x10001;
    resource_context.resources = &resource;
    resource_context.resource_count = 1;
    status = aribcc_b62_decoder_decode_with_resources(
        decoder, (const uint8_t*)kTTML, strlen(kTTML), &options, &resource_context, &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_GOT_CAPTION);
    aribcc_b62_decode_result_cleanup(&result);

    resource_context.struct_size = 0;
    status = aribcc_b62_decoder_decode_with_resources(
        decoder, (const uint8_t*)kTTML, strlen(kTTML), &options, &resource_context, &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_ERROR);
    assert(result.captions == NULL && result.caption_count == 0);

    resource_context.struct_size = sizeof(resource_context) + 16;
    status = aribcc_b62_decoder_decode_with_resources(
        decoder, (const uint8_t*)kTTML, strlen(kTTML), &options, &resource_context, &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_GOT_CAPTION);
    aribcc_b62_decode_result_cleanup(&result);

    resource_context.struct_size = sizeof(resource_context);
    resource_context.resources = NULL;
    resource_context.resource_count = 1;
    status = aribcc_b62_decoder_decode_with_resources(
        decoder, (const uint8_t*)kTTML, strlen(kTTML), &options, &resource_context, &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_ERROR);

    resource_context.resources = &resource;
    resource_context.resource_count = 257;
    status = aribcc_b62_decoder_decode_with_resources(
        decoder, (const uint8_t*)kTTML, strlen(kTTML), &options, &resource_context, &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_ERROR);

    resource_context.resource_count = 1;
    resource.data = NULL;
    resource.size = 1;
    status = aribcc_b62_decoder_decode_with_resources(
        decoder, (const uint8_t*)kTTML, strlen(kTTML), &options, &resource_context, &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_ERROR);

    resource.data = resource_bytes;
    resource.size = sizeof(resource_bytes);
    options.operation_mode = (aribcc_b62_operation_mode_t)99;
    status = aribcc_b62_decoder_decode_with_resources(
        decoder, (const uint8_t*)kTTML, strlen(kTTML), &options, &resource_context, &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_ERROR);
    options.operation_mode = ARIBCC_B62_OPERATION_MODE_SEGMENT;

    status = aribcc_b62_decoder_decode(
        decoder,
        (const uint8_t*)kStructuredEmptyTTML,
        strlen(kStructuredEmptyTTML),
        2000,
        &result);
    assert(status == ARIBCC_B62_DECODE_STATUS_NO_CAPTION);
    assert(result.captions == NULL && result.caption_count == 0);

    uint8_t font_bytes[] = {0x10, 0x11, 0x12};
    uint8_t audio_bytes[] = {0x20, 0x21, 0x22, 0x23};
    uint8_t image_bytes[] = {0x30, 0x31, 0x32, 0x33, 0x34};
    aribcc_b62_resource_t document_resources[] = {
        {.index = 1, .data = font_bytes, .size = sizeof(font_bytes), .mime_type = "font/woff"},
        {.index = 2, .data = audio_bytes, .size = sizeof(audio_bytes), .mime_type = "audio/aiff"},
        {.index = 3, .data = image_bytes, .size = sizeof(image_bytes), .mime_type = "image/png"},
    };
    aribcc_b62_resource_context_init(&resource_context);
    resource_context.scope_id = 0x20002;
    resource_context.resources = document_resources;
    resource_context.resource_count = 3;
    options.document_pts = 0;

    aribcc_b62_document_result_t document_result = {0};
    status = aribcc_b62_decoder_decode_document_with_resources(
        decoder, (const uint8_t*)kDocumentTTML, strlen(kDocumentTTML),
        &options, &resource_context, &document_result);
    assert(status == ARIBCC_B62_DECODE_STATUS_GOT_CAPTION);
    assert(document_result.caption_count != 0);
    assert(document_result.captions != NULL);
    assert(document_result.sidecar != NULL);

    assert(aribcc_b62_document_sidecar_ruby_association_count(document_result.sidecar) == 1);
    const aribcc_b62_ruby_association_view_t* ruby =
        aribcc_b62_document_sidecar_ruby_association_at(document_result.sidecar, 0);
    assert(ruby != NULL);
    assert(ruby->annotation_type == ARIBCC_B62_ELEMENT_TYPE_SPAN);
    assert(ruby->target_type == ARIBCC_B62_ELEMENT_TYPE_PARAGRAPH);
    assert(strcmp(ruby->annotation_id, "ruby-annotation") == 0);
    assert(strcmp(ruby->target_id, "ruby-base") == 0);
    assert(strcmp(ruby->annotation_text, "Ruby") == 0);
    assert(strcmp(ruby->target_text, "BaseRuby") == 0);
    assert(aribcc_b62_document_sidecar_ruby_association_at(document_result.sidecar, 1) == NULL);

    assert(aribcc_b62_document_sidecar_font_face_count(document_result.sidecar) == 1);
    const aribcc_b62_font_face_view_t* face =
        aribcc_b62_document_sidecar_font_face_at(document_result.sidecar, 0);
    assert(face != NULL);
    assert(strcmp(face->id, "gaiji") == 0);
    assert(strcmp(face->family, "External") == 0);
    assert(strcmp(face->unicode_range, "U+E000-E001") == 0);
    assert(face->source_count == 1 && face->sources != NULL);
    assert(face->sources[0].format == ARIBCC_B62_FONT_FORMAT_WOFF);
    assert(strcmp(face->sources[0].resource.uri, "subt://1") == 0);
    assert(face->sources[0].resource.resolved != NULL);
    assert(face->sources[0].resource.resolved->scope_id == 0x20002);
    assert(face->sources[0].resource.resolved->index == 1);
    assert(face->sources[0].resource.resolved->kind == ARIBCC_B62_RESOURCE_KIND_WOFF_FONT);
    assert(strcmp(face->sources[0].resource.resolved->mime_type, "font/woff") == 0);
    assert(face->sources[0].resource.resolved->byte_count == sizeof(font_bytes));
    assert(aribcc_b62_document_sidecar_font_face_at(document_result.sidecar, 1) == NULL);

    assert(aribcc_b62_document_sidecar_audio_cue_count(document_result.sidecar) == 1);
    const aribcc_b62_audio_cue_view_t* audio =
        aribcc_b62_document_sidecar_audio_cue_at(document_result.sidecar, 0);
    assert(audio != NULL);
    assert(audio->owner_type == ARIBCC_B62_ELEMENT_TYPE_DIV);
    assert(strcmp(audio->owner_id, "audio-owner") == 0);
    assert(strcmp(audio->id, "audio") == 0);
    assert(strcmp(audio->source.uri, "subt://2") == 0);
    assert(audio->source.resolved != NULL);
    assert(audio->source.resolved->kind == ARIBCC_B62_RESOURCE_KIND_AUDIO);
    assert(audio->begin_pts == 2000 && audio->has_end_pts && audio->end_pts == 4000);
    assert(audio->loop);
    assert(aribcc_b62_document_sidecar_audio_cue_at(document_result.sidecar, 1) == NULL);

    assert(aribcc_b62_document_sidecar_background_image_count(document_result.sidecar) == 2);
    const aribcc_b62_background_image_view_t* external_image =
        aribcc_b62_document_sidecar_background_image_at(document_result.sidecar, 0);
    const aribcc_b62_background_image_view_t* embedded_image =
        aribcc_b62_document_sidecar_background_image_at(document_result.sidecar, 1);
    assert(external_image != NULL && embedded_image != NULL);
    assert(strcmp(external_image->owner_id, "external-image") == 0);
    assert(external_image->layout_box.x == 100 && external_image->layout_box.y == 200);
    assert(external_image->layout_box.width == 640 && external_image->layout_box.height == 360);
    assert(external_image->begin_pts == 5000 && external_image->has_end_pts &&
           external_image->end_pts == 7000);
    assert(external_image->source.resolved != NULL);
    assert(external_image->source.resolved->kind == ARIBCC_B62_RESOURCE_KIND_PNG_IMAGE);
    assert(strcmp(embedded_image->source.uri, "#embedded") == 0);
    assert(embedded_image->source.resolved != NULL);
    assert(embedded_image->source.resolved->index == UINT32_MAX);
    assert(embedded_image->source.resolved->byte_count == 4);
    assert(aribcc_b62_document_sidecar_background_image_at(document_result.sidecar, 2) == NULL);

    memset(font_bytes, 0, sizeof(font_bytes));
    memset(audio_bytes, 0, sizeof(audio_bytes));
    memset(image_bytes, 0, sizeof(image_bytes));
    assert(face->sources[0].resource.resolved->bytes[0] == 0x10);
    assert(audio->source.resolved->bytes[0] == 0x20);
    assert(external_image->source.resolved->bytes[0] == 0x30);

    assert(aribcc_b62_document_sidecar_ruby_association_count(NULL) == 0);
    assert(aribcc_b62_document_sidecar_ruby_association_at(NULL, 0) == NULL);
    assert(aribcc_b62_document_sidecar_font_face_count(NULL) == 0);
    assert(aribcc_b62_document_sidecar_font_face_at(NULL, 0) == NULL);
    assert(aribcc_b62_document_sidecar_audio_cue_count(NULL) == 0);
    assert(aribcc_b62_document_sidecar_audio_cue_at(NULL, 0) == NULL);
    assert(aribcc_b62_document_sidecar_background_image_count(NULL) == 0);
    assert(aribcc_b62_document_sidecar_background_image_at(NULL, 0) == NULL);

#ifndef ARIBCC_NO_RENDERER
    aribcc_renderer_t* renderer = aribcc_renderer_alloc(context);
    assert(renderer != NULL);
    assert(aribcc_renderer_initialize(renderer, ARIBCC_CAPTIONTYPE_CAPTION,
                                      ARIBCC_FONTPROVIDER_TYPE_AUTO,
                                      ARIBCC_TEXTRENDERER_TYPE_AUTO));
    assert(aribcc_renderer_set_frame_size(renderer, 1920, 1080));
    assert(aribcc_renderer_set_margins(renderer, 0, 0, 0, 0));
    assert(aribcc_renderer_append_b62_document(renderer, &document_result));
    aribcc_b62_document_result_cleanup(&document_result);
    assert(document_result.captions == NULL && document_result.caption_count == 0 &&
           document_result.sidecar == NULL);
    aribcc_render_result_t render_result = {0};
    assert(aribcc_renderer_render(renderer, 0, &render_result) ==
           ARIBCC_RENDER_STATUS_GOT_IMAGE);
    assert(render_result.image_count != 0 && render_result.images != NULL);
    aribcc_render_result_cleanup(&render_result);
    aribcc_renderer_flush(renderer);
    assert(aribcc_renderer_render(renderer, 0, &render_result) ==
           ARIBCC_RENDER_STATUS_NO_IMAGE);
    aribcc_renderer_free(renderer);
#else
    aribcc_b62_document_result_cleanup(&document_result);
#endif
    assert(document_result.captions == NULL && document_result.caption_count == 0 &&
           document_result.sidecar == NULL);
    aribcc_b62_document_result_cleanup(&document_result);

    resource_context.struct_size = 0;
    document_result.captions = (aribcc_caption_t*)(uintptr_t)1;
    document_result.caption_count = 1;
    document_result.sidecar = (aribcc_b62_document_sidecar_t*)(uintptr_t)1;
    status = aribcc_b62_decoder_decode_document_with_resources(
        decoder, (const uint8_t*)kDocumentTTML, strlen(kDocumentTTML),
        &options, &resource_context, &document_result);
    assert(status == ARIBCC_B62_DECODE_STATUS_ERROR);
    assert(document_result.captions == NULL && document_result.caption_count == 0 &&
           document_result.sidecar == NULL);

    aribcc_b62_decoder_free(decoder);
    aribcc_context_free(context);
    return 0;
}
