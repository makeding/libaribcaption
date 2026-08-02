/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_TEXT_UTIL_HPP
#define ARIBCAPTION_B62_TEXT_UTIL_HPP

#include <string>
#include <string_view>

namespace aribcaption::internal {

std::string B62TrimASCII(std::string value);
std::string B62NormalizeText(std::string_view input);

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_TEXT_UTIL_HPP
