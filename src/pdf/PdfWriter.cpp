/**
 * PdfWriter.cpp
 *
 * 組版結果（GlyphInfo 列）→ PDF
 *
 * グリフ ID を直接書くので、PDF ビューア側で再シェイピングされることがない。
 * これが「同じ組版結果から画面と PDF が出る」ための条件になる。
 */

#include "richtext/pdf/PdfWriter.hpp"

#include "richtext/FontFace.hpp"
#include "richtext/GlyphTransform.hpp"

#include "SfntInfo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

namespace richtext::pdf {

namespace {

/// PDF の実数表記（指数表記は使えない）
std::string num(float v) {
    if (!std::isfinite(v)) v = 0.0f;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(v));
    std::string s(buf);
    // 末尾の 0 と小数点を落とす
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s.empty() || s == "-0") s = "0";
    return s;
}

std::string hex4(uint32_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", v & 0xFFFF);
    return std::string(buf);
}

/// PDF の文字列リテラル用エスケープ
std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '(' || c == ')' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

/// PDF 名前に使える形へ（英数字と一部記号のみ）
std::string sanitizeName(const std::string& s) {
    std::string out;
    for (char c : s) {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            c == '-' || c == '+' || c == '_') {
            out += c;
        }
    }
    if (out.empty()) out = "Font";
    return out;
}

struct Rgba {
    float r = 0, g = 0, b = 0, a = 1;
};

Rgba unpackArgb(uint32_t argb) {
    Rgba c;
    c.a = static_cast<float>((argb >> 24) & 0xFF) / 255.0f;
    c.r = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
    c.g = static_cast<float>((argb >> 8) & 0xFF) / 255.0f;
    c.b = static_cast<float>(argb & 0xFF) / 255.0f;
    return c;
}

} // namespace

//------------------------------------------------------------------------------

struct PdfWriter::Impl {
    struct FontResource {
        const FontFace* face = nullptr;
        std::string resourceName;          ///< /F1 など
        std::string baseFont;
        SfntInfo sfnt;
        const uint8_t* data = nullptr;
        size_t dataSize = 0;
        std::set<uint32_t> usedGlyphs;
        std::map<uint32_t, uint32_t> toUnicode;   ///< GID → コードポイント
        bool embeddable = false;
    };

    struct Page {
        float width = 0;
        float height = 0;
        std::string content;
    };

    std::string title;
    std::string author;
    std::string creator = "richtext";
    bool embedToUnicode = true;

    std::vector<Page> pages;
    bool pageOpen = false;

    std::vector<std::unique_ptr<FontResource>> fonts;
    std::unordered_map<const FontFace*, FontResource*> fontMap;

    /// 透明度 → ExtGState 名
    std::map<int, std::string> alphaStates;

    std::vector<std::string> warnings;

    // --- 現在のページの描画状態（無駄なオペレータを出さないためのキャッシュ）---
    std::string curFontName;
    float curFontSize = -1.0f;
    int curTextRender = -1;
    std::string curFillColor;
    std::string curStrokeColor;
    float curLineWidth = -1.0f;
    std::string curAlphaState;

    Page& page() { return pages.back(); }

    void resetState() {
        curFontName.clear();
        curFontSize = -1.0f;
        curTextRender = -1;
        curFillColor.clear();
        curStrokeColor.clear();
        curLineWidth = -1.0f;
        curAlphaState.clear();
    }

    FontResource* acquireFont(const FontFace* face);
    std::string alphaStateName(float alpha);

    void ensureFillColor(const Rgba& c);
    void ensureStrokeColor(const Rgba& c);
    void ensureLineWidth(float w);
    void ensureAlpha(float alpha);

    void emitGlyph(FontResource* font, const GlyphInfo& glyph, float fontSize,
                   const Matrix2D& glyphMat, float penX, float penY,
                   int textRender, float strokeWidth,
                   const Rgba& color, bool stroke);

    std::string buildFontObjects(std::vector<std::string>& objects,
                                 std::string& fontDict);
};

//------------------------------------------------------------------------------

PdfWriter::PdfWriter() : impl_(std::make_unique<Impl>()) {}
PdfWriter::~PdfWriter() = default;

