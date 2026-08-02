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
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aribcaption/caption.hpp"
#include "base/tinyxml2.h"
#include "decoder/b62_document_model.hpp"
#include "decoder/b62_layout.hpp"
#include "decoder/b62_resource_resolver.hpp"
#include "decoder/b62_text_util.hpp"
#include "decoder/b62_time.hpp"
#include "decoder/b62_xml.hpp"

namespace aribcaption::internal {
namespace {

struct RawCue {
    const tinyxml2::XMLElement* node = nullptr;
    std::string id;
    std::optional<int64_t> start;
    std::optional<int64_t> end;
    bool indefinite_start = false;
    bool indefinite = false;
};

struct RawAudioCue {
    const tinyxml2::XMLElement* node = nullptr;
    const tinyxml2::XMLElement* owner = nullptr;
    std::optional<int64_t> start;
    std::optional<int64_t> end;
    bool indefinite = false;
};

struct RawBackgroundImage {
    const tinyxml2::XMLElement* owner = nullptr;
    const char* source = nullptr;
    std::optional<int64_t> start;
    std::optional<int64_t> end;
    bool indefinite = false;
};

bool HasARIBRubyAncestor(const tinyxml2::XMLElement* element) {
    for (const tinyxml2::XMLNode* node = element; node; node = node->Parent()) {
        const tinyxml2::XMLElement* current = node->ToElement();
        if (!current) {
            continue;
        }
        if (B62FindARIBAttribute(current, "ruby")) {
            return true;
        }
        if (B62LocalName(current->Name()) == "tt") {
            break;
        }
    }
    return false;
}

void AppendElementText(const tinyxml2::XMLNode* parent, std::string& output) {
    for (const tinyxml2::XMLNode* child = parent ? parent->FirstChild() : nullptr;
         child; child = child->NextSibling()) {
        if (const tinyxml2::XMLText* text = child->ToText()) {
            output += text->Value();
            continue;
        }
        const tinyxml2::XMLElement* element = child->ToElement();
        if (!element) {
            continue;
        }
        if (B62LocalName(element->Name()) == "br") {
            output.push_back('\n');
        } else {
            AppendElementText(element, output);
        }
    }
}

std::string ElementText(const tinyxml2::XMLElement* element) {
    std::string text;
    AppendElementText(element, text);
    return B62NormalizeText(text);
}

B62ElementType ElementType(const tinyxml2::XMLElement* element) {
    if (!element) {
        return B62ElementType::kUnknown;
    }
    std::string_view name = B62LocalName(element->Name());
    if (name == "div") return B62ElementType::kDiv;
    if (name == "p") return B62ElementType::kParagraph;
    if (name == "span") return B62ElementType::kSpan;
    return B62ElementType::kUnknown;
}

void CollectRubyElements(const tinyxml2::XMLElement* parent,
                         std::vector<const tinyxml2::XMLElement*>& elements) {
    if (!parent) {
        return;
    }
    for (const tinyxml2::XMLElement* child = parent->FirstChildElement();
         child; child = child->NextSiblingElement()) {
        if (ElementType(child) != B62ElementType::kUnknown) {
            elements.push_back(child);
        }
        CollectRubyElements(child, elements);
    }
}

void CollectRubyAssociations(const tinyxml2::XMLElement* tt,
                             std::vector<B62RubyAssociation>& associations) {
    std::vector<const tinyxml2::XMLElement*> elements;
    CollectRubyElements(tt, elements);
    std::unordered_map<std::string, const tinyxml2::XMLElement*> targets;
    for (const tinyxml2::XMLElement* element : elements) {
        if (const char* id = B62FindXMLID(element)) {
            targets.emplace(id, element);
        }
    }
    for (const tinyxml2::XMLElement* element : elements) {
        const char* target_id = B62FindARIBAttribute(element, "ruby");
        if (!target_id) {
            continue;
        }
        B62RubyAssociation association;
        association.annotation_type = ElementType(element);
        if (const char* annotation_id = B62FindXMLID(element)) {
            association.annotation_id = annotation_id;
        }
        association.target_id = target_id;
        association.annotation_text = ElementText(element);
        auto target = targets.find(target_id);
        if (target != targets.end()) {
            association.target_type = ElementType(target->second);
            association.target_text = ElementText(target->second);
        }
        associations.push_back(std::move(association));
    }
}

void ApplyStyleAttributes(const tinyxml2::XMLElement* element, B62Style& style) {
    static constexpr std::array<std::string_view, 21> kStyleAttributes = {
        "fontSize",     "lineHeight", "fontWeight",     "fontStyle",   "color",       "backgroundColor",
        "displayAlign", "textAlign",  "textDecoration", "textOutline", "textShadow",  "writingMode", "direction",
        "opacity",      "border",     "border-top",     "border-bottom", "border-left", "border-right",
        "letter-spacing", "text-shadow",
    };
    for (std::string_view name : kStyleAttributes) {
        if (const char* value = B62FindAttribute(element, name)) {
            std::string key(name);
            if (key == "letter-spacing") {
                key = "letterSpacing";
            } else if (key == "text-shadow") {
                key = "textShadow";
            }
            style[key] = value;
        }
    }
}

B62Style ResolveStyleReference(const std::string& id,
                            const std::unordered_map<std::string, const tinyxml2::XMLElement*>& nodes,
                            std::unordered_map<std::string, B62Style>& cache,
                            std::vector<std::string>& resolving);

void ApplyStyleReferences(const tinyxml2::XMLElement* element,
                          const std::unordered_map<std::string, const tinyxml2::XMLElement*>& nodes,
                          std::unordered_map<std::string, B62Style>& cache,
                          std::vector<std::string>& resolving,
                          B62Style& style) {
    const char* refs = B62FindAttribute(element, "style");
    if (!refs) {
        return;
    }
    std::string list(refs);
    size_t position = 0;
    while (position < list.size()) {
        position = list.find_first_not_of(" \t\r\n", position);
        if (position == std::string::npos) {
            break;
        }
        size_t end = list.find_first_of(" \t\r\n", position);
        B62Style referenced = ResolveStyleReference(list.substr(position, end - position), nodes, cache, resolving);
        for (const auto& [key, value] : referenced) {
            style[key] = value;
        }
        position = end == std::string::npos ? list.size() : end;
    }
}

B62Style ResolveStyleReference(const std::string& id,
                            const std::unordered_map<std::string, const tinyxml2::XMLElement*>& nodes,
                            std::unordered_map<std::string, B62Style>& cache,
                            std::vector<std::string>& resolving) {
    if (auto it = cache.find(id); it != cache.end()) {
        return it->second;
    }
    if (std::find(resolving.begin(), resolving.end(), id) != resolving.end()) {
        return {};
    }
    auto node = nodes.find(id);
    if (node == nodes.end()) {
        return {};
    }
    resolving.push_back(id);
    B62Style style;
    ApplyStyleReferences(node->second, nodes, cache, resolving, style);
    ApplyStyleAttributes(node->second, style);
    resolving.pop_back();
    cache[id] = style;
    return style;
}

B62Style MergeNodeStyle(const tinyxml2::XMLElement* element,
                     B62Style base,
                     const std::unordered_map<std::string, const tinyxml2::XMLElement*>& style_nodes,
                     std::unordered_map<std::string, B62Style>& style_cache) {
    std::vector<std::string> resolving;
    ApplyStyleReferences(element, style_nodes, style_cache, resolving, base);
    ApplyStyleAttributes(element, base);
    return base;
}

B62Style CollectInheritedStyle(const tinyxml2::XMLElement* element,
                            const B62Style& region_style,
                            const std::unordered_map<std::string, const tinyxml2::XMLElement*>& style_nodes,
                            std::unordered_map<std::string, B62Style>& style_cache) {
    std::vector<const tinyxml2::XMLElement*> ancestors;
    for (const tinyxml2::XMLNode* node = element; node; node = node->Parent()) {
        const tinyxml2::XMLElement* current = node->ToElement();
        if (!current) {
            continue;
        }
        std::string_view name = B62LocalName(current->Name());
        if (name == "body" || name == "div" || name == "p" || name == "span") {
            ancestors.push_back(current);
        }
        if (name == "tt") {
            break;
        }
    }
    B62Style result = region_style;
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        result = MergeNodeStyle(*it, std::move(result), style_nodes, style_cache);
    }
    return result;
}

void AppendInlineSpans(const tinyxml2::XMLElement* parent,
                       const B62Style& inherited_style,
                       const std::unordered_map<std::string, const tinyxml2::XMLElement*>& style_nodes,
                       std::unordered_map<std::string, B62Style>& style_cache,
                       std::vector<B62InlineSpan>& spans,
                       const std::unordered_map<std::string, B62RegionDefinition>* region_definitions,
                       const B62RegionDefinition* inherited_region,
                       bool inherited_is_ruby) {
    for (const tinyxml2::XMLNode* child = parent->FirstChild(); child; child = child->NextSibling()) {
        if (const tinyxml2::XMLText* text = child->ToText()) {
            std::string normalized = B62NormalizeText(text->Value());
            if (!normalized.empty()) {
                B62InlineSpan span;
                span.text = std::move(normalized);
                span.style = inherited_style;
                span.region = inherited_region;
                span.is_ruby = inherited_is_ruby;
                spans.push_back(std::move(span));
            }
            continue;
        }
        const tinyxml2::XMLElement* element = child->ToElement();
        if (!element) {
            continue;
        }
        std::string_view name = B62LocalName(element->Name());
        if (name == "br") {
            B62InlineSpan span;
            span.text = "\n";
            span.style = inherited_style;
            span.region = inherited_region;
            span.is_ruby = inherited_is_ruby;
            spans.push_back(std::move(span));
            continue;
        }
        if (name != "span") {
            AppendInlineSpans(element, inherited_style, style_nodes, style_cache, spans,
                              region_definitions, inherited_region, inherited_is_ruby);
            continue;
        }

        const B62RegionDefinition* region = inherited_region;
        B62Style region_style = inherited_style;
        bool resets_position = false;
        if (region_definitions) {
            if (const char* region_id = B62FindAttribute(element, "region")) {
                auto resolved = region_definitions->find(region_id);
                if (resolved != region_definitions->end()) {
                    region = &resolved->second;
                    resets_position = true;
                    for (const auto& [key, value] : region->style) {
                        region_style[key] = value;
                    }
                }
            }
        }
        B62Style style = MergeNodeStyle(element, std::move(region_style), style_nodes, style_cache);
        const bool is_ruby = inherited_is_ruby ||
            (region_definitions && B62FindARIBAttribute(element, "ruby") != nullptr);
        size_t begin = spans.size();
        AppendInlineSpans(element, style, style_nodes, style_cache, spans,
                          region_definitions, region, is_ruby);
        if (resets_position && begin < spans.size()) {
            spans[begin].resets_position = true;
        }
        const char* id = B62FindAttribute(element, "id");
        const char* ruby = B62FindAttribute(element, "ruby");
        for (size_t i = begin; i < spans.size(); i++) {
            if (id && spans[i].id.empty()) {
                spans[i].id = id;
            }
            if (ruby && spans[i].ruby_target_id.empty()) {
                spans[i].ruby_target_id = ruby;
            }
        }
    }
}

void ResolveRuby(std::vector<B62InlineSpan>& spans) {
    std::unordered_map<std::string, size_t> by_id;
    for (size_t i = 0; i < spans.size(); i++) {
        if (!spans[i].id.empty() && spans[i].ruby_target_id.empty() && !by_id.count(spans[i].id)) {
            by_id[spans[i].id] = i;
        }
    }
    for (size_t i = 0; i < spans.size(); i++) {
        if (!spans[i].ruby_target_id.empty()) {
            auto target = by_id.find(spans[i].ruby_target_id);
            if (target != by_id.end()) {
                spans[target->second].ruby_text += spans[i].text;
            }
        }
    }
    std::vector<B62InlineSpan> resolved;
    resolved.reserve(spans.size());
    for (size_t i = 0; i < spans.size(); i++) {
        if (!spans[i].ruby_target_id.empty() && by_id.count(spans[i].ruby_target_id)) {
            continue;
        }
        resolved.push_back(std::move(spans[i]));
    }
    spans = std::move(resolved);
}

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

