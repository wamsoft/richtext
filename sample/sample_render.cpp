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

// Windows Header for path resolution
#ifdef _WIN32
#include <windows.h>
#endif

//------------------------------------------------------------------------------
// BMP 出力ヘルパー
//------------------------------------------------------------------------------

#pragma pack(push, 1)
struct BMPFileHeader {
    uint16_t type = 0x4D42;  // "BM"
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
    infoHeader.height = height;  // 正の値で bottom-up
    infoHeader.imageSize = width * height * 4;
    fileHeader.size = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + infoHeader.imageSize;
    
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));
    
    // BMP は bottom-up なので上下反転して書き込み
    // また ARGB → BGRA 変換
    std::vector<uint32_t> bgrPixels(width * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int srcIdx = y * width + x;
            int dstIdx = (height - 1 - y) * width + x;
            uint32_t argb = pixels[srcIdx];
            // ARGB → BGRA (BMP format)
            uint8_t a = (argb >> 24) & 0xFF;
            uint8_t r = (argb >> 16) & 0xFF;
            uint8_t g = (argb >> 8) & 0xFF;
            uint8_t b = argb & 0xFF;
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
        
        if ((c & 0x80) == 0) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = (c & 0x1F) << 6;
            if (i + 1 < utf8.size()) cp |= (utf8[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = (c & 0x0F) << 12;
            if (i + 1 < utf8.size()) cp |= (utf8[i + 1] & 0x3F) << 6;
            if (i + 2 < utf8.size()) cp |= (utf8[i + 2] & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = (c & 0x07) << 18;
            if (i + 1 < utf8.size()) cp |= (utf8[i + 1] & 0x3F) << 12;
            if (i + 2 < utf8.size()) cp |= (utf8[i + 2] & 0x3F) << 6;
            if (i + 3 < utf8.size()) cp |= (utf8[i + 3] & 0x3F);
            i += 4;
        } else {
            i += 1;
            continue;
        }
        
        if (cp <= 0xFFFF) {
            result += static_cast<char16_t>(cp);
        } else if (cp <= 0x10FFFF) {
            // サロゲートペア
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

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    printf("=== richtext Sample Renderer ===\n\n");
    
    // キャンバスサイズ
    const int WIDTH = 800;
    const int HEIGHT = 600;
    
    // ピクセルバッファ (ARGB)
    std::vector<uint32_t> buffer(WIDTH * HEIGHT, 0xFFFFFFFF);  // 白背景
    
    //--------------------------------------------------------------------------
    // 1. フォントの登録
    //--------------------------------------------------------------------------
    printf("1. Registering fonts...\n");
    
    auto& fontManager = richtext::FontManager::instance();
    
    // システムフォントのパス（Windows）
    const char* fontPaths[] = {
        "C:/Windows/Fonts/msgothic.ttc",    // MS Gothic
        "C:/Windows/Fonts/meiryo.ttc",      // Meiryo
        "C:/Windows/Fonts/YuGothM.ttc",     // Yu Gothic
        "C:/Windows/Fonts/arial.ttf",       // Arial
        "C:/Windows/Fonts/NotoSansCJKjp-Regular.otf",  // Noto Sans CJK
    };
    
    bool fontRegistered = false;
    for (const char* path : fontPaths) {
        if (fontManager.registerFont(path, "default")) {
            printf("   Registered: %s\n", path);
            fontRegistered = true;
            break;
        }
    }
    
    if (!fontRegistered) {
        fprintf(stderr, "ERROR: No font could be registered!\n");
        fprintf(stderr, "Please ensure a Japanese font is available.\n");
        return 1;
    }
    
    // ロケール登録
    fontManager.registerLocale("ja_JP-u-lb-strict");
    
    //--------------------------------------------------------------------------
    // 2. TextRenderer の初期化
    //--------------------------------------------------------------------------
    printf("\n2. Initializing TextRenderer...\n");
    
    richtext::TextRenderer renderer;
    renderer.setCanvas(buffer.data(), WIDTH, HEIGHT, WIDTH * sizeof(uint32_t));
    
    //--------------------------------------------------------------------------
    // 3. スタイルと外観の設定
    //--------------------------------------------------------------------------
    printf("\n3. Setting up styles...\n");
    
    // フォントコレクション作成
    auto fontCollection = fontManager.createCollection({"default"});
    
    // 基本スタイル
    richtext::TextStyle baseStyle;
    baseStyle.fontCollection = fontCollection;
    baseStyle.fontSize = 32.0f;
    baseStyle.fontWeight = 400;
    baseStyle.localeId = fontManager.getLocaleId("ja_JP-u-lb-strict");
    
    // 基本外観（白塗り＋黒縁取り）
    richtext::Appearance baseAppearance;
    baseAppearance.addStroke(0xFF000000, 3.0f);  // 黒縁取り
    baseAppearance.addFill(0xFFFFFFFF);          // 白塗り
    
    // 赤色外観
    richtext::Appearance redAppearance;
    redAppearance.addFill(0xFFFF0000);
    
    // 青色外観（影付き）
    richtext::Appearance blueWithShadow;
    blueWithShadow.addFill(0x80000000, 3.0f, 3.0f);  // 影
    blueWithShadow.addFill(0xFF0000FF);               // 青塗り
    
    //--------------------------------------------------------------------------
    // 4. 単純なテキスト描画
    //--------------------------------------------------------------------------
    printf("\n4. Drawing simple text...\n");
    
    std::u16string text1 = utf8ToUtf16("Hello, richtext!");
    renderer.drawText(text1, 50.0f, 80.0f, baseStyle, baseAppearance);
    
    //--------------------------------------------------------------------------
    // 5. 日本語テキスト描画
    //--------------------------------------------------------------------------
    printf("\n5. Drawing Japanese text...\n");
    
    std::u16string text2 = utf8ToUtf16("こんにちは、リッチテキスト！");
    renderer.drawText(text2, 50.0f, 140.0f, baseStyle, baseAppearance);
    
    //--------------------------------------------------------------------------
    // 6. スタイル変更（サイズ・色）
    //--------------------------------------------------------------------------
    printf("\n6. Drawing styled text...\n");
    
    richtext::TextStyle largeStyle = baseStyle;
    largeStyle.fontSize = 48.0f;
    largeStyle.fontWeight = 700;  // Bold
    
    std::u16string text3 = utf8ToUtf16("大きな太字");
    renderer.drawText(text3, 50.0f, 220.0f, largeStyle, redAppearance);
    
    //--------------------------------------------------------------------------
    // 7. 影付きテキスト
    //--------------------------------------------------------------------------
    printf("\n7. Drawing text with shadow...\n");
    
    std::u16string text4 = utf8ToUtf16("Shadow Effect");
    renderer.drawText(text4, 50.0f, 300.0f, baseStyle, blueWithShadow);
    
    //--------------------------------------------------------------------------
    // 8. パラグラフ（複数行）描画
    //--------------------------------------------------------------------------
    printf("\n8. Drawing paragraph...\n");
    
    richtext::TextStyle paraStyle = baseStyle;
    paraStyle.fontSize = 24.0f;
    
    richtext::Appearance paraAppearance;
    paraAppearance.addFill(0xFF333333);
    
    std::u16string paraText = utf8ToUtf16(
        "これは複数行のパラグラフテストです。"
        "minikinによる行分割が正しく動作すれば、"
        "指定した幅に収まるように自動的に改行されます。"
        "日本語の禁則処理も適用されます。"
    );
    
    richtext::RectF paraRect(50.0f, 360.0f, 700.0f, 200.0f);
    renderer.drawParagraph(paraText, paraRect,
                          richtext::ParagraphLayout::HAlign::Left,
                          richtext::ParagraphLayout::VAlign::Top,
                          paraStyle, paraAppearance);
    
    //--------------------------------------------------------------------------
    // 9. タグ付きテキスト（TagParser使用）
    //--------------------------------------------------------------------------
    printf("\n9. Drawing tagged text...\n");
    
    richtext::TagParser parser;
    
    std::string taggedTextUtf8 = 
        "<b>太字</b>と<i>斜体</i>と"
        "<color value=0xFFFF0000>赤色</color>の"
        "<font size=40>大きな</font>テキスト";
    
    auto parseResult = parser.parse(taggedTextUtf8, baseStyle, baseAppearance);
    
    printf("   Plain text: ");
    // UTF-16 を表示（簡易的にASCII部分のみ）
    for (char16_t c : parseResult.plainText) {
        if (c < 128) putchar(static_cast<char>(c));
        else putchar('?');
    }
    printf("\n");
    printf("   Spans: %zu\n", parseResult.spans.size());
    
    // TODO: パース結果を使った描画
    // 現在は StyleRun として描画するためのインタフェースが必要
    
    //--------------------------------------------------------------------------
    // 10. 描画の同期と出力
    //--------------------------------------------------------------------------
    printf("\n10. Syncing and saving...\n");
    
    renderer.sync();
    
    // BMP として保存
    if (saveBMP("output.bmp", buffer.data(), WIDTH, HEIGHT)) {
        printf("\nSuccess! Output saved to output.bmp\n");
    } else {
        fprintf(stderr, "\nFailed to save output.bmp\n");
        return 1;
    }
    
    printf("\n=== Done ===\n");
    return 0;
}
