/**
 * TextRenderer.cpp
 * 
 * テキスト描画の統合インタフェース
 */

#include "richtext/TextRenderer.hpp"
#include "richtext/StyledLayout.hpp"
#include "richtext/GlyphRenderer.hpp"
#include "richtext/TagParser.hpp"

#include <algorithm>
#include <vector>

namespace richtext {

//------------------------------------------------------------------------------
// コンストラクタ・デストラクタ
//------------------------------------------------------------------------------

TextRenderer::TextRenderer() = default;

TextRenderer::~TextRenderer() {
    glyphRenderer_.reset();
}

//------------------------------------------------------------------------------
// 初期化・設定
//------------------------------------------------------------------------------

void TextRenderer::setCanvas(uint32_t* buffer, int width, int height, int pitch) {
    canvasWidth_ = width;
    canvasHeight_ = height;
    externalCanvas_ = false;
    if (pitch < 0) {
        // pitch が負の場合は上下反転（DIB形式）
        flipYMatrix_.e23 = static_cast<float>(canvasHeight_);
        flipYMatrixPtr_ = &flipYMatrix_;
        buffer = (uint32_t*)((uint8_t*)buffer + (height - 1) * pitch);
        pitch = -pitch;
    } else {
        flipYMatrixPtr_ = nullptr;
    }

    const int stridePixels = static_cast<int>(static_cast<size_t>(pitch) / sizeof(uint32_t));
    target_ = RenderTarget(buffer, width, height, stridePixels);

    // GlyphRenderer を作成
    glyphRenderer_ = std::make_unique<GlyphRenderer>(target_);
    glyphRenderer_->setUseCache(useCache_);
    glyphRenderer_->setFlipTransform(flipYMatrixPtr_);
}

void TextRenderer::clearCanvas(uint32_t color) {
    if (!target_.valid()) return;
    if (color != 0) {
        target_.fillRect(0, 0, target_.width(), target_.height(), color);
    }
}

void TextRenderer::sync() {
    // 直接バッファへ書き込むので同期は不要（API 互換のために残している）
}

//------------------------------------------------------------------------------
// キャッシュ制御
//------------------------------------------------------------------------------

void TextRenderer::setUseCache(bool use) {
    useCache_ = use;
    if (glyphRenderer_) {
        glyphRenderer_->setUseCache(use);
    }
}

bool TextRenderer::getUseCache() const {
    return useCache_;
}

void TextRenderer::clearCache() {
    if (glyphRenderer_) {
        glyphRenderer_->clearCache();
    }
}

void TextRenderer::setCacheMaxSize(size_t bytes) {
    if (glyphRenderer_) {
        glyphRenderer_->setCacheMaxSize(bytes);
    }
}

//------------------------------------------------------------------------------
// 描画メソッド
//------------------------------------------------------------------------------

RectF TextRenderer::drawText(const std::u16string& text,
                             float x, float y,
                             const TextStyle& style,
                             const Appearance& appearance) {
    // レイアウト実行
    TextLayout layout;
    layout.layout(text, style);
    
    return drawLayout(layout, x, y, appearance);
}

RectF TextRenderer::drawLayout(const TextLayout& layout,
                               float x, float y,
                               const Appearance& appearance,
                               int maxChars) {
    if (!glyphRenderer_) {
        return RectF();
    }

    // 各グリフを描画
    const auto& glyphs = layout.getGlyphs();
    const TextStyle& style = layout.getStyle();

    if (maxChars >= 0) {
        // 論理順（charIndex 順）で先頭 maxChars 文字分のグリフのみ描画
        // Bidi テキストではビジュアル順と論理順が異なるため、
        // charIndex でフィルタリングする必要がある
        // まず論理順での N 番目の文字位置を特定
        std::vector<size_t> charIndices;
        charIndices.reserve(glyphs.size());
        for (const auto& g : glyphs) {
            charIndices.push_back(g.charIndex);
        }
        // charIndex をソートして重複除去し、先頭 maxChars 文字の charIndex を得る
        std::vector<size_t> sorted = charIndices;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        size_t threshold = (static_cast<size_t>(maxChars) < sorted.size())
                           ? sorted[maxChars]
                           : SIZE_MAX;
        for (size_t i = 0; i < glyphs.size(); ++i) {
            if (glyphs[i].charIndex < threshold) {
                glyphRenderer_->renderGlyph(glyphs[i], x, y, style, appearance);
            }
        }
    } else {
        for (size_t i = 0; i < glyphs.size(); ++i) {
            glyphRenderer_->renderGlyph(glyphs[i], x, y, style, appearance);
        }
    }

    // getBounds() は Y上向き座標系（ベースライン基準）なので、
    // 左上原点の Y下向き座標系に変換して返す
    auto bounds = layout.getBounds();
    float top = y - bounds.top;
    float bottom = y - bounds.bottom;
    return RectF(x + bounds.left, top,
                bounds.right - bounds.left, bottom - top);
}

//------------------------------------------------------------------------------
// 縦組み
//------------------------------------------------------------------------------

RectF TextRenderer::drawVerticalText(const std::u16string& text,
                                     float x, float y,
                                     const TextStyle& style,
                                     const Appearance& appearance,
                                     vertical::TextOrientation orientation) {
    vertical::VerticalLayout layout;
    layout.layout(text, style, orientation);

    return drawVerticalLayout(layout, x, y, appearance);
}

RectF TextRenderer::drawVerticalLayout(const vertical::VerticalLayout& layout,
                                       float x, float y,
                                       const Appearance& appearance,
                                       int maxChars) {
    if (!glyphRenderer_) {
        return RectF();
    }

    const auto& glyphs = layout.getGlyphs();
    const TextStyle& style = layout.getStyle();

    if (maxChars >= 0) {
        // 論理順（charIndex 順）で先頭 maxChars 文字分のグリフのみ描画
        std::vector<size_t> sorted;
        sorted.reserve(glyphs.size());
        for (const auto& g : glyphs) sorted.push_back(g.charIndex);
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        size_t threshold = (static_cast<size_t>(maxChars) < sorted.size())
                           ? sorted[maxChars]
                           : SIZE_MAX;
        for (const auto& g : glyphs) {
            if (g.charIndex < threshold) {
                glyphRenderer_->renderGlyph(g, x, y, style, appearance);
            }
        }
    } else {
        glyphRenderer_->renderGlyphs(glyphs, x, y, style, appearance);
    }

    // 座標系は既に左上原点・y-down なのでそのまま矩形にする
    return RectF(x + layout.getExtentLeft(), y,
                 layout.getWidth(), layout.getLength());
}

RectF TextRenderer::drawVerticalParagraphLayout(const vertical::VerticalParagraphLayout& para,
                                                float originX, float originY,
                                                const Appearance& appearance,
                                                int maxChars) {
    if (!glyphRenderer_ || para.getLineCount() == 0) {
        return RectF();
    }

    const TextStyle& style = para.getStyle();
    int remaining = maxChars;

    float minX = 0.0f, maxX = 0.0f, maxLen = 0.0f;
    bool first = true;

    for (size_t i = 0; i < para.getLineCount(); ++i) {
        if (remaining == 0) break;

        const auto& line = para.getLine(i);
        const auto pos = para.getLinePosition(i, originX, originY);

        if (remaining < 0) {
            glyphRenderer_->renderGlyphs(line.glyphs, pos.x, pos.y, style, appearance);
        } else {
            // 論理順（charIndex 順）で先頭 remaining 文字分のみ描画
            std::vector<size_t> sorted;
            sorted.reserve(line.glyphs.size());
            for (const auto& g : line.glyphs) sorted.push_back(g.charIndex);
            std::sort(sorted.begin(), sorted.end());
            sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
            size_t threshold = (static_cast<size_t>(remaining) < sorted.size())
                               ? sorted[remaining]
                               : SIZE_MAX;
            for (const auto& g : line.glyphs) {
                if (g.charIndex < threshold) {
                    glyphRenderer_->renderGlyph(g, pos.x, pos.y, style, appearance);
                }
            }
            remaining -= static_cast<int>(std::min(sorted.size(),
                                                   static_cast<size_t>(remaining)));
        }

        const float left = pos.x + line.extentLeft;
        const float right = pos.x + line.extentRight;
        if (first) {
            minX = left;
            maxX = right;
            first = false;
        } else {
            minX = std::min(minX, left);
            maxX = std::max(maxX, right);
        }
        maxLen = std::max(maxLen, line.length);
    }

    if (first) return RectF();
    return RectF(minX, originY, maxX - minX, maxLen);
}

RectF TextRenderer::drawVerticalParagraph(const std::u16string& text,
                                          float originX, float originY,
                                          float lineLength,
                                          const TextStyle& style,
                                          const Appearance& appearance,
                                          const vertical::VerticalLayoutOptions& opts) {
    vertical::VerticalParagraphLayout para;
    para.layout(text, style, lineLength, opts);
    return drawVerticalParagraphLayout(para, originX, originY, appearance);
}

RectF TextRenderer::drawVerticalParagraph(const std::u16string& text,
                                          const std::vector<vertical::InlineAnnotation>& annotations,
                                          float originX, float originY,
                                          float lineLength,
                                          const TextStyle& style,
                                          const Appearance& appearance,
                                          const vertical::VerticalLayoutOptions& opts) {
    vertical::VerticalParagraphLayout para;
    para.layout(text, annotations, style, lineLength, opts);
    return drawVerticalParagraphLayout(para, originX, originY, appearance);
}

namespace {

/// 1 行のうち先頭 maxChars 文字分だけ描く（-1 で全て）。描いた文字数を返す
int renderLineChars(GlyphRenderer& renderer,
                    const std::vector<GlyphInfo>& glyphs,
                    float x, float y,
                    const TextStyle& style,
                    const Appearance& appearance,
                    int maxChars) {
    std::vector<size_t> sorted;
    sorted.reserve(glyphs.size());
    for (const auto& g : glyphs) sorted.push_back(g.charIndex);
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    if (maxChars < 0) {
        renderer.renderGlyphs(glyphs, x, y, style, appearance);
        return static_cast<int>(sorted.size());
    }

    const size_t threshold = (static_cast<size_t>(maxChars) < sorted.size())
                             ? sorted[maxChars]
                             : SIZE_MAX;
    for (const auto& g : glyphs) {
        if (g.charIndex < threshold) {
            renderer.renderGlyph(g, x, y, style, appearance);
        }
    }
    return static_cast<int>(std::min(sorted.size(), static_cast<size_t>(maxChars)));
}

} // namespace

RectF TextRenderer::drawBlockLayout(const vertical::BlockLayout& block,
                                    const Appearance& appearance,
                                    int maxChars) {
    if (!glyphRenderer_ || block.getPlacedLineCount() == 0) {
        return RectF();
    }

    int remaining = maxChars;
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool first = true;

    for (const auto& pl : block.getPlacedLines()) {
        if (remaining == 0) break;

        const auto& line = block.getLine(pl);
        const int drawn = renderLineChars(*glyphRenderer_, line.glyphs, pl.x, pl.y,
                                          block.getStyle(pl), appearance, remaining);
        if (remaining > 0) remaining -= drawn;

        const float left = pl.x + line.extentLeft;
        const float right = pl.x + line.extentRight;
        if (first) {
            minX = left; maxX = right;
            minY = pl.y; maxY = pl.y + line.length;
            first = false;
        } else {
            minX = std::min(minX, left);
            maxX = std::max(maxX, right);
            minY = std::min(minY, pl.y);
            maxY = std::max(maxY, pl.y + line.length);
        }
    }

    if (first) return RectF();
    return RectF(minX, minY, maxX - minX, maxY - minY);
}

RectF TextRenderer::drawBlockContainer(const vertical::BlockLayout& block,
                                       size_t containerIndex,
                                       float offsetX, float offsetY,
                                       const Appearance& appearance) {
    if (!glyphRenderer_) {
        return RectF();
    }

    size_t first = 0, count = 0;
    block.getContainerLineRange(containerIndex, first, count);
    if (count == 0) return RectF();

    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool init = false;

    for (size_t i = first; i < first + count; ++i) {
        const auto& pl = block.getPlacedLine(i);
        const auto& line = block.getLine(pl);
        const float x = pl.x + offsetX;
        const float y = pl.y + offsetY;
        glyphRenderer_->renderGlyphs(line.glyphs, x, y, block.getStyle(pl), appearance);

        const float left = x + line.extentLeft;
        const float right = x + line.extentRight;
        if (!init) {
            minX = left; maxX = right;
            minY = y; maxY = y + line.length;
            init = true;
        } else {
            minX = std::min(minX, left);
            maxX = std::max(maxX, right);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y + line.length);
        }
    }
    return RectF(minX, minY, maxX - minX, maxY - minY);
}

