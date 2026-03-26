/**
 * sample_render.cpp
 *
 * richtext ライブラリのサンプル実行プログラム
 * ビットマップに描画して BMP ファイルとして出力
 *
 * data/ 以下の Noto フォントを使用
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>

// richtext headers
#include "richtext/FontManager.hpp"
#include "richtext/FontFace.hpp"
#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/TextLayout.hpp"
#include "richtext/ParagraphLayout.hpp"
#include "richtext/TextRenderer.hpp"
#include "richtext/TagParser.hpp"

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

    // BMP は bottom-up、ARGB → BGRA 変換
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
// 枠線描画ヘルパー（デバッグ用）
//------------------------------------------------------------------------------

/**
 * ピクセルバッファに矩形枠線を描画する
 * @param buffer   ARGB ピクセルバッファ
 * @param bufW     バッファ幅
 * @param bufH     バッファ高さ
 * @param x, y     矩形左上座標
 * @param w, h     矩形サイズ
 * @param color    枠線色（ARGB）
 * @param thickness 枠線の太さ（ピクセル）
 */
void drawRect(uint32_t* buffer, int bufW, int bufH,
              int x, int y, int w, int h,
              uint32_t color = 0xFFCC4444, int thickness = 1) {
    auto putPixel = [&](int px, int py) {
        if (px >= 0 && px < bufW && py >= 0 && py < bufH) {
            buffer[py * bufW + px] = color;
        }
    };

    for (int t = 0; t < thickness; ++t) {
        // 上辺
        for (int px = x; px < x + w; ++px) putPixel(px, y + t);
        // 下辺
        for (int px = x; px < x + w; ++px) putPixel(px, y + h - 1 - t);
        // 左辺
        for (int py = y; py < y + h; ++py) putPixel(x + t, py);
        // 右辺
        for (int py = y; py < y + h; ++py) putPixel(x + w - 1 - t, py);
    }
}

/**
 * RectF を使った枠線描画（float → int 変換付き）
 */
void drawRectF(uint32_t* buffer, int bufW, int bufH,
               const richtext::RectF& rect,
               uint32_t color = 0xFFCC4444, int thickness = 1) {
    drawRect(buffer, bufW, bufH,
             static_cast<int>(rect.x), static_cast<int>(rect.y),
             static_cast<int>(rect.width), static_cast<int>(rect.height),
             color, thickness);
}

// セクションラベルを描画するヘルパー
void drawSectionLabel(richtext::TextRenderer& renderer,
                      const richtext::TextStyle& style,
                      const std::string& label, float x, float y) {
    richtext::Appearance gray;
    gray.addFill(0xFF888888);
    richtext::TextStyle labelStyle = style;
    labelStyle.fontSize = 14.0f;
    labelStyle.fontWeight = 400;
    renderer.drawText(utf8ToUtf16(label), x, y, labelStyle, gray);
}

