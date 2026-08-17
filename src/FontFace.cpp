/**
 * FontFace.cpp
 *
 * FontBackend（glyphware / ホスト注入フォントエンジン）と minikin の橋渡しを
 * 行うフォントフェイスクラス
 */

#include "richtext/FontFace.hpp"
#include "richtext/FontManager.hpp"
#include "richtext/Raster.hpp"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <functional>

// minikin types
#include <minikin/MinikinPaint.h>
#include <minikin/MinikinRect.h>
#include <minikin/MinikinExtent.h>

namespace richtext {

namespace {

// ユニークID生成
static int32_t sUniqueIdCounter = 0;

// バリアブルフォント軸タグ
constexpr uint32_t kWdthTag = 0x77647468;  // 'wdth'

} // anonymous namespace


// ----------------------------------------------------------------------------
// FontFace 実装
// ----------------------------------------------------------------------------

FontFace::FontFace(const std::string& name,
                   std::shared_ptr<FontBackendFace> face,
                   int index)
    : minikin::MinikinFont(sUniqueIdCounter++)
    , fontName_(name)
    , fontIndex_(index)
    , face_(std::move(face))
{
    if (!face_) {
        throw std::runtime_error("Null font backend face for: " + name);
    }

    familyName_ = face_->familyName();
    styleName_ = face_->styleName();
    faceMetrics_ = face_->faceMetrics();

    // バリアブルフォント軸
    const std::vector<FontVarCoord> axes = face_->getAxes();
    isVariable_ = !axes.empty();
    axes_.reserve(axes.size());
    for (const auto& axis : axes) {
        axes_.emplace_back(static_cast<minikin::AxisTag>(axis.tag), axis.value);
    }
    hasWdthAxis_ = face_->getAxisRange(kWdthTag, wdthMin_, wdthDefault_, wdthMax_);
}

FontFace::~FontFace() {
    releaseFace();
}

void FontFace::releaseFace() {
    face_.reset();
}

const void* FontFace::GetFontData() const {
    return face_ ? face_->fontData() : nullptr;
}

size_t FontFace::GetFontSize() const {
    return face_ ? face_->fontDataSize() : 0;
}

void FontFace::setVariations(const std::vector<minikin::FontVariation>& variations) {
    if (!face_ || !isVariable_) return;

    // axes_ を更新
    axes_ = variations;

    // 未指定の軸は既定値へ戻す（従来の FreeType 実装と同じ挙動）
    std::vector<FontVarCoord> coords = face_->getAxes();
    for (const auto& var : variations) {
        bool found = false;
        for (auto& c : coords) {
            if (c.tag == var.axisTag) {
                c.value = var.value;
                found = true;
                break;
            }
        }
        if (!found) {
            coords.push_back({static_cast<uint32_t>(var.axisTag), var.value});
        }
    }
    face_->setVariations(coords);
}

bool FontFace::getWidthAxisRange(float& minWidth, float& maxWidth) const {
    if (!hasWdthAxis_) return false;
    minWidth = wdthMin_;
    maxWidth = wdthMax_;
    return true;
}

void FontFace::applyWidth(float width) const {
    if (!face_ || !hasWdthAxis_) return;
    const float clamped = std::clamp(width, wdthMin_, wdthMax_);
    face_->setVariations({{kWdthTag, clamped}});
}

float FontFace::GetHorizontalAdvance(uint32_t glyphId,
                                     const minikin::MinikinPaint& paint,
                                     const minikin::FontFakery& /*fakery*/) const {
    if (!face_) return 0.0f;

    // fontWidth が 100% 以外の場合、wdth 軸を設定してからグリフをロード
    float fakeScaleX = 1.0f;
    if (paint.fontWidth != 100.0f) {
        if (hasWdthAxis_) {
            float clampedWidth = std::clamp(paint.fontWidth, wdthMin_, wdthMax_);
            applyWidth(clampedWidth);
            fakeScaleX = paint.fontWidth / clampedWidth;
        } else {
            fakeScaleX = paint.fontWidth / 100.0f;
        }
    }

    // レイアウト用なのでヒンティング無し（アドバンスが整数に丸まらないように）
    FontGlyphMetrics m;
    if (!face_->getGlyphMetrics(glyphId, paint.size, false, false, true, m)) {
        return 0.0f;
    }

    // 固定サイズのカラービットマップフォントのスケーリングはバックエンドが行う
    return m.advanceX * fakeScaleX;
}

void FontFace::GetBounds(minikin::MinikinRect* bounds, uint32_t glyphId,
                         const minikin::MinikinPaint& paint,
                         const minikin::FontFakery& /*fakery*/) const {
    if (!face_ || !bounds) return;

    FontGlyphMetrics m;
    if (!face_->getGlyphMetrics(glyphId, paint.size, false, false, true, m)) {
        bounds->mLeft = bounds->mTop = bounds->mRight = bounds->mBottom = 0;
        return;
    }

    bounds->mLeft = m.bearingX;
    bounds->mTop = m.bearingY;
    bounds->mRight = m.bearingX + m.width;
    bounds->mBottom = m.bearingY - m.height;
}

void FontFace::GetFontExtent(minikin::MinikinExtent* extent,
                             const minikin::MinikinPaint& paint,
                             const minikin::FontFakery& /*fakery*/) const {
    if (!face_ || !extent) return;

    float upem = faceMetrics_.unitsPerEm;
    if (upem <= 0) upem = 1000.0f;

    extent->ascent = -faceMetrics_.ascenderUnits * paint.size / upem;
    extent->descent = -faceMetrics_.descenderUnits * paint.size / upem;
}

bool FontFace::isColorGlyph(uint32_t /*glyphId*/) const {
    return face_ && face_->isColorFont();
}
bool FontFace::getGlyphBitmap(uint32_t glyphId, float size,
                              GlyphBitmap& bitmap) const {
    if (!face_) return false;

    const bool hasColor = face_->isColorFont();

    FontGlyphBitmapView view;
    if (!face_->getGlyphBitmap(glyphId, size, hasColor, false, false, view)) {
        // FreeType が合成できない COLRv1 はペイントグラフを自前走査する
        if (hasColor) {
            return renderCOLRv1Glyph(glyphId, size, bitmap);
        }
        return false;
    }

    bitmap.width = view.width;
    bitmap.height = view.rows;
    bitmap.bearingX = view.left;
    bitmap.bearingY = view.top;
    // バックエンドが要求サイズへスケール済みなので、描画側での再スケールは不要
    bitmap.strikeHeight = 0.0f;

    const size_t pixels = static_cast<size_t>(view.width) * view.rows;
    bitmap.data.resize(pixels * 4);

    if (view.format == FontBitmapFormat::BGRA) {
        // カラービットマップ (BGRA -> RGBA)
        for (int y = 0; y < view.rows; ++y) {
            const uint8_t* src = view.buffer + static_cast<ptrdiff_t>(view.pitch) * y;
            uint8_t* dst = bitmap.data.data() + static_cast<size_t>(y) * view.width * 4;
            for (int x = 0; x < view.width; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];  // R
                dst[x * 4 + 1] = src[x * 4 + 1];  // G
                dst[x * 4 + 2] = src[x * 4 + 0];  // B
                dst[x * 4 + 3] = src[x * 4 + 3];  // A
            }
        }
    } else {
        // グレースケール -> RGBA（白 + カバレッジ）
        for (int y = 0; y < view.rows; ++y) {
            const uint8_t* src = view.buffer + static_cast<ptrdiff_t>(view.pitch) * y;
            uint8_t* dst = bitmap.data.data() + static_cast<size_t>(y) * view.width * 4;
            for (int x = 0; x < view.width; ++x) {
                dst[x * 4 + 0] = 255;
                dst[x * 4 + 1] = 255;
                dst[x * 4 + 2] = 255;
                dst[x * 4 + 3] = src[x];
            }
        }
    }