void PdfWriter::setTitle(std::string title) { impl_->title = std::move(title); }
void PdfWriter::setAuthor(std::string author) { impl_->author = std::move(author); }
void PdfWriter::setCreator(std::string creator) { impl_->creator = std::move(creator); }
void PdfWriter::setEmbedToUnicode(bool embed) { impl_->embedToUnicode = embed; }

size_t PdfWriter::getPageCount() const { return impl_->pages.size(); }

const std::vector<std::string>& PdfWriter::getWarnings() const { return impl_->warnings; }

//------------------------------------------------------------------------------
// ページ
//------------------------------------------------------------------------------

void PdfWriter::beginPage(float width, float height) {
    if (impl_->pageOpen) endPage();
    Impl::Page p;
    p.width = width;
    p.height = height;
    impl_->pages.push_back(std::move(p));
    impl_->pageOpen = true;
    impl_->resetState();
}

void PdfWriter::endPage() {
    impl_->pageOpen = false;
}

//------------------------------------------------------------------------------
// フォント
//------------------------------------------------------------------------------

PdfWriter::Impl::FontResource* PdfWriter::Impl::acquireFont(const FontFace* face) {
    auto it = fontMap.find(face);
    if (it != fontMap.end()) return it->second;

    auto res = std::make_unique<FontResource>();
    res->face = face;
    res->resourceName = "F" + std::to_string(fonts.size() + 1);

    const auto& backend = face->getBackendFace();
    if (backend) {
        res->data = static_cast<const uint8_t*>(backend->fontData());
        res->dataSize = backend->fontDataSize();
    }

    std::string base = face->getFamilyName();
    if (base.empty()) base = face->getFontName();
    const std::string style = face->getStyleName();
    if (!style.empty() && style != "Regular") base += "-" + style;
    res->baseFont = sanitizeName(base);

    if (res->data && res->dataSize > 0 && parseSfnt(res->data, res->dataSize, res->sfnt)) {
        res->embeddable = true;
    } else {
        res->embeddable = false;
        warnings.push_back("font not embeddable: " + res->baseFont +
                           (res->sfnt.isCollection ? " (TTC is not supported)"
                                                   : " (no usable sfnt data)"));
    }

    FontResource* raw = res.get();
    fonts.push_back(std::move(res));
    fontMap.emplace(face, raw);
    return raw;
}

std::string PdfWriter::Impl::alphaStateName(float alpha) {
    const int key = static_cast<int>(alpha * 255.0f + 0.5f);
    auto it = alphaStates.find(key);
    if (it != alphaStates.end()) return it->second;
    const std::string name = "GS" + std::to_string(alphaStates.size() + 1);
    alphaStates.emplace(key, name);
    return name;
}

//------------------------------------------------------------------------------
// 描画状態
//------------------------------------------------------------------------------

void PdfWriter::Impl::ensureFillColor(const Rgba& c) {
    const std::string op = num(c.r) + " " + num(c.g) + " " + num(c.b) + " rg\n";
    if (op == curFillColor) return;
    page().content += op;
    curFillColor = op;
}

void PdfWriter::Impl::ensureStrokeColor(const Rgba& c) {
    const std::string op = num(c.r) + " " + num(c.g) + " " + num(c.b) + " RG\n";
    if (op == curStrokeColor) return;
    page().content += op;
    curStrokeColor = op;
}

void PdfWriter::Impl::ensureLineWidth(float w) {
    if (std::fabs(w - curLineWidth) < 1e-4f) return;
    page().content += num(w) + " w\n";
    curLineWidth = w;
}

void PdfWriter::Impl::ensureAlpha(float alpha) {
    const std::string name = (alpha >= 0.999f) ? std::string() : alphaStateName(alpha);
    if (name == curAlphaState) return;
    if (name.empty()) {
        // 不透明へ戻す
        page().content += "/" + alphaStateName(1.0f) + " gs\n";
    } else {
        page().content += "/" + name + " gs\n";
    }
    curAlphaState = name;
}

//------------------------------------------------------------------------------
// グリフ
//------------------------------------------------------------------------------

