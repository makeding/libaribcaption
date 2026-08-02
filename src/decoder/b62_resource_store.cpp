/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_resource_store.hpp"

#include <algorithm>
#include <utility>

namespace aribcaption::internal {
namespace {

constexpr size_t kMaxResourceScopes = 32;
constexpr size_t kMaxResourcesPerScope = 256;
constexpr size_t kMaxResourceBytes = 16u * 1024u * 1024u;
constexpr size_t kMaxResourceScopeBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxTotalResourceBytes = 128u * 1024u * 1024u;
constexpr size_t kMaxResourceMimeLength = 255;

}  // namespace

B62ResourceStore::B62ResourceStore(std::shared_ptr<Logger> logger)
    : logger_(std::move(logger)) {}

void B62ResourceStore::Reset() {
    scopes_.clear();
    scope_order_.clear();
    transient_scope_.clear();
    active_scope_id_ = 0;
    has_active_scope_ = false;
    total_bytes_ = 0;
}

void B62ResourceStore::Deactivate() {
    transient_scope_.clear();
    active_scope_id_ = 0;
    has_active_scope_ = false;
}

B62ResourceStore::Snapshot B62ResourceStore::Capture(uint64_t scope_id) const {
    Snapshot snapshot;
    snapshot.scope_id = scope_id;
    snapshot.active_scope_id = active_scope_id_;
    snapshot.has_active_scope = has_active_scope_;
    snapshot.total_bytes = total_bytes_;
    snapshot.scope_order = scope_order_;
    if (scope_id == 0) {
        snapshot.transient_scope = transient_scope_;
    } else {
        auto existing = scopes_.find(scope_id);
        if (existing != scopes_.end()) {
            snapshot.had_scope = true;
            snapshot.scope = existing->second;
        }
    }
    return snapshot;
}

void B62ResourceStore::Restore(Snapshot&& snapshot) {
    if (snapshot.scope_id == 0) {
        transient_scope_ = std::move(snapshot.transient_scope);
    } else if (snapshot.had_scope) {
        scopes_[snapshot.scope_id] = std::move(snapshot.scope);
    } else {
        scopes_.erase(snapshot.scope_id);
    }
    scope_order_ = std::move(snapshot.scope_order);
    active_scope_id_ = snapshot.active_scope_id;
    has_active_scope_ = snapshot.has_active_scope;
    total_bytes_ = snapshot.total_bytes;
}

bool B62ResourceStore::Store(const B62ResourceContextView& resource_context) {
    if ((resource_context.resource_count != 0 && !resource_context.resources) ||
        resource_context.resource_count > kMaxResourcesPerScope) {
        logger_->e("B62ResourceStore: invalid resource context");
        return false;
    }

    Scope staged;
    size_t staged_bytes = 0;
    for (size_t i = 0; i < resource_context.resource_count; ++i) {
        const B62ResourceView& view = resource_context.resources[i];
        if ((view.size != 0 && !view.data) || view.size > kMaxResourceBytes ||
            staged.count(view.index)) {
            logger_->e("B62ResourceStore: invalid resource view at index %zu", i);
            return false;
        }
        size_t mime_length = 0;
        if (view.mime_type) {
            while (mime_length <= kMaxResourceMimeLength && view.mime_type[mime_length] != '\0') {
                ++mime_length;
            }
            if (mime_length > kMaxResourceMimeLength) {
                logger_->e("B62ResourceStore: resource MIME type is too long");
                return false;
            }
        }
        if (staged_bytes > kMaxResourceScopeBytes - view.size) {
            logger_->e("B62ResourceStore: resource scope is too large");
            return false;
        }
        Resource owned;
        owned.data = view.size != 0
            ? std::make_shared<const std::vector<uint8_t>>(view.data, view.data + view.size)
            : std::make_shared<const std::vector<uint8_t>>();
        if (view.mime_type) {
            owned.mime_type.assign(view.mime_type, mime_length);
        }
        staged_bytes += view.size;
        staged.emplace(view.index, std::move(owned));
    }

    if (resource_context.scope_id == 0) {
        transient_scope_ = std::move(staged);
        active_scope_id_ = 0;
        has_active_scope_ = true;
        return true;
    }

    auto existing = scopes_.find(resource_context.scope_id);
    size_t existing_bytes = 0;
    size_t replaced_bytes = 0;
    size_t existing_count = 0;
    if (existing != scopes_.end()) {
        existing_count = existing->second.size();
        for (const auto& [index, resource] : existing->second) {
            existing_bytes += resource.data->size();
            if (staged.count(index)) {
                replaced_bytes += resource.data->size();
                --existing_count;
            }
        }
    }
    if (existing_count + staged.size() > kMaxResourcesPerScope ||
        existing_bytes - replaced_bytes > kMaxResourceScopeBytes - staged_bytes) {
        logger_->e("B62ResourceStore: merged resource scope is too large");
        return false;
    }
    const size_t final_scope_bytes = existing_bytes - replaced_bytes + staged_bytes;
    Scope merged = existing != scopes_.end() ? existing->second : Scope{};
    for (auto& [index, resource] : staged) {
        merged[index] = std::move(resource);
    }
    scopes_[resource_context.scope_id] = std::move(merged);
    total_bytes_ = total_bytes_ - existing_bytes + final_scope_bytes;
    scope_order_.erase(
        std::remove(scope_order_.begin(), scope_order_.end(), resource_context.scope_id),
        scope_order_.end());
    scope_order_.push_back(resource_context.scope_id);
    active_scope_id_ = resource_context.scope_id;
    has_active_scope_ = true;
    return true;
}

void B62ResourceStore::Erase(uint64_t scope_id) {
    auto scope = scopes_.find(scope_id);
    if (scope != scopes_.end()) {
        for (const auto& [index, resource] : scope->second) {
            (void)index;
            total_bytes_ -= resource.data->size();
        }
        scopes_.erase(scope);
    }
    scope_order_.erase(
        std::remove(scope_order_.begin(), scope_order_.end(), scope_id),
        scope_order_.end());
    if (active_scope_id_ == scope_id) {
        active_scope_id_ = 0;
        has_active_scope_ = false;
    }
}

void B62ResourceStore::EnforceLimits(uint64_t protected_scope_id) {
    while ((scopes_.size() > kMaxResourceScopes || total_bytes_ > kMaxTotalResourceBytes) &&
           scope_order_.size() > 1) {
        uint64_t evict_id = scope_order_.front();
        scope_order_.pop_front();
        if (evict_id == protected_scope_id) {
            scope_order_.push_back(evict_id);
            continue;
        }
        auto evict = scopes_.find(evict_id);
        if (evict == scopes_.end()) {
            continue;
        }
        for (const auto& [index, resource] : evict->second) {
            (void)index;
            total_bytes_ -= resource.data->size();
        }
        scopes_.erase(evict);
    }
}

const B62ResourceStore::Resource* B62ResourceStore::FindActive(uint32_t index) const {
    if (!has_active_scope_) {
        return nullptr;
    }
    const Scope* scope = nullptr;
    if (active_scope_id_ == 0) {
        scope = &transient_scope_;
    } else {
        auto found = scopes_.find(active_scope_id_);
        if (found != scopes_.end()) {
            scope = &found->second;
        }
    }
    if (!scope) {
        return nullptr;
    }
    auto resource = scope->find(index);
    return resource == scope->end() ? nullptr : &resource->second;
}

}  // namespace aribcaption::internal
