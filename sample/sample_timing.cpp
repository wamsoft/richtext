/**
 * sample_timing.cpp
 *
 * delay, wait, sync, keywait, graph 入りテキストのレンダリングと
 * TimingEntry, LinkInfo, GraphInfo のダンプサンプル
 */

#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <map>

#include "richtext/FontManager.hpp"
#include "richtext/FontFace.hpp"
#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/StyledLayout.hpp"
#include "richtext/TimingInfo.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

//------------------------------------------------------------------------------
// UTF-8 → UTF-16 変換
//------------------------------------------------------------------------------

static std::u16string utf8ToUtf16(const std::string& utf8) {
    std::u16string result;
    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = 0;
        unsigned char c = utf8[i];
        if      ((c & 0x80) == 0)   { cp = c;                       i += 1; }
        else if ((c & 0xE0) == 0xC0){ cp = (c & 0x1F) << 6;
                                       if (i+1 < utf8.size()) cp |= (utf8[i+1] & 0x3F); i += 2; }
        else if ((c & 0xF0) == 0xE0){ cp = (c & 0x0F) << 12;
                                       if (i+1 < utf8.size()) cp |= (utf8[i+1] & 0x3F) << 6;
                                       if (i+2 < utf8.size()) cp |= (utf8[i+2] & 0x3F); i += 3; }
        else if ((c & 0xF8) == 0xF0){ cp = (c & 0x07) << 18;
                                       if (i+1 < utf8.size()) cp |= (utf8[i+1] & 0x3F) << 12;
                                       if (i+2 < utf8.size()) cp |= (utf8[i+2] & 0x3F) << 6;
                                       if (i+3 < utf8.size()) cp |= (utf8[i+3] & 0x3F); i += 4; }
        else { i += 1; continue; }

        if (cp <= 0xFFFF) {
            result += static_cast<char16_t>(cp);
        } else if (cp <= 0x10FFFF) {
            cp -= 0x10000;
            result += static_cast<char16_t>(0xD800 | (cp >> 10));
            result += static_cast<char16_t>(0xDC00 | (cp & 0x3FF));
        }
    }
    return result;
}