void PdfWriter::Impl::emitGlyph(FontResource* font, const GlyphInfo& glyph, float fontSize,
                                const Matrix2D& glyphMat, float penX, float penY,
                                int textRender, float strokeWidth,
                                const Rgba& color, bool stroke) {
    if (!font->embeddable) return;

    font->usedGlyphs.insert(glyph.glyphId);

    ensureAlpha(color.a);
    if (stroke) {
        ensureStrokeColor(color);
        ensureLineWidth(strokeWidth);
    }
    ensureFillColor(color);

    std::string& out = page().content;
    out += "BT\n";

    if (font->resourceName != curFontName || std::fabs(fontSize - curFontSize) > 1e-4f) {
        out += "/" + font->resourceName + " " + num(fontSize) + " Tf\n";
        curFontName = font->resourceName;
        curFontSize = fontSize;
    }
    if (textRender != curTextRender) {
        out += std::to_string(textRender) + " Tr\n";
        curTextRender = textRender;
    }

    // 画面（y-down）の 2x2 を PDF（y-up）へ移す。Y 反転で共役を取るので
    // シアー成分の符号が入れ替わる。
    // フォントサイズは Tf が掛けるので Tm には入れない（入れると二重に効く）。
    const float a = glyphMat.e11;
    const float b = -glyphMat.e21;
    const float c = -glyphMat.e12;
    const float d = glyphMat.e22;

    out += num(a) + " " + num(b) + " " + num(c) + " " + num(d) + " " +
           num(penX) + " " + num(page().height - penY) + " Tm\n";
    out += "<" + hex4(glyph.glyphId) + "> Tj\n";
    out += "ET\n";
}

//------------------------------------------------------------------------------

void PdfWriter::drawGlyphs(const std::vector<GlyphInfo>& glyphs,
                           float x, float y,
                           const TextStyle& style,
                           const Appearance& appearance,
                           const std::u16string* sourceText) {
    if (!impl_->pageOpen || glyphs.empty()) return;

    for (const GlyphInfo& glyph : glyphs) {
        if (!glyph.font) continue;

        Impl::FontResource* font = impl_->acquireFont(glyph.font);
        if (!font->embeddable) continue;

        const float fontSize = glyphFontSize(glyph, style);

        // wdth 軸は PDF へ持ち出せないので、フォント幅はすべて水平スケールで表す
        // （バリアブルフォントのインスタンス化は未対応）
        const float fakeScaleX = (style.fontWidth != 100.0f)
                                 ? style.fontWidth / 100.0f
                                 : 1.0f;
        const Matrix2D glyphMat = makeGlyphMatrix(glyph, fakeScaleX);
        const float fakeBold = fakeBoldStrokeWidth(glyph, fontSize);

        const float penX = x + glyph.x;
        const float penY = y + glyph.y;

        // ToUnicode 用に、このグリフが出てきた文字を控える
        if (impl_->embedToUnicode && sourceText && glyph.charIndex < sourceText->size()) {
            const char16_t c = (*sourceText)[glyph.charIndex];
            uint32_t cp = c;
            if (c >= 0xD800 && c <= 0xDBFF && glyph.charIndex + 1 < sourceText->size()) {
                const char16_t c2 = (*sourceText)[glyph.charIndex + 1];
                if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                    cp = 0x10000 + ((static_cast<uint32_t>(c - 0xD800)) << 10) +
                         (c2 - 0xDC00);
                }
            }
            font->toUnicode.emplace(glyph.glyphId, cp);
        }

        for (const auto& drawStyle : appearance.getStyles()) {
            const Rgba color = unpackArgb((static_cast<uint32_t>(drawStyle.a) << 24) |
                                          (static_cast<uint32_t>(drawStyle.r) << 16) |
                                          (static_cast<uint32_t>(drawStyle.g) << 8) |
                                          static_cast<uint32_t>(drawStyle.b));
            if (color.a <= 0.0f) continue;

            const float ox = penX + drawStyle.offsetX;
            const float oy = penY + drawStyle.offsetY;

            if (drawStyle.type == DrawStyle::Type::Fill) {
                // 塗り。フェイクボールドは同色の縁取りを重ねて太らせる
                const int mode = (fakeBold > 0.0f) ? 2 : 0;
                impl_->emitGlyph(font, glyph, fontSize, glyphMat, ox, oy, mode,
                                 fakeBold, color, fakeBold > 0.0f);
            } else {
                float totalStroke = drawStyle.strokeWidth + fakeBold;
                impl_->emitGlyph(font, glyph, fontSize, glyphMat, ox, oy, 1,
                                 totalStroke, color, true);
            }
        }
    }
}

