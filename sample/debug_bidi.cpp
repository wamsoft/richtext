#include "richtext/FontManager.hpp"
#include "richtext/TextStyle.hpp"
#include "richtext/TextLayout.hpp"
#include <minikin/Layout.h>
#include <cstdio>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    SetConsoleOutputCP(65001);
    fprintf(stderr, "[1] start\n");

    auto& fm = richtext::FontManager::instance();

    // フルパスをそのまま開くローダー
    fm.setFontDataLoader([](const std::string& name) -> richtext::FontDataBuffer {
        std::ifstream file(name, std::ios::binary | std::ios::ate);
        if (!file) return nullptr;
        auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        auto buffer = std::make_shared<std::vector<uint8_t>>(size);
        if (!file.read(reinterpret_cast<char*>(buffer->data()), size)) return nullptr;
        return buffer;
    });

    fm.registerFont("C:/Windows/Fonts/msgothic.ttc", "ja");
    fm.registerFont("C:/Windows/Fonts/arial.ttf", "ar");
    fprintf(stderr, "[2] fonts registered\n");

    auto jaCol = fm.createCollection({"ja"});
    auto arCol = fm.createCollection({"ar"});
    auto multiCol = fm.createCollection({"ar", "ja"});
    fprintf(stderr, "[3] collections\n");

    // Test 1: jaCollection
    {
        richtext::TextStyle s;
        s.fontCollection = jaCol;
        s.fontSize = 28.0f;
        richtext::TextLayout layout;
        fprintf(stderr, "[4] jaCol layout...\n");
        layout.layout(u"Hello", s);
        fprintf(stderr, "[5] jaCol OK, glyphs=%zu\n", layout.getGlyphs().size());
    }

    // Test 2: arCollection
    {
        richtext::TextStyle s;
        s.fontCollection = arCol;
        s.fontSize = 28.0f;
        richtext::TextLayout layout;
        fprintf(stderr, "[6] arCol layout...\n");
        layout.layout(u"Hello", s);
        fprintf(stderr, "[7] arCol OK, glyphs=%zu\n", layout.getGlyphs().size());
    }

    // Test 3: multiCollection
    {
        richtext::TextStyle s;
        s.fontCollection = multiCol;
        s.fontSize = 28.0f;
        richtext::TextLayout layout;
        fprintf(stderr, "[8] multiCol layout...\n");
        layout.layout(u"Hello", s);
        fprintf(stderr, "[9] multiCol OK, glyphs=%zu\n", layout.getGlyphs().size());
    }

    fprintf(stderr, "[10] done\n");
    return 0;
}
