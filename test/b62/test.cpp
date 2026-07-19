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
      <style xml:id="base" tts:fontSize="120px" tts:lineHeight="150px"
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

}  // namespace

int main() {
    aribcaption::Context context;
    aribcaption::B62Decoder decoder(context);
    aribcaption::B62DecodeResult result;

    auto status = decoder.Decode(reinterpret_cast<const uint8_t*>(kBasicTTML), std::strlen(kBasicTTML), 10000, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 2);
    assert(result.captions[0].pts == 10000);
    assert(result.captions[0].wait_duration == 4600);
    assert(result.captions[1].pts == 14800);
    assert(result.captions[1].wait_duration == 4500);
    assert(result.captions[0].plane_width == 3840);
    assert(result.captions[0].plane_height == 2160);
    assert(result.captions[0].text == "仁和寺　京都市右京区");
    assert(result.captions[0].regions.size() == 2);
    assert(!result.captions[0].regions[0].is_ruby);
    assert(result.captions[0].regions[0].x == 240);
    assert(result.captions[0].regions[0].y == 1480);
    assert(result.captions[0].regions[0].width == 3360);
    assert(result.captions[0].regions[0].height == 420);
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

    return 0;
}
