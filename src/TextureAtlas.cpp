/**
 * TextureAtlas.cpp
 *
 * グリフのテクスチャアトラス管理
 */

#include "richtext/TextureAtlas.hpp"
#include "richtext/FontFace.hpp"
#include "richtext/GlyphRenderer.hpp"
#include "richtext/TextRenderer.hpp"
#include "richtext/GlyphTransform.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace richtext {

//------------------------------------------------------------------------------
// コンストラクタ・デストラクタ
//------------------------------------------------------------------------------

TextureAtlas::TextureAtlas(ITexture* texture)
    : texture_(texture)
{
    if (texture_) {
        bufferWidth_ = texture_->getWidth();
        bufferHeight_ = texture_->getHeight();
        renderBuffer_.resize(static_cast<size_t>(bufferWidth_) * bufferHeight_, 0);
    }
}

TextureAtlas::~TextureAtlas() = default;

//------------------------------------------------------------------------------
// クリア
//------------------------------------------------------------------------------

void TextureAtlas::clear() {
    glyphMap_.clear();
    currentX_ = 0;
    currentY_ = 0;
    shelfHeight_ = 0;
    std::fill(renderBuffer_.begin(), renderBuffer_.end(), 0);
}

//------------------------------------------------------------------------------
// グリフ追加
//------------------------------------------------------------------------------

TextureAtlas::GlyphKey TextureAtlas::makeKey(const GlyphInfo& glyph,
                                              const TextStyle& style,
                                              const Appearance& appearance) {
    GlyphKey key;
    key.fontPtr = reinterpret_cast<uintptr_t>(glyph.font);
    key.glyphId = glyph.glyphId;
    key.fontSizeQ = static_cast<uint32_t>(glyphFontSize(glyph, style) * 64.0f + 0.5f);
    key.fontWidthQ = static_cast<uint32_t>(style.fontWidth * 64.0f + 0.5f);
    // FontFakery::isFakeBold/isFakeItalic は非 const なのでコピーを使う
    minikin::FontFakery fakery = glyph.fakery;
    key.fakeryFlags = (fakery.isFakeBold() ? 1 : 0)
                    | (fakery.isFakeItalic() ? 2 : 0);
    key.rotationQ = static_cast<int32_t>(std::lround(glyph.rotation * 4096.0f));
    key.scaleXQ = static_cast<int32_t>(std::lround(glyph.scaleX * 4096.0f));
    key.scaleYQ = static_cast<int32_t>(std::lround(glyph.scaleY * 4096.0f));
    key.appearanceHash = appearance.hash();
    return key;
}

TextureAtlas::GlyphKey TextureAtlas::makeKey(const StyledLayout::GlyphPlacement& placement,
                                              const TextStyle& style,
                                              const Appearance& appearance) {
    GlyphKey key = makeKey(placement.glyph, style, appearance);
    if (placement.decorationCount > 0) {
        key.decorationColor = placement.decorationColor;
        for (int i = 0; i < placement.decorationCount && i < 2; ++i) {
            const auto& d = placement.decorations[i];
            key.decorationQ[i * 4 + 0] = static_cast<int32_t>(std::lround(d.x * 64.0f));
            key.decorationQ[i * 4 + 1] = static_cast<int32_t>(std::lround(d.y * 64.0f));
            key.decorationQ[i * 4 + 2] = static_cast<int32_t>(std::lround(d.width * 64.0f));
            key.decorationQ[i * 4 + 3] = static_cast<int32_t>(std::lround(d.height * 64.0f))
                                       | (d.capStart ? (1 << 29) : 0) | (d.capEnd ? (1 << 30) : 0);
        }
    }
    return key;
}

