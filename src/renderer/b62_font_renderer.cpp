/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * This file is part of libaribcaption.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "renderer/b62_font_renderer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/logger.hpp"
#include "base/tinyxml2.h"
#include "base/utf_helper.hpp"
#include "renderer/bitmap.hpp"
#include "renderer/canvas.hpp"
#include "renderer/rect.hpp"

namespace aribcaption {
namespace {

constexpr size_t kMaxSVGGlyphs = 4096;
constexpr size_t kMaxSVGSegments = 200000;
constexpr size_t kMaxSVGPathBytes = 2u * 1024u * 1024u;
constexpr int kCurveSteps = 24;
constexpr int kSupersample = 4;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Edge {
    Point from;
    Point to;
};

struct SVGGlyph {
    double advance = 0.0;
    std::vector<Edge> edges;
};

struct SVGFont {
    double units_per_em = 360.0;
    double ascent = 360.0;
    double descent = 0.0;
    double default_advance = 360.0;
    std::unordered_map<uint32_t, SVGGlyph> glyphs;
};

bool IsCommand(char ch) {
    switch (ch) {
        case 'M': case 'm': case 'L': case 'l': case 'H': case 'h':
        case 'V': case 'v': case 'C': case 'c': case 'S': case 's':
        case 'Q': case 'q': case 'T': case 't': case 'A': case 'a':
        case 'Z': case 'z':
            return true;
        default:
            return false;
    }
}

class PathScanner {
public:
    explicit PathScanner(std::string_view input) : input_(input) {}

    void SkipSeparators() {
        while (position_ < input_.size()) {
            char ch = input_[position_];
            if (ch == ',' || std::isspace(static_cast<unsigned char>(ch))) {
                ++position_;
            } else {
                break;
            }
        }
    }

    bool AtEnd() {
        SkipSeparators();
        return position_ >= input_.size();
    }

    bool PeekCommand(char& command) {
        SkipSeparators();
        if (position_ < input_.size() && IsCommand(input_[position_])) {
            command = input_[position_++];
            return true;
        }
        return false;
    }

    bool HasNumber() {
        SkipSeparators();
        if (position_ >= input_.size()) {
            return false;
        }
        char ch = input_[position_];
        return ch == '+' || ch == '-' || ch == '.' ||
               std::isdigit(static_cast<unsigned char>(ch));
    }

