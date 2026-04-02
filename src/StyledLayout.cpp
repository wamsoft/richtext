/**
 * StyledLayout.cpp
 *
 * スタイルタグ付きテキストのレイアウト処理
 */

#include "richtext/StyledLayout.hpp"

#include <algorithm>
#include <set>

namespace richtext {

void StyledLayout::layout(const std::u16string& text,
                          float maxWidth, float maxHeight,
                          ParagraphLayout::HAlign hAlign,
                          ParagraphLayout::VAlign vAlign,
                          const std::map<std::string, TextStyle>& styles,
                          const std::map<std::string, Appearance>& appearances,
                          float lineSpacing) {
    valid_ = false;
    lineLayouts_.clear();
    totalGlyphCount_ = 0;
    totalCharCount_ = 0;

    hAlign_ = hAlign;
    vAlign_ = vAlign;
    maxWidth_ = maxWidth;
    maxHeight_ = maxHeight;

    if (text.empty()) {
        return;
    }

    // デフォルトのスタイル・外観を取得
    TextStyle defaultStyle;
    auto sit = styles.find("default");
    if (sit != styles.end()) defaultStyle = sit->second;

    Appearance defaultAppearance;
    auto ait = appearances.find("default");
    if (ait != appearances.end()) defaultAppearance = ait->second;
    if (defaultAppearance.isEmpty()) defaultAppearance = Appearance::defaultAppearance();

    // タグ付きテキストを解析
    TagParser parser;
    parser.setOptions(parserOptions_);
    if (evalCallback_) parser.setEvalCallback(evalCallback_);
    parsed_ = parser.parse(text, defaultStyle, defaultAppearance, styles, appearances);

    if (parsed_.plainText.empty()) {
        return;
    }

    // パラグラフレイアウト（行分割）
    if (lineSpacing > 0.0f) {
        para_.setLineSpacing(lineSpacing);
    }
    if (!parsed_.styleRuns.empty()) {
        para_.layout(parsed_.plainText, maxWidth, parsed_.styleRuns);
    } else {
        para_.layout(parsed_.plainText, maxWidth, defaultStyle);
    }

    // 各行のセグメントレイアウトを構築
    std::set<size_t> allCharIndices;

    for (size_t lineIdx = 0; lineIdx < para_.getLineCount(); ++lineIdx) {
        const ParagraphLayout::LineInfo& line = para_.getLine(lineIdx);

        // この行に重なるスパンを収集
        struct SpanSegment {
            size_t spanIdx;
            size_t segStart;
            size_t segEnd;
        };
        std::vector<SpanSegment> segments;

        for (size_t si = 0; si < parsed_.spans.size(); ++si) {
            const auto& span = parsed_.spans[si];
            size_t overlapStart = std::max(span.start, line.startIndex);
            size_t overlapEnd   = std::min(span.end,   line.endIndex);
            if (overlapStart >= overlapEnd) continue;
            segments.push_back({si, overlapStart, overlapEnd});
        }

        LineLayout ll;
        ll.lineIdx = lineIdx;
        ll.totalWidth = 0.0f;

        for (const auto& seg : segments) {
            const auto& span = parsed_.spans[seg.spanIdx];
            std::u16string segText = parsed_.plainText.substr(
                seg.segStart, seg.segEnd - seg.segStart);

            SegmentLayout sl;
            sl.spanIdx = seg.spanIdx;
            sl.segStart = seg.segStart;
            sl.segEnd = seg.segEnd;
            sl.yOffset = span.yOffset;
            sl.layout.layout(std::move(segText), span.style);
            sl.measuredWidth = para_.getRunWidth(seg.segStart, seg.segEnd);
            ll.totalWidth += sl.measuredWidth;

            // グリフ数・文字数の集計
            const auto& glyphs = sl.layout.getGlyphs();
            totalGlyphCount_ += glyphs.size();
            for (const auto& g : glyphs) {
                allCharIndices.insert(seg.segStart + g.charIndex);
            }

            ll.segments.push_back(std::move(sl));
        }

        lineLayouts_.push_back(std::move(ll));
    }

    totalCharCount_ = allCharIndices.size();
    valid_ = true;
}

//------------------------------------------------------------------------------
// リンク矩形領域の構築
//------------------------------------------------------------------------------

std::vector<LinkRegion> StyledLayout::buildLinkRegions() const {
    std::vector<LinkRegion> regions;
    if (!valid_ || parsed_.links.empty()) return regions;

    // charIndex → (x, y, width, height) のルックアップテーブルを構築
    struct CharPos {
        float x, y, w, h;
        size_t lineIdx;
    };
    std::map<size_t, CharPos> charPosMap;

    for (size_t li = 0; li < lineLayouts_.size(); li++) {
        const auto& line = lineLayouts_[li];
        // 行のベースライン Y を取得
        float lineY = 0;
        float lineH = 0;
        if (li < para_.getLineCount()) {
            const auto& lineInfo = para_.getLine(li);
            lineH = lineInfo.height();
        }

        for (const auto& seg : line.segments) {
            const auto& glyphs = seg.layout.getGlyphs();
            for (const auto& g : glyphs) {
                size_t charIdx = seg.segStart + g.charIndex;
                CharPos cp;
                cp.x = g.x;
                cp.y = g.y + seg.yOffset;
                cp.w = g.advance;
                cp.h = lineH > 0 ? lineH : parsed_.spans.empty() ? 16.0f : parsed_.spans[0].style.fontSize;
                cp.lineIdx = li;
                charPosMap[charIdx] = cp;
            }
        }
    }

    // 各リンクについて矩形を構築
    for (const auto& link : parsed_.links) {
        LinkRegion region;
        region.name = link.name;

        size_t lastLine = static_cast<size_t>(-1);
        for (size_t ci = link.startIndex; ci < link.endIndex; ci++) {
            auto it = charPosMap.find(ci);
            if (it == charPosMap.end()) continue;

            const auto& cp = it->second;
            region.charIndices.push_back(static_cast<int>(ci));

            float l = cp.x;
            float t = cp.y;
            float r = l + cp.w;
            float b = t + cp.h;

            if (region.rects.empty() || cp.lineIdx != lastLine) {
                region.rects.push_back({l, t, r, b});
                lastLine = cp.lineIdx;
            } else {
                auto& rect = region.rects.back();
                if (l < rect.left) rect.left = l;
                if (t < rect.top) rect.top = t;
                if (r > rect.right) rect.right = r;
                if (b > rect.bottom) rect.bottom = b;
            }
        }

        regions.push_back(std::move(region));
    }

    return regions;
}

} // namespace richtext
