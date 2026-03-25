/**
 * TextRenderer.cpp
 * 
 * テキスト描画の統合インタフェース
 */

#include "richtext/TextRenderer.hpp"
#include "richtext/GlyphRenderer.hpp"
#include "richtext/TagParser.hpp"

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
    buffer_ = buffer;
    canvasWidth_ = width;
    canvasHeight_ = height;
    canvasPitch_ = pitch;
    
    // SwCanvas を作成
    auto* rawCanvas = tvg::SwCanvas::gen();
    canvas_.reset(rawCanvas);
    if (!canvas_) return;
    
    // バッファを設定
    // pitch が負の場合は上下反転（DIB形式）
    if (pitch < 0) {
        // 上下反転の場合、最後の行のポインタを渡す
        uint32_t absPitch = static_cast<uint32_t>(-pitch);
        uint32_t stridePixels = absPitch / sizeof(uint32_t);
        uint32_t* lastRow = buffer + (height - 1) * (absPitch / sizeof(uint32_t));
        canvas_->target(lastRow, stridePixels,
                       static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                       tvg::ColorSpace::ARGB8888);
    } else {
        uint32_t stridePixels = static_cast<uint32_t>(pitch) / sizeof(uint32_t);
        canvas_->target(buffer, stridePixels,
                       static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                       tvg::ColorSpace::ARGB8888);
    }
    
    // GlyphRenderer を作成
    glyphRenderer_ = std::make_unique<GlyphRenderer>(canvas_.get());
    glyphRenderer_->setUseCache(useCache_);
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
                               const Appearance& appearance) {
    if (!glyphRenderer_) {
        return RectF();
    }
    
    // 各グリフを描画
    const auto& glyphs = layout.getGlyphs();
    const TextStyle& style = layout.getStyle();
    
    for (const auto& glyph : glyphs) {
        glyphRenderer_->renderGlyph(glyph, x, y, style, appearance);
    }
    
    // バウンディングボックスを返す
    auto bounds = layout.getBounds();
    return RectF(x + bounds.left, y + bounds.top,
                bounds.width(), bounds.height());
}