    bool Number(double& value) {
        SkipSeparators();
        if (position_ >= input_.size()) {
            return false;
        }
        const char* begin = input_.data() + position_;
        char* end = nullptr;
        value = std::strtod(begin, &end);
        if (end == begin || !std::isfinite(value)) {
            return false;
        }
        position_ += static_cast<size_t>(end - begin);
        return true;
    }

private:
    std::string_view input_;
    size_t position_ = 0;
};

Point Add(Point first, Point second) {
    return {first.x + second.x, first.y + second.y};
}

void AppendEdge(std::vector<Edge>& edges, Point from, Point to) {
    if (from.x != to.x || from.y != to.y) {
        edges.push_back({from, to});
    }
}

void AppendCubic(std::vector<Edge>& edges, Point start, Point first,
                 Point second, Point end) {
    Point previous = start;
    for (int index = 1; index <= kCurveSteps; ++index) {
        double t = static_cast<double>(index) / kCurveSteps;
        double inverse = 1.0 - t;
        Point point{
            inverse * inverse * inverse * start.x +
                3.0 * inverse * inverse * t * first.x +
                3.0 * inverse * t * t * second.x + t * t * t * end.x,
            inverse * inverse * inverse * start.y +
                3.0 * inverse * inverse * t * first.y +
                3.0 * inverse * t * t * second.y + t * t * t * end.y,
        };
        AppendEdge(edges, previous, point);
        previous = point;
    }
}

void AppendQuadratic(std::vector<Edge>& edges, Point start, Point control,
                     Point end) {
    Point previous = start;
    for (int index = 1; index <= kCurveSteps; ++index) {
        double t = static_cast<double>(index) / kCurveSteps;
        double inverse = 1.0 - t;
        Point point{
            inverse * inverse * start.x + 2.0 * inverse * t * control.x + t * t * end.x,
            inverse * inverse * start.y + 2.0 * inverse * t * control.y + t * t * end.y,
        };
        AppendEdge(edges, previous, point);
        previous = point;
    }
}

double VectorAngle(double ux, double uy, double vx, double vy) {
    double length = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
    if (length == 0.0) {
        return 0.0;
    }
    double cosine = std::clamp((ux * vx + uy * vy) / length, -1.0, 1.0);
    double angle = std::acos(cosine);
    return ux * vy - uy * vx < 0.0 ? -angle : angle;
}

void AppendArc(std::vector<Edge>& edges, Point start, double radius_x,
               double radius_y, double rotation, bool large_arc, bool sweep,
               Point end) {
    radius_x = std::abs(radius_x);
    radius_y = std::abs(radius_y);
    if (radius_x == 0.0 || radius_y == 0.0 ||
        (start.x == end.x && start.y == end.y)) {
        AppendEdge(edges, start, end);
        return;
    }

    constexpr double kPi = 3.14159265358979323846;
    double phi = std::fmod(rotation, 360.0) * kPi / 180.0;
    double cosine = std::cos(phi);
    double sine = std::sin(phi);
    double midpoint_x = (start.x - end.x) / 2.0;
    double midpoint_y = (start.y - end.y) / 2.0;
    double transformed_x = cosine * midpoint_x + sine * midpoint_y;
    double transformed_y = -sine * midpoint_x + cosine * midpoint_y;

    double scale = transformed_x * transformed_x / (radius_x * radius_x) +
                   transformed_y * transformed_y / (radius_y * radius_y);
    if (scale > 1.0) {
        scale = std::sqrt(scale);
        radius_x *= scale;
        radius_y *= scale;
    }

    double numerator = radius_x * radius_x * radius_y * radius_y -
                       radius_x * radius_x * transformed_y * transformed_y -
                       radius_y * radius_y * transformed_x * transformed_x;
    double denominator = radius_x * radius_x * transformed_y * transformed_y +
                         radius_y * radius_y * transformed_x * transformed_x;
    double factor = denominator == 0.0
        ? 0.0
        : std::sqrt(std::max(0.0, numerator / denominator));
    if (large_arc == sweep) {
        factor = -factor;
    }
    double center_x_prime = factor * radius_x * transformed_y / radius_y;
    double center_y_prime = factor * -radius_y * transformed_x / radius_x;
    double center_x = cosine * center_x_prime - sine * center_y_prime +
                      (start.x + end.x) / 2.0;
    double center_y = sine * center_x_prime + cosine * center_y_prime +
                      (start.y + end.y) / 2.0;

    double start_angle = VectorAngle(
        1.0, 0.0,
        (transformed_x - center_x_prime) / radius_x,
        (transformed_y - center_y_prime) / radius_y);
    double delta = VectorAngle(
        (transformed_x - center_x_prime) / radius_x,
        (transformed_y - center_y_prime) / radius_y,
        (-transformed_x - center_x_prime) / radius_x,
        (-transformed_y - center_y_prime) / radius_y);
    if (!sweep && delta > 0.0) {
        delta -= 2.0 * kPi;
    } else if (sweep && delta < 0.0) {
        delta += 2.0 * kPi;
    }

    int steps = std::max(1, static_cast<int>(std::ceil(
        std::abs(delta) / (2.0 * kPi) * kCurveSteps * 2.0)));
    Point previous = start;
    for (int index = 1; index <= steps; ++index) {
        double angle = start_angle + delta * static_cast<double>(index) / steps;
        double x = radius_x * std::cos(angle);
        double y = radius_y * std::sin(angle);
        Point point{
            cosine * x - sine * y + center_x,
            sine * x + cosine * y + center_y,
        };
        AppendEdge(edges, previous, point);
        previous = point;
    }
}

bool ParsePath(std::string_view path, std::vector<Edge>& edges) {
    if (path.empty() || path.size() > kMaxSVGPathBytes) {
        return false;
    }
    PathScanner scanner(path);
    Point current{};
    Point subpath_start{};
    Point previous_cubic_control{};
    Point previous_quadratic_control{};
    char command = 0;
    char previous_command = 0;

    auto point = [&](bool relative, Point& output) {
        if (!scanner.Number(output.x) || !scanner.Number(output.y)) {
            return false;
        }
        if (relative) {
            output = Add(output, current);
        }
        return true;
    };

    while (!scanner.AtEnd()) {
        char next = 0;
        if (scanner.PeekCommand(next)) {
            command = next;
        } else if (command == 0 || !scanner.HasNumber()) {
            return false;
        }
        bool relative = std::islower(static_cast<unsigned char>(command));
        char normalized = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));

