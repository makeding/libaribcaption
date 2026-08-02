/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_RESOURCE_RESOLVER_HPP
#define ARIBCAPTION_B62_RESOURCE_RESOLVER_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "aribcaption/b62_document.hpp"
#include "base/tinyxml2.h"
#include "decoder/b62_resource_store.hpp"

namespace aribcaption::internal {

class B62ResourceResolver {
public:
    B62ResourceResolver(const B62ResourceStore& resource_store,
                        const tinyxml2::XMLElement* document_root,
                        bool collect_embedded_images);

    B62ResourceReference Resolve(const char* uri_value,
                                 B62ResourceKind expected_kind);

private:
    const B62ResourceStore& resource_store_;
    std::unordered_map<std::string, std::shared_ptr<const B62ResourceBlob>> embedded_images_;
    std::unordered_map<uint64_t, std::shared_ptr<const B62ResourceBlob>> resolved_cache_;
};

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_RESOURCE_RESOLVER_HPP