    tinyxml2::XMLDocument document;
    tinyxml2::XMLError error = document.Parse(reinterpret_cast<const char*>(ttml_data), length);
    const tinyxml2::XMLElement* tt = document.RootElement();
    if (error != tinyxml2::XML_SUCCESS || !tt || B62LocalName(tt->Name()) != "tt") {
        log_->e("B62DecoderImpl: invalid TTML document: %s", document.ErrorStr());
        return B62DecodeStatus::kError;
    }
    B62PresentationMetadata parsed_metadata;
    if (preserve_document_layout && document_metadata) {
        CollectRubyAssociations(tt, parsed_metadata.ruby_associations);
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
    if (B62IsEmptyTTMLDocument(tt)) {
        return emit_clear();
    }

    const tinyxml2::XMLElement* body = B62FirstChild(tt, "body");
    if (!body) {
        return B62DecodeStatus::kNoCaption;
    }

    B62ResourceResolver resource_resolver(
        resource_store_, tt, preserve_document_layout && document_metadata);

    if (preserve_document_layout && document_metadata) {
        std::vector<const tinyxml2::XMLElement*> font_face_elements;
        B62CollectDescendants(tt, "font-face", font_face_elements);
        for (const tinyxml2::XMLElement* font_face_element : font_face_elements) {
            if (!B62IsARIBElement(font_face_element, "font-face")) {
                continue;
            }
            const char* family = B62FindAttribute(font_face_element, "font-family");
            if (!family || B62TrimASCII(family).empty()) {
                continue;
            }
            B62FontFace font_face;
            if (const char* id = B62FindXMLID(font_face_element)) {
                font_face.id = id;
            }
            font_face.family = B62TrimASCII(family);
            if (const char* unicode_range = B62FindAttribute(font_face_element, "unicode-range")) {
                font_face.unicode_range = B62TrimASCII(unicode_range);
            }
            for (const tinyxml2::XMLElement* source = font_face_element->FirstChildElement();
                 source; source = source->NextSiblingElement()) {
                if (!B62IsARIBElement(source, "src")) {
                    continue;
                }
                const char* uri = B62FindAttribute(source, "url");
                if (!uri) {
                    continue;
                }
                B62FontSource font_source;
                std::string format = B62TrimASCII(B62FindAttribute(source, "format")
                    ? B62FindAttribute(source, "format") : "");
                std::transform(format.begin(), format.end(), format.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                B62ResourceKind resource_kind = B62ResourceKind::kUnknown;
                if (format == "svg") {
                    font_source.format = B62FontFormat::kSVG;
                    resource_kind = B62ResourceKind::kSVGFont;
                } else if (format == "woff") {
                    font_source.format = B62FontFormat::kWOFF;
                    resource_kind = B62ResourceKind::kWOFFFont;
                }
                font_source.resource = resource_resolver.Resolve(uri, resource_kind);
                font_face.sources.push_back(std::move(font_source));
            }
            if (!font_face.sources.empty()) {
                parsed_metadata.font_faces.push_back(std::move(font_face));
            }
        }
    }

    std::array<int, 2> plane{3840, 2160};
    if (auto extent = B62ParseLengthPair(B62FindAttribute(tt, "extent"), plane)) {
        plane = *extent;
    }

    std::vector<const tinyxml2::XMLElement*> style_elements;
    B62CollectDescendants(tt, "style", style_elements);
    std::unordered_map<std::string, const tinyxml2::XMLElement*> style_nodes;
    for (const tinyxml2::XMLElement* style : style_elements) {
        if (const char* id = B62FindAttribute(style, "id")) {
            style_nodes[id] = style;
        }
    }
    std::unordered_map<std::string, B62Style> style_cache;

    std::vector<const tinyxml2::XMLElement*> region_elements;
    B62CollectDescendants(tt, "region", region_elements);
    std::unordered_map<std::string, B62RegionDefinition> region_definitions;
    for (const tinyxml2::XMLElement* region : region_elements) {
        const char* id = B62FindAttribute(region, "id");
        if (!id) {
            continue;
        }
        B62RegionDefinition definition;
        definition.x = plane[0] / 10;
        definition.y = plane[1] * 78 / 100;
        definition.width = plane[0] * 8 / 10;
        definition.height = plane[1] * 16 / 100;
        if (auto origin = B62ParseLengthPair(B62FindAttribute(region, "origin"), plane)) {
            definition.x = (*origin)[0];
            definition.y = (*origin)[1];
        }
        if (auto extent = B62ParseLengthPair(B62FindAttribute(region, "extent"), plane)) {
            definition.width = (*extent)[0];
            definition.height = (*extent)[1];
        }
        definition.style = MergeNodeStyle(region, {}, style_nodes, style_cache);
        region_definitions[id] = std::move(definition);
    }

    std::vector<const tinyxml2::XMLElement*> paragraphs;
    B62CollectDescendants(body, "p", paragraphs);
    std::vector<RawCue> raw_cues;
    raw_cues.reserve(paragraphs.size());
    std::optional<int64_t> minimum_start;
    for (const tinyxml2::XMLElement* paragraph : paragraphs) {
        RawCue cue;
        cue.node = paragraph;
        if (const char* id = B62FindAttribute(paragraph, "id")) {
            cue.id = id;
        }
        const tinyxml2::XMLElement* timing_node = B62FindNearestTimedNode(paragraph);
        cue.start = B62ParseTime(B62FindAttribute(paragraph, "begin"), &cue.indefinite_start);
        if (!cue.start && !cue.indefinite_start && timing_node) {
            cue.start = B62ParseTime(B62FindAttribute(timing_node, "begin"), &cue.indefinite_start);
        }
        bool indefinite_end = false;
        cue.end = B62ParseTime(B62FindAttribute(paragraph, "end"), &indefinite_end);
        if (!cue.end && !indefinite_end && timing_node) {
            cue.end = B62ParseTime(B62FindAttribute(timing_node, "end"), &indefinite_end);
        }
        bool indefinite_duration = false;
        std::optional<int64_t> duration = B62ParseTime(B62FindAttribute(paragraph, "dur"), &indefinite_duration);
        if (!duration && !indefinite_duration && timing_node) {
            duration = B62ParseTime(B62FindAttribute(timing_node, "dur"), &indefinite_duration);
        }
        cue.indefinite = indefinite_end || indefinite_duration;
        if (!cue.end && duration && cue.start) {
            cue.end = *cue.start + *duration;
        }
        if (cue.start && (!minimum_start || *cue.start < *minimum_start)) {
            minimum_start = cue.start;
        }
        raw_cues.push_back(cue);
    }
    std::vector<const tinyxml2::XMLElement*> audio_elements;
    B62CollectDescendants(body, "audio", audio_elements);
    std::vector<RawAudioCue> raw_audio_cues;
    for (const tinyxml2::XMLElement* audio : audio_elements) {
        if (!B62IsARIBElement(audio, "audio") || !B62FindAttribute(audio, "src")) {
            continue;
        }
        RawAudioCue cue;
        cue.node = audio;
        for (const tinyxml2::XMLNode* node = audio->Parent(); node; node = node->Parent()) {
            const tinyxml2::XMLElement* element = node->ToElement();
            if (!element) {
                continue;
            }
            std::string_view name = B62LocalName(element->Name());
            if (name == "p" || name == "div") {
                cue.owner = element;
                break;
            }
        }
        const tinyxml2::XMLElement* timing_node = B62FindNearestTimedNode(audio);
        cue.start = B62ParseTime(B62FindAttribute(timing_node, "begin"));
        bool indefinite_end = false;
        cue.end = B62ParseTime(B62FindAttribute(timing_node, "end"), &indefinite_end);
        bool indefinite_duration = false;
        std::optional<int64_t> duration = B62ParseTime(
            B62FindAttribute(timing_node, "dur"), &indefinite_duration);
        cue.indefinite = indefinite_end || indefinite_duration;
        if (!cue.end && duration && cue.start) {
            cue.end = *cue.start + *duration;
        }
        if (cue.start && (!minimum_start || *cue.start < *minimum_start)) {
            minimum_start = cue.start;
        }
        raw_audio_cues.push_back(std::move(cue));
    }
    std::vector<const tinyxml2::XMLElement*> background_owners;
    B62CollectDescendants(body, "div", background_owners);
    background_owners.insert(background_owners.end(), paragraphs.begin(), paragraphs.end());
    std::vector<RawBackgroundImage> raw_background_images;
    for (const tinyxml2::XMLElement* owner : background_owners) {
        const char* source = B62FindNamespacedAttribute(
            owner, "backgroundImage",
            "http://www.smpte-ra.org/schemas/2052-1/2013/smpte-tt");
        if (!source) {
            continue;
        }
        RawBackgroundImage image;
        image.owner = owner;
        image.source = source;
        const tinyxml2::XMLElement* timing_node = B62FindNearestTimedNode(owner);
        image.start = B62ParseTime(B62FindAttribute(timing_node, "begin"));
        bool indefinite_end = false;
        image.end = B62ParseTime(B62FindAttribute(timing_node, "end"), &indefinite_end);
        bool indefinite_duration = false;
        std::optional<int64_t> duration = B62ParseTime(
            B62FindAttribute(timing_node, "dur"), &indefinite_duration);
        image.indefinite = indefinite_end || indefinite_duration;
        if (!image.end && duration && image.start) {
            image.end = *image.start + *duration;
        }
        if (image.start && (!minimum_start || *image.start < *minimum_start)) {
            minimum_start = image.start;
        }
        raw_background_images.push_back(std::move(image));
    }
    if (raw_cues.empty() && raw_audio_cues.empty() && raw_background_images.empty()) {
        return B62DecodeStatus::kNoCaption;
    }

    int64_t timeline_offset = 0;
    if (options.time_base_pts != PTS_NOPTS) {
        timeline_offset = options.time_base_pts;
    } else if (options.align_earliest_to_document_pts &&
               options.document_pts != PTS_NOPTS && minimum_start) {
        timeline_offset = options.document_pts - *minimum_start;
    }
    uint32_t language = ParseLanguage(B62FindAttribute(tt, "lang"));

    if (preserve_document_layout && document_metadata) {
        for (const RawAudioCue& raw_audio : raw_audio_cues) {
            B62AudioCue audio;
            audio.owner_type = ElementType(raw_audio.owner);
            if (const char* owner_id = B62FindXMLID(raw_audio.owner)) {
                audio.owner_id = owner_id;
            }
            if (const char* id = B62FindXMLID(raw_audio.node)) {
                audio.id = id;
            }
            audio.source = resource_resolver.Resolve(B62FindAttribute(raw_audio.node, "src"),
                                             B62ResourceKind::kAudio);
            const char* loop = B62FindAttribute(raw_audio.node, "loop");
            audio.loop = loop && (std::strcmp(loop, "true") == 0 || std::strcmp(loop, "1") == 0);
            audio.begin_pts = raw_audio.start
                ? *raw_audio.start + timeline_offset
                : options.document_pts;
            if (raw_audio.end) {
                audio.end_pts = *raw_audio.end + timeline_offset;
            } else if (!raw_audio.indefinite && audio.begin_pts != PTS_NOPTS) {
                audio.end_pts = audio.begin_pts + 5000;
            }
            parsed_metadata.audio_cues.push_back(std::move(audio));
        }
        for (const RawBackgroundImage& raw_image : raw_background_images) {
            B62BackgroundImage image;
            image.owner_type = ElementType(raw_image.owner);
            image.layout_box.width = plane[0];
            image.layout_box.height = plane[1];
            if (const char* region_id = B62FindNearestAttribute(raw_image.owner, "region")) {
                auto region = region_definitions.find(region_id);
                if (region != region_definitions.end()) {
                    image.layout_box.x = region->second.x;
                    image.layout_box.y = region->second.y;
                    image.layout_box.width = region->second.width;
                    image.layout_box.height = region->second.height;
                }
            }
            if (const char* owner_id = B62FindXMLID(raw_image.owner)) {
                image.owner_id = owner_id;
            }
            image.source = resource_resolver.Resolve(raw_image.source, B62ResourceKind::kUnknown);
            image.begin_pts = raw_image.start
                ? *raw_image.start + timeline_offset
                : options.document_pts;
            if (raw_image.end) {
                image.end_pts = *raw_image.end + timeline_offset;
            } else if (!raw_image.indefinite && image.begin_pts != PTS_NOPTS) {
                image.end_pts = image.begin_pts + 5000;
            }
            parsed_metadata.background_images.push_back(std::move(image));
        }
    }

    if (options.operation_mode != B62OperationMode::kLive || options.discontinuity) {
        presentation_state.Reset();
    }

    bool continuation_applied = false;
    if (options.operation_mode == B62OperationMode::kLive) {
        for (const RawCue& raw : raw_cues) {
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

    for (const RawCue& raw : raw_cues) {
        if (raw.indefinite_start) {
            continue;
        }
        B62RegionDefinition definition;
        definition.x = plane[0] / 10;
        definition.y = plane[1] * 78 / 100;
        definition.width = plane[0] * 8 / 10;
        definition.height = plane[1] * 16 / 100;
        bool has_paragraph_region = false;
        const char* region_id = B62FindNearestAttribute(raw.node, "region");
        if (region_id) {
            if (auto it = region_definitions.find(region_id); it != region_definitions.end()) {
                definition = it->second;
                has_paragraph_region = true;
            }
        }
        B62Style inherited = CollectInheritedStyle(raw.node, definition.style, style_nodes, style_cache);
        definition.style = inherited;
        B62RegionDefinition paragraph_formatting = preserve_document_layout
            ? B62MakeFormattingDefinition(definition, inherited)
            : definition;
        std::vector<B62InlineSpan> spans;
        AppendInlineSpans(raw.node, inherited, style_nodes, style_cache, spans,
                          preserve_document_layout ? &region_definitions : nullptr,
                          preserve_document_layout ? &definition : nullptr,
                          preserve_document_layout && HasARIBRubyAncestor(raw.node));
        if (!preserve_document_layout) {
            ResolveRuby(spans);
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
