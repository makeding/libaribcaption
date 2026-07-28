/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_presentation_state.hpp"

#include <algorithm>
#include <iterator>
#include <limits>

namespace aribcaption::internal {
namespace {

constexpr int64_t kPruneWindowMilliseconds = 30000;
constexpr size_t kMaxStoredNodes = 300;

bool PresentationLess(const B62Presentation& lhs, const B62Presentation& rhs) {
    if (lhs.start != rhs.start) {
        return lhs.start < rhs.start;
    }
    return lhs.event_id < rhs.event_id;
}

}  // namespace

void B62PresentationState::Reset() {
    presentations_.clear();
    next_event_id_ = 1;
}

bool B62PresentationState::ApplyContinuation(const std::string& id,
                                             std::optional<int64_t> end,
                                             bool close_with_default_duration) {
    if (id.empty()) {
        return false;
    }
    for (auto presentation = presentations_.rbegin(); presentation != presentations_.rend(); ++presentation) {
        auto node = std::find_if(presentation->nodes.rbegin(), presentation->nodes.rend(),
                                 [&](const B62PresentationNode& candidate) {
                                     return candidate.id == id && candidate.indefinite;
                                 });
        if (node == presentation->nodes.rend()) {
            continue;
        }
        if (end) {
            node->end = *end;
            node->indefinite = false;
        } else if (close_with_default_duration && node->start != PTS_NOPTS) {
            node->end = node->start + 5000;
            node->indefinite = false;
        }
        return true;
    }
    return false;
}

void B62PresentationState::Commit(B62Presentation presentation) {
    if (presentation.start == PTS_NOPTS) {
        for (const B62PresentationNode& node : presentation.nodes) {
            if (node.start != PTS_NOPTS &&
                (presentation.start == PTS_NOPTS || node.start < presentation.start)) {
                presentation.start = node.start;
            }
        }
    }
    if (presentation.start == PTS_NOPTS) {
        return;
    }

    presentation.event_id = next_event_id_++;
    presentations_.erase(
        std::remove_if(presentations_.begin(), presentations_.end(),
                       [&](const B62Presentation& current) {
                           return current.start == presentation.start;
                       }),
        presentations_.end());
    presentations_.push_back(std::move(presentation));
    std::sort(presentations_.begin(), presentations_.end(), PresentationLess);
    EnforceNodeLimit();
}

void B62PresentationState::Prune(int64_t current_pts) {
    if (current_pts == PTS_NOPTS || presentations_.size() < 2) {
        return;
    }
    const int64_t keep_from = current_pts - kPruneWindowMilliseconds;
    auto first_recent = std::lower_bound(
        presentations_.begin(), presentations_.end(), keep_from,
        [](const B62Presentation& presentation, int64_t pts) {
            return presentation.start < pts;
        });
    if (first_recent == presentations_.begin()) {
        return;
    }

    auto predecessor = std::prev(first_recent);
    presentations_.erase(presentations_.begin(), predecessor);
    B62Presentation& barrier = presentations_.front();
    const bool expired = std::all_of(barrier.nodes.begin(), barrier.nodes.end(),
                                     [&](const B62PresentationNode& node) {
                                         return node.end && *node.end < keep_from;
                                     });
    if (expired) {
        barrier.nodes.clear();
    }
}

void B62PresentationState::BuildScenes(int64_t current_pts,
                                       size_t max_events,
                                       std::vector<Caption>& out_captions) const {
    out_captions.clear();
    if (presentations_.empty() || max_events == 0) {
        return;
    }

    std::vector<int64_t> boundaries;
    for (size_t presentation_index = 0; presentation_index < presentations_.size(); ++presentation_index) {
        const B62Presentation& presentation = presentations_[presentation_index];
        const int64_t next_start = presentation_index + 1 < presentations_.size()
            ? presentations_[presentation_index + 1].start
            : std::numeric_limits<int64_t>::max();
        boundaries.push_back(presentation.start);
        for (const B62PresentationNode& node : presentation.nodes) {
            if (node.start != PTS_NOPTS && node.start >= presentation.start && node.start < next_start) {
                boundaries.push_back(node.start);
            }
            if (node.end && *node.end >= presentation.start && *node.end < next_start) {
                boundaries.push_back(*node.end);
            }
        }
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    size_t first_boundary = 0;
    if (current_pts != PTS_NOPTS) {
        const auto after_current = std::upper_bound(boundaries.begin(), boundaries.end(), current_pts);
        if (after_current != boundaries.begin()) {
            first_boundary = static_cast<size_t>(std::distance(boundaries.begin(), after_current) - 1);
        }
    }
    const size_t last_boundary = std::min(boundaries.size(), first_boundary + max_events);
    for (size_t boundary_index = first_boundary; boundary_index < last_boundary; ++boundary_index) {
        const int64_t pts = boundaries[boundary_index];
        const B62Presentation* selected = nullptr;
        for (const B62Presentation& presentation : presentations_) {
            if (presentation.start > pts) {
                break;
            }
            selected = &presentation;
        }
        if (!selected) {
            continue;
        }

        Caption scene;
        scene.flags = kCaptionFlagsClearScreen;
        scene.pts = pts;
        scene.plane_width = selected->plane_width;
        scene.plane_height = selected->plane_height;
        scene.iso6392_language_code = selected->language;
        for (const B62PresentationNode& node : selected->nodes) {
            if (node.start > pts || (node.end && pts >= *node.end)) {
                continue;
            }
            scene.text += node.caption.text;
            scene.regions.insert(scene.regions.end(), node.caption.regions.begin(), node.caption.regions.end());
            scene.drcs_map.insert(node.caption.drcs_map.begin(), node.caption.drcs_map.end());
        }
        if (boundary_index + 1 < boundaries.size()) {
            scene.wait_duration = boundaries[boundary_index + 1] - pts;
            scene.flags = static_cast<CaptionFlags>(scene.flags | kCaptionFlagsWaitDuration);
        } else {
            scene.wait_duration = DURATION_INDEFINITE;
        }
        out_captions.push_back(std::move(scene));
    }
}

void B62PresentationState::EnforceNodeLimit() {
    size_t node_count = 0;
    for (const B62Presentation& presentation : presentations_) {
        node_count += presentation.nodes.size();
    }
    while (node_count > kMaxStoredNodes && presentations_.size() > 1) {
        node_count -= presentations_.front().nodes.size();
        presentations_.erase(presentations_.begin());
    }
}

}  // namespace aribcaption::internal
