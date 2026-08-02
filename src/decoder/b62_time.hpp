/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_TIME_HPP
#define ARIBCAPTION_B62_TIME_HPP

#include <cstdint>
#include <optional>

namespace aribcaption::internal {

std::optional<int64_t> B62ParseTime(const char* value,
                                    bool* indefinite = nullptr);

}  // namespace aribcaption::internal

#endif  // ARIBCAPTION_B62_TIME_HPP