RectF TextRenderer::drawParagraphLayout(const ParagraphLayout& para,
                                         const RectF& rect,
                                         ParagraphLayout::HAlign hAlign,
                                         ParagraphLayout::VAlign vAlign,
                                         const TextStyle& style,
                                         const Appearance& appearance,
                                         int maxChars) {
    RectF totalBounds;
    bool first = true;
    int remaining = maxChars;

    for (size_t i = 0; i < para.getLineCount(); ++i) {
        if (remaining == 0) break;

        auto pos = para.getLinePosition(i, rect.x, rect.y,
                                        rect.width, rect.height,
                                        hAlign, vAlign);

        TextLayout lineLayout = para.getLineLayout(i, style);

        int lineMax = remaining;  // -1 はそのまま「全て」として渡る
        RectF lineBounds = drawLayout(lineLayout, pos.x, pos.y, appearance, lineMax);

        if (remaining > 0) {
            // 論理文字数（ユニーク charIndex 数）で減算
            size_t lineCharCount = lineLayout.getCharCount();
            size_t drawn = std::min(static_cast<size_t>(remaining), lineCharCount);
            remaining -= static_cast<int>(drawn);
        }

        if (first) {
            totalBounds = lineBounds;
            first = false;
        } else {
            float left = std::min(totalBounds.x, lineBounds.x);
            float top = std::min(totalBounds.y, lineBounds.y);
            float right = std::max(totalBounds.right(), lineBounds.right());
            float bottom = std::max(totalBounds.bottom(), lineBounds.bottom());
            totalBounds = RectF(left, top, right - left, bottom - top);
        }
    }

    return totalBounds;
}