//------------------------------------------------------------------------------
// メイン
//------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    printf("=== richtext Sample Renderer ===\n\n");

    const int WIDTH  = 900;
    const int HEIGHT = 4730;
    std::vector<uint32_t> buffer(WIDTH * HEIGHT, 0xFFFFFFFF);  // 白背景

    //--------------------------------------------------------------------------
    // 1. Noto フォントの登録（data/ ディレクトリ）
    //--------------------------------------------------------------------------
    printf("1. Registering Noto fonts from data/...\n");

    auto& fm = richtext::FontManager::instance();

    // data/ ディレクトリのパス（トップディレクトリから実行する前提）
    const std::string dataDir = "./data/";

    struct FontEntry {
        const char* file;
        const char* name;
    };
    FontEntry fonts[] = {
        {"NotoSans-Regular.ttf",        "sans"},
        {"NotoSansJP-Regular.otf",      "ja"},
        {"NotoSansKR-Regular.otf",      "ko"},
        {"NotoSansSC-Regular.otf",      "zh-hans"},
        {"NotoSansTC-Regular.otf",      "zh-hant"},
        {"NotoSansArabic-Regular.ttf",  "ar"},
        {"NotoNaskhArabic-Regular.ttf", "ar-naskh"},
        {"NotoColorEmoji.ttf",          "emoji"},
        {"NotoEmoji-Regular.ttf",       "emoji-mono"},
    };

    for (const auto& f : fonts) {
        std::string path = dataDir + f.file;
        if (fm.registerFont(path, f.name)) {
            printf("   [%s] %s\n", f.name, f.file);
        } else {
            fprintf(stderr, "   [%s] FAILED: %s\n", f.name, f.file);
        }
    }

    fm.registerLocale("ja_JP-u-lb-strict");
    fm.registerLocale("ko_KR");
    fm.registerLocale("zh_CN");
    fm.registerLocale("zh_TW");
    fm.registerLocale("ar");

    //--------------------------------------------------------------------------
    // 2. TextRenderer 初期化
    //--------------------------------------------------------------------------
    printf("\n2. Initializing TextRenderer...\n");
    richtext::TextRenderer renderer;
    renderer.setCanvas(buffer.data(), WIDTH, HEIGHT, WIDTH * sizeof(uint32_t));

    //--------------------------------------------------------------------------
    // 3. フォントコレクション・スタイルの設定
    //--------------------------------------------------------------------------
    printf("\n3. Setting up font collections and styles...\n");

    // 各言語用コレクション（フォールバックチェーン付き）
    auto jaCollection = fm.createCollection({"ja", "sans", "emoji"});
    auto koCollection = fm.createCollection({"ko", "sans", "emoji"});
    auto zhCollection = fm.createCollection({"zh-hans", "sans", "emoji"});
    auto multiCollection = fm.createCollection({"sans", "ja", "ko", "zh-hans", "ar", "emoji"});

    auto makeStyle = [&](std::shared_ptr<minikin::FontCollection> col,
                         float size, uint16_t weight = 400,
                         const char* locale = "ja_JP-u-lb-strict") {
        richtext::TextStyle s;
        s.fontCollection = col;
        s.fontSize       = size;
        s.fontWeight     = weight;
        s.localeId       = fm.getLocaleId(locale);
        return s;
    };

    richtext::TextStyle baseStyle = makeStyle(jaCollection, 28.0f);

    // 外観
    richtext::Appearance blackFill;
    blackFill.addFill(0xFF111111);

    richtext::Appearance redFill;
    redFill.addFill(0xFFCC0000);

    richtext::Appearance blueFill;
    blueFill.addFill(0xFF0033CC);

    richtext::Appearance greenFill;
    greenFill.addFill(0xFF007700);

    richtext::Appearance whiteOutline;
    whiteOutline.addStroke(0xFF000000, 2.5f);
    whiteOutline.addFill(0xFFFFFFFF);

    richtext::Appearance shadowBlue;
    shadowBlue.addFill(0x60000000, 3.0f, 3.0f);
    shadowBlue.addFill(0xFF0055AA);

    float y = 50.0f;
    const float LINE = 48.0f;
    const float SECTION = 30.0f;
    const float LEFT = 40.0f;
    const float PARA_W = WIDTH - LEFT * 2;

    // 枠線色の定義
    const uint32_t BORDER_RED    = 0xFFDD4444;
    const uint32_t BORDER_BLUE   = 0xFF4444DD;
    const uint32_t BORDER_GREEN  = 0xFF44AA44;
    const uint32_t BORDER_ORANGE = 0xFFDD8800;

    //--------------------------------------------------------------------------
    // 4. 基本テキスト（Notoフォント確認）
    //--------------------------------------------------------------------------
    printf("\n4. Basic text with Noto fonts...\n");
    drawSectionLabel(renderer, baseStyle, "[4] Basic LTR text (Noto Sans JP)", LEFT, y);
    y += 18;
    renderer.drawText(utf8ToUtf16("Hello, richtext! 0123456789"),
                      LEFT, y, baseStyle, whiteOutline);
    y += LINE;
    renderer.drawText(utf8ToUtf16("こんにちは、リッチテキスト！"),
                      LEFT, y, baseStyle, whiteOutline);
    y += LINE + SECTION;

    //--------------------------------------------------------------------------
    // 5. スタイル変更（太字・サイズ・影）
    //--------------------------------------------------------------------------
    printf("\n5. Styled text...\n");
    drawSectionLabel(renderer, baseStyle, "[5] Bold / Shadow", LEFT, y);
    y += 18;

    richtext::TextStyle boldStyle = makeStyle(jaCollection, 36.0f, 700);
    renderer.drawText(utf8ToUtf16("太字テキスト Bold"), LEFT, y, boldStyle, redFill);
    y += 44;

    renderer.drawText(utf8ToUtf16("Shadow Effect"), LEFT, y, baseStyle, shadowBlue);
    y += LINE + SECTION;

    //--------------------------------------------------------------------------
    // 6. パラグラフ（行分割・左揃え）＋枠線表示
    //--------------------------------------------------------------------------
    printf("\n6. Paragraph (left-aligned) with border...\n");
    drawSectionLabel(renderer, baseStyle, "[6] Paragraph - Left (with border)", LEFT, y);
    y += 18;

    {
        richtext::TextStyle paraStyle = makeStyle(jaCollection, 22.0f);
        richtext::RectF paraRect(LEFT, y, PARA_W, 120.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, paraRect, BORDER_RED, 2);
        renderer.drawParagraph(
            utf8ToUtf16("これは複数行のパラグラフです。minikin の行分割アルゴリズムによって、"
                        "指定した幅に収まるよう自動的に改行されます。日本語の禁則処理も適用されます。"),
            paraRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            paraStyle, blackFill);
    }
    y += 130 + SECTION;

    //--------------------------------------------------------------------------
    // 7. タグ付きテキスト（drawStyledText）
    //--------------------------------------------------------------------------
    printf("\n7. Tagged text (drawStyledText)...\n");
    drawSectionLabel(renderer, baseStyle, "[7] Tagged text (drawStyledText)", LEFT, y);
    y += 18;

    {
        std::map<std::string, richtext::TextStyle>    styles;
        std::map<std::string, richtext::Appearance>   appearances;
        styles["default"]      = makeStyle(jaCollection, 28.0f);
        appearances["default"] = blackFill;

        richtext::RectF tagRect(LEFT, y, PARA_W, 100.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, tagRect, BORDER_BLUE);
        renderer.drawStyledText(
            utf8ToUtf16("<b>太字</b>と<i>斜体</i>と"
                        "<color value=0xFFCC0000>赤色</color>と"
                        "<font size=36>大きな</font>テキスト"),
            tagRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
    }
    y += 100 + SECTION;

    //--------------------------------------------------------------------------
    // 8. 上付き・下付き（sup / sub）
    //--------------------------------------------------------------------------
    printf("\n8. Superscript / Subscript...\n");
    drawSectionLabel(renderer, baseStyle, "[8] Superscript / Subscript", LEFT, y);
    y += 18;

    {
        std::map<std::string, richtext::TextStyle>    styles;
        std::map<std::string, richtext::Appearance>   appearances;
        styles["default"]      = makeStyle(jaCollection, 28.0f);
        appearances["default"] = blackFill;

        richtext::RectF supRect(LEFT, y, PARA_W, 60.0f);
        renderer.drawStyledText(
            utf8ToUtf16("H<sub>2</sub>O  E=mc<sup>2</sup>  x<sup>n+1</sup> = x<sup>n</sup> + 1"),
            supRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
    }
    y += 60 + SECTION;

    //--------------------------------------------------------------------------
    // 9. 縁取り・影タグの組み合わせ網羅
    //--------------------------------------------------------------------------
    printf("\n9. Outline / Shadow tag combinations...\n");
    drawSectionLabel(renderer, baseStyle, "[9] Outline / Shadow combinations (Normal, Outline, Shadow, Both)", LEFT, y);
    y += 18;

    {
        std::map<std::string, richtext::TextStyle>    styles;
        std::map<std::string, richtext::Appearance>   appearances;
        styles["default"]      = makeStyle(jaCollection, 30.0f);
        appearances["default"] = blackFill;

        // 9a. 通常テキスト
        richtext::RectF normalRect(LEFT, y, PARA_W, 50.0f);
        renderer.drawStyledText(
            utf8ToUtf16("通常テキスト（効果なし）"),
            normalRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
        y += 50;

        // 9b. 縁取りのみ
        richtext::RectF outlineRect(LEFT, y, PARA_W, 50.0f);
        renderer.drawStyledText(
            utf8ToUtf16("<outline color=0xFF000000 width=3><color value=0xFFFFDD00>縁取りのみ Outline Only</color></outline>"),
            outlineRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
        y += 50;

        // 9c. 影のみ
        richtext::RectF shadowRect(LEFT, y, PARA_W, 50.0f);
        renderer.drawStyledText(
            utf8ToUtf16("<shadow color=0x88000000 x=3 y=3>影のみ Shadow Only</shadow>"),
            shadowRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
        y += 50;

        // 9d. 縁取り＋影（両方）
        richtext::RectF bothRect(LEFT, y, PARA_W, 50.0f);
        renderer.drawStyledText(
            utf8ToUtf16("<outline color=0xFF000000 width=3><shadow color=0x88000000 x=3 y=3>"
                        "<color value=0xFF44CCFF>縁取り＋影 Both</color></shadow></outline>"),
            bothRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
        y += 50;

        // 9e. 効果の切り替え（隣接）
        richtext::RectF seqRect(LEFT, y, PARA_W, 50.0f);
        renderer.drawStyledText(
            utf8ToUtf16("<outline color=0xFF000000 width=3><color value=0xFFFFDD00>縁取り</color></outline>"
                        "→通常→"
                        "<shadow color=0x88000000 x=3 y=3>影付き</shadow>"
                        "→通常"),
            seqRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
        y += 50;
    }
    y += SECTION;

    //--------------------------------------------------------------------------
    // 10. 双方向テキスト（Bidi）
    //--------------------------------------------------------------------------
    printf("\n10. Bidirectional text (Bidi)...\n");

    // 10a. Bidi::LTR（強制LTR段落方向）
    // LTR段落: Hello が左端、アラビア語は内部で右から左に表示
    drawSectionLabel(renderer, baseStyle, "[10a] Bidi::LTR - LTR paragraph direction", LEFT, y);
    y += 18;
    {
        richtext::TextStyle ltrStyle = makeStyle(multiCollection, 28.0f);
        ltrStyle.bidi = minikin::Bidi::LTR;
        auto bidiText = utf8ToUtf16("Hello \u0645\u0631\u062D\u0628\u0627 World \u0634\u0643\u0631\u0627");
        richtext::RectF ltrRect(LEFT, y, PARA_W, 40.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, ltrRect, BORDER_RED);
        renderer.drawParagraph(bidiText, ltrRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            ltrStyle, blackFill);
    }
    y += 48 + 8;

    // 10b. Bidi::RTL（強制RTL段落方向）
    // RTL段落: アラビア語 شكرا が左端（視覚的右から左）、Hello が右端
    drawSectionLabel(renderer, baseStyle, "[10b] Bidi::RTL - RTL paragraph direction", LEFT, y);
    y += 18;
    {
        richtext::TextStyle rtlStyle = makeStyle(multiCollection, 28.0f);
        rtlStyle.bidi = minikin::Bidi::RTL;
        auto bidiText = utf8ToUtf16("Hello \u0645\u0631\u062D\u0628\u0627 World \u0634\u0643\u0631\u0627");
        richtext::RectF rtlRect(LEFT, y, PARA_W, 40.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, rtlRect, BORDER_BLUE);
        renderer.drawParagraph(bidiText, rtlRect,
            richtext::ParagraphLayout::HAlign::Right,
            richtext::ParagraphLayout::VAlign::Top,
            rtlStyle, blackFill);
    }
    y += 48 + 8;

    // 10c/10d. 日本語+アラビア語混在
    // 注: CJK・ひらがな・カタカナの Unicode bidi クラスは "L"（LTR）。
    // RTL段落ではラン（ブロック）の配置順は入れ替わるが、
    // 日本語ラン内部の文字順は LTR のまま維持される（UBA仕様通り）。
    drawSectionLabel(renderer, baseStyle, "[10c] Japanese+Arabic LTR (run order: JA > AR > JA)", LEFT, y);
    y += 18;
    {
        auto jaArCollection = fm.createCollection({"ja", "ar", "sans", "emoji"});
        richtext::TextStyle ltrStyle = makeStyle(jaArCollection, 28.0f, 400, "ja_JP");
        ltrStyle.bidi = minikin::Bidi::LTR;
        auto bidiText = utf8ToUtf16("\u65E5\u672C\u8A9E\u3068\u0627\u0644\u0639\u0631\u0628\u064A\u0629\u306E\u6DF7\u5728\u30C6\u30B9\u30C8");
        richtext::RectF rect(LEFT, y, PARA_W, 40.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, rect, BORDER_RED);
        renderer.drawParagraph(bidiText, rect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            ltrStyle, blackFill);
    }
    y += 48 + 8;

    // 10d. 同テキスト RTL — ランの配置順が逆転（JA < AR < JA）
    drawSectionLabel(renderer, baseStyle, "[10d] Same text RTL (run order reversed, chars unchanged)", LEFT, y);
    y += 18;
    {
        auto jaArCollection = fm.createCollection({"ja", "ar", "sans", "emoji"});
        richtext::TextStyle rtlStyle = makeStyle(jaArCollection, 28.0f, 400, "ja_JP");
        rtlStyle.bidi = minikin::Bidi::RTL;
        auto bidiText = utf8ToUtf16("\u65E5\u672C\u8A9E\u3068\u0627\u0644\u0639\u0631\u0628\u064A\u0629\u306E\u6DF7\u5728\u30C6\u30B9\u30C8");
        richtext::RectF rect(LEFT, y, PARA_W, 40.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, rect, BORDER_BLUE);
        renderer.drawParagraph(bidiText, rect,
            richtext::ParagraphLayout::HAlign::Right,
            richtext::ParagraphLayout::VAlign::Top,
            rtlStyle, blackFill);
    }
    y += 48 + SECTION;

    //==========================================================================
    // 11. 日本語長文テキスト折り返しサンプル（絵文字付き）
    //==========================================================================
    printf("\n11. Japanese long text with word wrap and emoji...\n");
    drawSectionLabel(renderer, baseStyle, "[11] Japanese long text + emoji (with border)", LEFT, y);
    y += 18;

    {
        richtext::TextStyle jaStyle = makeStyle(jaCollection, 24.0f, 400, "ja_JP-u-lb-strict");
        richtext::RectF jaRect(LEFT, y, PARA_W, 200.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, jaRect, BORDER_RED, 2);
        renderer.drawParagraph(
            utf8ToUtf16(
                "吾輩は猫である。名前はまだ無い。\U0001F431"
                "どこで生れたかとんと見当がつかぬ。何でも薄暗いじめじめした所で"
                "ニャーニャー泣いていた事だけは記憶している。\U0001F63F"
                "吾輩はここで始めて人間というものを見た。"
                "しかもあとで聞くとそれは書生という人間中で一番獰悪な種族であったそうだ。\U0001F4DA"
                "この書生というのは時々我々を捕えて煮て食うという話である。\U0001F372"),
            jaRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            jaStyle, blackFill);
    }
    y += 210 + SECTION;

    //==========================================================================
    // 12. 韓国語長文テキスト折り返しサンプル（絵文字付き）
    //==========================================================================
    printf("\n12. Korean long text with word wrap and emoji...\n");
    drawSectionLabel(renderer, baseStyle, "[12] Korean long text + emoji (with border)", LEFT, y);
    y += 18;

    {
        richtext::TextStyle koStyle = makeStyle(koCollection, 24.0f, 400, "ko_KR");
        richtext::RectF koRect(LEFT, y, PARA_W, 200.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, koRect, BORDER_BLUE, 2);
        renderer.drawParagraph(
            utf8ToUtf16(
                "모든 인간은 태어날 때부터 자유로우며 그 존엄과 권리에 있어 동등하다. \U0001F30F"
                "인간은 천부적으로 이성과 양심을 부여받았으며 서로 형제애의 정신으로 행동하여야 한다. \U0001F91D"
                "모든 사람은 인종, 피부색, 성별, 언어, 종교에 따른 어떠한 차별도 없이 "
                "이 선언에 규정된 모든 권리와 자유를 향유할 자격이 있다. \U0001F3F3\uFE0F"),
            koRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            koStyle, blackFill);
    }
    y += 210 + SECTION;

    //==========================================================================
    // 13. 中国語（簡体字）長文テキスト折り返しサンプル（絵文字付き）
    //==========================================================================
    printf("\n13. Chinese (Simplified) long text with word wrap and emoji...\n");
    drawSectionLabel(renderer, baseStyle, "[13] Chinese (Simplified) long text + emoji (with border)", LEFT, y);
    y += 18;

    {
        richtext::TextStyle zhStyle = makeStyle(zhCollection, 24.0f, 400, "zh_CN");
        richtext::RectF zhRect(LEFT, y, PARA_W, 200.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, zhRect, BORDER_GREEN, 2);
        renderer.drawParagraph(
            utf8ToUtf16(
                "人人生而自由，在尊严和权利上一律平等。\U0001F30D"
                "他们赋有理性和良心，并应以兄弟关系的精神相对待。\U0001F91D"
                "人人有资格享有本宣言所载的一切权利和自由，不分种族、肤色、性别、语言、"
                "宗教、政治或其他见解、国籍或社会出身、财产、出生或其他身分等任何区别。\U0001F3AF"
                "此外，不得因一人所属的国家或领土的政治的、行政的或者国际的地位之不同而有所区别。\U0001F5FA"),
            zhRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            zhStyle, blackFill);
    }
    y += 210 + SECTION;

    //==========================================================================
    // 14. CJK 字形比較サンプル（同一文字の日本語・繁体・簡体字形差）
    //==========================================================================
    printf("\n14. CJK glyph comparison (ja / zh-Hant / zh-Hans)...\n");
    drawSectionLabel(renderer, baseStyle, "[14] CJK glyph comparison: same codepoints, different locale+font", LEFT, y);
    y += 22;

    {
        // 字形が異なる代表的な漢字
        // 直・骨・角・令・花・草・者・黄・海・冷・領・返・飲・画・国・学
        const char* glyphChars = "直骨角令花草者黄海冷領返飲画国学";

        auto zhHantCollection = fm.createCollection({"zh-hant", "sans"});

        // 日本語
        drawSectionLabel(renderer, baseStyle, "Japanese (NotoSansJP, ja_JP):", LEFT, y);
        y += 16;
        {
            richtext::TextStyle jaGlyphStyle = makeStyle(jaCollection, 36.0f, 400, "ja_JP-u-lb-strict");
            renderer.drawText(utf8ToUtf16(glyphChars), LEFT, y, jaGlyphStyle, blackFill);
        }
        y += 50;

        // 繁体字（Traditional Chinese）
        drawSectionLabel(renderer, baseStyle, "Traditional Chinese (NotoSansTC, zh_TW):", LEFT, y);
        y += 16;
        {
            richtext::TextStyle tcGlyphStyle = makeStyle(zhHantCollection, 36.0f, 400, "zh_TW");
            renderer.drawText(utf8ToUtf16(glyphChars), LEFT, y, tcGlyphStyle, blueFill);
        }
        y += 50;

        // 簡体字（Simplified Chinese）
        drawSectionLabel(renderer, baseStyle, "Simplified Chinese (NotoSansSC, zh_CN):", LEFT, y);
        y += 16;
        {
            richtext::TextStyle scGlyphStyle = makeStyle(zhCollection, 36.0f, 400, "zh_CN");
            renderer.drawText(utf8ToUtf16(glyphChars), LEFT, y, scGlyphStyle, redFill);
        }
        y += 50;

        // 注釈
        {
            richtext::TextStyle noteStyle = makeStyle(jaCollection, 13.0f);
            richtext::Appearance noteApp;
            noteApp.addFill(0xFF999999);
            renderer.drawText(
                utf8ToUtf16("※ 同一の Unicode コードポイントでも、フォント・ロケール設定により字形が異なります"),
                LEFT, y, noteStyle, noteApp);
        }
        y += 20;
    }
    y += SECTION;

    //==========================================================================
    // 15. アラビア語長文テキスト折り返しサンプル（RTL・絵文字付き）
    //==========================================================================
    printf("\n15. Arabic long text with word wrap and emoji (RTL)...\n");
    drawSectionLabel(renderer, baseStyle, "[15] Arabic long text + emoji RTL (with border)", LEFT, y);
    y += 18;

    {
        auto arCollection = fm.createCollection({"ar", "sans", "emoji"});
        richtext::TextStyle arStyle = makeStyle(arCollection, 24.0f, 400, "ar");
        arStyle.bidi = minikin::Bidi::DEFAULT_RTL;
        richtext::RectF arRect(LEFT, y, PARA_W, 200.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, arRect, BORDER_ORANGE, 2);

        // 世界人権宣言 第1条・第2条（アラビア語）
        renderer.drawParagraph(
            utf8ToUtf16(
                "\u064A\u0648\u0644\u062F \u062C\u0645\u064A\u0639 \u0627\u0644\u0646\u0627\u0633 "
                "\u0623\u062D\u0631\u0627\u0631\u064B\u0627 \u0645\u062A\u0633\u0627\u0648\u064A\u0646 "
                "\u0641\u064A \u0627\u0644\u0643\u0631\u0627\u0645\u0629 \u0648\u0627\u0644\u062D\u0642\u0648\u0642. \U0001F30D "
                "\u0648\u0642\u062F \u0648\u0647\u0628\u0648\u0627 \u0639\u0642\u0644\u064B\u0627 "
                "\u0648\u0636\u0645\u064A\u0631\u064B\u0627 \u0648\u0639\u0644\u064A\u0647\u0645 "
                "\u0623\u0646 \u064A\u0639\u0627\u0645\u0644 \u0628\u0639\u0636\u0647\u0645 "
                "\u0628\u0639\u0636\u064B\u0627 \u0628\u0631\u0648\u062D "
                "\u0627\u0644\u0625\u062E\u0627\u0621. \U0001F91D "
                "\u0644\u0643\u0644 \u0625\u0646\u0633\u0627\u0646 \u062D\u0642 "
                "\u0627\u0644\u062A\u0645\u062A\u0639 \u0628\u0643\u0627\u0641\u0629 "
                "\u0627\u0644\u062D\u0642\u0648\u0642 \u0648\u0627\u0644\u062D\u0631\u064A\u0627\u062A "
                "\u0627\u0644\u0648\u0627\u0631\u062F\u0629 \u0641\u064A \u0647\u0630\u0627 "
                "\u0627\u0644\u0625\u0639\u0644\u0627\u0646. \U0001F3F3\uFE0F"),
            arRect,
            richtext::ParagraphLayout::HAlign::Right,
            richtext::ParagraphLayout::VAlign::Top,
            arStyle, blackFill);
    }
    y += 210 + SECTION;

    //==========================================================================
    // 15. フォントサイズ変更レイアウトサンプル
    //==========================================================================
    printf("\n16. Various font sizes layout...\n");
    drawSectionLabel(renderer, baseStyle, "[16] Various font sizes (12, 18, 24, 36, 48, 64)", LEFT, y);
    y += 22;

    {
        const float sizes[] = {12.0f, 18.0f, 24.0f, 36.0f, 48.0f, 64.0f};
        const char* sizeLabels[] = {"12px", "18px", "24px", "36px", "48px", "64px"};

        for (int i = 0; i < 6; ++i) {
            richtext::TextStyle sizedStyle = makeStyle(jaCollection, sizes[i]);
            char label[128];
            snprintf(label, sizeof(label), "%s: こんにちは Hello", sizeLabels[i]);
            renderer.drawText(utf8ToUtf16(label), LEFT, y, sizedStyle, blackFill);
            y += sizes[i] * 1.5f;
        }
    }
    y += SECTION;

    //==========================================================================
    // 15. タグによるフォントサイズ混在パラグラフ
    //==========================================================================
    printf("\n17. Mixed font sizes in paragraph via tags...\n");
    drawSectionLabel(renderer, baseStyle, "[17] Mixed font sizes in paragraph (with border)", LEFT, y);
    y += 18;

    {
        std::map<std::string, richtext::TextStyle>    styles;
        std::map<std::string, richtext::Appearance>   appearances;
        styles["default"]      = makeStyle(jaCollection, 20.0f);
        appearances["default"] = blackFill;

        richtext::RectF mixRect(LEFT, y, PARA_W, 200.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, mixRect, BORDER_ORANGE, 2);
        renderer.drawStyledText(
            utf8ToUtf16(
                "<font size=14>小さなテキスト（14px）</font>"
                "通常テキスト（20px）"
                "<font size=28><b>中サイズ太字（28px）</b></font>"
                "<font size=40><color value=0xFF0055AA>大きなテキスト（40px）</color></font>"
                "<font size=16>また小さく（16px）</font>"
                "そして<font size=52><color value=0xFFCC0000>最大（52px）</color></font>です。"),
            mixRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
    }
    y += 210 + SECTION;

    //==========================================================================
    // 18. CJK 3言語 + 絵文字混在パラグラフ
    //==========================================================================
    printf("\n18. CJK trilingual + emoji mixed paragraph...\n");
    drawSectionLabel(renderer, baseStyle, "[18] CJK trilingual + emoji mixed (with border)", LEFT, y);
    y += 18;

    {
        // 全言語対応コレクション
        std::map<std::string, richtext::TextStyle>    styles;
        std::map<std::string, richtext::Appearance>   appearances;
        styles["default"]      = makeStyle(multiCollection, 22.0f);
        appearances["default"] = blackFill;

        richtext::RectF cjkRect(LEFT, y, PARA_W, 180.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, cjkRect, BORDER_RED, 2);
        renderer.drawStyledText(
            utf8ToUtf16(
                "<color value=0xFFCC0000>日本語：</color>桜の花が咲きました。\U0001F338 "
                "<color value=0xFF0033CC>한국어：</color>벚꽃이 피었습니다. \U0001F338 "
                "<color value=0xFF007700>中文：</color>樱花开了。\U0001F338 "
                "<color value=0xFF666666>English: Cherry blossoms are blooming.</color> \U0001F33A\U0001F33B\U0001F33C "
                "\U0001F1EF\U0001F1F5\U0001F1F0\U0001F1F7\U0001F1E8\U0001F1F3"),
            cjkRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
    }
    y += 190 + SECTION;

    //==========================================================================
    // 19. 欧文 + 絵文字混在サンプル
    //==========================================================================
    printf("\n19. English text with emoji...\n");
    drawSectionLabel(renderer, baseStyle, "[19] English text with emoji (with border)", LEFT, y);
    y += 18;

    {
        auto enCollection = fm.createCollection({"sans", "emoji"});
        std::map<std::string, richtext::TextStyle>    styles;
        std::map<std::string, richtext::Appearance>   appearances;
        styles["default"]      = makeStyle(enCollection, 22.0f, 400, "en_US");
        appearances["default"] = blackFill;

        richtext::RectF enRect(LEFT, y, PARA_W, 100.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, enRect, BORDER_BLUE, 2);
        renderer.drawStyledText(
            utf8ToUtf16(
                "Hello World! \U0001F44B Welcome to the RichText library. \U0001F680\U0001F31F "
                "Emoji are rendered inline: \U0001F60A\U0001F389\U0001F3B5 "
                "and support flags \U0001F1FA\U0001F1F8\U0001F1EC\U0001F1E7\U0001F1EF\U0001F1F5 too!"),
            enRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
    }
    y += 110 + SECTION;

    //==========================================================================
    // 20. ルビ（振り仮名）サンプル
    //==========================================================================
    printf("\n20. Ruby (furigana) text...\n");
    drawSectionLabel(renderer, baseStyle, "[20] Ruby (furigana) text (with border)", LEFT, y);
    y += 18;

    {
        std::map<std::string, richtext::TextStyle>    styles;
        std::map<std::string, richtext::Appearance>   appearances;
        styles["default"]      = makeStyle(jaCollection, 28.0f);
        appearances["default"] = blackFill;

        // ルビ付きテキスト（<br> で強制改行して2行にする）
        auto rubyText = utf8ToUtf16(
            "<ruby text='きょう'>今日</ruby>は<ruby text='てんき'>天気</ruby>がいい。"
            "<ruby text='さくら'>桜</ruby>の<ruby text='はな'>花</ruby>が<ruby text='さ'>咲</ruby>きました。<br>"
            "<ruby text='とうきょうタワー'>東京塔</ruby>に"
            "<ruby text='い'>行</ruby>きたい。");

        // 19a: 行間なし（デフォルト）— 2行目のルビが1行目のテキストに被る
        drawSectionLabel(renderer, baseStyle, "  [19a] lineSpacing=0 (default, ruby overlaps)", LEFT, y);
        y += 16;
        richtext::RectF rubyRect1(LEFT, y, PARA_W, 90.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, rubyRect1, BORDER_BLUE, 2);
        renderer.drawStyledText(rubyText, rubyRect1,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances);
        y += 100 + 5;

        // 19b: ルビ分の行間を追加 — ルビが被らない
        drawSectionLabel(renderer, baseStyle, "  [19b] lineSpacing=16 (room for ruby)", LEFT, y);
        y += 16;
        richtext::RectF rubyRect2(LEFT, y, PARA_W, 110.0f);
        drawRectF(buffer.data(), WIDTH, HEIGHT, rubyRect2, BORDER_GREEN, 2);
        renderer.drawStyledText(rubyText, rubyRect2,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            styles, appearances, 16.0f);
        y += 120;
    }
    y += SECTION;

    //==========================================================================
    // 20. アラインメント比較（左・中央・右）
    //==========================================================================
    printf("\n21. Alignment comparison...\n");
    drawSectionLabel(renderer, baseStyle, "[21] Alignment: Left / Center / Right (with borders)", LEFT, y);
    y += 18;

    {
        richtext::TextStyle alignStyle = makeStyle(jaCollection, 20.0f);
        const float alignH = 80.0f;

        // 左揃え
        richtext::RectF leftRect(LEFT, y, PARA_W, alignH);
        drawRectF(buffer.data(), WIDTH, HEIGHT, leftRect, BORDER_RED);
        renderer.drawParagraph(
            utf8ToUtf16("左揃え（Left）テキストサンプル。行が折り返された場合でも左端に揃います。"),
            leftRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            alignStyle, blackFill);
        y += alignH + 8;

        // 中央揃え
        richtext::RectF centerRect(LEFT, y, PARA_W, alignH);
        drawRectF(buffer.data(), WIDTH, HEIGHT, centerRect, BORDER_BLUE);
        renderer.drawParagraph(
            utf8ToUtf16("中央揃え（Center）テキストサンプル。各行が中央に配置されます。"),
            centerRect,
            richtext::ParagraphLayout::HAlign::Center,
            richtext::ParagraphLayout::VAlign::Top,
            alignStyle, blueFill);
        y += alignH + 8;

        // 右揃え
        richtext::RectF rightRect(LEFT, y, PARA_W, alignH);
        drawRectF(buffer.data(), WIDTH, HEIGHT, rightRect, BORDER_GREEN);
        renderer.drawParagraph(
            utf8ToUtf16("右揃え（Right）テキストサンプル。各行が右端に配置されます。"),
            rightRect,
            richtext::ParagraphLayout::HAlign::Right,
            richtext::ParagraphLayout::VAlign::Top,
            alignStyle, greenFill);
    }
    y += 90 + SECTION;

    //==========================================================================
    // 21. 垂直アラインメント比較（上・中央・下）
    //==========================================================================
    printf("\n22. Vertical alignment comparison...\n");
    drawSectionLabel(renderer, baseStyle, "[22] VAlign: Top / Middle / Bottom (Center, with borders)", LEFT, y);
    y += 18;

    {
        richtext::TextStyle valignStyle = makeStyle(jaCollection, 18.0f);
        const float boxW = (PARA_W - 20) / 3.0f;
        const float boxH = 120.0f;

        auto valignText = utf8ToUtf16("上下アライン確認用テキスト。");

        // Top
        richtext::RectF topRect(LEFT, y, boxW, boxH);
        drawRectF(buffer.data(), WIDTH, HEIGHT, topRect, BORDER_RED, 2);
        renderer.drawParagraph(valignText, topRect,
            richtext::ParagraphLayout::HAlign::Center,
            richtext::ParagraphLayout::VAlign::Top,
            valignStyle, redFill);

        // Middle
        richtext::RectF midRect(LEFT + boxW + 10, y, boxW, boxH);
        drawRectF(buffer.data(), WIDTH, HEIGHT, midRect, BORDER_BLUE, 2);
        renderer.drawParagraph(valignText, midRect,
            richtext::ParagraphLayout::HAlign::Center,
            richtext::ParagraphLayout::VAlign::Middle,
            valignStyle, blueFill);

        // Bottom
        richtext::RectF botRect(LEFT + (boxW + 10) * 2, y, boxW, boxH);
        drawRectF(buffer.data(), WIDTH, HEIGHT, botRect, BORDER_GREEN, 2);
        renderer.drawParagraph(valignText, botRect,
            richtext::ParagraphLayout::HAlign::Center,
            richtext::ParagraphLayout::VAlign::Bottom,
            valignStyle, greenFill);
    }
    y += 130 + SECTION;

    //--------------------------------------------------------------------------
    // 20. 描画同期・保存
    //--------------------------------------------------------------------------
    printf("\n23. Syncing and saving...\n");
    renderer.sync();

    // 枠線はバッファに直接描画済みなので sync 後に saveBMP
    if (saveBMP("output.bmp", buffer.data(), WIDTH, HEIGHT)) {
        printf("\nSuccess! Output saved to output.bmp\n");
    } else {
        fprintf(stderr, "\nFailed to save output.bmp\n");
        return 1;
    }

    printf("\n=== Done ===\n");
    return 0;
}
