/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_document_parser.hpp"

#include <cstring>
#include <string_view>
#include <utility>

#include "decoder/b62_time.hpp"
#include "decoder/b62_xml.hpp"

namespace aribcaption::internal {
namespace {

B62ElementType ElementType(const tinyxml2::XMLElement* element) {
    if (!element) {
        return B62ElementType::kUnknown;
    }
    std::string_view name = B62LocalName(element->Name());
    if (name == "div") return B62ElementType::kDiv;
    if (name == "p") return B62ElementType::kParagraph;
    if (name == "span") return B62ElementType::kSpan;
    return B62ElementType::kUnknown;
}

void IncludeStart(const std::optional<int64_t>& start,
                  std::optional<int64_t>& minimum_start) {
    if (start && (!minimum_start || *start < *minimum_start)) {
        minimum_start = start;
    }
}

}  // namespace

B62ParseDisposition B62ParsedDocument::Parse(const uint8_t* data,
                                              size_t length) {
    root_ = nullptr;
    body_ = nullptr;
    tinyxml2::XMLError error = document_.Parse(
        reinterpret_cast<const char*>(data), length);
    root_ = document_.RootElement();
    if (error != tinyxml2::XML_SUCCESS || !root_ ||
        B62LocalName(root_->Name()) != "tt") {
        return B62ParseDisposition::kInvalid;
    }
    if (B62IsEmptyTTMLDocument(root_)) {
        return B62ParseDisposition::kEmpty;
    }
    body_ = B62FirstChild(root_, "body");
    return body_ ? B62ParseDisposition::kReady
                 : B62ParseDisposition::kNoBody;
}

B62TimedContent B62CollectTimedContent(
    const tinyxml2::XMLElement* body) {
    B62TimedContent content;

    std::vector<const tinyxml2::XMLElement*> paragraphs;
    B62CollectDescendants(body, "p", paragraphs);
    content.cues.reserve(paragraphs.size());
    for (const tinyxml2::XMLElement* paragraph : paragraphs) {
        B62RawCue cue;
        cue.paragraph = paragraph;
        if (const char* id = B62FindAttribute(paragraph, "id")) {
            cue.id = id;
        }
        const tinyxml2::XMLElement* timing_node =
            B62FindNearestTimedNode(paragraph);
        cue.start = B62ParseTime(B62FindAttribute(paragraph, "begin"),
                                 &cue.indefinite_start);
        if (!cue.start && !cue.indefinite_start && timing_node) {
            cue.start = B62ParseTime(B62FindAttribute(timing_node, "begin"),
                                     &cue.indefinite_start);
        }
        bool indefinite_end = false;
        cue.end = B62ParseTime(B62FindAttribute(paragraph, "end"),
                               &indefinite_end);
        if (!cue.end && !indefinite_end && timing_node) {
            cue.end = B62ParseTime(B62FindAttribute(timing_node, "end"),
                                   &indefinite_end);
        }
        bool indefinite_duration = false;
        std::optional<int64_t> duration = B62ParseTime(
            B62FindAttribute(paragraph, "dur"), &indefinite_duration);
        if (!duration && !indefinite_duration && timing_node) {
            duration = B62ParseTime(B62FindAttribute(timing_node, "dur"),
                                    &indefinite_duration);
        }
        cue.indefinite = indefinite_end || indefinite_duration;
        if (!cue.end && duration && cue.start) {
            cue.end = *cue.start + *duration;
        }
        IncludeStart(cue.start, content.minimum_start);
        content.cues.push_back(std::move(cue));
    }

    std::vector<const tinyxml2::XMLElement*> audio_elements;
    B62CollectDescendants(body, "audio", audio_elements);
    for (const tinyxml2::XMLElement* audio : audio_elements) {
        const char* source = B62FindAttribute(audio, "src");
        if (!B62IsARIBElement(audio, "audio") || !source) {
            continue;
        }
        B62RawAudioCue cue;
        cue.source = source;
        if (const char* id = B62FindXMLID(audio)) {
            cue.id = id;
        }
        if (const char* loop = B62FindAttribute(audio, "loop")) {
            cue.loop = std::strcmp(loop, "true") == 0 ||
                       std::strcmp(loop, "1") == 0;
        }
        for (const tinyxml2::XMLNode* node = audio->Parent(); node;
             node = node->Parent()) {
            const tinyxml2::XMLElement* owner = node->ToElement();
            if (!owner) {
                continue;
            }
            std::string_view name = B62LocalName(owner->Name());
            if (name == "p" || name == "div") {
                cue.owner_type = ElementType(owner);
                if (const char* owner_id = B62FindXMLID(owner)) {
                    cue.owner_id = owner_id;
                }
                break;
            }
        }
        const tinyxml2::XMLElement* timing_node =
            B62FindNearestTimedNode(audio);
        cue.start = B62ParseTime(B62FindAttribute(timing_node, "begin"));
        bool indefinite_end = false;
        cue.end = B62ParseTime(B62FindAttribute(timing_node, "end"),
                               &indefinite_end);
        bool indefinite_duration = false;
        std::optional<int64_t> duration = B62ParseTime(
            B62FindAttribute(timing_node, "dur"), &indefinite_duration);
        cue.indefinite = indefinite_end || indefinite_duration;
        if (!cue.end && duration && cue.start) {
            cue.end = *cue.start + *duration;
        }
        IncludeStart(cue.start, content.minimum_start);
        content.audio_cues.push_back(std::move(cue));
    }

    std::vector<const tinyxml2::XMLElement*> background_owners;
    B62CollectDescendants(body, "div", background_owners);
    background_owners.insert(background_owners.end(), paragraphs.begin(),
                             paragraphs.end());
    for (const tinyxml2::XMLElement* owner : background_owners) {
        const char* source = B62FindNamespacedAttribute(
            owner, "backgroundImage",
            "http://www.smpte-ra.org/schemas/2052-1/2013/smpte-tt");
        if (!source) {
            continue;
        }
        B62RawBackgroundImage image;
        image.owner_type = ElementType(owner);
        image.source = source;
        if (const char* owner_id = B62FindXMLID(owner)) {
            image.owner_id = owner_id;
        }
        if (const char* region_id = B62FindNearestAttribute(owner, "region")) {
            image.region_id = region_id;
        }
        const tinyxml2::XMLElement* timing_node =
            B62FindNearestTimedNode(owner);
        image.start = B62ParseTime(B62FindAttribute(timing_node, "begin"));
        bool indefinite_end = false;
        image.end = B62ParseTime(B62FindAttribute(timing_node, "end"),
                                 &indefinite_end);
        bool indefinite_duration = false;
        std::optional<int64_t> duration = B62ParseTime(
            B62FindAttribute(timing_node, "dur"), &indefinite_duration);
        image.indefinite = indefinite_end || indefinite_duration;
        if (!image.end && duration && image.start) {
            image.end = *image.start + *duration;
        }
        IncludeStart(image.start, content.minimum_start);
        content.background_images.push_back(std::move(image));
    }

    return content;
}

}  // namespace aribcaption::internal
