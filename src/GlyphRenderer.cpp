/**
 * GlyphRenderer.cpp
 * 
 * グリフ単位の描画処理
 */

#include "richtext/GlyphRenderer.hpp"

#include <algorithm>

namespace richtext {

//------------------------------------------------------------------------------
// コンストラクタ・デストラクタ
//------------------------------------------------------------------------------

GlyphRenderer::GlyphRenderer(tvg::Canvas* canvas)
    : canvas_(canvas)
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
    if (!canvas_ || !glyph.font) {
        return;
    }

    // グリフ位置
    float glyphX = x + glyph.x;
    float glyphY = y + glyph.y;

    // FontFace からグリフパスまたはビットマップを取得
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
        // ビットマップ描画
        // 絵文字の下端をベースラインに合わせる
        if (useCache_) {
            auto key = makeKey(font, glyph.glyphId, style.fontSize);
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
    } else {
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

        // ベクターパス描画
        if (useCache_) {
            auto key = makeKey(font, glyph.glyphId, style.fontSize, style.fontWidth);
            auto it = pathCache_.find(key);
            if (it == pathCache_.end()) {
                CachedPath cached;
                if (font->getGlyphPath(glyph.glyphId, style.fontSize, cached.commands, cached.points)) {
                    size_t byteSize = cached.commands.size() * sizeof(tvg::PathCommand)
                                    + cached.points.size() * sizeof(tvg::Point);
                    pathCache_.emplace(key, std::move(cached));
                    cacheUsedBytes_ += byteSize;
                    evictCacheIfNeeded();
                    it = pathCache_.find(key);
                }
            }
            if (it != pathCache_.end()) {
                renderPath(it->second.commands, it->second.points,
                           glyphX, glyphY, appearance, skewX, fakeBoldStroke, fakeScaleX);
            }
        } else {
            std::vector<tvg::PathCommand> commands;
            std::vector<tvg::Point> points;
            if (font->getGlyphPath(glyph.glyphId, style.fontSize, commands, points)) {
                renderPath(commands, points,
                           glyphX, glyphY, appearance, skewX, fakeBoldStroke, fakeScaleX);
            }
        }
    }
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
    pathCache_.clear();
    bitmapCache_.clear();
    cacheUsedBytes_ = 0;
}

GlyphRenderer::GlyphCacheKey GlyphRenderer::makeKey(const FontFace* font,
                                                      uint32_t glyphId,
                                                      float fontSize,
                                                      float fontWidth) const {
    GlyphCacheKey key;
    key.fontPtr = reinterpret_cast<uintptr_t>(font);
    key.glyphId = glyphId;
    key.fontSizeQ = static_cast<uint32_t>(fontSize * 64.0f + 0.5f);
    key.fontWidthQ = static_cast<uint32_t>(fontWidth * 64.0f + 0.5f);
    return key;
}

void GlyphRenderer::evictCacheIfNeeded() {
    if (cacheMaxBytes_ == 0) return;
    if (cacheUsedBytes_ <= cacheMaxBytes_) return;
    // 超過したら全クリア（シンプルな実装）
    clearCache();
}

//------------------------------------------------------------------------------
// 内部実装
//------------------------------------------------------------------------------

