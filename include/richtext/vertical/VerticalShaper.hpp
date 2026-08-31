#ifndef RICHTEXT_VERTICAL_SHAPER_HPP
#define RICHTEXT_VERTICAL_SHAPER_HPP

#include <string>
#include <vector>

#include <cstdint>

#include "richtext/TextLayout.hpp"   // GlyphInfo
#include "richtext/TextStyle.hpp"
#include "richtext/vertical/CharClass.hpp"
#include "richtext/vertical/WritingMode.hpp"

namespace richtext::vertical {

/**
 * シェイピング結果のクラスタ（組版の最小単位）
 *
 * HarfBuzz のクラスタ 1 つに対応する。組版層（LineItem / LineBreaker）は
 * グリフではなくこの単位で扱い、行を確定したあとで
 * 「クラスタの新しい位置 - origin」をグリフの y に足して再配置する。
 */
struct ShapedCluster {
    uint32_t glyphStart = 0;    ///< VerticalShaper::Result::glyphs 内の開始
    uint32_t glyphCount = 0;
    size_t charStart = 0;       ///< 元テキストでの範囲（UTF-16 単位）
    size_t charEnd = 0;
    float origin = 0.0f;        ///< ベタ組みでの v 位置（再配置の基準）
    float advance = 0.0f;       ///< シェイパーが返した送り
    CharClass charClass = CharClass::Unknown;
    bool upright = true;        ///< 正立か横倒しか
};

/**
 * VerticalShaper — 縦組みのシェイピング
 *
 * minikin の FontCollection::itemize() でフォントフォールバックを解決し、
 * 各ランを HarfBuzz に直接シェイピングさせる。minikin::Layout は LTR / RTL
 * しか扱えないので縦組みでは通さない。
 *
 *  - 正立ラン … HB_DIRECTION_TTB。vert / vrt2（縦字形置換）、vmtx / VORG
 *               （縦アドバンスと原点）、vkrn / vpal が HarfBuzz 側で効く
 *  - 横倒しラン … HB_DIRECTION_LTR で組んでから列へ 90 度倒して配置する
 *
 * 出力は GlyphInfo 列。座標は WritingMode.hpp の (u, v) を x, y に入れたもの
 * （u = 縦ベースラインからの左右、v = 行頭からの送り）。
 */
class VerticalShaper {
public:
    struct Result {
        std::vector<GlyphInfo> glyphs;
        std::vector<ShapedCluster> clusters;
        float advance = 0.0f;       ///< 行方向（v）の総送り
        float extentLeft = 0.0f;    ///< 縦ベースラインからの左への張り出し（負値）
        float extentRight = 0.0f;   ///< 同・右への張り出し（正値）

        float width() const { return extentRight - extentLeft; }
    };

    /**
     * シェイピング実行
     * @param text UTF-16 テキスト（改行は含めない。1 行分）
     * @param style テキストスタイル
     * @param orientation 正立／横倒しの指定
     */
    static Result shape(const std::u16string& text,
                        const TextStyle& style,
                        TextOrientation orientation = TextOrientation::Mixed);

    /**
     * 縦ベースラインからの左右の張り出し（1em 幅の正立ラン相当）
     * クラスタ単位での列幅計算に使う。
     */
    static float uprightHalfWidth(float fontSize) { return fontSize * 0.5f; }
};

} // namespace richtext::vertical

#endif // RICHTEXT_VERTICAL_SHAPER_HPP
