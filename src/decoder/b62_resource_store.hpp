/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_RESOURCE_STORE_HPP
#define ARIBCAPTION_B62_RESOURCE_STORE_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "aribcaption/b62_decoder.hpp"
#include "base/logger.hpp"

namespace aribcaption::internal {

class B62ResourceStore {
public:
    struct Resource {
        std::shared_ptr<const std::vector<uint8_t>> data;
        std::string mime_type;
    };
    using Scope = std::unordered_map<uint32_t, Resource>;

    struct Snapshot {
        uint64_t scope_id = 0;
        uint64_t active_scope_id = 0;
        bool has_active_scope = false;
        bool had_scope = false;
        size_t total_bytes = 0;
        Scope scope;
        Scope transient_scope;
        std::deque<uint64_t> scope_order;
    };

    explicit B62ResourceStore(std::shared_ptr<Logger> logger);

    void Reset();
    void Deactivate();
    [[nodiscard]] Snapshot Capture(uint64_t scope_id) const;
    void Restore(Snapshot&& snapshot);
    bool Store(const B62ResourceContextView& resource_context);
    void EnforceLimits(uint64_t protected_scope_id);

    [[nodiscard]] const Resource* FindActive(uint32_t index) const;
    [[nodiscard]] uint64_t active_scope_id() const { return active_scope_id_; }
    [[nodiscard]] bool has_active_scope() const { return has_active_scope_; }

private:
    void Erase(uint64_t scope_id);

    std::shared_ptr<Logger> logger_;
    std::unordered_map<uint64_t, Scope> scopes_;
    std::deque<uint64_t> scope_order_;
    Scope transient_scope_;
    uint64_t active_scope_id_ = 0;
    bool has_active_scope_ = false;
    size_t total_bytes_ = 0;
};

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_RESOURCE_STORE_HPP
