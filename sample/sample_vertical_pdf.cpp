/**
 * sample_vertical_pdf.cpp
 *
 * PDF backend（Phase 5）の動作確認サンプル
 *
 * 縦組みの流し込み結果を **1 回だけ組んで**、同じ結果から
 *   - ラスタライズ（TextRenderer → output_vertical_pdf.bmp）
 *   - PDF（PdfWriter → output_vertical.pdf）
 * の両方を出す。組版が二重実装になっていないことの確認が目的。
 *
 * ※ リポジトリルートから実行すること（フォントを ./data/ から読む）
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

#include "richtext/FontManager.hpp"
#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/TextRenderer.hpp"
#include "richtext/vertical/BlockLayout.hpp"
#include "richtext/pdf/PdfWriter.hpp"

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
            bgrPixels[static_cast<size_t>(height - 1 - y) * width + x] =
                    pixels[static_cast<size_t>(y) * width + x];
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

    printf("=== richtext PDF Backend Sample ===\n\n");

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

    // 和文は CFF（.otf → CIDFontType0）、欧文は glyf（.ttf → CIDFontType2）。
    // PDF 側で両方の埋め込み経路を通す。
    struct FontEntry { const char* file; const char* name; };
    const FontEntry fonts[] = {
        {"NotoSerifJP-Regular.otf", "serif-ja"},
        {"NotoSans-Regular.ttf",    "sans"},
    };
    for (const auto& f : fonts) {
        if (!fm.registerFont(f.file, f.name)) {
            fprintf(stderr, "   [%s] FAILED: %s\n", f.name, f.file);
        }
    }
    fm.registerLocale("ja_JP-u-lb-strict");

    richtext::TextStyle style;
    style.fontCollection = fm.createCollection({"serif-ja", "sans"});
    style.fontSize = 18.0f;
    style.localeId = fm.getLocaleId("ja_JP-u-lb-strict");

    richtext::Appearance black;
    black.addFill(0xFF111111);

    //--------------------------------------------------------------------------
    // 2. 組版（1 回だけ）
    //--------------------------------------------------------------------------
    printf("2. Laying out once (shared by raster and PDF)...\n");

    using namespace richtext::vertical;

    const float pageW = 420.0f;
    const float pageH = 595.0f;   // A5 横相当

    std::vector<BlockContainer> containers;
    for (int page = 0; page < 3; ++page) {
        BlockContainer c;
        c.x = 40.0f;
        c.y = 40.0f;
        c.width = pageW - 80.0f;
        c.height = pageH - 80.0f;
        c.columnCount = 1;
        containers.push_back(c);
    }

    VerticalLayoutOptions opts;
    opts.lineGap = 12.0f;
    opts.spacing.hangingPunctuation = true;

    const char* bodies[] = {
        u8"　組版結果を一度だけ作り、そこから画面と PDF の両方を出す。"
        u8"PDF にはグリフ ID を直接書くので、ビューア側で組み直されることがない。"
        u8"これが同じ結果になることの条件になる。",

        u8"　縦組みでも Identity-V は使わない。縦のメトリクスは組版層で"
        u8"計算し終えているので、PDF 側にもう一度計算させる必要が無いからだ。"
        u8"横倒しが要る欧文は Tm に回転行列を入れて寝かせる。",

        u8"　和文には CFF のフォント、欧文 ABC 123 には glyf のフォントを"
        u8"当ててある。前者は CIDFontType0 として FontFile3 に、"
        u8"後者は CIDFontType2 として FontFile2 に埋め込まれる。",

        u8"　サブセット化はしていないので、埋め込まれるのはフォントの全体である。"
        u8"台本のような用途では許容できる大きさに収まる。"
        u8"必要になったら、使用グリフだけを切り出す処理を足せばよい。",

        u8"　ToUnicode の CMap も書いているので、出来上がった PDF の上で"
        u8"文字を選んだり検索したりできる。グリフと文字の対応は、"
        u8"描画のときに渡した原文から取っている。",
    };

    std::vector<BlockLayout::Paragraph> paragraphs;
    for (const char* body : bodies) {
        BlockLayout::Paragraph p;
        p.text = utf8ToUtf16(body);
        p.style = style;
        p.options = opts;
        paragraphs.push_back(std::move(p));
    }

    BlockLayout block;
    block.setContainers(containers);
    block.layout(paragraphs);

    printf("   placed lines=%zu  used pages=%zu  overflow=%s\n",
           block.getPlacedLineCount(), block.getUsedContainerCount(),
           block.getOverflow().has ? "yes" : "no");

    const size_t pageCount = std::max<size_t>(1, block.getUsedContainerCount());

    //--------------------------------------------------------------------------
    // 3. ラスタライズ（ページを横に並べて 1 枚の BMP へ）
    //--------------------------------------------------------------------------
    printf("3. Rasterizing...\n");

    const int gap = 20;
    const int WIDTH = static_cast<int>(pageCount) * (static_cast<int>(pageW) + gap) + gap;
    const int HEIGHT = static_cast<int>(pageH) + gap * 2;
    std::vector<uint32_t> buffer(static_cast<size_t>(WIDTH) * HEIGHT, 0xFFEEEEEE);

    richtext::TextRenderer renderer;
    renderer.setCanvas(buffer.data(), WIDTH, HEIGHT,
                       WIDTH * static_cast<int>(sizeof(uint32_t)));

    for (size_t i = 0; i < pageCount; ++i) {
        const float ox = static_cast<float>(gap + static_cast<int>(i) * (static_cast<int>(pageW) + gap));
        const float oy = static_cast<float>(gap);
        renderer.drawRect(ox, oy, pageW, pageH, 0xFFFFFFFF, 0xFFBBBBBB, 1.0f);
        renderer.drawBlockContainer(block, i, ox, oy, black);
    }
    saveBMP("output_vertical_pdf.bmp", buffer.data(), WIDTH, HEIGHT);

    //--------------------------------------------------------------------------
    // 4. PDF（同じ block からもう一度描くだけ）
    //--------------------------------------------------------------------------
    printf("4. Writing PDF...\n");

    richtext::pdf::PdfWriter pdf;
    pdf.setTitle("richtext vertical sample");
    pdf.setCreator("richtext sample_vertical_pdf");

    for (size_t i = 0; i < pageCount; ++i) {
        pdf.beginPage(pageW, pageH);
        pdf.drawBlockContainer(block, i, 0.0f, 0.0f, black);
        pdf.endPage();
    }

    for (const auto& w : pdf.getWarnings()) {
        fprintf(stderr, "   warning: %s\n", w.c_str());
    }

    const std::string bytes = pdf.build();
    printf("   pages=%zu  size=%zu bytes\n", pdf.getPageCount(), bytes.size());

    if (!pdf.save("output_vertical.pdf")) {
        fprintf(stderr, "   FAILED to write output_vertical.pdf\n");
        return 1;
    }
    printf("Saved: output_vertical.pdf\n");

    printf("\nDone.\n");
    return 0;
}
