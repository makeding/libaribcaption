/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_presentation_state.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace aribcaption::internal {
namespace {

constexpr int64_t kPruneWindowMilliseconds = 30000;
constexpr size_t kMaxStoredNodes = 300;
constexpr size_t kMaxStoredPresentations = 64;
constexpr size_t kMaxStoredMetadataItems = 1024;
constexpr size_t kMaxPinnedResourceBytes = 128u * 1024u * 1024u;

bool PresentationLess(const B62Presentation& lhs, const B62Presentation& rhs) {
    if (lhs.start != rhs.start) {
        return lhs.start < rhs.start;
    }
    return lhs.event_id < rhs.event_id;
}

bool PresentationExpiredBefore(const B62Presentation& presentation, int64_t cutoff) {
    const bool nodes_expired = std::all_of(
        presentation.nodes.begin(), presentation.nodes.end(),
        [&](const B62PresentationNode& node) {
            return node.end && *node.end < cutoff;
        });
    const bool audio_expired = std::all_of(
        presentation.metadata.audio_cues.begin(), presentation.metadata.audio_cues.end(),
        [&](const B62AudioCue& audio) {
            return audio.end_pts && *audio.end_pts < cutoff;
        });
    const bool images_expired = std::all_of(
        presentation.metadata.background_images.begin(),
        presentation.metadata.background_images.end(),
        [&](const B62BackgroundImage& image) {
            return image.end_pts && *image.end_pts < cutoff;
        });
    return nodes_expired && audio_expired && images_expired;
}

void IncludeResourceBytes(const B62ResourceReference& reference,
                          std::unordered_set<const void*>& seen,
                          size_t& total) {
    if (!reference.resolved || !reference.resolved->bytes) {
        return;
    }
    const void* identity = reference.resolved->bytes.get();
    if (seen.insert(identity).second) {
        total += reference.resolved->bytes->size();
    }
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
        for (const B62AudioCue& audio : presentation.metadata.audio_cues) {
            if (audio.begin_pts != PTS_NOPTS &&
                (presentation.start == PTS_NOPTS || audio.begin_pts < presentation.start)) {
                presentation.start = audio.begin_pts;
            }
        }
        for (const B62BackgroundImage& image : presentation.metadata.background_images) {
            if (image.begin_pts != PTS_NOPTS &&
                (presentation.start == PTS_NOPTS || image.begin_pts < presentation.start)) {
                presentation.start = image.begin_pts;
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
    EnforceLimits();
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
    if (PresentationExpiredBefore(barrier, keep_from)) {
        barrier.nodes.clear();
        barrier.metadata = {};
    }
}

void B62PresentationState::BuildScenes(int64_t current_pts,
                                       size_t max_events,
                                       CaptionType caption_type,
                                       std::vector<Caption>& out_captions,
                                       B62PresentationMetadata* out_metadata) const {
    out_captions.clear();
    if (out_metadata) {
        *out_metadata = {};
    }
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
        for (const B62AudioCue& audio : presentation.metadata.audio_cues) {
            if (audio.begin_pts != PTS_NOPTS && audio.begin_pts >= presentation.start &&
                audio.begin_pts < next_start) {
                boundaries.push_back(audio.begin_pts);
            }
            if (audio.end_pts && *audio.end_pts >= presentation.start &&
                *audio.end_pts < next_start) {
                boundaries.push_back(*audio.end_pts);
            }
        }
        for (const B62BackgroundImage& image : presentation.metadata.background_images) {
            if (image.begin_pts != PTS_NOPTS && image.begin_pts >= presentation.start &&
                image.begin_pts < next_start) {
                boundaries.push_back(image.begin_pts);
            }
            if (image.end_pts && *image.end_pts >= presentation.start &&
                *image.end_pts < next_start) {
                boundaries.push_back(*image.end_pts);
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
    std::vector<uint64_t> emitted_metadata_events;
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
        if (out_metadata &&
            std::find(emitted_metadata_events.begin(), emitted_metadata_events.end(),
                      selected->event_id) == emitted_metadata_events.end()) {
            emitted_metadata_events.push_back(selected->event_id);
            for (const B62RubyAssociation& association : selected->metadata.ruby_associations) {
                const bool exists = std::any_of(
                    out_metadata->ruby_associations.begin(), out_metadata->ruby_associations.end(),
                    [&](const B62RubyAssociation& current) {
                        return current.annotation_type == association.annotation_type &&
                               current.target_type == association.target_type &&
                               current.annotation_id == association.annotation_id &&
                               current.target_id == association.target_id &&
                               current.annotation_text == association.annotation_text &&
                               current.target_text == association.target_text;
                    });
                if (!exists) {
                    out_metadata->ruby_associations.push_back(association);
                }
            }
            out_metadata->font_faces.insert(out_metadata->font_faces.end(),
                                             selected->metadata.font_faces.begin(),
                                             selected->metadata.font_faces.end());
            out_metadata->audio_cues.insert(out_metadata->audio_cues.end(),
                                            selected->metadata.audio_cues.begin(),
                                            selected->metadata.audio_cues.end());
            out_metadata->background_images.insert(
                out_metadata->background_images.end(),
                selected->metadata.background_images.begin(),
                selected->metadata.background_images.end());
        }

        Caption scene;
        scene.type = caption_type;
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

void B62PresentationState::EnforceLimits() {
    const auto usage = [&]() {
        size_t node_count = 0;
        size_t metadata_count = 0;
        size_t resource_bytes = 0;
        std::unordered_set<const void*> seen_resources;
        for (const B62Presentation& presentation : presentations_) {
            node_count += presentation.nodes.size();
            metadata_count += presentation.metadata.ruby_associations.size();
            metadata_count += presentation.metadata.font_faces.size();
            metadata_count += presentation.metadata.audio_cues.size();
            metadata_count += presentation.metadata.background_images.size();
            for (const B62FontFace& face : presentation.metadata.font_faces) {
                for (const B62FontSource& source : face.sources) {
                    IncludeResourceBytes(source.resource, seen_resources, resource_bytes);
                }
            }
            for (const B62AudioCue& audio : presentation.metadata.audio_cues) {
                IncludeResourceBytes(audio.source, seen_resources, resource_bytes);
            }
            for (const B62BackgroundImage& image : presentation.metadata.background_images) {
                IncludeResourceBytes(image.source, seen_resources, resource_bytes);
            }
        }
        return std::array<size_t, 3>{node_count, metadata_count, resource_bytes};
    };
    auto current = usage();
    while ((presentations_.size() > kMaxStoredPresentations ||
            current[0] > kMaxStoredNodes ||
            current[1] > kMaxStoredMetadataItems ||
            current[2] > kMaxPinnedResourceBytes) &&
           presentations_.size() > 1) {
        presentations_.erase(presentations_.begin());
        current = usage();
    }
}

}  // namespace aribcaption::internal