void PdfWriter::drawVerticalParagraph(const vertical::VerticalParagraphLayout& para,
                                      float originX, float originY,
                                      const Appearance& appearance) {
    const std::u16string& text = para.getText();
    for (size_t i = 0; i < para.getLineCount(); ++i) {
        const auto& line = para.getLine(i);
        const auto pos = para.getLinePosition(i, originX, originY);
        drawGlyphs(line.glyphs, pos.x, pos.y, para.getStyle(), appearance, &text);
    }
}

void PdfWriter::drawBlockContainer(const vertical::BlockLayout& block,
                                   size_t containerIndex,
                                   float offsetX, float offsetY,
                                   const Appearance& appearance) {
    size_t first = 0, count = 0;
    block.getContainerLineRange(containerIndex, first, count);
    for (size_t i = first; i < first + count; ++i) {
        const auto& pl = block.getPlacedLine(i);
        const auto& line = block.getLine(pl);
        const std::u16string& text = block.getChunk(pl.chunkIndex).layout.getText();
        drawGlyphs(line.glyphs, pl.x + offsetX, pl.y + offsetY,
                   block.getStyle(pl), appearance, &text);
    }
}

void PdfWriter::drawRect(float x, float y, float width, float height,
                         uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    if (!impl_->pageOpen) return;

    const float pageHeight = impl_->page().height;
    const std::string rect = num(x) + " " + num(pageHeight - (y + height)) + " " +
                             num(width) + " " + num(height) + " re\n";

    std::string& out = impl_->page().content;
    const Rgba fill = unpackArgb(fillColor);
    const Rgba stroke = unpackArgb(strokeColor);

    if (fill.a > 0.0f) {
        out += "q\n";
        if (fill.a < 0.999f) out += "/" + impl_->alphaStateName(fill.a) + " gs\n";
        out += num(fill.r) + " " + num(fill.g) + " " + num(fill.b) + " rg\n";
        out += rect + "f\n";
        out += "Q\n";
    }
    if (stroke.a > 0.0f && strokeWidth > 0.0f) {
        out += "q\n";
        if (stroke.a < 0.999f) out += "/" + impl_->alphaStateName(stroke.a) + " gs\n";
        out += num(stroke.r) + " " + num(stroke.g) + " " + num(stroke.b) + " RG\n";
        out += num(strokeWidth) + " w\n";
        out += rect + "S\n";
        out += "Q\n";
    }
    impl_->resetState();
}

//------------------------------------------------------------------------------
// 書き出し
//------------------------------------------------------------------------------

namespace {

std::string streamObject(const std::string& dictExtra, const std::string& data) {
    std::string out = "<< /Length " + std::to_string(data.size());
    if (!dictExtra.empty()) out += " " + dictExtra;
    out += " >>\nstream\n";
    out += data;
    out += "\nendstream";
    return out;
}

/// 使用グリフの幅配列 /W を組み立てる
std::string buildWidthArray(const FontFace* face, const std::set<uint32_t>& glyphs,
                            float unitsPerEm) {
    const auto& backend = face->getBackendFace();
    if (!backend || glyphs.empty()) return std::string();

    std::string out = "[";
    auto it = glyphs.begin();
    while (it != glyphs.end()) {
        const uint32_t start = *it;
        std::vector<int> run;
        uint32_t expected = start;
        while (it != glyphs.end() && *it == expected) {
            FontGlyphMetrics metrics;
            int w = 1000;
            if (backend->getGlyphMetricsUnscaled(*it, false, false, metrics)) {
                w = static_cast<int>(metrics.advanceX * 1000.0f / unitsPerEm + 0.5f);
            }
            run.push_back(w);
            ++it;
            ++expected;
        }
        out += " " + std::to_string(start) + " [";
        for (size_t i = 0; i < run.size(); ++i) {
            if (i) out += " ";
            out += std::to_string(run[i]);
        }
        out += "]";
    }
    out += " ]";
    return out;
}

std::string buildToUnicodeCMap(const std::map<uint32_t, uint32_t>& map) {
    std::string body =
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\n"
        "begincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n"
        "/CMapType 2 def\n"
        "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";

    std::vector<std::pair<uint32_t, uint32_t>> entries(map.begin(), map.end());
    for (size_t i = 0; i < entries.size(); i += 100) {
        const size_t n = std::min<size_t>(100, entries.size() - i);
        body += std::to_string(n) + " beginbfchar\n";
        for (size_t k = 0; k < n; ++k) {
            const uint32_t gid = entries[i + k].first;
            const uint32_t cp = entries[i + k].second;
            body += "<" + hex4(gid) + "> <";
            if (cp <= 0xFFFF) {
                body += hex4(cp);
            } else {
                const uint32_t v = cp - 0x10000;
                body += hex4(0xD800 | (v >> 10));
                body += hex4(0xDC00 | (v & 0x3FF));
            }
            body += ">\n";
        }
        body += "endbfchar\n";
    }

    body += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    return body;
}

} // namespace

