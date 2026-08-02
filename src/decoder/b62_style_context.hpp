/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_STYLE_CONTEXT_HPP
#define ARIBCAPTION_B62_STYLE_CONTEXT_HPP

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/tinyxml2.h"
#include "decoder/b62_document_model.hpp"

namespace aribcaption::internal {

class B62StyleContext {
public:
    B62StyleContext(const tinyxml2::XMLElement* document_root,
                    const std::array<int, 2>& plane);
    B62StyleContext(const B62StyleContext&) = delete;
    B62StyleContext& operator=(const B62StyleContext&) = delete;
    B62StyleContext(B62StyleContext&&) = delete;
    B62StyleContext& operator=(B62StyleContext&&) = delete;

    [[nodiscard]] const std::unordered_map<std::string, B62RegionDefinition>&
    regions() const noexcept {
        return region_definitions_;
    }

    B62Style CollectInheritedStyle(const tinyxml2::XMLElement* element,
                                   const B62Style& region_style);

    void AppendInlineSpans(const tinyxml2::XMLElement* parent,
                           const B62Style& inherited_style,
                           std::vector<B62InlineSpan>& spans,
                           const B62RegionDefinition* inherited_region,
                           bool preserve_document_layout,
                           bool inherited_is_ruby);

    static void ResolveLegacyRuby(std::vector<B62InlineSpan>& spans);
    static bool HasARIBRubyAncestor(const tinyxml2::XMLElement* element);

private:
    void ApplyStyleAttributes(const tinyxml2::XMLElement* element,
                              B62Style& style) const;
    void ApplyStyleReferences(const tinyxml2::XMLElement* element,
                              std::vector<std::string>& resolving,
                              B62Style& style);
    B62Style ResolveStyleReference(const std::string& id,
                                   std::vector<std::string>& resolving);
    B62Style MergeNodeStyle(const tinyxml2::XMLElement* element,
                            B62Style base);

    std::unordered_map<std::string, const tinyxml2::XMLElement*> style_nodes_;
    std::unordered_map<std::string, B62Style> style_cache_;
    // Built completely by the constructor and never mutated afterwards, so
    // B62InlineSpan::region pointers remain valid for the context lifetime.
    std::unordered_map<std::string, B62RegionDefinition> region_definitions_;
};

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_STYLE_CONTEXT_HPP
