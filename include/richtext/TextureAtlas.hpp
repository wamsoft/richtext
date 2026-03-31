#ifndef RICHTEXT_TEXTURE_ATLAS_HPP
#define RICHTEXT_TEXTURE_ATLAS_HPP

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>

#include "richtext/TextLayout.hpp"
#include "richtext/ParagraphLayout.hpp"
#include "richtext/StyledLayout.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/TextStyle.hpp"

namespace richtext {
struct RectF;
} // namespace richtext

namespace richtext {

/**
 * 仮想テクスチャインタフェース
 *
 * GPU テクスチャの抽象化。ホスト側が実装して渡す。
 */
class ITexture {
public:
    virtual ~ITexture() = default;

    /** テクスチャ幅 */
    virtual int getWidth() const = 0;
    /** テクスチャ高さ */
    virtual int getHeight() const = 0;

    /**
     * テクスチャの矩形領域に ARGB ピクセルを書き込む
     * @param x 書き込み先X
     * @param y 書き込み先Y
     * @param width 幅
     * @param height 高さ
     * @param pixels ARGB ピクセルデータ
     * @param pitch 1行のバイト数
     */
    virtual void update(int x, int y, int width, int height,
                        const uint32_t* pixels, int pitch) = 0;
};

/**
 * コピー矩形情報（テクスチャアトラスからの転送単位）
 */
struct CopyRect {
    // ソース（テクスチャアトラス内）
    int srcX, srcY, srcWidth, srcHeight;
    // デスティネーション（画面上）
    float dstX, dstY;
    // 表示順序インデックス（逐次表示用）
    int displayIndex;
};

/**
 * テクスチャアトラス
 *
 * グリフを事前レンダリングしてテクスチャに格納し、
 * コピー矩形配列で表示順に転送する。
 */
class TextureAtlas {
public:
    /**
     * コンストラクタ
     * @param texture テクスチャ（所有権は呼び出し側が保持）
     */
    explicit TextureAtlas(ITexture* texture);

    ~TextureAtlas();

    /**
     * アトラスをクリア
     */
    void clear();

    /**
     * TextLayout のグリフをアトラスに追加
     * 同一グリフ（font + glyphId + style + appearance）は重複排除される。
     * @param layout TextLayout
     * @param appearance 描画外観
     * @return 成功時 true
     */
    bool addLayout(const TextLayout& layout,
                   const Appearance& appearance);

    /**
     * ParagraphLayout のグリフをアトラスに追加
     * @param para ParagraphLayout
     * @param style テキストスタイル
     * @param appearance 描画外観
     * @return 成功時 true
     */
    bool addParagraphLayout(const ParagraphLayout& para,
                            const TextStyle& style,
                            const Appearance& appearance);

    /**
     * StyledLayout のグリフをアトラスに追加
     * @param layout StyledLayout
     * @return 成功時 true
     */
    bool addStyledLayout(const StyledLayout& layout);

    /**
     * アトラスをテクスチャに書き込み（確定）
     * addLayout/addParagraphLayout で追加したグリフを
     * テクスチャにレンダリングする。
     */
    void commit();

    /**
     * 表示用コピー矩形配列を生成
     * @param layout TextLayout
     * @param x 描画開始X
     * @param y 描画開始Y（ベースライン）
     * @param appearance 描画外観（キャッシュキーに使用）
     * @param maxGlyphs 最大グリフ数（-1 = 全て）
     * @return コピー矩形配列（表示順）
     */
    std::vector<CopyRect> getCopyRects(const TextLayout& layout,
                                       float x, float y,
                                       const Appearance& appearance,
                                       int maxGlyphs = -1) const;

    /**
     * 表示用コピー矩形配列を生成（パラグラフ）
     * @param para ParagraphLayout
     * @param rect 描画領域
     * @param hAlign 水平アライン
     * @param vAlign 垂直アライン
     * @param style テキストスタイル
     * @param appearance 描画外観（キャッシュキーに使用）
     * @param maxGlyphs 最大グリフ数（-1 = 全て）
     * @return コピー矩形配列（表示順）
     */
    std::vector<CopyRect> getCopyRects(const ParagraphLayout& para,
                                       const RectF& rect,
                                       ParagraphLayout::HAlign hAlign,
                                       ParagraphLayout::VAlign vAlign,
                                       const TextStyle& style,
                                       const Appearance& appearance,
                                       int maxGlyphs = -1) const;

    /**
     * 表示用コピー矩形配列を生成（StyledLayout）
     * @param layout StyledLayout
     * @param x 描画開始X
     * @param y 描画開始Y
     * @param maxGlyphs 最大グリフ数（-1 = 全て）
     * @return コピー矩形配列（表示順）
     */
    std::vector<CopyRect> getCopyRects(const StyledLayout& layout,
                                       float x, float y,
                                       int maxGlyphs = -1) const;

private:
    ITexture* texture_;

    // グリフ配置マップ（重複排除用）
    // キー: font + glyphId + fontSize + fontWidth + fakery + appearance
    struct GlyphKey {
        uintptr_t fontPtr;
        uint32_t glyphId;
        uint32_t fontSizeQ;       // fontSize * 64 で量子化
        uint32_t fontWidthQ;      // fontWidth * 64 で量子化
        uint8_t fakeryFlags;      // bit0: fakeBold, bit1: fakeItalic
        size_t appearanceHash;    // Appearance のハッシュ値
        bool operator==(const GlyphKey& o) const {
            return fontPtr == o.fontPtr && glyphId == o.glyphId
                && fontSizeQ == o.fontSizeQ && fontWidthQ == o.fontWidthQ
                && fakeryFlags == o.fakeryFlags
                && appearanceHash == o.appearanceHash;
        }
    };
    struct GlyphKeyHash {
        size_t operator()(const GlyphKey& k) const {
            size_t h = k.fontPtr;
            h ^= static_cast<size_t>(k.glyphId) * 2654435761u;
            h ^= static_cast<size_t>(k.fontSizeQ) * 40503u;
            h ^= static_cast<size_t>(k.fontWidthQ) * 16777619u;
            h ^= static_cast<size_t>(k.fakeryFlags) * 2246822519u;
            h ^= k.appearanceHash * 13;
            return h;
        }
    };

    struct AtlasEntry {
        int atlasX, atlasY;
        int atlasWidth, atlasHeight;
        // グリフ原点からのオフセット（Appearance の装飾分を含む）
        float offsetX, offsetY;
    };

    std::unordered_map<GlyphKey, AtlasEntry, GlyphKeyHash> glyphMap_;

    // Shelf packing
    int currentX_ = 0;
    int currentY_ = 0;
    int shelfHeight_ = 0;
    int padding_ = 1;

    // 内部レンダリングバッファ
    std::vector<uint32_t> renderBuffer_;
    int bufferWidth_ = 0;
    int bufferHeight_ = 0;

    GlyphKey makeKey(const GlyphInfo& glyph, const TextStyle& style,
                     const Appearance& appearance) const;
    bool allocateRect(int width, int height, int& outX, int& outY);
    bool renderGlyphToAtlas(const GlyphInfo& glyph,
                            const TextStyle& style,
                            const Appearance& appearance);
};

} // namespace richtext

#endif // RICHTEXT_TEXTURE_ATLAS_HPP