bool TextureAtlas::allocateRect(int width, int height, int& outX, int& outY) {
    if (bufferWidth_ <= 0 || bufferHeight_ <= 0) return false;

    int w = width + padding_;
    int h = height + padding_;

    // 現在の棚に収まるか
    if (currentX_ + w <= bufferWidth_) {
        outX = currentX_;
        outY = currentY_;
        currentX_ += w;
        if (h > shelfHeight_) shelfHeight_ = h;
        return true;
    }

    // 新しい棚に移る
    currentX_ = 0;
    currentY_ += shelfHeight_;
    shelfHeight_ = 0;

    if (currentY_ + h > bufferHeight_ || w > bufferWidth_) {
        return false;  // テクスチャ溢れ
    }

    outX = currentX_;
    outY = currentY_;
    currentX_ += w;
    shelfHeight_ = h;
    return true;
}

namespace {

bool rasterizeCell(const GlyphInfo& glyph, const TextStyle& style, const Appearance& appearance,
                   const StyledLayout::GlyphPlacement* placement, GlyphCell& out);

} // namespace

bool TextureAtlas::rasterizeGlyphCell(const GlyphInfo& glyph,
                                      const TextStyle& style,
                                      const Appearance& appearance,
                                      GlyphCell& out) {
    return rasterizeCell(glyph, style, appearance, nullptr, out);
}

bool TextureAtlas::rasterizeGlyphCell(const StyledLayout::GlyphPlacement& placement,
                                      const TextStyle& style,
                                      const Appearance& appearance,
                                      GlyphCell& out) {
    return rasterizeCell(placement.glyph, style, appearance, &placement, out);
}

namespace {

bool rasterizeCell(const GlyphInfo& glyph, const TextStyle& style, const Appearance& appearance,
                   const StyledLayout::GlyphPlacement* placement, GlyphCell& out) {
    if (!glyph.font) return false;

    // Appearance の装飾分（ストローク幅、影オフセット）
    float maxStroke = 0;
    float maxOffset = 0;
    for (const auto& ds : appearance.getStyles()) {
        if (ds.type == DrawStyle::Type::Stroke) {
            maxStroke = std::max(maxStroke, ds.strokeWidth);
        }
        maxOffset = std::max(maxOffset, std::max(std::abs(ds.offsetX), std::abs(ds.offsetY)));
    }

    // ペン位置を中心に、回転・倍率・斜体・縦組みのずれがあっても収まる大きさで描く
    const float size = glyphFontSize(glyph, style);
    const float scale = std::max(1.0f, std::max(std::abs(glyph.scaleX), std::abs(glyph.scaleY)));
    float extent = std::max(std::abs(glyph.advance), size) * scale * 1.6f;
    if (placement) {
        for (int i = 0; i < placement->decorationCount && i < 2; ++i) {
            const auto& d = placement->decorations[i];
            extent = std::max(extent, std::max(std::abs(d.x), std::abs(d.x + d.width)));
            extent = std::max(extent, std::max(std::abs(d.y), std::abs(d.y + d.height)));
        }
    }
    const int half = static_cast<int>(std::ceil(extent + maxStroke + maxOffset)) + 4;
    const int dim = half * 2;

    std::vector<uint32_t> temp(static_cast<size_t>(dim) * dim, 0);
    RenderTarget cell(temp.data(), dim, dim, dim);
    GlyphRenderer renderer(cell);
    renderer.setUseCache(false);
    // renderGlyph は glyph.x / glyph.y を足すので、ペン位置が (half, half) に来るよう打ち消す
    const float penX = static_cast<float>(half), penY = static_cast<float>(half);
    if (placement && placement->decorationCount > 0 && !appearance.getStyles().empty()) {
        // 下線・取り消し線のこの文字の受け持ち分。DrawStyle ごとに文字と交互に描いて、
        // 影・縁取り・塗りが文字と同じ重なり方になるようにする
        for (const auto& ds : appearance.getStyles()) {
            Appearance one;
            if (ds.type == DrawStyle::Type::Fill) one.addFill(ds); else one.addStroke(ds);
            renderer.renderGlyph(glyph, penX - glyph.x, penY - glyph.y, style, one);
            for (int i = 0; i < placement->decorationCount && i < 2; ++i) {
                float x, y, w, h;
                if (StyledLayout::decorationRect(placement->decorations[i], ds, x, y, w, h)) {
                    cell.fillRectF(penX + x, penY + y, w, h, ds.getColor());
                }
            }
        }
    } else {
        renderer.renderGlyph(glyph, penX - glyph.x, penY - glyph.y, style, appearance);
        if (placement) {
            for (int i = 0; i < placement->decorationCount && i < 2; ++i) {
                const auto& d = placement->decorations[i];
                cell.fillRectF(penX + d.x, penY + d.y, d.width, d.height, placement->decorationColor);
            }
        }
    }

    // インクのある範囲
    int minX = dim, minY = dim, maxX = -1, maxY = -1;
    for (int yy = 0; yy < dim; ++yy) {
        const uint32_t* row = &temp[static_cast<size_t>(yy) * dim];
        for (int xx = 0; xx < dim; ++xx) {
            if (row[xx] >> 24) {
                if (xx < minX) minX = xx;
                if (xx > maxX) maxX = xx;
                if (yy < minY) minY = yy;
                if (yy > maxY) maxY = yy;
            }
        }
    }
    if (maxX < 0) {
        // インクが無い（空白など）
        out.width = out.height = 1;
        out.offsetX = out.offsetY = 0;
        out.pixels.assign(1, 0);
        return true;
    }
    // 拡大縮小して貼ったときに隣と混ざらないよう 1px の透明な縁を残す
    minX = std::max(0, minX - 1);
    minY = std::max(0, minY - 1);
    maxX = std::min(dim - 1, maxX + 1);
    maxY = std::min(dim - 1, maxY + 1);

    out.width = maxX - minX + 1;
    out.height = maxY - minY + 1;
    out.offsetX = static_cast<float>(minX - half);
    out.offsetY = static_cast<float>(minY - half);
    out.pixels.resize(static_cast<size_t>(out.width) * out.height);
    for (int yy = 0; yy < out.height; ++yy) {
        std::memcpy(&out.pixels[static_cast<size_t>(yy) * out.width],
                    &temp[static_cast<size_t>(minY + yy) * dim + minX],
                    out.width * sizeof(uint32_t));
    }
    return true;
}

} // namespace

