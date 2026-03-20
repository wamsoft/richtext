#ifndef RICHTEXT_GLYPH_RENDERER_HPP
#define RICHTEXT_GLYPH_RENDERER_HPP

#include <memory>
#include <vector>
#include <cstdint>

#include <thorvg.h>

#include "richtext/TextLayout.hpp"
#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/FontFace.hpp"

namespace richtext {

/**
 * グリフレンダラ
 * 
 * グリフ単位の描画処理を行う。
 */
class GlyphRenderer {
public:
    /**
     * コンストラクタ
     * @param canvas thorvg キャンバス
     */
    explicit GlyphRenderer(tvg::Canvas* canvas);
    
    /**
     * デストラクタ
     */
    ~GlyphRenderer();
    
    // ------------------------------------------------------------------
    // グリフ描画
    // ------------------------------------------------------------------
    
    /**
     * グリフを描画
     * @param glyph グリフ情報
     * @param x 描画位置X
     * @param y 描画位置Y（ベースライン）
     * @param style テキストスタイル
     * @param appearance 描画外観
     */
    void renderGlyph(const GlyphInfo& glyph,
                     float x, float y,
                     const TextStyle& style,
                     const Appearance& appearance);
    
    /**
     * レイアウト全体を描画
     * @param layout TextLayout
     * @param x 描画開始X
     * @param y 描画開始Y（ベースライン）
     * @param appearance 描画外観
     */
    void renderLayout(const TextLayout& layout,
                      float x, float y,
                      const Appearance& appearance);
    
    // ------------------------------------------------------------------
    // キャッシュ制御
    // ------------------------------------------------------------------
    
    /**
     * キャッシュ使用の有無
     */
    void setUseCache(bool use) { useCache_ = use; }
    bool getUseCache() const { return useCache_; }
    
    /**
     * キャッシュクリア
     */
    void clearCache();

private:
    tvg::Canvas* canvas_;
    bool useCache_ = true;
    
    /**
     * パス描画
     */
    void renderPath(const std::vector<tvg::PathCommand>& commands,
                    const std::vector<tvg::Point>& points,
                    float x, float y,
                    const Appearance& appearance);
    
    /**
     * ビットマップ描画（カラー絵文字）
     */
    void renderBitmap(const GlyphBitmap& bitmap,
                      float x, float y,
                      float scale = 1.0f);
};

} // namespace richtext

#endif // RICHTEXT_GLYPH_RENDERER_HPP
