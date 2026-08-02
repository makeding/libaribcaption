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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aribcaption/caption.hpp"
#include "base/tinyxml2.h"
#include "base/unicode_helper.hpp"
#include "base/utf_helper.hpp"
#include "decoder/b62_xml.hpp"

namespace aribcaption::internal {
namespace {

constexpr size_t kMaxEmbeddedResourceBytes = 16u * 1024u * 1024u;

using Style = std::unordered_map<std::string, std::string>;

struct RegionDefinition {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    Style style;
};

struct InlineSpan {
    std::string text;
    Style style;
    std::string id;
    std::string ruby_target_id;
    std::string ruby_text;
    const RegionDefinition* region = nullptr;
    bool resets_position = false;
    bool is_ruby = false;
};

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

struct CharacterPlacement {
    uint32_t codepoint = 0;
    const InlineSpan* span = nullptr;
    int advance = 0;
};

struct FontSize {
    int width = 0;
    int height = 0;
};

struct BorderPaint {
    bool visible = false;
    bool solid_compatible = false;
    int thickness = 0;
    ColorRGBA color;
};

struct BoundingBox {
    int left = std::numeric_limits<int>::max();
    int top = std::numeric_limits<int>::max();
    int right = std::numeric_limits<int>::min();
    int bottom = std::numeric_limits<int>::min();

    void Include(int x, int y, int width, int height) {
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x + width);
        bottom = std::max(bottom, y + height);
    }

    [[nodiscard]] bool IsValid() const { return left < right && top < bottom; }
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

std::optional<uint32_t> ParseSubtResourceIndex(std::string_view uri) {
    constexpr std::string_view kPrefix = "subt://";
    if (uri.substr(0, kPrefix.size()) != kPrefix || uri.size() == kPrefix.size()) {
        return std::nullopt;
    }
    uint64_t value = 0;
    for (char ch : uri.substr(kPrefix.size())) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        value = value * 10 + static_cast<uint64_t>(ch - '0');
        if (value > std::numeric_limits<uint32_t>::max()) {
            return std::nullopt;
        }
    }
    return static_cast<uint32_t>(value);
}

std::optional<std::vector<uint8_t>> DecodeBase64(std::string_view encoded) {
    std::vector<uint8_t> decoded;
    decoded.reserve(std::min(kMaxEmbeddedResourceBytes, encoded.size() * 3 / 4));
    uint32_t accumulator = 0;
    int bits = 0;
    bool padding = false;
    for (unsigned char ch : encoded) {
        if (std::isspace(ch)) {
            continue;
        }
        if (ch == '=') {
            padding = true;
            continue;
        }
        if (padding) {
            return std::nullopt;
        }
        int value = -1;
        if (ch >= 'A' && ch <= 'Z') value = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') value = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') value = ch - '0' + 52;
        else if (ch == '+') value = 62;
        else if (ch == '/') value = 63;
        if (value < 0) {
            return std::nullopt;
        }
        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (decoded.size() >= kMaxEmbeddedResourceBytes) {
                return std::nullopt;
            }
            decoded.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xff));
        }
    }
    return decoded;
}

std::string TrimASCII(std::string value) {
    auto is_space = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
    };
    auto begin = std::find_if_not(value.begin(), value.end(), is_space);
    auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
    return begin < end ? std::string(begin, end) : std::string();
}

bool IsEmptyTTMLDocument(const tinyxml2::XMLElement* tt) {
    if (!tt) {
        return false;
    }
    for (const tinyxml2::XMLNode* child = tt->FirstChild(); child; child = child->NextSibling()) {
        if (child->ToElement()) {
            return false;
        }
        if (const tinyxml2::XMLText* text = child->ToText()) {
            if (!TrimASCII(text->Value()).empty()) {
                return false;
            }
        }
    }
    return true;
}

std::string NormalizeText(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    bool pending_space = false;
    bool pending_newline = false;
    for (size_t i = 0; i < input.size(); i++) {
        char ch = input[i];
        if (ch == '\r') {
            if (i + 1 < input.size() && input[i + 1] == '\n') {
                i++;
            }
            ch = '\n';
        }
        if (ch == '\n') {
            while (!output.empty() && (output.back() == ' ' || output.back() == '\t')) {
                output.pop_back();
            }
            pending_space = false;
            pending_newline = true;
            continue;
        }
        if (ch == ' ' || ch == '\f' || ch == '\v') {
            pending_space = true;
            continue;
        }
        if (ch == '\t') {
            continue;
        }
        if (pending_newline) {
            output.push_back('\n');
        } else if (pending_space) {
            output.push_back(' ');
        }
        pending_newline = false;
        pending_space = false;
        output.push_back(ch);
    }
    return TrimASCII(std::move(output));
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
    return NormalizeText(text);
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

std::optional<double> ParseLength(std::string_view value, double base) {
    std::string text = TrimASCII(std::string(value));
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    double number = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(number)) {
        return std::nullopt;
    }
    std::string_view suffix(end);
    if (suffix == "%") {
        return number * base / 100.0;
    }
    if (suffix.empty() || suffix == "px") {
        return number;
    }
    return std::nullopt;
}

