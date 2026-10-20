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
    totalCharCount_ = 0;
    resolvedTimings_.clear();
    resolvedKeyWaits_.clear();
    totalRenderDelay_ = 0;
    timingsResolved_ = false;
    placements_.clear();
    placementStyles_.clear();
    bounds_ = Bounds();

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
    defaultAppearance_ = defaultAppearance;

    // タグ付きテキストを解析
    TagParser parser;
    parser.setOptions(parserOptions_);
    if (evalCallback_) parser.setEvalCallback(evalCallback_);
    if (labelResolver_) parser.setLabelResolver(labelResolver_);
    parsed_ = parser.parse(text, defaultStyle, defaultAppearance, styles, appearances);

    if (parsed_.plainText.empty()) {
        return;
    }

    // 縦組み
    if (isVertical()) {
        layoutVertical(defaultStyle, lineSpacing);
        valid_ = true;
        return;
    }

    // パラグラフレイアウト（行分割）
    // 0 も設定する (使い回したときに前回の行間が残らないように)
    para_.setLineSpacing(lineSpacing);
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

            // 文字数の集計
            const auto& glyphs = sl.layout.getGlyphs();
            for (const auto& g : glyphs) {
                allCharIndices.insert(seg.segStart + g.charIndex);
            }

            ll.segments.push_back(std::move(sl));
        }

        lineLayouts_.push_back(std::move(ll));
    }

    totalCharCount_ = allCharIndices.size();
    buildHorizontalPlacements();
    valid_ = true;
}

//------------------------------------------------------------------------------
// 文字ごとの配置
//------------------------------------------------------------------------------

namespace {

struct BoundsAccumulator {
    bool valid = false;
    float l = 0, t = 0, r = 0, b = 0;
    void add(float left, float top, float right, float bottom) {
        if (right < left) std::swap(left, right);
        if (bottom < top) std::swap(top, bottom);
        if (!valid) {
            l = left; t = top; r = right; b = bottom;
            valid = true;
        } else {
            l = std::min(l, left); t = std::min(t, top);
            r = std::max(r, right); b = std::max(b, bottom);
        }
    }
};

} // namespace

size_t StyledLayout::findSpan(size_t charIndex) const {
    // ギャップ補填後の spans は重ならず昇順
    size_t lo = 0, hi = parsed_.spans.size();
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        const auto& sp = parsed_.spans[mid];
        if (charIndex < sp.start) hi = mid;
        else if (charIndex >= sp.end) lo = mid + 1;
        else return mid;
    }
    return SIZE_MAX;
}

