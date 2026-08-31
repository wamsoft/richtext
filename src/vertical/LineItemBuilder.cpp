/**
 * LineItemBuilder.cpp
 *
 * シェイピング結果 → Box / Glue / Penalty 列
 */

#include "richtext/vertical/LineItemBuilder.hpp"

#include <algorithm>

namespace richtext::vertical {

namespace {

/**
 * 2 つのクラスタの間で改行できるか
 *
 * JLReq の禁則。ここで false になる位置には Penalty(∞) を置き、
 * 続く Glue がブレーク点にならないようにする。
 */
bool canBreakBetween(CharClass before, CharClass after) {
    // 行末禁則: 始め括弧類・前置省略記号の直後では切らない
    if (isLineEndProhibited(before)) return false;
    // 行頭禁則: 終わり括弧類・句読点・中点類・小書きの仮名等の直前では切らない
    if (isLineStartProhibited(after)) return false;

    // 分離禁止文字（—— …… 等）は 2 つ並びを割らない
    if (before == CharClass::Inseparable && after == CharClass::Inseparable) {
        return false;
    }

    // 欧文単語の内部と連数字は割らない。欧文の切れ目は空白（Glue）だけ
    if (isWestern(before) && isWestern(after)) return false;
    // 省略記号は続く／先立つ数字と離さない
    if (before == CharClass::PrefixAbbr && isWestern(after)) return false;
    if (isWestern(before) && after == CharClass::PostfixAbbr) return false;

    return true;
}

/**
 * 詰めた仮想ボディの中でのグリフ位置
 *
 * シェイパーが返す送り（通常 1em）と、詰めたあとのボディ幅の差を、
 * 字面の寄りに応じて前後へ振り分ける。
 */
float bodyGlyphOffset(CharClass cls, float shapedAdvance, float bodyWidth) {
    const float slack = shapedAdvance - bodyWidth;
    if (slack <= 0.0f) return 0.0f;
    switch (getBodyAlign(cls)) {
        case BodyAlign::End:    return -slack;
        case BodyAlign::Center: return -slack * 0.5f;
        case BodyAlign::Start:
        case BodyAlign::Full:
        default:                return 0.0f;
    }
}

} // namespace

std::vector<LineItem> buildLineItems(const VerticalShaper::Result& shaped,
                                     float em,
                                     const VerticalSpacingOptions& opts) {
    std::vector<LineItem> items;
    if (shaped.clusters.empty()) {
        return items;
    }
    items.reserve(shaped.clusters.size() * 3 + 2);

    const float letterSpacing = opts.letterSpacing * em;

    bool prevWasBox = false;
    CharClass prevClass = CharClass::Unknown;
    float prevBoxWidth = 0.0f;

    for (uint32_t ci = 0; ci < shaped.clusters.size(); ++ci) {
        const ShapedCluster& cluster = shaped.clusters[ci];
        const CharClass cls = cluster.charClass;

        // --- 欧文間隔は Box ではなく Glue にする（そこが唯一の欧文の切れ目） ---
        if (cls == CharClass::Space) {
            const float w = cluster.advance;
            items.push_back(LineItem::gluePx(w, w * 0.5f, w / 3.0f, cluster.charStart));
            prevWasBox = false;
            prevClass = cls;
            continue;
        }

        // --- クラスタ間のアキと禁則 ---
        if (prevWasBox) {
            const bool latinBoundary = (isJapanese(prevClass) && isWestern(cls)) ||
                                       (isWestern(prevClass) && isJapanese(cls));
            GlueSpec spec = getSpacing(prevClass, cls);
            if (latinBoundary ? !opts.latinGap : !opts.punctuationSpacing) {
                spec = GlueSpec{};
            }
            float natural = spec.natural * em + letterSpacing;
            float stretch = spec.stretch * em;
            float shrink = spec.shrink * em;

            const bool breakable = canBreakBetween(prevClass, cls);

            // ぶら下げ: 句読点の直後は「幅が負の Penalty」で切る。ブレークすると
            // 句読点 1 文字分が行長から引かれる＝版面外へ出る。
            // Penalty を挟むと直後の Glue はブレーク点でなくなるので
            // （TeX の「Glue の直前が Box のときだけ切れる」規則）、
            // ここでの切り方はぶら下げに一本化される。
            if (breakable && opts.hangingPunctuation && isHangable(prevClass)) {
                items.push_back(
                        LineItem::penaltyItem(0.0f, -prevBoxWidth, cluster.charStart));
            } else if (!breakable) {
                items.push_back(
                        LineItem::penaltyItem(kInfinitePenalty, 0.0f, cluster.charStart));
            }

            if (breakable || natural != 0.0f || stretch != 0.0f || shrink != 0.0f) {
                items.push_back(
                        LineItem::gluePx(natural, stretch, shrink, cluster.charStart));
            }
        }

        // --- クラスタ本体 ---
        float bodyEm = opts.punctuationSpacing ? getBodyWidth(cls) : 0.0f;
        if (!opts.punctuationSpacing && isJapanese(cls)) {
            bodyEm = 1.0f;
        }
        float width = (bodyEm > 0.0f) ? bodyEm * em : cluster.advance;
        // シェイパーの送りより広い仮想ボディにはしない（合成フォントで
        // 縦アドバンスが 1em に満たない場合に隙間が空くのを避ける）
        if (bodyEm > 0.0f && cluster.advance > 0.0f) {
            width = std::min(width, cluster.advance);
        }

        LineItem box = LineItem::box(width, ci, cluster.charStart);
        box.glyphOffset = bodyGlyphOffset(cls, cluster.advance, width);
        items.push_back(box);

        prevWasBox = true;
        prevClass = cls;
        prevBoxWidth = width;
    }

    // --- 段落の終端（TeX と同じ形） ---
    // 無限に伸びる Glue で最終行を伸ばさないようにし、強制ブレークで閉じる
    items.push_back(LineItem::penaltyItem(kInfinitePenalty, 0.0f, shaped.clusters.back().charEnd));
    items.push_back(LineItem::gluePx(0.0f, 1.0e6f, 0.0f, shaped.clusters.back().charEnd));
    items.push_back(
            LineItem::penaltyItem(kForcedBreakPenalty, 0.0f, shaped.clusters.back().charEnd));

    return items;
}

} // namespace richtext::vertical
