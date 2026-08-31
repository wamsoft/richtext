/**
 * VerticalParagraphLayout.cpp
 *
 * 縦組みの段落レイアウト
 */

#include "richtext/vertical/VerticalParagraphLayout.hpp"

#include "richtext/vertical/VerticalShaper.hpp"

#include <algorithm>

namespace richtext::vertical {

void VerticalParagraphLayout::layout(const std::u16string& text, const TextStyle& style,
                                     float lineLength, const VerticalLayoutOptions& opts) {
    layout(text, {}, style, lineLength, opts);
}

void VerticalParagraphLayout::layout(const std::u16string& text,
                                     const std::vector<InlineAnnotation>& annotations,
                                     const TextStyle& style,
                                     float lineLength, const VerticalLayoutOptions& opts) {
    text_ = text;
    annotations_ = annotations;
    style_ = style;
    options_ = opts;
    lineLength_ = lineLength;
    lineAdvance_ = style.fontSize + opts.lineGap;
    lines_.clear();
    maxLineLength_ = 0.0f;

    if (text_.empty() || !style_.fontCollection || lineLength <= 0.0f) {
        return;
    }

    // 改行で段落へ分ける（シェイパーは改行文字を扱わない）
    size_t pos = 0;
    while (pos <= text_.size()) {
        size_t nl = text_.find(u'\n', pos);
        size_t end = (nl == std::u16string::npos) ? text_.size() : nl;
        size_t trimmed = end;
        if (trimmed > pos && text_[trimmed - 1] == u'\r') {
            --trimmed;
        }
        layoutParagraph(text_.substr(pos, trimmed - pos), pos);
        if (nl == std::u16string::npos) break;
        pos = nl + 1;
    }

    for (const Line& line : lines_) {
        maxLineLength_ = std::max(maxLineLength_, line.length);
    }
}

void VerticalParagraphLayout::layoutParagraph(const std::u16string& paragraph,
                                              size_t charOffset) {
    if (paragraph.empty()) {
        // 空行もそのまま 1 列分を占める
        Line line;
        line.charStart = charOffset;
        line.charEnd = charOffset;
        line.extentLeft = -style_.fontSize * 0.5f;
        line.extentRight = style_.fontSize * 0.5f;
        lines_.push_back(line);
        return;
    }

    const VerticalShaper::Result shaped =
            VerticalShaper::shape(paragraph, style_, options_.orientation);
    if (shaped.clusters.empty()) {
        return;
    }

    VerticalSpacingOptions spacing = options_.spacing;
    // 字間はスタイル側の指定を既定にする（明示指定があればそちらを優先）
    if (spacing.letterSpacing == 0.0f) {
        spacing.letterSpacing = style_.letterSpacing;
    }

    // この段落に掛かる注記だけを、段落内の位置へずらして渡す
    std::vector<InlineAnnotation> paraAnnotations;
    if (!annotations_.empty()) {
        const size_t paraEnd = charOffset + paragraph.size();
        for (const InlineAnnotation& ann : annotations_) {
            if (ann.end <= charOffset || ann.start >= paraEnd) continue;
            InlineAnnotation shifted = ann;
            shifted.start = std::max(ann.start, charOffset) - charOffset;
            shifted.end = std::min(ann.end, paraEnd) - charOffset;
            paraAnnotations.push_back(std::move(shifted));
        }
    }

    const std::vector<LineItem> items =
            buildLineItems(shaped, style_, paraAnnotations, spacing);
    const std::vector<BreakLine> breaks =
            LineBreaker::breakLines(items, lineLength_, options_.lineBreak);

    const float halfEm = style_.fontSize * 0.5f;

    for (const BreakLine& br : breaks) {
        Line line;
        line.length = br.width;
        line.naturalLength = br.naturalWidth;
        line.extentLeft = -halfEm;
        line.extentRight = halfEm;
        line.charStart = charOffset + items[br.itemStart].charIndex;
        line.charEnd = line.charStart;
        if (br.itemEnd < items.size() && items[br.itemEnd].isPenalty() &&
            items[br.itemEnd].width < 0.0f) {
            line.hanging = true;
            line.hangWidth = -items[br.itemEnd].width;
        }

        float v = 0.0f;
        for (uint32_t i = br.itemStart; i < br.itemEnd; ++i) {
            const LineItem& item = items[i];

            if (item.isGlue()) {
                v += item.natural +
                     (br.ratio >= 0.0f ? br.ratio * item.stretch : br.ratio * item.shrink);
                continue;
            }
            if (!item.isBox()) {
                continue;   // Penalty は幅を持たない（ブレークしたときだけ効く）
            }

            const ShapedCluster& cluster = shaped.clusters[item.clusterIndex];

            if (!item.ownGlyphs) {
                // クラスタはベタ組み位置で組んであるので、その差分だけずらす
                const float delta = (v + item.glyphOffset) - cluster.origin;
                for (uint32_t g = 0; g < cluster.glyphCount; ++g) {
                    GlyphInfo glyph = shaped.glyphs[cluster.glyphStart + g];
                    glyph.y += delta;
                    glyph.charIndex += charOffset;
                    line.glyphs.push_back(glyph);
                }
                if (!cluster.upright) {
                    // 横倒しラン（欧文）は 1em より広く／狭くなりうる
                    line.extentLeft = std::min(line.extentLeft, shaped.extentLeft);
                    line.extentRight = std::max(line.extentRight, shaped.extentRight);
                }
            }

            // 合成 Box の本体（縦中横・割注）と、付随グリフ（ルビ・圏点）。
            // どちらも Box の先頭からの相対座標で入っている。
            for (GlyphInfo glyph : item.glyphs) {
                glyph.y += v;
                glyph.charIndex += charOffset;
                line.glyphs.push_back(glyph);
            }
            if (item.extentLeft != 0.0f) {
                line.extentLeft = std::min(line.extentLeft, item.extentLeft);
            }
            if (item.extentRight != 0.0f) {
                line.extentRight = std::max(line.extentRight, item.extentRight);
            }

            line.charEnd = charOffset + cluster.charEnd;
            v += item.width;
        }

        lines_.push_back(std::move(line));
    }
}

VerticalParagraphLayout::RenderPosition
VerticalParagraphLayout::getLinePosition(size_t index, float originX, float originY) const {
    const float offset = lineAdvance_ * static_cast<float>(index);
    if (options_.writingMode == WritingMode::VerticalLr) {
        return RenderPosition{originX + offset, originY};
    }
    // vertical-rl: 列は右から左へ進む
    return RenderPosition{originX - offset, originY};
}

} // namespace richtext::vertical
