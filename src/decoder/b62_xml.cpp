/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_xml.hpp"

#include <cstring>
#include <string>

namespace aribcaption::internal {
namespace {

constexpr char kARIBNamespace[] = "http://www.arib.or.jp/ns/arib-tt";
constexpr char kLegacyARIBNamespace[] = "http://www.arib.or.jp/ns/arib-ttml/v1_0";
constexpr char kSMPTENamespace[] =
    "http://www.smpte-ra.org/schemas/2052-1/2013/smpte-tt";

const char* ResolveNamespace(const tinyxml2::XMLElement* element,
                             std::string_view prefix) {
    const std::string declaration = "xmlns:" + std::string(prefix);
    for (const tinyxml2::XMLNode* node = element; node; node = node->Parent()) {
        const tinyxml2::XMLElement* current = node->ToElement();
        if (!current) {
            continue;
        }
        if (const char* uri = current->Attribute(declaration.c_str())) {
            return uri;
        }
    }
    return nullptr;
}

bool IsElementInNamespace(const tinyxml2::XMLElement* element,
                          std::string_view local_name,
                          const char* namespace_uri) {
    if (!element || B62LocalName(element->Name()) != local_name) {
        return false;
    }
    std::string_view name(element->Name());
    size_t colon = name.rfind(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    const char* uri = ResolveNamespace(element, name.substr(0, colon));
    return uri && std::strcmp(uri, namespace_uri) == 0;
}

}  // namespace

std::string_view B62LocalName(std::string_view name) {
    size_t colon = name.rfind(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

const char* B62FindAttribute(const tinyxml2::XMLElement* element,
                             std::string_view local_name) {
    if (!element) {
        return nullptr;
    }
    for (const tinyxml2::XMLAttribute* attr = element->FirstAttribute(); attr;
         attr = attr->Next()) {
        if (B62LocalName(attr->Name()) == local_name) {
            return attr->Value();
        }
    }
    return nullptr;
}

const char* B62FindXMLID(const tinyxml2::XMLElement* element) {
    return element ? element->Attribute("xml:id") : nullptr;
}

const char* B62FindNamespacedAttribute(const tinyxml2::XMLElement* element,
                                       std::string_view local_name,
                                       std::string_view namespace_uri) {
    if (!element) {
        return nullptr;
    }
    for (const tinyxml2::XMLAttribute* attr = element->FirstAttribute(); attr;
         attr = attr->Next()) {
        std::string_view name(attr->Name());
        size_t colon = name.rfind(':');
        if (colon == std::string_view::npos || name.substr(colon + 1) != local_name) {
            continue;
        }
        const char* uri = ResolveNamespace(element, name.substr(0, colon));
        if (uri && std::string_view(uri) == namespace_uri) {
            return attr->Value();
        }
    }
    return nullptr;
}

const char* B62FindARIBAttribute(const tinyxml2::XMLElement* element,
                                 std::string_view local_name) {
    const char* value = B62FindNamespacedAttribute(element, local_name, kARIBNamespace);
    return value ? value
                 : B62FindNamespacedAttribute(element, local_name, kLegacyARIBNamespace);
}

bool B62IsARIBElement(const tinyxml2::XMLElement* element,
                      std::string_view local_name) {
    return IsElementInNamespace(element, local_name, kARIBNamespace) ||
           IsElementInNamespace(element, local_name, kLegacyARIBNamespace);
}

bool B62IsSMPTEElement(const tinyxml2::XMLElement* element,
                       std::string_view local_name) {
    return IsElementInNamespace(element, local_name, kSMPTENamespace);
}

const tinyxml2::XMLElement* B62FirstChild(const tinyxml2::XMLElement* parent,
                                          std::string_view local_name) {
    if (!parent) {
        return nullptr;
    }
    for (const tinyxml2::XMLElement* child = parent->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        if (B62LocalName(child->Name()) == local_name) {
            return child;
        }
    }
    return nullptr;
}

void B62CollectDescendants(const tinyxml2::XMLElement* parent,
                           std::string_view local_name,
                           std::vector<const tinyxml2::XMLElement*>& out) {
    if (!parent) {
        return;
    }
    for (const tinyxml2::XMLElement* child = parent->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        if (B62LocalName(child->Name()) == local_name) {
            out.push_back(child);
        }
        B62CollectDescendants(child, local_name, out);
    }
}

const char* B62FindNearestAttribute(const tinyxml2::XMLElement* element,
                                    std::string_view local_name) {
    for (const tinyxml2::XMLNode* node = element; node; node = node->Parent()) {
        const tinyxml2::XMLElement* current = node->ToElement();
        if (current) {
            if (const char* value = B62FindAttribute(current, local_name)) {
                return value;
            }
        }
    }
    return nullptr;
}

const tinyxml2::XMLElement* B62FindNearestTimedNode(
    const tinyxml2::XMLElement* element) {
    for (const tinyxml2::XMLNode* node = element; node; node = node->Parent()) {
        const tinyxml2::XMLElement* current = node->ToElement();
        if (!current) {
            continue;
        }
        if (B62FindAttribute(current, "begin") || B62FindAttribute(current, "end")) {
            return current;
        }
    }
    return nullptr;
}

}  // namespace aribcaption::internal
