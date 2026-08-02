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
