/**
 * Raster.cpp
 *
 * ARGB8888 バッファへの合成（マスク / 矩形 / 画像）
 */

#include "richtext/Raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace richtext {

namespace {

inline uint32_t pack(uint32_t a, uint32_t r, uint32_t g, uint32_t b) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/// SRC_OVER（非前乗算 ARGB、src のアルファは事前に係数を掛けた値を渡す）
inline void blendPixel(uint32_t& dst, uint32_t sr, uint32_t sg, uint32_t sb, uint32_t sa) {
    if (sa == 0) return;
    if (sa == 255) {
        dst = pack(255, sr, sg, sb);
        return;
    }
    const uint32_t da = (dst >> 24) & 0xFF;
    const uint32_t dr = (dst >> 16) & 0xFF;
    const uint32_t dg = (dst >> 8) & 0xFF;
    const uint32_t db = dst & 0xFF;
    const uint32_t ia = 255 - sa;
    // 非前乗算のままの合成: 出力色 = (src*sa + dst*da*ia/255) / outA
    const uint32_t contrib = da * ia / 255;
    const uint32_t outA = sa + contrib;
    if (outA == 0) { dst = 0; return; }
    const uint32_t outR = (sr * sa + dr * contrib) / outA;
    const uint32_t outG = (sg * sa + dg * contrib) / outA;
    const uint32_t outB = (sb * sa + db * contrib) / outA;
    dst = pack(outA, outR, outG, outB);
}

/// カラーストップ列から offset 位置の色を線形補間で得る
void sampleStops(const std::vector<ColorStop>& stops, float t,
                 uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    if (stops.empty()) { r = g = b = 0; a = 0; return; }
    if (t <= stops.front().offset) {
        const ColorStop& s = stops.front();
        r = s.r; g = s.g; b = s.b; a = s.a;
        return;
    }
    if (t >= stops.back().offset) {
        const ColorStop& s = stops.back();
        r = s.r; g = s.g; b = s.b; a = s.a;
        return;
    }
    for (size_t i = 1; i < stops.size(); ++i) {
        const ColorStop& s1 = stops[i];
        if (t > s1.offset) continue;
        const ColorStop& s0 = stops[i - 1];
        const float span = s1.offset - s0.offset;
        const float f = span > 0.0f ? (t - s0.offset) / span : 0.0f;
        r = static_cast<uint8_t>(s0.r + (s1.r - s0.r) * f);
        g = static_cast<uint8_t>(s0.g + (s1.g - s0.g) * f);
        b = static_cast<uint8_t>(s0.b + (s1.b - s0.b) * f);
        a = static_cast<uint8_t>(s0.a + (s1.a - s0.a) * f);
        return;
    }
    const ColorStop& s = stops.back();
    r = s.r; g = s.g; b = s.b; a = s.a;
}

} // namespace

//------------------------------------------------------------------------------

Matrix2D multiply(const Matrix2D& a, const Matrix2D& b) {
    Matrix2D m;
    m.e11 = a.e11 * b.e11 + a.e12 * b.e21;
    m.e12 = a.e11 * b.e12 + a.e12 * b.e22;
    m.e13 = a.e11 * b.e13 + a.e12 * b.e23 + a.e13;
    m.e21 = a.e21 * b.e11 + a.e22 * b.e21;
    m.e22 = a.e21 * b.e12 + a.e22 * b.e22;
    m.e23 = a.e21 * b.e13 + a.e22 * b.e23 + a.e23;
    return m;
}

//------------------------------------------------------------------------------

void RenderTarget::clear(uint32_t argb) {
    if (!valid()) return;
    for (int y = 0; y < height_; ++y) {
        uint32_t* dst = row(y);
        std::fill(dst, dst + width_, argb);
    }
}

void RenderTarget::fillRect(int x, int y, int w, int h, uint32_t argb) {
    if (!valid() || w <= 0 || h <= 0) return;
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(width_, x + w);
    const int y1 = std::min(height_, y + h);
    if (x0 >= x1 || y0 >= y1) return;

    const uint32_t sa = (argb >> 24) & 0xFF;
    const uint32_t sr = (argb >> 16) & 0xFF;
    const uint32_t sg = (argb >> 8) & 0xFF;
    const uint32_t sb = argb & 0xFF;
    if (sa == 0) return;

    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* dst = row(yy);
        if (sa == 255) {
            std::fill(dst + x0, dst + x1, argb);
        } else {
            for (int xx = x0; xx < x1; ++xx) blendPixel(dst[xx], sr, sg, sb, sa);
        }
    }
}

