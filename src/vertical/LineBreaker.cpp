/**
 * LineBreaker.cpp
 *
 * TeX 型の行分割
 *
 * Greedy と Knuth–Plass（total-fit）の 2 つを持つ。どちらも「合法ブレーク点」
 * と「グルーの調整比」の定義は共有していて、違うのはどのブレーク点の組を
 * 選ぶかだけ。
 */

#include "richtext/vertical/LineBreaker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace richtext::vertical {

namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();

/// アイテム i が合法なブレーク点か
bool isLegalBreak(const std::vector<LineItem>& items, size_t i) {
    const LineItem& it = items[i];
    if (it.isPenalty()) {
        return it.penalty < kInfinitePenalty;
    }
    if (it.isGlue()) {
        // グルーの直前が Box のときだけ切れる（禁則は直前に Penalty(∞) を
        // 挟むことでこの条件を崩して表現している）
        return i > 0 && items[i - 1].isBox();
    }
    return false;
}

/// ブレーク点 b の次の行の開始位置（捨てられるアイテムを読み飛ばす）
uint32_t nextLineStart(const std::vector<LineItem>& items, size_t b) {
    size_t i = items[b].isPenalty() ? b + 1 : b;
    while (i < items.size() && !items[i].isBox()) {
        ++i;
    }
    return static_cast<uint32_t>(i);
}

/// 各アイテムまでの累積（幅・伸び・縮み）
struct Prefix {
    std::vector<float> width;
    std::vector<float> stretch;
    std::vector<float> shrink;

    explicit Prefix(const std::vector<LineItem>& items) {
        const size_t n = items.size();
        width.resize(n + 1, 0.0f);
        stretch.resize(n + 1, 0.0f);
        shrink.resize(n + 1, 0.0f);
        for (size_t i = 0; i < n; ++i) {
            const LineItem& it = items[i];
            float w = 0.0f, st = 0.0f, sh = 0.0f;
            if (it.isBox()) {
                w = it.width;
            } else if (it.isGlue()) {
                w = it.natural;
                st = it.stretch;
                sh = it.shrink;
            }
            width[i + 1] = width[i] + w;
            stretch[i + 1] = stretch[i] + st;
            shrink[i + 1] = shrink[i] + sh;
        }
    }
};

/// 行 [start, breakAt) の自然幅（ブレーク点が Penalty ならその幅を足す）
float lineWidth(const std::vector<LineItem>& items, const Prefix& pre,
                uint32_t start, uint32_t breakAt) {
    float w = pre.width[breakAt] - pre.width[start];
    if (items[breakAt].isPenalty()) {
        w += items[breakAt].width;
    }
    return w;
}

/**
 * グルーの調整比
 *
 * 目標より短ければ正（伸ばす）、長ければ負（縮める）。-1 未満は物理的に
 * 詰めきれないので不可（呼び出し側で弾く）。
 */
float adjustRatio(float natural, float stretch, float shrink, float target) {
    const float diff = target - natural;
    if (std::fabs(diff) < 1e-4f) return 0.0f;
    if (diff > 0.0f) {
        return (stretch > 0.0f) ? diff / stretch : kInfinity;
    }
    return (shrink > 0.0f) ? diff / shrink : -kInfinity;
}

float badness(float ratio) {
    if (!std::isfinite(ratio)) return 10000.0f;
    const float r = std::fabs(ratio);
    return 100.0f * r * r * r;
}

//------------------------------------------------------------------------------
// Greedy
//------------------------------------------------------------------------------

std::vector<BreakLine> breakGreedy(const std::vector<LineItem>& items,
                                   const Prefix& pre,
                                   float lineLength,
                                   const LineBreakOptions& opts) {
    std::vector<BreakLine> lines;
    const uint32_t n = static_cast<uint32_t>(items.size());

    uint32_t start = 0;
    while (start < n && !items[start].isBox()) ++start;

    while (start < n) {
        uint32_t chosen = n;
        bool forced = false;

        for (uint32_t i = start; i < n; ++i) {
            if (isLegalBreak(items, i) && i > start) {
                const float w = lineWidth(items, pre, start, i);
                const float sh = pre.shrink[i] - pre.shrink[start];
                if (w - sh <= lineLength) {
                    chosen = i;
                    if (items[i].isForcedBreak()) {
                        forced = true;
                        break;
                    }
                } else if (chosen != n) {
                    // これ以上は入らない
                    break;
                } else {
                    // 1 つも入らない場合は溢れを承知でここで切る
                    chosen = i;
                    if (items[i].isForcedBreak()) forced = true;
                    break;
                }
            }
        }

        if (chosen == n) {
            // ブレーク点が見つからない（末尾の段落終端が無い場合など）
            chosen = n - 1;
            forced = true;
        }

        BreakLine line;
        line.itemStart = start;
        line.itemEnd = chosen;
        line.naturalWidth = lineWidth(items, pre, start, chosen);
        if (opts.justify) {
            const float st = pre.stretch[chosen] - pre.stretch[start];
            const float sh = pre.shrink[chosen] - pre.shrink[start];
            float r = adjustRatio(line.naturalWidth, st, sh, lineLength);
            if (!std::isfinite(r)) r = 0.0f;
            line.ratio = std::clamp(r, -1.0f, 1.0f);
        }
        const float st = pre.stretch[chosen] - pre.stretch[start];
        const float sh = pre.shrink[chosen] - pre.shrink[start];
        line.width = line.naturalWidth +
                     (line.ratio >= 0.0f ? line.ratio * st : line.ratio * sh);
        lines.push_back(line);

        if (forced && chosen >= n - 1) break;

        const uint32_t next = nextLineStart(items, chosen);
        if (next <= start) break;   // 前進しない場合の保険
        start = next;
    }

    return lines;
}

