/**
 * ParagraphLayout.cpp
 * 
 * 複数行テキスト（パラグラフ）のレイアウト処理
 */

#include "richtext/ParagraphLayout.hpp"
#include "richtext/FontManager.hpp"

#include <minikin/Range.h>
#include <minikin/MinikinPaint.h>
#include <minikin/MeasuredText.h>

namespace richtext {

//------------------------------------------------------------------------------
// RectangleLineWidth - 矩形（全行同一幅）
//------------------------------------------------------------------------------

class RectangleLineWidth : public minikin::LineWidth {
public:
    RectangleLineWidth(float width) : width_(width) {}
    float getAt(size_t) const override { return width_; }
    float getMin() const override { return width_; }
private:
    float width_;
};

//------------------------------------------------------------------------------
// ParagraphLayout
//------------------------------------------------------------------------------

ParagraphLayout::ParagraphLayout() = default;

minikin::BreakStrategy ParagraphLayout::toMinikinStrategy() const {
    switch (breakStrategy_) {
    case BreakStrategy::Greedy:
        return minikin::BreakStrategy::Greedy;
    case BreakStrategy::HighQuality:
        return minikin::BreakStrategy::HighQuality;
    case BreakStrategy::Balanced:
        return minikin::BreakStrategy::Balanced;
    default:
        return minikin::BreakStrategy::HighQuality;
    }
}

void ParagraphLayout::layout(const std::u16string& text,
                             float maxWidth,
                             const TextStyle& style) {
    // 単一スタイルを StyleRun に変換
    std::vector<StyleRun> runs;
    StyleRun run;
    run.start = 0;
    run.end = text.size();
    run.style = style;
    runs.push_back(run);
    
    layout(text, maxWidth, runs);
}

void ParagraphLayout::layout(const std::u16string& text,
                             float maxWidth,
                             const std::vector<StyleRun>& styleRuns) {
    text_ = text;
    styleRuns_ = styleRuns;
    maxWidth_ = maxWidth;
    lines_.clear();
    totalHeight_ = 0;
    
    if (text.empty() || styleRuns.empty()) {
        return;
    }
    
    // U16StringPiece は uint16_t* を要求するので reinterpret_cast
    const uint16_t* textData = reinterpret_cast<const uint16_t*>(text_.data());
    uint32_t textLen = static_cast<uint32_t>(text_.size());
    
    // MeasuredText を構築
    minikin::MeasuredTextBuilder builder;
    
    for (const auto& run : styleRuns_) {
        // フォントコレクションを確認
        if (!run.style.fontCollection) {
            continue;
        }
        
        // MinikinPaint 設定
        minikin::MinikinPaint paint(run.style.fontCollection);
        run.style.applyTo(paint);
        
        // スタイルラン追加
        builder.addStyleRun(
            static_cast<uint32_t>(run.start),
            static_cast<uint32_t>(run.end),
            std::move(paint),
            minikin::isRtl(run.style.bidi)
        );
    }
    
    const bool computeHyphenation = false;
    const bool computeLayout = false;
    minikin::MeasuredText* hint = nullptr;
    
    measuredText_ = builder.build(
        minikin::U16StringPiece(textData, textLen),
        computeHyphenation,
        computeLayout,
        hint
    );
    
    if (!measuredText_) {
        return;
    }
    
    // 行分割実行
    RectangleLineWidth lineWidth(maxWidth);
    minikin::TabStops tabStops(nullptr, 0, 0);
    const bool justified = (breakStrategy_ == BreakStrategy::Balanced);
    
    breakResult_ = minikin::breakIntoLines(
        minikin::U16StringPiece(textData, textLen),
        toMinikinStrategy(),
        minikin::HyphenationFrequency::None,
        justified,
        *measuredText_,
        lineWidth,
        tabStops
    );
    
    // 行情報を構築
    size_t lastBreakPoint = 0;
    for (size_t i = 0; i < breakResult_.breakPoints.size(); ++i) {
        LineInfo line;
        line.startIndex = lastBreakPoint;
        line.endIndex = breakResult_.breakPoints[i];
        line.width = breakResult_.widths[i];
        line.ascent = breakResult_.ascents[i];
        line.descent = breakResult_.descents[i];
        
        lines_.push_back(line);
        
        totalHeight_ += line.height();
        if (i > 0) {
            totalHeight_ += lineSpacing_;
        }
        
        lastBreakPoint = breakResult_.breakPoints[i];
    }
}

TextLayout ParagraphLayout::getLineLayout(size_t lineIndex, const TextStyle& style) const {
    TextLayout layout;
    
    if (lineIndex >= lines_.size()) {
        return layout;
    }
    
    const LineInfo& line = lines_[lineIndex];
    std::u16string lineText = text_.substr(line.startIndex, line.endIndex - line.startIndex);
    
    layout.layout(std::move(lineText), style);
    
    return layout;
}

float ParagraphLayout::getRunWidth(size_t start, size_t end) const {
    if (!measuredText_ || start >= end) {
        return 0.0f;
    }
    if (end > measuredText_->widths.size()) {
        end = measuredText_->widths.size();
    }
    float width = 0.0f;
    for (size_t i = start; i < end; ++i) {
        width += measuredText_->widths[i];
    }
    return width;
}

ParagraphLayout::RenderPosition ParagraphLayout::getLinePosition(
    size_t lineIndex,
    float rectX, float rectY,
    float rectWidth, float rectHeight,
    HAlign hAlign,
    VAlign vAlign) const {
    
    RenderPosition pos = { rectX, rectY };
    
    if (lineIndex >= lines_.size()) {
        return pos;
    }
    
    const LineInfo& line = lines_[lineIndex];
    
    // 水平アライン
    switch (hAlign) {
    case HAlign::Left:
        pos.x = rectX;
        break;
    case HAlign::Center:
        pos.x = rectX + (rectWidth - line.width) / 2.0f;
        break;
    case HAlign::Right:
        pos.x = rectX + rectWidth - line.width;
        break;
    case HAlign::Justify:
        pos.x = rectX;  // 両端揃えは行幅を調整（別途処理）
        break;
    }
    
    // 垂直位置の計算（lineIndex までの累積高さ）
    float yOffset = 0;
    for (size_t i = 0; i <= lineIndex; ++i) {
        if (i > 0) {
            yOffset += lineSpacing_;
        }
        yOffset += -lines_[i].ascent;  // ascent は負の値
        if (i < lineIndex) {
            yOffset += lines_[i].descent;
        }
    }
    
    // 垂直アライン
    switch (vAlign) {
    case VAlign::Top:
        pos.y = rectY + yOffset;
        break;
    case VAlign::Middle:
        pos.y = rectY + (rectHeight - totalHeight_) / 2.0f + yOffset;
        break;
    case VAlign::Bottom:
        pos.y = rectY + rectHeight - totalHeight_ + yOffset;
        break;
    }
    
    return pos;
}

} // namespace richtext
