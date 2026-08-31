#ifndef RICHTEXT_VERTICAL_WRITING_MODE_HPP
#define RICHTEXT_VERTICAL_WRITING_MODE_HPP

#include <cstdint>

/**
 * WritingMode — 書字方向と縦組みの座標系
 *
 * 縦組みの組版結果は「縦ベースライン」を基準にした 2 軸で表す。
 *
 *   u 軸 … 行に直交する方向。0 = 縦ベースライン（列の中心線）、右が正
 *   v 軸 … 行が進む方向。0 = 行頭（上端）、下が正
 *
 * GlyphInfo へは u を x、v を y に入れて渡す。描画時に列の中心 X と行頭 Y を
 * 足せばそのままピクセル座標になる（描画先は y-down）。
 *
 * 列そのものの送り（vertical-rl なら右から左へ）は行より上の層（BlockLayout,
 * Phase 4）の担当で、この層では扱わない。
 */
namespace richtext::vertical {

/**
 * 書字方向
 */
enum class WritingMode {
    HorizontalTb,   ///< 横組み（既存経路）
    VerticalRl,     ///< 縦組み・列は右から左へ（日本語の既定）
    VerticalLr,     ///< 縦組み・列は左から右へ
};

inline bool isVertical(WritingMode mode) {
    return mode != WritingMode::HorizontalTb;
}

/**
 * 縦組み中の文字の正立／横倒し（CSS text-orientation 相当）
 */
enum class TextOrientation {
    Mixed,      ///< 和文は正立、欧文は横倒し（既定）
    Upright,    ///< すべて正立
    Sideways,   ///< すべて横倒し
};

/**
 * 1 文字の向き
 */
enum class CharOrientation {
    Upright,    ///< 正立（縦字形置換は HarfBuzz の vert/vrt2 が行う）
    Rotated,    ///< 横倒し（時計回りに 90 度）
};

/**
 * UAX #50 Vertical_Orientation 相当の判定
 *
 * U / Tu / Tr を Upright、R を Rotated として返す。Tu・Tr（縦字形へ置換して
 * 正立させる類。括弧・句読点・波ダッシュ等）を Upright に寄せているのは、
 * この層が TTB でシェイピングして vert/vrt2 を効かせるため。縦字形を持たない
 * フォントでは正立のままになるが、和文フォントで実害が出る組み合わせは無い。
 */
CharOrientation getCharOrientation(char32_t cp);

/**
 * 横倒しグリフの回転角（ラジアン）
 *
 * 角度は数学慣習（y-up）の反時計回りが正。描画先は y-down なので、画面上では
 * 時計回りに 90 度倒れる（欧文の天が右を向く＝首を右に傾けて読む向き）。
 */
inline constexpr float kSidewaysRotation = -1.5707963267948966f;   // -90°

} // namespace richtext::vertical

#endif // RICHTEXT_VERTICAL_WRITING_MODE_HPP