std::optional<std::array<int, 2>> ParseLengthPair(const char* value, const std::array<int, 2>& plane) {
    if (!value) {
        return std::nullopt;
    }
    std::string text = TrimASCII(value);
    size_t separator = text.find_first_of(" \t\r\n");
    std::string first = text.substr(0, separator);
    std::string second;
    if (separator != std::string::npos) {
        size_t second_begin = text.find_first_not_of(" \t\r\n", separator);
        if (second_begin != std::string::npos) {
            second = text.substr(second_begin);
        }
    }
    auto x = ParseLength(first, plane[0]);
    auto y = ParseLength(second.empty() ? first : second, plane[1]);
    if (!x || !y) {
        return std::nullopt;
    }
    return std::array<int, 2>{static_cast<int>(std::lround(*x)), static_cast<int>(std::lround(*y))};
}

std::optional<int64_t> ParseTime(const char* value, bool* indefinite = nullptr) {
    if (indefinite) {
        *indefinite = false;
    }
    if (!value) {
        return std::nullopt;
    }
    std::string text = TrimASCII(value);
    if (text == "indefinite") {
        if (indefinite) {
            *indefinite = true;
        }
        return std::nullopt;
    }
    if (text.size() > 2 && text.compare(text.size() - 2, 2, "ms") == 0) {
        char* end = nullptr;
        double millis = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || std::string_view(end) != "ms" || !std::isfinite(millis)) {
            return std::nullopt;
        }
        return static_cast<int64_t>(std::llround(millis));
    }
    if (!text.empty() && text.back() == 's') {
        char* end = nullptr;
        double seconds = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || std::string_view(end) != "s" || !std::isfinite(seconds)) {
            return std::nullopt;
        }
        return static_cast<int64_t>(std::llround(seconds * 1000.0));
    }

    int hours = 0;
    int minutes = 0;
    double seconds = 0.0;
    char trailing = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%lf%c", &hours, &minutes, &seconds, &trailing) != 3 || hours < 0 ||
        minutes < 0 || minutes > 59 || seconds < 0 || seconds >= 60) {
        return std::nullopt;
    }
    return static_cast<int64_t>(std::llround((hours * 3600.0 + minutes * 60.0 + seconds) * 1000.0));
}

std::optional<ColorRGBA> ParseColor(std::string value) {
    value = TrimASCII(std::move(value));
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    static const std::unordered_map<std::string, ColorRGBA> named = {
        {"black", ColorRGBA(0, 0, 0)},    {"white", ColorRGBA(255, 255, 255)}, {"red", ColorRGBA(255, 0, 0)},
        {"green", ColorRGBA(0, 255, 0)},  {"blue", ColorRGBA(0, 0, 255)},      {"yellow", ColorRGBA(255, 255, 0)},
        {"cyan", ColorRGBA(0, 255, 255)}, {"magenta", ColorRGBA(255, 0, 255)}, {"transparent", ColorRGBA(0, 0, 0, 0)},
    };
    if (auto it = named.find(value); it != named.end()) {
        return it->second;
    }
    if (value.size() != 7 && value.size() != 9) {
        return std::nullopt;
    }
    if (value[0] != '#') {
        return std::nullopt;
    }
    char* end = nullptr;
    unsigned long rgba = std::strtoul(value.c_str() + 1, &end, 16);
    if (end != value.c_str() + value.size()) {
        return std::nullopt;
    }
    if (value.size() == 7) {
        return ColorRGBA(static_cast<uint8_t>((rgba >> 16) & 0xff), static_cast<uint8_t>((rgba >> 8) & 0xff),
                         static_cast<uint8_t>(rgba & 0xff));
    }
    return ColorRGBA(static_cast<uint8_t>((rgba >> 24) & 0xff), static_cast<uint8_t>((rgba >> 16) & 0xff),
                     static_cast<uint8_t>((rgba >> 8) & 0xff), static_cast<uint8_t>(rgba & 0xff));
}

void ApplyStyleAttributes(const tinyxml2::XMLElement* element, Style& style) {
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

Style ResolveStyleReference(const std::string& id,
                            const std::unordered_map<std::string, const tinyxml2::XMLElement*>& nodes,
                            std::unordered_map<std::string, Style>& cache,
                            std::vector<std::string>& resolving);

void ApplyStyleReferences(const tinyxml2::XMLElement* element,
                          const std::unordered_map<std::string, const tinyxml2::XMLElement*>& nodes,
                          std::unordered_map<std::string, Style>& cache,
                          std::vector<std::string>& resolving,
                          Style& style) {
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
        Style referenced = ResolveStyleReference(list.substr(position, end - position), nodes, cache, resolving);
        for (const auto& [key, value] : referenced) {
            style[key] = value;
        }
        position = end == std::string::npos ? list.size() : end;
    }
}

