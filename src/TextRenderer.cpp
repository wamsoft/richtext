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

TextRenderer::TextRenderer() {
    // thorvg 初期化（スレッド数指定）
    tvg::Initializer::init(4);
}

TextRenderer::~TextRenderer() {
    glyphRenderer_.reset();
    canvas_.reset();
    tvg::Initializer::term();
}

//------------------------------------------------------------------------------
// 初期化・設定
//------------------------------------------------------------------------------

void TextRenderer::setCanvas(uint32_t* buffer, int width, int height, int pitch) {
    canvasWidth_ = width;
    canvasHeight_ = height;
    if (pitch < 0) {
        // pitch が負の場合は上下反転（DIB形式）
        flipYMatrix_.e23 = static_cast<float>(canvasHeight_);
        flipYMatrixPtr_ = &flipYMatrix_;
        buffer = (uint32_t*)((uint8_t*)buffer + (height - 1) * pitch);
        pitch = -pitch;
    } else {
        flipYMatrixPtr_ = nullptr;
    }
    
    // SwCanvas を作成
    // EngineOption::None で dirty region（部分描画最適化）を無効化する。
    // Default のままだと draw()/sync() 後に fulldraw フラグが false になり、
    // 次回の draw() 時に preRender() が変更領域を 0x00000000 でクリアしてから
    // 再描画するため、グリフ周辺の背景が黒で塗りつぶされてしまう。
    auto* rawCanvas = tvg::SwCanvas::gen(tvg::EngineOption::None);
    canvas_.reset(rawCanvas);
    if (!canvas_) return;
    
    uint32_t stridePixels = static_cast<uint32_t>(pitch) / sizeof(uint32_t);
    canvas_->target(buffer, stridePixels,
                    static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                    tvg::ColorSpace::ARGB8888);
    
    // GlyphRenderer を作成
    glyphRenderer_ = std::make_unique<GlyphRenderer>(canvas_.get());
    glyphRenderer_->setUseCache(useCache_);
    glyphRenderer_->setFlipTransform(flipYMatrixPtr_);
}

void TextRenderer::clearCanvas(uint32_t color) {
    if (!canvas_) return;
    
    // キャンバスをクリア（描画オブジェクトを削除してリセット）
    // thorvg 1.0 では clear() の代わりに手動でやる必要がある可能性
    
    if (color != 0) {
        // 背景色で塗りつぶし
        tvg::Shape* bg = tvg::Shape::gen();
        if (bg) {
            bg->appendRect(0, 0, static_cast<float>(canvasWidth_),
                          static_cast<float>(canvasHeight_), 0, 0);
            uint8_t a = (color >> 24) & 0xFF;
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;
            bg->fill(r, g, b, a);
            if (flipYMatrixPtr_) {
                bg->transform(*flipYMatrixPtr_);
            }
            canvas_->add(bg);
        }
    }
}