// UTF-16 → UTF-8 変換（表示用）
static std::string utf16ToUtf8(const std::u16string& utf16) {
    std::string result;
    for (size_t i = 0; i < utf16.size(); ++i) {
        uint32_t cp = utf16[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < utf16.size()) {
            uint32_t lo = utf16[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return result;
}

//------------------------------------------------------------------------------
// メイン
//------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    printf("=== Timing / Link / Graph Info Dump Sample ===\n\n");

    //--------------------------------------------------------------------------
    // フォント登録
    //--------------------------------------------------------------------------
    auto& fm = richtext::FontManager::instance();
    const std::string dataDir = "./data/";

    fm.setFontDataLoader([&dataDir](const std::string& name) -> richtext::FontDataBuffer {
        std::string path = dataDir + name;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return nullptr;
        auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        auto buffer = std::make_shared<std::vector<uint8_t>>(size);
        if (!file.read(reinterpret_cast<char*>(buffer->data()), size)) return nullptr;
        return buffer;
    });

    struct FontEntry { const char* file; const char* name; };
    FontEntry fonts[] = {
        {"NotoSans-Regular.ttf",   "sans"},
        {"NotoSansJP-Regular.otf", "ja"},
        {"NotoColorEmoji.ttf",     "emoji"},
    };
    for (const auto& f : fonts) {
        if (fm.registerFont(f.file, f.name)) {
            printf("  Font registered: [%s] %s\n", f.name, f.file);
        } else {
            fprintf(stderr, "  Font FAILED: [%s] %s\n", f.name, f.file);
        }
    }

    auto collection = fm.createCollection({"sans", "ja", "emoji"});

    //--------------------------------------------------------------------------
    // スタイル定義
    //--------------------------------------------------------------------------
    std::map<std::string, richtext::TextStyle> styles;
    std::map<std::string, richtext::Appearance> appearances;

    richtext::TextStyle defaultStyle;
    defaultStyle.fontCollection = collection;
    defaultStyle.fontSize = 24.0f;
    styles["default"] = defaultStyle;

    richtext::Appearance defaultApp;
    defaultApp.addFill(0xFF333333);
    appearances["default"] = defaultApp;

    //--------------------------------------------------------------------------
    // テストテキスト: delay, wait, sync, keywait, graph, link を含む
    //--------------------------------------------------------------------------
    std::u16string taggedText = utf8ToUtf16(
        u8"こんにちは"
        u8"<delay value='50%'/>"        // delay 50%
        u8"世界！"
        u8"<wait value='200'/>"         // wait 200ms
        u8"<delay value='500'/>"        // delay 500ms
        u8"テスト中"
        u8"<sync value='3000'/>"        // sync 3000ms
        u8"同期後の"
        u8"<keywait/>"                  // keywait
        u8"<link name='link1'>"
        u8"リンクテキスト"
        u8"</link>"
        u8"と"
        u8"<graph name='icon.png' width='24' height='24'/>"
        u8"画像付き"
        u8"<wait value='100%'/>"        // wait 100%
        u8"<sync value='label_end'/>"   // sync label
        u8"終わり"
    );

    //--------------------------------------------------------------------------
    // StyledLayout でレイアウト
    //--------------------------------------------------------------------------
    richtext::StyledLayout layout;
    layout.layout(taggedText, 600.0f, 400.0f,
                  richtext::ParagraphLayout::HAlign::Left,
                  richtext::ParagraphLayout::VAlign::Top,
                  styles, appearances);

    const auto& parsed = layout.getParsed();

    //--------------------------------------------------------------------------
    // プレーンテキスト表示
    //--------------------------------------------------------------------------
    printf("\n--- Plain Text ---\n");
    printf("  \"%s\"\n", utf16ToUtf8(parsed.plainText).c_str());
    printf("  length: %zu\n", parsed.plainText.size());

    //--------------------------------------------------------------------------
    // TimingEntry ダンプ
    //--------------------------------------------------------------------------
    printf("\n--- TimingEntry (%zu entries) ---\n", parsed.timings.size());
    for (size_t i = 0; i < parsed.timings.size(); ++i) {
        const auto& t = parsed.timings[i];
        switch (t.type) {
        case richtext::TimingEntry::Type::Char:
            printf("  [%3zu] Char     charIndex=%d  delayPercent=%.1f  delayMs=%.1f",
                   i, t.charIndex, t.delayPercent, t.delayMs);
            if (t.charIndex >= 0 && t.charIndex < static_cast<int>(parsed.plainText.size())) {
                std::u16string ch(1, parsed.plainText[t.charIndex]);
                printf("  char='%s'", utf16ToUtf8(ch).c_str());
            }
            printf("\n");
            break;
        case richtext::TimingEntry::Type::Wait:
            printf("  [%3zu] Wait     charIndex=%d  waitPercent=%.1f  waitMs=%.1f\n",
                   i, t.charIndex, t.waitPercent, t.waitMs);
            break;
        case richtext::TimingEntry::Type::Sync:
            printf("  [%3zu] Sync     charIndex=%d  syncMs=%.1f  syncLabel='%s'\n",
                   i, t.charIndex, t.syncMs, t.syncLabel.c_str());
            break;
        case richtext::TimingEntry::Type::KeyWait:
            printf("  [%3zu] KeyWait  charIndex=%d\n",
                   i, t.charIndex);
            break;
        }
    }

    //--------------------------------------------------------------------------
    // LinkInfo ダンプ
    //--------------------------------------------------------------------------
    printf("\n--- LinkInfo (%zu entries) ---\n", parsed.links.size());
    for (size_t i = 0; i < parsed.links.size(); ++i) {
        const auto& l = parsed.links[i];
        std::u16string linkText = parsed.plainText.substr(l.startIndex, l.endIndex - l.startIndex);
        printf("  [%zu] name='%s'  range=[%zu, %zu)  text='%s'\n",
               i, l.name.c_str(), l.startIndex, l.endIndex,
               utf16ToUtf8(linkText).c_str());
    }

    //--------------------------------------------------------------------------
    // GraphInfo ダンプ
    //--------------------------------------------------------------------------
    printf("\n--- GraphInfo (%zu entries) ---\n", parsed.graphics.size());
    for (size_t i = 0; i < parsed.graphics.size(); ++i) {
        const auto& g = parsed.graphics[i];
        printf("  [%zu] name='%s'  charIndex=%d  size=%.0fx%.0f\n",
               i, utf16ToUtf8(g.name).c_str(), g.charIndex, g.width, g.height);
    }

    //--------------------------------------------------------------------------
    // resolveTimings でタイミング解決
    //--------------------------------------------------------------------------
    printf("\n--- Resolved Timings ---\n");

    float diff = 50.0f;   // 1文字あたり 50ms
    float all = 0.0f;     // 自動計算
    float timeScale = 1.0f;
    bool widthTimeScale = false;
    std::vector<float> charWidths;

    // 文字幅配列を構築
    const auto& lineLayouts = layout.getLineLayouts();
    for (const auto& line : lineLayouts) {
        for (const auto& seg : line.segments) {
            const auto& glyphs = seg.layout.getGlyphs();
            for (const auto& g : glyphs) {
                charWidths.push_back(g.advance);
            }
        }
    }

    // ラベルリゾルバ（テスト用）
    richtext::LabelResolver labelResolver = [](const std::string& label) -> float {
        printf("  [LabelResolver] resolving '%s' -> ", label.c_str());
        if (label == "label_end") {
            printf("10000.0 ms\n");
            return 10000.0f;
        }
        printf("0.0 ms (unknown)\n");
        return 0.0f;
    };

    std::vector<richtext::KeyWaitInfo> keyWaits;
    auto resolved = richtext::resolveTimings(
        parsed.timings, diff, all, timeScale,
        widthTimeScale, charWidths,
        labelResolver, &keyWaits);

    printf("\n  Resolved character timings (%zu entries):\n", resolved.size());
    for (size_t i = 0; i < resolved.size(); ++i) {
        const auto& rt = resolved[i];
        printf("    [%3zu] charIndex=%d  delay=%.1f ms", i, rt.charIndex, rt.delay);
        if (rt.charIndex >= 0 && rt.charIndex < static_cast<int>(parsed.plainText.size())) {
            std::u16string ch(1, parsed.plainText[rt.charIndex]);
            printf("  char='%s'", utf16ToUtf8(ch).c_str());
        }
        printf("\n");
    }

    printf("\n  KeyWait points (%zu entries):\n", keyWaits.size());
    for (size_t i = 0; i < keyWaits.size(); ++i) {
        printf("    [%zu] charIndex=%d  delay=%.1f ms\n",
               i, keyWaits[i].charIndex, keyWaits[i].delay);
    }

    //--------------------------------------------------------------------------
    // リンク矩形ダンプ
    //--------------------------------------------------------------------------
    auto linkRegions = layout.buildLinkRegions();
    printf("\n--- Link Regions (%zu entries) ---\n", linkRegions.size());
    for (size_t i = 0; i < linkRegions.size(); ++i) {
        const auto& lr = linkRegions[i];
        printf("  [%zu] name='%s'  chars=%zu  rects=%zu\n",
               i, lr.name.c_str(), lr.charIndices.size(), lr.rects.size());
        for (size_t j = 0; j < lr.rects.size(); ++j) {
            const auto& r = lr.rects[j];
            printf("       rect[%zu]: (%.1f, %.1f) - (%.1f, %.1f)\n",
                   j, r.left, r.top, r.right, r.bottom);
        }
    }

    printf("\n=== Done ===\n");
    return 0;
}
