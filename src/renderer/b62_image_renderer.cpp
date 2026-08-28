/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "renderer/b62_image_renderer.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <vector>

#include <png.h>
#include <plutosvg.h>

#include "base/logger.hpp"

namespace aribcaption::internal {
namespace {

constexpr int kMaximumImageDimension = 16384;

Bitmap ScaleRGBA(const uint8_t* rgba,
                 int source_width,
                 int source_height,
                 int target_width,
                 int target_height) {
    Bitmap output(target_width, target_height, PixelFormat::kRGBA8888);
    for (int y = 0; y < target_height; ++y) {
        const int source_y = std::min(
            static_cast<int>((static_cast<int64_t>(y) * source_height) / target_height),
            source_height - 1);
        for (int x = 0; x < target_width; ++x) {
            const int source_x = std::min(
                static_cast<int>((static_cast<int64_t>(x) * source_width) / target_width),
                source_width - 1);
            const uint8_t* source = rgba +
                (static_cast<size_t>(source_y) * source_width + source_x) * 4;
            std::memcpy(output.GetPixelAt(x, y), source, 4);
        }
    }
    return output;
}

}  // namespace

B62ImageRenderer::B62ImageRenderer(Context& context)
    : log_(GetContextLogger(context)) {}

std::optional<Bitmap> B62ImageRenderer::Render(const B62ResourceBlob& resource,
                                               int width,
                                               int height) {
    if (width <= 0 || height <= 0 ||
        width > kMaximumImageDimension || height > kMaximumImageDimension ||
        !resource.bytes || resource.bytes->empty()) {
        log_->e("B62ImageRenderer: invalid image dimensions or empty resource %u",
                resource.index);
        return std::nullopt;
    }
    if (resource.kind == B62ResourceKind::kPNGImage) {
        return RenderPNG(resource, width, height);
    }
    if (resource.kind == B62ResourceKind::kSVGImage) {
        return RenderSVG(resource, width, height);
    }
    log_->e("B62ImageRenderer: unsupported image resource kind %d for resource %u",
            static_cast<int>(resource.kind), resource.index);
    return std::nullopt;
}

std::optional<Bitmap> B62ImageRenderer::RenderPNG(const B62ResourceBlob& resource,
                                                  int width,
                                                  int height) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image,
                                          resource.bytes->data(),
                                          resource.bytes->size())) {
        log_->e("B62ImageRenderer: invalid PNG resource %u: %s",
                resource.index, image.message);
        return std::nullopt;
    }
    if (image.width == 0 || image.height == 0 ||
        image.width > static_cast<png_uint_32>(kMaximumImageDimension) ||
        image.height > static_cast<png_uint_32>(kMaximumImageDimension)) {
        log_->e("B62ImageRenderer: PNG resource %u has unsupported dimensions %ux%u",
                resource.index, image.width, image.height);
        png_image_free(&image);
        return std::nullopt;
    }

    image.format = PNG_FORMAT_RGBA;
    std::vector<uint8_t> rgba(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr)) {
        log_->e("B62ImageRenderer: failed to decode PNG resource %u: %s",
                resource.index, image.message);
        png_image_free(&image);
        return std::nullopt;
    }
    const int source_width = static_cast<int>(image.width);
    const int source_height = static_cast<int>(image.height);
    png_image_free(&image);
    return ScaleRGBA(rgba.data(), source_width, source_height, width, height);
}

std::optional<Bitmap> B62ImageRenderer::RenderSVG(const B62ResourceBlob& resource,
                                                  int width,
                                                  int height) {
    if (resource.bytes->size() > static_cast<size_t>(INT_MAX)) {
        log_->e("B62ImageRenderer: SVG resource %u is too large", resource.index);
        return std::nullopt;
    }
    const char* data = reinterpret_cast<const char*>(resource.bytes->data());
    plutosvg_document_t* document = plutosvg_document_load_from_data(
        data, static_cast<int>(resource.bytes->size()),
        static_cast<float>(width), static_cast<float>(height), nullptr, nullptr);
    if (!document) {
        log_->e("B62ImageRenderer: invalid SVG resource %u", resource.index);
        return std::nullopt;
    }
    plutovg_surface_t* surface = plutosvg_document_render_to_surface(
        document, nullptr, width, height, nullptr, nullptr, nullptr);
    plutosvg_document_destroy(document);
    if (!surface) {
        log_->e("B62ImageRenderer: failed to render SVG resource %u", resource.index);
        return std::nullopt;
    }

    Bitmap output(width, height, PixelFormat::kRGBA8888);
    const uint8_t* source = plutovg_surface_get_data(surface);
    const int stride = plutovg_surface_get_stride(surface);
    for (int y = 0; y < height; ++y) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(source + y * stride);
        for (int x = 0; x < width; ++x) {
            const uint32_t argb = row[x];
            const unsigned alpha = (argb >> 24) & 0xff;
            const auto unpremultiply = [alpha](unsigned component) -> uint8_t {
                if (alpha == 0) {
                    return 0;
                }
                return static_cast<uint8_t>(std::min(
                    255U, (component * 255U + alpha / 2U) / alpha));
            };
            *output.GetPixelAt(x, y) = ColorRGBA(
                unpremultiply((argb >> 16) & 0xff),
                unpremultiply((argb >> 8) & 0xff),
                unpremultiply(argb & 0xff),
                static_cast<uint8_t>(alpha));
        }
    }
    plutovg_surface_destroy(surface);
    return output;
}

}  // namespace aribcaption::internal