RectF TextRenderer::drawParagraph(const std::u16string& text,
                                  const RectF& rect,
                                  ParagraphLayout::HAlign hAlign,
                                  ParagraphLayout::VAlign vAlign,
                                  const TextStyle& style,
                                  const Appearance& appearance) {
    // パラグラフレイアウト
    ParagraphLayout para;
    para.layout(text, rect.width, style);
    
    RectF totalBounds;
    bool first = true;
    
    // 各行を描画
    for (size_t i = 0; i < para.getLineCount(); ++i) {
        auto pos = para.getLinePosition(i, rect.x, rect.y,
                                        rect.width, rect.height,
                                        hAlign, vAlign);
        
        TextLayout lineLayout = para.getLineLayout(i, style);
        RectF lineBounds = drawLayout(lineLayout, pos.x, pos.y, appearance);
        
        if (first) {
            totalBounds = lineBounds;
            first = false;
        } else {
            // Union
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
                                  const std::vector<ParagraphLayout::StyleRun>& styleRuns,
                                  const Appearance& defaultAppearance) {
    // パラグラフレイアウト（複数スタイル）
    ParagraphLayout para;
    para.layout(text, rect.width, styleRuns);
    
    RectF totalBounds;
    bool first = true;
    
    // 各行を描画
    for (size_t i = 0; i < para.getLineCount(); ++i) {
        // デフォルトスタイルを使用（簡易実装）
        TextStyle defaultStyle;
        if (!styleRuns.empty()) {
            defaultStyle = styleRuns[0].style;
        }
        
        auto pos = para.getLinePosition(i, rect.x, rect.y,
                                        rect.width, rect.height,
                                        hAlign, vAlign);
        
        TextLayout lineLayout = para.getLineLayout(i, defaultStyle);
        RectF lineBounds = drawLayout(lineLayout, pos.x, pos.y, defaultAppearance);
        
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

RectF TextRenderer::drawStyledText(const std::u16string& text,
                                   const RectF& rect,
                                   ParagraphLayout::HAlign hAlign,
                                   ParagraphLayout::VAlign vAlign,
                                   const std::map<std::string, TextStyle>& styles,
                                   const std::map<std::string, Appearance>& appearances) {
    if (!glyphRenderer_ || text.empty()) {
        return rect;
    }

    // デフォルトのスタイル・外観を styles/appearances から取得するか空で生成
    TextStyle defaultStyle;
    auto sit = styles.find("default");
    if (sit != styles.end()) defaultStyle = sit->second;

    Appearance defaultAppearance;
    auto ait = appearances.find("default");
    if (ait != appearances.end()) defaultAppearance = ait->second;
    if (defaultAppearance.isEmpty()) defaultAppearance = Appearance::defaultAppearance();

    // タグ付きテキストを解析
    TagParser parser;
    TagParser::ParseResult parsed = parser.parse(text, defaultStyle, defaultAppearance, styles, appearances);

    if (parsed.plainText.empty()) {
        return rect;
    }

    // パラグラフレイアウト（行分割のみ目的。styleRuns を使ってフォント選択も正確に）
    ParagraphLayout para;
    if (!parsed.styleRuns.empty()) {
        para.layout(parsed.plainText, rect.width, parsed.styleRuns);
    } else {
        para.layout(parsed.plainText, rect.width, defaultStyle);
    }

    RectF totalBounds;
    bool first = true;

    for (size_t lineIdx = 0; lineIdx < para.getLineCount(); ++lineIdx) {
        const ParagraphLayout::LineInfo& line = para.getLine(lineIdx);

        // 行の垂直位置を計算
        auto pos = para.getLinePosition(lineIdx, rect.x, rect.y,
                                        rect.width, rect.height,
                                        ParagraphLayout::HAlign::Left,  // X は後で調整
                                        vAlign);
        float baseY = pos.y;

        // この行に重なるスパンを収集して左から描画
        // 各スパン部分ごとに TextLayout を作り幅を計算する
        struct SpanSegment {
            size_t spanIdx;
            size_t segStart;  // plainText 内の開始
            size_t segEnd;
        };
        std::vector<SpanSegment> segments;

        for (size_t si = 0; si < parsed.spans.size(); ++si) {
            const auto& span = parsed.spans[si];
            size_t overlapStart = std::max(span.start, line.startIndex);
            size_t overlapEnd   = std::min(span.end,   line.endIndex);
            if (overlapStart >= overlapEnd) continue;
            segments.push_back({si, overlapStart, overlapEnd});
        }

        // スパンが全く無い行（空行など）はそのままスキップ
        if (segments.empty()) {
            continue;
        }

        // 各セグメントの幅を計算して合計幅を求める（Center/Right アライン用）
        // MeasuredText の文字幅を使い、行分割時の計算と整合させる
        struct SegmentLayout {
            size_t spanIdx;
            size_t segStart;  // plainText 内の開始位置
            size_t segEnd;    // plainText 内の終了位置
            float yOffset;
            float measuredWidth;  // MeasuredText ベースの幅
            TextLayout layout;
        };
        std::vector<SegmentLayout> segLayouts;
        float totalWidth = 0.0f;

        for (const auto& seg : segments) {
            const auto& span = parsed.spans[seg.spanIdx];
            std::u16string segText = parsed.plainText.substr(seg.segStart, seg.segEnd - seg.segStart);

            SegmentLayout sl;
            sl.spanIdx = seg.spanIdx;
            sl.segStart = seg.segStart;
            sl.segEnd = seg.segEnd;
            sl.yOffset = span.yOffset;
            sl.layout.layout(std::move(segText), span.style);
            // 行分割と整合する幅を ParagraphLayout から取得
            sl.measuredWidth = para.getRunWidth(seg.segStart, seg.segEnd);
            totalWidth += sl.measuredWidth;
            segLayouts.push_back(std::move(sl));
        }

        // 水平アライン開始X を決定
        float startX = rect.x;
        switch (hAlign) {
        case ParagraphLayout::HAlign::Left:
            startX = rect.x;
            break;
        case ParagraphLayout::HAlign::Center:
            startX = rect.x + (rect.width - totalWidth) / 2.0f;
            break;
        case ParagraphLayout::HAlign::Right:
            startX = rect.x + rect.width - totalWidth;
            break;
        case ParagraphLayout::HAlign::Justify:
            startX = rect.x;
            break;
        }

        // 各セグメントを順番に描画
        float curX = startX;
        for (const auto& sl : segLayouts) {
            const auto& span = parsed.spans[sl.spanIdx];
            float drawY = baseY + sl.yOffset;

            RectF segBounds = drawLayout(sl.layout, curX, drawY, span.appearance);
            // MeasuredText ベースの幅で位置を進める（行分割と整合）
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
// 計測メソッド
//------------------------------------------------------------------------------

TextLayout TextRenderer::measureText(const std::u16string& text,
                                     const TextStyle& style) {
    TextLayout layout;
    layout.layout(text, style);
    return layout;
}

ParagraphLayout TextRenderer::measureParagraph(const std::u16string& text,
                                               float maxWidth,
                                               const TextStyle& style) {
    ParagraphLayout para;
    para.layout(text, maxWidth, style);
    return para;
}

ParagraphLayout TextRenderer::measureParagraph(const std::u16string& text,
                                               float maxWidth,
                                               const std::vector<ParagraphLayout::StyleRun>& styleRuns) {
    ParagraphLayout para;
    para.layout(text, maxWidth, styleRuns);
    return para;
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

} // namespace richtext