Style ResolveStyleReference(const std::string& id,
                            const std::unordered_map<std::string, const tinyxml2::XMLElement*>& nodes,
                            std::unordered_map<std::string, Style>& cache,
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
    Style style;
    ApplyStyleReferences(node->second, nodes, cache, resolving, style);
    ApplyStyleAttributes(node->second, style);
    resolving.pop_back();
    cache[id] = style;
    return style;
}

Style MergeNodeStyle(const tinyxml2::XMLElement* element,
                     Style base,
                     const std::unordered_map<std::string, const tinyxml2::XMLElement*>& style_nodes,
                     std::unordered_map<std::string, Style>& style_cache) {
    std::vector<std::string> resolving;
    ApplyStyleReferences(element, style_nodes, style_cache, resolving, base);
    ApplyStyleAttributes(element, base);
    return base;
}

Style CollectInheritedStyle(const tinyxml2::XMLElement* element,
                            const Style& region_style,
                            const std::unordered_map<std::string, const tinyxml2::XMLElement*>& style_nodes,
                            std::unordered_map<std::string, Style>& style_cache) {
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
    Style result = region_style;
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        result = MergeNodeStyle(*it, std::move(result), style_nodes, style_cache);
    }
    return result;
}

void AppendInlineSpans(const tinyxml2::XMLElement* parent,
                       const Style& inherited_style,
                       const std::unordered_map<std::string, const tinyxml2::XMLElement*>& style_nodes,
                       std::unordered_map<std::string, Style>& style_cache,
                       std::vector<InlineSpan>& spans,
                       const std::unordered_map<std::string, RegionDefinition>* region_definitions,
                       const RegionDefinition* inherited_region,
                       bool inherited_is_ruby) {
    for (const tinyxml2::XMLNode* child = parent->FirstChild(); child; child = child->NextSibling()) {
        if (const tinyxml2::XMLText* text = child->ToText()) {
            std::string normalized = NormalizeText(text->Value());
            if (!normalized.empty()) {
                InlineSpan span;
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
            InlineSpan span;
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

        const RegionDefinition* region = inherited_region;
        Style region_style = inherited_style;
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
        Style style = MergeNodeStyle(element, std::move(region_style), style_nodes, style_cache);
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

void ResolveRuby(std::vector<InlineSpan>& spans) {
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
    std::vector<InlineSpan> resolved;
    resolved.reserve(spans.size());
    for (size_t i = 0; i < spans.size(); i++) {
        if (!spans[i].ruby_target_id.empty() && by_id.count(spans[i].ruby_target_id)) {
            continue;
        }
        resolved.push_back(std::move(spans[i]));
    }
    spans = std::move(resolved);
}

int StyleLength(const Style& style, const char* key, int base, int fallback) {
    auto it = style.find(key);
    if (it == style.end()) {
        return fallback;
    }
    auto value = ParseLength(it->second, base);
    return value ? std::max(1, static_cast<int>(std::lround(*value))) : fallback;
}

FontSize StyleFontSize(const Style& style,
                       const std::array<int, 2>& plane,
                       FontSize fallback) {
    auto it = style.find("fontSize");
    if (it == style.end()) {
        return fallback;
    }
    auto value = ParseLengthPair(it->second.c_str(), plane);
    if (!value) {
        return fallback;
    }
    return {
        std::max(1, (*value)[0]),
        std::max(1, (*value)[1]),
    };
}

std::string StyleValue(const Style& style, const char* key, const char* fallback = "") {
    auto it = style.find(key);
    return it == style.end() ? fallback : it->second;
}

ColorRGBA StyleColor(const Style& style, const char* key, ColorRGBA fallback) {
    auto it = style.find(key);
    if (it == style.end()) {
        return fallback;
    }
    auto color = ParseColor(it->second);
    return color.value_or(fallback);
}

ColorRGBA StrokeColor(const Style& style) {
    auto outline = style.find("textOutline");
    if (outline != style.end()) {
        size_t first_space = outline->second.find_first_of(" \t");
        std::string value = outline->second.substr(0, first_space);
        if (auto color = ParseColor(value)) {
            return *color;
        }
    }
    auto shadow = style.find("textShadow");
    if (shadow != style.end()) {
        size_t last_space = shadow->second.find_last_of(" \t");
        std::string value = last_space == std::string::npos ? shadow->second : shadow->second.substr(last_space + 1);
        if (auto color = ParseColor(value)) {
            return *color;
        }
    }
    return ColorRGBA(0, 0, 0);
}

CharStyle MakeCharStyle(const Style& style) {
    unsigned flags = kCharStyleDefault;
    if (StyleValue(style, "fontWeight") == "bold") {
        flags |= kCharStyleBold;
    }
    if (StyleValue(style, "fontStyle") == "italic") {
        flags |= kCharStyleItalic;
    }
    if (StyleValue(style, "textDecoration").find("underline") != std::string::npos) {
        flags |= kCharStyleUnderline;
    }
    if (style.count("textOutline") || style.count("textShadow")) {
        flags |= kCharStyleStroke;
    }
    return static_cast<CharStyle>(flags);
}

BorderPaint ParseBorder(std::string value) {
    value = TrimASCII(std::move(value));
    std::vector<std::string> tokens;
    size_t position = 0;
    while (position < value.size()) {
        position = value.find_first_not_of(" \t", position);
        if (position == std::string::npos) {
            break;
        }
        size_t end = value.find_first_of(" \t", position);
        tokens.push_back(value.substr(position, end - position));
        position = end == std::string::npos ? value.size() : end;
    }
    BorderPaint paint;
    if (tokens.empty() ||
        std::find(tokens.begin(), tokens.end(), "none") != tokens.end() ||
        std::find(tokens.begin(), tokens.end(), "hidden") != tokens.end()) {
        return paint;
    }
    paint.visible = true;
    paint.solid_compatible = std::find(tokens.begin(), tokens.end(), "solid") != tokens.end();
    for (const std::string& token : tokens) {
        if (auto color = ParseColor(token)) {
            paint.color = *color;
            continue;
        }
        if (auto width = ParseLength(token, 1.0)) {
            paint.thickness = std::max(1, static_cast<int>(std::lround(*width)));
        }
    }
    return paint;
}

EnclosureStyle MakeEnclosureStyle(const Style& style) {
    unsigned flags = kEnclosureStyleDefault;
    auto border = style.find("border");
    if (border != style.end() && ParseBorder(border->second).visible) {
        flags = kEnclosureStyleTop | kEnclosureStyleBottom | kEnclosureStyleLeft | kEnclosureStyleRight;
    }
    static constexpr std::array<std::pair<const char*, EnclosureStyle>, 4> kSides = {{
        {"border-top", kEnclosureStyleTop},
        {"border-bottom", kEnclosureStyleBottom},
        {"border-left", kEnclosureStyleLeft},
        {"border-right", kEnclosureStyleRight},
    }};
    for (const auto& [key, side] : kSides) {
        auto it = style.find(key);
        if (it == style.end()) {
            continue;
        }
        if (ParseBorder(it->second).visible) {
            flags |= side;
        } else {
            flags &= ~static_cast<unsigned>(side);
        }
    }
    return static_cast<EnclosureStyle>(flags);
}

BorderPaint MakeEnclosurePaint(const Style& style) {
    static constexpr std::array<const char*, 5> kBorderAttributes = {
        "border", "border-top", "border-bottom", "border-left", "border-right",
    };
    for (const char* key : kBorderAttributes) {
        auto it = style.find(key);
        if (it == style.end()) {
            continue;
        }
        BorderPaint paint = ParseBorder(it->second);
        if (paint.visible) {
            return paint;
        }
    }
    return {};
}

CaptionChar MakeCaptionChar(uint32_t codepoint,
                            const Style& style,
                            int x,
                            int y,
                            int font_width,
                            int font_height,
                            int line_height,
                            int letter_spacing,
                            bool halfwidth) {
    CaptionChar character;
    character.codepoint = codepoint;
    character.x = x;
    character.y = y;
    character.char_width = font_width;
    character.char_height = font_height;
    character.char_horizontal_spacing = halfwidth ? letter_spacing * 2 : letter_spacing;
    character.char_vertical_spacing = std::max(0, line_height - font_height);
    character.char_horizontal_scale = halfwidth ? 0.5f : 1.0f;
    character.char_vertical_scale = 1.0f;
    character.text_color = StyleColor(style, "color", ColorRGBA(255, 255, 255));
    character.back_color = StyleColor(style, "backgroundColor", ColorRGBA(0, 0, 0, 0));
    character.stroke_color = StrokeColor(style);
    character.style = MakeCharStyle(style);
    character.enclosure_style = MakeEnclosureStyle(style);
    BorderPaint enclosure = MakeEnclosurePaint(style);
    if (enclosure.visible) {
        character.enclosure_color = enclosure.color;
        character.style = static_cast<CharStyle>(character.style | kCharStyleColoredEnclosure);
        if (enclosure.solid_compatible) {
            character.enclosure_thickness = enclosure.thickness;
        }
    }
    utf::UTF8AppendCodePoint(character.u8str, codepoint);
    return character;
}

std::vector<uint32_t> DecodeUTF8(std::string_view text) {
    std::vector<uint32_t> result;
    size_t offset = 0;
    while (offset < text.size()) {
        size_t processed = 0;
        uint32_t codepoint = utf::DecodeUTF8ToCodePoint(reinterpret_cast<const uint8_t*>(text.data() + offset),
                                                        text.size() - offset, &processed);
        if (processed == 0) {
            break;
        }
        result.push_back(codepoint);
        offset += processed;
    }
    return result;
}

void ExpandRegionToFitCharacters(CaptionRegion& region) {
    if (region.chars.empty()) {
        return;
    }
    int left = region.x;
    int top = region.y;
    int right = region.x + region.width;
    int bottom = region.y + region.height;
    for (const CaptionChar& character : region.chars) {
        left = std::min(left, character.x);
        top = std::min(top, character.y);
        right = std::max(right, character.x + character.section_width());
        bottom = std::max(bottom, character.y + character.section_height());
    }
    region.x = left;
    region.y = top;
    region.width = right - left;
    region.height = bottom - top;
}

void AppendRubyRegion(const InlineSpan& span,
                      const BoundingBox& base,
                      const std::array<int, 2>& plane,
                      std::vector<CaptionRegion>& regions) {
    if (span.ruby_text.empty() || !base.IsValid()) {
        return;
    }
    std::vector<uint32_t> codepoints = DecodeUTF8(span.ruby_text);
    if (codepoints.empty()) {
        return;
    }
    FontSize base_font_size = StyleFontSize(span.style, plane, {72, 72});
    FontSize font_size = {
        std::max(1, base_font_size.width / 2),
        std::max(1, base_font_size.height / 2),
    };
    int width = base.right - base.left;
    int advance = std::max(1, width / static_cast<int>(codepoints.size()));
    int y = std::max(0, base.top - font_size.height);
    CaptionRegion ruby;
    ruby.x = base.left;
    ruby.y = y;
    ruby.width = width;
    ruby.height = font_size.height;
    ruby.is_ruby = true;
    int x = base.left;
    for (uint32_t codepoint : codepoints) {
        CaptionChar character = MakeCaptionChar(
            codepoint, span.style, x, y, font_size.width, font_size.height,
            font_size.height, 0, false);
        character.char_width = advance;
        ruby.chars.push_back(std::move(character));
        x += advance;
    }
    regions.push_back(std::move(ruby));
}

void LayoutHorizontal(const std::vector<InlineSpan>& spans,
                      const RegionDefinition& definition,
                      const RegionDefinition& clip_definition,
                      const std::array<int, 2>& plane,
                      Caption& caption,
                      bool preserve_region_bounds) {
    std::vector<std::vector<CharacterPlacement>> lines(1);
    FontSize default_font_size = StyleFontSize(definition.style, plane, {72, 72});
    int line_height = StyleLength(definition.style, "lineHeight", plane[1],
                                  std::max(default_font_size.height, default_font_size.height * 5 / 4));

    for (const InlineSpan& span : spans) {
        FontSize font_size = StyleFontSize(span.style, plane, default_font_size);
        int letter_spacing = StyleLength(span.style, "letterSpacing", definition.width, 0);
        line_height = std::max(
            line_height,
            StyleLength(span.style, "lineHeight", plane[1],
                        std::max(font_size.height, font_size.height * 5 / 4)));
        for (uint32_t codepoint : DecodeUTF8(span.text)) {
            if (codepoint == '\n') {
                lines.emplace_back();
                continue;
            }
            bool halfwidth = unicode::IsHalfwidthCharacter(codepoint);
            int advance = std::max(1, (halfwidth ? font_size.width / 2 : font_size.width) + letter_spacing);
            lines.back().push_back({codepoint, &span, advance});
        }
    }

    int content_height = static_cast<int>(lines.size()) * line_height;
    std::string display_align = StyleValue(definition.style, "displayAlign", "before");
    int y = definition.y;
    if (display_align == "center") {
        y += std::max(0, (definition.height - content_height) / 2);
    } else if (display_align == "after") {
        y += std::max(0, definition.height - content_height);
    }

    CaptionRegion regions[2];
    for (size_t i = 0; i < 2; ++i) {
        regions[i].x = clip_definition.x;
        regions[i].y = clip_definition.y;
        regions[i].width = clip_definition.width;
        regions[i].height = clip_definition.height;
        regions[i].is_ruby = i == 1;
    }
    std::unordered_map<const InlineSpan*, BoundingBox> span_bounds;
    std::string text_align = StyleValue(definition.style, "textAlign", "center");

    for (const auto& line : lines) {
        int line_width = 0;
        for (const CharacterPlacement& placement : line) {
            line_width += placement.advance;
        }
        int x = definition.x;
        if (text_align == "center") {
            x += (definition.width - line_width) / 2;
        } else if (text_align == "right" || text_align == "end") {
            x += definition.width - line_width;
        }
        for (const CharacterPlacement& placement : line) {
            const Style& style = placement.span->style;
            FontSize font_size = StyleFontSize(style, plane, default_font_size);
            int letter_spacing = StyleLength(style, "letterSpacing", definition.width, 0);
            bool halfwidth = unicode::IsHalfwidthCharacter(placement.codepoint);
            CaptionChar character = MakeCaptionChar(placement.codepoint, style, x, y, font_size.width,
                                                     font_size.height, line_height, letter_spacing, halfwidth);
            int glyph_x = x + character.char_horizontal_spacing / 2;
            int glyph_y = y + character.char_vertical_spacing / 2;
            span_bounds[placement.span].Include(glyph_x, glyph_y, placement.advance, font_size.height);
            regions[placement.span->is_ruby ? 1 : 0].chars.push_back(std::move(character));
            x += placement.advance;
        }
        y += line_height;
    }
    for (CaptionRegion& region : regions) {
        if (!region.chars.empty()) {
            if (!preserve_region_bounds) {
                ExpandRegionToFitCharacters(region);
            }
            caption.regions.push_back(std::move(region));
        }
    }
    for (const InlineSpan& span : spans) {
        auto bounds = span_bounds.find(&span);
        if (bounds != span_bounds.end()) {
            AppendRubyRegion(span, bounds->second, plane, caption.regions);
        }
    }
}

void LayoutVertical(const std::vector<InlineSpan>& spans,
                    const RegionDefinition& definition,
                    const RegionDefinition& clip_definition,
                    const std::array<int, 2>& plane,
                    Caption& caption,
                    bool preserve_region_bounds) {
    std::vector<std::vector<CharacterPlacement>> columns(1);
    FontSize default_font_size = StyleFontSize(definition.style, plane, {72, 72});
    int column_width = StyleLength(definition.style, "lineHeight", plane[0],
                                   std::max(default_font_size.width, default_font_size.width * 5 / 4));
    for (const InlineSpan& span : spans) {
        FontSize font_size = StyleFontSize(span.style, plane, default_font_size);
        int letter_spacing = StyleLength(span.style, "letterSpacing", definition.height, 0);
        for (uint32_t codepoint : DecodeUTF8(span.text)) {
            if (codepoint == '\n') {
                columns.emplace_back();
                continue;
            }
            columns.back().push_back({codepoint, &span, std::max(1, font_size.height + letter_spacing)});
        }
    }

    bool right_to_left = StyleValue(definition.style, "writingMode", "tbrl") != "tblr" &&
                         StyleValue(definition.style, "writingMode", "tbrl") != "tb-lr";
    int x = right_to_left ? definition.x + definition.width - column_width : definition.x;
    CaptionRegion regions[2];
    for (size_t i = 0; i < 2; ++i) {
        regions[i].x = clip_definition.x;
        regions[i].y = clip_definition.y;
        regions[i].width = clip_definition.width;
        regions[i].height = clip_definition.height;
        regions[i].is_ruby = i == 1;
    }
    for (const auto& column : columns) {
        int column_height = 0;
        for (const CharacterPlacement& placement : column) {
            column_height += placement.advance;
        }
        std::string text_align = StyleValue(definition.style, "textAlign", "center");
        int y = definition.y;
        if (text_align == "center") {
            y += (definition.height - column_height) / 2;
        } else if (text_align == "end" || text_align == "right") {
            y += definition.height - column_height;
        }
        for (const CharacterPlacement& placement : column) {
            FontSize font_size = StyleFontSize(placement.span->style, plane, default_font_size);
            int letter_spacing = StyleLength(placement.span->style, "letterSpacing", definition.height, 0);
            CaptionChar character = MakeCaptionChar(placement.codepoint, placement.span->style,
                                                    x, y, font_size.width, font_size.height,
                                                    font_size.height + letter_spacing, 0, false);
            character.char_horizontal_spacing = std::max(0, column_width - font_size.width);
            character.char_vertical_spacing = letter_spacing;
            regions[placement.span->is_ruby ? 1 : 0].chars.push_back(std::move(character));
            y += placement.advance;
        }
        x += right_to_left ? -column_width : column_width;
    }
    for (CaptionRegion& region : regions) {
        if (!region.chars.empty()) {
            if (!preserve_region_bounds) {
                ExpandRegionToFitCharacters(region);
            }
            caption.regions.push_back(std::move(region));
        }
    }
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

bool IsVerticalWritingMode(const Style& style) {
    std::string writing_mode = StyleValue(style, "writingMode");
    return writing_mode == "tbrl" || writing_mode == "tb-rl" ||
           writing_mode == "tblr" || writing_mode == "tb-lr";
}

RegionDefinition MakeFormattingDefinition(const RegionDefinition& region,
                                          const Style& effective_style) {
    RegionDefinition result = region;
    result.style = effective_style;
    if (StyleValue(result.style, "writingMode") == "tbrl") {
        // TR-B39 defines tts:origin as the upper-right corner for vertical
        // writing, while CaptionRegion and RegionRenderer use upper-left.
        result.x -= result.width;
    }
    return result;
}

void MergeCaptionRegionsWithSameClip(Caption& caption) {
    std::vector<CaptionRegion> merged;
    for (CaptionRegion& region : caption.regions) {
        auto target = std::find_if(merged.begin(), merged.end(),
                                   [&](const CaptionRegion& candidate) {
                                       return candidate.is_ruby == region.is_ruby &&
                                              candidate.x == region.x && candidate.y == region.y &&
                                              candidate.width == region.width &&
                                              candidate.height == region.height;
                                   });
        if (target == merged.end()) {
            merged.push_back(std::move(region));
        } else {
            target->chars.insert(target->chars.end(),
                                 std::make_move_iterator(region.chars.begin()),
                                 std::make_move_iterator(region.chars.end()));
        }
    }
    caption.regions = std::move(merged);
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
    if (IsEmptyTTMLDocument(tt)) {
        return emit_clear();
    }

    const tinyxml2::XMLElement* body = B62FirstChild(tt, "body");
    if (!body) {
        return B62DecodeStatus::kNoCaption;
    }

    std::unordered_map<std::string, std::shared_ptr<const B62ResourceBlob>> embedded_images;
    if (preserve_document_layout && document_metadata) {
        std::vector<const tinyxml2::XMLElement*> image_elements;
        B62CollectDescendants(tt, "image", image_elements);
        for (const tinyxml2::XMLElement* image : image_elements) {
            if (!B62IsSMPTEElement(image, "image")) {
                continue;
            }
            const char* id = B62FindXMLID(image);
            const char* payload = image->GetText();
            std::string encoding = TrimASCII(B62FindAttribute(image, "encoding")
                ? B62FindAttribute(image, "encoding") : "base64");
            std::string image_type = TrimASCII(B62FindAttribute(image, "imageType")
                ? B62FindAttribute(image, "imageType") : "png");
            std::transform(encoding.begin(), encoding.end(), encoding.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            std::transform(image_type.begin(), image_type.end(), image_type.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (!id || !payload || encoding != "base64" || image_type != "png") {
                continue;
            }
            auto bytes = DecodeBase64(payload);
            if (!bytes) {
                continue;
            }
            auto blob = std::make_shared<B62ResourceBlob>();
            blob->scope_id = resource_store_.active_scope_id();
            blob->index = std::numeric_limits<uint32_t>::max();
            blob->kind = B62ResourceKind::kPNGImage;
            blob->mime_type = "image/png";
            blob->bytes = std::make_shared<const std::vector<uint8_t>>(std::move(*bytes));
            embedded_images.emplace(id, std::move(blob));
        }
    }

    std::unordered_map<uint64_t, std::shared_ptr<const B62ResourceBlob>> resolved_resource_cache;
    const auto resolve_resource = [&](const char* uri_value,
                                      B62ResourceKind kind) -> B62ResourceReference {
        B62ResourceReference reference;
        if (!uri_value) {
            return reference;
        }
        reference.uri = TrimASCII(uri_value);
        if (!reference.uri.empty() && reference.uri.front() == '#') {
            auto embedded = embedded_images.find(reference.uri.substr(1));
            if (embedded != embedded_images.end()) {
                reference.resolved = embedded->second;
            }
            return reference;
        }
        auto index = ParseSubtResourceIndex(reference.uri);
        if (!index || !resource_store_.has_active_scope()) {
            return reference;
        }
        const B62ResourceStore::Resource* found_resource = resource_store_.FindActive(*index);
        if (!found_resource) {
            return reference;
        }
        B62ResourceKind resolved_kind = kind;
        if (resolved_kind == B62ResourceKind::kUnknown) {
            if (found_resource->mime_type == "image/png") {
                resolved_kind = B62ResourceKind::kPNGImage;
            } else if (found_resource->mime_type == "image/svg+xml") {
                resolved_kind = B62ResourceKind::kSVGImage;
            }
        }
        const uint64_t cache_key = (static_cast<uint64_t>(resolved_kind) << 32) | *index;
        auto cached = resolved_resource_cache.find(cache_key);
        if (cached != resolved_resource_cache.end()) {
            reference.resolved = cached->second;
            return reference;
        }
        auto blob = std::make_shared<B62ResourceBlob>();
        blob->scope_id = resource_store_.active_scope_id();
        blob->index = *index;
        blob->kind = resolved_kind;
        blob->mime_type = found_resource->mime_type;
        blob->bytes = found_resource->data;
        reference.resolved = blob;
        resolved_resource_cache.emplace(cache_key, std::move(blob));
        return reference;
    };

    if (preserve_document_layout && document_metadata) {
        std::vector<const tinyxml2::XMLElement*> font_face_elements;
        B62CollectDescendants(tt, "font-face", font_face_elements);
        for (const tinyxml2::XMLElement* font_face_element : font_face_elements) {
            if (!B62IsARIBElement(font_face_element, "font-face")) {
                continue;
            }
            const char* family = B62FindAttribute(font_face_element, "font-family");
            if (!family || TrimASCII(family).empty()) {
                continue;
            }
            B62FontFace font_face;
            if (const char* id = B62FindXMLID(font_face_element)) {
                font_face.id = id;
            }
            font_face.family = TrimASCII(family);
            if (const char* unicode_range = B62FindAttribute(font_face_element, "unicode-range")) {
                font_face.unicode_range = TrimASCII(unicode_range);
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
                std::string format = TrimASCII(B62FindAttribute(source, "format")
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
                font_source.resource = resolve_resource(uri, resource_kind);
                font_face.sources.push_back(std::move(font_source));
            }
            if (!font_face.sources.empty()) {
                parsed_metadata.font_faces.push_back(std::move(font_face));
            }
        }
    }

    std::array<int, 2> plane{3840, 2160};
    if (auto extent = ParseLengthPair(B62FindAttribute(tt, "extent"), plane)) {
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
    std::unordered_map<std::string, Style> style_cache;

    std::vector<const tinyxml2::XMLElement*> region_elements;
    B62CollectDescendants(tt, "region", region_elements);
    std::unordered_map<std::string, RegionDefinition> region_definitions;
    for (const tinyxml2::XMLElement* region : region_elements) {
        const char* id = B62FindAttribute(region, "id");
        if (!id) {
            continue;
        }
        RegionDefinition definition;
        definition.x = plane[0] / 10;
        definition.y = plane[1] * 78 / 100;
        definition.width = plane[0] * 8 / 10;
        definition.height = plane[1] * 16 / 100;
        if (auto origin = ParseLengthPair(B62FindAttribute(region, "origin"), plane)) {
            definition.x = (*origin)[0];
            definition.y = (*origin)[1];
        }
        if (auto extent = ParseLengthPair(B62FindAttribute(region, "extent"), plane)) {
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
        cue.start = ParseTime(B62FindAttribute(paragraph, "begin"), &cue.indefinite_start);
        if (!cue.start && !cue.indefinite_start && timing_node) {
            cue.start = ParseTime(B62FindAttribute(timing_node, "begin"), &cue.indefinite_start);
        }
        bool indefinite_end = false;
        cue.end = ParseTime(B62FindAttribute(paragraph, "end"), &indefinite_end);
        if (!cue.end && !indefinite_end && timing_node) {
            cue.end = ParseTime(B62FindAttribute(timing_node, "end"), &indefinite_end);
        }
        bool indefinite_duration = false;
        std::optional<int64_t> duration = ParseTime(B62FindAttribute(paragraph, "dur"), &indefinite_duration);
        if (!duration && !indefinite_duration && timing_node) {
            duration = ParseTime(B62FindAttribute(timing_node, "dur"), &indefinite_duration);
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
        cue.start = ParseTime(B62FindAttribute(timing_node, "begin"));
        bool indefinite_end = false;
        cue.end = ParseTime(B62FindAttribute(timing_node, "end"), &indefinite_end);
        bool indefinite_duration = false;
        std::optional<int64_t> duration = ParseTime(
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
        image.start = ParseTime(B62FindAttribute(timing_node, "begin"));
        bool indefinite_end = false;
        image.end = ParseTime(B62FindAttribute(timing_node, "end"), &indefinite_end);
        bool indefinite_duration = false;
        std::optional<int64_t> duration = ParseTime(
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
            audio.source = resolve_resource(B62FindAttribute(raw_audio.node, "src"),
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
            if (const char* owner_id = B62FindXMLID(raw_image.owner)) {
                image.owner_id = owner_id;
            }
            image.source = resolve_resource(raw_image.source, B62ResourceKind::kUnknown);
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
        RegionDefinition definition;
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
        Style inherited = CollectInheritedStyle(raw.node, definition.style, style_nodes, style_cache);
        definition.style = inherited;
        RegionDefinition paragraph_formatting = preserve_document_layout
            ? MakeFormattingDefinition(definition, inherited)
            : definition;
        std::vector<InlineSpan> spans;
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
        for (const InlineSpan& span : spans) {
            if (!preserve_document_layout || !span.is_ruby) {
                caption.text += span.text;
            }
        }

        const auto layout = [&](const std::vector<InlineSpan>& layout_spans,
                                const RegionDefinition& layout_definition,
                                const RegionDefinition& clip_definition,
                                bool preserve_region_bounds) {
            if (IsVerticalWritingMode(layout_definition.style)) {
                LayoutVertical(layout_spans, layout_definition, clip_definition,
                               plane, caption, preserve_region_bounds);
            } else {
                LayoutHorizontal(layout_spans, layout_definition, clip_definition,
                                 plane, caption, preserve_region_bounds);
            }
        };
        if (preserve_document_layout) {
            struct SpanFlow {
                const RegionDefinition* definition = nullptr;
                bool explicit_position = false;
                std::vector<InlineSpan> spans;
            };
            std::vector<SpanFlow> flows;
            for (const InlineSpan& span : spans) {
                if (flows.empty() || span.resets_position) {
                    flows.push_back({span.region ? span.region : &definition,
                                     span.resets_position, {}});
                }
                flows.back().spans.push_back(span);
            }
            for (const SpanFlow& flow : flows) {
                RegionDefinition formatting = MakeFormattingDefinition(
                    *flow.definition, flow.spans.front().style);
                if (flow.explicit_position) {
                    // A span region origin is the operation-position reference
                    // point of its first character, not an alignment box.
                    formatting.style["textAlign"] = "start";
                    formatting.style["displayAlign"] = "before";
                }
                const RegionDefinition& clip = has_paragraph_region
                    ? paragraph_formatting
                    : formatting;
                layout(flow.spans, formatting, clip, true);
            }
            MergeCaptionRegionsWithSameClip(caption);
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
