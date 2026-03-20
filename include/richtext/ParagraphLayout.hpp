#ifndef RICHTEXT_PARAGRAPH_LAYOUT_HPP
#define RICHTEXT_PARAGRAPH_LAYOUT_HPP

#include <string>
#include <vector>
#include <memory>

#include <minikin/MeasuredText.h>
#include <minikin/LineBreaker.h>
#include <minikin/FontCollection.h>

#include "richtext/TextStyle.hpp"
#include "richtext/TextLayout.hpp"

namespace richtext {

/**
 * パラグラフレイアウト
 * 
 * 複数行テキスト（パラグラフ）のレイアウト結果を保持する。
 */
class ParagraphLayout {
public:
    /**
     * 水平アライン
     */
    enum class HAlign {
        Left,       // 左揃え
        Center,     // 中央揃え
        Right,      // 右揃え
        Justify     // 両端揃え
    };
    
    /**
     * 垂直アライン
     */
    enum class VAlign {
        Top,        // 上揃え
        Middle,     // 中央揃え
        Bottom      // 下揃え
    };
    
    /**
     * 行分割戦略
     */
    enum class BreakStrategy {
        Greedy,         // 高速（各改行候補で即座に判断）
        HighQuality,    // 高品質（Knuth-Plassアルゴリズム）
        Balanced        // バランス（各行の長さを均等に）
    };
    
    /**
     * スタイル区間
     */
    struct StyleRun {
        size_t start;       // 開始文字位置（UTF-16単位）
        size_t end;         // 終了文字位置（UTF-16単位）
        TextStyle style;    // この区間のスタイル
    };
    
    /**
     * 行情報
     */
    struct LineInfo {
        size_t startIndex;  // 開始文字位置
        size_t endIndex;    // 終了文字位置
        float width;        // 行幅
        float ascent;       // アセント（負の値）
        float descent;      // ディセント（正の値）
        float height() const { return -ascent + descent; }
    };
    
    /**
     * 描画位置
     */
    struct RenderPosition {
        float x;
        float y;
    };
    
    // ------------------------------------------------------------------
    // コンストラクタ・設定
    // ------------------------------------------------------------------
    
    /**
     * デフォルトコンストラクタ
     */
    ParagraphLayout();
    
    /**
     * 行分割戦略の設定
     */
    void setBreakStrategy(BreakStrategy strategy) { breakStrategy_ = strategy; }
    
    /**
     * 行分割戦略の取得
     */
    BreakStrategy getBreakStrategy() const { return breakStrategy_; }
    
    /**
     * 行間（デフォルト: 0）
     */
    void setLineSpacing(float spacing) { lineSpacing_ = spacing; }
    float getLineSpacing() const { return lineSpacing_; }
    
    // ------------------------------------------------------------------
    // レイアウト
    // ------------------------------------------------------------------
    
    /**
     * パラグラフレイアウト実行（単一スタイル）
     * @param text UTF-16テキスト
     * @param maxWidth 最大幅
     * @param style テキストスタイル
     */
    void layout(const std::u16string& text,
                float maxWidth,
                const TextStyle& style);
    
    /**
     * パラグラフレイアウト実行（複数スタイル）
     * @param text UTF-16テキスト
     * @param maxWidth 最大幅
     * @param styleRuns スタイル区間配列
     */
    void layout(const std::u16string& text,
                float maxWidth,
                const std::vector<StyleRun>& styleRuns);
    
    // ------------------------------------------------------------------
    // 結果取得
    // ------------------------------------------------------------------
    
    /**
     * 行数
     */
    size_t getLineCount() const { return lines_.size(); }
    
    /**
     * 全行の高さ合計
     */
    float getTotalHeight() const { return totalHeight_; }
    
    /**
     * 最大行幅
     */
    float getMaxWidth() const { return maxWidth_; }
    
    /**
     * 行情報の取得
     * @param index 行インデックス
     */
    const LineInfo& getLine(size_t index) const { return lines_[index]; }
    
    /**
     * 全行情報
     */
    const std::vector<LineInfo>& getLines() const { return lines_; }
    
    /**
     * 行のレイアウトを取得
     * @param lineIndex 行インデックス
     * @param style この行のスタイル（複数スタイルの場合は最初のスタイル）
     * @return TextLayout
     */
    TextLayout getLineLayout(size_t lineIndex, const TextStyle& style) const;
    
    /**
     * 行の描画位置を計算
     * @param lineIndex 行インデックス
     * @param rectX 描画領域の左端
     * @param rectY 描画領域の上端
     * @param rectWidth 描画領域の幅
     * @param rectHeight 描画領域の高さ
     * @param hAlign 水平アライン
     * @param vAlign 垂直アライン
     * @return 描画位置
     */
    RenderPosition getLinePosition(size_t lineIndex,
                                   float rectX, float rectY,
                                   float rectWidth, float rectHeight,
                                   HAlign hAlign,
                                   VAlign vAlign) const;
    
    /**
     * 元テキストの取得
     */
    const std::u16string& getText() const { return text_; }

private:
    std::u16string text_;
    std::vector<LineInfo> lines_;
    std::vector<StyleRun> styleRuns_;
    
    float totalHeight_ = 0;
    float maxWidth_ = 0;
    float lineSpacing_ = 0;
    BreakStrategy breakStrategy_ = BreakStrategy::HighQuality;
    
    // minikin の結果キャッシュ
    std::unique_ptr<minikin::MeasuredText> measuredText_;
    minikin::LineBreakResult breakResult_;
    
    minikin::BreakStrategy toMinikinStrategy() const;
};

} // namespace richtext

#endif // RICHTEXT_PARAGRAPH_LAYOUT_HPP