RectF TextRenderer::drawParagraph(const std::u16string& text,
                                  const RectF& rect,
                                  ParagraphLayout::HAlign hAlign,
                                  ParagraphLayout::VAlign vAlign,
                                  const TextStyle& style,
                                  const Appearance& appearance) {
    ParagraphLayout para;
    para.layout(text, rect.width, style);
    return drawParagraphLayout(para, rect, hAlign, vAlign, style, appearance);
}

RectF TextRenderer::drawParagraph(const std::u16string& text,
                                  const RectF& rect,
                                  ParagraphLayout::HAlign hAlign,
                                  ParagraphLayout::VAlign vAlign,
                                  const std::vector<ParagraphLayout::StyleRun>& styleRuns,
                                  const Appearance& defaultAppearance) {
    ParagraphLayout para;
    para.layout(text, rect.width, styleRuns);

    TextStyle defaultStyle;
    if (!styleRuns.empty()) {
        defaultStyle = styleRuns[0].style;
    }

    return drawParagraphLayout(para, rect, hAlign, vAlign, defaultStyle, defaultAppearance);
}

RectF TextRenderer::drawStyledText(const std::u16string& text,
                                   const RectF& rect,
                                   ParagraphLayout::HAlign hAlign,
                                   ParagraphLayout::VAlign vAlign,
                                   const std::map<std::string, TextStyle>& styles,
                                   const std::map<std::string, Appearance>& appearances,
                                   float lineSpacing) {
    if (!glyphRenderer_ || text.empty()) {
        return rect;
    }

    StyledLayout styledLayout;
    styledLayout.layout(text, rect.width, rect.height, hAlign, vAlign,
                        styles, appearances, lineSpacing);

    if (!styledLayout.isValid()) {
        return rect;
    }

    return drawStyledLayout(styledLayout, rect.x, rect.y);
}

