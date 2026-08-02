/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_decoder_impl.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aribcaption/caption.hpp"
#include "base/tinyxml2.h"
#include "decoder/b62_document_metadata.hpp"
#include "decoder/b62_document_parser.hpp"
#include "decoder/b62_document_model.hpp"
#include "decoder/b62_layout.hpp"
#include "decoder/b62_resource_resolver.hpp"
#include "decoder/b62_style_context.hpp"
#include "decoder/b62_xml.hpp"

namespace aribcaption::internal {
namespace {

uint32_t ParseLanguage(const char* value) {
    if (!value) {
        return 0;
    }
    std::string language(value);
    std::transform(language.begin(), language.end(), language.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    size_t separator = language.find_first_of("-_");
    if (separator != std::string::npos) {
        language.resize(separator);
    }
    if (language == "ja" || language == "jpn")
        return ThreeCC("jpn");
    if (language == "en" || language == "eng")
        return ThreeCC("eng");
    if (language == "pt" || language == "por")
        return ThreeCC("por");
    if (language == "es" || language == "spa")
        return ThreeCC("spa");
    if (language.size() == 3) {
        return (static_cast<uint32_t>(language[0]) << 16) | (static_cast<uint32_t>(language[1]) << 8) |
               static_cast<uint32_t>(language[2]);
    }
    return 0;
}

}  // namespace

B62DecoderImpl::B62DecoderImpl(Context& context)
    : log_(GetContextLogger(context)), resource_store_(log_) {}

B62DecoderImpl::~B62DecoderImpl() = default;

void B62DecoderImpl::Reset() {
    legacy_presentation_state_.Reset();
    document_presentation_state_.Reset();
    resource_store_.Reset();
}

B62DecodeStatus B62DecoderImpl::Decode(const uint8_t* ttml_data,
                                       size_t length,
                                       int64_t base_pts,
                                       B62DecodeResult& out_result) {
    B62DecodeOptions options;
    options.document_pts = base_pts;
    options.align_earliest_to_document_pts = true;
    return Decode(ttml_data, length, options, out_result);
}

B62DecodeStatus B62DecoderImpl::Decode(const uint8_t* ttml_data,
                                       size_t length,
                                       const B62DecodeOptions& options,
                                       B62DecodeResult& out_result) {
    if (options.discontinuity) {
        Reset();
    }
    resource_store_.Deactivate();
    return DecodeInternal(ttml_data, length, options, false, nullptr, out_result);
}

B62DecodeStatus B62DecoderImpl::DecodeInternal(const uint8_t* ttml_data,
                                               size_t length,
                                               const B62DecodeOptions& options,
                                               bool preserve_document_layout,
                                               B62PresentationMetadata* document_metadata,
                                               B62DecodeResult& out_result) {
    out_result.captions.clear();
    B62PresentationState& presentation_state = preserve_document_layout
        ? document_presentation_state_
        : legacy_presentation_state_;
    if (document_metadata) {
        *document_metadata = {};
    }
    if (!ttml_data || length == 0) {
        log_->e("B62DecoderImpl: empty TTML document");
        return B62DecodeStatus::kError;
    }

    B62ParsedDocument document;
    B62ParseDisposition disposition = document.Parse(ttml_data, length);
    if (disposition == B62ParseDisposition::kInvalid) {
        log_->e("B62DecoderImpl: invalid TTML document: %s", document.error());
        return B62DecodeStatus::kError;
    }
    const tinyxml2::XMLElement* tt = document.root();
    B62PresentationMetadata parsed_metadata;
    if (preserve_document_layout && document_metadata) {
        B62CollectRubyAssociations(tt, parsed_metadata);
    }
    const auto emit_clear = [&]() {
        Reset();
        Caption clear;
        clear.flags = kCaptionFlagsClearScreen;
        clear.pts = options.document_pts;
        clear.wait_duration = DURATION_INDEFINITE;
        clear.plane_width = 3840;
        clear.plane_height = 2160;
        out_result.captions.push_back(std::move(clear));
        return B62DecodeStatus::kGotCaption;
    };
    if (disposition == B62ParseDisposition::kEmpty) {
        return emit_clear();
    }

    if (disposition == B62ParseDisposition::kNoBody) {
        return B62DecodeStatus::kNoCaption;
    }
    const tinyxml2::XMLElement* body = document.body();

    B62ResourceResolver resource_resolver(
        resource_store_, tt, preserve_document_layout && document_metadata);

    if (preserve_document_layout && document_metadata) {
        B62CollectFontFaces(tt, resource_resolver, parsed_metadata);
    }

    std::array<int, 2> plane{3840, 2160};
    if (auto extent = B62ParseLengthPair(B62FindAttribute(tt, "extent"), plane)) {
        plane = *extent;
    }

    B62StyleContext style_context(tt, plane);
    const auto& region_definitions = style_context.regions();

    B62TimedContent timed_content = B62CollectTimedContent(body);
    if (timed_content.empty()) {
        return B62DecodeStatus::kNoCaption;
    }

    int64_t timeline_offset = 0;
    if (options.time_base_pts != PTS_NOPTS) {
        timeline_offset = options.time_base_pts;
    } else if (options.align_earliest_to_document_pts &&
               options.document_pts != PTS_NOPTS && timed_content.minimum_start) {
        timeline_offset = options.document_pts - *timed_content.minimum_start;
    }
    uint32_t language = ParseLanguage(B62FindAttribute(tt, "lang"));

    if (preserve_document_layout && document_metadata) {
        B62MaterializeTimedMetadata(
            timed_content, plane, region_definitions, resource_resolver,
            timeline_offset, options.document_pts, parsed_metadata);
    }

    if (options.operation_mode != B62OperationMode::kLive || options.discontinuity) {
        presentation_state.Reset();
    }

    bool continuation_applied = false;
    if (options.operation_mode == B62OperationMode::kLive) {
        for (const B62RawCue& raw : timed_content.cues) {
            if (!raw.indefinite_start || raw.id.empty()) continue;
            std::optional<int64_t> end;
            if (raw.end) {
                end = *raw.end + timeline_offset;
            }
            continuation_applied = presentation_state.ApplyContinuation(
                raw.id, end, !raw.indefinite) || continuation_applied;
        }
    }

    std::vector<B62PresentationNode> document_nodes;

    for (const B62RawCue& raw : timed_content.cues) {
        if (raw.indefinite_start) {
            continue;
        }
        B62RegionDefinition definition;
        definition.x = plane[0] / 10;
        definition.y = plane[1] * 78 / 100;
        definition.width = plane[0] * 8 / 10;
        definition.height = plane[1] * 16 / 100;
        bool has_paragraph_region = false;
        const char* region_id = B62FindNearestAttribute(raw.paragraph, "region");
        if (region_id) {
            if (auto it = region_definitions.find(region_id); it != region_definitions.end()) {
                definition = it->second;
                has_paragraph_region = true;
            }
        }
        B62Style inherited = style_context.CollectInheritedStyle(
            raw.paragraph, definition.style);
        definition.style = inherited;
        B62RegionDefinition paragraph_formatting = preserve_document_layout
            ? B62MakeFormattingDefinition(definition, inherited)
            : definition;
        std::vector<B62InlineSpan> spans;
        style_context.AppendInlineSpans(
            raw.paragraph, inherited, spans,
            preserve_document_layout ? &definition : nullptr,
            preserve_document_layout,
            preserve_document_layout &&
                B62StyleContext::HasARIBRubyAncestor(raw.paragraph));
        if (!preserve_document_layout) {
            B62StyleContext::ResolveLegacyRuby(spans);
        }
        if (spans.empty()) {
            continue;
        }

        Caption caption;
        caption.flags = kCaptionFlagsClearScreen;
        caption.iso6392_language_code = language;
        caption.plane_width = plane[0];
        caption.plane_height = plane[1];
        caption.pts = options.ignore_document_timing
            ? options.document_pts
            : (raw.start ? *raw.start + timeline_offset : options.document_pts);
        for (const B62InlineSpan& span : spans) {
            if (!preserve_document_layout || !span.is_ruby) {
                caption.text += span.text;
            }
        }

        const auto layout = [&](const std::vector<B62InlineSpan>& layout_spans,
                                const B62RegionDefinition& layout_definition,
                                const B62RegionDefinition& clip_definition,
                                bool preserve_region_bounds) {
            if (B62IsVerticalWritingMode(layout_definition.style)) {
                B62LayoutVertical(layout_spans, layout_definition, clip_definition,
                                  plane, caption, preserve_region_bounds);
            } else {
                B62LayoutHorizontal(layout_spans, layout_definition, clip_definition,
                                    plane, caption, preserve_region_bounds);
            }
        };
        if (preserve_document_layout) {
            struct SpanFlow {
                const B62RegionDefinition* definition = nullptr;
                bool explicit_position = false;
                std::vector<B62InlineSpan> spans;
            };
            std::vector<SpanFlow> flows;
            for (const B62InlineSpan& span : spans) {
                if (flows.empty() || span.resets_position) {
                    flows.push_back({span.region ? span.region : &definition,
                                     span.resets_position, {}});
                }
                flows.back().spans.push_back(span);
            }
            for (const SpanFlow& flow : flows) {
                B62RegionDefinition formatting = B62MakeFormattingDefinition(
                    *flow.definition, flow.spans.front().style);
                if (flow.explicit_position) {
                    // A span region origin is the operation-position reference
                    // point of its first character, not an alignment box.
                    formatting.style["textAlign"] = "start";
                    formatting.style["displayAlign"] = "before";
                }
                const B62RegionDefinition& clip = has_paragraph_region
                    ? paragraph_formatting
                    : formatting;
                layout(flow.spans, formatting, clip, true);
            }
            B62MergeCaptionRegionsWithSameClip(caption);
        } else {
            layout(spans, definition, definition, false);
        }
        if (!caption.regions.empty()) {
            B62PresentationNode node;
            node.id = raw.id;
            node.start = caption.pts;
            node.indefinite = options.ignore_document_timing || raw.indefinite;
            if (!node.indefinite) {
                int64_t end = raw.end ? *raw.end + timeline_offset
                                      : (node.start == PTS_NOPTS ? 5000 : node.start + 5000);
                if (node.start != PTS_NOPTS && end <= node.start) {
                    end = node.start + 50;
                }
                node.end = end;
            }
            node.caption = std::move(caption);
            document_nodes.push_back(std::move(node));
        }
    }

    constexpr size_t kMaxPresentationEvents = 300;
    B62Presentation presentation;
    presentation.plane_width = plane[0];
    presentation.plane_height = plane[1];
    presentation.language = language;
    presentation.metadata = std::move(parsed_metadata);
    presentation.nodes = std::move(document_nodes);
    const bool has_presentation_content = !presentation.nodes.empty() ||
        !presentation.metadata.audio_cues.empty() ||
        !presentation.metadata.background_images.empty();

    if (options.operation_mode == B62OperationMode::kLive) {
        if (has_presentation_content) {
            presentation_state.Commit(std::move(presentation));
        } else if (!continuation_applied) {
            return B62DecodeStatus::kNoCaption;
        }
        presentation_state.Prune(options.document_pts);
        presentation_state.BuildScenes(
            options.document_pts, kMaxPresentationEvents, out_result.captions,
            document_metadata);
    } else {
        if (!has_presentation_content) {
            return B62DecodeStatus::kNoCaption;
        }
        B62PresentationState document_state;
        document_state.Commit(std::move(presentation));
        document_state.BuildScenes(
            options.document_pts, kMaxPresentationEvents, out_result.captions,
            document_metadata);
    }

    return out_result.captions.empty() ? B62DecodeStatus::kNoCaption : B62DecodeStatus::kGotCaption;
}

B62DecodeStatus B62DecoderImpl::Decode(const uint8_t* ttml_data,
                                       size_t length,
                                       const B62DecodeOptions& options,
                                       const B62ResourceContextView& resource_context,
                                       B62DecodeResult& out_result) {
    return DecodeWithResourceContext(ttml_data, length, options, resource_context,
                                     false, nullptr, out_result);
}

B62DecodeStatus B62DecoderImpl::DecodeDocument(
    const uint8_t* ttml_data,
    size_t length,
    const B62DecodeOptions& options,
    const B62ResourceContextView& resource_context,
    B62DocumentDecodeResult& out_result) {
    B62DecodeResult legacy_result;
    B62PresentationMetadata document_metadata;
    out_result.captions.clear();
    out_result.sidecar.reset();
    B62DecodeStatus status = DecodeWithResourceContext(
        ttml_data, length, options, resource_context, true,
        &document_metadata, legacy_result);
    out_result.captions = std::move(legacy_result.captions);
    if (status == B62DecodeStatus::kGotCaption) {
        out_result.sidecar = std::shared_ptr<const B62DocumentSidecar>(
            new B62DocumentSidecar(std::move(document_metadata.ruby_associations),
                                   std::move(document_metadata.font_faces),
                                   std::move(document_metadata.audio_cues),
                                   std::move(document_metadata.background_images)));
    }
    return status;
}

B62DecodeStatus B62DecoderImpl::DecodeWithResourceContext(
    const uint8_t* ttml_data,
    size_t length,
    const B62DecodeOptions& options,
    const B62ResourceContextView& resource_context,
    bool preserve_document_layout,
    B62PresentationMetadata* document_metadata,
    B62DecodeResult& out_result) {
    if (options.discontinuity) {
        Reset();
    }
    B62ResourceStore::Snapshot resource_snapshot =
        resource_store_.Capture(resource_context.scope_id);
    const auto restore_resources = [&]() {
        resource_store_.Restore(std::move(resource_snapshot));
    };
#if defined(__cpp_exceptions)
    B62PresentationState previous_presentation_state = preserve_document_layout
        ? document_presentation_state_
        : legacy_presentation_state_;
    try {
#endif
    if (!resource_store_.Store(resource_context)) {
        restore_resources();
        out_result.captions.clear();
        if (document_metadata) {
            *document_metadata = {};
        }
        return B62DecodeStatus::kError;
    }
    B62DecodeStatus status = DecodeInternal(
        ttml_data, length, options, preserve_document_layout, document_metadata, out_result);
    if (resource_context.scope_id == 0) {
        restore_resources();
    } else if (status == B62DecodeStatus::kError) {
        restore_resources();
    } else {
        resource_store_.EnforceLimits(resource_context.scope_id);
    }
    return status;
#if defined(__cpp_exceptions)
    } catch (...) {
        restore_resources();
        if (preserve_document_layout) {
            document_presentation_state_ = std::move(previous_presentation_state);
        } else {
            legacy_presentation_state_ = std::move(previous_presentation_state);
        }
        throw;
    }
#endif
}

}  // namespace aribcaption::internal
