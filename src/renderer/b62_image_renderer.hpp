/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_IMAGE_RENDERER_HPP
#define ARIBCAPTION_B62_IMAGE_RENDERER_HPP

#include <optional>

#include "aribcaption/b62_document.hpp"
#include "aribcaption/context.hpp"
#include "renderer/bitmap.hpp"

namespace aribcaption::internal {

class B62ImageRenderer {
public:
    explicit B62ImageRenderer(Context& context);

    std::optional<Bitmap> Render(const B62ResourceBlob& resource,
                                 int width,
                                 int height);

private:
    std::optional<Bitmap> RenderPNG(const B62ResourceBlob& resource,
                                    int width,
                                    int height);
    std::optional<Bitmap> RenderSVG(const B62ResourceBlob& resource,
                                    int width,
                                    int height);

    std::shared_ptr<Logger> log_;
};

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_IMAGE_RENDERER_HPP
