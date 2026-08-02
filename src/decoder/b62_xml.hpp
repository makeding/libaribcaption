/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_XML_HPP
#define ARIBCAPTION_B62_XML_HPP

#include <string_view>
#include <vector>

#include "base/tinyxml2.h"

namespace aribcaption::internal {

std::string_view B62LocalName(std::string_view name);
const char* B62FindAttribute(const tinyxml2::XMLElement* element,
                             std::string_view local_name);
const char* B62FindXMLID(const tinyxml2::XMLElement* element);
const char* B62FindARIBAttribute(const tinyxml2::XMLElement* element,
                                 std::string_view local_name);
const char* B62FindNamespacedAttribute(const tinyxml2::XMLElement* element,
                                       std::string_view local_name,
                                       std::string_view namespace_uri);
bool B62IsARIBElement(const tinyxml2::XMLElement* element,
                      std::string_view local_name);
bool B62IsSMPTEElement(const tinyxml2::XMLElement* element,
                       std::string_view local_name);
bool B62IsEmptyTTMLDocument(const tinyxml2::XMLElement* tt);
const tinyxml2::XMLElement* B62FirstChild(const tinyxml2::XMLElement* parent,
                                          std::string_view local_name);
void B62CollectDescendants(const tinyxml2::XMLElement* parent,
                           std::string_view local_name,
                           std::vector<const tinyxml2::XMLElement*>& out);
const char* B62FindNearestAttribute(const tinyxml2::XMLElement* element,
                                    std::string_view local_name);
const tinyxml2::XMLElement* B62FindNearestTimedNode(
    const tinyxml2::XMLElement* element);

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_XML_HPP
