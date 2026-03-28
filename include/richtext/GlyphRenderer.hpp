#ifndef RICHTEXT_GLYPH_RENDERER_HPP
#define RICHTEXT_GLYPH_RENDERER_HPP

#include <memory>
#include <unordered_map>
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
     * 上下反転用行列の設定（nullptr なら反転なし）
     */
    void setFlipTransform(const tvg::Matrix* flipYMatrix) {
        flipYMatrix_ = flipYMatrix;
    }
    
    /**
     * キャッシュクリア
     */
    void clearCache();

    /**
     * キャッシュ最大サイズ設定（バイト数、0 = 無制限）
     */
    void setCacheMaxSize(size_t bytes) { cacheMaxBytes_ = bytes; }

private:
    tvg::Canvas* canvas_;
    bool useCache_ = true;
    const tvg::Matrix* flipYMatrix_ = nullptr;

    // ------------------------------------------------------------------
    // グリフキャッシュ
    // ------------------------------------------------------------------

    // キャッシュキー: フォントポインタ + グリフID + フォントサイズ + フォント幅
    struct GlyphCacheKey {
        uintptr_t fontPtr;
        uint32_t glyphId;
        uint32_t fontSizeQ;   // fontSize * 64 を uint32_t に変換（固定小数点）
        uint32_t fontWidthQ;  // fontWidth * 64 を uint32_t に変換（固定小数点）

        bool operator==(const GlyphCacheKey& o) const {
            return fontPtr == o.fontPtr && glyphId == o.glyphId
                && fontSizeQ == o.fontSizeQ && fontWidthQ == o.fontWidthQ;
        }
    };

    struct GlyphCacheKeyHash {
        size_t operator()(const GlyphCacheKey& k) const {
            size_t h = k.fontPtr;
            h ^= static_cast<size_t>(k.glyphId) * 2654435761u;
            h ^= static_cast<size_t>(k.fontSizeQ) * 40503u;
            h ^= static_cast<size_t>(k.fontWidthQ) * 16777619u;
            return h;
        }
    };

    // パスキャッシュ
    struct CachedPath {
        std::vector<tvg::PathCommand> commands;
        std::vector<tvg::Point> points;
    };

    std::unordered_map<GlyphCacheKey, CachedPath, GlyphCacheKeyHash> pathCache_;
    std::unordered_map<GlyphCacheKey, GlyphBitmap, GlyphCacheKeyHash> bitmapCache_;

    size_t cacheUsedBytes_ = 0;
    size_t cacheMaxBytes_ = 0;  // 0 = 無制限

    GlyphCacheKey makeKey(const FontFace* font, uint32_t glyphId, float fontSize, float fontWidth = 100.0f) const;

    // キャッシュサイズ超過チェック・クリア
    void evictCacheIfNeeded();

    /**
     * パス描画
     */
    void renderPath(const std::vector<tvg::PathCommand>& commands,
                    const std::vector<tvg::Point>& points,
                    float x, float y,
                    const Appearance& appearance,
                    float skewX = 0.0f,
                    float fakeBoldStroke = 0.0f,
                    float scaleX = 1.0f);

    /**
     * ビットマップ描画（カラー絵文字）
     */
    void renderBitmap(const GlyphBitmap& bitmap,
                      float x, float y,
                      float scale = 1.0f);
};

} // namespace richtext

#endif // RICHTEXT_GLYPH_RENDERER_HPP