bool TextureAtlas::renderGlyphToAtlas(const GlyphInfo& glyph,
                                       const TextStyle& style,
                                       const Appearance& appearance) {
    if (!glyph.font) return false;

    auto key = makeKey(glyph, style, appearance);
    if (glyphMap_.count(key)) return true;  // 既にアトラスにある

    GlyphCell cell;
    if (!rasterizeGlyphCell(glyph, style, appearance, cell)) return false;
    return storeCell(key, cell);
}

bool TextureAtlas::storeCell(const GlyphKey& key, const GlyphCell& cell) {
    // 配置位置を確保
    int atlasX, atlasY;
    if (!allocateRect(cell.width, cell.height, atlasX, atlasY)) {
        return false;  // テクスチャ溢れ
    }

    // セル → renderBuffer_ にコピー
    for (int row = 0; row < cell.height; ++row) {
        int dstY = atlasY + row;
        if (dstY >= bufferHeight_) break;
        int copyWidth = std::min(cell.width, bufferWidth_ - atlasX);
        std::memcpy(&renderBuffer_[static_cast<size_t>(dstY) * bufferWidth_ + atlasX],
                    &cell.pixels[static_cast<size_t>(row) * cell.width],
                    copyWidth * sizeof(uint32_t));
    }

    // アトラスエントリ登録（オフセットはペン位置からの左上）
    AtlasEntry entry;
    entry.atlasX = atlasX;
    entry.atlasY = atlasY;
    entry.atlasWidth = cell.width;
    entry.atlasHeight = cell.height;
    entry.offsetX = cell.offsetX;
    entry.offsetY = cell.offsetY;
    glyphMap_[key] = entry;

    return true;
}
bool TextureAtlas::addLayout(const TextLayout& layout,
                              const Appearance& appearance) {
    const TextStyle& style = layout.getStyle();
    for (const auto& glyph : layout.getGlyphs()) {
        if (!renderGlyphToAtlas(glyph, style, appearance)) {
            return false;
        }
    }
    return true;
}

