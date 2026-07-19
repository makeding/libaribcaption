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

namespace aribcaption::internal {
namespace {

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
};

struct RawCue {
    const tinyxml2::XMLElement* node = nullptr;
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

std::string_view LocalName(std::string_view name) {
    size_t colon = name.rfind(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

const char* FindAttribute(const tinyxml2::XMLElement* element, std::string_view local_name) {
    if (!element) {
        return nullptr;
    }
    for (const tinyxml2::XMLAttribute* attr = element->FirstAttribute(); attr; attr = attr->Next()) {
        if (LocalName(attr->Name()) == local_name) {
            return attr->Value();
        }
    }
    return nullptr;
}

const tinyxml2::XMLElement* FirstChild(const tinyxml2::XMLElement* parent, std::string_view local_name) {
    if (!parent) {
        return nullptr;
    }
    for (const tinyxml2::XMLElement* child = parent->FirstChildElement(); child; child = child->NextSiblingElement()) {
        if (LocalName(child->Name()) == local_name) {
            return child;
        }
    }
    return nullptr;
}

void CollectDescendants(const tinyxml2::XMLElement* parent,
                        std::string_view local_name,
                        std::vector<const tinyxml2::XMLElement*>& out) {
    if (!parent) {
        return;
    }
    for (const tinyxml2::XMLElement* child = parent->FirstChildElement(); child; child = child->NextSiblingElement()) {
        if (LocalName(child->Name()) == local_name) {
            out.push_back(child);
        }
        CollectDescendants(child, local_name, out);
    }
}

std::string TrimASCII(std::string value) {
    auto is_space = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
    };
    auto begin = std::find_if_not(value.begin(), value.end(), is_space);
    auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
    return begin < end ? std::string(begin, end) : std::string();
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
    static constexpr std::array<std::string_view, 20> kStyleAttributes = {
        "fontSize",     "lineHeight", "fontWeight",     "fontStyle",   "color",       "backgroundColor",
        "displayAlign", "textAlign",  "textDecoration", "textShadow",  "writingMode", "direction",
        "opacity",      "border",     "border-top",     "border-bottom", "border-left", "border-right",
        "letter-spacing", "text-shadow",
    };
    for (std::string_view name : kStyleAttributes) {
        if (const char* value = FindAttribute(element, name)) {
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
    const char* refs = FindAttribute(element, "style");
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
        std::string_view name = LocalName(current->Name());
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
                       std::vector<InlineSpan>& spans) {
    for (const tinyxml2::XMLNode* child = parent->FirstChild(); child; child = child->NextSibling()) {
        if (const tinyxml2::XMLText* text = child->ToText()) {
            std::string normalized = NormalizeText(text->Value());
            if (!normalized.empty()) {
                spans.push_back({std::move(normalized), inherited_style, {}, {}, {}});
            }
            continue;
        }
        const tinyxml2::XMLElement* element = child->ToElement();
        if (!element) {
            continue;
        }
        std::string_view name = LocalName(element->Name());
        if (name == "br") {
            spans.push_back({"\n", inherited_style, {}, {}, {}});
            continue;
        }
        if (name != "span") {
            AppendInlineSpans(element, inherited_style, style_nodes, style_cache, spans);
            continue;
        }

        Style style = MergeNodeStyle(element, inherited_style, style_nodes, style_cache);
        size_t begin = spans.size();
        AppendInlineSpans(element, style, style_nodes, style_cache, spans);
        const char* id = FindAttribute(element, "id");
        const char* ruby = FindAttribute(element, "ruby");
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

FontSize StyleFontSize(const Style& style, const std::array<int, 2>& plane, FontSize fallback) {
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
    if (style.count("textShadow")) {
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
    if (tokens.empty() || tokens[0] == "none" || tokens[0] == "hidden") {
        return paint;
    }
    paint.visible = true;
    if (tokens.size() >= 3) {
        paint.color = ParseColor(tokens[2]).value_or(ColorRGBA());
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
    if (enclosure.visible && !(character.style & kCharStyleStroke)) {
        character.stroke_color = enclosure.color;
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
            codepoint, span.style, x, y, font_size.width, font_size.height, font_size.height, 0, false);
        character.char_width = advance;
        ruby.chars.push_back(std::move(character));
        x += advance;
    }
    regions.push_back(std::move(ruby));
}

void LayoutHorizontal(const std::vector<InlineSpan>& spans,
                      const RegionDefinition& definition,
                      const std::array<int, 2>& plane,
                      Caption& caption) {
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

    CaptionRegion region;
    region.x = definition.x;
    region.y = definition.y;
    region.width = definition.width;
    region.height = definition.height;
    std::unordered_map<const InlineSpan*, BoundingBox> span_bounds;
    std::string text_align = StyleValue(definition.style, "textAlign", "start");

    for (const auto& line : lines) {
        int line_width = 0;
        for (const CharacterPlacement& placement : line) {
            line_width += placement.advance;
        }
        int x = definition.x;
        if (text_align == "center") {
            x += std::max(0, (definition.width - line_width) / 2);
        } else if (text_align == "right" || text_align == "end") {
            x += std::max(0, definition.width - line_width);
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
            region.chars.push_back(std::move(character));
            x += placement.advance;
        }
        y += line_height;
    }
    if (!region.chars.empty()) {
        caption.regions.push_back(std::move(region));
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
                    const std::array<int, 2>& plane,
                    Caption& caption) {
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
    CaptionRegion region;
    region.x = definition.x;
    region.y = definition.y;
    region.width = definition.width;
    region.height = definition.height;
    for (const auto& column : columns) {
        int column_height = 0;
        for (const CharacterPlacement& placement : column) {
            column_height += placement.advance;
        }
        std::string text_align = StyleValue(definition.style, "textAlign", "start");
        int y = definition.y;
        if (text_align == "center") {
            y += std::max(0, (definition.height - column_height) / 2);
        } else if (text_align == "end" || text_align == "right") {
            y += std::max(0, definition.height - column_height);
        }
        for (const CharacterPlacement& placement : column) {
            FontSize font_size = StyleFontSize(placement.span->style, plane, default_font_size);
            int letter_spacing = StyleLength(placement.span->style, "letterSpacing", definition.height, 0);
            CaptionChar character = MakeCaptionChar(placement.codepoint, placement.span->style,
                                                    x, y, font_size.width, font_size.height,
                                                    font_size.height + letter_spacing, 0, false);
            character.char_horizontal_spacing = std::max(0, column_width - font_size.width);
            character.char_vertical_spacing = letter_spacing;
            region.chars.push_back(std::move(character));
            y += placement.advance;
        }
        x += right_to_left ? -column_width : column_width;
    }
    if (!region.chars.empty()) {
        caption.regions.push_back(std::move(region));
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

const char* FindNearestAttribute(const tinyxml2::XMLElement* element, std::string_view name) {
    for (const tinyxml2::XMLNode* node = element; node; node = node->Parent()) {
        const tinyxml2::XMLElement* current = node->ToElement();
        if (current) {
            if (const char* value = FindAttribute(current, name)) {
                return value;
            }
        }
    }
    return nullptr;
}

const tinyxml2::XMLElement* FindNearestTimedNode(const tinyxml2::XMLElement* element) {
    for (const tinyxml2::XMLNode* node = element; node; node = node->Parent()) {
        const tinyxml2::XMLElement* current = node->ToElement();
        if (!current) {
            continue;
        }
        if (FindAttribute(current, "begin") || FindAttribute(current, "end")) {
            return current;
        }
    }
    return nullptr;
}

}  // namespace

B62DecoderImpl::B62DecoderImpl(Context& context) : log_(GetContextLogger(context)) {}

B62DecoderImpl::~B62DecoderImpl() = default;

B62DecodeStatus B62DecoderImpl::Decode(const uint8_t* ttml_data,
                                       size_t length,
                                       int64_t base_pts,
                                       B62DecodeResult& out_result) {
    out_result.captions.clear();
    if (!ttml_data || length == 0) {
        log_->e("B62DecoderImpl: empty TTML document");
        return B62DecodeStatus::kError;
    }

    tinyxml2::XMLDocument document;
    tinyxml2::XMLError error = document.Parse(reinterpret_cast<const char*>(ttml_data), length);
    const tinyxml2::XMLElement* tt = document.RootElement();
    if (error != tinyxml2::XML_SUCCESS || !tt || LocalName(tt->Name()) != "tt") {
        log_->e("B62DecoderImpl: invalid TTML document: %s", document.ErrorStr());
        return B62DecodeStatus::kError;
    }
    const tinyxml2::XMLElement* body = FirstChild(tt, "body");
    if (!body) {
        log_->e("B62DecoderImpl: TTML document does not contain a body");
        return B62DecodeStatus::kError;
    }

    std::array<int, 2> plane{3840, 2160};
    if (auto extent = ParseLengthPair(FindAttribute(tt, "extent"), plane)) {
        plane = *extent;
    }

    std::vector<const tinyxml2::XMLElement*> style_elements;
    CollectDescendants(tt, "style", style_elements);
    std::unordered_map<std::string, const tinyxml2::XMLElement*> style_nodes;
    for (const tinyxml2::XMLElement* style : style_elements) {
        if (const char* id = FindAttribute(style, "id")) {
            style_nodes[id] = style;
        }
    }
    std::unordered_map<std::string, Style> style_cache;

    std::vector<const tinyxml2::XMLElement*> region_elements;
    CollectDescendants(tt, "region", region_elements);
    std::unordered_map<std::string, RegionDefinition> region_definitions;
    for (const tinyxml2::XMLElement* region : region_elements) {
        const char* id = FindAttribute(region, "id");
        if (!id) {
            continue;
        }
        RegionDefinition definition;
        definition.x = plane[0] / 10;
        definition.y = plane[1] * 78 / 100;
        definition.width = plane[0] * 8 / 10;
        definition.height = plane[1] * 16 / 100;
        if (auto origin = ParseLengthPair(FindAttribute(region, "origin"), plane)) {
            definition.x = (*origin)[0];
            definition.y = (*origin)[1];
        }
        if (auto extent = ParseLengthPair(FindAttribute(region, "extent"), plane)) {
            definition.width = (*extent)[0];
            definition.height = (*extent)[1];
        }
        definition.style = MergeNodeStyle(region, {}, style_nodes, style_cache);
        region_definitions[id] = std::move(definition);
    }

    std::vector<const tinyxml2::XMLElement*> paragraphs;
    CollectDescendants(body, "p", paragraphs);
    std::vector<RawCue> raw_cues;
    raw_cues.reserve(paragraphs.size());
    std::optional<int64_t> minimum_start;
    for (const tinyxml2::XMLElement* paragraph : paragraphs) {
        RawCue cue;
        cue.node = paragraph;
        const tinyxml2::XMLElement* timing_node = FindNearestTimedNode(paragraph);
        cue.start = ParseTime(FindAttribute(paragraph, "begin"));
        if (!cue.start && timing_node) {
            cue.start = ParseTime(FindAttribute(timing_node, "begin"));
        }
        bool indefinite_end = false;
        cue.end = ParseTime(FindAttribute(paragraph, "end"), &indefinite_end);
        if (!cue.end && !indefinite_end && timing_node) {
            cue.end = ParseTime(FindAttribute(timing_node, "end"), &indefinite_end);
        }
        bool indefinite_duration = false;
        std::optional<int64_t> duration = ParseTime(FindAttribute(paragraph, "dur"), &indefinite_duration);
        if (!duration && !indefinite_duration && timing_node) {
            duration = ParseTime(FindAttribute(timing_node, "dur"), &indefinite_duration);
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
    if (raw_cues.empty()) {
        return B62DecodeStatus::kNoCaption;
    }

    int64_t timeline_offset = 0;
    if (base_pts != PTS_NOPTS && minimum_start && std::llabs(base_pts - *minimum_start) > 50) {
        timeline_offset = base_pts - *minimum_start;
    }
    uint32_t language = ParseLanguage(FindAttribute(tt, "lang"));

    for (const RawCue& raw : raw_cues) {
        RegionDefinition definition;
        definition.x = plane[0] / 10;
        definition.y = plane[1] * 78 / 100;
        definition.width = plane[0] * 8 / 10;
        definition.height = plane[1] * 16 / 100;
        const char* region_id = FindNearestAttribute(raw.node, "region");
        if (region_id) {
            if (auto it = region_definitions.find(region_id); it != region_definitions.end()) {
                definition = it->second;
            }
        }
        Style inherited = CollectInheritedStyle(raw.node, definition.style, style_nodes, style_cache);
        definition.style = inherited;
        std::vector<InlineSpan> spans;
        AppendInlineSpans(raw.node, inherited, style_nodes, style_cache, spans);
        ResolveRuby(spans);
        if (spans.empty()) {
            continue;
        }

        Caption caption;
        caption.flags = kCaptionFlagsClearScreen;
        caption.iso6392_language_code = language;
        caption.plane_width = plane[0];
        caption.plane_height = plane[1];
        caption.pts = raw.start ? *raw.start + timeline_offset : base_pts;
        if (raw.indefinite) {
            caption.wait_duration = DURATION_INDEFINITE;
        } else {
            int64_t end = raw.end ? *raw.end + timeline_offset : (caption.pts == PTS_NOPTS ? 5000 : caption.pts + 5000);
            if (caption.pts != PTS_NOPTS && end <= caption.pts) {
                end = caption.pts + 50;
            }
            caption.wait_duration = caption.pts == PTS_NOPTS ? 5000 : end - caption.pts;
            caption.flags = static_cast<CaptionFlags>(caption.flags | kCaptionFlagsWaitDuration);
        }
        for (const InlineSpan& span : spans) {
            caption.text += span.text;
        }

        std::string writing_mode = StyleValue(definition.style, "writingMode");
        if (writing_mode == "tbrl" || writing_mode == "tb-rl" || writing_mode == "tblr" || writing_mode == "tb-lr") {
            LayoutVertical(spans, definition, plane, caption);
        } else {
            LayoutHorizontal(spans, definition, plane, caption);
        }
        if (!caption.regions.empty()) {
            out_result.captions.push_back(std::move(caption));
        }
    }

    return out_result.captions.empty() ? B62DecodeStatus::kNoCaption : B62DecodeStatus::kGotCaption;
}

}  // namespace aribcaption::internal
