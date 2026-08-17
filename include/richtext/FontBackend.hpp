#ifndef RICHTEXT_FONT_BACKEND_HPP
#define RICHTEXT_FONT_BACKEND_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * FontBackend — グリフ供給層の差し替え口
 *
 * richtext はフォントの実体（FreeType の FT_Face 等）を直接持たず、この
 * インタフェース越しにメトリクス・アウトライン・ビットマップを取得する。
 *
 * 実装は 2 系統を想定する:
 *
 *  1. 自前初期化: glyphware を直接リンクする GlyphwareFontBackend
 *     （`RICHTEXT_USE_GLYPHWARE` ビルド時の既定。FontManager が
 *       登録済みの FontDataLoader をバイト供給に使って生成する）
 *  2. ホスト注入: 吉里吉里本体が持つ統一フォントエンジンを
 *     プラグインが FontServiceIntf 経由で包んだもの
 *     （`FontManager::setFontBackend()` で差し込む）
 *
 * 2 の場合、フォントバイト列も face も本体と共有されるので、本体・thorvg・
 * richtext で FT_Face とオンメモリバッファが 1 つに統一される。
 */
namespace richtext {

/**
 * アウトライン受け取り
 *
 * 座標は**フォントユニット**（y-up、FreeType 格納系）。ピクセルへは
 * pixelSize / unitsPerEm 倍で変換する。
 */
class FontOutlineSink {
public:
    virtual ~FontOutlineSink() = default;
    virtual void moveTo(float x, float y) = 0;
    virtual void lineTo(float x, float y) = 0;
    virtual void quadTo(float cx, float cy, float x, float y) = 0;
    virtual void cubicTo(float c1x, float c1y, float c2x, float c2y,
                         float x, float y) = 0;
    virtual void close() = 0;
};

/**
 * face 全体のメトリクス
 *
 * *Units はフォントユニット（サイズ非依存）。ascender は正、descender は負
 * （FreeType 慣習）。
 */
struct FontFaceMetrics {
    float unitsPerEm = 1000.0f;
    float ascenderUnits = 0.0f;
    float descenderUnits = 0.0f;
    float heightUnits = 0.0f;
};

/**
 * グリフ単位のメトリクス
 *
 * getGlyphMetrics() ではピクセル、getGlyphMetricsUnscaled() ではフォント
 * ユニット。bearingY は上向き正。
 */
struct FontGlyphMetrics {
    float advanceX = 0.0f;
    float advanceY = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/**
 * ラスタライズ済みグリフの参照
 *
 * buffer は同一 face への次のグリフ取得呼び出しまでのみ有効。保持するなら
 * コピーすること。
 */
enum class FontBitmapFormat { Gray, BGRA };
struct FontGlyphBitmapView {
    FontBitmapFormat format = FontBitmapFormat::Gray;
    int left = 0;       // 原点からの水平オフセット
    int top = 0;        // 原点からの垂直オフセット（上向き正）
    int width = 0;
    int rows = 0;
    int pitch = 0;      // バイト/行
    const uint8_t* buffer = nullptr;
};

/**
 * バリアブルフォントの軸座標
 */
struct FontVarCoord {
    uint32_t tag = 0;   // ビッグエンディアン詰めタグ（'wght' 等）
    float value = 0.0f;
};

/**
 * カラーグリフ（COLR v0/v1）のレイヤー
 *
 * カラー絵文字のうち、CBDT/sbix のようなビットマップではなくベクターで
 * 定義されているもの（COLR）は、「アウトライン + 塗り」のレイヤー列に
 * 展開して自前のラスタライザで描ける。展開はバックエンドが行う。
 *
 * 座標系は FreeType 準拠の y-up。アウトラインはフォントユニット、transform と
 * グラデーション座標は**現在のピクセルサイズ**（transform がフォントユニット→
 * ピクセルのスケールを含む）。
 */
struct FontColorStop {
    float offset = 0.0f;
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

enum class FontPaintKind { Solid, LinearGradient, RadialGradient };

struct FontColorPaint {
    FontPaintKind kind = FontPaintKind::Solid;
    uint8_t r = 0, g = 0, b = 0, a = 255;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float r0 = 0.0f, r1 = 0.0f;
    std::vector<FontColorStop> stops;
};

/// transform は行優先 2x3: {xx, xy, dx, yx, yy, dy}
struct FontColorLayer {
    uint32_t glyphId = 0;
    float transform[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    FontColorPaint paint;
};

/// カラーグリフのクリップボックス（ピクセル、y-up）
struct FontColorGlyphBox {
    float xMin = 0.0f, yMin = 0.0f, xMax = 0.0f, yMax = 0.0f;
    bool valid = false;
};

/**
 * 1 フォントフェイス
 *
 * ピクセルサイズは face の状態なので、グリフ取得系はいずれも pixelSize を
 * 引数で受け取り、実装側が呼び出しごとに設定する（複数の利用者で face を
 * 共有しても取り違えないようにするため）。
 */
class FontBackendFace {
public:
    virtual ~FontBackendFace() = default;

