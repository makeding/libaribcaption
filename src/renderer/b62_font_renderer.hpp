/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * This file is part of libaribcaption.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_FONT_RENDERER_HPP
#define ARIBCAPTION_B62_FONT_RENDERER_HPP

#include <cstdint>
#include <memory>
#include <optional>

#include "aribcaption/b62_document.hpp"
#include "aribcaption/caption.hpp"
#include "aribcaption/color.hpp"
#include "aribcaption/context.hpp"
#include "renderer/text_renderer.hpp"

namespace aribcaption {

class B62FontRenderer {
public:
    enum class DrawStatus {
        kOK,
        kNotFound,
        kError,
    };

    explicit B62FontRenderer(Context& context);
    ~B62FontRenderer();

    void SetDocumentSidecar(std::shared_ptr<const B62DocumentSidecar> sidecar,
                            TextRenderer& text_renderer);

    auto DrawChar(TextRenderer& text_renderer,
                  TextRenderContext& render_context,
                  int x, int y, uint32_t codepoint, CharStyle style,
                  ColorRGBA color, ColorRGBA stroke_color, float stroke_width,
                  int char_width, int char_height, float aspect_ratio,
                  std::optional<UnderlineInfo> underline_info) -> DrawStatus;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

}  // namespace aribcaption

#endif  // ARIBCAPTION_B62_FONT_RENDERER_HPP
