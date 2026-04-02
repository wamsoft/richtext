/**
 * sample_texture.cpp
 *
 * TextureAtlas を使った描画サンプル
 *
 * 1. グリフをテクスチャアトラスに事前レンダリング
 * 2. CopyRect を使ってアトラスから画面バッファに転送
 * 3. アトラス内容と転送結果の両方を BMP で出力
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <map>

#include "richtext/FontManager.hpp"
#include "richtext/FontFace.hpp"
#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/TextLayout.hpp"
#include "richtext/ParagraphLayout.hpp"
#include "richtext/StyledLayout.hpp"
#include "richtext/TextRenderer.hpp"
#include "richtext/TextureAtlas.hpp"

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
// ITexture 実装（メモリバッファ）
//------------------------------------------------------------------------------

class MemoryTexture : public richtext::ITexture {
public:
    MemoryTexture(int width, int height)
        : width_(width), height_(height), pixels_(width * height, 0) {}

    int getWidth() const override { return width_; }
    int getHeight() const override { return height_; }

    void update(int x, int y, int width, int height,
                const uint32_t* pixels, int pitch) override {
        int bytesPerRow = pitch;
        int pixelsPerRow = bytesPerRow / sizeof(uint32_t);
        for (int row = 0; row < height; ++row) {
            int dstY = y + row;
            if (dstY < 0 || dstY >= height_) continue;
            int copyW = std::min(width, width_ - x);
            if (copyW <= 0 || x < 0) continue;
            std::memcpy(&pixels_[dstY * width_ + x],
                        &pixels[row * pixelsPerRow],
                        copyW * sizeof(uint32_t));
        }
    }

    const uint32_t* data() const { return pixels_.data(); }
    uint32_t* data() { return pixels_.data(); }

private:
    int width_, height_;
    std::vector<uint32_t> pixels_;
};

//------------------------------------------------------------------------------
// CopyRect を使った ARGB ブリット（アルファブレンド付き）
//------------------------------------------------------------------------------

void blitCopyRect(uint32_t* dst, int dstW, int dstH,
                  const uint32_t* src, int srcW, int srcH,
                  const richtext::CopyRect& cr) {
    for (int row = 0; row < cr.srcHeight; ++row) {
        int sy = cr.srcY + row;
        int dy = static_cast<int>(cr.dstY) + row;
        if (sy < 0 || sy >= srcH || dy < 0 || dy >= dstH) continue;
        for (int col = 0; col < cr.srcWidth; ++col) {
            int sx = cr.srcX + col;
            int dx = static_cast<int>(cr.dstX) + col;
            if (sx < 0 || sx >= srcW || dx < 0 || dx >= dstW) continue;

            uint32_t srcPx = src[sy * srcW + sx];
            uint8_t sa = (srcPx >> 24) & 0xFF;
            if (sa == 0) continue;

            uint32_t& dstPx = dst[dy * dstW + dx];
            if (sa == 255) {
                dstPx = srcPx;
            } else {
                // アルファブレンド
                uint8_t da = (dstPx >> 24) & 0xFF;
                uint8_t dr = (dstPx >> 16) & 0xFF;
                uint8_t dg = (dstPx >>  8) & 0xFF;
                uint8_t db =  dstPx        & 0xFF;
                uint8_t sr = (srcPx >> 16) & 0xFF;
                uint8_t sg = (srcPx >>  8) & 0xFF;
                uint8_t sb =  srcPx        & 0xFF;
                uint8_t oa = sa + ((da * (255 - sa)) >> 8);
                uint8_t or_ = (sr * sa + dr * (255 - sa)) >> 8;
                uint8_t og = (sg * sa + dg * (255 - sa)) >> 8;
                uint8_t ob = (sb * sa + db * (255 - sa)) >> 8;
                dstPx = (oa << 24) | (or_ << 16) | (og << 8) | ob;
            }
        }
    }
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

    printf("=== TextureAtlas Sample ===\n\n");

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
        {"NotoSans-Regular.ttf",        "sans"},
        {"NotoSansJP-Regular.otf",      "ja"},
        {"NotoSansKR-Regular.otf",      "ko"},
        {"NotoSansSC-Regular.otf",      "zh-hans"},
        {"NotoSansArabic-Regular.ttf",  "ar"},
        {"NotoColorEmoji.ttf",          "emoji"},
    };
    for (const auto& f : fonts) {
        if (fm.registerFont(f.file, f.name)) {
            printf("  [%s] %s\n", f.name, f.file);
        } else {
            fprintf(stderr, "  [%s] FAILED: %s\n", f.name, f.file);
        }
    }
    fm.registerLocale("ja_JP-u-lb-strict");

    auto collection = fm.createCollection({"sans", "ja", "ko", "zh-hans", "ar", "emoji"});

    //--------------------------------------------------------------------------
    // ラベル用の TextRenderer（画面バッファに直接描画）
    //--------------------------------------------------------------------------
    const int SCREEN_W = 800;
    const int SCREEN_H = 1200;
    std::vector<uint32_t> screenBuffer(SCREEN_W * SCREEN_H, 0xFFFFFFFF);

    richtext::TextRenderer labelRenderer;
    labelRenderer.setCanvas(screenBuffer.data(), SCREEN_W, SCREEN_H,
                            SCREEN_W * sizeof(uint32_t));

    richtext::TextStyle labelStyle;
    labelStyle.fontCollection = collection;

    //--------------------------------------------------------------------------
    // テクスチャアトラス作成（1024x512）
    //--------------------------------------------------------------------------
    const int ATLAS_W = 1024;
    const int ATLAS_H = 512;
    MemoryTexture texture(ATLAS_W, ATLAS_H);
    richtext::TextureAtlas atlas(&texture);

    printf("\nAtlas size: %dx%d\n", ATLAS_W, ATLAS_H);

    //--------------------------------------------------------------------------
    // サンプルテキストとスタイルの定義
    //--------------------------------------------------------------------------
    struct Sample {
        const char* title;
        const char* text;
        float fontSize;
        uint32_t fillColor;
        uint32_t strokeColor;
        float strokeWidth;
    };

    Sample samples[] = {
        {
            "1. Basic text (fill only)",
            u8"Hello World! こんにちは世界！",
            28.0f, 0xFF333333, 0, 0
        },
        {
            "2. Outlined text",
            u8"Outlined 縁取りテキスト 🌟",
            32.0f, 0xFFFF6600, 0xFF000000, 2.0f
        },
        {
            "3. Multilingual paragraph",
            u8"The quick brown fox jumped over the lazy dog. "
            u8"素早い茶色の狐が怠惰な犬を飛び越えた。"
            u8"빠른 갈색 여우가 게으른 개를 뛰어넘었다.",
            22.0f, 0xFF222222, 0, 0
        },
        {
            "4. Large with shadow",
            u8"Big Shadow 大きな影 🎮✨",
            36.0f, 0xFFFFFFFF, 0xFF444444, 3.0f
        },
    };
    const int NUM_SAMPLES = sizeof(samples) / sizeof(samples[0]);

    //--------------------------------------------------------------------------
    // Step 1: 全レイアウトを準備してアトラスに追加（commit は最後に1回）
    //--------------------------------------------------------------------------
    printf("\n--- Step 1: Preparing layouts and adding to atlas ---\n");

    // --- サンプル 1-4: ParagraphLayout ---
    struct PreparedLayout {
        richtext::ParagraphLayout para;
        richtext::TextStyle style;
        richtext::Appearance appearance;
    };
    std::vector<PreparedLayout> layouts(NUM_SAMPLES);

    for (int i = 0; i < NUM_SAMPLES; ++i) {
        auto& s = samples[i];
        auto& pl = layouts[i];

        pl.style.fontCollection = collection;
        pl.style.fontSize = s.fontSize;

        if (s.strokeColor != 0 && s.strokeWidth > 0) {
            pl.appearance.addStroke(s.strokeColor, s.strokeWidth);
        }
        pl.appearance.addFill(s.fillColor);

        pl.para.layout(utf8ToUtf16(s.text), 700.0f, pl.style);

        bool ok = atlas.addParagraphLayout(pl.para, pl.style, pl.appearance);
        printf("  %s: %zu glyphs, lines=%zu %s\n",
               s.title,
               pl.para.getTotalCharCount(),
               pl.para.getLineCount(),
               ok ? "OK" : "OVERFLOW");
    }

    // --- サンプル 5: 逐次表示デモ（ParagraphLayout） ---
    richtext::TextStyle seqStyle;
    seqStyle.fontCollection = collection;
    seqStyle.fontSize = 24.0f;

    richtext::Appearance seqAppearance;
    seqAppearance.addFill(0xFF005599);

    richtext::ParagraphLayout seqPara;
    seqPara.layout(
        utf8ToUtf16(u8"テクスチャアトラスからの逐次表示デモ 🚀 Sequential display from atlas!"),
        700.0f, seqStyle);

    {
        bool ok = atlas.addParagraphLayout(seqPara, seqStyle, seqAppearance);
        printf("  5. Sequential: %zu glyphs, lines=%zu %s\n",
               seqPara.getTotalCharCount(), seqPara.getLineCount(),
               ok ? "OK" : "OVERFLOW");
    }

    // --- サンプル 6: StyledLayout 逐次表示デモ ---
    std::map<std::string, richtext::TextStyle> stStyles;
    std::map<std::string, richtext::Appearance> stAppearances;

    richtext::TextStyle defStyle;
    defStyle.fontCollection = collection;
    defStyle.fontSize = 24.0f;
    stStyles["default"] = defStyle;

    richtext::Appearance defApp;
    defApp.addFill(0xFF333333);
    stAppearances["default"] = defApp;

    richtext::TextStyle emphStyle = defStyle;
    emphStyle.fontWeight = 700;
    stStyles["emph"] = emphStyle;
    richtext::Appearance emphApp;
    emphApp.addFill(0xFFCC0000);
    stAppearances["emph"] = emphApp;

    richtext::TextStyle hlStyle = defStyle;
    hlStyle.fontSize = 30.0f;
    stStyles["hl"] = hlStyle;
    richtext::Appearance hlApp;
    hlApp.addStroke(0xFF003366, 1.5f);
    hlApp.addFill(0xFF0077CC);
    stAppearances["hl"] = hlApp;

    richtext::StyledLayout styledLayout;
    styledLayout.layout(
        utf8ToUtf16(
            u8"テクスチャ<style name='emph'>アトラス</style>から"
            u8"<style name='hl'>StyledLayout</style>で逐次表示🚀"
        ),
        700.0f, 200.0f,
        richtext::ParagraphLayout::HAlign::Left,
        richtext::ParagraphLayout::VAlign::Top,
        stStyles, stAppearances);

    {
        bool ok = atlas.addStyledLayout(styledLayout);
        size_t totalChars = styledLayout.getTotalCharCount();
        printf("  6. StyledLayout: %zu chars, %zu glyphs, %zu lines %s\n",
               totalChars, styledLayout.getTotalCharCount(),
               styledLayout.getLineCount(),
               ok ? "OK" : "OVERFLOW");
    }

    //--------------------------------------------------------------------------
    // Step 2: アトラスをテクスチャに一括書き込み
    //--------------------------------------------------------------------------
    printf("\n--- Step 2: Committing atlas to texture ---\n");
    atlas.commit();

    saveBMP("output_atlas.bmp", texture.data(), ATLAS_W, ATLAS_H);

    //--------------------------------------------------------------------------
    // Step 3: CopyRect で画面バッファに転送（全セクション）
    //--------------------------------------------------------------------------
    printf("\n--- Step 3: Blitting from atlas to screen ---\n");

    float y = 20.0f;

    // --- サンプル 1-4 ---
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        auto& s = samples[i];
        auto& pl = layouts[i];

        drawLabel(labelRenderer, labelStyle, s.title, 20.0f, y);
        y += 20.0f;

        richtext::RectF rect(20.0f, y, 700.0f, 200.0f);
        auto copyRects = atlas.getCopyRects(
            pl.para, rect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            pl.style, pl.appearance);

        printf("  %s: %zu copy rects\n", s.title, copyRects.size());

        for (const auto& cr : copyRects) {
            blitCopyRect(screenBuffer.data(), SCREEN_W, SCREEN_H,
                         texture.data(), ATLAS_W, ATLAS_H, cr);
        }

        y += pl.para.getTotalHeight() + 30.0f;
    }

    // --- サンプル 5: 逐次表示デモ ---
    drawLabel(labelRenderer, labelStyle,
              "5. Sequential display via CopyRect (maxChars = 5, 15, 30, all)",
              20.0f, y);
    y += 20.0f;

    {
        size_t total = seqPara.getTotalCharCount();
        int stages[] = {5, 15, 30, -1};

        for (int maxChars : stages) {
            char buf[64];
            if (maxChars < 0) {
                snprintf(buf, sizeof(buf), "maxChars = -1 (all: %zu)", total);
            } else {
                snprintf(buf, sizeof(buf), "maxChars = %d / %zu", maxChars, total);
            }
            drawLabel(labelRenderer, labelStyle, buf, 30.0f, y);
            y += 16.0f;

            richtext::RectF rect(30.0f, y, 700.0f, 60.0f);
            auto rects = atlas.getCopyRects(
                seqPara, rect,
                richtext::ParagraphLayout::HAlign::Left,
                richtext::ParagraphLayout::VAlign::Top,
                seqStyle, seqAppearance, maxChars);

            for (const auto& cr : rects) {
                blitCopyRect(screenBuffer.data(), SCREEN_W, SCREEN_H,
                             texture.data(), ATLAS_W, ATLAS_H, cr);
            }

            y += 40.0f;
        }
    }

    // --- サンプル 6: StyledLayout 逐次表示デモ ---
    drawLabel(labelRenderer, labelStyle,
              "6. StyledLayout + TextureAtlas (maxChars = 5, 12, 20, all)",
              20.0f, y);
    y += 20.0f;

    {
        size_t totalChars = styledLayout.getTotalCharCount();

        int stages[] = {5, 12, 20, -1};
        for (int maxChars : stages) {
            char buf[64];
            if (maxChars < 0) {
                snprintf(buf, sizeof(buf), "maxChars = -1 (all: %zu chars)", totalChars);
            } else {
                snprintf(buf, sizeof(buf), "maxChars = %d / %zu chars", maxChars, totalChars);
            }
            drawLabel(labelRenderer, labelStyle, buf, 30.0f, y);
            y += 16.0f;

            auto stRects = atlas.getCopyRects(styledLayout, 30.0f, y, maxChars);
            printf("  stage maxChars=%d: %zu copy rects\n", maxChars, stRects.size());

            for (const auto& cr : stRects) {
                blitCopyRect(screenBuffer.data(), SCREEN_W, SCREEN_H,
                             texture.data(), ATLAS_W, ATLAS_H, cr);
            }

            y += 44.0f;
        }
    }

    //--------------------------------------------------------------------------
    // 保存
    //--------------------------------------------------------------------------
    printf("\n--- Saving ---\n");
    labelRenderer.sync();

    saveBMP("output_texture.bmp", screenBuffer.data(), SCREEN_W, SCREEN_H);

    printf("\n=== Done ===\n");
    printf("  output_atlas.bmp   - Atlas contents (glyph cache)\n");
    printf("  output_texture.bmp - Screen output (blitted from atlas)\n");

    return 0;
}
