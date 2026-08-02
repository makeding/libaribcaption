/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_resource_resolver.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "decoder/b62_text_util.hpp"
#include "decoder/b62_xml.hpp"

namespace aribcaption::internal {
namespace {

constexpr size_t kMaxEmbeddedResourceBytes = 16u * 1024u * 1024u;

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

B62ResourceKind InferResourceKind(const B62ResourceStore::Resource& resource,
                                  B62ResourceKind expected_kind) {
    if (expected_kind != B62ResourceKind::kUnknown) {
        return expected_kind;
    }
    if (resource.mime_type == "image/png") {
        return B62ResourceKind::kPNGImage;
    }
    if (resource.mime_type == "image/svg+xml") {
        return B62ResourceKind::kSVGImage;
    }
    return B62ResourceKind::kUnknown;
}

}  // namespace

B62ResourceResolver::B62ResourceResolver(const B62ResourceStore& resource_store,
                                         const tinyxml2::XMLElement* document_root,
                                         bool collect_embedded_images)
    : resource_store_(resource_store) {
    if (!collect_embedded_images) {
        return;
    }
    std::vector<const tinyxml2::XMLElement*> image_elements;
    B62CollectDescendants(document_root, "image", image_elements);
    for (const tinyxml2::XMLElement* image : image_elements) {
        if (!B62IsSMPTEElement(image, "image")) {
            continue;
        }
        const char* id = B62FindXMLID(image);
        const char* payload = image->GetText();
        std::string encoding = B62TrimASCII(B62FindAttribute(image, "encoding")
            ? B62FindAttribute(image, "encoding") : "base64");
        std::string image_type = B62TrimASCII(B62FindAttribute(image, "imageType")
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
        embedded_images_.emplace(id, std::move(blob));
    }
}

B62ResourceReference B62ResourceResolver::Resolve(
    const char* uri_value,
    B62ResourceKind expected_kind) {
    B62ResourceReference reference;
    if (!uri_value) {
        return reference;
    }
    reference.uri = B62TrimASCII(uri_value);
    if (!reference.uri.empty() && reference.uri.front() == '#') {
        auto embedded = embedded_images_.find(reference.uri.substr(1));
        if (embedded != embedded_images_.end()) {
            reference.resolved = embedded->second;
        }
        return reference;
    }
    auto index = ParseSubtResourceIndex(reference.uri);
    if (!index || !resource_store_.has_active_scope()) {
        return reference;
    }
    const B62ResourceStore::Resource* resource = resource_store_.FindActive(*index);
    if (!resource) {
        return reference;
    }
    B62ResourceKind resolved_kind = InferResourceKind(*resource, expected_kind);
    const uint64_t cache_key = (static_cast<uint64_t>(resolved_kind) << 32) | *index;
    auto cached = resolved_cache_.find(cache_key);
    if (cached != resolved_cache_.end()) {
        reference.resolved = cached->second;
        return reference;
    }
    auto blob = std::make_shared<B62ResourceBlob>();
    blob->scope_id = resource_store_.active_scope_id();
    blob->index = *index;
    blob->kind = resolved_kind;
    blob->mime_type = resource->mime_type;
    blob->bytes = resource->data;
    reference.resolved = blob;
    resolved_cache_.emplace(cache_key, std::move(blob));
    return reference;
}

}  // namespace aribcaption::internal
