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

#include <algorithm>
#include <vector>

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
    glyphCountCached_ = false;

    if (text.empty() || styleRuns.empty()) {
        return;
    }

    // テキスト全体の MeasuredText を構築（getRunWidth 用）
    const uint16_t* textData = reinterpret_cast<const uint16_t*>(text_.data());
    uint32_t textLen = static_cast<uint32_t>(text_.size());

    minikin::MeasuredTextBuilder fullBuilder;
    for (const auto& run : styleRuns_) {
        if (!run.style.fontCollection) continue;
        minikin::MinikinPaint paint(run.style.fontCollection);
        run.style.applyTo(paint);
        fullBuilder.addStyleRun(
            static_cast<uint32_t>(run.start),
            static_cast<uint32_t>(run.end),
            std::move(paint),
            minikin::isRtl(run.style.bidi)
        );
    }

    const bool computeHyphenation = false;
    const bool computeLayout = false;
    minikin::MeasuredText* hint = nullptr;

    measuredText_ = fullBuilder.build(
        minikin::U16StringPiece(textData, textLen),
        computeHyphenation, computeLayout, hint
    );

    if (!measuredText_) {
        return;
    }

    // テキストを改行文字(\n)で分割し、各段落を個別にレイアウトする
    // minikin の breakIntoLines は \n を強制改行として扱わないため
    std::vector<std::pair<size_t, size_t>> paragraphs;
    {
        size_t start = 0;
        for (size_t i = 0; i <= text_.size(); ++i) {
            if (i == text_.size() || text_[i] == u'\n') {
                paragraphs.push_back({start, i});
                start = i + 1;
            }
        }
    }

    for (const auto& para : paragraphs) {
        size_t paraStart = para.first;
        size_t paraEnd = para.second;

        if (paraStart > text_.size()) break;

        // 空段落（連続改行や末尾改行）は空行として追加
        if (paraStart >= paraEnd) {
            LineInfo emptyLine;
            emptyLine.startIndex = paraStart;
            emptyLine.endIndex = paraStart;
            emptyLine.width = 0;
            if (!styleRuns_.empty()) {
                emptyLine.ascent = -(styleRuns_[0].style.fontSize * 0.8f);
                emptyLine.descent = styleRuns_[0].style.fontSize * 0.2f;
            } else {
                emptyLine.ascent = -16.0f;
                emptyLine.descent = 4.0f;
            }
            if (!lines_.empty()) {
                totalHeight_ += lineSpacing_;
            }
            lines_.push_back(emptyLine);
            totalHeight_ += emptyLine.height();
            continue;
        }

        // 段落用の MeasuredText を構築
        std::u16string paraText = text_.substr(paraStart, paraEnd - paraStart);
        const uint16_t* paraData = reinterpret_cast<const uint16_t*>(paraText.data());
        uint32_t paraLen = static_cast<uint32_t>(paraText.size());

        minikin::MeasuredTextBuilder paraBuilder;
        for (const auto& run : styleRuns_) {
            if (!run.style.fontCollection) continue;
            size_t overlapStart = std::max(run.start, paraStart);
            size_t overlapEnd = std::min(run.end, paraEnd);
            if (overlapStart >= overlapEnd) continue;

            minikin::MinikinPaint paint(run.style.fontCollection);
            run.style.applyTo(paint);
            paraBuilder.addStyleRun(
                static_cast<uint32_t>(overlapStart - paraStart),
                static_cast<uint32_t>(overlapEnd - paraStart),
                std::move(paint),
                minikin::isRtl(run.style.bidi)
            );
        }

        auto paraMeasured = paraBuilder.build(
            minikin::U16StringPiece(paraData, paraLen),
            computeHyphenation, computeLayout, hint
        );
        if (!paraMeasured) continue;

        RectangleLineWidth lineWidth(maxWidth);
        minikin::TabStops tabStops(nullptr, 0, 0);
        const bool justified = (breakStrategy_ == BreakStrategy::Balanced);

        auto breakResult = minikin::breakIntoLines(
            minikin::U16StringPiece(paraData, paraLen),
            toMinikinStrategy(),
            minikin::HyphenationFrequency::None,
            justified,
            *paraMeasured,
            lineWidth,
            tabStops
        );

        // 行情報を構築（インデックスを元テキストのオフセットに変換）
        size_t lastBreak = 0;
        for (size_t i = 0; i < breakResult.breakPoints.size(); ++i) {
            LineInfo line;
            line.startIndex = paraStart + lastBreak;
            line.endIndex = paraStart + breakResult.breakPoints[i];
            line.width = breakResult.widths[i];
            line.ascent = breakResult.ascents[i];
            line.descent = breakResult.descents[i];

            if (!lines_.empty()) {
                totalHeight_ += lineSpacing_;
            }
            lines_.push_back(line);
            totalHeight_ += line.height();

            lastBreak = breakResult.breakPoints[i];
        }
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

size_t ParagraphLayout::getTotalGlyphCount() const {
    if (glyphCountCached_) return cachedTotalGlyphCount_;

    size_t total = 0;
    TextStyle defaultStyle;
    if (!styleRuns_.empty()) defaultStyle = styleRuns_[0].style;

    for (size_t i = 0; i < lines_.size(); ++i) {
        TextLayout lineLayout = getLineLayout(i, defaultStyle);
        // 論理文字数（ユニーク charIndex 数）でカウント
        const auto& glyphs = lineLayout.getGlyphs();
        std::vector<size_t> chars;
        chars.reserve(glyphs.size());
        for (const auto& g : glyphs) chars.push_back(g.charIndex);
        std::sort(chars.begin(), chars.end());
        chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
        total += chars.size();
    }

    cachedTotalGlyphCount_ = total;
    glyphCountCached_ = true;
    return total;
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