void TextRenderer::sync() {
    if (!canvas_) return;
    canvas_->draw();
    canvas_->sync();
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

    const auto& parsed = styledLayout.getParsed();
    const auto& para = styledLayout.getParagraphLayout();
    const auto& lineLayouts = styledLayout.getLineLayouts();
    ParagraphLayout::HAlign hAlign = styledLayout.getHAlign();
    ParagraphLayout::VAlign vAlign = styledLayout.getVAlign();
    float maxWidth = styledLayout.getMaxWidth();
    float maxHeight = styledLayout.getMaxHeight();
    const auto& parserOptions = styledLayout.getParserOptions();

    RectF totalBounds;
    bool first = true;
    int remaining = maxChars;

    for (const auto& ll : lineLayouts) {
        if (remaining == 0) break;

        const ParagraphLayout::LineInfo& line = para.getLine(ll.lineIdx);

        // 行の垂直位置を計算
        auto pos = para.getLinePosition(ll.lineIdx, x, y,
                                        maxWidth, maxHeight,
                                        ParagraphLayout::HAlign::Left,  // X は後で調整
                                        vAlign);
        float baseY = pos.y;

        if (ll.segments.empty()) {
            continue;
        }

        // 水平アライン開始X を決定
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

        // 各セグメントを順番に描画
        float curX = startX;
        for (const auto& sl : ll.segments) {
            if (remaining == 0) break;

            const auto& span = parsed.spans[sl.spanIdx];
            float drawY = baseY + sl.yOffset;

            int segMax = remaining;  // -1 はそのまま「全て」として渡る
            RectF segBounds = drawLayout(sl.layout, curX, drawY, span.appearance, segMax);

            if (remaining > 0) {
                // 論理文字数（ユニーク charIndex 数）で減算
                size_t segCharCount = sl.layout.getCharCount();
                size_t drawn = std::min(static_cast<size_t>(remaining), segCharCount);
                remaining -= static_cast<int>(drawn);
            }

            // 下線・取り消し線の描画
            if (span.hasUnderline || span.hasStrikethrough) {
                uint32_t lineColor = 0xFF000000;
                for (const auto& ds : span.appearance.getStyles()) {
                    if (ds.type == DrawStyle::Type::Fill &&
                        ds.offsetX == 0.0f && ds.offsetY == 0.0f) {
                        lineColor = ds.getColor();
                    }
                }
                float lineThickness = std::max(1.0f, span.style.fontSize / 18.0f);

                if (span.hasUnderline) {
                    float underlineY = drawY + line.descent * 0.3f;
                    drawRect(curX, underlineY, sl.measuredWidth, lineThickness,
                             lineColor, 0, 0);
                }
                if (span.hasStrikethrough) {
                    float strikeY = drawY + line.ascent * 0.35f;
                    drawRect(curX, strikeY, sl.measuredWidth, lineThickness,
                             lineColor, 0, 0);
                }
            }

            // ルビ描画
            if (span.hasRuby && !span.rubyText.empty()) {
                TextStyle rubyStyle = span.style;
                rubyStyle.fontSize *= parserOptions.rubyScale;

                TextLayout rubyLayout;
                rubyLayout.layout(span.rubyText, rubyStyle);

                float rubyWidth = rubyLayout.getWidth();
                float rubyX = curX + (sl.measuredWidth - rubyWidth) / 2.0f;
                float rubyY = drawY + sl.layout.getAscent() + rubyLayout.getDescent();

                drawLayout(rubyLayout, rubyX, rubyY, span.appearance);
            }

            curX += sl.measuredWidth;

            if (first) {
                totalBounds = segBounds;
                first = false;
            } else {
                float left   = std::min(totalBounds.x, segBounds.x);
                float top    = std::min(totalBounds.y, segBounds.y);
                float right  = std::max(totalBounds.right(), segBounds.right());
                float bottom = std::max(totalBounds.bottom(), segBounds.bottom());
                totalBounds = RectF(left, top, right - left, bottom - top);
            }
        }
    }

    return totalBounds;
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
    if (!canvas_) return;

    tvg::Shape* shape = tvg::Shape::gen();
    if (!shape) return;

    shape->appendRect(x, y, width, height, 0, 0);

    uint8_t fa = (fillColor >> 24) & 0xFF;
    uint8_t fr = (fillColor >> 16) & 0xFF;
    uint8_t fg = (fillColor >> 8) & 0xFF;
    uint8_t fb = fillColor & 0xFF;
    shape->fill(fr, fg, fb, fa);

    if (strokeWidth > 0 && strokeColor != 0) {
        uint8_t sa = (strokeColor >> 24) & 0xFF;
        uint8_t sr = (strokeColor >> 16) & 0xFF;
        uint8_t sg = (strokeColor >> 8) & 0xFF;
        uint8_t sb = strokeColor & 0xFF;
        shape->strokeWidth(strokeWidth);
        shape->strokeFill(sr, sg, sb, sa);
    }

    if (flipYMatrixPtr_) {
        shape->transform(*flipYMatrixPtr_);
    }

    canvas_->add(shape);
}

} // namespace richtext
