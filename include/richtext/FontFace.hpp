#ifndef RICHTEXT_FONT_FACE_HPP
#define RICHTEXT_FONT_FACE_HPP

#include <string>
#include <vector>
#include <memory>

// FreeType
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

// minikin
#include <minikin/MinikinFont.h>
#include <minikin/FontVariation.h>

// thorvg
#include <thorvg.h>

namespace richtext {

/**
 * グリフビットマップ情報（カラー絵文字用）
 */
struct GlyphBitmap {
    std::vector<uint8_t> data;  // ピクセルデータ (RGBA)
    int width = 0;
    int height = 0;
    int bearingX = 0;           // 原点からの水平オフセット
    int bearingY = 0;           // 原点からの垂直オフセット（上向き正）
    float strikeHeight = 0;     // 固定サイズビットマップの基準高さ（スケール計算用）
};

/**
 * フォントフェイスクラス
 * 
 * FreeType と minikin の橋渡しを行う。
 * minikin::MinikinFont を継承し、グリフメトリクスの取得を提供。
 * また、thorvg 用のパスデータ取得機能も提供。
 */
class FontFace : public minikin::MinikinFont {
public:
    /**
     * コンストラクタ（バッファ方式）
     * @param name フォント名（識別用）
     * @param data フォントデータバッファ（shared_ptr で寿命管理）
     * @param index フォントインデックス（OTC用）
     */
    FontFace(const std::string& name,
             std::shared_ptr<std::vector<uint8_t>> data,
             int index = 0);

    /**
     * コンストラクタ（ストリーム方式）
     * @param name フォント名（識別用）
     * @param stream FreeType ストリーム（FontFace が所有権を取得）
     * @param index フォントインデックス（OTC用）
     */
    FontFace(const std::string& name,
             FT_Stream stream,
             int index = 0);
    
    /**
     * デストラクタ
     */
    virtual ~FontFace();
    
    // ------------------------------------------------------------------
    // MinikinFont インタフェース実装
    // ------------------------------------------------------------------
    
    /**
     * グリフの水平アドバンス（送り幅）を取得
     */
    float GetHorizontalAdvance(uint32_t glyphId,
                               const minikin::MinikinPaint& paint,
                               const minikin::FontFakery& fakery) const override;
    
    /**
     * グリフのバウンディングボックスを取得
     */
    void GetBounds(minikin::MinikinRect* bounds,
                   uint32_t glyphId,
                   const minikin::MinikinPaint& paint,
                   const minikin::FontFakery& fakery) const override;
    
    /**
     * フォント全体のエクステント（ascent/descent）を取得
     */
    void GetFontExtent(minikin::MinikinExtent* extent,
                       const minikin::MinikinPaint& paint,
                       const minikin::FontFakery& fakery) const override;
    
    /**
     * フォントデータへのアクセス
     */
    const void* GetFontData() const override { return fontData_; }
    size_t GetFontSize() const override { return fontDataSize_; }
    int GetFontIndex() const override { return fontIndex_; }
    
    /**
     * バリエーション軸の取得
     */
    const std::vector<minikin::FontVariation>& GetAxes() const override {
        return axes_;
    }
    
    // ------------------------------------------------------------------
    // 追加機能
    // ------------------------------------------------------------------
    
    /**
     * フォントパスの取得
     */
    const std::string& getFontName() const { return fontName_; }

    /**
     * FT_Face の取得（内部使用）
     */
    FT_Face getFTFace() const { return ftFace_; }

    /**
     * バリアブルフォントかどうか
     */
    bool isVariableFont() const { return isVariable_; }

    /**
     * バリエーション軸の設定
     * @param variations 軸タグと値のペア配列
     *
     * 対応軸タグ:
     *   'wght' (weight): 100-900
     *   'wdth' (width): 62.5-100
     *   'ital' (italic): 0 or 1
     */
    void setVariations(const std::vector<minikin::FontVariation>& variations);

    /**
     * wdth 軸の範囲を取得
     * @param minWidth 最小値（出力）
     * @param maxWidth 最大値（出力）
     * @return wdth 軸が存在する場合 true
     */
    bool getWidthAxisRange(float& minWidth, float& maxWidth) const;

    /**
     * wdth 軸の値を一時的に設定する（描画前に呼び出し）
     * @param width 幅（パーセント、100.0 = 通常）
     *
     * 他の軸（wght 等）は setVariations() で設定済みの値を維持する。
     */
    void applyWidth(float width) const;

    /**
     * グリフパスの取得（thorvg用）
     * @param glyphId グリフID
     * @param size フォントサイズ
     * @param commands 出力パスコマンド
     * @param points 出力パス座標
     * @return 成功時 true（カラー絵文字等の場合 false）
     */
    bool getGlyphPath(uint32_t glyphId,
                      float size,
                      std::vector<tvg::PathCommand>& commands,
                      std::vector<tvg::Point>& points) const;
    
    /**
     * グリフビットマップの取得（カラー絵文字用）
     * @param glyphId グリフID
     * @param size フォントサイズ
     * @param bitmap 出力ビットマップ
     * @return 成功時 true
     */
    bool getGlyphBitmap(uint32_t glyphId,
                        float size,
                        GlyphBitmap& bitmap) const;
    
    /**
     * カラーグリフかどうかの判定
     * @param glyphId グリフID
     * @return カラー絵文字の場合 true
     */
    bool isColorGlyph(uint32_t glyphId) const;
    
    /**
     * FT_Face を明示的に解放
     * （shared_ptr の解放順序問題を回避するため）
     */
    void releaseFace();

private:
    std::string fontName_;
    int fontIndex_;

    FT_Face ftFace_ = nullptr;
    const void* fontData_ = nullptr;
    size_t fontDataSize_ = 0;
    std::shared_ptr<std::vector<uint8_t>> fontDataBuffer_;  // バッファ方式
    FT_Stream ftStream_ = nullptr;                          // ストリーム方式
    
    std::vector<minikin::FontVariation> axes_;
    bool isVariable_ = false;
    bool hasWdthAxis_ = false;
    float wdthMin_ = 100.0f;
    float wdthMax_ = 100.0f;
    float wdthDefault_ = 100.0f;
    
    /**
     * FreeType アウトラインを thorvg パスに変換
     */
    static void outlineToPath(FT_Outline* outline,
                              float scale,
                              std::vector<tvg::PathCommand>& commands,
                              std::vector<tvg::Point>& points);

    /**
     * COLRv1 ペイントグラフを走査して RGBA ビットマップを生成
     */
    bool renderCOLRv1Glyph(uint32_t glyphId, float size,
                           GlyphBitmap& bitmap) const;
};

} // namespace richtext

#endif // RICHTEXT_FONT_FACE_HPP
