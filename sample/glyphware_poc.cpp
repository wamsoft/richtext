/**
 * glyphware_poc.cpp
 *
 * 検証用 PoC / スモークテスト: glyphware を minikin のフォントバックエンドと
 * して使う経路を確認する。
 *
 * 1. glyphware::Face を包む minikin::MinikinFont 実装 (GwMinikinFont) を作り、
 *    minikin::FontCollection を構築してレイアウトできるか
 * 2. 同じテキストを richtext::FontFace 経由 (FontBackend 実装) でもレイアウトし、
 *    グリフID / アドバンス / エクステントが一致するか
 * 3. グリフアウトラインが一致するか (font unit スケール変換込み)
 * 4. glyphware のアウトラインから thorvg で実際に描画できるか (BMP 出力)
 *
 * 本体ライブラリ (richtext_lib) には一切手を入れていない。
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <glyphware/glyphware.h>

// ヒンティング差の切り分けで FreeType を直接叩くため
#include <ft2build.h>
#include FT_FREETYPE_H

#include <minikin/Font.h>
#include <minikin/FontCollection.h>
#include <minikin/FontFamily.h>
#include <minikin/Layout.h>
#include <minikin/LocaleList.h>
#include <minikin/MinikinExtent.h>
#include <minikin/MinikinFont.h>
#include <minikin/MinikinPaint.h>
#include <minikin/MinikinRect.h>

#include <thorvg.h>

#include "richtext/FontFace.hpp"
#include "richtext/FontManager.hpp"

namespace {

const char* kDataDir = "../../../data";

//------------------------------------------------------------------------------
// ファイルから読むだけの glyphware::FontLoader
//------------------------------------------------------------------------------

class FileFontLoader : public glyphware::FontLoader {
public:
    explicit FileFontLoader(std::string dir) : dir_(std::move(dir)) {}

    std::shared_ptr<glyphware::FontBlob> load(std::string_view key) override {
        std::string path = dir_ + "/" + std::string(key);
        std::ifstream f(path, std::ios::binary);
        if (!f) return nullptr;
        std::string bytes((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        if (bytes.empty()) return nullptr;
        return std::make_shared<glyphware::OwnedFontBlob>(std::move(bytes));
    }

private:
    std::string dir_;
};

//------------------------------------------------------------------------------
// glyphware::Face を minikin::MinikinFont に橋渡しするアダプタ
//
// richtext::FontFace が FreeType を直接叩いている部分を、そのまま
// glyphware::Face の呼び出しに置き換えたもの。
//------------------------------------------------------------------------------

class GwMinikinFont : public minikin::MinikinFont {
public:
    GwMinikinFont(int32_t uniqueId,
                  std::shared_ptr<glyphware::Face> face,
                  std::shared_ptr<glyphware::FontBlob> blob,
                  int index)
        : minikin::MinikinFont(uniqueId)
        , face_(std::move(face))
        , blob_(std::move(blob))
        , index_(index) {}

    float GetHorizontalAdvance(uint32_t glyphId,
                               const minikin::MinikinPaint& paint,
                               const minikin::FontFakery& fakery) const override {
        minikin::FontFakery fk = fakery;
        face_->setPixelSize(toPixels(paint.size));
        glyphware::GlyphMetrics m;
        if (!face_->glyphMetrics(glyphId, m, fk.isFakeBold(), fk.isFakeItalic(),
                                 glyphware::Hinting::Unhinted)) {
            return 0.0f;
        }
        return m.advanceX;
    }

    void GetBounds(minikin::MinikinRect* bounds, uint32_t glyphId,
                   const minikin::MinikinPaint& paint,
                   const minikin::FontFakery& fakery) const override {
        if (!bounds) return;
        minikin::FontFakery fk = fakery;
        face_->setPixelSize(toPixels(paint.size));
        glyphware::GlyphMetrics m;
        if (!face_->glyphMetrics(glyphId, m, fk.isFakeBold(), fk.isFakeItalic(),
                                 glyphware::Hinting::Unhinted)) {
            bounds->mLeft = bounds->mTop = bounds->mRight = bounds->mBottom = 0;
            return;
        }
        bounds->mLeft = m.bearingX;
        bounds->mTop = m.bearingY;
        bounds->mRight = m.bearingX + m.width;
        bounds->mBottom = m.bearingY - m.height;
    }

    void GetFontExtent(minikin::MinikinExtent* extent,
                       const minikin::MinikinPaint& paint,
                       const minikin::FontFakery& /*fakery*/) const override {
        if (!extent) return;
        face_->setPixelSize(toPixels(paint.size));
        const glyphware::LineMetrics lm = face_->lineMetrics();
        const float upem = lm.unitsPerEm > 0 ? lm.unitsPerEm : 1000.0f;
        // font unit から直接計算する (richtext::FontFace と同じ式)
        extent->ascent = -lm.ascenderUnits * paint.size / upem;
        extent->descent = -lm.descenderUnits * paint.size / upem;
    }

    const void* GetFontData() const override { return blob_->data(); }
    size_t GetFontSize() const override { return blob_->size(); }
    int GetFontIndex() const override { return index_; }

    const std::vector<minikin::FontVariation>& GetAxes() const override {
        return axes_;
    }

    glyphware::Face& face() const { return *face_; }