void StyledLayout::buildHorizontalPlacements() {
    BoundsAccumulator acc;

    // 文字ごとの位置（ルビを置くため）
    struct CharPos {
        bool valid = false;
        float left = 0, right = 0;
        float baseY = 0;
        float ascent = 0;       // TextLayout::getAscent()（負値）
        size_t lineIdx = 0;
        size_t spanIdx = SIZE_MAX;
    };
    std::vector<CharPos> charPos(parsed_.plainText.size());

    for (const auto& ll : lineLayouts_) {
        auto pos = para_.getLinePosition(ll.lineIdx, 0.0f, 0.0f,
                                         maxWidth_, maxHeight_,
                                         ParagraphLayout::HAlign::Left, vAlign_);
        float startX = 0.0f;
        switch (hAlign_) {
        case ParagraphLayout::HAlign::Center: startX = (maxWidth_ - ll.totalWidth) / 2.0f; break;
        case ParagraphLayout::HAlign::Right:  startX = maxWidth_ - ll.totalWidth; break;
        default: break;
        }

        float curX = startX;
        for (const auto& sl : ll.segments) {
            float drawY = pos.y + sl.yOffset;
            size_t styleIdx = placementStyles_.size();
            placementStyles_.push_back(sl.layout.getStyle());

            for (const auto& g : sl.layout.getGlyphs()) {
                GlyphPlacement p;
                p.glyph = g;
                p.glyph.x = curX + g.x;
                p.glyph.y = drawY + g.y;
                p.charIndex = sl.segStart + g.charIndex;
                p.glyph.charIndex = p.charIndex;
                p.styleIndex = styleIdx;
                p.spanIdx = sl.spanIdx;
                p.kind = GlyphPlacement::Kind::Base;
                placements_.push_back(p);

                if (p.charIndex < charPos.size()) {
                    CharPos& cp = charPos[p.charIndex];
                    float l = curX + g.x, r = l + g.advance;
                    if (!cp.valid) {
                        cp.valid = true;
                        cp.left = l; cp.right = r;
                        cp.baseY = drawY;
                        cp.ascent = sl.layout.getAscent();
                        cp.lineIdx = ll.lineIdx;
                        cp.spanIdx = sl.spanIdx;
                    } else {
                        cp.left = std::min(cp.left, l);
                        cp.right = std::max(cp.right, r);
                    }
                }
            }

            auto b = sl.layout.getBounds();
            acc.add(curX + b.left, drawY - b.top, curX + b.right, drawY - b.bottom);
            curX += sl.measuredWidth;
        }
    }

    // ルビ: <ruby> 区間ごとに 1 回だけ置く。区間が行をまたぐときは、
    // 区間の先頭の文字がある行の部分の中央に置く
    for (const auto& rb : parsed_.rubies) {
        size_t first = SIZE_MAX;
        for (size_t ci = rb.start; ci < rb.end && ci < charPos.size(); ++ci) {
            if (charPos[ci].valid) { first = ci; break; }
        }
        if (first == SIZE_MAX || rb.text.empty()) continue;

        const CharPos& fp = charPos[first];
        float left = fp.left, right = fp.right, ascent = fp.ascent;
        for (size_t ci = first + 1; ci < rb.end && ci < charPos.size(); ++ci) {
            const CharPos& cp = charPos[ci];
            if (!cp.valid || cp.lineIdx != fp.lineIdx) continue;
            left = std::min(left, cp.left);
            right = std::max(right, cp.right);
            ascent = std::min(ascent, cp.ascent);
        }

        TextStyle rubyStyle = fp.spanIdx < parsed_.spans.size()
            ? parsed_.spans[fp.spanIdx].style : placementStyles_.front();
        rubyStyle.fontSize *= parserOptions_.rubyScale;
        TextLayout rubyLayout;
        rubyLayout.layout(rb.text, rubyStyle);

        float rubyX = left + (right - left - rubyLayout.getWidth()) / 2.0f;
        float rubyY = fp.baseY + ascent + rubyLayout.getDescent();
        size_t styleIdx = placementStyles_.size();
        placementStyles_.push_back(rubyStyle);

        for (const auto& g : rubyLayout.getGlyphs()) {
            GlyphPlacement p;
            p.glyph = g;
            p.glyph.x = rubyX + g.x;
            p.glyph.y = rubyY + g.y;
            p.charIndex = first;    // 親文字の先頭と一緒に出る
            p.glyph.charIndex = first;
            p.styleIndex = styleIdx;
            p.spanIdx = fp.spanIdx;
            p.kind = GlyphPlacement::Kind::Ruby;
            placements_.push_back(p);
        }
        auto b = rubyLayout.getBounds();
        acc.add(rubyX + b.left, rubyY - b.top, rubyX + b.right, rubyY - b.bottom);
    }

    if (acc.valid) bounds_ = { acc.l, acc.t, acc.r - acc.l, acc.b - acc.t };
}

