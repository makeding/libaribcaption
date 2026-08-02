/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_PRESENTATION_STATE_HPP
#define ARIBCAPTION_B62_PRESENTATION_STATE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "aribcaption/b62_decoder.hpp"

namespace aribcaption::internal {

struct B62PresentationNode {
    std::string id;
    int64_t start = PTS_NOPTS;
    std::optional<int64_t> end;
    bool indefinite = false;
    Caption caption;
};

struct B62PresentationMetadata {
    std::vector<B62RubyAssociation> ruby_associations;
    std::vector<B62FontFace> font_faces;
    std::vector<B62AudioCue> audio_cues;
    std::vector<B62BackgroundImage> background_images;
};

struct B62Presentation {
    uint64_t event_id = 0;
    int64_t start = PTS_NOPTS;
    int plane_width = 3840;
    int plane_height = 2160;
    uint32_t language = 0;
    B62PresentationMetadata metadata;
    std::vector<B62PresentationNode> nodes;
};

// Owns ARIB-TTML document replacement and live-presentation state.  It is
// intentionally separate from the stateless XML parser and from the B24
// caption renderer.
class B62PresentationState {
public:
    void Reset();

    // A continuation document may close an indefinite node from the current
    // presentation without becoming a new presentation itself.
    bool ApplyContinuation(const std::string& id,
                           std::optional<int64_t> end,
                           bool close_with_default_duration);

    // Commits one TTML document atomically.  A later delivery with the same
    // presentation start replaces the earlier (usually partial) delivery.
    void Commit(B62Presentation presentation);

    // Retains one predecessor as a state barrier so pruning cannot expose an
    // older presentation after a newer one has expired.
    void Prune(int64_t current_pts);

    void BuildScenes(int64_t current_pts,
                     size_t max_events,
                     std::vector<Caption>& out_captions,
                     B62PresentationMetadata* out_metadata = nullptr) const;

    [[nodiscard]] bool empty() const { return presentations_.empty(); }

private:
    void EnforceNodeLimit();

    uint64_t next_event_id_ = 1;
    std::vector<B62Presentation> presentations_;
};

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_PRESENTATION_STATE_HPP
