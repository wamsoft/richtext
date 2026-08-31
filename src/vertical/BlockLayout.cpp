/**
 * BlockLayout.cpp
 *
 * 段組・連結コンテナへの流し込み・ページ分割
 *
 * 列の位置は「次の列が置かれる側の端（edge）」で持つ。行送りが段落ごとに
 * 違っても、段落後のアキを入れても、この 1 つの値を動かすだけで済む。
 */

#include "richtext/vertical/BlockLayout.hpp"

#include <algorithm>
#include <cmath>

namespace richtext::vertical {

namespace {

constexpr float kEps = 0.01f;

/// 段落の注記を「charOffset 以降のテキスト」に対する位置へずらす
std::vector<InlineAnnotation> shiftAnnotations(
        const std::vector<InlineAnnotation>& source, size_t charOffset) {
    std::vector<InlineAnnotation> out;
    out.reserve(source.size());
    for (const InlineAnnotation& ann : source) {
        if (ann.end <= charOffset) continue;
        InlineAnnotation shifted = ann;
        shifted.start = (ann.start > charOffset) ? ann.start - charOffset : 0;
        shifted.end = ann.end - charOffset;
        out.push_back(std::move(shifted));
    }
    return out;
}

} // namespace

void BlockLayout::layout(const std::vector<Paragraph>& paragraphs) {
    chunks_.clear();
    placed_.clear();
    overflow_ = Overflow{};
    usedContainers_ = 0;

    if (paragraphs.empty()) return;
    if (containers_.empty()) {
        overflow_ = Overflow{true, 0, 0};
        return;
    }

    size_t containerIndex = 0;
    int sectionIndex = 0;
    float edge = 0.0f;       ///< 次の列が置かれる側の端
    bool freshSection = true;

    // 段（コンテナ, セクション）を開いて edge を初期化する
    const auto openSection = [&](WritingMode mode) {
        const BlockContainer& c = containers_[containerIndex];
        edge = (mode == WritingMode::VerticalLr) ? c.x : (c.x + c.width);
        freshSection = true;
    };

    // 次の段（無ければ次のコンテナ）へ進む。コンテナが尽きたら false
    const auto advanceSection = [&](WritingMode mode) -> bool {
        const BlockContainer& c = containers_[containerIndex];
        if (sectionIndex + 1 < c.columnCount) {
            ++sectionIndex;
        } else {
            if (containerIndex + 1 >= containers_.size()) return false;
            ++containerIndex;
            sectionIndex = 0;
        }
        openSection(mode);
        return true;
    };

    const auto hasRoom = [&](WritingMode mode, float advance) -> bool {
        const BlockContainer& c = containers_[containerIndex];
        if (mode == WritingMode::VerticalLr) {
            return edge + advance <= c.x + c.width + kEps;
        }
        return edge - advance >= c.x - kEps;
    };

    openSection(paragraphs.front().options.writingMode);

    for (size_t pi = 0; pi < paragraphs.size(); ++pi) {
        const Paragraph& para = paragraphs[pi];
        const WritingMode mode = para.options.writingMode;
        if (para.text.empty()) continue;

        size_t charOffset = 0;

        // 段の高さが変わるところで組み直すので、1 段落で複数回まわることがある
        while (true) {
            const float lineLength = containers_[containerIndex].sectionHeight();
            if (lineLength <= 0.0f) {
                overflow_ = Overflow{true, pi, charOffset};
                return;
            }

            Chunk chunk;
            chunk.paragraphIndex = pi;
            chunk.charOffset = charOffset;
            chunk.layout.layout(para.text.substr(charOffset),
                                shiftAnnotations(para.annotations, charOffset),
                                para.style, lineLength, para.options);
            chunks_.push_back(std::move(chunk));
            const uint32_t chunkIndex = static_cast<uint32_t>(chunks_.size() - 1);
            const VerticalParagraphLayout& lay = chunks_.back().layout;

            const float advance = lay.getLineAdvance();
            size_t placedCount = 0;
            bool relayout = false;

            for (size_t li = 0; li < lay.getLineCount(); ++li) {
                // 現在の段に入るまで段／コンテナを進める
                while (!hasRoom(mode, advance) && !freshSection) {
                    if (!advanceSection(mode)) {
                        overflow_ = Overflow{true, pi,
                                             charOffset + lay.getLine(li).charStart};
                        if (placedCount == 0) chunks_.pop_back();
                        usedContainers_ = containerIndex + 1;
                        return;
                    }
                    if (std::fabs(containers_[containerIndex].sectionHeight() - lineLength)
                        > kEps) {
                        relayout = true;
                        break;
                    }
                }
                if (relayout) {
                    charOffset += lay.getLine(li).charStart;
                    break;
                }

                const BlockContainer& c = containers_[containerIndex];
                PlacedLine pl;
                pl.chunkIndex = chunkIndex;
                pl.lineIndex = static_cast<uint32_t>(li);
                pl.containerIndex = static_cast<uint32_t>(containerIndex);
                pl.sectionIndex = static_cast<uint32_t>(sectionIndex);
                pl.y = c.sectionY(sectionIndex);
                if (mode == WritingMode::VerticalLr) {
                    pl.x = edge + advance * 0.5f;
                    edge += advance;
                } else {
                    pl.x = edge - advance * 0.5f;
                    edge -= advance;
                }
                freshSection = false;
                placed_.push_back(pl);
                usedContainers_ = containerIndex + 1;
                ++placedCount;
            }

            if (relayout) {
                if (placedCount == 0) chunks_.pop_back();
                continue;   // 新しい行長で残りを組み直す
            }
            break;          // この段落は置き終わった
        }

        // 段落後のアキ
        if (para.spaceAfter > 0.0f) {
            if (mode == WritingMode::VerticalLr) {
                edge += para.spaceAfter;
            } else {
                edge -= para.spaceAfter;
            }
        }
    }
}

void BlockLayout::getContainerLineRange(size_t index, size_t& first, size_t& count) const {
    first = 0;
    count = 0;
    bool found = false;
    for (size_t i = 0; i < placed_.size(); ++i) {
        if (placed_[i].containerIndex == index) {
            if (!found) {
                first = i;
                found = true;
            }
            ++count;
        } else if (found) {
            break;
        }
    }
}

} // namespace richtext::vertical
