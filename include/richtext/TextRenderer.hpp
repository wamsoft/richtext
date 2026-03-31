#ifndef RICHTEXT_TEXT_RENDERER_HPP
#define RICHTEXT_TEXT_RENDERER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

#include <thorvg.h>

#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/TextLayout.hpp"
#include "richtext/ParagraphLayout.hpp"
#include "richtext/StyledLayout.hpp"

namespace richtext {

class GlyphRenderer;
class GlyphCache;
class PathCache;

/**
 * 矩形
 */
struct RectF {
    float x = 0, y = 0, width = 0, height = 0;
    
    RectF() = default;
    RectF(float x, float y, float w, float h) : x(x), y(y), width(w), height(h) {}
    
    float left() const { return x; }
    float top() const { return y; }
    float right() const { return x + width; }
    float bottom() const { return y + height; }
};

/**
 * グリフ描画情報（順次表示用）
 */
struct GlyphRenderInfo {
    size_t charIndex;       // 元テキストでの文字位置
    float x, y;             // 描画位置
    float width, height;    // サイズ
    float advance;          // 次の文字までの距離
    bool isEmoji;           // カラー絵文字かどうか
};

/**
 * テキストレンダラ
 * 
 * テキスト描画の統合インタフェース。
 */
class TextRenderer {
public:
    /**
     * コンストラクタ
     */
    TextRenderer();
    
    /**
     * デストラクタ
     */
    ~TextRenderer();
    
    // ------------------------------------------------------------------
    // 初期化・設定
    // ------------------------------------------------------------------
    
    /**
     * 描画先キャンバスの設定
     * @param buffer ピクセルバッファ（ARGB形式）
     * @param width 幅
     * @param height 高さ
     * @param pitch ピッチ（1行のバイト数、負の値で上下反転）
     */
    void setCanvas(uint32_t* buffer, int width, int height, int pitch);
    
    /**
     * キャンバスのクリア
     * @param color クリア色（ARGB）
     */
    void clearCanvas(uint32_t color = 0);
    
    /**
     * 描画の同期（thorvg の描画完了を待機）
     */
    void sync();
    
    // ------------------------------------------------------------------
    // キャッシュ制御
    // ------------------------------------------------------------------
    
    /**
     * グリフキャッシュの使用有無
     */
    void setUseCache(bool use);
    bool getUseCache() const;
    
    /**
     * キャッシュのクリア
     */
    void clearCache();
    
    /**
     * キャッシュの最大サイズ設定
     * @param bytes 最大バイト数
     */
    void setCacheMaxSize(size_t bytes);
    
    // ------------------------------------------------------------------
    // 描画メソッド
    // ------------------------------------------------------------------
    
    /**
     * 1行テキストの描画
     * @param text UTF-16テキスト
     * @param x 描画開始X座標
     * @param y 描画開始Y座標（ベースライン）
     * @param style テキストスタイル
     * @param appearance 描画外観
     * @return 描画領域
     */
    RectF drawText(const std::u16string& text,
                   float x, float y,
                   const TextStyle& style,
                   const Appearance& appearance);
    
    /**
     * レイアウト済みテキストの描画
     * @param layout TextLayout
     * @param x 描画開始X座標
     * @param y 描画開始Y座標（ベースライン）
     * @param appearance 描画外観
     * @return 描画領域
     */
    RectF drawLayout(const TextLayout& layout,
                     float x, float y,
                     const Appearance& appearance,
                     int maxGlyphs = -1);
    
    /**
     * パラグラフレイアウト済みの描画（単一スタイル）
     * @param para ParagraphLayout（事前計算済み）
     * @param rect 描画領域
     * @param hAlign 水平アライン
     * @param vAlign 垂直アライン
     * @param style テキストスタイル
     * @param appearance 描画外観
     * @param maxGlyphs 描画するグリフ数上限（-1 = 全て）
     * @return 実際に描画した領域
     */
    RectF drawParagraphLayout(const ParagraphLayout& para,
                               const RectF& rect,
                               ParagraphLayout::HAlign hAlign,
                               ParagraphLayout::VAlign vAlign,
                               const TextStyle& style,
                               const Appearance& appearance,
                               int maxGlyphs = -1);