        if (normalized == 'Z') {
            AppendEdge(edges, current, subpath_start);
            current = subpath_start;
            previous_command = command;
            command = 0;
            continue;
        }

        bool consumed = false;
        do {
            consumed = true;
            if (normalized == 'M' || normalized == 'L') {
                Point end;
                if (!point(relative, end)) return false;
                if (normalized == 'M') {
                    current = end;
                    subpath_start = end;
                    normalized = 'L';
                    command = relative ? 'l' : 'L';
                } else {
                    AppendEdge(edges, current, end);
                    current = end;
                }
            } else if (normalized == 'H') {
                double x = 0.0;
                if (!scanner.Number(x)) return false;
                if (relative) x += current.x;
                Point end{x, current.y};
                AppendEdge(edges, current, end);
                current = end;
            } else if (normalized == 'V') {
                double y = 0.0;
                if (!scanner.Number(y)) return false;
                if (relative) y += current.y;
                Point end{current.x, y};
                AppendEdge(edges, current, end);
                current = end;
            } else if (normalized == 'C') {
                Point first, second, end;
                if (!point(relative, first) || !point(relative, second) ||
                    !point(relative, end)) return false;
                AppendCubic(edges, current, first, second, end);
                current = end;
                previous_cubic_control = second;
            } else if (normalized == 'S') {
                Point second, end;
                if (!point(relative, second) || !point(relative, end)) return false;
                Point first = (previous_command == 'C' || previous_command == 'c' ||
                               previous_command == 'S' || previous_command == 's')
                    ? Point{2.0 * current.x - previous_cubic_control.x,
                            2.0 * current.y - previous_cubic_control.y}
                    : current;
                AppendCubic(edges, current, first, second, end);
                current = end;
                previous_cubic_control = second;
            } else if (normalized == 'Q') {
                Point control, end;
                if (!point(relative, control) || !point(relative, end)) return false;
                AppendQuadratic(edges, current, control, end);
                current = end;
                previous_quadratic_control = control;
            } else if (normalized == 'T') {
                Point end;
                if (!point(relative, end)) return false;
                Point control = (previous_command == 'Q' || previous_command == 'q' ||
                                 previous_command == 'T' || previous_command == 't')
                    ? Point{2.0 * current.x - previous_quadratic_control.x,
                            2.0 * current.y - previous_quadratic_control.y}
                    : current;
                AppendQuadratic(edges, current, control, end);
                current = end;
                previous_quadratic_control = control;
            } else if (normalized == 'A') {
                double radius_x, radius_y, rotation, large_arc, sweep;
                Point end;
                if (!scanner.Number(radius_x) || !scanner.Number(radius_y) ||
                    !scanner.Number(rotation) || !scanner.Number(large_arc) ||
                    !scanner.Number(sweep) || !point(relative, end)) return false;
                AppendArc(edges, current, radius_x, radius_y, rotation,
                          large_arc != 0.0, sweep != 0.0, end);
                current = end;
            } else {
                return false;
            }
            previous_command = command;
            if (edges.size() > kMaxSVGSegments) {
                return false;
            }
        } while (scanner.HasNumber());
        if (!consumed) {
            return false;
        }
    }
    return !edges.empty();
}

std::string_view LocalName(const char* name) {
    if (!name) return {};
    std::string_view view(name);
    size_t colon = view.find(':');
    return colon == std::string_view::npos ? view : view.substr(colon + 1);
}

const tinyxml2::XMLElement* FindDescendant(
    const tinyxml2::XMLElement* element, std::string_view local_name) {
    if (!element) return nullptr;
    if (LocalName(element->Name()) == local_name) return element;
    for (const tinyxml2::XMLElement* child = element->FirstChildElement();
         child; child = child->NextSiblingElement()) {
        if (LocalName(child->Name()) == local_name) {
            return child;
        }
        if (const tinyxml2::XMLElement* descendant = FindDescendant(child, local_name)) {
            return descendant;
        }
    }
    return nullptr;
}

