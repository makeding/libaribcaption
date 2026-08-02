/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * This file is part of libaribcaption.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef ARIBCAPTION_B62_DECODER_HPP
#define ARIBCAPTION_B62_DECODER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "aribcc_export.h"
#include "caption.hpp"
#include "context.hpp"

namespace aribcaption {

namespace internal {
class B62DecoderImpl;
}

enum class B62DecodeStatus {
    kError = 0,
    kNoCaption = 1,
    kGotCaption = 2,
};

enum class B62OperationMode {
    kLive = 0,
    kSegment = 1,
    kProgram = 2,
};

struct B62DecodeOptions {
    // Presentation time of the TTML document's MPU on the media timeline.
    int64_t document_pts = PTS_NOPTS;
    // Media-timeline position corresponding to TTML time zero.
    int64_t time_base_pts = PTS_NOPTS;
    B62OperationMode operation_mode = B62OperationMode::kSegment;
    bool align_earliest_to_document_pts = false;
    bool ignore_document_timing = false;
    bool discontinuity = false;
};

struct B62DecodeResult {
    std::vector<Caption> captions;
};

/**
 * ARIB STD-B62 / ARIB-TTML decoder.
 *
 * The decoder accepts one UTF-8 TTML document and maps its timed paragraphs
 * into the same Caption structures used by the ARIB STD-B24 decoder. A single
 * document may produce more than one Caption.
 */
class B62Decoder {
public:
    ARIBCC_API explicit B62Decoder(Context& context);
    ARIBCC_API ~B62Decoder();
    ARIBCC_API B62Decoder(B62Decoder&&) noexcept;
    ARIBCC_API B62Decoder& operator=(B62Decoder&&) noexcept;

public:
    ARIBCC_API void Reset();

public:
    /**
     * Decode a UTF-8 ARIB-TTML document.
     *
     * If base_pts is specified, the earliest explicitly timed paragraph is
     * aligned to it while relative TTML timing is preserved.
     */
    ARIBCC_API B62DecodeStatus Decode(const uint8_t* ttml_data,
                                      size_t length,
                                      int64_t base_pts,
                                      B62DecodeResult& out_result);

    ARIBCC_API B62DecodeStatus Decode(const uint8_t* ttml_data,
                                      size_t length,
                                      const B62DecodeOptions& options,
                                      B62DecodeResult& out_result);

public:
    B62Decoder(const B62Decoder&) = delete;
    B62Decoder& operator=(const B62Decoder&) = delete;

private:
    std::unique_ptr<internal::B62DecoderImpl> pimpl_;
};

}  // namespace aribcaption

#endif  // ARIBCAPTION_B62_DECODER_HPP