    /**
     * パラグラフの描画（単一スタイル）
     * @param text UTF-16テキスト
     * @param rect 描画領域
     * @param hAlign 水平アライン
     * @param vAlign 垂直アライン
     * @param style テキストスタイル
     * @param appearance 描画外観
     * @return 実際に描画した領域
     */
    RectF drawParagraph(const std::u16string& text,
                        const RectF& rect,
                        ParagraphLayout::HAlign hAlign,
                        ParagraphLayout::VAlign vAlign,
                        const TextStyle& style,
                        const Appearance& appearance);
    
    /**
     * パラグラフの描画（複数スタイル）
     * @param text UTF-16テキスト
     * @param rect 描画領域
     * @param hAlign 水平アライン
     * @param vAlign 垂直アライン
     * @param styleRuns スタイル区間配列
     * @param defaultAppearance デフォルト外観
     * @return 実際に描画した領域
     */
    RectF drawParagraph(const std::u16string& text,
                        const RectF& rect,
                        ParagraphLayout::HAlign hAlign,
                        ParagraphLayout::VAlign vAlign,
                        const std::vector<ParagraphLayout::StyleRun>& styleRuns,
                        const Appearance& defaultAppearance);
    
    /**
     * スタイルタグ付きテキストの描画
     * タグ形式: <style:スタイル名>テキスト</style>
     * @param text スタイルタグ付きテキスト
     * @param rect 描画領域
     * @param hAlign 水平アライン
     * @param vAlign 垂直アライン
     * @param styles スタイル名 → TextStyle のマップ
     * @param appearances スタイル名 → Appearance のマップ
     * @return 実際に描画した領域
     */
    RectF drawStyledText(const std::u16string& text,
                         const RectF& rect,
                         ParagraphLayout::HAlign hAlign,
                         ParagraphLayout::VAlign vAlign,
                         const std::map<std::string, TextStyle>& styles,
                         const std::map<std::string, Appearance>& appearances,
                         float lineSpacing = 0.0f);

    /**
     * StyledLayout を使ったスタイルタグ付きテキストの描画
     * @param layout レイアウト済み StyledLayout
     * @param x 描画開始X座標
     * @param y 描画開始Y座標
     * @param maxGlyphs 描画するグリフ数上限（-1 = 全て）
     * @return 実際に描画した領域
     */
    RectF drawStyledLayout(const StyledLayout& layout,
                           float x, float y,
                           int maxGlyphs = -1);
    
    /**
     * 矩形描画
     * @param x X座標
     * @param y Y座標
     * @param width 幅
     * @param height 高さ
     * @param fillColor 塗り色（ARGB）
     * @param strokeColor ストローク色（ARGB、0で無効）
     * @param strokeWidth ストローク幅
     */
    void drawRect(float x, float y, float width, float height,
                  uint32_t fillColor, uint32_t strokeColor = 0,
                  float strokeWidth = 0);
    
    // ------------------------------------------------------------------
    // グリフ情報取得（順次表示用）
    // ------------------------------------------------------------------
    
    /**
     * グリフ情報の取得
     * @param layout TextLayout
     * @param x 基準X座標
     * @param y 基準Y座標
     * @return グリフ情報配列
     */
    std::vector<GlyphRenderInfo> getGlyphInfos(const TextLayout& layout,
                                               float x, float y);
    
    /**
     * 個別グリフの描画
     * @param layout TextLayout
     * @param glyphIndex グリフインデックス
     * @param x 基準X座標
     * @param y 基準Y座標
     * @param appearance 描画外観
     */
    void drawGlyph(const TextLayout& layout,
                   size_t glyphIndex,
                   float x, float y,
                   const Appearance& appearance);

private:
    std::unique_ptr<tvg::SwCanvas> canvas_;
    std::unique_ptr<GlyphRenderer> glyphRenderer_;
    
    int canvasWidth_ = 0;
    int canvasHeight_ = 0;
    tvg::Matrix flipYMatrix_ = {
        1.0f, 0.0f, 0.0f,
        0.0f, -1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    const tvg::Matrix* flipYMatrixPtr_ = nullptr;
    
    bool useCache_ = true;
};

} // namespace richtext

#endif // RICHTEXT_TEXT_RENDERER_HPP
