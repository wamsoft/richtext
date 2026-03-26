/**
 * sample_sequential.cpp
 *
 * 逐次表示（maxGlyphs）のサンプル
 * 多言語・絵文字混在テキストを段階的に表示する
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>

#include "richtext/FontManager.hpp"
#include "richtext/FontFace.hpp"
#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/TextLayout.hpp"
#include "richtext/ParagraphLayout.hpp"
#include "richtext/TextRenderer.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

//------------------------------------------------------------------------------
// BMP 出力ヘルパー
//------------------------------------------------------------------------------

#pragma pack(push, 1)
struct BMPFileHeader {
    uint16_t type = 0x4D42;
    uint32_t size = 0;
    uint16_t reserved1 = 0;
    uint16_t reserved2 = 0;
    uint32_t offset = 54;
};
struct BMPInfoHeader {
    uint32_t size = 40;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t planes = 1;
    uint16_t bitCount = 32;
    uint32_t compression = 0;
    uint32_t imageSize = 0;
    int32_t xPixelsPerMeter = 2835;
    int32_t yPixelsPerMeter = 2835;
    uint32_t colorsUsed = 0;
    uint32_t colorsImportant = 0;
};
#pragma pack(pop)

bool saveBMP(const char* filename, const uint32_t* pixels, int width, int height) {
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    infoHeader.width = width;
    infoHeader.height = height;
    infoHeader.imageSize = width * height * 4;
    fileHeader.size = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + infoHeader.imageSize;

    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

    std::vector<uint32_t> bgrPixels(width * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int srcIdx = y * width + x;
            int dstIdx = (height - 1 - y) * width + x;
            uint32_t argb = pixels[srcIdx];
            uint8_t a = (argb >> 24) & 0xFF;
            uint8_t r = (argb >> 16) & 0xFF;
            uint8_t g = (argb >>  8) & 0xFF;
            uint8_t b =  argb        & 0xFF;
            bgrPixels[dstIdx] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    file.write(reinterpret_cast<const char*>(bgrPixels.data()), infoHeader.imageSize);
    printf("Saved: %s (%dx%d)\n", filename, width, height);
    return true;
}

//------------------------------------------------------------------------------
// UTF-8 → UTF-16 変換
//------------------------------------------------------------------------------

std::u16string utf8ToUtf16(const std::string& utf8) {
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

//------------------------------------------------------------------------------
// ラベル描画ヘルパー
//------------------------------------------------------------------------------

void drawLabel(richtext::TextRenderer& renderer,
               const richtext::TextStyle& baseStyle,
               const std::string& label, float x, float y) {
    richtext::Appearance gray;
    gray.addFill(0xFF888888);
    richtext::TextStyle s = baseStyle;
    s.fontSize = 13.0f;
    s.fontWeight = 400;
    renderer.drawText(utf8ToUtf16(label), x, y, s, gray);
}

//------------------------------------------------------------------------------
// メイン
//------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    printf("=== Sequential Display (maxGlyphs) Sample ===\n\n");

    // サンプルテキスト定義
    struct Sample {
        const char* title;
        const char* text;
        float maxWidth;
        float fontSize;
        minikin::Bidi bidi;
        // maxGlyphs の段階（-1 = 全表示）
        std::vector<int> stages;
    };

    std::vector<Sample> samples = {
        // --- LTR サンプル ---
        {
            "1. Japanese + Emoji",
            u8"桜の季節🌸がやってきました！今年も美しい花を見に行きましょう🏯✨",
            700.0f, 24.0f, minikin::Bidi::DEFAULT_LTR,
            {5, 12, 20, 28, -1}
        },
        {
            "2. English + CJK mixed",
            u8"The quick brown fox 🦊 jumped over the lazy dog 🐕. "
            u8"素早い茶色の狐が怠惰な犬を飛び越えた。",
            700.0f, 22.0f, minikin::Bidi::DEFAULT_LTR,
            {8, 16, 30, 45, -1}
        },
        {
            "3. Korean + Emoji",
            u8"오늘 날씨가 정말 좋아요☀️! 공원에서 산책하면서 🌳 아이스크림🍦을 먹었어요.",
            700.0f, 22.0f, minikin::Bidi::DEFAULT_LTR,
            {6, 14, 22, 30, -1}
        },
        {
            "4. Chinese + Emoji",
            u8"春天来了🌱，万物复苏🌻。小鸟在枝头歌唱🐦，孩子们在草地上奔跑🏃‍♂️。",
            700.0f, 22.0f, minikin::Bidi::DEFAULT_LTR,
            {5, 12, 20, 28, -1}
        },
        {
            "5. Multilingual paragraph",
            u8"Hello World! こんにちは世界！안녕하세요 세계! "
            u8"你好世界！🌍🌏🌎 "
            u8"Diversity makes us stronger 💪✨ "
            u8"多様性は私たちを強くする",
            700.0f, 22.0f, minikin::Bidi::DEFAULT_LTR,
            {6, 15, 25, 40, 55, -1}
        },
        {
            "6. Emoji-heavy",
            u8"🎮 Game Over! 🏆 Score: ⭐⭐⭐⭐⭐ "
            u8"🎉🎊🥳 おめでとう！ 축하합니다! 恭喜！",
            700.0f, 24.0f, minikin::Bidi::DEFAULT_LTR,
            {4, 10, 18, 26, -1}
        },

        // --- RTL 混在サンプル（LTR パラグラフ方向） ---
        {
            "7. LTR paragraph: English + Arabic",
            u8"Welcome to مرحبًا بكم في the conference المؤتمر! Let's begin لنبدأ 🎤",
            700.0f, 22.0f, minikin::Bidi::LTR,
            {4, 10, 18, 26, -1}
        },
        {
            "8. LTR paragraph: Japanese + Arabic",
            u8"日本語テキスト مع نص عربي を混在させた文章です。複雑なレイアウト تخطيط معقد ✨",
            700.0f, 22.0f, minikin::Bidi::LTR,
            {5, 12, 20, 30, -1}
        },

        // --- RTL パラグラフ方向 ---
        {
            "9. RTL paragraph: Arabic + English",
            u8"مرحبًا بكم في Welcome to المؤتمر الدولي International Conference 🌍✨",
            700.0f, 22.0f, minikin::Bidi::RTL,
            {4, 10, 18, 26, -1}
        },
        {
            "10. RTL paragraph: Arabic + Japanese",
            u8"النص العربي 日本語テキスト مختلط مع 混在テスト في هذا المثال この例では 🏯",
            700.0f, 22.0f, minikin::Bidi::RTL,
            {5, 12, 20, 28, -1}
        },
        {
            "11. RTL paragraph: Arabic-dominant with emoji",
            u8"🌙 رمضان كريم! كل عام وأنتم بخير 🕌✨ Happy Ramadan おめでとう 🎉",
            700.0f, 24.0f, minikin::Bidi::RTL,
            {4, 10, 18, 26, -1}
        },
    };

    const int NUM_SAMPLES = static_cast<int>(samples.size());

    // 各サンプルの段階数を数えて、必要な高さを計算
    const float LEFT = 20.0f;
    const float SECTION_GAP = 30.0f;
    const float STAGE_GAP = 8.0f;
    const float LABEL_HEIGHT = 20.0f;
    const float BOX_HEIGHT = 80.0f;  // パラグラフ描画領域の高さ

    // 概算で HEIGHT を決定
    float totalHeight = 40.0f;  // 上マージン
    for (int s = 0; s < NUM_SAMPLES; ++s) {
        totalHeight += LABEL_HEIGHT + 4.0f;  // タイトル
        totalHeight += static_cast<float>(samples[s].stages.size()) * (BOX_HEIGHT + STAGE_GAP);
        totalHeight += SECTION_GAP;
    }

    const int WIDTH = 760;
    const int HEIGHT = static_cast<int>(totalHeight + 40.0f);

    printf("Canvas: %d x %d\n", WIDTH, HEIGHT);

    std::vector<uint32_t> buffer(WIDTH * HEIGHT, 0xFFFFFFFF);

    //--------------------------------------------------------------------------
    // フォント登録
    //--------------------------------------------------------------------------
    printf("Registering fonts...\n");

    auto& fm = richtext::FontManager::instance();
    const std::string dataDir = "./data/";

    struct FontEntry { const char* file; const char* name; };
    FontEntry fonts[] = {
        {"NotoSans-Regular.ttf",        "sans"},
        {"NotoSansJP-Regular.otf",      "ja"},
        {"NotoSansKR-Regular.otf",      "ko"},
        {"NotoSansSC-Regular.otf",      "zh-hans"},
        {"NotoSansArabic-Regular.ttf",  "ar"},
        {"NotoNaskhArabic-Regular.ttf", "ar-naskh"},
        {"NotoColorEmoji.ttf",          "emoji"},
    };
    for (const auto& f : fonts) {
        std::string path = dataDir + f.file;
        if (fm.registerFont(path, f.name)) {
            printf("  [%s] %s\n", f.name, f.file);
        } else {
            fprintf(stderr, "  [%s] FAILED: %s\n", f.name, f.file);
        }
    }
    fm.registerLocale("ja_JP-u-lb-strict");
    fm.registerLocale("ko_KR");
    fm.registerLocale("zh_CN");

    //--------------------------------------------------------------------------
    // TextRenderer 初期化
    //--------------------------------------------------------------------------
    richtext::TextRenderer renderer;
    renderer.setCanvas(buffer.data(), WIDTH, HEIGHT, WIDTH * sizeof(uint32_t));

    auto multiCollection = fm.createCollection({"sans", "ja", "ko", "zh-hans", "ar", "emoji"});

    //--------------------------------------------------------------------------
    // 各サンプルを段階的に描画
    //--------------------------------------------------------------------------
    float y = 30.0f;

    richtext::TextStyle labelStyle;
    labelStyle.fontCollection = multiCollection;
    labelStyle.fontSize = 13.0f;

    for (int s = 0; s < NUM_SAMPLES; ++s) {
        const auto& sample = samples[s];
        printf("\n%s\n", sample.title);

        // セクションタイトル
        drawLabel(renderer, labelStyle, sample.title, LEFT, y);
        y += LABEL_HEIGHT + 4.0f;

        // スタイル設定
        richtext::TextStyle style;
        style.fontCollection = multiCollection;
        style.fontSize = sample.fontSize;
        style.bidi = sample.bidi;

        richtext::Appearance appearance;
        appearance.addFill(0xFF333333);

        // ParagraphLayout を事前計算
        richtext::ParagraphLayout para;
        para.layout(utf8ToUtf16(sample.text), sample.maxWidth, style);

        size_t totalGlyphs = para.getTotalGlyphCount();
        printf("  totalGlyphs = %zu, lines = %zu\n",
               totalGlyphs, para.getLineCount());

        // 各段階を描画
        for (int maxGlyphs : sample.stages) {
            // ラベル（maxGlyphs 値を表示）
            char buf[64];
            if (maxGlyphs < 0) {
                snprintf(buf, sizeof(buf), "maxGlyphs = -1 (all: %zu)", totalGlyphs);
            } else {
                snprintf(buf, sizeof(buf), "maxGlyphs = %d / %zu", maxGlyphs, totalGlyphs);
            }
            drawLabel(renderer, labelStyle, buf, LEFT, y);

            // 描画領域
            richtext::RectF rect(LEFT, y + 16.0f, sample.maxWidth, BOX_HEIGHT - 16.0f);

            renderer.drawParagraphLayout(
                para, rect,
                richtext::ParagraphLayout::HAlign::Left,
                richtext::ParagraphLayout::VAlign::Top,
                style, appearance,
                maxGlyphs);

            printf("  stage: maxGlyphs=%d\n", maxGlyphs);
            y += BOX_HEIGHT + STAGE_GAP;
        }

        y += SECTION_GAP;
    }

    //--------------------------------------------------------------------------
    // 保存
    //--------------------------------------------------------------------------
    printf("\nSyncing and saving...\n");
    renderer.sync();

    if (saveBMP("output_sequential.bmp", buffer.data(), WIDTH, HEIGHT)) {
        printf("\nSuccess! Output saved to output_sequential.bmp\n");
    } else {
        fprintf(stderr, "\nFailed to save output_sequential.bmp\n");
        return 1;
    }

    return 0;
}
