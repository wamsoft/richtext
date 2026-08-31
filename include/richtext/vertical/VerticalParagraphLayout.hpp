#ifndef RICHTEXT_VERTICAL_PARAGRAPH_LAYOUT_HPP
#define RICHTEXT_VERTICAL_PARAGRAPH_LAYOUT_HPP

#include <string>
#include <vector>

#include "richtext/TextLayout.hpp"   // GlyphInfo
#include "richtext/TextStyle.hpp"
#include "richtext/vertical/LineBreaker.hpp"
#include "richtext/vertical/LineItemBuilder.hpp"
#include "richtext/vertical/WritingMode.hpp"

namespace richtext::vertical {

/**
 * 縦組みのレイアウトオプション
 */
struct VerticalLayoutOptions {
    WritingMode writingMode = WritingMode::VerticalRl;
    TextOrientation orientation = TextOrientation::Mixed;

    /// 約物の詰め・和欧間・ぶら下げ
    VerticalSpacingOptions spacing;

    /// 行分割の戦略と両端揃え
    LineBreakOptions lineBreak;

    /// 行間（ピクセル）。行送り = フォントサイズ + lineGap
    float lineGap = 0.0f;
};

/**
 * VerticalParagraphLayout — 縦組みの段落レイアウト
 *
 * 横組みの ParagraphLayout に対応する。処理の流れは
 *
 *   1. 改行で段落へ分ける
 *   2. VerticalShaper でクラスタ列を得る
 *   3. buildLineItems() で Box / Glue / Penalty 列へ変換（JLReq のアキと禁則）
 *   4. LineBreaker で行分割し、行ごとのグルー調整比を決める
 *   5. 調整比に従ってクラスタを配置し直し、行ごとの GlyphInfo 列を作る
 *
 * 座標系は VerticalLayout と同じで、GlyphInfo::x が縦ベースラインからの
 * 左右、GlyphInfo::y が行頭からの送り。列の位置は getLinePosition() で得る。
 */
class VerticalParagraphLayout {
public:
    /**
     * 確定した 1 行
     */
    struct Line {
        std::vector<GlyphInfo> glyphs;
        float length = 0.0f;        ///< 調整後の行長
        float naturalLength = 0.0f; ///< 調整前の行長
        float extentLeft = 0.0f;    ///< 縦ベースラインからの左への張り出し（負値）
        float extentRight = 0.0f;   ///< 同・右への張り出し（正値）
        size_t charStart = 0;       ///< 元テキストでの範囲（UTF-16 単位）
        size_t charEnd = 0;
        /// 行末の約物を版面外へ出した（ぶら下げた）行か
        bool hanging = false;
        /// ぶら下げた分の長さ（hanging が false なら 0）
        float hangWidth = 0.0f;

        float width() const { return extentRight - extentLeft; }
    };

    /**
     * 行の描画基準点
     */
    struct RenderPosition {
        float x;   ///< 縦ベースライン（列の中心線）のX
        float y;   ///< 行頭のY
    };

    VerticalParagraphLayout() = default;

    /**
     * レイアウト実行
     * @param text UTF-16 テキスト（改行 \n / \r\n を含んでよい）
     * @param style テキストスタイル
     * @param lineLength 1 行の長さ（列の長さ）
     * @param opts オプション
     */
    void layout(const std::u16string& text, const TextStyle& style,
                float lineLength, const VerticalLayoutOptions& opts = {});

    /**
     * レイアウト実行（インライン注記あり）
     * @param text UTF-16 テキスト（改行 \n / \r\n を含んでよい）
     * @param annotations ルビ・縦中横・圏点・割注・字取り。
     *                    範囲は text 内の UTF-16 位置で指定する
     * @param style テキストスタイル
     * @param lineLength 1 行の長さ（列の長さ）
     * @param opts オプション
     */
    void layout(const std::u16string& text,
                const std::vector<InlineAnnotation>& annotations,
                const TextStyle& style,
                float lineLength, const VerticalLayoutOptions& opts = {});

    // ------------------------------------------------------------------
    // 結果
    // ------------------------------------------------------------------

    size_t getLineCount() const { return lines_.size(); }
    const Line& getLine(size_t index) const { return lines_[index]; }
    const std::vector<Line>& getLines() const { return lines_; }

    /// 指定された行長
    float getLineLength() const { return lineLength_; }

    /// 行送り（隣り合う列の縦ベースラインの間隔）
    float getLineAdvance() const { return lineAdvance_; }

    /// 全列を並べたときの幅
    float getTotalWidth() const {
        return lines_.empty() ? 0.0f : lineAdvance_ * static_cast<float>(lines_.size());
    }

    /// 最長の行長
    float getMaxLineLength() const { return maxLineLength_; }

    /**
     * 行の描画基準点を返す
     *
     * @param index 行インデックス
     * @param originX 1 列目の縦ベースラインのX
     * @param originY 行頭のY
     *
     * vertical-rl では列が右から左へ進むので X は減っていく。
     */
    RenderPosition getLinePosition(size_t index, float originX, float originY) const;

    const std::u16string& getText() const { return text_; }
    const TextStyle& getStyle() const { return style_; }
    const VerticalLayoutOptions& getOptions() const { return options_; }

private:
    std::u16string text_;
    TextStyle style_;
    VerticalLayoutOptions options_;
    std::vector<InlineAnnotation> annotations_;

    std::vector<Line> lines_;
    float lineLength_ = 0.0f;
    float lineAdvance_ = 0.0f;
    float maxLineLength_ = 0.0f;

    void layoutParagraph(const std::u16string& paragraph, size_t charOffset);
};

} // namespace richtext::vertical

#endif // RICHTEXT_VERTICAL_PARAGRAPH_LAYOUT_HPP
