#ifndef RICHTEXT_VERTICAL_SHAPER_HPP
#define RICHTEXT_VERTICAL_SHAPER_HPP

#include <string>
#include <vector>

#include "richtext/TextLayout.hpp"   // GlyphInfo
#include "richtext/TextStyle.hpp"
#include "richtext/vertical/WritingMode.hpp"

namespace richtext::vertical {

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
};

} // namespace richtext::vertical

#endif // RICHTEXT_VERTICAL_SHAPER_HPP