bool TextureAtlas::addParagraphLayout(const ParagraphLayout& para,
                                       const TextStyle& style,
                                       const Appearance& appearance) {
    for (size_t i = 0; i < para.getLineCount(); ++i) {
        TextLayout lineLayout = para.getLineLayout(i, style);
        if (!addLayout(lineLayout, appearance)) {
            return false;
        }
    }
    return true;
}

bool TextureAtlas::addStyledLayout(const StyledLayout& styledLayout) {
    if (!styledLayout.isValid()) return false;

    // 本文・ルビ・縦組みの注記をすべて登録する（下線・取り消し線の受け持ち分を含む）
    for (const auto& p : styledLayout.getPlacements()) {
        if (!p.glyph.font) return false;
        const TextStyle& style = styledLayout.getPlacementStyle(p);
        const Appearance& app = styledLayout.getPlacementAppearance(p);
        auto key = makeKey(p, style, app);
        if (glyphMap_.count(key)) continue;
        GlyphCell cell;
        if (!rasterizeGlyphCell(p, style, app, cell)) return false;
        if (!storeCell(key, cell)) return false;
    }
    return true;
}
//------------------------------------------------------------------------------
// テクスチャに書き込み
//------------------------------------------------------------------------------

void TextureAtlas::commit() {
    if (!texture_ || renderBuffer_.empty()) return;

    texture_->update(0, 0, bufferWidth_, bufferHeight_,
                     renderBuffer_.data(), bufferWidth_ * sizeof(uint32_t));
}

//------------------------------------------------------------------------------
// コピー矩形配列生成
//------------------------------------------------------------------------------

std::vector<CopyRect> TextureAtlas::getCopyRects(const TextLayout& layout,
                                                  float x, float y,
                                                  const Appearance& appearance,
                                                  int maxChars) const {
    std::vector<CopyRect> rects;
    const auto& glyphs = layout.getGlyphs();
    const TextStyle& style = layout.getStyle();

    // 論理順（charIndex 順）で先頭 maxChars 文字分のグリフを選択
    size_t threshold = SIZE_MAX;
    if (maxChars >= 0) {
        std::vector<size_t> sorted;
        sorted.reserve(glyphs.size());
        for (const auto& g : glyphs) sorted.push_back(g.charIndex);
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        if (static_cast<size_t>(maxChars) < sorted.size()) {
            threshold = sorted[maxChars];
        }
    }

    int displayIdx = 0;
    rects.reserve(glyphs.size());
    for (size_t i = 0; i < glyphs.size(); ++i) {
        if (glyphs[i].charIndex >= threshold) continue;

        auto key = makeKey(glyphs[i], style, appearance);
        auto it = glyphMap_.find(key);
        if (it == glyphMap_.end()) continue;

        const AtlasEntry& entry = it->second;
        CopyRect cr;
        cr.srcX = entry.atlasX;
        cr.srcY = entry.atlasY;
        cr.srcWidth = entry.atlasWidth;
        cr.srcHeight = entry.atlasHeight;
        cr.dstX = x + glyphs[i].x + entry.offsetX;
        cr.dstY = y + glyphs[i].y + entry.offsetY;
        cr.displayIndex = displayIdx++;
        cr.charIndex = static_cast<int>(glyphs[i].charIndex);
        rects.push_back(cr);
    }

    return rects;
}

