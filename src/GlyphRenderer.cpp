/**
 * GlyphRenderer.cpp
 *
 * グリフ単位の描画処理
 *
 * ベクターの塗り／縁取りはフォントバックエンドにカバレッジマスクを作らせ
 * （glyphware = FreeType のラスタライザ）、それを RenderTarget へ合成する。
 * 変形はマスク生成時にアウトラインへ焼き込むので、拡大縮小や斜体で品質が
 * 落ちることはない。
 */

#include "richtext/GlyphRenderer.hpp"

#include <algorithm>
#include <cmath>

namespace richtext {

namespace {

/// サブピクセル位相（1/4 px）に量子化した値と、その残差を返す
inline uint8_t quantizePhase(float v, float& floorOut) {
    const float f = std::floor(v);
    floorOut = f;
    int phase = static_cast<int>((v - f) * 4.0f + 0.5f);
    if (phase > 3) phase = 3;
    if (phase < 0) phase = 0;
    return static_cast<uint8_t>(phase);
}

inline int32_t quantizeMat(float v) {
    return static_cast<int32_t>(std::lround(v * 4096.0f));
}

} // namespace

//------------------------------------------------------------------------------
// コンストラクタ・デストラクタ
//------------------------------------------------------------------------------

GlyphRenderer::GlyphRenderer(const RenderTarget& target)
    : target_(target)
{
}

GlyphRenderer::~GlyphRenderer() = default;

//------------------------------------------------------------------------------
// グリフ描画
//------------------------------------------------------------------------------

void GlyphRenderer::renderGlyph(const GlyphInfo& glyph,
                                float x, float y,
                                const TextStyle& style,
                                const Appearance& appearance) {
    if (!target_.valid() || !glyph.font) {
        return;
    }

    // グリフ位置
    float glyphX = x + glyph.x;
    float glyphY = y + glyph.y;

    const FontFace* font = glyph.font;

    // フォント幅の処理: wdth 軸 + フェイク水平スケール
    float fakeScaleX = 1.0f;
    if (style.fontWidth != 100.0f) {
        float minW, maxW;
        if (font->getWidthAxisRange(minW, maxW)) {
            // wdth 軸あり: 軸範囲にクランプして適用
            float clampedWidth = std::clamp(style.fontWidth, minW, maxW);
            font->applyWidth(clampedWidth);
            // 軸でカバーできない残り分をフェイクスケールで補う
            fakeScaleX = style.fontWidth / clampedWidth;
        } else {
            // wdth 軸なし: 全てフェイクスケール
            fakeScaleX = style.fontWidth / 100.0f;
        }
    }

    // カラー絵文字判定
    if (font->isColorGlyph(glyph.glyphId)) {
        // ビットマップ描画（絵文字の下端をベースラインに合わせる）
        if (useCache_) {
            BitmapCacheKey key{reinterpret_cast<uintptr_t>(font), glyph.glyphId,
                               static_cast<uint32_t>(style.fontSize * 64.0f + 0.5f)};
            auto it = bitmapCache_.find(key);
            if (it == bitmapCache_.end()) {
                GlyphBitmap bitmap;
                if (font->getGlyphBitmap(glyph.glyphId, style.fontSize, bitmap)) {
                    size_t byteSize = bitmap.data.size();
                    bitmapCache_.emplace(key, std::move(bitmap));
                    cacheUsedBytes_ += byteSize;
                    evictCacheIfNeeded();
                    it = bitmapCache_.find(key);
                }
            }
            if (it != bitmapCache_.end()) {
                const GlyphBitmap& bitmap = it->second;
                float scale = (bitmap.strikeHeight > 0) ? (style.fontSize / bitmap.strikeHeight) : 1.0f;
                renderBitmap(bitmap, glyphX + bitmap.bearingX * scale,
                             glyphY - bitmap.bearingY * scale, scale);
            }
        } else {
            GlyphBitmap bitmap;
            if (font->getGlyphBitmap(glyph.glyphId, style.fontSize, bitmap)) {
                float scale = (bitmap.strikeHeight > 0) ? (style.fontSize / bitmap.strikeHeight) : 1.0f;
                renderBitmap(bitmap, glyphX + bitmap.bearingX * scale,
                             glyphY - bitmap.bearingY * scale, scale);
            }
        }
        return;
    }

    // フェイクイタリック: シアー係数（Android と同じ -0.25 ≒ 14度）
    float skewX = 0.0f;
    minikin::FontFakery fakery = glyph.fakery;
    if (fakery.isFakeItalic()) {
        skewX = -0.25f;
    }

    // フェイクボールド: フォントサイズの 1/24 のストローク幅で太字をシミュレート
    float fakeBoldStroke = 0.0f;
    if (fakery.isFakeBold()) {
        fakeBoldStroke = style.fontSize / 24.0f;
    }

    // グリフ固有の変形（ベースライン原点・y-down）
    //   フェイク幅は X スケール、フェイクイタリックは X シアー
    Matrix2D glyphMat;
    glyphMat.e11 = fakeScaleX;
    glyphMat.e12 = skewX;   // y-down 座標では pt.x += skewX * pt.y
    glyphMat.e21 = 0.0f;
    glyphMat.e22 = 1.0f;

    // 各 DrawStyle を描画
    for (const auto& drawStyle : appearance.getStyles()) {
        const float ox = glyphX + drawStyle.offsetX;
        const float oy = glyphY + drawStyle.offsetY;

        if (drawStyle.type == DrawStyle::Type::Fill) {
            // 塗り（フェイクボールドは同色の縁取りを重ねて太らせる）
            blendGlyph(font, glyph.glyphId, ox, oy, glyphMat, 0.0f, nullptr,
                       false, false, style.fontSize, style.fontWidth,
                       drawStyle.r, drawStyle.g, drawStyle.b, drawStyle.a);
            if (fakeBoldStroke > 0.0f) {
                blendGlyph(font, glyph.glyphId, ox, oy, glyphMat, fakeBoldStroke, nullptr,
                           false, false, style.fontSize, style.fontWidth,
                           drawStyle.r, drawStyle.g, drawStyle.b, drawStyle.a);
            }
        } else {
            // 縁取り
            float totalStroke = drawStyle.strokeWidth;
            if (fakeBoldStroke > 0.0f) totalStroke += fakeBoldStroke;
            blendGlyph(font, glyph.glyphId, ox, oy, glyphMat, totalStroke, &drawStyle,
                       false, false, style.fontSize, style.fontWidth,
                       drawStyle.r, drawStyle.g, drawStyle.b, drawStyle.a);
        }
    }
}

void GlyphRenderer::blendGlyph(const FontFace* font, uint32_t glyphId,
                               float penX, float penY,
                               const Matrix2D& glyphMat,
                               float strokeWidth,
                               const DrawStyle* strokeStyle,
                               bool fakeBold, bool fakeItalic,
                               float fontSize, float fontWidth,
                               uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const auto& face = font->getBackendFace();
    if (!face || a == 0) return;

    float upem = font->getFaceMetrics().unitsPerEm;
    if (upem <= 0) upem = 1000.0f;
    const float scale = fontSize / upem;

    // フォントユニット(y-up) → 描画先(y-down) の変換を組み立てる
    //   1) スケール（フォントユニット → ピクセル、Y 反転）
    //   2) グリフ固有の変形（フェイク幅・斜体）
    //   3) ペン位置への平行移動
    //   4) flipY / ユーザ変換
    Matrix2D m;
    m.e11 = scale;
    m.e12 = 0.0f;
    m.e13 = 0.0f;
    m.e21 = 0.0f;
    m.e22 = -scale;     // y-up → y-down
    m.e23 = 0.0f;

    m = multiply(glyphMat, m);
    Matrix2D place;
    place.e13 = penX;
    place.e23 = penY;
    m = multiply(place, m);

    if (flipYMatrix_) m = multiply(*flipYMatrix_, m);
    if (transform_) m = multiply(*transform_, m);

    // バックエンドは y-up で受け取るので、y 行を反転して渡す
    // （返るマスクは (left, -top) を左上として y-down に並ぶ）
    FontRenderParams params;
    params.transform.xx = m.e11;
    params.transform.xy = m.e12;
    params.transform.dx = m.e13;
    params.transform.yx = -m.e21;
    params.transform.yy = -m.e22;
    params.transform.dy = -m.e23;
    params.strokeWidth = strokeWidth;
    params.bold = fakeBold;
    params.italic = fakeItalic;
    if (strokeStyle) {
        params.join = strokeStyle->strokeJoin == StrokeJoin::Miter ? FontStrokeJoin::Miter
                    : strokeStyle->strokeJoin == StrokeJoin::Bevel ? FontStrokeJoin::Bevel
                                                                   : FontStrokeJoin::Round;
        params.cap = strokeStyle->strokeCap == StrokeCap::Butt   ? FontStrokeCap::Butt
                   : strokeStyle->strokeCap == StrokeCap::Square ? FontStrokeCap::Square
                                                                 : FontStrokeCap::Round;
        params.miterLimit = strokeStyle->miterLimit;
    }

    if (!useCache_) {
        FontGlyphMask mask;
        if (!face->getGlyphMask(glyphId, params, mask)) return;
        target_.blendMask(mask.buffer, mask.width, mask.rows, mask.pitch,
                          mask.left, -mask.top, r, g, b, a);
        return;
    }

    // キャッシュ: 平行移動の整数部だけを外に出し、位相は 1/4 px に量子化する
    float baseX = 0.0f, baseY = 0.0f;
    const uint8_t phaseX = quantizePhase(params.transform.dx, baseX);
    const uint8_t phaseY = quantizePhase(params.transform.dy, baseY);

    GlyphCacheKey key{};
    key.fontPtr = reinterpret_cast<uintptr_t>(font);
    key.glyphId = glyphId;
    key.fontSizeQ = static_cast<uint32_t>(fontSize * 64.0f + 0.5f);
    key.fontWidthQ = static_cast<uint32_t>(fontWidth * 64.0f + 0.5f);
    key.strokeQ = static_cast<uint32_t>(strokeWidth * 64.0f + 0.5f);
    key.xxQ = quantizeMat(params.transform.xx);
    key.xyQ = quantizeMat(params.transform.xy);
    key.yxQ = quantizeMat(params.transform.yx);
    key.yyQ = quantizeMat(params.transform.yy);
    key.phaseX = phaseX;
    key.phaseY = phaseY;
    key.flags = static_cast<uint8_t>((fakeBold ? 1 : 0) | (fakeItalic ? 2 : 0));

    auto it = maskCache_.find(key);
    if (it == maskCache_.end()) {
        // 量子化した位相でラスタライズし、整数部はブリット時に足す
        FontRenderParams cacheParams = params;
        cacheParams.transform.dx = phaseX * 0.25f;
        cacheParams.transform.dy = phaseY * 0.25f;

        FontGlyphMask mask;
        if (!face->getGlyphMask(glyphId, cacheParams, mask)) return;

        CachedMask cached;
        cached.left = mask.left;
        cached.top = mask.top;
        cached.width = mask.width;
        cached.rows = mask.rows;
        cached.coverage.resize(static_cast<size_t>(mask.width) * mask.rows);
        for (int row = 0; row < mask.rows; ++row) {
            std::memcpy(cached.coverage.data() + static_cast<size_t>(row) * mask.width,
                        mask.buffer + static_cast<ptrdiff_t>(mask.pitch) * row,
                        static_cast<size_t>(mask.width));
        }
        const size_t bytes = cached.coverage.size();
        maskCache_.emplace(key, std::move(cached));
        cacheUsedBytes_ += bytes;
        evictCacheIfNeeded();
        it = maskCache_.find(key);
        if (it == maskCache_.end()) return;
    }

    const CachedMask& cached = it->second;
    if (cached.width <= 0 || cached.rows <= 0) return;
    // マスクは位相だけで焼いてあるので、平行移動の整数部をここで足す。
    // top はバックエンドの y-up 基準なので、描画先（y-down）では符号が反転する。
    const int originX = static_cast<int>(baseX) + cached.left;
    const int originY = -(static_cast<int>(baseY) + cached.top);
    target_.blendMask(cached.coverage.data(), cached.width, cached.rows, cached.width,
                      originX, originY, r, g, b, a);
}

void GlyphRenderer::renderLayout(const TextLayout& layout,
                                 float x, float y,
                                 const Appearance& appearance) {
    const TextStyle& style = layout.getStyle();

    for (const auto& glyph : layout.getGlyphs()) {
        renderGlyph(glyph, x, y, style, appearance);
    }
}

//------------------------------------------------------------------------------
// キャッシュ制御
//------------------------------------------------------------------------------

void GlyphRenderer::clearCache() {
    maskCache_.clear();
    bitmapCache_.clear();
    cacheUsedBytes_ = 0;
}

void GlyphRenderer::evictCacheIfNeeded() {
    if (cacheMaxBytes_ == 0 || cacheUsedBytes_ <= cacheMaxBytes_) {
        return;
    }
    // 単純化のため全クリア（従来実装と同じ挙動）
    clearCache();
}

//------------------------------------------------------------------------------
// ビットマップ描画（カラー絵文字）
//------------------------------------------------------------------------------

void GlyphRenderer::renderBitmap(const GlyphBitmap& bitmap,
                                 float x, float y,
                                 float scale) {
    if (bitmap.data.empty() || !target_.valid()) {
        return;
    }

    // 画像ピクセル → 描画先の変換（配置 + スケール、その後 flipY / ユーザ変換）
    Matrix2D m;
    m.e11 = scale;
    m.e22 = scale;
    m.e13 = x;
    m.e23 = y;
    if (flipYMatrix_) m = multiply(*flipYMatrix_, m);
    if (transform_) m = multiply(*transform_, m);

    target_.blendImage(bitmap.data.data(), bitmap.width, bitmap.height, m);
}

} // namespace richtext