    // --- メタデータ ---
    virtual const std::string& familyName() const = 0;
    virtual const std::string& styleName() const = 0;
    virtual bool isColorFont() const = 0;
    virtual bool isScalable() const = 0;
    virtual FontFaceMetrics faceMetrics() const = 0;

    /**
     * SFNT バイト列（minikin が自前の hb_face を作るのに使う）
     * 取得できない場合 data が nullptr。
     */
    virtual const void* fontData() const = 0;
    virtual size_t fontDataSize() const = 0;
    virtual int faceIndex() const = 0;

    // --- グリフ ---
    /**
     * ピクセル単位のメトリクス
     * @param unhinted true でヒンティング無し（リニアなアドバンス）。
     *                 レイアウト用途では必ず true にすること
     */
    virtual bool getGlyphMetrics(uint32_t glyphId, float pixelSize,
                                 bool bold, bool italic, bool unhinted,
                                 FontGlyphMetrics& out) const = 0;

    /**
     * フォントユニット単位のメトリクス（サイズ非依存）
     * ビットマップ専用フォントでは false
     */
    virtual bool getGlyphMetricsUnscaled(uint32_t glyphId, bool bold, bool italic,
                                         FontGlyphMetrics& out) const = 0;

    /**
     * アウトライン取得（フォントユニット、y-up）
     * カラー絵文字等アウトラインを持たないグリフでは false
     */
    virtual bool getGlyphOutline(uint32_t glyphId, bool bold, bool italic,
                                 FontOutlineSink& sink) const = 0;

    /**
     * ラスタライズ
     * @param color true でカラー絵文字（BGRA）を要求
     */
    virtual bool getGlyphBitmap(uint32_t glyphId, float pixelSize, bool color,
                                bool bold, bool italic,
                                FontGlyphBitmapView& out) = 0;

    /**
     * コードポイント→グリフID（0 = 未収録）
     */
    virtual uint32_t getGlyphIndex(char32_t codepoint) const = 0;

    // --- バリアブルフォント ---
    virtual bool isVariableFont() const = 0;

    /// 全軸とその既定値（バリアブルフォントでなければ空）
    virtual std::vector<FontVarCoord> getAxes() const = 0;

    virtual bool setVariations(const std::vector<FontVarCoord>& coords) = 0;
    virtual bool getAxisRange(uint32_t tag, float& minValue, float& defaultValue,
                              float& maxValue) const = 0;

    /**
     * カラーグリフ（COLR v0/v1）を描画レイヤー列に展開する
     *
     * @param pixelSize transform / box のスケール基準
     * @param out 背面から前面の順のレイヤー列
     * @param box グリフのクリップボックス（不要なら nullptr）
     * @return ペイントグラフを持たないグリフでは false
     *         （CBDT/sbix はビットマップなので getGlyphBitmap を使う）
     */
    virtual bool getColorLayers(uint32_t glyphId, float pixelSize,
                                std::vector<FontColorLayer>& out,
                                FontColorGlyphBox* box) = 0;
};

/**
 * フォントバックエンド（face の生成口）
 */
class FontBackend {
public:
    virtual ~FontBackend() = default;

    /**
     * フォントを開く
     * @param key フォント識別子（ローダーに渡されるファイル名／本体側のフォントキー）
     * @param index フォントインデックス（OTC/TTC 用）
     * @return 失敗時 nullptr
     *
     * バリアブルフォントのインスタンス（wght 違い等）は face の状態なので、
     * 呼び出しごとに**独立した face** を返すこと（共有すると軸設定が競合する）。
     */
    virtual std::shared_ptr<FontBackendFace> openFace(const std::string& key,
                                                      int index) = 0;
};

} // namespace richtext

#endif // RICHTEXT_FONT_BACKEND_HPP