private:
    static int toPixels(float size) { return static_cast<int>(size + 0.5f); }

    std::shared_ptr<glyphware::Face> face_;
    std::shared_ptr<glyphware::FontBlob> blob_;
    int index_ = 0;
    std::vector<minikin::FontVariation> axes_;
};

//------------------------------------------------------------------------------
// glyphware アウトライン -> thorvg パス
//------------------------------------------------------------------------------

class TvgPathSink : public glyphware::OutlineSink {
public:
    TvgPathSink(float scale, float originX, float originY,
                std::vector<tvg::PathCommand>& cmds, std::vector<tvg::Point>& pts)
        : scale_(scale), ox_(originX), oy_(originY), cmds_(cmds), pts_(pts) {}

    void moveTo(float x, float y) override {
        cmds_.push_back(tvg::PathCommand::MoveTo);
        pts_.push_back(pt(x, y));
        cur_ = pt(x, y);
        start_ = cur_;
    }
    void lineTo(float x, float y) override {
        cmds_.push_back(tvg::PathCommand::LineTo);
        pts_.push_back(pt(x, y));
        cur_ = pt(x, y);
    }
    void quadTo(float cx, float cy, float x, float y) override {
        // 2次 -> 3次ベジェ昇格 (richtext::FontFace::outlineToPath と同じ)
        const tvg::Point c = pt(cx, cy);
        const tvg::Point e = pt(x, y);
        tvg::Point c1{cur_.x + 2.0f / 3.0f * (c.x - cur_.x),
                      cur_.y + 2.0f / 3.0f * (c.y - cur_.y)};
        tvg::Point c2{e.x + 2.0f / 3.0f * (c.x - e.x),
                      e.y + 2.0f / 3.0f * (c.y - e.y)};
        cmds_.push_back(tvg::PathCommand::CubicTo);
        pts_.push_back(c1);
        pts_.push_back(c2);
        pts_.push_back(e);
        cur_ = e;
    }
    void cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override {
        cmds_.push_back(tvg::PathCommand::CubicTo);
        pts_.push_back(pt(c1x, c1y));
        pts_.push_back(pt(c2x, c2y));
        pts_.push_back(pt(x, y));
        cur_ = pt(x, y);
    }
    void close() override {
        cmds_.push_back(tvg::PathCommand::Close);
        cur_ = start_;
    }

private:
    tvg::Point pt(float x, float y) const {
        // font unit (y-up) -> ピクセル (y-down)
        return tvg::Point{ox_ + x * scale_, oy_ - y * scale_};
    }
    float scale_, ox_, oy_;
    std::vector<tvg::PathCommand>& cmds_;
    std::vector<tvg::Point>& pts_;
    tvg::Point cur_{0, 0};
    tvg::Point start_{0, 0};
};

