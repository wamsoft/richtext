#ifndef RICHTEXT_GLYPH_RENDERER_HPP
#define RICHTEXT_GLYPH_RENDERER_HPP

#include <memory>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "richtext/Raster.hpp"
#include "richtext/TextLayout.hpp"
#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/FontFace.hpp"

namespace richtext {

/**
 * グリフレンダラ
 *
 * グリフ単位の描画処理を行う。ベクターの塗り／縁取りはフォントバックエンドに
 * カバレッジマスクを作らせ、それを RenderTarget へ合成する。
 */
class GlyphRenderer {
public:
    /**
     * コンストラクタ
     * @param target 描画先
     */
    explicit GlyphRenderer(const RenderTarget& target);

    /**
     * デストラクタ
     */
    ~GlyphRenderer();

    /**
     * 描画先の差し替え
     */
    void setTarget(const RenderTarget& target) { target_ = target; }

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

    /**
     * グリフ列を描画
     *
     * GlyphInfo の x/y は基準点からの相対座標として扱う。横組みでは
     * (x, y) = 行頭・ベースライン、縦組みでは (x, y) = 縦ベースライン・行頭。
     *
     * @param glyphs グリフ列
     * @param x 基準点X
     * @param y 基準点Y
     * @param style テキストスタイル
     * @param appearance 描画外観
     */
    void renderGlyphs(const std::vector<GlyphInfo>& glyphs,
                      float x, float y,
                      const TextStyle& style,
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
    void setFlipTransform(const Matrix2D* flipYMatrix) {
        flipYMatrix_ = flipYMatrix;
    }

    /**
     * 描画変換行列の設定（nullptr なら変換なし）
     * 全グリフに適用される。flipYMatrix と併用する場合は
     * flipYMatrix が先に適用され、その後 transform が適用される。
     */
    void setTransform(const Matrix2D* transform) {
        transform_ = transform;
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
    RenderTarget target_;
    bool useCache_ = true;
    const Matrix2D* flipYMatrix_ = nullptr;
    const Matrix2D* transform_ = nullptr;

    // ------------------------------------------------------------------
    // グリフキャッシュ
    // ------------------------------------------------------------------

    // マスクは変形を焼き込んだ結果なので、キーは変形の 2x2 成分と
    // サブピクセル位相まで含める（平行移動の整数部だけがキャッシュ間で共有される）
    struct GlyphCacheKey {
        uintptr_t fontPtr;
        uint32_t glyphId;
        uint32_t fontSizeQ;    // fontSize * 64
        uint32_t fontWidthQ;   // fontWidth * 64
        uint32_t strokeQ;      // strokeWidth * 64（0 = 塗り）
        int32_t xxQ, xyQ, yxQ, yyQ;   // 変形の 2x2 成分 * 4096
        uint8_t phaseX, phaseY;       // サブピクセル位相（1/4 px）
        uint8_t flags;                // bit0: bold, bit1: italic

        bool operator==(const GlyphCacheKey& o) const {
            return fontPtr == o.fontPtr && glyphId == o.glyphId
                && fontSizeQ == o.fontSizeQ && fontWidthQ == o.fontWidthQ
                && strokeQ == o.strokeQ
                && xxQ == o.xxQ && xyQ == o.xyQ && yxQ == o.yxQ && yyQ == o.yyQ
                && phaseX == o.phaseX && phaseY == o.phaseY && flags == o.flags;
        }
    };

    struct GlyphCacheKeyHash {
        size_t operator()(const GlyphCacheKey& k) const {
            size_t h = k.fontPtr;
            h ^= static_cast<size_t>(k.glyphId) * 2654435761u;
            h ^= static_cast<size_t>(k.fontSizeQ) * 40503u;
            h ^= static_cast<size_t>(k.fontWidthQ) * 16777619u;
            h ^= static_cast<size_t>(k.strokeQ) * 2246822519u;
            h ^= static_cast<size_t>(k.xxQ) * 3266489917u;
            h ^= static_cast<size_t>(k.yyQ) * 668265263u;
            h ^= static_cast<size_t>(k.xyQ) * 374761393u;
            h ^= static_cast<size_t>(k.yxQ) * 2654435761u;
            h ^= static_cast<size_t>(k.phaseX) * 31u;
            h ^= static_cast<size_t>(k.phaseY) * 131u;
            h ^= static_cast<size_t>(k.flags) * 8191u;
            return h;
        }
    };

    /// ラスタライズ済みカバレッジマスク（実体を保持する版）
    struct CachedMask {
        std::vector<uint8_t> coverage;
        int left = 0;      // ペン原点からのオフセット（y 上向き正）
        int top = 0;
        int width = 0;
        int rows = 0;
    };

    // ビットマップキャッシュ用の簡易キー（カラー絵文字はサイズのみで決まる）
    struct BitmapCacheKey {
        uintptr_t fontPtr;
        uint32_t glyphId;
        uint32_t fontSizeQ;
        bool operator==(const BitmapCacheKey& o) const {
            return fontPtr == o.fontPtr && glyphId == o.glyphId && fontSizeQ == o.fontSizeQ;
        }
    };
    struct BitmapCacheKeyHash {
        size_t operator()(const BitmapCacheKey& k) const {
            size_t h = k.fontPtr;
            h ^= static_cast<size_t>(k.glyphId) * 2654435761u;
            h ^= static_cast<size_t>(k.fontSizeQ) * 40503u;
            return h;
        }
    };

    std::unordered_map<GlyphCacheKey, CachedMask, GlyphCacheKeyHash> maskCache_;
    std::unordered_map<BitmapCacheKey, GlyphBitmap, BitmapCacheKeyHash> bitmapCache_;

    size_t cacheUsedBytes_ = 0;
    size_t cacheMaxBytes_ = 0;  // 0 = 無制限

    // キャッシュサイズ超過チェック・クリア
    void evictCacheIfNeeded();

    /**
     * グリフ 1 つ分のマスクを描画先へ合成する
     *
     * @param penX,penY ペン位置（描画先ピクセル座標、ベースライン）
     * @param glyphMat  グリフ固有の変形（スケール・シアー等、y-down）
     * @param strokeWidth 0 = 塗り、>0 = 縁取り
     */
    void blendGlyph(const FontFace* font, uint32_t glyphId,
                    float penX, float penY,
                    const Matrix2D& glyphMat,
                    float strokeWidth,
                    const DrawStyle* strokeStyle,
                    bool fakeBold, bool fakeItalic,
                    float fontSize, float fontWidth,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /**
     * ビットマップ描画（カラー絵文字）
     */
    void renderBitmap(const GlyphBitmap& bitmap,
                      float x, float y,
                      float scale = 1.0f);
};

} // namespace richtext

#endif // RICHTEXT_GLYPH_RENDERER_HPP