std::string PdfWriter::build() {
    if (impl_->pageOpen) endPage();

    std::vector<std::string> objects;   // 1-based（objects[0] が 1 0 obj）
    const auto addObject = [&](std::string body) -> int {
        objects.push_back(std::move(body));
        return static_cast<int>(objects.size());
    };

    // 1: Catalog, 2: Pages（あとで中身を埋める）
    const int catalogId = addObject("");
    const int pagesId = addObject("");

    // --- フォント ---
    std::string fontDictEntries;
    for (const auto& fontPtr : impl_->fonts) {
        Impl::FontResource& font = *fontPtr;
        if (!font.embeddable || font.usedGlyphs.empty()) continue;

        const float upem = static_cast<float>(font.sfnt.unitsPerEm);
        const float toPdf = 1000.0f / upem;

        // 埋め込むフォントファイル
        const std::string fontData(reinterpret_cast<const char*>(font.data),
                                   font.dataSize);
        int fontFileId;
        if (font.sfnt.isCFF) {
            fontFileId = addObject(streamObject("/Subtype /OpenType", fontData));
        } else {
            fontFileId = addObject(
                    streamObject("/Length1 " + std::to_string(font.dataSize), fontData));
        }

        // FontDescriptor
        std::string flags = "4";   // Symbolic
        if (font.sfnt.isSerif) flags = "6";
        if (font.sfnt.isFixedPitch) flags = std::to_string(std::stoi(flags) | 1);

        std::string descriptor =
            "<< /Type /FontDescriptor /FontName /" + font.baseFont +
            " /Flags " + flags +
            " /FontBBox [" + std::to_string(static_cast<int>(font.sfnt.xMin * toPdf)) + " " +
            std::to_string(static_cast<int>(font.sfnt.yMin * toPdf)) + " " +
            std::to_string(static_cast<int>(font.sfnt.xMax * toPdf)) + " " +
            std::to_string(static_cast<int>(font.sfnt.yMax * toPdf)) + "]" +
            " /ItalicAngle " + num(font.sfnt.italicAngle) +
            " /Ascent " + std::to_string(static_cast<int>(font.sfnt.ascender * toPdf)) +
            " /Descent " + std::to_string(static_cast<int>(font.sfnt.descender * toPdf)) +
            " /CapHeight " +
            std::to_string(static_cast<int>(
                    (font.sfnt.capHeight ? font.sfnt.capHeight : font.sfnt.ascender) * toPdf)) +
            " /StemV 80" +
            (font.sfnt.isCFF ? " /FontFile3 " : " /FontFile2 ") +
            std::to_string(fontFileId) + " 0 R >>";
        const int descriptorId = addObject(std::move(descriptor));

        // CIDFont
        const std::string widths = buildWidthArray(font.face, font.usedGlyphs, upem);
        std::string cidFont =
            "<< /Type /Font /Subtype /" +
            std::string(font.sfnt.isCFF ? "CIDFontType0" : "CIDFontType2") +
            " /BaseFont /" + font.baseFont +
            " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>"
            " /FontDescriptor " + std::to_string(descriptorId) + " 0 R"
            " /DW 1000";
        if (!widths.empty()) cidFont += " /W " + widths;
        if (!font.sfnt.isCFF) cidFont += " /CIDToGIDMap /Identity";
        cidFont += " >>";
        const int cidFontId = addObject(std::move(cidFont));

        int toUnicodeId = 0;
        if (impl_->embedToUnicode && !font.toUnicode.empty()) {
            toUnicodeId = addObject(streamObject("", buildToUnicodeCMap(font.toUnicode)));
        }

        std::string type0 =
            "<< /Type /Font /Subtype /Type0 /BaseFont /" + font.baseFont +
            " /Encoding /Identity-H /DescendantFonts [" + std::to_string(cidFontId) +
            " 0 R]";
        if (toUnicodeId) type0 += " /ToUnicode " + std::to_string(toUnicodeId) + " 0 R";
        type0 += " >>";
        const int type0Id = addObject(std::move(type0));

        fontDictEntries += " /" + font.resourceName + " " + std::to_string(type0Id) + " 0 R";
    }

    // --- ExtGState（透明度）---
    std::string gsDictEntries;
    for (const auto& entry : impl_->alphaStates) {
        const float alpha = static_cast<float>(entry.first) / 255.0f;
        const int id = addObject("<< /Type /ExtGState /ca " + num(alpha) +
                                 " /CA " + num(alpha) + " >>");
        gsDictEntries += " /" + entry.second + " " + std::to_string(id) + " 0 R";
    }

    std::string resources = "<< /ProcSet [/PDF /Text]";
    if (!fontDictEntries.empty()) resources += " /Font <<" + fontDictEntries + " >>";
    if (!gsDictEntries.empty()) resources += " /ExtGState <<" + gsDictEntries + " >>";
    resources += " >>";
    const int resourcesId = addObject(resources);

    // --- ページ ---
    std::vector<int> pageIds;
    for (const auto& p : impl_->pages) {
        const int contentId = addObject(streamObject("", p.content));
        const int pageId = addObject(
            "<< /Type /Page /Parent " + std::to_string(pagesId) + " 0 R"
            " /MediaBox [0 0 " + num(p.width) + " " + num(p.height) + "]"
            " /Resources " + std::to_string(resourcesId) + " 0 R"
            " /Contents " + std::to_string(contentId) + " 0 R >>");
        pageIds.push_back(pageId);
    }

    std::string kids = "[";
    for (size_t i = 0; i < pageIds.size(); ++i) {
        if (i) kids += " ";
        kids += std::to_string(pageIds[i]) + " 0 R";
    }
    kids += "]";
    objects[pagesId - 1] = "<< /Type /Pages /Kids " + kids + " /Count " +
                           std::to_string(pageIds.size()) + " >>";
    objects[catalogId - 1] = "<< /Type /Catalog /Pages " + std::to_string(pagesId) +
                             " 0 R >>";

    // --- 文書情報 ---
    int infoId = 0;
    if (!impl_->title.empty() || !impl_->author.empty() || !impl_->creator.empty()) {
        std::string info = "<<";
        if (!impl_->title.empty()) info += " /Title (" + escapeString(impl_->title) + ")";
        if (!impl_->author.empty()) info += " /Author (" + escapeString(impl_->author) + ")";
        if (!impl_->creator.empty()) info += " /Creator (" + escapeString(impl_->creator) + ")";
        info += " /Producer (richtext) >>";
        infoId = addObject(std::move(info));
    }

    // --- 直列化 ---
    std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offsets(objects.size() + 1, 0);

    for (size_t i = 0; i < objects.size(); ++i) {
        offsets[i + 1] = out.size();
        out += std::to_string(i + 1) + " 0 obj\n";
        out += objects[i];
        out += "\nendobj\n";
    }

    const size_t xrefOffset = out.size();
    out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (size_t i = 1; i <= objects.size(); ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010zu 00000 n \n", offsets[i]);
        out += buf;
    }

    out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
           " /Root " + std::to_string(catalogId) + " 0 R";
    if (infoId) out += " /Info " + std::to_string(infoId) + " 0 R";
    out += " >>\nstartxref\n" + std::to_string(xrefOffset) + "\n%%EOF\n";

    return out;
}

bool PdfWriter::save(const std::string& path) {
    const std::string data = build();
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(file);
}

} // namespace richtext::pdf