RectF TextRenderer::drawStyledLayout(const StyledLayout& styledLayout,
                                     float x, float y,
                                     int maxChars) {
    if (!glyphRenderer_ || !styledLayout.isValid()) {
        return RectF();
    }

    const auto& placements = styledLayout.getPlacements();

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

    for (const auto& p : placements) {
        if (p.charIndex >= threshold) continue;
        const TextStyle& style = styledLayout.getPlacementStyle(p);
        const Appearance& app = styledLayout.getPlacementAppearance(p);
        if (p.decorationCount == 0) {
            glyphRenderer_->renderGlyph(p.glyph, x, y, style, app);
            continue;
        }
        // 下線・取り消し線（文字ごとに切ってある）。DrawStyle ごとに文字と交互に描く
        const float px = x + p.glyph.x, py = y + p.glyph.y;
        if (app.getStyles().empty()) {
            glyphRenderer_->renderGlyph(p.glyph, x, y, style, app);
            for (int i = 0; i < p.decorationCount && i < 2; ++i) {
                const auto& d = p.decorations[i];
                drawRect(px + d.x, py + d.y, d.width, d.height, p.decorationColor, 0, 0);
            }
            continue;
        }
        for (const auto& ds : app.getStyles()) {
            Appearance one;
            if (ds.type == DrawStyle::Type::Fill) one.addFill(ds); else one.addStroke(ds);
            glyphRenderer_->renderGlyph(p.glyph, x, y, style, one);
            for (int i = 0; i < p.decorationCount && i < 2; ++i) {
                float rx, ry, rw, rh;
                if (StyledLayout::decorationRect(p.decorations[i], ds, rx, ry, rw, rh)) {
                    drawRect(px + rx, py + ry, rw, rh, ds.getColor(), 0, 0);
                }
            }
        }
    }

    const auto& b = styledLayout.getBounds();
    return RectF(x + b.x, y + b.y, b.width, b.height);
}
//------------------------------------------------------------------------------
// グリフ情報取得
//------------------------------------------------------------------------------