std::vector<CopyRect> TextureAtlas::getCopyRects(const ParagraphLayout& para,
                                                  const RectF& rect,
                                                  ParagraphLayout::HAlign hAlign,
                                                  ParagraphLayout::VAlign vAlign,
                                                  const TextStyle& style,
                                                  const Appearance& appearance,
                                                  int maxChars) const {
    std::vector<CopyRect> rects;
    int remaining = maxChars;
    int globalIndex = 0;

    for (size_t i = 0; i < para.getLineCount(); ++i) {
        if (remaining == 0) break;

        auto pos = para.getLinePosition(i, rect.x, rect.y,
                                        rect.width, rect.height,
                                        hAlign, vAlign);

        TextLayout lineLayout = para.getLineLayout(i, style);
        int lineMax = remaining;
        auto lineRects = getCopyRects(lineLayout, pos.x, pos.y, appearance, lineMax);

        for (auto& cr : lineRects) {
            cr.displayIndex = globalIndex++;
            rects.push_back(cr);
        }

        if (remaining > 0) {
            // 論理文字数（ユニーク charIndex 数）で減算
            size_t lineCharCount = lineLayout.getCharCount();
            size_t drawn = std::min(static_cast<size_t>(remaining), lineCharCount);
            remaining -= static_cast<int>(drawn);
        }
    }

    return rects;
}

std::vector<CopyRect> TextureAtlas::getCopyRects(const StyledLayout& styledLayout,
                                                  float x, float y,
                                                  int maxChars,
                                                  const std::vector<ResolvedTiming>* resolvedTimings) const {
    std::vector<CopyRect> rects;
    if (!styledLayout.isValid()) return rects;

    const auto& parsed = styledLayout.getParsed();
    const auto& placements = styledLayout.getPlacements();

    // charIndex → delay のルックアップテーブルを構築
    std::unordered_map<int, float> delayMap;
    if (resolvedTimings) {
        for (const auto& rt : *resolvedTimings) {
            delayMap[rt.charIndex] = rt.delay;
        }
    }

    // 論理順（本文の charIndex 順）で先頭 maxChars 文字。
    // ルビ・注記は親文字の charIndex を持つので、親文字と一緒に出る
    size_t threshold = SIZE_MAX;
    if (maxChars >= 0) {
        std::vector<size_t> sorted;
        sorted.reserve(placements.size());
        for (const auto& p : placements) {
            if (p.kind == StyledLayout::GlyphPlacement::Kind::Base) sorted.push_back(p.charIndex);
        }
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        if (static_cast<size_t>(maxChars) < sorted.size()) threshold = sorted[maxChars];
    }

    const auto& links = parsed.links;
    int globalIndex = 0;
    rects.reserve(placements.size());
    for (const auto& p : placements) {
        if (p.charIndex >= threshold) continue;

        auto key = makeKey(p, styledLayout.getPlacementStyle(p),
                           styledLayout.getPlacementAppearance(p));
        auto it = glyphMap_.find(key);
        if (it == glyphMap_.end()) continue;

        const AtlasEntry& entry = it->second;
        CopyRect cr;
        cr.srcX = entry.atlasX;
        cr.srcY = entry.atlasY;
        cr.srcWidth = entry.atlasWidth;
        cr.srcHeight = entry.atlasHeight;
        cr.dstX = x + p.glyph.x + entry.offsetX;
        cr.dstY = y + p.glyph.y + entry.offsetY;
        cr.displayIndex = globalIndex++;
        cr.charIndex = static_cast<int>(p.charIndex);
        switch (p.kind) {
        case StyledLayout::GlyphPlacement::Kind::Ruby:       cr.kind = 1; break;
        case StyledLayout::GlyphPlacement::Kind::Annotation: cr.kind = 2; break;
        default: cr.kind = 0; break;
        }

        if (!delayMap.empty()) {
            auto dit = delayMap.find(static_cast<int>(p.charIndex));
            if (dit != delayMap.end()) cr.delay = dit->second;
        }

        for (int lk = 0; lk < static_cast<int>(links.size()); ++lk) {
            if (p.charIndex >= links[lk].startIndex && p.charIndex < links[lk].endIndex) {
                cr.linkIndex = lk;
                break;
            }
        }

        rects.push_back(cr);
    }

    return rects;
}
} // namespace richtext