    return true;
}


//==============================================================================
// COLR (v0/v1) カラーグリフのレンダリング
//
// ペイントグラフの走査はバックエンドが行い（FontBackendFace::getColorLayers）、
// ここは受け取ったレイヤーをカバレッジマスクとして描き、塗りを乗せて RGBA
// ビットマップに合成する。
//==============================================================================

bool FontFace::renderCOLRv1Glyph(uint32_t glyphId, float size,
                                 GlyphBitmap& bitmap) const {
    if (!face_) return false;

    std::vector<FontColorLayer> layers;
    FontColorGlyphBox box;
    if (!face_->getColorLayers(glyphId, size, layers, &box) || layers.empty()) {
        return false;
    }

    float upem = faceMetrics_.unitsPerEm;
    if (upem <= 0) upem = 1000.0f;

    // 描画範囲。ClipBox があればそれを使い、無ければフォントサイズから見積もる
    // （レイヤー座標は y-up ピクセル。ビットマップは y-down なので上下を入れ替える）
    int originX, originY, bmpW, bmpH;
    if (box.valid) {
        originX = static_cast<int>(std::floor(box.xMin));
        originY = static_cast<int>(std::floor(-box.yMax));
        bmpW = static_cast<int>(std::ceil(box.xMax)) - originX;
        bmpH = static_cast<int>(std::ceil(-box.yMin)) - originY;
    } else {
        bmpW = static_cast<int>(std::ceil(size * 1.2f));
        bmpH = static_cast<int>(std::ceil(size * 1.2f));
        originX = 0;
        originY = -static_cast<int>(std::ceil(faceMetrics_.ascenderUnits * size / upem));
    }

    if (bmpW <= 0 || bmpH <= 0) return false;
    if (bmpW > 1024 || bmpH > 1024) return false;   // 安全上限

    std::vector<uint32_t> buffer(static_cast<size_t>(bmpW) * bmpH, 0);
    RenderTarget target(buffer.data(), bmpW, bmpH, bmpW);

    for (const auto& layer : layers) {
        // レイヤー行列はビットマップ（y-down）側の空間で解釈する。バックエンドの
        // ラスタライザは y-up で受け取るので F*M*F（F = diag(1,-1)）に直し、
        // 原点 (originX, originY) 分を平行移動で寄せる。
        FontRenderParams params;
        params.transform.xx = layer.transform[0];
        params.transform.xy = -layer.transform[1];
        params.transform.dx = layer.transform[2] - static_cast<float>(originX);
        params.transform.yx = -layer.transform[3];
        params.transform.yy = layer.transform[4];
        params.transform.dy = -layer.transform[5] + static_cast<float>(originY);

        FontGlyphMask mask;
        if (!face_->getGlyphMask(layer.glyphId, params, mask)) continue;

        Paint paint;
        switch (layer.paint.kind) {
        case FontPaintKind::LinearGradient:
            paint.type = PaintType::LinearGradient;
            paint.x0 = layer.paint.x0 - originX;
            paint.y0 = -layer.paint.y0 - originY;
            paint.x1 = layer.paint.x1 - originX;
            paint.y1 = -layer.paint.y1 - originY;
            break;
        case FontPaintKind::RadialGradient:
            paint.type = PaintType::RadialGradient;
            paint.x0 = layer.paint.x0 - originX;
            paint.y0 = -layer.paint.y0 - originY;
            paint.r0 = layer.paint.r0;
            paint.x1 = layer.paint.x1 - originX;
            paint.y1 = -layer.paint.y1 - originY;
            paint.r1 = layer.paint.r1;
            break;
        case FontPaintKind::Solid:
        default:
            paint.type = PaintType::Solid;
            break;
        }
        paint.r = layer.paint.r;
        paint.g = layer.paint.g;
        paint.b = layer.paint.b;
        paint.a = layer.paint.a;
        for (const auto& s : layer.paint.stops) {
            paint.stops.push_back({s.offset, s.r, s.g, s.b, s.a});
        }

        target.blendMask(mask.buffer, mask.width, mask.rows, mask.pitch,
                         mask.left, -mask.top, paint);
    }

    // ARGB → RGBA
    bitmap.width = bmpW;
    bitmap.height = bmpH;
    bitmap.bearingX = originX;
    bitmap.bearingY = -originY;
    bitmap.strikeHeight = 0.0f;
    bitmap.data.resize(static_cast<size_t>(bmpW) * bmpH * 4);

    bool hasPixels = false;
    for (int i = 0; i < bmpW * bmpH; ++i) {
        const uint32_t argb = buffer[i];
        const uint8_t a = (argb >> 24) & 0xFF;
        bitmap.data[i * 4 + 0] = (argb >> 16) & 0xFF;
        bitmap.data[i * 4 + 1] = (argb >> 8) & 0xFF;
        bitmap.data[i * 4 + 2] = argb & 0xFF;
        bitmap.data[i * 4 + 3] = a;
        if (a > 0) hasPixels = true;
    }

    return hasPixels;
}

} // namespace richtext