void StyledLayout::layoutVertical(const TextStyle& defaultStyle, float lineSpacing) {
    // ルビ・縦中横を注記にする
    std::vector<vertical::InlineAnnotation> annotations;
    for (const auto& rb : parsed_.rubies) {
        if (rb.text.empty() || rb.end <= rb.start) continue;
        annotations.push_back(vertical::InlineAnnotation::ruby(
            rb.start, rb.end, rb.text, vertical::RubyMode::Group, parserOptions_.rubyScale));
    }
    for (const auto& r : parsed_.tateChuYoko) {
        if (r.end <= r.start) continue;
        annotations.push_back(vertical::InlineAnnotation::tateChuYoko(r.start, r.end));
    }
    std::sort(annotations.begin(), annotations.end(),
              [](const vertical::InlineAnnotation& a, const vertical::InlineAnnotation& b) {
                  return a.start < b.start;
              });

    vertical::VerticalLayoutOptions opts;
    opts.writingMode = (writingMode_ == WritingMode::VerticalLr)
        ? vertical::WritingMode::VerticalLr : vertical::WritingMode::VerticalRl;
    opts.lineGap = lineSpacing;
    opts.lineBreak.strategy = (para_.getBreakStrategy() == ParagraphLayout::BreakStrategy::Greedy)
        ? vertical::LineBreakStrategy::Greedy : vertical::LineBreakStrategy::KnuthPlass;

    const float lineLength = maxHeight_ > 0.0f ? maxHeight_ : 100000.0f;
    vpara_.layout(parsed_.plainText, annotations, defaultStyle, lineLength, opts);

    placementStyles_.push_back(vpara_.getStyle());

    // 1 列目の縦ベースライン: vertical-rl は枠の右端から、vertical-lr は左端から
    const float em = defaultStyle.fontSize;
    const size_t n = vpara_.getLineCount();
    float blockWidth = maxWidth_ > 0.0f
        ? maxWidth_
        : (n > 0 ? vpara_.getLineAdvance() * static_cast<float>(n) - lineSpacing : em);
    const float originX = (opts.writingMode == vertical::WritingMode::VerticalRl)
        ? blockWidth - em / 2.0f : em / 2.0f;

    BoundsAccumulator acc;
    std::set<size_t> chars;
    for (size_t i = 0; i < n; ++i) {
        const auto& line = vpara_.getLine(i);
        const auto pos = vpara_.getLinePosition(i, originX, 0.0f);

        // 列の中の揃え（hAlign: Left=行頭 / Center / Right=行末）
        float off = 0.0f;
        if (maxHeight_ > 0.0f) {
            switch (hAlign_) {
            case ParagraphLayout::HAlign::Center: off = (lineLength - line.length) / 2.0f; break;
            case ParagraphLayout::HAlign::Right:  off = lineLength - line.length; break;
            default: break;
            }
        }

        for (const auto& g : line.glyphs) {
            GlyphPlacement p;
            p.glyph = g;
            p.glyph.x = pos.x + g.x;
            p.glyph.y = pos.y + off + g.y;
            p.charIndex = g.charIndex;
            p.styleIndex = 0;
            p.spanIdx = findSpan(g.charIndex);
            p.kind = (g.fontSize > 0.0f) ? GlyphPlacement::Kind::Annotation : GlyphPlacement::Kind::Base;
            if (p.kind == GlyphPlacement::Kind::Base) chars.insert(g.charIndex);
            placements_.push_back(p);
        }
        acc.add(pos.x + line.extentLeft, pos.y + off, pos.x + line.extentRight, pos.y + off + line.length);
    }

    totalCharCount_ = chars.size();
    if (acc.valid) bounds_ = { acc.l, acc.t, acc.r - acc.l, acc.b - acc.t };
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

//------------------------------------------------------------------------------
// タイミング解決
//------------------------------------------------------------------------------

void StyledLayout::resolveAllTimings(float timeScale,
                                     bool widthTimeScale) {
    resolvedTimings_.clear();
    resolvedKeyWaits_.clear();
    totalRenderDelay_ = 0;
    timingsResolved_ = true;

    if (!valid_) return;

    // 文字幅の配列を構築（widthTimeScale 用）
    std::vector<float> charWidths;
    const std::vector<float>* charWidthsPtr = nullptr;
    if (widthTimeScale) {
        for (const auto& line : lineLayouts_) {
            for (const auto& seg : line.segments) {
                const auto& glyphs = seg.layout.getGlyphs();
                for (const auto& g : glyphs) {
                    charWidths.push_back(g.advance);
                }
            }
        }
        charWidthsPtr = &charWidths;
    }

    // タイミング情報を解決
    resolvedTimings_ = resolveTimings(
        parsed_.timings, timeScale,
        charWidthsPtr, &resolvedKeyWaits_);

    // 最大遅延時間を計算
    for (const auto& rt : resolvedTimings_) {
        if (rt.delay > totalRenderDelay_) totalRenderDelay_ = rt.delay;
    }
    for (const auto& kw : resolvedKeyWaits_) {
        if (kw.delay > totalRenderDelay_) totalRenderDelay_ = kw.delay;
    }
}

void StyledLayout::ensureTimingsResolved() const {
    if (!timingsResolved_) {
        const_cast<StyledLayout*>(this)->resolveAllTimings();
    }
}

const std::vector<ResolvedTiming>& StyledLayout::getResolvedTimings() const {
    ensureTimingsResolved();
    return resolvedTimings_;
}

const std::vector<KeyWaitInfo>& StyledLayout::getKeyWaits() const {
    ensureTimingsResolved();
    return resolvedKeyWaits_;
}

float StyledLayout::getTotalRenderDelay() const {
    ensureTimingsResolved();
    return totalRenderDelay_;
}

int StyledLayout::calcShowCount(float time) const {
    ensureTimingsResolved();
    int count = 0;
    for (const auto& rt : resolvedTimings_) {
        if (rt.delay <= time) count++;
    }
    return count;
}

} // namespace richtext
