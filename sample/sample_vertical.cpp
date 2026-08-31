/**
 * sample_vertical.cpp
 *
 * 縦組み（Phase 1）の動作確認サンプル
 *
 * 縦 1 行のベタ組みと、和文正立／欧文横倒しの混在を確認する。
 * data/ 以下の Noto フォントを使用し、output_vertical.bmp を出力する。
 *
 * ※ リポジトリルートから実行すること（フォントを ./data/ から読む）
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
#include "richtext/TextRenderer.hpp"
#include "richtext/vertical/VerticalLayout.hpp"

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
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return false;
    }
    file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

    std::vector<uint32_t> bgrPixels(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int srcIdx = y * width + x;
            const int dstIdx = (height - 1 - y) * width + x;
            bgrPixels[dstIdx] = pixels[srcIdx];
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
// メイン
//------------------------------------------------------------------------------

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    printf("=== richtext Vertical Sample (Phase 1) ===\n\n");

    const int WIDTH  = 900;
    const int HEIGHT = 1000;
    std::vector<uint32_t> buffer(static_cast<size_t>(WIDTH) * HEIGHT, 0xFFFFFFFF);

    //--------------------------------------------------------------------------
    // 1. フォント登録
    //--------------------------------------------------------------------------
    printf("1. Registering fonts from data/...\n");

    auto& fm = richtext::FontManager::instance();
    const std::string dataDir = "./data/";

    fm.setFontDataLoader([&dataDir](const std::string& name) -> richtext::FontDataBuffer {
        std::string path = dataDir + name;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return nullptr;
        auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        auto buf = std::make_shared<std::vector<uint8_t>>(size);
        if (!file.read(reinterpret_cast<char*>(buf->data()), size)) return nullptr;
        return buf;
    });

    struct FontEntry { const char* file; const char* name; };
    const FontEntry fonts[] = {
        {"NotoSansJP-Regular.otf",  "ja"},
        {"NotoSans-Regular.ttf",    "sans"},
        {"NotoSerifJP-Regular.otf", "serif-ja"},
        {"NotoSerif-Regular.ttf",   "serif"},
    };
    for (const auto& f : fonts) {
        if (!fm.registerFont(f.file, f.name)) {
            fprintf(stderr, "   [%s] FAILED: %s\n", f.name, f.file);
        }
    }
    fm.registerLocale("ja_JP-u-lb-strict");

    //--------------------------------------------------------------------------
    // 2. レンダラ・スタイル
    //--------------------------------------------------------------------------
    richtext::TextRenderer renderer;
    renderer.setCanvas(buffer.data(), WIDTH, HEIGHT,
                       WIDTH * static_cast<int>(sizeof(uint32_t)));

    auto gothic = fm.createCollection({"ja", "sans"});
    auto mincho = fm.createCollection({"serif-ja", "serif"});

    richtext::TextStyle style;
    style.fontCollection = gothic;
    style.fontSize = 32.0f;
    style.localeId = fm.getLocaleId("ja_JP-u-lb-strict");

    richtext::TextStyle minchoStyle = style;
    minchoStyle.fontCollection = mincho;

    richtext::TextStyle smallStyle = style;
    smallStyle.fontSize = 14.0f;

    richtext::Appearance black;
    black.addFill(0xFF111111);
    richtext::Appearance gray;
    gray.addFill(0xFF888888);

    //--------------------------------------------------------------------------
    // 3. 縦組み描画
    //--------------------------------------------------------------------------
    printf("2. Laying out vertical lines...\n");

    struct Column {
        const char* text;
        const richtext::TextStyle* style;
        richtext::vertical::TextOrientation orientation;
        const char* label;
    };

    const Column columns[] = {
        {u8"吾輩は猫である。名前はまだ無い。",
         &style, richtext::vertical::TextOrientation::Mixed, "mixed / gothic"},
        {u8"縦組みテスト「括弧」も、句読点も。",
         &style, richtext::vertical::TextOrientation::Mixed, "punctuation"},
        {u8"和文の中に ABC Vertical 123 が混ざる",
         &style, richtext::vertical::TextOrientation::Mixed, "latin sideways"},
        {u8"和文の中に ABC Vertical 123 が混ざる",
         &style, richtext::vertical::TextOrientation::Upright, "all upright"},
        {u8"明朝体でも組めること――長音記号ー〜",
         &minchoStyle, richtext::vertical::TextOrientation::Mixed, "mincho"},
        {u8"Sideways only: The quick brown fox",
         &style, richtext::vertical::TextOrientation::Sideways, "all sideways"},
    };

    const float top = 90.0f;
    const float columnGap = 20.0f;
    float centerX = WIDTH - 90.0f;   // vertical-rl: 右の列から左へ

    for (const auto& col : columns) {
        richtext::vertical::VerticalLayout layout;
        layout.layout(utf8ToUtf16(col.text), *col.style, col.orientation);

        // 列の枠（デバッグ用）
        renderer.drawRect(centerX + layout.getExtentLeft(), top,
                          layout.getWidth(), layout.getLength(),
                          0x00000000, 0xFFDDDDDD, 1.0f);
        // 縦ベースライン
        renderer.drawRect(centerX, top, 1.0f, layout.getLength(), 0xFFEEBBBB);

        richtext::RectF r = renderer.drawVerticalLayout(layout, centerX, top, black);

        // 列見出し（横組み）
        renderer.drawText(utf8ToUtf16(col.label),
                          centerX - 40.0f, top - 24.0f, smallStyle, gray);

        printf("   %-16s glyphs=%zu chars=%zu length=%.1f width=%.1f\n",
               col.label, layout.getGlyphCount(), layout.getCharCount(),
               layout.getLength(), layout.getWidth());

        centerX -= (r.width + columnGap + 60.0f);
    }

    //--------------------------------------------------------------------------
    // 4. 出力
    //--------------------------------------------------------------------------
    printf("\n3. Saving...\n");
    saveBMP("output_vertical.bmp", buffer.data(), WIDTH, HEIGHT);

    printf("\nDone.\n");
    return 0;
}