void GlyphRenderer::renderPath(const std::vector<tvg::PathCommand>& commands,
                               const std::vector<tvg::Point>& points,
                               float x, float y,
                               const Appearance& appearance,
                               float skewX,
                               float fakeBoldStroke,
                               float scaleX) {
    if (commands.empty() || !canvas_) {
        return;
    }

    // 各 DrawStyle に対して描画
    for (const auto& drawStyle : appearance.getStyles()) {
        tvg::Shape* shape = tvg::Shape::gen();
        if (!shape) continue;

        // パスを設定（コピーして座標をオフセット + シアー/スケール変換）
        std::vector<tvg::Point> offsetPoints = points;
        for (auto& pt : offsetPoints) {
            // フェイク幅: X方向にスケール
            if (scaleX != 1.0f) {
                pt.x *= scaleX;
            }
            // フェイクイタリック: X方向にシアー変換（ベースライン基準）
            // pt.y はベースライン(0)からの相対座標で、上方向が負
            if (skewX != 0.0f) {
                pt.x += skewX * pt.y;
            }
            pt.x += x + drawStyle.offsetX;
            pt.y += y + drawStyle.offsetY;
        }

        shape->appendPath(commands.data(), static_cast<uint32_t>(commands.size()),
                         offsetPoints.data(), static_cast<uint32_t>(offsetPoints.size()));

        if (drawStyle.type == DrawStyle::Type::Fill) {
            // Fill
            shape->fill(drawStyle.r, drawStyle.g, drawStyle.b, drawStyle.a);
            if (fakeBoldStroke > 0.0f) {
                // フェイクボールド: 同色ストロークで線を太くする
                shape->strokeWidth(fakeBoldStroke);
                shape->strokeFill(drawStyle.r, drawStyle.g, drawStyle.b, drawStyle.a);
                shape->strokeJoin(tvg::StrokeJoin::Round);
            } else {
                shape->strokeWidth(0);
            }
        } else {
            // Stroke
            shape->fill(0, 0, 0, 0);  // 透明なフィル
            float totalStroke = drawStyle.strokeWidth;
            if (fakeBoldStroke > 0.0f) {
                totalStroke += fakeBoldStroke;
            }
            shape->strokeWidth(totalStroke);
            shape->strokeFill(drawStyle.r, drawStyle.g, drawStyle.b, drawStyle.a);
            shape->strokeCap(drawStyle.strokeCap);
            shape->strokeJoin(drawStyle.strokeJoin);
            shape->strokeMiterlimit(drawStyle.miterLimit);
        }

        canvas_->add(shape);
    }
}

void GlyphRenderer::renderBitmap(const GlyphBitmap& bitmap,
                                 float x, float y,
                                 float scale) {
    if (bitmap.data.empty() || !canvas_) {
        return;
    }
    
    // thorvg Picture を使用してビットマップを描画
    // ~Picture は protected のため tvg::Paint::rel() でリソース解放する
    struct TvgPictureDeleter {
        void operator()(tvg::Picture* p) const { tvg::Paint::rel(p); }
    };
    auto picture = std::unique_ptr<tvg::Picture, TvgPictureDeleter>(tvg::Picture::gen());
    if (!picture) return;

    // RGBA データを ARGB (32bit) に変換
    // bitmap.data は RGBA8888 形式
    size_t pixelCount = static_cast<size_t>(bitmap.width) * bitmap.height;
    std::vector<uint32_t> argbData(pixelCount);

    for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t r = bitmap.data[i * 4 + 0];
        uint8_t g = bitmap.data[i * 4 + 1];
        uint8_t b = bitmap.data[i * 4 + 2];
        uint8_t a = bitmap.data[i * 4 + 3];
        // ARGB8888 format
        argbData[i] = (static_cast<uint32_t>(a) << 24) |
                      (static_cast<uint32_t>(r) << 16) |
                      (static_cast<uint32_t>(g) << 8) |
                      static_cast<uint32_t>(b);
    }

    tvg::Result result = picture->load(
        argbData.data(),
        static_cast<uint32_t>(bitmap.width),
        static_cast<uint32_t>(bitmap.height),
        tvg::ColorSpace::ARGB8888,
        true  // copy
    );

    if (result != tvg::Result::Success) {
        // unique_ptr がスコープを抜けると自動的に delete される
        return;
    }

    // スケールと位置を設定
    if (scale != 1.0f) {
        picture->scale(scale);
    }
    picture->translate(x, y);

    // canvas に追加（所有権を移譲）
    canvas_->add(picture.release());
}

} // namespace richtext
