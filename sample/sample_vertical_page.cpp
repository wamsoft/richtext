/**
 * sample_vertical_page.cpp
 *
 * 縦組みのブロック組版（Phase 4）の動作確認サンプル
 *
 * 連結された複数コンテナ（＝ページ）へ段落を流し込み、段組とページ分割、
 * 段／ページをまたぐ段落の継続を確認する。
 * data/ 以下の Noto フォントを使用し、output_vertical_page.bmp を出力する。
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

    printf("=== richtext Vertical Block Layout Sample ===\n\n");

    const int WIDTH  = 1560;
    const int HEIGHT = 800;
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

    richtext::TextStyle style;
    style.fontCollection = fm.createCollection({"serif-ja", "serif"});
    style.fontSize = 20.0f;
    style.localeId = fm.getLocaleId("ja_JP-u-lb-strict");

    richtext::TextStyle labelStyle = style;
    labelStyle.fontSize = 13.0f;

    richtext::Appearance black;
    black.addFill(0xFF111111);
    richtext::Appearance gray;
    gray.addFill(0xFF888888);

    //--------------------------------------------------------------------------
    // 3. コンテナ（3 ページ × 2 段）
    //--------------------------------------------------------------------------
    printf("2. Setting up containers (3 pages x 2 columns)...\n");

    using namespace richtext::vertical;

    std::vector<BlockContainer> containers;
    for (int page = 0; page < 3; ++page) {
        BlockContainer c;
        c.x = 60.0f + static_cast<float>(page) * 500.0f;
        c.y = 60.0f;
        c.width = 420.0f;
        c.height = 660.0f;
        // 3 ページ目だけ段組を変える（段の高さ＝行長が変わるので、
        // またいだ段落はそこで組み直される）
        c.columnCount = (page == 2) ? 1 : 2;
        c.columnGap = 40.0f;
        containers.push_back(c);
    }

    //--------------------------------------------------------------------------
    // 4. 段落
    //--------------------------------------------------------------------------
    VerticalLayoutOptions opts;
    opts.lineGap = 14.0f;
    opts.spacing.hangingPunctuation = true;

    const char* bodies[] = {
        u8"　どこかで、鐘が鳴っている。低く、ゆっくりと、遠くの空を渡ってくる音だった。"
        u8"少年は縁側に腰を下ろしたまま、庭の隅に立つ古い木を見上げていた。"
        u8"葉のほとんどは落ちて、細い枝が乾いた空へ突き出している。",

        u8"　「もう行くのか」と、背中の方から声がした。振り返らなくても誰だか分かる。"
        u8"祖父はいつも同じ調子で、同じ位置から声をかけてくる。"
        u8"少年は返事をしなかった。代わりに、足元の石を軽く蹴った。",

        u8"　列車は正午に出る。駅までは歩いて二十分ほどで、荷物は鞄ひとつきりだった。"
        u8"母が持たせようとした包みは、結局置いてきてしまった。"
        u8"重いものを提げて歩くのが、どうしても嫌だったのだ。",

        u8"　鐘の音が止んだ。ひどく静かになって、風の音だけが残った。"
        u8"少年は立ち上がり、庭に降りて、木の幹に手を当てた。"
        u8"冷たく、ざらついていて、思っていたよりずっと細かった。",

        u8"　「行ってきます」と、ようやく声に出した。返事は無かったが、"
        u8"それでよかった。門をくぐるとき、一度だけ振り返った。"
        u8"縁側には誰もいなくなっていて、代わりに日が差していた。",

        u8"　坂を下りきったところで、川に出た。橋の欄干は塗りが剥げていて、"
        u8"手を掛けると粉になって落ちた。水は少なく、底の石がよく見えている。"
        u8"魚の影はどこにも無かった。",

        u8"　駅の待合には、老人がひとり座っていた。少年が入っていくと、"
        u8"目だけをこちらへ向けて、それきり動かなかった。"
        u8"時計の針が、こつん、こつんと鳴っている。",

        u8"　やがて汽笛が聞こえた。少年は鞄を持ち直し、外へ出た。"
        u8"風は朝より少し暖かくなっていて、遠くの山の稜線がよく見えた。"
        u8"鐘は、もう鳴らなかった。",

        u8"　この段落は二ページ目の途中から始まり、三ページ目へ続く。"
        u8"三ページ目は段組を一段にしてあるので行長が変わり、"
        u8"またいだところで残りが組み直される。段の高さが違うコンテナへ"
        u8"流し込むときに必要になる処理で、行長を変えずに引き継ぐと"
        u8"版面からあふれてしまう。組み直しの回数はコンソールに出る。",
    };

    std::vector<BlockLayout::Paragraph> paragraphs;
    for (const char* body : bodies) {
        BlockLayout::Paragraph p;
        p.text = utf8ToUtf16(body);
        p.style = style;
        p.options = opts;
        p.spaceAfter = 0.0f;
        paragraphs.push_back(std::move(p));
    }

    //--------------------------------------------------------------------------
    // 5. 流し込み
    //--------------------------------------------------------------------------
    printf("3. Flowing text...\n");

    BlockLayout block;
    block.setContainers(containers);
    block.layout(paragraphs);

    // 版面と段の枠
    for (const auto& c : containers) {
        renderer.drawRect(c.x, c.y, c.width, c.height, 0x00000000, 0xFFCCCCCC, 1.0f);
        for (int s = 0; s < c.columnCount; ++s) {
            renderer.drawRect(c.x, c.sectionY(s), c.width, c.sectionHeight(),
                              0x00000000, 0xFFEEEEEE, 1.0f);
        }
    }

    renderer.drawBlockLayout(block, black);

    for (size_t i = 0; i < containers.size(); ++i) {
        char label[64];
        std::snprintf(label, sizeof(label), "page %zu", i + 1);
        renderer.drawText(utf8ToUtf16(label), containers[i].x,
                          containers[i].y - 14.0f, labelStyle, gray);
    }

    //--------------------------------------------------------------------------
    // 6. 結果
    //--------------------------------------------------------------------------
    printf("\n4. Result\n");
    printf("   paragraphs=%zu  chunks=%zu  placed lines=%zu  used containers=%zu\n",
           paragraphs.size(), block.getChunkCount(), block.getPlacedLineCount(),
           block.getUsedContainerCount());

    for (size_t i = 0; i < containers.size(); ++i) {
        size_t first = 0, count = 0;
        block.getContainerLineRange(i, first, count);
        int perSection[8] = {};
        for (size_t k = first; k < first + count; ++k) {
            const auto& pl = block.getPlacedLine(k);
            if (pl.sectionIndex < 8) ++perSection[pl.sectionIndex];
        }
        printf("   page %zu: lines=%zu (section0=%d section1=%d)\n",
               i + 1, count, perSection[0], perSection[1]);
    }

    // 行長（段の高さ）が変わったところで組み直された断片
    int rebroken = 0;
    for (size_t i = 0; i < block.getChunkCount(); ++i) {
        if (block.getChunk(i).charOffset != 0) ++rebroken;
    }
    printf("   chunks re-broken for a new line length: %d\n", rebroken);

    // 段をまたいだ段落を、配置結果からも数える
    int crossing = 0;
    for (size_t k = 1; k < block.getPlacedLineCount(); ++k) {
        const auto& prev = block.getPlacedLine(k - 1);
        const auto& cur = block.getPlacedLine(k);
        const size_t prevPara = block.getChunk(prev.chunkIndex).paragraphIndex;
        const size_t curPara = block.getChunk(cur.chunkIndex).paragraphIndex;
        if (prevPara == curPara &&
            (prev.sectionIndex != cur.sectionIndex ||
             prev.containerIndex != cur.containerIndex)) {
            ++crossing;
        }
    }
    printf("   paragraph continuations at section/page boundaries: %d\n", crossing);

    if (block.getOverflow().has) {
        printf("   OVERFLOW: paragraph %zu char %zu did not fit\n",
               block.getOverflow().paragraphIndex, block.getOverflow().charOffset);
    } else {
        printf("   all text fit\n");
    }

    //--------------------------------------------------------------------------
    // 7. 出力
    //--------------------------------------------------------------------------
    printf("\n5. Saving...\n");
    saveBMP("output_vertical_page.bmp", buffer.data(), WIDTH, HEIGHT);

    printf("\nDone.\n");
    return 0;
}
