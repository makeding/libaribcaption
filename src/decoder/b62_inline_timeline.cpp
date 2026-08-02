/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_inline_timeline.hpp"

#include <algorithm>
#include <string_view>

#include "decoder/b62_time.hpp"
#include "decoder/b62_xml.hpp"

namespace aribcaption::internal {
namespace {

struct TimingContext {
    int64_t local_origin = 0;
    B62InlineInterval active;
    bool indefinite_start = false;
    bool indefinite_end = false;
};

TimingContext ResolveParallelChild(const tinyxml2::XMLElement* element,
                                   const TimingContext& parent) {
    TimingContext resolved;
    bool indefinite_begin = false;
    std::optional<int64_t> begin = B62ParseTime(
        B62FindAttribute(element, "begin"), &indefinite_begin);
    resolved.indefinite_start = parent.indefinite_start || indefinite_begin;
    resolved.local_origin = parent.local_origin + begin.value_or(0);
    resolved.active.begin = std::max(parent.active.begin, resolved.local_origin);
    resolved.active.end = parent.active.end;

    bool indefinite_end = false;
    std::optional<int64_t> end = B62ParseTime(
        B62FindAttribute(element, "end"), &indefinite_end);
    resolved.indefinite_end = parent.indefinite_end || indefinite_end;
    if (end) {
        const int64_t absolute_end = parent.local_origin + *end;
        if (!resolved.active.end || absolute_end < *resolved.active.end) {
            resolved.active.end = absolute_end;
        }
    }
    if (resolved.active.end && *resolved.active.end <= resolved.active.begin) {
        resolved.active.begin = *resolved.active.end;
    }
    return resolved;
}

void IncludeBoundary(int64_t boundary,
                     const B62InlineInterval& paragraph,
                     std::vector<int64_t>& boundaries) {
    if (boundary >= paragraph.begin &&
        (!paragraph.end || boundary <= *paragraph.end)) {
        boundaries.push_back(boundary);
    }
}

void CollectSpanIntervals(
    const tinyxml2::XMLElement* parent,
    const TimingContext& parent_timing,
    const B62InlineInterval& paragraph,
    std::unordered_map<const tinyxml2::XMLElement*, B62InlineInterval>& intervals,
    std::vector<int64_t>& boundaries) {
    for (const tinyxml2::XMLElement* child = parent->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        if (B62LocalName(child->Name()) != "span") {
            CollectSpanIntervals(child, parent_timing, paragraph, intervals,
                                 boundaries);
            continue;
        }

        TimingContext timing = ResolveParallelChild(child, parent_timing);
        intervals.emplace(child, timing.active);
        IncludeBoundary(timing.active.begin, paragraph, boundaries);
        if (timing.active.end) {
            IncludeBoundary(*timing.active.end, paragraph, boundaries);
        }
        CollectSpanIntervals(child, timing, paragraph, intervals, boundaries);
    }
}

}  // namespace

bool B62InlineTimeline::IsElementActive(
    const tinyxml2::XMLElement* element, int64_t time) const {
    auto interval = span_intervals_.find(element);
    if (interval == span_intervals_.end()) {
        return true;
    }
    return time >= interval->second.begin &&
        (!interval->second.end || time < *interval->second.end);
}

B62InlineTimeline B62BuildInlineTimeline(
    const tinyxml2::XMLElement* paragraph,
    std::optional<int64_t> legacy_start,
    std::optional<int64_t> legacy_end,
    bool legacy_indefinite) {
    B62InlineTimeline timeline;
    if (!paragraph || B62LocalName(paragraph->Name()) != "p") {
        return timeline;
    }

    std::vector<const tinyxml2::XMLElement*> timed_ancestors;
    for (const tinyxml2::XMLNode* node = paragraph; node; node = node->Parent()) {
        const tinyxml2::XMLElement* element = node->ToElement();
        if (!element) {
            continue;
        }
        std::string_view name = B62LocalName(element->Name());
        if (name == "p" || name == "div") {
            timed_ancestors.push_back(element);
        }
        if (name == "body") {
            break;
        }
    }
    std::reverse(timed_ancestors.begin(), timed_ancestors.end());

    TimingContext timing;
    timing.active.begin = 0;
    for (const tinyxml2::XMLElement* element : timed_ancestors) {
        timing = ResolveParallelChild(element, timing);
    }
    if (timing.indefinite_start) {
        return timeline;
    }

    // Preserve the already-supported paragraph dur fallback without extending
    // dur semantics to containers or inline elements.
    if (!timing.active.end && legacy_end) {
        int64_t duration = *legacy_end - legacy_start.value_or(0);
        if (duration > 0) {
            timing.active.end = timing.active.begin + duration;
        }
    }
    timeline.valid_ = !timing.active.end ||
        *timing.active.end > timing.active.begin;
    if (!timeline.valid_) {
        return timeline;
    }
    timeline.paragraph_interval_ = timing.active;
    timeline.paragraph_indefinite_ = !timing.active.end &&
        (timing.indefinite_end || legacy_indefinite);

    std::vector<int64_t> boundaries{timing.active.begin};
    CollectSpanIntervals(paragraph, timing, timing.active,
                         timeline.span_intervals_, boundaries);
    if (timing.active.end) {
        boundaries.push_back(*timing.active.end);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    for (size_t i = 0; i < boundaries.size(); i++) {
        if (timing.active.end && boundaries[i] >= *timing.active.end) {
            break;
        }
        B62InlineScene scene;
        scene.begin = boundaries[i];
        if (i + 1 < boundaries.size()) {
            scene.end = boundaries[i + 1];
        } else {
            scene.end = timing.active.end;
        }
        if (!scene.end || *scene.end > scene.begin) {
            timeline.scenes_.push_back(scene);
        }
    }
    return timeline;
}

}  // namespace aribcaption::internal
