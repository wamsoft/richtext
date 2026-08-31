#ifndef RICHTEXT_GLYPH_TRANSFORM_HPP
#define RICHTEXT_GLYPH_TRANSFORM_HPP

#include <cmath>

#include "richtext/Raster.hpp"
#include "richtext/TextLayout.hpp"
#include "richtext/TextStyle.hpp"

/**
 * GlyphTransform — グリフ固有の変形の組み立て
 *
 * ラスタライズ（GlyphRenderer）と PDF 出力（pdf::PdfWriter）で同じ変形を
 * 使うために切り出してある。ここが 2 箇所に分かれていると、画面と PDF で
 * 斜体や縦組みの横倒しがずれる。
 */
namespace richtext {

/**
 * グリフ固有の変形を 1 つの 2x2 行列へ畳み込む（ベースライン原点・y-down）
 *
 * 順序は「フェイク幅／斜体 → GlyphInfo のスケール → GlyphInfo の回転」。
 *
 * @param glyph グリフ情報
 * @param fakeScaleX フォント幅のうち wdth 軸で吸収できない分の水平スケール
 */
inline Matrix2D makeGlyphMatrix(const GlyphInfo& glyph, float fakeScaleX) {
    // フェイクイタリック: シアー係数（Android と同じ -0.25 ≒ 14度）
    // minikin::FontFakery の判定は非 const なのでコピーしてから呼ぶ
    minikin::FontFakery fakery = glyph.fakery;
    const float skewX = fakery.isFakeItalic() ? -0.25f : 0.0f;

    Matrix2D m;
    m.e11 = fakeScaleX;
    m.e12 = skewX;   // y-down 座標では pt.x += skewX * pt.y
    m.e21 = 0.0f;
    m.e22 = 1.0f;

    if (glyph.scaleX != 1.0f || glyph.scaleY != 1.0f) {
        Matrix2D s;
        s.e11 = glyph.scaleX;
        s.e22 = glyph.scaleY;
        m = multiply(s, m);
    }
    if (glyph.rotation != 0.0f) {
        // 角度は数学慣習（y-up）の反時計回りが正。描画先が y-down なので
        // Y 反転で共役を取った形になり、シアー成分の符号が入れ替わる。
        const float cosR = std::cos(glyph.rotation);
        const float sinR = std::sin(glyph.rotation);
        Matrix2D r;
        r.e11 = cosR;  r.e12 = sinR;
        r.e21 = -sinR; r.e22 = cosR;
        m = multiply(r, m);
    }
    return m;
}

/// このグリフに適用するフォントサイズ（GlyphInfo 側の指定が優先）
inline float glyphFontSize(const GlyphInfo& glyph, const TextStyle& style) {
    return (glyph.fontSize > 0.0f) ? glyph.fontSize : style.fontSize;
}

/// フェイクボールドのストローク幅（0 なら不要）
inline float fakeBoldStrokeWidth(const GlyphInfo& glyph, float fontSize) {
    minikin::FontFakery fakery = glyph.fakery;
    return fakery.isFakeBold() ? (fontSize / 24.0f) : 0.0f;
}

} // namespace richtext

#endif // RICHTEXT_GLYPH_TRANSFORM_HPP
