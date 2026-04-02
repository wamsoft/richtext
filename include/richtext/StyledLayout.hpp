#ifndef RICHTEXT_STYLED_LAYOUT_HPP
#define RICHTEXT_STYLED_LAYOUT_HPP

#include <string>
#include <vector>
#include <map>

#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/TextLayout.hpp"
#include "richtext/ParagraphLayout.hpp"
#include "richtext/TagParser.hpp"
#include "richtext/TimingInfo.hpp"
#include "richtext/LinkRegion.hpp"

namespace richtext {

/**
 * スタイルタグ付きテキストのレイアウト結果
 *
 * drawStyledText() のレイアウト処理を分離したクラス。
 * タグ解析・行分割・セグメント分割の結果を保持し、
 * TextRenderer::drawStyledLayout() で描画する。
 */
class StyledLayout {
public:
    /**
     * セグメントレイアウト情報
     */
    struct SegmentLayout {
        size_t spanIdx;         ///< spans 内のインデックス
        size_t segStart;        ///< plainText 内の開始位置
        size_t segEnd;          ///< plainText 内の終了位置
        float yOffset;          ///< Y方向オフセット
        float measuredWidth;    ///< MeasuredText ベースの幅
        TextLayout layout;      ///< セグメントの TextLayout
    };

    /**
     * 行レイアウト情報
     */
    struct LineLayout {
        size_t lineIdx;                         ///< 行インデックス
        float totalWidth;                       ///< 行の総幅
        std::vector<SegmentLayout> segments;    ///< セグメント配列
    };

    StyledLayout() = default;

    /**
     * TagParser オプションの設定（layout() 前に呼ぶ）
     */
    void setParserOptions(const TagParser::ParseOptions& opts) { parserOptions_ = opts; }

    /**
     * eval タグ用コールバックの設定（layout() 前に呼ぶ）
     */
    void setEvalCallback(EvalCallback cb) { evalCallback_ = std::move(cb); }

    /**
     * レイアウト実行
     * @param text スタイルタグ付きテキスト（UTF-16）
     * @param maxWidth 最大幅
     * @param maxHeight 最大高さ
     * @param hAlign 水平アライン
     * @param vAlign 垂直アライン
     * @param styles スタイル名 → TextStyle のマップ
     * @param appearances スタイル名 → Appearance のマップ
     * @param lineSpacing 行間
     */
    void layout(const std::u16string& text,
                float maxWidth, float maxHeight,
                ParagraphLayout::HAlign hAlign,
                ParagraphLayout::VAlign vAlign,
                const std::map<std::string, TextStyle>& styles,
                const std::map<std::string, Appearance>& appearances,
                float lineSpacing = 0.0f);

    // ------------------------------------------------------------------
    // 結果取得
    // ------------------------------------------------------------------

    /** パース結果の取得 */
    const TagParser::ParseResult& getParsed() const { return parsed_; }

    /** パラグラフレイアウトの取得 */
    const ParagraphLayout& getParagraphLayout() const { return para_; }

    /** 行レイアウト配列の取得 */
    const std::vector<LineLayout>& getLineLayouts() const { return lineLayouts_; }

    /** 行数 */
    size_t getLineCount() const { return lineLayouts_.size(); }

    /** 水平アライン */
    ParagraphLayout::HAlign getHAlign() const { return hAlign_; }

    /** 垂直アライン */
    ParagraphLayout::VAlign getVAlign() const { return vAlign_; }

    /** 最大幅 */
    float getMaxWidth() const { return maxWidth_; }

    /** 最大高さ */
    float getMaxHeight() const { return maxHeight_; }

    /** 総グリフ数 */
    size_t getTotalGlyphCount() const { return totalGlyphCount_; }

    /** 総文字数（ユニーク charIndex 数） */
    size_t getTotalCharCount() const { return totalCharCount_; }

    /** TagParser オプションの取得 */
    const TagParser::ParseOptions& getParserOptions() const { return parserOptions_; }

    /** レイアウト済みかどうか */
    bool isValid() const { return valid_; }

    // ------------------------------------------------------------------
    // メタデータアクセサ（TagParser が生成した情報）
    // ------------------------------------------------------------------

    /** タイミング情報 */
    const std::vector<TimingEntry>& getTimings() const { return parsed_.timings; }

    /** リンク情報 */
    const std::vector<LinkInfo>& getLinks() const { return parsed_.links; }

    /** グラフィック情報 */
    const std::vector<GraphInfo>& getGraphics() const { return parsed_.graphics; }

    /**
     * リンク矩形領域を構築
     * レイアウト完了後に呼び出し、文字位置情報から LinkRegion を生成する。
     */
    std::vector<LinkRegion> buildLinkRegions() const;

private:
    TagParser::ParseResult parsed_;
    ParagraphLayout para_;
    std::vector<LineLayout> lineLayouts_;
    TagParser::ParseOptions parserOptions_;
    EvalCallback evalCallback_;

    ParagraphLayout::HAlign hAlign_ = ParagraphLayout::HAlign::Left;
    ParagraphLayout::VAlign vAlign_ = ParagraphLayout::VAlign::Top;
    float maxWidth_ = 0;
    float maxHeight_ = 0;

    size_t totalGlyphCount_ = 0;
    size_t totalCharCount_ = 0;
    bool valid_ = false;
};

} // namespace richtext

#endif // RICHTEXT_STYLED_LAYOUT_HPP
