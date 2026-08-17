#ifndef RICHTEXT_RASTER_HPP
#define RICHTEXT_RASTER_HPP

#include <cstdint>
#include <vector>

/**
 * Raster — ARGB8888 バッファへの合成
 *
 * ベクター → カバレッジマスクの変換はフォントバックエンド（glyphware =
 * FreeType のラスタライザ）が行う。ここはそのマスクを色で塗って合成する層で、
 * 外部のベクターグラフィックスエンジンには依存しない。
 *
 * 座標系はピクセル・y-down。色は非前乗算 ARGB8888。
 */
namespace richtext {

/**
 * 2x3 アフィン行列（行優先、y-down ピクセル空間）
 *   x' = e11*x + e12*y + e13
 *   y' = e21*x + e22*y + e23
 */
struct Matrix2D {
    float e11 = 1.0f, e12 = 0.0f, e13 = 0.0f;
    float e21 = 0.0f, e22 = 1.0f, e23 = 0.0f;

    static Matrix2D identity() { return Matrix2D{}; }
    bool isIdentity() const {
        return e11 == 1.0f && e12 == 0.0f && e13 == 0.0f &&
               e21 == 0.0f && e22 == 1.0f && e23 == 0.0f;
    }
};

/// a を後から適用する合成（結果 = a * b）
Matrix2D multiply(const Matrix2D& a, const Matrix2D& b);

/// 線端キャップ
enum class StrokeCap { Butt, Round, Square };

/// 線結合
enum class StrokeJoin { Miter, Round, Bevel };

/// グラデーションのカラーストップ
struct ColorStop {
    float offset = 0.0f;
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

/// 塗り（単色 or グラデーション）
enum class PaintType { Solid, LinearGradient, RadialGradient };

struct Paint {
    PaintType type = PaintType::Solid;
    uint8_t r = 0, g = 0, b = 0, a = 255;
    // Linear: (x0,y0)-(x1,y1)   Radial: 焦点(x0,y0,r0) と 中心(x1,y1,r1)
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float r0 = 0.0f, r1 = 0.0f;
    std::vector<ColorStop> stops;

    static Paint solid(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        Paint p; p.r = r; p.g = g; p.b = b; p.a = a; return p;
    }
};

/**
 * 描画先バッファ
 *
 * バッファの所有権は持たない。stride はピクセル単位。
 */
class RenderTarget {
public:
    RenderTarget() = default;
    RenderTarget(uint32_t* pixels, int width, int height, int stridePixels)
        : pixels_(pixels), width_(width), height_(height), stride_(stridePixels) {}

    bool valid() const { return pixels_ != nullptr && width_ > 0 && height_ > 0; }
    uint32_t* pixels() const { return pixels_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }

    uint32_t* row(int y) const { return pixels_ + static_cast<ptrdiff_t>(stride_) * y; }

    /// 全面を色で埋める（合成せず上書き）
    void clear(uint32_t argb);

    /// 矩形を SRC_OVER 合成
    void fillRect(int x, int y, int w, int h, uint32_t argb);

    /// 矩形を SRC_OVER 合成（サブピクセル境界はカバレッジで按分）
    void fillRectF(float x, float y, float w, float h, uint32_t argb);

    /**
     * 8bit カバレッジマスクを単色で SRC_OVER 合成
     * @param originX マスク左端の X 座標
     * @param originY マスク上端の Y 座標
     */
    void blendMask(const uint8_t* mask, int maskW, int maskH, int maskPitch,
                   int originX, int originY, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /**
     * 8bit カバレッジマスクを Paint（単色/グラデーション）で SRC_OVER 合成
     * グラデーション座標は描画先のピクセル座標系で解釈する。
     */
    void blendMask(const uint8_t* mask, int maskW, int maskH, int maskPitch,
                   int originX, int originY, const Paint& paint);

    /**
     * RGBA8888 画像をアフィン変換して SRC_OVER 合成（バイリニア補間）
     * @param transform 画像ピクセル座標 → 描画先座標
     */
    void blendImage(const uint8_t* rgba, int imgW, int imgH,
                    const Matrix2D& transform, uint8_t alpha = 255);

private:
    uint32_t* pixels_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;
};

} // namespace richtext

#endif // RICHTEXT_RASTER_HPP
