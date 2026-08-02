/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_style_context.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#include "decoder/b62_layout.hpp"
#include "decoder/b62_text_util.hpp"
#include "decoder/b62_xml.hpp"

namespace aribcaption::internal {

B62StyleContext::B62StyleContext(const tinyxml2::XMLElement* document_root,
                                 const std::array<int, 2>& plane) {
    std::vector<const tinyxml2::XMLElement*> style_elements;
    B62CollectDescendants(document_root, "style", style_elements);
    for (const tinyxml2::XMLElement* style : style_elements) {
        if (const char* id = B62FindAttribute(style, "id")) {
            style_nodes_[id] = style;
        }
    }

    std::vector<const tinyxml2::XMLElement*> region_elements;
    B62CollectDescendants(document_root, "region", region_elements);
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
        definition.style = MergeNodeStyle(region, {});
        region_definitions_[id] = std::move(definition);
    }
}

bool B62StyleContext::HasARIBRubyAncestor(
    const tinyxml2::XMLElement* element) {
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

void B62StyleContext::ApplyStyleAttributes(
    const tinyxml2::XMLElement* element,
    B62Style& style) const {
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

void B62StyleContext::ApplyStyleReferences(
    const tinyxml2::XMLElement* element,
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
        B62Style referenced = ResolveStyleReference(
            list.substr(position, end - position), resolving);
        for (const auto& [key, value] : referenced) {
            style[key] = value;
        }
        position = end == std::string::npos ? list.size() : end;
    }
}

B62Style B62StyleContext::ResolveStyleReference(
    const std::string& id,
    std::vector<std::string>& resolving) {
    if (auto it = style_cache_.find(id); it != style_cache_.end()) {
        return it->second;
    }
    if (std::find(resolving.begin(), resolving.end(), id) != resolving.end()) {
        return {};
    }
    auto node = style_nodes_.find(id);
    if (node == style_nodes_.end()) {
        return {};
    }
    resolving.push_back(id);
    B62Style style;
    ApplyStyleReferences(node->second, resolving, style);
    ApplyStyleAttributes(node->second, style);
    resolving.pop_back();
    style_cache_[id] = style;
    return style;
}

B62Style B62StyleContext::MergeNodeStyle(
    const tinyxml2::XMLElement* element,
    B62Style base) {
    std::vector<std::string> resolving;
    ApplyStyleReferences(element, resolving, base);
    ApplyStyleAttributes(element, base);
    return base;
}

B62Style B62StyleContext::CollectInheritedStyle(
    const tinyxml2::XMLElement* element,
    const B62Style& region_style) {
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
        result = MergeNodeStyle(*it, std::move(result));
    }
    return result;
}

void B62StyleContext::AppendInlineSpans(
    const tinyxml2::XMLElement* parent,
    const B62Style& inherited_style,
    std::vector<B62InlineSpan>& spans,
    const B62RegionDefinition* inherited_region,
    bool preserve_document_layout,
    bool inherited_is_ruby) {
    for (const tinyxml2::XMLNode* child = parent->FirstChild(); child;
         child = child->NextSibling()) {
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
            AppendInlineSpans(element, inherited_style, spans, inherited_region,
                              preserve_document_layout, inherited_is_ruby);
            continue;
        }

        const B62RegionDefinition* region = inherited_region;
        B62Style region_style = inherited_style;
        bool resets_position = false;
        if (preserve_document_layout) {
            if (const char* region_id = B62FindAttribute(element, "region")) {
                auto resolved = region_definitions_.find(region_id);
                if (resolved != region_definitions_.end()) {
                    region = &resolved->second;
                    resets_position = true;
                    for (const auto& [key, value] : region->style) {
                        region_style[key] = value;
                    }
                }
            }
        }
        B62Style style = MergeNodeStyle(element, std::move(region_style));
        const bool is_ruby = inherited_is_ruby ||
            (preserve_document_layout &&
             B62FindARIBAttribute(element, "ruby") != nullptr);
        size_t begin = spans.size();
        AppendInlineSpans(element, style, spans, region,
                          preserve_document_layout, is_ruby);
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

void B62StyleContext::ResolveLegacyRuby(
    std::vector<B62InlineSpan>& spans) {
    std::unordered_map<std::string, size_t> by_id;
    for (size_t i = 0; i < spans.size(); i++) {
        if (!spans[i].id.empty() && spans[i].ruby_target_id.empty() &&
            !by_id.count(spans[i].id)) {
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
        if (!spans[i].ruby_target_id.empty() &&
            by_id.count(spans[i].ruby_target_id)) {
            continue;
        }
        resolved.push_back(std::move(spans[i]));
    }
    spans = std::move(resolved);
}

}  // namespace aribcaption::internal
