#ifndef RICHTEXT_VERTICAL_LAYOUT_HPP
#define RICHTEXT_VERTICAL_LAYOUT_HPP

#include <string>
#include <vector>

#include "richtext/TextLayout.hpp"   // GlyphInfo
#include "richtext/TextStyle.hpp"
#include "richtext/vertical/VerticalShaper.hpp"
#include "richtext/vertical/WritingMode.hpp"

namespace richtext::vertical {

/**
 * VerticalLayout — 縦組み 1 行のレイアウト結果
 *
 * 横組みの TextLayout に対応する。座標系は WritingMode.hpp の (u, v)。
 *
 *   GlyphInfo::x … 縦ベースライン（列の中心線）からの左右のずれ。右が正
 *   GlyphInfo::y … 行頭からの送り。下が正
 *
 * 描画側は列の中心 X と行頭 Y を足すだけでよい。
 *
 * 行分割・約物の詰め・禁則は Phase 2（LineItem / SpacingTable / LineBreaker）
 * の担当で、この段階ではベタ組みの 1 行のみを扱う。
 */
class VerticalLayout {
public:
    VerticalLayout() = default;

    /**
     * レイアウト実行
     * @param text UTF-16 テキスト（1 行分。改行は含めない）
     * @param style テキストスタイル
     * @param orientation 正立／横倒しの指定
     */
    void layout(const std::u16string& text, const TextStyle& style,
                TextOrientation orientation = TextOrientation::Mixed);

    void layout(std::u16string&& text, const TextStyle& style,
                TextOrientation orientation = TextOrientation::Mixed);

    // ------------------------------------------------------------------
    // レイアウト結果
    // ------------------------------------------------------------------

    /// 行方向（縦）の長さ
    float getLength() const { return length_; }

    /// 縦ベースラインからの左への張り出し（負値）
    float getExtentLeft() const { return extentLeft_; }

    /// 縦ベースラインからの右への張り出し（正値）
    float getExtentRight() const { return extentRight_; }

    /// 行の幅（列幅）
    float getWidth() const { return extentRight_ - extentLeft_; }

    // ------------------------------------------------------------------
    // グリフ情報
    // ------------------------------------------------------------------

    size_t getGlyphCount() const { return glyphs_.size(); }
    const GlyphInfo& getGlyph(size_t index) const { return glyphs_[index]; }
    const std::vector<GlyphInfo>& getGlyphs() const { return glyphs_; }

    /// 文字数（ユニーク charIndex 数）
    size_t getCharCount() const;

    const std::u16string& getText() const { return text_; }
    const TextStyle& getStyle() const { return style_; }
    TextOrientation getOrientation() const { return orientation_; }

private:
    std::u16string text_;
    TextStyle style_;
    TextOrientation orientation_ = TextOrientation::Mixed;

    std::vector<GlyphInfo> glyphs_;
    float length_ = 0.0f;
    float extentLeft_ = 0.0f;
    float extentRight_ = 0.0f;

    void doLayout();
};

} // namespace richtext::vertical

#endif // RICHTEXT_VERTICAL_LAYOUT_HPP
