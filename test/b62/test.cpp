/*
 * Copyright (C) 2026 huggy <i@huggy.moe>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "aribcaption/b62_decoder.hpp"
#ifndef ARIBCC_NO_RENDERER
#include "aribcaption/renderer.hpp"
#endif

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
             tts:color="white" tts:textAlign="center" tts:textOutline="red 2px"
             arib-tt:border="solid 3px black"/>
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

constexpr char kSpanRegionTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling">
  <head><styling><style xml:id="vertical" tts:writingMode="tbrl"/></styling><layout>
    <region xml:id="base" tts:origin="100px 100px" tts:extent="400px 200px"/>
    <region xml:id="side" tts:origin="900px 300px" tts:extent="200px 400px"/>
  </layout></head>
  <body><div><p region="base" begin="0s" end="10s"><span region="side" style="vertical">A</span><span style="vertical">B</span><span region="side" style="vertical">C</span></p></div></body>
</tt>)TTML";

constexpr char kDocumentRubyTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:arib-tt="http://www.arib.or.jp/ns/arib-tt">
  <head><layout><region xml:id="r" tts:origin="100px 100px" tts:extent="1000px 800px"/></layout></head>
  <body>
    <div xml:id="div-target"><p xml:id="p-target" region="r" begin="0s" end="10s">段落<span xml:id="span-target">本文</span></p></div>
    <div arib-tt:ruby="div-target"><p arib-tt:ruby="p-target" region="r" begin="0s" end="10s">注釈</p></div>
    <div><p region="r" begin="0s" end="10s"><span arib-tt:ruby="span-target">ルビ</span><span arib-tt:ruby="missing">未解決</span></p></div>
  </body>
</tt>)TTML";

constexpr char kFontAudioTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:arib-tt="http://www.arib.or.jp/ns/arib-tt"
    xmlns:smpte="http://www.smpte-ra.org/schemas/2052-1/2013/smpte-tt" xml:lang="ja">
  <head><styling>
    <arib-tt:font-face xml:id="gaiji" font-family="External" unicode-range="U+E000-E001">
      <arib-tt:src url="subt://1" format="woff"/>
    </arib-tt:font-face>
  </styling><layout><region xml:id="image-region" tts:origin="100px 200px" tts:extent="640px 360px"/></layout><metadata>
    <smpte:image xml:id="embedded" imageType="PNG" encoding="Base64">iVBORw==</smpte:image>
  </metadata></head>
  <body><div xml:id="audio-owner" begin="2s" end="4s">
    <arib-tt:audio xml:id="audio" src="subt://2" loop="true"/>
  </div><div xml:id="external-image" region="image-region" begin="5s" end="7s" smpte:backgroundImage="subt://3"/>
  <div xml:id="embedded-image" region="image-region" begin="8s" end="10s" smpte:backgroundImage="#embedded"/></body>
</tt>)TTML";

constexpr char kBackgroundImageTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:ttp="http://www.w3.org/ns/ttml#parameter"
    xmlns:smpte="http://www.smpte-ra.org/schemas/2052-1/2013/smpte-tt"
    ttp:extent="100px 100px">
  <head><layout>
    <region xml:id="png" tts:origin="10px 20px" tts:extent="20px 10px"/>
    <region xml:id="svg" tts:origin="40px 50px" tts:extent="30px 20px"/>
  </layout></head><body><div>
    <div region="png" begin="0s" end="1s" smpte:backgroundImage="subt://10"/>
    <div region="svg" begin="1s" end="2s" smpte:backgroundImage="subt://11"/>
  </div></body>
</tt>)TTML";

constexpr char kBackgroundSVG[] =
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="30" height="20"><rect width="30" height="20" fill="#00ff00"/></svg>)SVG";

constexpr char kBluePNGBase64[] =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQI12NgYPj/HwADAgH/HXJk"
    "1AAAAABJRU5ErkJggg==";

constexpr char kSVGFontTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:ttp="http://www.w3.org/ns/ttml#parameter"
    xmlns:arib-tt="http://www.arib.or.jp/ns/arib-tt" ttp:extent="320px 180px">
  <head><styling>
    <arib-tt:font-face xml:id="gaiji" font-family="External" unicode-range="U+E000,U+E11A">
      <arib-tt:src url="subt://7" format="svg"/>
    </arib-tt:font-face>
    <style xml:id="external" tts:fontSize="80px" tts:lineHeight="80px"
           tts:textAlign="start" tts:color="#12ab34"/>
  </styling><layout>
    <region xml:id="r" tts:origin="0px 0px" tts:extent="200px 100px"/>
  </layout></head>
  <body><div><p region="r" style="external" begin="0s" end="10s">&#xE000;&#xE11A;</p></div></body>
</tt>)TTML";

constexpr char kSVGFontResource[] = R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"><defs>
  <font id="external" horiz-adv-x="360">
    <font-face units-per-em="360" ascent="360" descent="0"/>
    <glyph unicode="&#xE000;" horiz-adv-x="360"
           d="M0 0 L72 0 L72 360 L0 360 Z"/>
    <!-- Exact U+E11A path extracted from the 2026-08-01 BS NTV 4K sample. -->
    <glyph unicode="&#xE11A;" horiz-adv-x="360" d="M113 345 c-7 -8 -13 -21 -13 -30 0 -8 -4 -15 -10 -15 -5 0 -10 -9 -10 -19 0
-11 -6 -21 -12 -24 -8 -2 -9 -8 -4 -13 5 -5 11 -4 13 4 5 14 63 17 63 2 0 -5
5 -10 10 -10 6 0 10 9 10 20 0 11 5 20 10 20 6 0 10 9 10 20 0 16 -7 20 -30
20 -16 0 -30 5 -30 10 0 6 5 10 10 10 6 0 10 5 10 10 0 14 -13 12 -27 -5z m47
-55 c0 -5 -13 -10 -30 -10 -16 0 -30 5 -30 10 0 6 14 10 30 10 17 0 30 -4 30
-10z M300 180 c0 -153 -1 -160 -20 -160 -11 0 -20 -4 -20 -10 0 -5 14 -10 30
-10 l30 0 0 170 c0 107 -4 170 -10 170 -6 0 -10 -60 -10 -160z M240 190 c0
-67 4 -110 10 -110 6 0 10 43 10 110 0 67 -4 110 -10 110 -6 0 -10 -43 -10
-110z M120 230 c0 -5 -18 -10 -40 -10 -22 0 -40 -4 -40 -10 0 -5 18 -10 40
-10 l40 0 0 -90 c0 -83 -1 -90 -20 -90 -11 0 -20 -4 -20 -10 0 -5 14 -10 30
-10 30 0 30 0 30 56 0 50 3 59 31 85 18 16 29 32 25 36 -3 3 -14 -4 -25 -15
-25 -29 -31 -28 -31 8 0 28 2 30 40 30 22 0 40 5 40 10 0 6 -18 10 -40 10 -22
0 -40 5 -40 10 0 6 -4 10 -10 10 -5 0 -10 -4 -10 -10z M40 170 c0 -5 5 -10 10
-10 6 0 10 -9 10 -20 0 -11 5 -20 10 -20 15 0 12 29 -4 46 -17 16 -26 18 -26
4z M61 74 c-12 -14 -21 -28 -18 -31 6 -5 57 41 57 52 0 11 -16 3 -39 -21z
M160 90 c0 -5 5 -10 10 -10 6 0 10 -9 10 -20 0 -11 5 -20 10 -20 15 0 12 29
-4 46 -17 16 -26 18 -26 4z"/>
  </font>
</defs></svg>)SVG";

constexpr char kWOFFFontTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:ttp="http://www.w3.org/ns/ttml#parameter"
    xmlns:arib-tt="http://www.arib.or.jp/ns/arib-tt" ttp:extent="320px 180px">
  <head><styling>
    <arib-tt:font-face xml:id="gaiji" font-family="External" unicode-range="U+E000">
      <arib-tt:src url="subt://8" format="woff"/>
    </arib-tt:font-face>
    <style xml:id="external" tts:fontSize="80px" tts:lineHeight="80px"
           tts:textAlign="start" tts:color="#12ab34"/>
  </styling><layout>
    <region xml:id="r" tts:origin="0px 0px" tts:extent="100px 100px"/>
  </layout></head>
  <body><div><p region="r" style="external" begin="0s" end="10s">&#xE000;</p></div></body>
</tt>)TTML";

// One-glyph WOFF 1.0 font containing U+E000. Its compressed sfnt tables also
// exercise the zlib-enabled embedded FreeType configuration used on Android.
constexpr char kWOFFFontBase64[] =
    "d09GRgABAAAAAAMIAA4AAAAAAvgAAQABAAAAAAAAAAAAAAAAAAAAAAAAAABHREVGAAACyAAAABQA"
    "AAAUAA8AA0dQT1MAAALcAAAAEAAAABAAGQAMR1NVQgAAAuwAAAAaAAAAGmyMdIVPUy8yAAAB/AAA"
    "AD0AAABg6kH8ZGNtYXAAAAI8AAAAKgAAADQADOBTZ2FzcAAAAsAAAAAIAAAACP//AANnbHlmAAAB"
    "RAAAADoAAAA68WhL1mhlYWQAAAGgAAAANgAAADYYOaD8aGhlYQAAAeAAAAAaAAAAJAP/AgJobXR4"
    "AAAB2AAAAAYAAAAGAisAEWxvY2EAAAGYAAAABgAAAAYAHQAAbWF4cAAAAYAAAAAYAAAAIAAbASFu"
    "YW1lAAACaAAAAEIAAABIBEAMYHBvc3QAAAKsAAAAEwAAACD/hgAyAAMAKwArAdUB1QADAAcADwAA"
    "JTUjFRc1IxUCMhYUBiImNAEVKioqQ7B9fbB964CAVisrAUB9sH19sAAAeJxjYGRgYGBiVGCQYAAB"
    "RgY0AAAF6AA+AAAAAAAdAAAAAQAAAAEEWpKyll5fDzz1AAkCAAAAAADYpKE3AAAAAN55um7//v/9"
    "AgACAwAAAAgAAgAAAAAAAAIAABEAKwAAeJxjYGRgYGIAASaG///AbEYGVMAIAC+vAgcAAHicY2Bh"
    "YmCcwMDKwMDow5jGwMDgDqW/MkgytDAwMDGwMjPAgQADGmh4wPAAqAYEICQDI5KsAoM2AHiwBogA"
    "AAB4nGNgYGBiYGBgBmIRIMkIplkYFIA0CxAC+Q8Y/v+HkAqMYHkGAFmKBj0AAHicLcgxDkAwAEDR"
    "17JYnEDEdQwWN2hERCIkxf11sL3/EfUqoW4EHb+jttQkeayyvegwGC0up9tc/uYtN8kfxLAIqQAA"
    "eJxjYGYAg//NDEYMWAAAKEQBuAAAAAAB//8AAgABAAAADAAAAAAAAAABAAEAAQACAAEAAAAKAAwA"
    "DgAAAAAAAAABAAAACgAWABgAAWxhdG4ACAAAAAAAAAAAAAA=";

std::vector<uint8_t> DecodeBase64(const char* input) {
    std::vector<uint8_t> output;
    uint32_t accumulator = 0;
    int bits = 0;
    for (const char* cursor = input; *cursor; ++cursor) {
        unsigned char ch = static_cast<unsigned char>(*cursor);
        int value = -1;
        if (ch >= 'A' && ch <= 'Z') value = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') value = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') value = ch - '0' + 52;
        else if (ch == '+') value = 62;
        else if (ch == '/') value = 63;
        else if (ch == '=') break;
        else continue;
        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFF));
        }
    }
    return output;
}

constexpr char kOverlappingTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div>
    <p begin="0s" end="10s">first</p>
    <p begin="5s" end="8s">second</p>
  </div></body>
</tt>)TTML";

constexpr char kIndependentlyTimedSpanTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling" xml:lang="ja">
  <head><layout>
    <region xml:id="r" tts:origin="100px 100px" tts:extent="1000px 300px"/>
  </layout></head>
  <body><div><p xml:id="p1" region="r" begin="1s" end="7s"><span>常時</span><span begin="2s" end="4s">追加</span></p></div></body>
</tt>)TTML";

constexpr char kNestedTimedSpanTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div begin="2s" end="9s"><div begin="1s" end="6s">
    <p begin="1s" end="8s"><span>A</span><span begin="1s" end="4s">B</span><span begin="2s" end="10s">C</span></p>
  </div></div></body>
</tt>)TTML";

constexpr char kLiveFirstTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p xml:id="p4" begin="0s" end="indefinite">continued</p></div></body>
</tt>)TTML";

constexpr char kLiveSecondTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p xml:id="p4" begin="indefinite" end="10s">ignored replacement</p></div></body>
</tt>)TTML";

constexpr char kLiveRubyFirstTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:arib-tt="http://www.arib.or.jp/ns/arib-tt" xml:lang="ja">
  <body><div><p xml:id="p4" begin="0s" end="indefinite"><span xml:id="ruby-base">本文</span><span arib-tt:ruby="ruby-base">ルビ</span></p></div></body>
</tt>)TTML";

constexpr char kLiveRubySecondTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p xml:id="p4" begin="indefinite" end="10s">ignored replacement</p></div></body>
</tt>)TTML";

constexpr char kLiveTimedSpanFirstTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p xml:id="timed-live" begin="0s" end="indefinite"><span>A</span><span begin="2s" end="4s">B</span></p></div></body>
</tt>)TTML";

constexpr char kLiveTimedSpanSecondTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="ja">
  <body><div><p xml:id="timed-live" begin="indefinite" end="10s">ignored replacement</p></div></body>
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

constexpr char kStructuredEmptyTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml">
  <head/><body/>
</tt>)TTML";

constexpr char kHeadOnlyTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml">
  <head><metadata/></head>
</tt>)TTML";

constexpr char kCyclicStyleTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:tts="http://www.w3.org/ns/ttml#styling">
  <head><styling>
    <style xml:id="a" style="b" tts:color="#ff0000"/>
    <style xml:id="b" style="a" tts:fontSize="80px"/>
    <style xml:id="c" style="a b"/>
  </styling></head>
  <body><div><p begin="0s" end="1s" style="c">cycle</p></div></body>
</tt>)TTML";

constexpr char kStaticMetadataOnlyTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:arib-tt="urn:arib:ttml:profile:B62:extension">
  <head><metadata>
    <arib-tt:font-face xml:id="unused" font-family="Unused">
      <arib-tt:src url="subt://1" format="woff"/>
    </arib-tt:font-face>
  </metadata></head>
  <body/>
</tt>)TTML";

constexpr char kNonTextEarliestTTML[] = R"TTML(<tt xmlns="http://www.w3.org/ns/ttml"
    xmlns:arib-tt="urn:arib:ttml:profile:B62:extension"
    xmlns:smpte="http://www.smpte-ra.org/schemas/2052-1/2013/smpte-tt">
  <body><div>
    <div begin="1s" end="2s" smpte:backgroundImage="subt://2"/>
    <div begin="2s" end="3s"><arib-tt:audio src="subt://3"/></div>
    <p begin="10s" end="11s">aligned by metadata</p>
  </div></body>
</tt>)TTML";

}  // namespace

int main() {
    aribcaption::Context context;
    aribcaption::B62Decoder decoder(context);
    aribcaption::B62DecodeResult result;

    auto status = decoder.Decode(reinterpret_cast<const uint8_t*>(kBasicTTML), std::strlen(kBasicTTML), 10000, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 4);
    assert(std::all_of(result.captions.begin(), result.captions.end(), [](const auto& caption) {
        return caption.type == aribcaption::CaptionType::kCaption;
    }));
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
    assert(result.captions[0].regions[0].chars[0].style & aribcaption::kCharStyleStroke);
    assert(result.captions[0].regions[0].chars[0].style & aribcaption::kCharStyleColoredEnclosure);
    assert(result.captions[0].regions[0].chars[0].enclosure_style ==
           (aribcaption::kEnclosureStyleTop | aribcaption::kEnclosureStyleBottom |
            aribcaption::kEnclosureStyleLeft | aribcaption::kEnclosureStyleRight));
    assert(result.captions[0].regions[0].chars[0].stroke_color.u32 == aribcaption::ColorRGBA(255, 0, 0).u32);
    assert(result.captions[0].regions[0].chars[0].enclosure_color.u32 == aribcaption::ColorRGBA(0, 0, 0).u32);
    assert(result.captions[0].regions[0].chars[0].enclosure_thickness == 3);
    assert(result.captions[0].regions[1].is_ruby);

    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kCyclicStyleTTML),
                            std::strlen(kCyclicStyleTTML), 0, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 2);
    assert(result.captions[0].regions[0].chars[0].char_width == 80);
    assert(result.captions[0].regions[0].chars[0].text_color.u32 ==
           aribcaption::ColorRGBA(255, 0, 0).u32);

    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kNonTextEarliestTTML),
                            std::strlen(kNonTextEarliestTTML), 1000, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions[0].pts == 10000);

    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kVerticalTTML), std::strlen(kVerticalTTML), 20000, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 1);
    assert(result.captions[0].pts == 20000);
    assert(result.captions[0].wait_duration == aribcaption::DURATION_INDEFINITE);
    assert(result.captions[0].regions.size() == 1);
    const auto& chars = result.captions[0].regions[0].chars;
    assert(chars.size() == 7);
    assert(chars[1].y > chars[0].y);

    const uint8_t resource_bytes[] = {0x00, 0x01, 0x02, 0x03};
    aribcaption::B62ResourceView resource;
    resource.index = 1;
    resource.data = resource_bytes;
    resource.size = sizeof(resource_bytes);
    resource.mime_type = "image/svg+xml";
    aribcaption::B62ResourceContextView resource_context;
    resource_context.scope_id = 0x10001;
    resource_context.resources = &resource;
    resource_context.resource_count = 1;
    aribcaption::B62DecodeOptions resource_options;
    resource_options.document_pts = 21000;
    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kVerticalTTML),
                            std::strlen(kVerticalTTML), resource_options, resource_context, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions[0].regions[0].x == 2920);

    aribcaption::B62ResourceView invalid_resource;
    invalid_resource.index = 2;
    invalid_resource.data = nullptr;
    invalid_resource.size = 1;
    resource_context.resources = &invalid_resource;
    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kVerticalTTML),
                            std::strlen(kVerticalTTML), resource_options, resource_context, result);
    assert(status == aribcaption::B62DecodeStatus::kError);
    assert(result.captions.empty());

    resource_context.scope_id = 0;
    resource_context.resources = nullptr;
    resource_context.resource_count = 0;
    resource_options.document_pts = 0;
    aribcaption::B62DocumentDecodeResult document_result;
    status = decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kStaticMetadataOnlyTTML),
        std::strlen(kStaticMetadataOnlyTTML), resource_options,
        resource_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kNoCaption);
    assert(document_result.captions.empty());
    assert(!document_result.sidecar);

    status = decoder.DecodeDocument(reinterpret_cast<const uint8_t*>(kOverlongCenteredTTML),
                                    std::strlen(kOverlongCenteredTTML), resource_options,
                                    resource_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(document_result.captions[0].regions[0].x == 1000);
    assert(document_result.captions[0].regions[0].width == 200);

    status = decoder.DecodeDocument(reinterpret_cast<const uint8_t*>(kSpanRegionTTML),
                                    std::strlen(kSpanRegionTTML), resource_options,
                                    resource_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(document_result.captions[0].regions.size() == 1);
    assert(document_result.captions[0].regions[0].x == 100);
    assert(document_result.captions[0].regions[0].y == 100);
    assert(document_result.captions[0].regions[0].width == 400);
    assert(document_result.captions[0].regions[0].height == 200);
    assert(document_result.captions[0].regions[0].chars.size() == 3);
    assert(document_result.captions[0].regions[0].chars[0].x >= 700);
    assert(document_result.captions[0].regions[0].chars[1].y >
           document_result.captions[0].regions[0].chars[0].y);
    assert(document_result.captions[0].regions[0].chars[2].y ==
           document_result.captions[0].regions[0].chars[0].y);

    status = decoder.DecodeDocument(reinterpret_cast<const uint8_t*>(kDocumentRubyTTML),
                                    std::strlen(kDocumentRubyTTML), resource_options,
                                    resource_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(document_result.captions.size() == 2);
    assert(document_result.captions[0].text == "段落本文");
    size_t normal_region_count = 0;
    size_t ruby_region_count = 0;
    for (const auto& region : document_result.captions[0].regions) {
        assert(region.x == 100);
        assert(region.y == 100);
        assert(region.width == 1000);
        assert(region.height == 800);
        if (region.is_ruby) {
            ruby_region_count++;
        } else {
            normal_region_count++;
        }
    }
    assert(normal_region_count == 1);
    assert(ruby_region_count == 2);
    assert(document_result.sidecar);
    const auto& ruby_associations = document_result.sidecar->ruby_associations();
    assert(ruby_associations.size() == 4);
    const auto span_ruby = std::find_if(
        ruby_associations.begin(), ruby_associations.end(),
        [](const aribcaption::B62RubyAssociation& association) {
            return association.target_id == "span-target";
        });
    assert(span_ruby != ruby_associations.end());
    assert(span_ruby->annotation_type == aribcaption::B62ElementType::kSpan);
    assert(span_ruby->target_type == aribcaption::B62ElementType::kSpan);
    assert(span_ruby->annotation_text == "ルビ");
    assert(span_ruby->target_text == "本文");
    const auto unresolved_ruby = std::find_if(
        ruby_associations.begin(), ruby_associations.end(),
        [](const aribcaption::B62RubyAssociation& association) {
            return association.target_id == "missing";
        });
    assert(unresolved_ruby != ruby_associations.end());
    assert(unresolved_ruby->target_type == aribcaption::B62ElementType::kUnknown);

    aribcaption::B62DecodeOptions timed_span_options;
    timed_span_options.document_pts = 10000;
    timed_span_options.align_earliest_to_document_pts = true;
    status = decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kIndependentlyTimedSpanTTML),
        std::strlen(kIndependentlyTimedSpanTTML), timed_span_options,
        resource_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(document_result.captions.size() == 4);
    assert(document_result.captions[0].pts == 10000);
    assert(document_result.captions[0].text == "常時");
    assert(document_result.captions[1].pts == 12000);
    assert(document_result.captions[1].text == "常時追加");
    assert(document_result.captions[2].pts == 14000);
    assert(document_result.captions[2].text == "常時");
    assert(document_result.captions[3].pts == 16000);
    assert(document_result.captions[3].regions.empty());

    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kIndependentlyTimedSpanTTML),
                            std::strlen(kIndependentlyTimedSpanTTML), 10000, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions.size() == 2);
    assert(result.captions[0].pts == 10000);
    assert(result.captions[0].text == "常時追加");
    assert(result.captions[1].pts == 16000);
    assert(result.captions[1].regions.empty());

    timed_span_options.document_pts = 20000;
    status = decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kNestedTimedSpanTTML),
        std::strlen(kNestedTimedSpanTTML), timed_span_options,
        resource_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(document_result.captions.size() == 4);
    assert(document_result.captions[0].pts == 20000);
    assert(document_result.captions[0].text == "A");
    assert(document_result.captions[1].pts == 21000);
    assert(document_result.captions[1].text == "AB");
    assert(document_result.captions[2].pts == 22000);
    assert(document_result.captions[2].text == "ABC");
    assert(document_result.captions[3].pts == 24000);
    assert(document_result.captions[3].regions.empty());

    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kDocumentRubyTTML),
                            std::strlen(kDocumentRubyTTML), 0, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);

    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kOverlongCenteredTTML),
                            std::strlen(kOverlongCenteredTTML), 30000, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions[0].regions[0].x < 1000);
    assert(result.captions[0].regions[0].width > 200);

    aribcaption::B62Decoder isolated_decoder(context);
    aribcaption::B62DecodeOptions isolated_options;
    isolated_options.operation_mode = aribcaption::B62OperationMode::kLive;
    isolated_options.document_pts = 1000;
    isolated_options.time_base_pts = 1000;
    status = isolated_decoder.Decode(reinterpret_cast<const uint8_t*>(kLiveFirstTTML),
                                     std::strlen(kLiveFirstTTML), isolated_options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    status = isolated_decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kLiveRubyFirstTTML),
        std::strlen(kLiveRubyFirstTTML), isolated_options, resource_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(document_result.captions[0].text == "本文");
    assert(document_result.sidecar);
    assert(document_result.sidecar->ruby_associations().size() == 1);

    isolated_options.document_pts = 6000;
    status = isolated_decoder.Decode(reinterpret_cast<const uint8_t*>(kLiveSecondTTML),
                                     std::strlen(kLiveSecondTTML), isolated_options, result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(result.captions[0].text == "continued");
    status = isolated_decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kLiveRubySecondTTML),
        std::strlen(kLiveRubySecondTTML), isolated_options, resource_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(document_result.captions[0].text == "本文");
    assert(document_result.sidecar);
    assert(document_result.sidecar->ruby_associations().size() == 1);
    assert(document_result.sidecar->ruby_associations()[0].target_id == "ruby-base");

    aribcaption::B62Decoder timed_live_decoder(context);
    aribcaption::B62DecodeOptions timed_live_options;
    timed_live_options.operation_mode = aribcaption::B62OperationMode::kLive;
    timed_live_options.document_pts = 1000;
    timed_live_options.time_base_pts = 1000;
    status = timed_live_decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kLiveTimedSpanFirstTTML),
        std::strlen(kLiveTimedSpanFirstTTML), timed_live_options,
        resource_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(document_result.captions.size() == 3);
    assert(document_result.captions[0].pts == 1000);
    assert(document_result.captions[0].text == "A");
    assert(document_result.captions[1].pts == 3000);
    assert(document_result.captions[1].text == "AB");
    assert(document_result.captions[2].pts == 5000);
    assert(document_result.captions[2].text == "A");
    assert(document_result.captions[2].wait_duration ==
           aribcaption::DURATION_INDEFINITE);

    timed_live_options.document_pts = 6000;
    status = timed_live_decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kLiveTimedSpanSecondTTML),
        std::strlen(kLiveTimedSpanSecondTTML), timed_live_options,
        resource_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(document_result.captions.size() == 2);
    assert(document_result.captions[0].pts == 5000);
    assert(document_result.captions[0].text == "A");
    assert(document_result.captions[0].wait_duration == 6000);
    assert(document_result.captions[1].pts == 11000);
    assert(document_result.captions[1].regions.empty());

    const uint8_t font_bytes[] = {0x77, 0x4f, 0x46, 0x46};
    const uint8_t audio_bytes[] = {0x49, 0x44, 0x33};
    const uint8_t image_bytes[] = {0x89, 0x50, 0x4e, 0x47};
    aribcaption::B62ResourceView font_audio_resources[3];
    font_audio_resources[0].index = 1;
    font_audio_resources[0].data = font_bytes;
    font_audio_resources[0].size = sizeof(font_bytes);
    font_audio_resources[0].mime_type = "font/woff";
    font_audio_resources[1].index = 2;
    font_audio_resources[1].data = audio_bytes;
    font_audio_resources[1].size = sizeof(audio_bytes);
    font_audio_resources[1].mime_type = "audio/mpeg";
    font_audio_resources[2].index = 3;
    font_audio_resources[2].data = image_bytes;
    font_audio_resources[2].size = sizeof(image_bytes);
    font_audio_resources[2].mime_type = "image/png";
    aribcaption::B62ResourceContextView font_audio_context;
    font_audio_context.resources = font_audio_resources;
    font_audio_context.resource_count = 3;
    aribcaption::B62DecodeOptions font_audio_options;
    font_audio_options.document_pts = 0;
    status = isolated_decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kFontAudioTTML),
        std::strlen(kFontAudioTTML), font_audio_options,
        font_audio_context, document_result);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(document_result.sidecar);
    assert(document_result.sidecar->font_faces().size() == 1);
    const auto& font_face = document_result.sidecar->font_faces()[0];
    assert(font_face.id == "gaiji");
    assert(font_face.family == "External");
    assert(font_face.sources.size() == 1);
    assert(font_face.sources[0].format == aribcaption::B62FontFormat::kWOFF);
    assert(font_face.sources[0].resource.resolved);
    assert(font_face.sources[0].resource.resolved->scope_id == 0);
    assert(font_face.sources[0].resource.resolved->bytes->size() == sizeof(font_bytes));
    assert(document_result.sidecar->audio_cues().size() == 1);
    const auto& audio_cue = document_result.sidecar->audio_cues()[0];
    assert(audio_cue.owner_type == aribcaption::B62ElementType::kDiv);
    assert(audio_cue.owner_id == "audio-owner");
    assert(audio_cue.loop);
    assert(audio_cue.begin_pts == 2000);
    assert(audio_cue.end_pts && *audio_cue.end_pts == 4000);
    assert(audio_cue.source.resolved);
    assert(audio_cue.source.resolved->bytes->size() == sizeof(audio_bytes));
    assert(document_result.sidecar->background_images().size() == 2);
    const auto& external_image = document_result.sidecar->background_images()[0];
    assert(external_image.owner_id == "external-image");
    assert(external_image.begin_pts == 5000);
    assert(external_image.layout_box.x == 100);
    assert(external_image.layout_box.y == 200);
    assert(external_image.layout_box.width == 640);
    assert(external_image.layout_box.height == 360);
    assert(external_image.source.resolved);
    assert(external_image.source.resolved->kind == aribcaption::B62ResourceKind::kPNGImage);
    assert(external_image.source.resolved->bytes->size() == sizeof(image_bytes));
    const auto& embedded_image = document_result.sidecar->background_images()[1];
    assert(embedded_image.owner_id == "embedded-image");
    assert(embedded_image.source.resolved);
    assert(embedded_image.source.resolved->index == std::numeric_limits<uint32_t>::max());
    assert(embedded_image.source.resolved->bytes->size() == 4);

#ifndef ARIBCC_NO_RENDERER
    const std::vector<uint8_t> background_png = DecodeBase64(kBluePNGBase64);
    aribcaption::B62ResourceView background_resources[2];
    background_resources[0].index = 10;
    background_resources[0].data = background_png.data();
    background_resources[0].size = background_png.size();
    background_resources[0].mime_type = "image/png";
    background_resources[1].index = 11;
    background_resources[1].data =
        reinterpret_cast<const uint8_t*>(kBackgroundSVG);
    background_resources[1].size = std::strlen(kBackgroundSVG);
    background_resources[1].mime_type = "image/svg+xml";
    aribcaption::B62ResourceContextView background_resource_context;
    background_resource_context.scope_id = 44;
    background_resource_context.resources = background_resources;
    background_resource_context.resource_count = 2;
    aribcaption::B62DocumentDecodeResult background_document;
    aribcaption::B62Decoder background_decoder(context);
    status = background_decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kBackgroundImageTTML),
        std::strlen(kBackgroundImageTTML), font_audio_options,
        background_resource_context, background_document);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(background_document.sidecar);
    assert(background_document.sidecar->background_images().size() == 2);

    aribcaption::Renderer background_renderer(context);
    assert(background_renderer.Initialize());
    assert(background_renderer.SetFrameSize(100, 100));
    assert(background_renderer.SetMargins(0, 0, 0, 0));
    assert(background_renderer.AppendB62Document(std::move(background_document)));
    aribcaption::RenderResult background_render_result;
    assert(background_renderer.Render(0, background_render_result) ==
           aribcaption::RenderStatus::kGotImage);
    assert(background_render_result.images.size() == 1);
    const aribcaption::Image& png_image = background_render_result.images[0];
    assert(png_image.dst_x == 10 && png_image.dst_y == 20);
    assert(png_image.width == 20 && png_image.height == 10);
    assert(png_image.bitmap[0] == 0 && png_image.bitmap[1] == 0 &&
           png_image.bitmap[2] == 255 && png_image.bitmap[3] == 255);

    assert(background_renderer.Render(1000, background_render_result) ==
           aribcaption::RenderStatus::kGotImage);
    assert(background_render_result.images.size() == 1);
    const aribcaption::Image& svg_background = background_render_result.images[0];
    assert(svg_background.dst_x == 40 && svg_background.dst_y == 50);
    assert(svg_background.width == 30 && svg_background.height == 20);
    assert(svg_background.bitmap[0] == 0 && svg_background.bitmap[1] >= 254 &&
           svg_background.bitmap[2] == 0 && svg_background.bitmap[3] == 255);
    background_renderer.Flush();
    background_decoder.Reset();
    assert(background_renderer.Render(1000, background_render_result) ==
           aribcaption::RenderStatus::kNoImage);

    aribcaption::B62Decoder superimpose_decoder(
        context, aribcaption::CaptionType::kSuperimpose);
    aribcaption::B62DocumentDecodeResult superimpose_document;
    aribcaption::B62DecodeOptions superimpose_options;
    superimpose_options.document_pts = 0;
    aribcaption::B62ResourceContextView empty_resource_context;
    aribcaption::B62DecodeResult superimpose_clear;
    status = superimpose_decoder.Decode(
        reinterpret_cast<const uint8_t*>(kEmptyTTML), std::strlen(kEmptyTTML),
        0, superimpose_clear);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(superimpose_clear.captions.size() == 1);
    assert(superimpose_clear.captions[0].type == aribcaption::CaptionType::kSuperimpose);
    status = superimpose_decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kBasicTTML), std::strlen(kBasicTTML),
        superimpose_options, empty_resource_context, superimpose_document);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(std::all_of(superimpose_document.captions.begin(), superimpose_document.captions.end(),
                       [](const auto& caption) {
        return caption.type == aribcaption::CaptionType::kSuperimpose;
    }));

    aribcaption::Renderer caption_only_renderer(context);
    assert(caption_only_renderer.Initialize(aribcaption::CaptionType::kCaption));
    assert(!caption_only_renderer.AppendB62Document(superimpose_document));

    aribcaption::Renderer superimpose_renderer(context);
    assert(superimpose_renderer.Initialize(aribcaption::CaptionType::kSuperimpose));
    assert(superimpose_renderer.AppendB62Document(std::move(superimpose_document)));

    aribcaption::B62Decoder svg_decoder(context);
    aribcaption::B62ResourceView svg_resource;
    svg_resource.index = 7;
    svg_resource.data = reinterpret_cast<const uint8_t*>(kSVGFontResource);
    svg_resource.size = std::strlen(kSVGFontResource);
    svg_resource.mime_type = "image/svg+xml";
    aribcaption::B62ResourceContextView svg_resource_context;
    svg_resource_context.resources = &svg_resource;
    svg_resource_context.resource_count = 1;
    aribcaption::B62DocumentDecodeResult svg_document;
    aribcaption::B62DecodeOptions svg_options;
    status = svg_decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kSVGFontTTML),
        std::strlen(kSVGFontTTML), svg_options, svg_resource_context,
        svg_document);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(svg_document.captions.size() == 2);
    assert(svg_document.sidecar);
    const auto& svg_region = svg_document.captions[0].regions[0];
    const auto& svg_char = svg_region.chars[0];
    assert(svg_char.codepoint == 0xE000);
    assert(svg_region.chars.size() == 2);
    assert(svg_region.chars[1].codepoint == 0xE11A);
    const int svg_char_x = svg_char.x - svg_region.x;
    const int svg_char_y = svg_char.y - svg_region.y;
    const int svg_char_width = svg_char.char_width;
    const int svg_char_height = svg_char.char_height;

    aribcaption::Renderer svg_renderer(context);
    assert(svg_renderer.Initialize());
    assert(svg_renderer.SetFrameSize(320, 180));
    assert(svg_renderer.SetMargins(0, 0, 0, 0));
    assert(svg_renderer.AppendB62Document(std::move(svg_document)));
    aribcaption::RenderResult svg_render_result;
    assert(svg_renderer.Render(0, svg_render_result) ==
           aribcaption::RenderStatus::kGotImage);
    assert(svg_render_result.images.size() == 1);
    const aribcaption::Image& svg_image = svg_render_result.images[0];
    auto alpha_at = [&](int x, int y) {
        assert(x >= 0 && x < svg_image.width);
        assert(y >= 0 && y < svg_image.height);
        return svg_image.bitmap[static_cast<size_t>(y) * svg_image.stride +
                               static_cast<size_t>(x) * 4 + 3];
    };
    const int svg_sample_y = svg_char_y + svg_char_height / 2;
    assert(alpha_at(svg_char_x + svg_char_width / 10, svg_sample_y) > 200);
    assert(alpha_at(svg_char_x + svg_char_width / 2, svg_sample_y) == 0);
    const auto& real_sample_char = svg_region.chars[1];
    size_t real_sample_opaque_pixels = 0;
    for (int y = real_sample_char.y - svg_region.y;
         y < real_sample_char.y - svg_region.y + real_sample_char.char_height; ++y) {
        for (int x = real_sample_char.x - svg_region.x;
             x < real_sample_char.x - svg_region.x + real_sample_char.char_width; ++x) {
            if (alpha_at(x, y) > 200) ++real_sample_opaque_pixels;
        }
    }
    assert(real_sample_opaque_pixels > 100);

#if defined(ARIBCC_USE_FREETYPE)
    const std::vector<uint8_t> woff_bytes = DecodeBase64(kWOFFFontBase64);
    assert(woff_bytes.size() == 776);
    aribcaption::B62ResourceView woff_resource;
    woff_resource.index = 8;
    woff_resource.data = woff_bytes.data();
    woff_resource.size = woff_bytes.size();
    woff_resource.mime_type = "font/woff";
    aribcaption::B62ResourceContextView woff_resource_context;
    woff_resource_context.resources = &woff_resource;
    woff_resource_context.resource_count = 1;
    aribcaption::B62DocumentDecodeResult woff_document;
    aribcaption::B62Decoder woff_decoder(context);
    status = woff_decoder.DecodeDocument(
        reinterpret_cast<const uint8_t*>(kWOFFFontTTML),
        std::strlen(kWOFFFontTTML), svg_options, woff_resource_context,
        woff_document);
    assert(status == aribcaption::B62DecodeStatus::kGotCaption);
    assert(woff_document.sidecar);

    aribcaption::Renderer woff_renderer(context);
    assert(woff_renderer.Initialize(aribcaption::CaptionType::kCaption,
                                    aribcaption::FontProviderType::kAuto,
                                    aribcaption::TextRendererType::kFreetype));
    assert(woff_renderer.SetFrameSize(320, 180));
    assert(woff_renderer.SetMargins(0, 0, 0, 0));
    assert(woff_renderer.AppendB62Document(std::move(woff_document)));
    aribcaption::RenderResult woff_render_result;
    assert(woff_renderer.Render(0, woff_render_result) ==
           aribcaption::RenderStatus::kGotImage);
    assert(woff_render_result.images.size() == 1);
    const aribcaption::Image& woff_image = woff_render_result.images[0];
    size_t opaque_pixels = 0;
    for (int y = 0; y < woff_image.height; ++y) {
        for (int x = 0; x < woff_image.width; ++x) {
            if (woff_image.bitmap[static_cast<size_t>(y) * woff_image.stride +
                                  static_cast<size_t>(x) * 4 + 3] != 0) {
                ++opaque_pixels;
            }
        }
    }
    assert(opaque_pixels > 100);
#endif

    std::weak_ptr<const aribcaption::B62ResourceBlob> retained_font_resource =
        font_face.sources[0].resource.resolved;
    aribcaption::Renderer document_renderer(context);
    assert(document_renderer.Initialize());
    assert(document_renderer.AppendB62Document(std::move(document_result)));
    assert(document_result.captions.empty());
    assert(!document_result.sidecar);
    assert(!retained_font_resource.expired());
    isolated_decoder.Reset();
    assert(!retained_font_resource.expired());
    document_renderer.Flush();
    assert(retained_font_resource.expired());
#endif

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

    options.document_pts = 5000;
    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kStructuredEmptyTTML),
                            std::strlen(kStructuredEmptyTTML), options, result);
    assert(status == aribcaption::B62DecodeStatus::kNoCaption);
    assert(result.captions.empty());

    status = decoder.Decode(reinterpret_cast<const uint8_t*>(kHeadOnlyTTML),
                            std::strlen(kHeadOnlyTTML), options, result);
    assert(status == aribcaption::B62DecodeStatus::kNoCaption);
    assert(result.captions.empty());

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