void RenderTarget::fillRectF(float x, float y, float w, float h, uint32_t argb) {
    if (!valid() || w <= 0.0f || h <= 0.0f) return;
    const uint32_t sa = (argb >> 24) & 0xFF;
    if (sa == 0) return;
    const uint32_t sr = (argb >> 16) & 0xFF;
    const uint32_t sg = (argb >> 8) & 0xFF;
    const uint32_t sb = argb & 0xFF;

    const float fx0 = x, fy0 = y, fx1 = x + w, fy1 = y + h;
    const int ix0 = std::max(0, static_cast<int>(std::floor(fx0)));
    const int iy0 = std::max(0, static_cast<int>(std::floor(fy0)));
    const int ix1 = std::min(width_, static_cast<int>(std::ceil(fx1)));
    const int iy1 = std::min(height_, static_cast<int>(std::ceil(fy1)));
    if (ix0 >= ix1 || iy0 >= iy1) return;

    for (int py = iy0; py < iy1; ++py) {
        // 縦方向の被覆率
        const float top = std::max(fy0, static_cast<float>(py));
        const float bottom = std::min(fy1, static_cast<float>(py + 1));
        const float covY = bottom - top;
        if (covY <= 0.0f) continue;
        uint32_t* dst = row(py);
        for (int px = ix0; px < ix1; ++px) {
            const float left = std::max(fx0, static_cast<float>(px));
            const float right = std::min(fx1, static_cast<float>(px + 1));
            const float covX = right - left;
            if (covX <= 0.0f) continue;
            const uint32_t alpha = static_cast<uint32_t>(sa * covX * covY + 0.5f);
            blendPixel(dst[px], sr, sg, sb, alpha);
        }
    }
}

void RenderTarget::blendMask(const uint8_t* mask, int maskW, int maskH, int maskPitch,
                             int originX, int originY,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!valid() || !mask || maskW <= 0 || maskH <= 0 || a == 0) return;

    const int srcY0 = std::max(0, -originY);
    const int srcY1 = std::min(maskH, height_ - originY);
    const int srcX0 = std::max(0, -originX);
    const int srcX1 = std::min(maskW, width_ - originX);
    if (srcX0 >= srcX1 || srcY0 >= srcY1) return;

    for (int my = srcY0; my < srcY1; ++my) {
        const uint8_t* src = mask + static_cast<ptrdiff_t>(maskPitch) * my;
        uint32_t* dst = row(originY + my);
        for (int mx = srcX0; mx < srcX1; ++mx) {
            const uint32_t cov = src[mx];
            if (!cov) continue;
            const uint32_t sa = (cov * a + 127) / 255;
            blendPixel(dst[originX + mx], r, g, b, sa);
        }
    }
}

void RenderTarget::blendMask(const uint8_t* mask, int maskW, int maskH, int maskPitch,
                             int originX, int originY, const Paint& paint) {
    if (paint.type == PaintType::Solid || paint.stops.empty()) {
        blendMask(mask, maskW, maskH, maskPitch, originX, originY,
                  paint.r, paint.g, paint.b, paint.a);
        return;
    }
    if (!valid() || !mask || maskW <= 0 || maskH <= 0) return;

    const int srcY0 = std::max(0, -originY);
    const int srcY1 = std::min(maskH, height_ - originY);
    const int srcX0 = std::max(0, -originX);
    const int srcX1 = std::min(maskW, width_ - originX);
    if (srcX0 >= srcX1 || srcY0 >= srcY1) return;

    // 線形: 軸への射影を [0,1] に正規化
    const float dx = paint.x1 - paint.x0;
    const float dy = paint.y1 - paint.y0;
    const float lenSq = dx * dx + dy * dy;

    for (int my = srcY0; my < srcY1; ++my) {
        const uint8_t* src = mask + static_cast<ptrdiff_t>(maskPitch) * my;
        uint32_t* dst = row(originY + my);
        const float py = static_cast<float>(originY + my) + 0.5f;
        for (int mx = srcX0; mx < srcX1; ++mx) {
            const uint32_t cov = src[mx];
            if (!cov) continue;
            const float px = static_cast<float>(originX + mx) + 0.5f;

            float t = 0.0f;
            if (paint.type == PaintType::LinearGradient) {
                t = lenSq > 0.0f ? ((px - paint.x0) * dx + (py - paint.y0) * dy) / lenSq : 0.0f;
            } else {
                // 放射: 中心からの距離を r0..r1 で正規化（焦点は近似で中心扱い）
                const float ddx = px - paint.x1;
                const float ddy = py - paint.y1;
                const float dist = std::sqrt(ddx * ddx + ddy * ddy);
                const float span = paint.r1 - paint.r0;
                t = span > 0.0f ? (dist - paint.r0) / span : 0.0f;
            }
            t = std::clamp(t, 0.0f, 1.0f);

            uint8_t sr, sg, sb, sa;
            sampleStops(paint.stops, t, sr, sg, sb, sa);
            const uint32_t alpha = (cov * sa + 127) / 255;
            blendPixel(dst[originX + mx], sr, sg, sb, alpha);
        }
    }
}

