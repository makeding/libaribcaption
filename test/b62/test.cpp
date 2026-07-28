/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

#include "aribcaption/b62_decoder.hpp"

namespace {

constexpr char kBasicTTML[] = R"TTML(<?xml version="1.0" encoding="UTF-8"?>
<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:ttp="http://www.w3.org/ns/ttml#parameter"
    xmlns:arib-tt="http://www.arib.or.jp/ns/arib-ttml/v1_0"
    ttp:extent="3840px 2160px" xml:lang="ja">
  <head>
    <styling>
      <style xml:id="base" tts:fontSize="96px 144px" tts:lineHeight="160px"
             tts:color="white" tts:textAlign="center" arib-tt:border="solid 3px black"/>
      <style xml:id="animated" style="base"/>
    </styling>
    <layout>
      <region xml:id="bottom" tts:origin="240px 1480px" tts:extent="3360px 420px"
              tts:displayAlign="center"/>
    </layout>
  </head>
  <body>
    <div>
      <p begin="00:00:00.200" end="00:00:04.800" region="bottom" style="animated">
        <span xml:id="ji">仁和寺</span><span arib-tt:ruby="ji">にんなじ</span>　京都市右京区
      </p>
      <p begin="00:00:05.000" end="00:00:09.500" region="bottom" style="base">第二字幕</p>
    </div>
  </body>
</tt>)TTML";

constexpr char kVerticalTTML[] = R"TTML(<?xml version="1.0" encoding="UTF-8"?>
<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:ttp="http://www.w3.org/ns/ttml#parameter"
    xmlns:arib-tt="http://www.arib.or.jp/ns/arib-ttml/v1_0"
    ttp:extent="3840px 2160px" xml:lang="ja">
  <head>
    <styling>
      <style xml:id="vertical" tts:fontSize="112px" tts:lineHeight="132px"
             tts:writingMode="tbrl" arib-tt:letter-spacing="8px"/>
    </styling>
    <layout>
      <region xml:id="right" tts:origin="2920px 180px" tts:extent="520px 1640px"/>
    </layout>
  </head>
  <body>
    <div>
      <p begin="1s" dur="indefinite" region="right" style="vertical">縦書き字幕表示</p>
    </div>
  </body>
</tt>)TTML";

constexpr char kOverlongCenteredTTML[] = R"TTML(<?xml version="1.0" encoding="UTF-8"?>
<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:ttp="http://www.w3.org/ns/ttml#parameter"
    ttp:extent="3840px 2160px" xml:lang="ja">
  <head>
    <styling><style xml:id="base" tts:fontSize="120px"/></styling>
    <layout><region xml:id="narrow" tts:origin="1000px 100px" tts:extent="200px 200px"/></layout>
  </head>
  <body><div><p region="narrow" style="base">中央寄せ</p></div></body>
</tt>)TTML";

constexpr char kOverlappingTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div>
    <p begin="0s" end="10s">first</p>
    <p begin="5s" end="8s">second</p>
  </div></body>
</tt>)TTML";

constexpr char kLiveFirstTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p xml:id="p4" begin="0s" end="indefinite">continued</p></div></body>
</tt>)TTML";

constexpr char kLiveSecondTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p xml:id="p4" begin="indefinite" end="10s">ignored replacement</p></div></body>
</tt>)TTML";

constexpr char kLiveBarrierOldTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p begin="0s" end="10s">old presentation</p></div></body>
</tt>)TTML";

constexpr char kLiveBarrierNewTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p begin="2s" end="4s">new presentation</p></div></body>
</tt>)TTML";

constexpr char kPartialTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p begin="0s" end="5s">partial</p></div></body>
</tt>)TTML";

constexpr char kCompleteTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p begin="0s" end="5s">complete</p></div></body>
</tt>)TTML";

constexpr char kEmptyTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"></tt>)TTML";

}  // namespace

