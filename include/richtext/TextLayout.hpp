#ifndef RICHTEXT_TEXT_LAYOUT_HPP
#define RICHTEXT_TEXT_LAYOUT_HPP

#include <string>
#include <vector>
#include <memory>

#include <minikin/Layout.h>
#include <minikin/MinikinPaint.h>
#include <minikin/FontCollection.h>

#include "richtext/TextStyle.hpp"

namespace richtext {

class FontFace;

/**
 * グリフ情報
 */
struct GlyphInfo {
    uint32_t glyphId;           // グリフID
    const FontFace* font;       // フォント
    float x;                    // X座標
    float y;                    // Y座標
    float advance;              // アドバンス（送り幅）
    size_t charIndex;           // 元テキストでの文字位置
    minikin::FontFakery fakery; // 擬似スタイル情報
};

/**
 * テキストレイアウト
 * 
 * 1行テキストのレイアウト結果を保持する。
 */
class TextLayout {
public:
    /**
     * デフォルトコンストラクタ
     */
    TextLayout() = default;
    
    /**
     * レイアウト実行
     * @param text UTF-16テキスト
     * @param style テキストスタイル
     */
    void layout(const std::u16string& text, const TextStyle& style);
    
    /**
     * レイアウト実行（ムーブ版）
     * @param text UTF-16テキスト
     * @param style テキストスタイル
     */
    void layout(std::u16string&& text, const TextStyle& style);
    
    // ------------------------------------------------------------------
    // レイアウト結果
    // ------------------------------------------------------------------
    
    /**
     * テキスト幅（ピクセル）
     */
    float getWidth() const { return width_; }
    
    /**
     * テキスト高さ（ascent + descent）
     */
    float getHeight() const { return -ascent_ + descent_; }
    
    /**
     * アセント（ベースラインから上端まで、負の値）
     */
    float getAscent() const { return ascent_; }
    
    /**
     * ディセント（ベースラインから下端まで、正の値）
     */
    float getDescent() const { return descent_; }
    
    /**
     * バウンディングボックス
     */
    struct Bounds {
        float left, top, right, bottom;
        float width() const { return right - left; }
        float height() const { return bottom - top; }
    };
    Bounds getBounds() const { return bounds_; }
    
    // ------------------------------------------------------------------
    // グリフ情報
    // ------------------------------------------------------------------

    /**
     * グリフ数
     */
    size_t getGlyphCount() const { return glyphs_.size(); }

    /**
     * 文字数（ユニーク charIndex 数）
     */
    size_t getCharCount() const;
    
    /**
     * グリフ情報の取得
     * @param index グリフインデックス
     */
    const GlyphInfo& getGlyph(size_t index) const { return glyphs_[index]; }
    
    /**
     * 全グリフ情報
     */
    const std::vector<GlyphInfo>& getGlyphs() const { return glyphs_; }
    
    /**
     * 元テキストの取得
     */
    const std::u16string& getText() const { return text_; }
    
    /**
     * スタイルの取得
     */
    const TextStyle& getStyle() const { return style_; }
    
    // ------------------------------------------------------------------
    // minikin Layout へのアクセス（内部使用）
    // ------------------------------------------------------------------
    
    /**
     * minikin::Layout の取得
     */
    const minikin::Layout& getMinikinLayout() const { return layout_; }

private:
    std::u16string text_;
    minikin::Layout layout_{0};  // デフォルトは空のLayout
    TextStyle style_;
    
    // キャッシュ
    float width_ = 0;
    float ascent_ = 0;
    float descent_ = 0;
    Bounds bounds_ = {};
    std::vector<GlyphInfo> glyphs_;
    
    void doLayout();
    void cacheMetrics(const minikin::MinikinPaint& paint);
    void buildGlyphInfos();
};

} // namespace richtext

#endif // RICHTEXT_TEXT_LAYOUT_HPP
