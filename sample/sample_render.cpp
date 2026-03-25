/**
 * sample_render.cpp
 *
 * richtext ライブラリのサンプル実行プログラム
 * ビットマップに描画して BMP ファイルとして出力
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

// セクションラベルを描画するヘルパー（デバッグ用、ASCII のみ）
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
    const int HEIGHT = 1400;
    std::vector<uint32_t> buffer(WIDTH * HEIGHT, 0xFFFFFFFF);  // 白背景

    //--------------------------------------------------------------------------
    // 1. フォントの登録
    //--------------------------------------------------------------------------
    printf("1. Registering fonts...\n");

    auto& fm = richtext::FontManager::instance();

    // 日本語フォント
    const char* jaFontPaths[] = {
        "C:/Windows/Fonts/msgothic.ttc",
        "C:/Windows/Fonts/meiryo.ttc",
        "C:/Windows/Fonts/YuGothM.ttc",
    };
    bool jaRegistered = false;
    for (const char* p : jaFontPaths) {
        if (fm.registerFont(p, "ja")) {
            printf("   [ja] %s\n", p); jaRegistered = true; break;
        }
    }
    if (!jaRegistered) {
        fprintf(stderr, "ERROR: No Japanese font found.\n"); return 1;
    }

    // アラビア語 / 多言語対応フォント（RTL テスト用）
    const char* arFontPaths[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/times.ttf",
    };
    bool arRegistered = false;
    for (const char* p : arFontPaths) {
        if (fm.registerFont(p, "ar")) {
            printf("   [ar] %s\n", p); arRegistered = true; break;
        }
    }
    if (!arRegistered) {
        printf("   [ar] No Arabic font found; bidi tests may show placeholders.\n");
    }

    fm.registerLocale("ja_JP-u-lb-strict");
    fm.registerLocale("ar");

    //--------------------------------------------------------------------------
    // 2. TextRenderer 初期化
    //--------------------------------------------------------------------------
    printf("\n2. Initializing TextRenderer...\n");
    richtext::TextRenderer renderer;
    renderer.setCanvas(buffer.data(), WIDTH, HEIGHT, WIDTH * sizeof(uint32_t));

    //--------------------------------------------------------------------------
    // 3. スタイル・外観の設定
    //--------------------------------------------------------------------------
    printf("\n3. Setting up styles...\n");

    // 日本語コレクション（ja のみ）
    auto jaCollection = fm.createCollection({"ja"});

    // 多言語コレクション（ar → ja フォールバック）
    auto multiCollection = arRegistered
        ? fm.createCollection({"ar", "ja"})
        : fm.createCollection({"ja"});

    auto makeStyle = [&](std::shared_ptr<minikin::FontCollection> col,
                         float size, uint16_t weight = 400) {
        richtext::TextStyle s;
        s.fontCollection = col;
        s.fontSize       = size;
        s.fontWeight     = weight;
        s.localeId       = fm.getLocaleId("ja_JP-u-lb-strict");
        return s;
    };

    richtext::TextStyle baseStyle = makeStyle(jaCollection, 28.0f);

    // 外観
    richtext::Appearance whiteOutline;
    whiteOutline.addStroke(0xFF000000, 2.5f);
    whiteOutline.addFill(0xFFFFFFFF);

    richtext::Appearance blackFill;
    blackFill.addFill(0xFF111111);

    richtext::Appearance redFill;
    redFill.addFill(0xFFCC0000);

    richtext::Appearance blueFill;
    blueFill.addFill(0xFF0033CC);

    richtext::Appearance greenFill;
    greenFill.addFill(0xFF007700);

    richtext::Appearance shadowBlue;
    shadowBlue.addFill(0x60000000, 3.0f, 3.0f);
    shadowBlue.addFill(0xFF0055AA);

    float y = 50.0f;
    const float LINE = 48.0f;
    const float SECTION = 30.0f;
    const float LEFT = 40.0f;

    //--------------------------------------------------------------------------
    // 4. 基本テキスト
    //--------------------------------------------------------------------------
    printf("\n4. Basic text...\n");
    drawSectionLabel(renderer, baseStyle, "[4] Basic LTR text", LEFT, y);
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
    // 6. パラグラフ（行分割・左揃え）
    //--------------------------------------------------------------------------
    printf("\n6. Paragraph (left-aligned)...\n");
    drawSectionLabel(renderer, baseStyle, "[6] Paragraph - Left", LEFT, y);
    y += 18;

    richtext::TextStyle paraStyle = makeStyle(jaCollection, 22.0f);
    richtext::RectF paraRect(LEFT, y, WIDTH - LEFT * 2, 120.0f);
    renderer.drawParagraph(
        utf8ToUtf16("これは複数行のパラグラフです。minikin の行分割アルゴリズムによって、"
                    "指定した幅に収まるよう自動的に改行されます。日本語の禁則処理も適用されます。"),
        paraRect,
        richtext::ParagraphLayout::HAlign::Left,
        richtext::ParagraphLayout::VAlign::Top,
        paraStyle, blackFill);
    y += 130 + SECTION;

    //--------------------------------------------------------------------------
    // 7. タグ付きテキスト（drawStyledText）
    //--------------------------------------------------------------------------
    printf("\n7. Tagged text (drawStyledText)...\n");
    drawSectionLabel(renderer, baseStyle, "[7] Tagged text (drawStyledText)", LEFT, y);
    y += 18;

    std::map<std::string, richtext::TextStyle>    styles;
    std::map<std::string, richtext::Appearance>   appearances;
    styles["default"]      = makeStyle(jaCollection, 28.0f);
    appearances["default"] = blackFill;

    richtext::RectF tagRect(LEFT, y, WIDTH - LEFT * 2, 100.0f);
    renderer.drawStyledText(
        utf8ToUtf16("<b>太字</b>と<i>斜体</i>と"
                    "<color value=0xFFCC0000>赤色</color>と"
                    "<font size=36>大きな</font>テキスト"),
        tagRect,
        richtext::ParagraphLayout::HAlign::Left,
        richtext::ParagraphLayout::VAlign::Top,
        styles, appearances);
    y += 50 + SECTION;

    //--------------------------------------------------------------------------
    // 8. 上付き・下付き（sup / sub）
    //--------------------------------------------------------------------------
    printf("\n8. Superscript / Subscript...\n");
    drawSectionLabel(renderer, baseStyle, "[8] Superscript / Subscript", LEFT, y);
    y += 18;

    styles["default"]      = makeStyle(jaCollection, 28.0f);
    appearances["default"] = blackFill;

    richtext::RectF supRect(LEFT, y, WIDTH - LEFT * 2, 60.0f);
    renderer.drawStyledText(
        utf8ToUtf16("H<sub>2</sub>O  E=mc<sup>2</sup>  x<sup>n+1</sup> = x<sup>n</sup> + 1"),
        supRect,
        richtext::ParagraphLayout::HAlign::Left,
        richtext::ParagraphLayout::VAlign::Top,
        styles, appearances);
    y += 60 + SECTION;

    //--------------------------------------------------------------------------
    // 9. 縁取り・影タグ
    //--------------------------------------------------------------------------
    printf("\n9. Outline / Shadow tags...\n");
    drawSectionLabel(renderer, baseStyle, "[9] Outline / Shadow via tags", LEFT, y);
    y += 18;

    styles["default"]      = makeStyle(jaCollection, 30.0f);
    appearances["default"] = blackFill;

    richtext::RectF fxRect(LEFT, y, WIDTH - LEFT * 2, 60.0f);
    renderer.drawStyledText(
        utf8ToUtf16("<outline color=0xFF000000 width=3><color value=0xFFFFDD00>縁取りテキスト</color></outline>"
                    "　"
                    "<shadow color=0x88000000 x=3 y=3>影テキスト</shadow>"),
        fxRect,
        richtext::ParagraphLayout::HAlign::Left,
        richtext::ParagraphLayout::VAlign::Top,
        styles, appearances);
    y += 60 + SECTION;

    //--------------------------------------------------------------------------
    // 10. 双方向テキスト（Bidi）
    //--------------------------------------------------------------------------
    printf("\n10. Bidirectional text (Bidi)...\n");

    // 10a. DEFAULT_LTR（自動判定）で混在テキスト
    drawSectionLabel(renderer, baseStyle, "[10a] Mixed LTR+RTL (DEFAULT_LTR, auto-detect)", LEFT, y);
    y += 18;
    {
        richtext::TextStyle mixStyle = makeStyle(multiCollection, 28.0f);
        mixStyle.bidi = minikin::Bidi::DEFAULT_LTR;

        // アラビア語「مرحبا」（マルハバ = Hello）を含む混在テキスト
        std::u16string mixedText = utf8ToUtf16("Hello \u0645\u0631\u062D\u0628\u0627 World");
        renderer.drawText(mixedText, LEFT, y, mixStyle, blackFill);
        printf("   DEFAULT_LTR: \"Hello مرحبا World\"\n");
    }
    y += LINE + 8;

    // 10b. DEFAULT_RTL（自動判定・RTL優先）で同テキスト
    drawSectionLabel(renderer, baseStyle, "[10b] Same text with DEFAULT_RTL", LEFT, y);
    y += 18;
    {
        richtext::TextStyle rtlStyle = makeStyle(multiCollection, 28.0f);
        rtlStyle.bidi = minikin::Bidi::DEFAULT_RTL;

        std::u16string mixedText = utf8ToUtf16("Hello \u0645\u0631\u062D\u0628\u0627 World");
        renderer.drawText(mixedText, LEFT, y, rtlStyle, blackFill);
        printf("   DEFAULT_RTL: \"Hello مرحبا World\"\n");
    }
    y += LINE + 8;

    // 10c. FORCE_RTL（強制 RTL）でアラビア語のみ
    drawSectionLabel(renderer, baseStyle, "[10c] Arabic only (FORCE_RTL)", LEFT, y);
    y += 18;
    {
        richtext::TextStyle arStyle = makeStyle(multiCollection, 28.0f);
        arStyle.bidi = minikin::Bidi::FORCE_RTL;

        // アラビア語「مرحبا بالعالم」（Hello World）
        std::u16string arText = utf8ToUtf16("\u0645\u0631\u062D\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645");
        renderer.drawText(arText, LEFT, y, arStyle, blackFill);
        printf("   FORCE_RTL:   \"مرحبا بالعالم\"\n");
    }
    y += LINE + 8;

    // 10d. FORCE_LTR（強制 LTR）で同じアラビア語テキスト（比較用）
    drawSectionLabel(renderer, baseStyle, "[10d] Arabic with FORCE_LTR (for comparison)", LEFT, y);
    y += 18;
    {
        richtext::TextStyle ltrStyle = makeStyle(multiCollection, 28.0f);
        ltrStyle.bidi = minikin::Bidi::FORCE_LTR;

        std::u16string arText = utf8ToUtf16("\u0645\u0631\u062D\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645");
        renderer.drawText(arText, LEFT, y, ltrStyle, blackFill);
        printf("   FORCE_LTR:   \"مرحبا بالعالم\" (rendered L->R)\n");
    }
    y += LINE + 8;

    // 10e. RTL パラグラフ（複数行、右揃え）
    drawSectionLabel(renderer, baseStyle, "[10e] RTL Paragraph (right-aligned)", LEFT, y);
    y += 18;
    {
        richtext::TextStyle rtlParaStyle = makeStyle(multiCollection, 24.0f);
        rtlParaStyle.bidi = minikin::Bidi::DEFAULT_RTL;

        // ヘブライ語「שלום עולם」+ アラビア語の段落（折り返し確認）
        std::u16string rtlText = utf8ToUtf16(
            "\u0645\u0631\u062D\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645\u060C "
            "\u0647\u0630\u0627 \u0646\u0635 \u0639\u0631\u0628\u064A "
            "\u0644\u0627\u062E\u062A\u0628\u0627\u0631 \u0627\u0644\u0643\u062A\u0627\u0628\u0629 "
            "\u0645\u0646 \u0627\u0644\u064A\u0645\u064A\u0646 \u0625\u0644\u0649 \u0627\u0644\u064A\u0633\u0627\u0631.");

        richtext::RectF rtlParaRect(LEFT, y, WIDTH - LEFT * 2, 110.0f);

        // ParagraphLayout を直接使って RTL スタイルラン付きで描画
        richtext::ParagraphLayout para;
        richtext::ParagraphLayout::StyleRun run;
        run.start = 0;
        run.end   = rtlText.size();
        run.style = rtlParaStyle;
        para.layout(rtlText, rtlParaRect.width, {run});

        richtext::Appearance darkGray;
        darkGray.addFill(0xFF222222);

        for (size_t li = 0; li < para.getLineCount(); ++li) {
            auto pos = para.getLinePosition(li,
                rtlParaRect.x, rtlParaRect.y,
                rtlParaRect.width, rtlParaRect.height,
                richtext::ParagraphLayout::HAlign::Right,
                richtext::ParagraphLayout::VAlign::Top);
            richtext::TextLayout lineLayout = para.getLineLayout(li, rtlParaStyle);
            renderer.drawLayout(lineLayout, pos.x, pos.y, darkGray);
        }
        printf("   RTL paragraph drawn.\n");
    }
    y += 120 + SECTION;

    // 10f. ヘブライ語テキスト（Hebrew shalom）
    drawSectionLabel(renderer, baseStyle, "[10f] Hebrew text (DEFAULT_RTL)", LEFT, y);
    y += 18;
    {
        richtext::TextStyle heStyle = makeStyle(multiCollection, 28.0f);
        heStyle.bidi = minikin::Bidi::DEFAULT_RTL;

        // שלום עולם = Shalom Olam (Hello World)
        std::u16string heText = utf8ToUtf16("\u05E9\u05DC\u05D5\u05DD \u05E2\u05D5\u05DC\u05DD");
        renderer.drawText(heText, LEFT, y, heStyle, blackFill);
        printf("   Hebrew: \"שלום עולם\" (Shalom Olam)\n");
    }
    y += LINE + SECTION;

    //--------------------------------------------------------------------------
    // 11. Bidi × タグ付きテキスト
    //--------------------------------------------------------------------------
    printf("\n11. Bidi + tagged text...\n");
    drawSectionLabel(renderer, baseStyle, "[11] Bidi in tagged text", LEFT, y);
    y += 18;

    {
        // タグ付きテキスト内でフォントを切り替えてアラビア語と日本語を混在
        richtext::TextStyle tagBaseStyle = makeStyle(multiCollection, 26.0f);
        tagBaseStyle.bidi = minikin::Bidi::DEFAULT_LTR;

        std::map<std::string, richtext::TextStyle>  bidiStyles;
        std::map<std::string, richtext::Appearance> bidiApps;
        bidiStyles["default"]      = tagBaseStyle;
        bidiApps["default"]        = blackFill;

        richtext::RectF bidiTagRect(LEFT, y, WIDTH - LEFT * 2, 60.0f);
        renderer.drawStyledText(
            utf8ToUtf16("LTR: <color value=0xFF0033CC>Hello</color>"
                        " RTL: <color value=0xFFCC0000>\u0645\u0631\u062D\u0628\u0627</color>"
                        " \u65E5\u672C\u8A9E"),  // 日本語
            bidiTagRect,
            richtext::ParagraphLayout::HAlign::Left,
            richtext::ParagraphLayout::VAlign::Top,
            bidiStyles, bidiApps);
    }
    y += 60 + SECTION;

    //--------------------------------------------------------------------------
    // 12. 描画同期・保存
    //--------------------------------------------------------------------------
    printf("\n12. Syncing and saving...\n");
    renderer.sync();

    if (saveBMP("output.bmp", buffer.data(), WIDTH, HEIGHT)) {
        printf("\nSuccess! Output saved to output.bmp\n");
    } else {
        fprintf(stderr, "\nFailed to save output.bmp\n");
        return 1;
    }

    printf("\n=== Done ===\n");
    return 0;
}