double NumericAttribute(const tinyxml2::XMLElement* element, const char* name,
                        double fallback) {
    if (!element) return fallback;
    const char* value = element->Attribute(name);
    if (!value) return fallback;
    char* end = nullptr;
    double parsed = std::strtod(value, &end);
    return end != value && std::isfinite(parsed) ? parsed : fallback;
}

std::unique_ptr<SVGFont> ParseSVGFont(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return nullptr;
    tinyxml2::XMLDocument document;
    if (document.Parse(reinterpret_cast<const char*>(bytes.data()), bytes.size()) !=
        tinyxml2::XML_SUCCESS) {
        return nullptr;
    }
    const tinyxml2::XMLElement* font_element =
        FindDescendant(document.RootElement(), "font");
    if (!font_element) return nullptr;

    auto font = std::make_unique<SVGFont>();
    const tinyxml2::XMLElement* font_face = FindDescendant(font_element, "font-face");
    font->units_per_em = NumericAttribute(font_face, "units-per-em", 360.0);
    if (font->units_per_em <= 0.0) font->units_per_em = 360.0;
    font->ascent = NumericAttribute(font_face, "ascent", font->units_per_em);
    font->descent = NumericAttribute(font_face, "descent", 0.0);
    font->default_advance = NumericAttribute(
        font_element, "horiz-adv-x", font->units_per_em);

    size_t total_segments = 0;
    for (const tinyxml2::XMLElement* glyph = font_element->FirstChildElement();
         glyph; glyph = glyph->NextSiblingElement()) {
        if (LocalName(glyph->Name()) != "glyph") continue;
        if (font->glyphs.size() >= kMaxSVGGlyphs) return nullptr;
        const char* unicode = glyph->Attribute("unicode");
        const char* path = glyph->Attribute("d");
        if (!unicode || !path) continue;
        size_t processed = 0;
        uint32_t codepoint = utf::DecodeUTF8ToCodePoint(
            reinterpret_cast<const uint8_t*>(unicode), std::strlen(unicode), &processed);
        if (processed == 0 || codepoint == 0) continue;
        SVGGlyph parsed;
        parsed.advance = NumericAttribute(glyph, "horiz-adv-x", font->default_advance);
        if (!ParsePath(path, parsed.edges)) continue;
        total_segments += parsed.edges.size();
        if (total_segments > kMaxSVGSegments) return nullptr;
        font->glyphs.emplace(codepoint, std::move(parsed));
    }
    return font->glyphs.empty() ? nullptr : std::move(font);
}

std::string TrimUpper(std::string_view value) {
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    std::string result(value.substr(begin, end - begin));
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return result;
}

bool ParseHex(std::string_view value, uint32_t& result) {
    if (value.empty() || value.size() > 6) return false;
    result = 0;
    for (char ch : value) {
        uint32_t digit = 0;
        if (ch >= '0' && ch <= '9') digit = static_cast<uint32_t>(ch - '0');
        else if (ch >= 'A' && ch <= 'F') digit = static_cast<uint32_t>(ch - 'A' + 10);
        else return false;
        result = (result << 4) | digit;
    }
    return result <= 0x10FFFF;
}

bool UnicodeRangeContains(std::string_view ranges, uint32_t codepoint) {
    if (ranges.empty()) return true;
    size_t position = 0;
    while (position <= ranges.size()) {
        size_t comma = ranges.find(',', position);
        std::string token = TrimUpper(ranges.substr(
            position, comma == std::string_view::npos ? ranges.size() - position
                                                       : comma - position));
        if (token.rfind("U+", 0) == 0) {
            std::string_view body(token.data() + 2, token.size() - 2);
            size_t wildcard = body.find('?');
            if (wildcard != std::string_view::npos) {
                bool valid = !body.empty() && body.size() <= 6;
                for (size_t i = wildcard; i < body.size(); ++i) {
                    if (body[i] != '?') valid = false;
                }
                uint32_t prefix = 0;
                bool parsed_prefix = wildcard == 0 ||
                                     ParseHex(body.substr(0, wildcard), prefix);
                if (valid && parsed_prefix) {
                    size_t wildcard_count = body.size() - wildcard;
                    uint32_t low = prefix << (wildcard_count * 4);
                    uint32_t high = low | ((1u << (wildcard_count * 4)) - 1u);
                    if (codepoint >= low && codepoint <= high) return true;
                }
            } else {
                size_t dash = body.find('-');
                uint32_t low = 0, high = 0;
                if (dash == std::string_view::npos) {
                    if (ParseHex(body, low) && codepoint == low) return true;
                } else if (ParseHex(body.substr(0, dash), low) &&
                           ParseHex(body.substr(dash + 1), high) &&
                           codepoint >= low && codepoint <= high) {
                    return true;
                }
            }
        }
        if (comma == std::string_view::npos) break;
        position = comma + 1;
    }
    return false;
}

