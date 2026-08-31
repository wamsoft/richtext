#ifndef RICHTEXT_VERTICAL_LINE_ITEM_BUILDER_HPP
#define RICHTEXT_VERTICAL_LINE_ITEM_BUILDER_HPP

#include <vector>

#include "richtext/TextStyle.hpp"
#include "richtext/vertical/InlineAnnotation.hpp"
#include "richtext/vertical/LineItem.hpp"
#include "richtext/vertical/VerticalShaper.hpp"

/**
 * LineItemBuilder — シェイピング結果 → Box / Glue / Penalty 列
 *
 * JLReq の文字クラス（CharClass）とアキ量表（SpacingTable）を使って、
 * クラスタ列を組版アイテム列へ変換する。ここで
 *
 *  - 約物の仮想ボディを半角へ詰め、字面のオフセットを Box に持たせる
 *  - 文字クラスの隣接ペアから伸縮するアキ（Glue）を入れる
 *  - 禁則（行頭禁則・行末禁則・分離禁止・欧文単語内）を Penalty(∞) で表す
 *  - ぶら下げを「幅が負の Penalty」で表す
 *
 * が確定する。
 */
namespace richtext::vertical {

/**
 * 組版オプション
 */
struct VerticalSpacingOptions {
    /// 約物の詰め（JLReq のアキ量表を適用する）。false ならベタ組み
    bool punctuationSpacing = true;

    /// 行末に来た句読点を版面外へ出す
    bool hangingPunctuation = false;

    /// 和欧間のアキを入れる
    bool latinGap = true;

    /// 字間（em 単位）。TextStyle::letterSpacing と同じ意味だが、
    /// 組版層ではクラスタ間の Glue として扱う
    float letterSpacing = 0.0f;
};

/**
 * アイテム列を組み立てる
 *
 * 末尾には段落終端（無限に伸びる Glue ＋ 強制ブレークの Penalty）を付ける。
 *
 * @param shaped シェイピング結果
 * @param style 本文のスタイル（ルビ・縦中横・割注のシェイピングにも使う）
 * @param annotations インライン注記（本文の文字位置を指す。空でよい）
 * @param opts 組版オプション
 */
std::vector<LineItem> buildLineItems(const VerticalShaper::Result& shaped,
                                     const TextStyle& style,
                                     const std::vector<InlineAnnotation>& annotations,
                                     const VerticalSpacingOptions& opts);

/// 注記なしの簡易版
inline std::vector<LineItem> buildLineItems(const VerticalShaper::Result& shaped,
                                            const TextStyle& style,
                                            const VerticalSpacingOptions& opts) {
    return buildLineItems(shaped, style, {}, opts);
}

} // namespace richtext::vertical

#endif // RICHTEXT_VERTICAL_LINE_ITEM_BUILDER_HPP