//------------------------------------------------------------------------------
// BMP 出力
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
    BMPFileHeader fh;
    BMPInfoHeader ih;
    ih.width = width;
    ih.height = height;
    ih.imageSize = width * height * 4;
    fh.size = sizeof(fh) + sizeof(ih) + ih.imageSize;

    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
    file.write(reinterpret_cast<const char*>(&ih), sizeof(ih));

    std::vector<uint32_t> flipped(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        std::memcpy(&flipped[static_cast<size_t>(height - 1 - y) * width],
                    &pixels[static_cast<size_t>(y) * width], width * 4);
    }
    file.write(reinterpret_cast<const char*>(flipped.data()), ih.imageSize);
    return true;
}

//------------------------------------------------------------------------------
// 比較ヘルパー
//------------------------------------------------------------------------------

struct Bbox {
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    void add(float x, float y) {
        minX = std::min(minX, x); minY = std::min(minY, y);
        maxX = std::max(maxX, x); maxY = std::max(maxY, y);
    }
};

Bbox bboxOf(const std::vector<tvg::Point>& pts) {
    Bbox b;
    for (const auto& p : pts) b.add(p.x, p.y);
    return b;
}

int32_t sUniqueId = 10000;

} // namespace

//------------------------------------------------------------------------------

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== glyphware / minikin 統合 PoC ===\n\n");

    // richtext::FontFace の COLRv1 合成が thorvg を使うため、先に初期化しておく
    tvg::Initializer::init(0);

    const std::u16string text = u"Hello 日本語 World!";
    const float fontSize = 32.0f;

    //--------------------------------------------------------------------------
    // A) glyphware 側: Registry + Face + minikin アダプタ
    //--------------------------------------------------------------------------
    auto loader = std::make_shared<FileFontLoader>(kDataDir);
    glyphware::Registry registry(loader);

    const char* kLatin = "NotoSans-Regular.ttf";
    const char* kJa    = "NotoSansJP-Regular.otf";

    const int idLatin = registry.registerKey(kLatin);
    const int idJa    = registry.registerKey(kJa);
    if (!registry.resolve(idLatin) || !registry.resolve(idJa)) {
        fprintf(stderr, "glyphware: font resolve failed (data dir = %s)\n", kDataDir);
        return 1;
    }
    auto gwLatin = registry.face(idLatin);
    auto gwJa    = registry.face(idJa);
    if (!gwLatin || !gwJa) {
        fprintf(stderr, "glyphware: face open failed\n");
        return 1;
    }
    printf("[glyphware] %s: family=\"%s\" style=\"%s\" upem=%.0f color=%d\n",
           kLatin, gwLatin->descriptor().family.c_str(),
           gwLatin->descriptor().subfamily.c_str(),
           gwLatin->lineMetrics().unitsPerEm, gwLatin->descriptor().color ? 1 : 0);
    printf("[glyphware] %s: family=\"%s\" style=\"%s\" upem=%.0f\n\n",
           kJa, gwJa->descriptor().family.c_str(),
           gwJa->descriptor().subfamily.c_str(), gwJa->lineMetrics().unitsPerEm);

    // minikin の GetFontData 用の生バイト列は Face::blob() で取れる
    // (glyphware に追加した API。以前はローダーから二重ロードが必要だった)
    auto blobLatin = gwLatin->blob();
    auto blobJa    = gwJa->blob();
    printf("[glyphware] Face::blob() 経由でフォントバイト列取得: %zu / %zu bytes\n\n",
           blobLatin->size(), blobJa->size());

    auto gwFontLatin = std::make_shared<GwMinikinFont>(sUniqueId++, gwLatin, blobLatin, 0);
    auto gwFontJa    = std::make_shared<GwMinikinFont>(sUniqueId++, gwJa, blobJa, 0);

    std::vector<minikin::Font> gwLatinFonts;
    gwLatinFonts.push_back(minikin::Font::Builder(gwFontLatin).build());
    std::vector<minikin::Font> gwJaFonts;
    gwJaFonts.push_back(minikin::Font::Builder(gwFontJa).build());

    std::vector<std::shared_ptr<minikin::FontFamily>> gwFamilies;
    gwFamilies.push_back(std::make_shared<minikin::FontFamily>(std::move(gwLatinFonts)));
    gwFamilies.push_back(std::make_shared<minikin::FontFamily>(std::move(gwJaFonts)));
    auto gwCollection = std::make_shared<minikin::FontCollection>(gwFamilies);
    printf("[glyphware] minikin::FontCollection 構築 OK\n");

    //--------------------------------------------------------------------------
    // B) richtext::FontFace 経路 (FontBackend 経由) を比較対象として構築
    //--------------------------------------------------------------------------
    auto& fm = richtext::FontManager::instance();
    fm.initialize();
    fm.setFontDataLoader([](const std::string& name) -> richtext::FontDataBuffer {
        std::string path = std::string(kDataDir) + "/" + name;
        std::ifstream f(path, std::ios::binary);
        if (!f) return nullptr;
        auto buf = std::make_shared<std::vector<uint8_t>>(
            (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return buf->empty() ? nullptr : buf;
    });
    fm.registerFont(kLatin, "latin");
    fm.registerFont(kJa, "ja");
    auto ftCollection = fm.createCollection({"latin", "ja"});
    if (!ftCollection) {
        fprintf(stderr, "richtext: collection 構築失敗\n");
        return 1;
    }
    printf("[richtext ] minikin::FontCollection 構築 OK\n\n");

    //--------------------------------------------------------------------------
    // C) 同一テキストをレイアウトして比較
    //--------------------------------------------------------------------------
    auto layoutWith = [&](std::shared_ptr<minikin::FontCollection> coll) {
        minikin::MinikinPaint paint(coll);
        paint.size = fontSize;
        paint.scaleX = 1.0f;
        paint.fontStyle = minikin::FontStyle();
        paint.localeListId = minikin::registerLocaleList("ja-JP");
        const uint16_t* data = reinterpret_cast<const uint16_t*>(text.data());
        minikin::U16StringPiece piece(data, static_cast<uint32_t>(text.size()));
        minikin::Range range(0, static_cast<uint32_t>(text.size()));
        return minikin::Layout(piece, range, minikin::Bidi::LTR, paint,
                               minikin::StartHyphenEdit::NO_EDIT,
                               minikin::EndHyphenEdit::NO_EDIT);
    };

    minikin::Layout gwLayout = layoutWith(gwCollection);
    minikin::Layout ftLayout = layoutWith(ftCollection);

    printf("--- レイアウト比較 (\"Hello 日本語 World!\" / %.0fpx) ---\n", fontSize);
    printf("glyph 数    : glyphware=%zu  richtext=%zu\n",
           gwLayout.nGlyphs(), ftLayout.nGlyphs());
    printf("総アドバンス: glyphware=%.3f  richtext=%.3f  (差 %.3f)\n\n",
           gwLayout.getAdvance(), ftLayout.getAdvance(),
           gwLayout.getAdvance() - ftLayout.getAdvance());

    size_t n = std::min(gwLayout.nGlyphs(), ftLayout.nGlyphs());
    int gidMismatch = 0;
    float maxXDiff = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t g1 = gwLayout.getGlyphId(static_cast<int>(i));
        const uint32_t g2 = ftLayout.getGlyphId(static_cast<int>(i));
        const float x1 = gwLayout.getX(static_cast<int>(i));
        const float x2 = ftLayout.getX(static_cast<int>(i));
        if (g1 != g2) ++gidMismatch;
        maxXDiff = std::max(maxXDiff, std::fabs(x1 - x2));
        if (i < 8) {
            printf("  [%2zu] gid gw=%-5u rt=%-5u   x gw=%8.3f rt=%8.3f  diff=%+.3f\n",
                   i, g1, g2, x1, x2, x1 - x2);
        }
    }
    printf("  ...\n");
    printf("  glyphId 不一致数 = %d / %zu,  X座標の最大差 = %.3f px\n\n",
           gidMismatch, n, maxXDiff);

    // エクステント比較
    {
        minikin::MinikinPaint p1(gwCollection); p1.size = fontSize;
        minikin::MinikinPaint p2(ftCollection); p2.size = fontSize;
        minikin::MinikinExtent e1{}, e2{};
        minikin::FontFakery fk;
        if (gwLayout.nGlyphs() > 0) gwLayout.getFont(0)->GetFontExtent(&e1, p1, fk);
        if (ftLayout.nGlyphs() > 0) ftLayout.getFont(0)->GetFontExtent(&e2, p2, fk);
        printf("FontExtent  : glyphware ascent=%.3f descent=%.3f\n", e1.ascent, e1.descent);
        printf("              richtext  ascent=%.3f descent=%.3f\n\n", e2.ascent, e2.descent);
    }

    //--------------------------------------------------------------------------
    // D) アウトライン比較 ('A' と '日')
    //--------------------------------------------------------------------------
    printf("--- アウトライン比較 ---\n");
    struct Sample { const char* label; char32_t cp; std::shared_ptr<glyphware::Face> gw;
                    const char* ftName; };
    const Sample samples[] = {
        {"'A'", U'A', gwLatin, "latin"},
        {"'日'", U'日', gwJa, "ja"},
    };
    for (const auto& s : samples) {
        const uint32_t gid = s.gw->glyphIndex(s.cp);
        const float upem = s.gw->lineMetrics().unitsPerEm;

        std::vector<tvg::PathCommand> gwCmds;
        std::vector<tvg::Point> gwPts;
        TvgPathSink sink(fontSize / upem, 0.0f, 0.0f, gwCmds, gwPts);
        const bool gwOk = s.gw->glyphOutline(gid, sink);

        auto ftFace = fm.getFont(s.ftName);
        std::vector<tvg::PathCommand> ftCmds;
        std::vector<tvg::Point> ftPts;
        const bool ftOk = ftFace && ftFace->getGlyphPath(gid, fontSize, ftCmds, ftPts);

        printf("  %s gid=%u  glyphware:%s(cmd=%zu pt=%zu)  richtext:%s(cmd=%zu pt=%zu)\n",
               s.label, gid, gwOk ? "OK" : "NG", gwCmds.size(), gwPts.size(),
               ftOk ? "OK" : "NG", ftCmds.size(), ftPts.size());
        if (gwOk && ftOk) {
            const Bbox b1 = bboxOf(gwPts);
            const Bbox b2 = bboxOf(ftPts);
            printf("      bbox gw=(%.2f,%.2f)-(%.2f,%.2f)  rt=(%.2f,%.2f)-(%.2f,%.2f)\n",
                   b1.minX, b1.minY, b1.maxX, b1.maxY,
                   b2.minX, b2.minY, b2.maxX, b2.maxY);
        }
    }
    printf("\n");

    //--------------------------------------------------------------------------
    // D-2) アドバンス差の原因切り分け: ヒンティング有無で FreeType を直接叩く
    //--------------------------------------------------------------------------
    printf("--- アドバンス差の原因切り分け ('H'/'e'/'l' @32px) ---\n");
    printf("  %-5s %-12s %-16s %-16s\n", "gid", "glyphware", "FT_LOAD_DEFAULT", "FT_LOAD_NO_HINTING");
    {
        // FreeType 直叩きは glyphware::Face::ft() から借りる
        FT_Face ft = gwLatin->ft();
        const uint32_t gids[] = {43, 72, 79};
        for (uint32_t gid : gids) {
            gwLatin->setPixelSize(static_cast<int>(fontSize + 0.5f));
            glyphware::GlyphMetrics m{};
            gwLatin->glyphMetrics(gid, m);

            float advDefault = 0.0f, advNoHint = 0.0f;
            if (ft) {
                FT_Set_Pixel_Sizes(ft, 0, static_cast<FT_UInt>(fontSize + 0.5f));
                if (FT_Load_Glyph(ft, gid, FT_LOAD_DEFAULT) == 0)
                    advDefault = ft->glyph->advance.x / 64.0f;
                if (FT_Load_Glyph(ft, gid, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP |
                                           FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH) == 0)
                    advNoHint = ft->glyph->advance.x / 64.0f;
            }
            printf("  %-5u %-12.3f %-16.3f %-16.3f\n", gid, m.advanceX, advDefault, advNoHint);
        }
    }
    printf("\n");

    //--------------------------------------------------------------------------
    // D-3) バリアブルフォント軸 (glyphware に追加した setVariations)
    //--------------------------------------------------------------------------
    printf("--- バリアブルフォント軸 ---\n");
    {
        const int idVf = registry.registerKey("NotoSans-VariableFont.ttf");
        auto vf = registry.face(idVf);
        if (!vf) {
            printf("  NotoSans-VariableFont.ttf: open NG\n");
        } else {
            constexpr uint32_t kWght = 0x77676874; // 'wght'
            constexpr uint32_t kWdth = 0x77647468; // 'wdth'
            float lo = 0, def = 0, hi = 0;
            if (vf->axisRange(kWght, lo, def, hi))
                printf("  wght 軸: %.0f..%.0f (既定 %.0f)\n", lo, hi, def);
            if (vf->axisRange(kWdth, lo, def, hi))
                printf("  wdth 軸: %.1f..%.1f (既定 %.1f)\n", lo, hi, def);

            vf->setPixelSize(static_cast<int>(fontSize + 0.5f));
            const uint32_t gid = vf->glyphIndex(U'H');
            for (float w : {400.0f, 700.0f, 900.0f}) {
                vf->setVariations({{kWght, w}});
                glyphware::GlyphMetrics m{};
                vf->glyphMetrics(gid, m, false, false, glyphware::Hinting::Unhinted);
                printf("  wght=%3.0f -> 'H' advance=%.3f  width=%.3f\n", w, m.advanceX, m.width);
            }
            auto cur = vf->variations();
            printf("  variations() = %zu 軸", cur.size());
            for (const auto& c : cur) {
                printf(" [%c%c%c%c=%.1f]", (c.tag >> 24) & 0xFF, (c.tag >> 16) & 0xFF,
                       (c.tag >> 8) & 0xFF, c.tag & 0xFF, c.value);
            }
            printf("\n");
        }
    }
    printf("\n");

    //--------------------------------------------------------------------------
    // E) カラー絵文字ビットマップ比較 (CBDT / COLRv1)
    //--------------------------------------------------------------------------
    printf("--- カラーグリフ比較 ---\n");
    struct EmojiCase { const char* file; const char* regName; char32_t cp; const char* kind; };
    const EmojiCase emojis[] = {
        {"NotoColorEmoji.ttf", "emoji-cbdt", U'\U0001F600', "CBDT"},
        {"Noto-COLRv1.ttf",    "emoji-colr", U'\U0001F600', "COLRv1"},
    };
    for (const auto& e : emojis) {
        const int id = registry.registerKey(e.file);
        auto gwFace = registry.face(id);
        if (!gwFace) { printf("  %s: glyphware open NG\n", e.file); continue; }
        const uint32_t gid = gwFace->glyphIndex(e.cp);
        gwFace->setPixelSize(static_cast<int>(fontSize + 0.5f));
        glyphware::GlyphBitmap gwBmp;
        const bool gwOk = gwFace->glyphBitmap(gid, /*color=*/true, gwBmp);

        fm.registerFont(e.file, e.regName);
        auto ftFace = fm.getFont(e.regName);
        richtext::GlyphBitmap ftBmp;
        const bool ftOk = ftFace && ftFace->getGlyphBitmap(gid, fontSize, ftBmp);

        // アドバンス比較（旧 FreeType 実装の式 vs glyphware）
        {
            glyphware::GlyphMetrics mh{}, mu{};
            gwFace->glyphMetrics(gid, mh, false, false, glyphware::Hinting::Hinted);
            gwFace->glyphMetrics(gid, mu, false, false, glyphware::Hinting::Unhinted);
            float oldAdv = 0.0f;
            {
                if (FT_Face ft = gwFace->ft()) {
                    if (FT_HAS_FIXED_SIZES(ft)) FT_Select_Size(ft, 0);
                    else FT_Set_Pixel_Sizes(ft, 0, static_cast<FT_UInt>(fontSize + 0.5f));
                    if (FT_Load_Glyph(ft, gid, FT_LOAD_COLOR | FT_LOAD_DEFAULT) == 0) {
                        oldAdv = ft->glyph->advance.x / 64.0f;
                        if (FT_HAS_FIXED_SIZES(ft) && ft->num_fixed_sizes > 0) {
                            float fixedSize = static_cast<float>(ft->available_sizes[0].height);
                            if (fixedSize > 0) oldAdv *= fontSize / fixedSize;
                        }
                    }
                }
            }
            printf("      advance: 旧FreeType式=%.3f  glyphware(hinted)=%.3f  (unhinted)=%.3f\n",
                   oldAdv, mh.advanceX, mu.advanceX);
        }

        const char* fmt = gwBmp.format == glyphware::BitmapFormat::BGRA ? "BGRA"
                        : gwBmp.format == glyphware::BitmapFormat::Mono ? "Mono" : "Gray";
        printf("  %-20s (%-6s) gid=%-5u glyphware:%s %dx%d fmt=%s / richtext:%s %dx%d\n",
               e.file, e.kind, gid,
               gwOk ? "OK" : "NG", gwBmp.width, gwBmp.rows, gwOk ? fmt : "-",
               ftOk ? "OK" : "NG", ftBmp.width, ftBmp.height);
    }
    printf("\n");

    //--------------------------------------------------------------------------
    // F) glyphware のアウトラインを thorvg で実描画
    //--------------------------------------------------------------------------
    const int W = 640, H = 120;
    {
        std::vector<uint32_t> buffer(static_cast<size_t>(W) * H, 0xFF202020);
        auto* canvas = tvg::SwCanvas::gen();
        canvas->target(buffer.data(), W, W, H, tvg::ColorSpace::ARGB8888);

        float penX = 20.0f;
        const float baseY = 80.0f;
        for (size_t i = 0; i < gwLayout.nGlyphs(); ++i) {
            const auto* mf = gwLayout.getFont(static_cast<int>(i));
            const auto* gwf = static_cast<const GwMinikinFont*>(mf);
            glyphware::Face& face = gwf->face();
            const uint32_t gid = gwLayout.getGlyphId(static_cast<int>(i));
            const float upem = face.lineMetrics().unitsPerEm;

            std::vector<tvg::PathCommand> cmds;
            std::vector<tvg::Point> pts;
            TvgPathSink sink(fontSize / upem,
                             penX + gwLayout.getX(static_cast<int>(i)),
                             baseY + gwLayout.getY(static_cast<int>(i)), cmds, pts);
            if (!face.glyphOutline(gid, sink) || cmds.empty()) continue;

            auto* shape = tvg::Shape::gen();
            shape->appendPath(cmds.data(), static_cast<uint32_t>(cmds.size()),
                              pts.data(), static_cast<uint32_t>(pts.size()));
            shape->fill(255, 220, 120);
            canvas->add(shape);
        }
        (void)penX;
        canvas->draw();
        canvas->sync();

        if (saveBMP("glyphware_poc.bmp", buffer.data(), W, H)) {
            printf("--- 描画 ---\n  glyphware_poc.bmp (%dx%d) を出力\n", W, H);
        }
    }
    tvg::Initializer::term();

    printf("\n=== Done ===\n");
    return 0;
}