std::vector<GlyphRenderInfo> TextRenderer::getGlyphInfos(const TextLayout& layout,
                                                         float x, float y) {
    std::vector<GlyphRenderInfo> infos;
    const auto& glyphs = layout.getGlyphs();
    infos.reserve(glyphs.size());
    
    for (const auto& glyph : glyphs) {
        GlyphRenderInfo info;
        info.charIndex = glyph.charIndex;
        info.x = x + glyph.x;
        info.y = y + glyph.y;
        info.advance = glyph.advance;
        
        // サイズはフォントメトリクスから取得する必要がある
        // 簡易的に ascent/descent を使用
        info.width = glyph.advance;
        info.height = -layout.getAscent() + layout.getDescent();
        
        // カラー絵文字判定
        info.isEmoji = glyph.font && glyph.font->isColorGlyph(glyph.glyphId);
        
        infos.push_back(info);
    }
    
    return infos;
}

void TextRenderer::drawGlyph(const TextLayout& layout,
                             size_t glyphIndex,
                             float x, float y,
                             const Appearance& appearance) {
    if (!glyphRenderer_ || glyphIndex >= layout.getGlyphCount()) {
        return;
    }
    
    const auto& glyph = layout.getGlyph(glyphIndex);
    const TextStyle& style = layout.getStyle();
    
    glyphRenderer_->renderGlyph(glyph, x, y, style, appearance);
}

void TextRenderer::drawRect(float x, float y, float width, float height,
                             uint32_t fillColor, uint32_t strokeColor,
                             float strokeWidth) {
    if (!target_.valid()) return;

    // 上下反転指定時は Y を反転して配置する
    float top = y;
    if (flipYMatrixPtr_) {
        top = flipYMatrixPtr_->e22 * y + flipYMatrixPtr_->e23 - height;
    }

    if (fillColor != 0) {
        target_.fillRectF(x, top, width, height, fillColor);
    }

    if (strokeWidth > 0 && strokeColor != 0) {
        // ストロークは辺の中心に乗る（内側・外側に半分ずつ）
        const float hw = strokeWidth * 0.5f;
        const float l = x - hw, t = top - hw;
        const float w = width + strokeWidth;
        target_.fillRectF(l, t, w, strokeWidth, strokeColor);                        // 上辺
        target_.fillRectF(l, top + height - hw, w, strokeWidth, strokeColor);        // 下辺
        target_.fillRectF(l, top + hw, strokeWidth, height - strokeWidth, strokeColor);  // 左辺
        target_.fillRectF(x + width - hw, top + hw, strokeWidth, height - strokeWidth,
                          strokeColor);                                              // 右辺
    }
}

} // namespace richtext
