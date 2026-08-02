/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_document_metadata.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "decoder/b62_presentation_state.hpp"
#include "decoder/b62_resource_resolver.hpp"
#include "decoder/b62_text_util.hpp"
#include "decoder/b62_xml.hpp"

namespace aribcaption::internal {
namespace {

void AppendElementText(const tinyxml2::XMLNode* parent,
                       std::string& output) {
    for (const tinyxml2::XMLNode* child =
             parent ? parent->FirstChild() : nullptr;
         child; child = child->NextSibling()) {
        if (const tinyxml2::XMLText* text = child->ToText()) {
            output += text->Value();
            continue;
        }
        const tinyxml2::XMLElement* element = child->ToElement();
        if (!element) {
            continue;
        }
        if (B62LocalName(element->Name()) == "br") {
            output.push_back('\n');
        } else {
            AppendElementText(element, output);
        }
    }
}

std::string ElementText(const tinyxml2::XMLElement* element) {
    std::string text;
    AppendElementText(element, text);
    return B62NormalizeText(text);
}

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

void CollectRubyElements(
    const tinyxml2::XMLElement* parent,
    std::vector<const tinyxml2::XMLElement*>& elements) {
    if (!parent) {
        return;
    }
    for (const tinyxml2::XMLElement* child = parent->FirstChildElement();
         child; child = child->NextSiblingElement()) {
        if (ElementType(child) != B62ElementType::kUnknown) {
            elements.push_back(child);
        }
        CollectRubyElements(child, elements);
    }
}

}  // namespace

void B62CollectRubyAssociations(
    const tinyxml2::XMLElement* document_root,
    B62PresentationMetadata& out_metadata) {
    std::vector<const tinyxml2::XMLElement*> elements;
    CollectRubyElements(document_root, elements);
    std::unordered_map<std::string, const tinyxml2::XMLElement*> targets;
    for (const tinyxml2::XMLElement* element : elements) {
        if (const char* id = B62FindXMLID(element)) {
            targets.emplace(id, element);
        }
    }
    for (const tinyxml2::XMLElement* element : elements) {
        const char* target_id = B62FindARIBAttribute(element, "ruby");
        if (!target_id) {
            continue;
        }
        B62RubyAssociation association;
        association.annotation_type = ElementType(element);
        if (const char* annotation_id = B62FindXMLID(element)) {
            association.annotation_id = annotation_id;
        }
        association.target_id = target_id;
        association.annotation_text = ElementText(element);
        auto target = targets.find(target_id);
        if (target != targets.end()) {
            association.target_type = ElementType(target->second);
            association.target_text = ElementText(target->second);
        }
        out_metadata.ruby_associations.push_back(std::move(association));
    }
}

void B62CollectFontFaces(
    const tinyxml2::XMLElement* document_root,
    B62ResourceResolver& resource_resolver,
    B62PresentationMetadata& out_metadata) {
    std::vector<const tinyxml2::XMLElement*> font_face_elements;
    B62CollectDescendants(document_root, "font-face", font_face_elements);
    for (const tinyxml2::XMLElement* font_face_element : font_face_elements) {
        if (!B62IsARIBElement(font_face_element, "font-face")) {
            continue;
        }
        const char* family = B62FindAttribute(font_face_element, "font-family");
        if (!family || B62TrimASCII(family).empty()) {
            continue;
        }
        B62FontFace font_face;
        if (const char* id = B62FindXMLID(font_face_element)) {
            font_face.id = id;
        }
        font_face.family = B62TrimASCII(family);
        if (const char* unicode_range =
                B62FindAttribute(font_face_element, "unicode-range")) {
            font_face.unicode_range = B62TrimASCII(unicode_range);
        }
        for (const tinyxml2::XMLElement* source =
                 font_face_element->FirstChildElement();
             source; source = source->NextSiblingElement()) {
            if (!B62IsARIBElement(source, "src")) {
                continue;
            }
            const char* uri = B62FindAttribute(source, "url");
            if (!uri) {
                continue;
            }
            B62FontSource font_source;
            std::string format = B62TrimASCII(B62FindAttribute(source, "format")
                ? B62FindAttribute(source, "format") : "");
            std::transform(format.begin(), format.end(), format.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            B62ResourceKind resource_kind = B62ResourceKind::kUnknown;
            if (format == "svg") {
                font_source.format = B62FontFormat::kSVG;
                resource_kind = B62ResourceKind::kSVGFont;
            } else if (format == "woff") {
                font_source.format = B62FontFormat::kWOFF;
                resource_kind = B62ResourceKind::kWOFFFont;
            }
            font_source.resource = resource_resolver.Resolve(uri, resource_kind);
            font_face.sources.push_back(std::move(font_source));
        }
        if (!font_face.sources.empty()) {
            out_metadata.font_faces.push_back(std::move(font_face));
        }
    }
}

void B62MaterializeTimedMetadata(
    const B62TimedContent& timed_content,
    const std::array<int, 2>& plane,
    const std::unordered_map<std::string, B62RegionDefinition>& regions,
    B62ResourceResolver& resource_resolver,
    int64_t timeline_offset,
    int64_t document_pts,
    B62PresentationMetadata& out_metadata) {
    for (const B62RawAudioCue& raw_audio : timed_content.audio_cues) {
        B62AudioCue audio;
        audio.owner_type = raw_audio.owner_type;
        audio.owner_id = raw_audio.owner_id;
        audio.id = raw_audio.id;
        audio.source = resource_resolver.Resolve(
            raw_audio.source.c_str(), B62ResourceKind::kAudio);
        audio.loop = raw_audio.loop;
        audio.begin_pts = raw_audio.start
            ? *raw_audio.start + timeline_offset
            : document_pts;
        if (raw_audio.end) {
            audio.end_pts = *raw_audio.end + timeline_offset;
        } else if (!raw_audio.indefinite && audio.begin_pts != PTS_NOPTS) {
            audio.end_pts = audio.begin_pts + 5000;
        }
        out_metadata.audio_cues.push_back(std::move(audio));
    }

    for (const B62RawBackgroundImage& raw_image :
         timed_content.background_images) {
        B62BackgroundImage image;
        image.owner_type = raw_image.owner_type;
        image.layout_box.width = plane[0];
        image.layout_box.height = plane[1];
        if (!raw_image.region_id.empty()) {
            auto region = regions.find(raw_image.region_id);
            if (region != regions.end()) {
                image.layout_box.x = region->second.x;
                image.layout_box.y = region->second.y;
                image.layout_box.width = region->second.width;
                image.layout_box.height = region->second.height;
            }
        }
        image.owner_id = raw_image.owner_id;
        image.source = resource_resolver.Resolve(
            raw_image.source.c_str(), B62ResourceKind::kUnknown);
        image.begin_pts = raw_image.start
            ? *raw_image.start + timeline_offset
            : document_pts;
        if (raw_image.end) {
            image.end_pts = *raw_image.end + timeline_offset;
        } else if (!raw_image.indefinite && image.begin_pts != PTS_NOPTS) {
            image.end_pts = image.begin_pts + 5000;
        }
        out_metadata.background_images.push_back(std::move(image));
    }
}

}  // namespace aribcaption::internal