Bitmap RasterizeSVG(const SVGFont& font, const SVGGlyph& glyph,
                    int width, int height, ColorRGBA color) {
    Bitmap bitmap(width, height, PixelFormat::kRGBA8888);
    double em_height = font.ascent + std::abs(font.descent);
    if (em_height <= 0.0) em_height = font.units_per_em;

    std::vector<Edge> edges;
    edges.reserve(glyph.edges.size());
    for (const Edge& edge : glyph.edges) {
        edges.push_back({
            {edge.from.x / font.units_per_em * width,
             (font.ascent - edge.from.y) / em_height * height},
            {edge.to.x / font.units_per_em * width,
             (font.ascent - edge.to.y) / em_height * height},
        });
    }

    std::vector<uint8_t> coverage(static_cast<size_t>(width) * height, 0);
    struct Intersection { double x; int delta; };
    std::vector<Intersection> intersections;
    intersections.reserve(edges.size());
    for (int sample_y_index = 0; sample_y_index < height * kSupersample;
         ++sample_y_index) {
        double sample_y = (sample_y_index + 0.5) / kSupersample;
        intersections.clear();
        for (const Edge& edge : edges) {
            double low = std::min(edge.from.y, edge.to.y);
            double high = std::max(edge.from.y, edge.to.y);
            if (edge.from.y == edge.to.y || sample_y < low || sample_y >= high) continue;
            double ratio = (sample_y - edge.from.y) / (edge.to.y - edge.from.y);
            intersections.push_back({
                edge.from.x + ratio * (edge.to.x - edge.from.x),
                edge.to.y > edge.from.y ? 1 : -1,
            });
        }
        std::sort(intersections.begin(), intersections.end(),
                  [](const Intersection& first, const Intersection& second) {
                      return first.x < second.x;
                  });
        int winding = 0;
        size_t intersection_index = 0;
        for (int sample_x_index = 0; sample_x_index < width * kSupersample;
             ++sample_x_index) {
            double sample_x = (sample_x_index + 0.5) / kSupersample;
            while (intersection_index < intersections.size() &&
                   intersections[intersection_index].x <= sample_x) {
                winding += intersections[intersection_index].delta;
                ++intersection_index;
            }
            if (winding != 0) {
                size_t pixel = static_cast<size_t>(sample_y_index / kSupersample) * width +
                               sample_x_index / kSupersample;
                ++coverage[pixel];
            }
        }
    }

    constexpr int kSamplesPerPixel = kSupersample * kSupersample;
    for (int y = 0; y < height; ++y) {
        ColorRGBA* destination = bitmap.GetPixelAt(0, y);
        for (int x = 0; x < width; ++x) {
            uint8_t alpha = static_cast<uint8_t>(
                static_cast<unsigned>(coverage[static_cast<size_t>(y) * width + x]) *
                color.a / kSamplesPerPixel);
            destination[x] = alpha == 0 ? ColorRGBA(0) : ColorRGBA(color, alpha);
        }
    }
    return bitmap;
}

void DrawSVGGlyph(Bitmap& target, const SVGFont& font, const SVGGlyph& glyph,
                  int x, int y, CharStyle style, ColorRGBA color,
                  ColorRGBA stroke_color, float stroke_width,
                  int width, int height, std::optional<UnderlineInfo> underline_info) {
    Canvas canvas(target);
    if ((style & kCharStyleStroke) && stroke_width > 0.0f) {
        Bitmap stroke = RasterizeSVG(font, glyph, width, height, stroke_color);
        int radius = std::max(1, static_cast<int>(std::ceil(stroke_width)));
        for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
            for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
                if (offset_x == 0 && offset_y == 0) continue;
                if (offset_x * offset_x + offset_y * offset_y > radius * radius) continue;
                canvas.DrawBitmap(stroke, x + offset_x, y + offset_y);
            }
        }
    }
    Bitmap fill = RasterizeSVG(font, glyph, width, height, color);
    canvas.DrawBitmap(fill, x, y);
    if ((style & kCharStyleUnderline) && underline_info) {
        int thickness = std::max(1, height / 24);
        canvas.DrawRect(color, Rect(underline_info->start_x, y + height - thickness,
                                    underline_info->start_x + underline_info->width,
                                    y + height));
    }
}

}  // namespace