void RenderTarget::blendImage(const uint8_t* rgba, int imgW, int imgH,
                              const Matrix2D& transform, uint8_t alpha) {
    if (!valid() || !rgba || imgW <= 0 || imgH <= 0 || alpha == 0) return;

    // 画像の 4 隅を変換して描画範囲を求める
    const float cx[4] = {0.0f, static_cast<float>(imgW), static_cast<float>(imgW), 0.0f};
    const float cy[4] = {0.0f, 0.0f, static_cast<float>(imgH), static_cast<float>(imgH)};
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (int i = 0; i < 4; ++i) {
        const float x = transform.e11 * cx[i] + transform.e12 * cy[i] + transform.e13;
        const float y = transform.e21 * cx[i] + transform.e22 * cy[i] + transform.e23;
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
    const int x1 = std::min(width_, static_cast<int>(std::ceil(maxX)) + 1);
    const int y1 = std::min(height_, static_cast<int>(std::ceil(maxY)) + 1);
    if (x0 >= x1 || y0 >= y1) return;

    // 逆変換（描画先 → 画像座標）
    const float det = transform.e11 * transform.e22 - transform.e12 * transform.e21;
    if (std::fabs(det) < 1e-9f) return;
    const float inv = 1.0f / det;
    const float i11 = transform.e22 * inv;
    const float i12 = -transform.e12 * inv;
    const float i21 = -transform.e21 * inv;
    const float i22 = transform.e11 * inv;
    const float i13 = -(i11 * transform.e13 + i12 * transform.e23);
    const float i23 = -(i21 * transform.e13 + i22 * transform.e23);

    for (int y = y0; y < y1; ++y) {
        uint32_t* dst = row(y);
        const float py = static_cast<float>(y) + 0.5f;
        for (int x = x0; x < x1; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            // 画像座標（ピクセル中心基準に -0.5）
            const float u = i11 * px + i12 * py + i13 - 0.5f;
            const float v = i21 * px + i22 * py + i23 - 0.5f;
            if (u < -1.0f || v < -1.0f || u > imgW || v > imgH) continue;

            const int u0 = static_cast<int>(std::floor(u));
            const int v0 = static_cast<int>(std::floor(v));
            const float fu = u - u0;
            const float fv = v - v0;

            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int dy = 0; dy < 2; ++dy) {
                const int sy = std::clamp(v0 + dy, 0, imgH - 1);
                const float wy = dy ? fv : (1.0f - fv);
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = std::clamp(u0 + dx, 0, imgW - 1);
                    const float w = wy * (dx ? fu : (1.0f - fu));
                    if (w <= 0.0f) continue;
                    const uint8_t* s = rgba + (static_cast<ptrdiff_t>(sy) * imgW + sx) * 4;
                    // アルファ加重（前乗算してから補間し、後で戻す）
                    const float sa = s[3] / 255.0f;
                    acc[0] += s[0] * sa * w;
                    acc[1] += s[1] * sa * w;
                    acc[2] += s[2] * sa * w;
                    acc[3] += s[3] * w;
                }
            }

            const uint32_t sa = static_cast<uint32_t>(std::clamp(acc[3], 0.0f, 255.0f));
            if (!sa) continue;
            const float unpre = 255.0f / std::max(1.0f, acc[3]);
            const uint32_t sr = static_cast<uint32_t>(std::clamp(acc[0] * unpre, 0.0f, 255.0f));
            const uint32_t sg = static_cast<uint32_t>(std::clamp(acc[1] * unpre, 0.0f, 255.0f));
            const uint32_t sb = static_cast<uint32_t>(std::clamp(acc[2] * unpre, 0.0f, 255.0f));
            blendPixel(dst[x], sr, sg, sb, (sa * alpha + 127) / 255);
        }
    }
}

} // namespace richtext