//------------------------------------------------------------------------------
// Knuth–Plass（total-fit）
//------------------------------------------------------------------------------

struct Node {
    uint32_t position;      ///< このノードから始まる行の開始アイテム
    uint32_t breakAt;       ///< このノードを生んだブレーク点
    uint32_t lineNumber;
    float totalDemerits;
    int previous;           ///< nodes 内のインデックス（-1 = 段落先頭）
    float ratio;
};

std::vector<BreakLine> breakKnuthPlass(const std::vector<LineItem>& items,
                                       const Prefix& pre,
                                       float lineLength,
                                       const LineBreakOptions& opts) {
    const uint32_t n = static_cast<uint32_t>(items.size());

    uint32_t start = 0;
    while (start < n && !items[start].isBox()) ++start;

    std::vector<Node> nodes;
    nodes.push_back(Node{start, start, 0, 0.0f, -1, 0.0f});
    std::vector<int> active{0};

    int lastNode = -1;

    for (uint32_t b = start; b < n; ++b) {
        if (!isLegalBreak(items, b)) continue;

        int bestNode = -1;
        float bestDemerits = kInfinity;
        float bestRatio = 0.0f;
        std::vector<int> stillActive;
        stillActive.reserve(active.size());

        for (int ai : active) {
            const Node& a = nodes[ai];
            if (b <= a.position) {
                stillActive.push_back(ai);
                continue;
            }
            const float w = lineWidth(items, pre, a.position, b);
            const float st = pre.stretch[b] - pre.stretch[a.position];
            const float sh = pre.shrink[b] - pre.shrink[a.position];
            const float r = adjustRatio(w, st, sh, lineLength);

            const bool tooLong = (r < -1.0f) || (r == -kInfinity);
            const bool forced = items[b].isForcedBreak();

            if (!tooLong && (forced || (std::isfinite(r) && r <= opts.tolerance))) {
                float d = opts.linePenalty + badness(r);
                d = d * d;
                if (items[b].isPenalty() && !forced) {
                    const float p = items[b].penalty;
                    d += (p >= 0.0f) ? p * p : -(p * p);
                }
                const float total = a.totalDemerits + d;
                if (total < bestDemerits) {
                    bestDemerits = total;
                    bestNode = ai;
                    bestRatio = std::clamp(r, -1.0f, 1.0f);
                }
            }

            // 行が長すぎるノードはこれ以降さらに長くなるだけなので落とす
            if (!tooLong) {
                stillActive.push_back(ai);
            }
        }

        if (bestNode >= 0) {
            const uint32_t next = nextLineStart(items, b);
            nodes.push_back(Node{next, b, nodes[bestNode].lineNumber + 1, bestDemerits,
                                 bestNode, bestRatio});
            const int newIndex = static_cast<int>(nodes.size()) - 1;
            if (items[b].isForcedBreak()) {
                lastNode = newIndex;
            }
            stillActive.push_back(newIndex);
        }

        active.swap(stillActive);
        if (active.empty()) {
            // どこにも繋がらなくなったら Greedy にフォールバックする
            return breakGreedy(items, pre, lineLength, opts);
        }
    }

    if (lastNode < 0) {
        // 強制ブレークまで届かなかった場合もフォールバック
        return breakGreedy(items, pre, lineLength, opts);
    }

    // 経路を復元する。ノード i の行は [nodes[prev].position, node.breakAt)。
    std::vector<BreakLine> lines;
    for (int i = lastNode; i > 0; i = nodes[i].previous) {
        const Node& node = nodes[i];
        const Node& prev = nodes[node.previous];
        const uint32_t breakAt = node.breakAt;

        BreakLine line;
        line.itemStart = prev.position;
        line.itemEnd = breakAt;
        line.ratio = opts.justify ? node.ratio : 0.0f;
        line.naturalWidth = lineWidth(items, pre, prev.position, breakAt);
        const float st = pre.stretch[breakAt] - pre.stretch[prev.position];
        const float sh = pre.shrink[breakAt] - pre.shrink[prev.position];
        line.width = line.naturalWidth +
                     (line.ratio >= 0.0f ? line.ratio * st : line.ratio * sh);
        lines.push_back(line);

        if (node.previous <= 0) break;
    }
    std::reverse(lines.begin(), lines.end());
    return lines;
}

} // namespace

std::vector<BreakLine> LineBreaker::breakLines(const std::vector<LineItem>& items,
                                               float lineLength,
                                               const LineBreakOptions& opts) {
    if (items.empty() || lineLength <= 0.0f) {
        return {};
    }
    const Prefix pre(items);
    if (opts.strategy == LineBreakStrategy::KnuthPlass) {
        return breakKnuthPlass(items, pre, lineLength, opts);
    }
    return breakGreedy(items, pre, lineLength, opts);
}

} // namespace richtext::vertical
