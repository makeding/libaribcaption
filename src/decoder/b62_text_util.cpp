/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "decoder/b62_text_util.hpp"

#include <algorithm>
#include <utility>

namespace aribcaption::internal {

std::string B62TrimASCII(std::string value) {
    auto is_space = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
    };
    auto begin = std::find_if_not(value.begin(), value.end(), is_space);
    auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
    return begin < end ? std::string(begin, end) : std::string();
}

std::string B62NormalizeText(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    bool pending_space = false;
    bool pending_newline = false;
    for (size_t i = 0; i < input.size(); i++) {
        char ch = input[i];
        if (ch == '\r') {
            if (i + 1 < input.size() && input[i + 1] == '\n') {
                i++;
            }
            ch = '\n';
        }
        if (ch == '\n') {
            while (!output.empty() && (output.back() == ' ' || output.back() == '\t')) {
                output.pop_back();
            }
            pending_space = false;
            pending_newline = true;
            continue;
        }
        if (ch == ' ' || ch == '\f' || ch == '\v') {
            pending_space = true;
            continue;
        }
        if (ch == '\t') {
            continue;
        }
        if (pending_newline) {
            output.push_back('\n');
        } else if (pending_space) {
            output.push_back(' ');
        }
        pending_newline = false;
        pending_space = false;
        output.push_back(ch);
    }
    return B62TrimASCII(std::move(output));
}

}  // namespace aribcaption::internal
