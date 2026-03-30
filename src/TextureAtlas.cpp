/**
 * TextureAtlas.cpp
 *
 * グリフのテクスチャアトラス管理
 */

#include "richtext/TextureAtlas.hpp"
#include "richtext/FontFace.hpp"
#include "richtext/GlyphRenderer.hpp"
#include "richtext/TextRenderer.hpp"

#include <thorvg.h>
#include <algorithm>
#include <cstring>

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

TextureAtlas::GlyphKey TextureAtlas::makeKey(const GlyphInfo& glyph, float fontSize) const {
    GlyphKey key;
    key.fontPtr = reinterpret_cast<uintptr_t>(glyph.font);
    key.glyphId = glyph.glyphId;
    key.fontSizeQ = static_cast<uint32_t>(fontSize * 64.0f + 0.5f);
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

    if (currentY_ + h > bufferHeight_) {
        return false;  // テクスチャ溢れ
    }

    outX = currentX_;
    outY = currentY_;
    currentX_ += w;
    shelfHeight_ = h;
    return true;
}

bool TextureAtlas::renderGlyphToAtlas(const GlyphInfo& glyph,
                                       const TextStyle& style,
                                       const Appearance& appearance) {
    if (!glyph.font) return false;

    auto key = makeKey(glyph, style.fontSize);
    if (glyphMap_.count(key)) return true;  // 既にアトラスにある

    // グリフのバウンディングサイズを推定
    // Appearance の装飾分（ストローク幅、影オフセット）を考慮
    float maxStroke = 0;
    float maxOffsetX = 0, maxOffsetY = 0;
    for (const auto& ds : appearance.getStyles()) {
        if (ds.type == DrawStyle::Type::Stroke) {
            maxStroke = std::max(maxStroke, ds.strokeWidth);
        }
        maxOffsetX = std::max(maxOffsetX, std::abs(ds.offsetX));
        maxOffsetY = std::max(maxOffsetY, std::abs(ds.offsetY));
    }

    float margin = maxStroke + 2.0f;
    int cellWidth = static_cast<int>(glyph.advance + margin * 2 + maxOffsetX * 2 + 1);
    int cellHeight = static_cast<int>(style.fontSize * 1.5f + margin * 2 + maxOffsetY * 2 + 1);

    // 配置位置を確保
    int atlasX, atlasY;
    if (!allocateRect(cellWidth, cellHeight, atlasX, atlasY)) {
        return false;  // テクスチャ溢れ
    }

    // 一時的な thorvg キャンバスでグリフをレンダリング
    auto* canvas = tvg::SwCanvas::gen();
    if (!canvas) return false;

    std::vector<uint32_t> tempBuffer(static_cast<size_t>(cellWidth) * cellHeight, 0);
    canvas->target(tempBuffer.data(), static_cast<uint32_t>(cellWidth),
                   static_cast<uint32_t>(cellWidth), static_cast<uint32_t>(cellHeight),
                   tvg::ColorSpace::ARGB8888);

    // GlyphRenderer でレンダリング
    GlyphRenderer renderer(canvas);
    renderer.setUseCache(false);

    // グリフの描画位置（セル内のベースライン位置）
    float localX = margin + maxOffsetX - glyph.x;
    float localY = margin + maxOffsetY + style.fontSize * 0.8f;

    renderer.renderGlyph(glyph, localX, localY, style, appearance);

    canvas->draw();
    canvas->sync();

    // tempBuffer → renderBuffer_ にコピー
    for (int row = 0; row < cellHeight; ++row) {
        int dstY = atlasY + row;
        if (dstY >= bufferHeight_) break;
        int srcOffset = row * cellWidth;
        int dstOffset = dstY * bufferWidth_ + atlasX;
        int copyWidth = std::min(cellWidth, bufferWidth_ - atlasX);
        std::memcpy(&renderBuffer_[dstOffset], &tempBuffer[srcOffset],
                     copyWidth * sizeof(uint32_t));
    }

    // キャンバス解放
    delete canvas;

    // アトラスエントリ登録
    AtlasEntry entry;
    entry.atlasX = atlasX;
    entry.atlasY = atlasY;
    entry.atlasWidth = cellWidth;
    entry.atlasHeight = cellHeight;
    entry.offsetX = -(margin + maxOffsetX);
    entry.offsetY = -(margin + maxOffsetY + style.fontSize * 0.8f);
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

    const auto& parsed = styledLayout.getParsed();

    for (const auto& ll : styledLayout.getLineLayouts()) {
        for (const auto& sl : ll.segments) {
            const auto& span = parsed.spans[sl.spanIdx];
            const TextStyle& segStyle = sl.layout.getStyle();
            for (const auto& glyph : sl.layout.getGlyphs()) {
                if (!renderGlyphToAtlas(glyph, segStyle, span.appearance)) {
                    return false;
                }
            }
        }
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
                                                  int maxGlyphs) const {
    std::vector<CopyRect> rects;
    const auto& glyphs = layout.getGlyphs();
    const TextStyle& style = layout.getStyle();

    // 論理順（charIndex 順）で先頭 maxGlyphs 文字分のグリフを選択
    size_t threshold = SIZE_MAX;
    if (maxGlyphs >= 0) {
        std::vector<size_t> sorted;
        sorted.reserve(glyphs.size());
        for (const auto& g : glyphs) sorted.push_back(g.charIndex);
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        if (static_cast<size_t>(maxGlyphs) < sorted.size()) {
            threshold = sorted[maxGlyphs];
        }
    }

    int displayIdx = 0;
    rects.reserve(glyphs.size());
    for (size_t i = 0; i < glyphs.size(); ++i) {
        if (glyphs[i].charIndex >= threshold) continue;

        auto key = makeKey(glyphs[i], style.fontSize);
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
        rects.push_back(cr);
    }

    return rects;
}

std::vector<CopyRect> TextureAtlas::getCopyRects(const ParagraphLayout& para,
                                                  const RectF& rect,
                                                  ParagraphLayout::HAlign hAlign,
                                                  ParagraphLayout::VAlign vAlign,
                                                  const TextStyle& style,
                                                  int maxGlyphs) const {
    std::vector<CopyRect> rects;
    int remaining = maxGlyphs;
    int globalIndex = 0;

    for (size_t i = 0; i < para.getLineCount(); ++i) {
        if (remaining == 0) break;

        auto pos = para.getLinePosition(i, rect.x, rect.y,
                                        rect.width, rect.height,
                                        hAlign, vAlign);

        TextLayout lineLayout = para.getLineLayout(i, style);
        int lineMax = remaining;
        auto lineRects = getCopyRects(lineLayout, pos.x, pos.y, lineMax);

        for (auto& cr : lineRects) {
            cr.displayIndex = globalIndex++;
            rects.push_back(cr);
        }

        if (remaining > 0) {
            // 論理文字数（ユニーク charIndex 数）で減算
            const auto& glyphs = lineLayout.getGlyphs();
            std::vector<size_t> chars;
            chars.reserve(glyphs.size());
            for (const auto& g : glyphs) chars.push_back(g.charIndex);
            std::sort(chars.begin(), chars.end());
            chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
            size_t lineCharCount = chars.size();
            size_t drawn = std::min(static_cast<size_t>(remaining), lineCharCount);
            remaining -= static_cast<int>(drawn);
        }
    }

    return rects;
}

std::vector<CopyRect> TextureAtlas::getCopyRects(const StyledLayout& styledLayout,
                                                  float x, float y,
                                                  int maxGlyphs) const {
    std::vector<CopyRect> rects;
    if (!styledLayout.isValid()) return rects;

    const auto& parsed = styledLayout.getParsed();
    const auto& para = styledLayout.getParagraphLayout();
    const auto& lineLayouts = styledLayout.getLineLayouts();
    ParagraphLayout::HAlign hAlign = styledLayout.getHAlign();
    ParagraphLayout::VAlign vAlign = styledLayout.getVAlign();
    float maxWidth = styledLayout.getMaxWidth();
    float maxHeight = styledLayout.getMaxHeight();

    int remaining = maxGlyphs;
    int globalIndex = 0;

    for (const auto& ll : lineLayouts) {
        if (remaining == 0) break;

        const ParagraphLayout::LineInfo& line = para.getLine(ll.lineIdx);

        auto pos = para.getLinePosition(ll.lineIdx, x, y,
                                        maxWidth, maxHeight,
                                        ParagraphLayout::HAlign::Left,
                                        vAlign);
        float baseY = pos.y;

        if (ll.segments.empty()) continue;

        // 水平アライン
        float startX = x;
        switch (hAlign) {
        case ParagraphLayout::HAlign::Left:
            startX = x;
            break;
        case ParagraphLayout::HAlign::Center:
            startX = x + (maxWidth - ll.totalWidth) / 2.0f;
            break;
        case ParagraphLayout::HAlign::Right:
            startX = x + maxWidth - ll.totalWidth;
            break;
        case ParagraphLayout::HAlign::Justify:
            startX = x;
            break;
        }

        float curX = startX;
        for (const auto& sl : ll.segments) {
            if (remaining == 0) break;

            const auto& span = parsed.spans[sl.spanIdx];
            float drawY = baseY + sl.yOffset;
            const TextStyle& segStyle = sl.layout.getStyle();
            const auto& glyphs = sl.layout.getGlyphs();

            // maxGlyphs 処理用に threshold を計算
            size_t threshold = SIZE_MAX;
            if (remaining >= 0) {
                std::vector<size_t> sorted;
                sorted.reserve(glyphs.size());
                for (const auto& g : glyphs) sorted.push_back(g.charIndex);
                std::sort(sorted.begin(), sorted.end());
                sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
                if (static_cast<size_t>(remaining) < sorted.size()) {
                    threshold = sorted[remaining];
                }
            }

            for (size_t i = 0; i < glyphs.size(); ++i) {
                if (glyphs[i].charIndex >= threshold) continue;

                auto key = makeKey(glyphs[i], segStyle.fontSize);
                auto it = glyphMap_.find(key);
                if (it == glyphMap_.end()) continue;

                const AtlasEntry& entry = it->second;
                CopyRect cr;
                cr.srcX = entry.atlasX;
                cr.srcY = entry.atlasY;
                cr.srcWidth = entry.atlasWidth;
                cr.srcHeight = entry.atlasHeight;
                cr.dstX = curX + glyphs[i].x + entry.offsetX;
                cr.dstY = drawY + glyphs[i].y + entry.offsetY;
                cr.displayIndex = globalIndex++;
                rects.push_back(cr);
            }

            if (remaining > 0) {
                std::vector<size_t> chars;
                chars.reserve(glyphs.size());
                for (const auto& g : glyphs) chars.push_back(g.charIndex);
                std::sort(chars.begin(), chars.end());
                chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
                size_t segCharCount = chars.size();
                size_t drawn = std::min(static_cast<size_t>(remaining), segCharCount);
                remaining -= static_cast<int>(drawn);
            }

            curX += sl.measuredWidth;
        }
    }

    return rects;
}

} // namespace richtext