struct B62FontRenderer::Impl {
    explicit Impl(std::shared_ptr<Logger> value) : logger(std::move(value)) {}

    std::shared_ptr<Logger> logger;
    std::shared_ptr<const B62DocumentSidecar> sidecar;
    std::unordered_map<const B62ResourceBlob*, std::unique_ptr<SVGFont>> svg_fonts;
    std::unordered_set<const B62ResourceBlob*> invalid_svg_fonts;
};

B62FontRenderer::B62FontRenderer(Context& context)
    : pimpl_(std::make_unique<Impl>(GetContextLogger(context))) {}

B62FontRenderer::~B62FontRenderer() = default;

void B62FontRenderer::SetDocumentSidecar(
    std::shared_ptr<const B62DocumentSidecar> sidecar,
    TextRenderer& text_renderer) {
    if (pimpl_->sidecar.get() == sidecar.get()) return;
    pimpl_->sidecar = std::move(sidecar);
    pimpl_->svg_fonts.clear();
    pimpl_->invalid_svg_fonts.clear();
    text_renderer.ClearEmbeddedFonts();
}

auto B62FontRenderer::DrawChar(
    TextRenderer& text_renderer, TextRenderContext& render_context,
    int x, int y, uint32_t codepoint, CharStyle style,
    ColorRGBA color, ColorRGBA stroke_color, float stroke_width,
    int char_width, int char_height, float aspect_ratio,
    std::optional<UnderlineInfo> underline_info) -> DrawStatus {
    if (!pimpl_->sidecar || char_width <= 0 || char_height <= 0) {
        return DrawStatus::kNotFound;
    }

    for (const B62FontFace& face : pimpl_->sidecar->font_faces()) {
        if (!UnicodeRangeContains(face.unicode_range, codepoint)) continue;
        for (const B62FontSource& source : face.sources) {
            const std::shared_ptr<const B62ResourceBlob>& resource = source.resource.resolved;
            if (!resource || !resource->bytes || resource->bytes->empty()) continue;
            if (source.format == B62FontFormat::kSVG) {
                SVGFont* svg_font = nullptr;
                auto cached = pimpl_->svg_fonts.find(resource.get());
                if (cached != pimpl_->svg_fonts.end()) {
                    svg_font = cached->second.get();
                } else if (!pimpl_->invalid_svg_fonts.count(resource.get())) {
                    std::unique_ptr<SVGFont> parsed = ParseSVGFont(*resource->bytes);
                    if (parsed) {
                        svg_font = parsed.get();
                        pimpl_->svg_fonts.emplace(resource.get(), std::move(parsed));
                    } else {
                        pimpl_->logger->w("B62FontRenderer: invalid SVG font resource %u",
                                          resource->index);
                        pimpl_->invalid_svg_fonts.insert(resource.get());
                    }
                }
                if (!svg_font) continue;
                auto glyph = svg_font->glyphs.find(codepoint);
                if (glyph == svg_font->glyphs.end()) continue;
                DrawSVGGlyph(render_context.GetBitmap(), *svg_font, glyph->second,
                             x, y, style, color, stroke_color, stroke_width,
                             char_width, char_height, underline_info);
                return DrawStatus::kOK;
            }
            if (source.format == B62FontFormat::kWOFF) {
                TextRenderStatus status = text_renderer.DrawCharFromEmbeddedFont(
                    render_context, resource->bytes, x, y, codepoint, style,
                    color, stroke_color, stroke_width, char_width, char_height,
                    aspect_ratio, underline_info);
                if (status == TextRenderStatus::kOK) return DrawStatus::kOK;
                if (status != TextRenderStatus::kCodePointNotFound &&
                    status != TextRenderStatus::kFontNotFound) {
                    return DrawStatus::kError;
                }
            }
        }
    }
    return DrawStatus::kNotFound;
}

}  // namespace aribcaption
