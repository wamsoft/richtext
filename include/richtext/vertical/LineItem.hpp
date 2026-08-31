#ifndef RICHTEXT_VERTICAL_LINE_ITEM_HPP
#define RICHTEXT_VERTICAL_LINE_ITEM_HPP

#include <cstddef>
#include <cstdint>
#include <limits>

#include "richtext/vertical/CharClass.hpp"
#include "richtext/vertical/SpacingTable.hpp"

/**
 * LineItem — Box / Glue / Penalty
 *
 * 組版対象を TeX と同じ 3 種のアイテム列で表す（縦組み設計.md 4.2）。
 * 寸法はピクセル（em ではない。SpacingTable の em 値にフォントサイズを
 * 掛けたもの）。
 *
 * 合法なブレーク点は TeX と同じ規則で決まる。
 *
 *  - `Penalty` アイテムで、penalty が kInfinitePenalty 未満のところ
 *  - `Glue` アイテムで、直前が `Box` のところ
 *
 * 禁則は「Glue の直前に Penalty(kInfinitePenalty) を挟む」ことで表す。
 * これで 2 番目の条件（直前が Box）が崩れ、その位置では切れなくなる。
 */
namespace richtext::vertical {

/// これ以上のペナルティはブレーク禁止
constexpr float kInfinitePenalty = 10000.0f;
/// これ以下のペナルティは強制ブレーク
constexpr float kForcedBreakPenalty = -10000.0f;

enum class ItemType : uint8_t {
    Box,        ///< 固定幅。グリフ（クラスタ）1 個
    Glue,       ///< 伸縮するアキ
    Penalty,    ///< 分割の可否とコスト
};

struct LineItem {
    ItemType type = ItemType::Box;

    /// Box: ボディ幅／Penalty: ブレークしたときにその位置に現れる幅
    /// （ぶら下げは負値で表す＝ブレーク時に行長から取り除かれる）
    float width = 0.0f;

    /// Glue の伸縮（ピクセル）
    float natural = 0.0f;
    float stretch = 0.0f;
    float shrink = 0.0f;

    /// Penalty のコスト
    float penalty = 0.0f;

    /// Box: 仮想ボディを詰めたときのグリフの描画オフセット
    /// （始め括弧のように字面がボディ後半に寄る約物で負値になる）
    float glyphOffset = 0.0f;

    /// Box が指すクラスタ（Box 以外では kNoCluster）
    uint32_t clusterIndex = kNoCluster;

    /// 元テキストでの位置（UTF-16 単位）
    size_t charIndex = 0;

    static constexpr uint32_t kNoCluster = std::numeric_limits<uint32_t>::max();

    static LineItem box(float w, uint32_t cluster, size_t charIndex) {
        LineItem it;
        it.type = ItemType::Box;
        it.width = w;
        it.clusterIndex = cluster;
        it.charIndex = charIndex;
        return it;
    }

    static LineItem glue(const GlueSpec& spec, float em, size_t charIndex) {
        LineItem it;
        it.type = ItemType::Glue;
        it.natural = spec.natural * em;
        it.stretch = spec.stretch * em;
        it.shrink = spec.shrink * em;
        it.charIndex = charIndex;
        return it;
    }

    static LineItem gluePx(float natural, float stretch, float shrink, size_t charIndex) {
        LineItem it;
        it.type = ItemType::Glue;
        it.natural = natural;
        it.stretch = stretch;
        it.shrink = shrink;
        it.charIndex = charIndex;
        return it;
    }

    static LineItem penaltyItem(float cost, float width, size_t charIndex) {
        LineItem it;
        it.type = ItemType::Penalty;
        it.penalty = cost;
        it.width = width;
        it.charIndex = charIndex;
        return it;
    }

    bool isBox() const { return type == ItemType::Box; }
    bool isGlue() const { return type == ItemType::Glue; }
    bool isPenalty() const { return type == ItemType::Penalty; }

    bool isForcedBreak() const {
        return type == ItemType::Penalty && penalty <= kForcedBreakPenalty;
    }
};

} // namespace richtext::vertical

#endif // RICHTEXT_VERTICAL_LINE_ITEM_HPP
