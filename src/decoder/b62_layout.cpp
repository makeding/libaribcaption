/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_layout.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/unicode_helper.hpp"
#include "base/utf_helper.hpp"
#include "decoder/b62_text_util.hpp"

namespace aribcaption::internal {
namespace {

struct CharacterPlacement {
    uint32_t codepoint = 0;
    const B62InlineSpan* span = nullptr;
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

std::optional<double> ParseLength(std::string_view value, double base) {
    std::string text = B62TrimASCII(std::string(value));
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

std::optional<std::array<int, 2>> ParseLengthPair(
    const char* value,
    const std::array<int, 2>& plane) {
    if (!value) {
        return std::nullopt;
    }
    std::string text = B62TrimASCII(value);
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
    return std::array<int, 2>{
        static_cast<int>(std::lround(*x)),
        static_cast<int>(std::lround(*y)),
    };
}

std::optional<ColorRGBA> ParseColor(std::string value) {
    value = B62TrimASCII(std::move(value));
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    static const std::unordered_map<std::string, ColorRGBA> named = {
        {"black", ColorRGBA(0, 0, 0)},    {"white", ColorRGBA(255, 255, 255)},
        {"red", ColorRGBA(255, 0, 0)},    {"green", ColorRGBA(0, 255, 0)},
        {"blue", ColorRGBA(0, 0, 255)},   {"yellow", ColorRGBA(255, 255, 0)},
        {"cyan", ColorRGBA(0, 255, 255)}, {"magenta", ColorRGBA(255, 0, 255)},
        {"transparent", ColorRGBA(0, 0, 0, 0)},
    };
    if (auto it = named.find(value); it != named.end()) {
        return it->second;
    }
    if ((value.size() != 7 && value.size() != 9) || value[0] != '#') {
        return std::nullopt;
    }
    char* end = nullptr;
    unsigned long rgba = std::strtoul(value.c_str() + 1, &end, 16);
    if (end != value.c_str() + value.size()) {
        return std::nullopt;
    }
    if (value.size() == 7) {
        return ColorRGBA(static_cast<uint8_t>((rgba >> 16) & 0xff),
                         static_cast<uint8_t>((rgba >> 8) & 0xff),
                         static_cast<uint8_t>(rgba & 0xff));
    }
    return ColorRGBA(static_cast<uint8_t>((rgba >> 24) & 0xff),
                     static_cast<uint8_t>((rgba >> 16) & 0xff),
                     static_cast<uint8_t>((rgba >> 8) & 0xff),
                     static_cast<uint8_t>(rgba & 0xff));
}

int StyleLength(const B62Style& style, const char* key, int base, int fallback) {
    auto it = style.find(key);
    if (it == style.end()) {
        return fallback;
    }
    auto value = ParseLength(it->second, base);
    return value ? std::max(1, static_cast<int>(std::lround(*value))) : fallback;
}

FontSize StyleFontSize(const B62Style& style,
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

std::string StyleValue(const B62Style& style,
                       const char* key,
                       const char* fallback = "") {
    auto it = style.find(key);
    return it == style.end() ? fallback : it->second;
}

ColorRGBA StyleColor(const B62Style& style, const char* key, ColorRGBA fallback) {
    auto it = style.find(key);
    if (it == style.end()) {
        return fallback;
    }
    auto color = ParseColor(it->second);
    return color.value_or(fallback);
}

ColorRGBA StrokeColor(const B62Style& style) {
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
        std::string value = last_space == std::string::npos
            ? shadow->second
            : shadow->second.substr(last_space + 1);
        if (auto color = ParseColor(value)) {
            return *color;
        }
    }
    return ColorRGBA(0, 0, 0);
}

CharStyle MakeCharStyle(const B62Style& style) {
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
    value = B62TrimASCII(std::move(value));
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
    paint.solid_compatible =
        std::find(tokens.begin(), tokens.end(), "solid") != tokens.end();
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

EnclosureStyle MakeEnclosureStyle(const B62Style& style) {
    unsigned flags = kEnclosureStyleDefault;
    auto border = style.find("border");
    if (border != style.end() && ParseBorder(border->second).visible) {
        flags = kEnclosureStyleTop | kEnclosureStyleBottom |
                kEnclosureStyleLeft | kEnclosureStyleRight;
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

BorderPaint MakeEnclosurePaint(const B62Style& style) {
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
                            const B62Style& style,
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
        uint32_t codepoint = utf::DecodeUTF8ToCodePoint(
            reinterpret_cast<const uint8_t*>(text.data() + offset),
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

void AppendRubyRegion(const B62InlineSpan& span,
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

void LayoutHorizontal(const std::vector<B62InlineSpan>& spans,
                      const B62RegionDefinition& definition,
                      const B62RegionDefinition& clip_definition,
                      const std::array<int, 2>& plane,
                      Caption& caption,
                      bool preserve_region_bounds) {
    std::vector<std::vector<CharacterPlacement>> lines(1);
    FontSize default_font_size = StyleFontSize(definition.style, plane, {72, 72});
    int line_height = StyleLength(
        definition.style, "lineHeight", plane[1],
        std::max(default_font_size.height, default_font_size.height * 5 / 4));

    for (const B62InlineSpan& span : spans) {
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
            int advance = std::max(
                1, (halfwidth ? font_size.width / 2 : font_size.width) + letter_spacing);
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
    std::unordered_map<const B62InlineSpan*, BoundingBox> span_bounds;
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
            const B62Style& style = placement.span->style;
            FontSize font_size = StyleFontSize(style, plane, default_font_size);
            int letter_spacing = StyleLength(style, "letterSpacing", definition.width, 0);
            bool halfwidth = unicode::IsHalfwidthCharacter(placement.codepoint);
            CaptionChar character = MakeCaptionChar(
                placement.codepoint, style, x, y, font_size.width, font_size.height,
                line_height, letter_spacing, halfwidth);
            int glyph_x = x + character.char_horizontal_spacing / 2;
            int glyph_y = y + character.char_vertical_spacing / 2;
            span_bounds[placement.span].Include(
                glyph_x, glyph_y, placement.advance, font_size.height);
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
    for (const B62InlineSpan& span : spans) {
        auto bounds = span_bounds.find(&span);
        if (bounds != span_bounds.end()) {
            AppendRubyRegion(span, bounds->second, plane, caption.regions);
        }
    }
}

void LayoutVertical(const std::vector<B62InlineSpan>& spans,
                    const B62RegionDefinition& definition,
                    const B62RegionDefinition& clip_definition,
                    const std::array<int, 2>& plane,
                    Caption& caption,
                    bool preserve_region_bounds) {
    std::vector<std::vector<CharacterPlacement>> columns(1);
    FontSize default_font_size = StyleFontSize(definition.style, plane, {72, 72});
    int column_width = StyleLength(
        definition.style, "lineHeight", plane[0],
        std::max(default_font_size.width, default_font_size.width * 5 / 4));
    for (const B62InlineSpan& span : spans) {
        FontSize font_size = StyleFontSize(span.style, plane, default_font_size);
        int letter_spacing = StyleLength(span.style, "letterSpacing", definition.height, 0);
        for (uint32_t codepoint : DecodeUTF8(span.text)) {
            if (codepoint == '\n') {
                columns.emplace_back();
                continue;
            }
            columns.back().push_back(
                {codepoint, &span, std::max(1, font_size.height + letter_spacing)});
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
            FontSize font_size = StyleFontSize(
                placement.span->style, plane, default_font_size);
            int letter_spacing = StyleLength(
                placement.span->style, "letterSpacing", definition.height, 0);
            CaptionChar character = MakeCaptionChar(
                placement.codepoint, placement.span->style,
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

}  // namespace

std::optional<std::array<int, 2>> B62ParseLengthPair(
    const char* value,
    const std::array<int, 2>& plane) {
    return ParseLengthPair(value, plane);
}

bool B62IsVerticalWritingMode(const B62Style& style) {
    std::string writing_mode = StyleValue(style, "writingMode");
    return writing_mode == "tbrl" || writing_mode == "tb-rl" ||
           writing_mode == "tblr" || writing_mode == "tb-lr";
}

B62RegionDefinition B62MakeFormattingDefinition(
    const B62RegionDefinition& region,
    const B62Style& effective_style) {
    B62RegionDefinition result = region;
    result.style = effective_style;
    if (StyleValue(result.style, "writingMode") == "tbrl") {
        // TR-B39 defines tts:origin as the upper-right corner for vertical
        // writing, while CaptionRegion and RegionRenderer use upper-left.
        result.x -= result.width;
    }
    return result;
}

void B62LayoutHorizontal(const std::vector<B62InlineSpan>& spans,
                         const B62RegionDefinition& definition,
                         const B62RegionDefinition& clip_definition,
                         const std::array<int, 2>& plane,
                         Caption& caption,
                         bool preserve_region_bounds) {
    LayoutHorizontal(spans, definition, clip_definition, plane, caption,
                     preserve_region_bounds);
}

void B62LayoutVertical(const std::vector<B62InlineSpan>& spans,
                       const B62RegionDefinition& definition,
                       const B62RegionDefinition& clip_definition,
                       const std::array<int, 2>& plane,
                       Caption& caption,
                       bool preserve_region_bounds) {
    LayoutVertical(spans, definition, clip_definition, plane, caption,
                   preserve_region_bounds);
}

void B62MergeCaptionRegionsWithSameClip(Caption& caption) {
    std::vector<CaptionRegion> merged;
    for (CaptionRegion& region : caption.regions) {
        auto target = std::find_if(
            merged.begin(), merged.end(),
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

}  // namespace aribcaption::internal