int main() {
    aribcaption::Context context;
    aribcaption::B62Decoder decoder(context);
    aribcaption::B62DecodeResult result;

    auto status = decoder.Decode(reinterpret_cast<const uint8_t*>(kBasicTTML), std::strlen(kBasicTTML), 10000, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 4);
    assert(result.captions[0].pts == 10000);
    assert(result.captions[0].wait_duration == 4600);
    assert(result.captions[1].pts == 14600);
    assert(result.captions[1].regions.empty());
    assert(result.captions[2].pts == 14800);
    assert(result.captions[2].wait_duration == 4500);
    assert(result.captions[0].plane_width == 3840);
    assert(result.captions[0].plane_height == 2160);
    assert(result.captions[0].text == "仁和寺　京都市右京区");
    assert(result.captions[0].regions.size() == 2);
    assert(!result.captions[0].regions[0].is_ruby);
    assert(result.captions[0].regions[0].x == 240);
    assert(result.captions[0].regions[0].y == 1480);
    assert(result.captions[0].regions[0].width == 3360);
    assert(result.captions[0].regions[0].height == 420);
    assert(result.captions[0].regions[0].chars[0].x > result.captions[0].regions[0].x);
    assert(result.captions[0].regions[0].chars[0].char_width == 96);
    assert(result.captions[0].regions[0].chars[0].char_height == 144);
    assert(result.captions[0].regions[0].chars[0].char_vertical_spacing == 16);
    assert(!(result.captions[0].regions[0].chars[0].style & aribcaption::kCharStyleStroke));
    assert(result.captions[0].regions[0].chars[0].style & aribcaption::kCharStyleColoredEnclosure);
    assert(result.captions[0].regions[0].chars[0].enclosure_style ==
           (aribcaption::kEnclosureStyleTop | aribcaption::kEnclosureStyleBottom |
            aribcaption::kEnclosureStyleLeft | aribcaption::kEnclosureStyleRight));
    assert(result.captions[0].regions[0].chars[0].stroke_color.u32 == aribcaption::ColorRGBA(0, 0, 0).u32);
    assert(result.captions[0].regions[1].is_ruby);

    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kVerticalTTML), std::strlen(kVerticalTTML), 20000, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 1);
    assert(result.captions[0].pts == 20000);
    assert(result.captions[0].wait_duration == aribcaption::DURATION_INDEFINITE);
    assert(result.captions[0].regions.size() == 1);
    const auto& chars = result.captions[0].regions[0].chars;
    assert(chars.size() == 7);
    assert(chars[1].y > chars[0].y);

    decoder.SetFontScale(1.25f);
    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kBasicTTML), std::strlen(kBasicTTML), 10000, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions[0].regions[0].chars[0].char_width == 120);
    assert(result.captions[0].regions[0].chars[0].char_height == 180);
    assert(result.captions[0].regions[0].chars[0].char_vertical_spacing == 20);

    decoder.SetFontScale(1.0f);
    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kOverlongCenteredTTML),
                            std::strlen(kOverlongCenteredTTML), 30000, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions[0].regions[0].x < 1000);
    assert(result.captions[0].regions[0].width > 200);

    aribcaption::B62DecodeOptions options;
    options.document_pts = 1000;
    options.time_base_pts = 1000;
    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kOverlappingTTML),
                            std::strlen(kOverlappingTTML), options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 4);
    assert(result.captions[0].pts == 1000 && result.captions[0].regions.size() == 1);
    assert(result.captions[1].pts == 6000 && result.captions[1].regions.size() == 2);
    assert(result.captions[2].pts == 9000 && result.captions[2].regions.size() == 1);
    assert(result.captions[3].pts == 11000 && result.captions[3].regions.empty());

    options.operation_mode = aribcaption::B62OperationMode::kLive;
    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kLiveFirstTTML),
                            std::strlen(kLiveFirstTTML), options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 1);
    assert(result.captions[0].text == "continued");
    assert(result.captions[0].wait_duration == aribcaption::DURATION_INDEFINITE);

    options.document_pts = 6000;
    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kLiveSecondTTML),
                            std::strlen(kLiveSecondTTML), options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 2);
    assert(result.captions[0].pts == 1000);
    assert(result.captions[0].wait_duration == 10000);
    assert(result.captions[0].text == "continued");
    assert(result.captions[1].pts == 11000 && result.captions[1].regions.empty());

    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kEmptyTTML),
                            std::strlen(kEmptyTTML), options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 1 && result.captions[0].regions.empty());

    aribcaption::B62Decoder presentation_decoder(context);
    aribcaption::B62DecodeOptions live_options;
    live_options.operation_mode = aribcaption::B62OperationMode::kLive;
    live_options.document_pts = 1000;
    live_options.time_base_pts = 1000;
    status = presentation_decoder.Decode(
        reinterpret_cast<const uint8_t*>(kLiveBarrierOldTTML),
        std::strlen(kLiveBarrierOldTTML), live_options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions[0].text == "old presentation");

    live_options.document_pts = 3000;
    status = presentation_decoder.Decode(
        reinterpret_cast<const uint8_t*>(kLiveBarrierNewTTML),
        std::strlen(kLiveBarrierNewTTML), live_options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 2);
    assert(result.captions[0].pts == 3000 && result.captions[0].text == "new presentation");
    assert(result.captions[1].pts == 5000 && result.captions[1].regions.empty());

    presentation_decoder.Reset();
    live_options.document_pts = 1000;
    status = presentation_decoder.Decode(
        reinterpret_cast<const uint8_t*>(kPartialTTML),
        std::strlen(kPartialTTML), live_options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    status = presentation_decoder.Decode(
        reinterpret_cast<const uint8_t*>(kCompleteTTML),
        std::strlen(kCompleteTTML), live_options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions[0].text == "complete");

    live_options.document_pts = 3000;
    live_options.discontinuity = true;
    status = presentation_decoder.Decode(
        reinterpret_cast<const uint8_t*>(kLiveBarrierNewTTML),
        std::strlen(kLiveBarrierNewTTML), live_options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions[0].text == "new presentation");
    for (const auto& caption : result.captions) {
        assert(caption.text != "complete");
    }

    return 0;
}
